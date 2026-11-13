#pragma once

#include <memory>

#include "types.hpp"

namespace esl::chi_vip {
// Single-threaded protocol core. Submit/Load/Store and OnEvent enqueue input.
// BeginCycle processes input and ready work. TakeActions transfers immutable
// outputs to a transport, which must acknowledge acceptance and actual sending.
// Commit publishes cache views, completions and observations. One pair per cycle.
class RN {
public:
    explicit RN(RNConfig config = {}, Observer observer = {});
    ~RN();
    RN(const RN&) = delete;
    RN& operator=(const RN&) = delete;
    SubmitResult Submit(const TxnRequest& request);
    SubmitResult Load(std::uint64_t address, unsigned size);
    SubmitResult Store(std::uint64_t address, const Line& data, std::uint64_t byte_enable);
    void OnEvent(const Event& event);
    bool CanReceiveSnoop() const;
    void BeginCycle(std::uint64_t cycle);
    std::vector<SendAction> TakeActions();
    void Commit();
    bool PollCompletion(Completion& result);
    std::optional<CacheLine> InspectLine(std::uint64_t address) const;
    bool IsIdle() const;
    std::string DescribeState() const;
    const RNConfig& Config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace esl::chi_vip
