#pragma once

#include <memory>
#include <set>

#include "types.hpp"

namespace esl::chi_vip {
struct HNConfig {
    NodeID node_id = 1;
    NodeID slave_id = 3;
    std::vector<NodeID> requester_ids{0, 2};
    std::uint64_t memory_size = 1 << 20;
    unsigned request_slots = 16;
    std::uint64_t timeout_cycles = 10000;
    bool grant_before_retry = false;
    void Validate() const;
};
// Permission directory, not a mirror of RN cache state: an owner can silently
// change clean data to dirty data. HN must snoop it to obtain the latest bytes.
struct DirectoryEntry {
    std::set<NodeID> sharers;
    std::optional<NodeID> owner;
};
class HN {
public:
    explicit HN(HNConfig config = {}, Observer observer = {});
    ~HN();
    HN(const HN&) = delete;
    HN& operator=(const HN&) = delete;
    const HNConfig& Config() const;
    void OnEvent(Event event);
    void BeginCycle(std::uint64_t cycle);
    std::vector<SendAction> TakeActions();
    void Commit();
    std::optional<DirectoryEntry> InspectDirectory(std::uint64_t address) const;
    bool IsIdle() const;
    std::string DescribeState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace esl::chi_vip
