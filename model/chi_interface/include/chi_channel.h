#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "chi_common.h"

namespace esl {
namespace chi_detail {
inline void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
inline void Require(bool condition, const std::string& name, const char* message) {
    if (!condition)
        throw std::runtime_error(name + ": " + message);
}
}  // namespace chi_detail

// Code-only parameters. Delays count calls to the owning port's Work/Xfer pair.
struct CHIChannelParams {
    unsigned tx_depth = 4;
    unsigned rx_depth = 4;
    std::uint64_t flit_delay_cycles = 1;
    std::uint64_t credit_delay_cycles = 1;

    void Validate() const {
        chi_detail::Require(tx_depth > 0, "CHI TX depth must be positive");
        chi_detail::Require(rx_depth >= 1 && rx_depth <= 15, "CHI RX depth must be 1..15");
        chi_detail::Require(flit_delay_cycles > 0 && credit_delay_cycles > 0,
                            "CHI delay must be positive");
    }
};

struct ChannelStats {
    std::uint64_t tx_flits = 0;      // Sent in local Xfer.
    std::uint64_t rx_flits = 0;      // Arrived in local Work, before Recv consumption.
    std::uint64_t send_rejects = 0;  // Send calls rejected by a full local TX FIFO.
    std::uint64_t credit_stalls = 0;
    std::uint64_t tx_peak = 0;
    std::uint64_t rx_peak = 0;
};

class CHIChannelBase {
public:
    virtual ~CHIChannelBase() = default;
    virtual void Work(std::uint64_t cycle) = 0;
    virtual void Xfer(std::uint64_t cycle) = 0;
    virtual bool IsIdle() const = 0;
    virtual std::string DescribeState() const = 0;
};

class CHIPort;

// One local endpoint. Only CHIPort binds/configures channels and supplies cycles.
// Payloads are owned by value; Xfer only appends to peer transit queues.
template <class Flit>
class CHIChannel final : public CHIChannelBase {
public:
    explicit CHIChannel(std::string name) : name_(std::move(name)) {}
    CHIChannel(const CHIChannel&) = delete;
    CHIChannel& operator=(const CHIChannel&) = delete;

    bool CanSend() const {
        CheckWork();
        return tx_.size() < params_.tx_depth;
    }
    bool Send(const Flit& flit) {
        if (!CanSend()) {
            ++stats_.send_rejects;
            return false;
        }
        tx_.push_back(flit);
        stats_.tx_peak = std::max<std::uint64_t>(stats_.tx_peak, tx_.size());
        return true;
    }
    bool CanRecv() const {
        CheckWork();
        return !rx_.empty();
    }
    bool Recv(Flit& flit) {
        if (!CanRecv())
            return false;
        flit = rx_.front();
        rx_.pop_front();
        ++returned_;
        CheckCredits();
        return true;
    }
    // Borrowed until Recv or the next Work. Empty returns nullptr.
    const Flit* Peek() const {
        return CanRecv() ? &rx_.front() : nullptr;
    }
    // Query after this port's Xfer, before its next Work.
    const Flit* Transmitted() const {
        chi_detail::Require(transferred_ && !in_work_, name_, "result requires completed Xfer");
        return sending_ ? &*sending_ : nullptr;
    }
    unsigned Credits() const {
        return credits_;
    }
    std::size_t TxSize() const {
        return tx_.size();
    }
    std::size_t RxSize() const {
        return rx_.size();
    }
    const ChannelStats& Stats() const {
        return stats_;
    }
    const CHIChannelParams& Params() const {
        return params_;
    }

