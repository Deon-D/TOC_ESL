#pragma once

#include <memory>

#include "memory_backend.hpp"

namespace esl::chi_vip {
struct SNConfig {
    NodeID node_id = 3;
    NodeID home_id = 1;
    std::uint64_t memory_size = 1 << 20;
    std::uint64_t service_latency = 5;
    std::uint64_t timeout_cycles = 10000;
    unsigned request_slots = 16;
    bool grant_before_retry = false;
    void Validate() const;
};

// GbCore SN-F subset: ReadNoSnp, WriteNoSnpFull/Ptl and protocol credits.
// HN returns data through itself: ReturnNID=SrcID, ReturnTxnID=TxnID.
class SN {
public:
    explicit SN(SNConfig config = {}, Observer observer = {});
    ~SN();
    SN(const SN&) = delete;
    SN& operator=(const SN&) = delete;

    const SNConfig& Config() const;
    ByteMemoryBackend& Memory();
    const ByteMemoryBackend& Memory() const;
    void OnEvent(Event event);
    void BeginCycle(std::uint64_t cycle);
    std::vector<SendAction> TakeActions();
    void Commit();
    bool IsIdle() const;
    std::string DescribeState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace esl::chi_vip
