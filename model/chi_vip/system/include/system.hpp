#pragma once

#include <memory>
#include <ostream>

#include "foundation/config.hpp"

namespace esl::chi_vip {
struct RunResult {
    SimConfig config;
    bool success = false;
    std::string error;
    std::string diagnostics;
    std::uint64_t completed = 0;
    std::uint64_t loads = 0;
    std::uint64_t stores = 0;
    std::uint64_t cycles = 0;
    std::uint64_t last_time_ps = 0;
    std::uint64_t req_flits = 0;
    std::uint64_t rsp_flits = 0;
    std::uint64_t snp_flits = 0;
    std::uint64_t dat_flits = 0;
    // Includes initialization/drain cycles in the simulated interval; maintenance
    // traffic is counted in flits but excluded from completed/loads/stores.
    double Throughput() const;
    std::string JSON() const;
};

// Main platform assembly: N RN-F + one HN-F + one SN-F, connected exclusively
// by CHIPort. The workload and byte oracle live outside all protocol nodes.
class System {
public:
    explicit System(const SimConfig& config, std::ostream* trace = nullptr);
    ~System();
    System(const System&) = delete;
    System& operator=(const System&) = delete;
    // Startup reset only. A system instance can run once. Protocol/timeout/data
    // failures return success=false with node diagnostics; invalid config throws.
    RunResult Run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace esl::chi_vip