    void Work(std::uint64_t cycle) override {
        chi_detail::Require(peer_ != nullptr && !in_work_, name_, "invalid Work");
        in_work_ = true;
        sending_.reset();
        while (!incoming_.empty() && incoming_.front().due <= cycle) {
            rx_.push_back(incoming_.front().flit);
            incoming_.pop_front();
            ++stats_.rx_flits;
            stats_.rx_peak = std::max<std::uint64_t>(stats_.rx_peak, rx_.size());
        }
        while (!incoming_credits_.empty() && incoming_credits_.front() <= cycle) {
            incoming_credits_.pop_front();
            ++pending_credits_;
        }
        CheckCredits();
    }
    void Xfer(std::uint64_t cycle) override {
        CheckWork();
        const bool send = !tx_.empty() && credits_ > 0;
        const bool grant_returned = returned_ > 0;
        const bool grant = grant_returned || unissued_ > 0;
        // Validate both arrival times before mutating either direction.
        const auto flit_due = send ? Due(cycle, params_.flit_delay_cycles) : 0;
        const auto credit_due = grant ? Due(cycle, params_.credit_delay_cycles) : 0;
        if (send) {
            sending_ = tx_.front();
            peer_->incoming_.push_back({flit_due, *sending_});
            tx_.pop_front();
            --credits_;
            ++stats_.tx_flits;
        } else if (!tx_.empty()) {
            ++stats_.credit_stalls;
        }
        if (grant) {
            peer_->incoming_credits_.push_back(credit_due);
            if (grant_returned)
                --returned_;
            else
                --unissued_;
        }
        // Arriving credits cannot fund a transmission in this same cycle.
        credits_ += pending_credits_;
        pending_credits_ = 0;
        in_work_ = false;
        transferred_ = true;
        CheckCredits();
        peer_->CheckCredits();
    }
    bool IsIdle() const override {
        return peer_ && tx_.empty() && rx_.empty() && incoming_.empty() &&
               incoming_credits_.empty() && pending_credits_ == 0 && returned_ == 0 &&
               unissued_ == 0 && credits_ == peer_->params_.rx_depth;
    }
    std::string DescribeState() const override {
        return name_ + " tx=" + std::to_string(tx_.size()) + " rx=" + std::to_string(rx_.size()) +
               " credits=" + std::to_string(credits_) +
               " incoming_flits=" + std::to_string(incoming_.size()) +
               " incoming_credits=" + std::to_string(incoming_credits_.size()) +
               " pending_credits=" + std::to_string(pending_credits_) +
               " returned=" + std::to_string(returned_) + " unissued=" + std::to_string(unissued_);
    }

private:
    friend class CHIPort;
    struct Arrival {
        std::uint64_t due;
        Flit flit;
    };
    void SetParams(const CHIChannelParams& params) {
        params_ = params;
        unissued_ = params.rx_depth;
    }
    void CheckWork() const {
        chi_detail::Require(peer_ != nullptr, name_, "unbound CHI channel");
        chi_detail::Require(in_work_, name_, "send/recv requires Work before Xfer");
    }
    std::uint64_t Due(std::uint64_t cycle, std::uint64_t delay) const {
        chi_detail::Require(delay <= std::numeric_limits<std::uint64_t>::max() - cycle, name_,
                            "cycle overflow");
        return cycle + delay;
    }
    void CheckCredits() const {
        chi_detail::Require(
            tx_.size() <= params_.tx_depth && rx_.size() + incoming_.size() <= params_.rx_depth,
            name_, "CHI FIFO overflow");
        chi_detail::Require(unissued_ + rx_.size() + incoming_.size() + returned_ +
                                    peer_->credits_ + peer_->pending_credits_ +
                                    peer_->incoming_credits_.size() ==
                                params_.rx_depth,
                            name_, "CHI credit conservation failure");
    }
    const std::string name_;
    CHIChannelParams params_;
    CHIChannel* peer_ = nullptr;
    std::deque<Flit> tx_;
    std::deque<Flit> rx_;
    std::deque<Arrival> incoming_;
    std::deque<std::uint64_t> incoming_credits_;
    unsigned credits_ = 0;
    unsigned pending_credits_ = 0;
    unsigned unissued_ = params_.rx_depth;
    unsigned returned_ = 0;
    bool in_work_ = false;
    bool transferred_ = false;
    std::optional<Flit> sending_;
    ChannelStats stats_;
};
}  // namespace esl
