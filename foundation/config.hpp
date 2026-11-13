#pragma once
#include "foundation/log.hpp"
namespace esl {
struct RequesterConfig {
    unsigned count = 2;
    unsigned cache_lines = 64;
    unsigned request_slots = 16;
    unsigned snoop_slots = 8;
    unsigned pending_operations = 64;
};
struct HomeConfig {
    unsigned request_slots = 16;
    bool grant_before_retry = false;
};
struct SlaveConfig {
    unsigned request_slots = 16;
    std::uint64_t service_latency = 5;
    bool grant_before_retry = false;
};
struct WorkloadConfig {
    std::uint64_t requests = 256;
    std::uint64_t seed = 1;
    std::uint64_t memory_size = 1048576;
    std::uint64_t start_address = 0;
    std::uint64_t issue_interval_cycles = 1;
    unsigned write_percent = 25;
    bool random_addresses = false;
};
struct SimConfig {
    std::uint64_t period_ps = 1000;
    std::uint64_t max_cycles = 1000000;
    std::uint64_t timeout_cycles = 10000;
    bool reverse_traversal = false;
    RequesterConfig rn;
    HomeConfig hn;
    SlaveConfig sn;
    WorkloadConfig workload;
    LogConfig log;
    std::string json_path = "result.json";
    std::string trace_path;
    // Startup only. Throws PlatformError on invalid fields, units, or output collisions.
    void Validate() const;
    std::string ToTOML() const;
};
// Defaults < TOML < explicit CLI; CLI precedence does not depend on argument position.
SimConfig LoadConfig(int argc, char** argv);
SimConfig ParseTOML(const std::string& text);
}  // namespace esl
