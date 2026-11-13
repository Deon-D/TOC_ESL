#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "chi_channel.h"

namespace esl {
struct CHIPortParams {
    CHIChannelParams req;
    CHIChannelParams rsp;
    CHIChannelParams snp;
    CHIChannelParams dat;
};

// Standalone synchronous endpoint. The owning module calls Work before its
// send/recv operations, then Xfer once per cycle. All modules finish Work before
// any module starts Xfer. Both endpoints must participate even when idle.
class CHIPort final {
public:
    explicit CHIPort(std::string name);
    ~CHIPort();
    CHIPort(const CHIPort&) = delete;
    CHIPort& operator=(const CHIPort&) = delete;

    // Startup-only, reciprocal non-owning binding; no kernel registration.
    void Connect(CHIPort& peer);
    CHIPort* Peer() const;
    void CheckBound() const;
    // Code-only setup before either connected endpoint has called Work.
    void SetParams(const CHIPortParams& params);
    CHIPortParams Params() const;
    template <class Flit>
    void SetParams(const CHIChannelParams& params) {
        CheckSetup();
        params.Validate();
        Channel<Flit>().SetParams(params);
    }
    template <class Flit>
    const CHIChannelParams& Params() const {
        return Channel<Flit>().Params();
    }

    // Between Work and Xfer. Success means local queue acceptance, not delivery.
    template <class Flit>
    bool Send(const Flit& flit) {
        return Channel<Flit>().Send(flit);
    }
    template <class Flit>
    bool Recv(Flit& flit) {
        return Channel<Flit>().Recv(flit);
    }
    template <class Flit>
    bool CanSend() const {
        return Channel<Flit>().CanSend();
    }
    template <class Flit>
    bool CanRecv() const {
        return Channel<Flit>().CanRecv();
    }
    template <class Flit>
    const Flit* Peek() const {
        return Channel<Flit>().Peek();
    }
    // After this port's Xfer, before its next Work; nullptr means no transfer.
    template <class Flit>
    const Flit* Transmitted() const {
        return Channel<Flit>().Transmitted();
    }
    template <class Flit>
    unsigned Credits() const {
        return Channel<Flit>().Credits();
    }
    template <class Flit>
    std::size_t TxSize() const {
        return Channel<Flit>().TxSize();
    }
    template <class Flit>
    std::size_t RxSize() const {
        return Channel<Flit>().RxSize();
    }
    template <class Flit>
    const ChannelStats& Stats() const {
        return Channel<Flit>().Stats();
    }

    void Work();
    void Xfer();
    bool IsIdle() const;
    std::string DescribeState() const;

private:
    void CheckSetup() const;
    template <class Flit>
    CHIChannel<Flit>& Channel() {
        if constexpr (std::is_same<Flit, CHIReqFlit>::value)
            return req_;
        else if constexpr (std::is_same<Flit, CHIRspFlit>::value)
            return rsp_;
        else if constexpr (std::is_same<Flit, CHISnpFlit>::value)
            return snp_;
        else {
            static_assert(std::is_same<Flit, CHIDatFlit>::value, "unsupported CHI flit type");
            return dat_;
        }
    }
    template <class Flit>
    const CHIChannel<Flit>& Channel() const {
        if constexpr (std::is_same<Flit, CHIReqFlit>::value)
            return req_;
        else if constexpr (std::is_same<Flit, CHIRspFlit>::value)
            return rsp_;
        else if constexpr (std::is_same<Flit, CHISnpFlit>::value)
            return snp_;
        else {
            static_assert(std::is_same<Flit, CHIDatFlit>::value, "unsupported CHI flit type");
            return dat_;
        }
    }
    const std::string name_;
    CHIPort* peer_ = nullptr;
    bool started_ = false;
    bool in_work_ = false;
    std::uint64_t cycle_ = 0;
    CHIChannel<CHIReqFlit> req_;
    CHIChannel<CHIRspFlit> rsp_;
    CHIChannel<CHISnpFlit> snp_;
    CHIChannel<CHIDatFlit> dat_;
    const std::array<CHIChannelBase*, 4> channels_;
};
}  // namespace esl
