#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "model/chi_vip/system/include/system.hpp"

namespace {
void Expect(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
esl::SimConfig Settings() {
    esl::SimConfig config;
    config.log.console = false;
    config.log.level = esl::LogLevel::kOff;
    config.log.directory.clear();
    config.workload.memory_size = 4096;
    config.workload.requests = 100;
    return config;
}
esl::chi_vip::RunResult Run(const esl::SimConfig& config, std::ostream* trace = nullptr) {
    esl::chi_vip::System system(config, trace);
    const auto result = system.Run();
    Expect(result.success, "runner failed: " + result.error + " " + result.diagnostics);
    Expect(result.completed == config.workload.requests, "completion count");
    Expect(result.loads + result.stores == result.completed, "operation accounting");
    Expect(result.cycles > 0 && result.cycles < config.max_cycles, "run did not terminate");
    Expect(result.last_time_ps == (result.cycles - 1) * config.period_ps, "physical time");
    Expect(result.req_flits > 0 && result.rsp_flits > 0 && result.dat_flits > 0,
           "runner did not traverse CHI channels");
    Expect(result.Throughput() > 0 && result.Throughput() <= 1, "throughput accounting");
    return result;
}
void ReadTest() {
    auto config = Settings();
    config.workload.write_percent = 0;
    config.workload.requests = 32;
    config.rn.count = 1;
    config.rn.cache_lines = 2;
    config.rn.request_slots = config.hn.request_slots = config.sn.request_slots = 1;
    config.sn.service_latency = 1;
    const auto fast = Run(config);
    Expect(fast.loads == 32 && fast.stores == 0, "read-only workload produced writes");
    config.sn.service_latency = 19;
    const auto slow = Run(config);
    Expect(slow.cycles > fast.cycles, "SN service latency did not affect execution");
    config.period_ps = 2500;
    const auto scaled = Run(config);
    Expect(scaled.cycles == slow.cycles && scaled.completed == slow.completed,
           "clock scaling changed cycle behavior");
}
void MixedTest() {
    for (unsigned seed = 0; seed != 6; ++seed) {
        auto config = Settings();
        config.rn.count = 1 + seed % 4;
        config.rn.cache_lines = 1 + seed % 3;
        config.rn.request_slots = 2;
        config.rn.snoop_slots = 1;
        config.rn.pending_operations = 4;
        config.hn.request_slots = 1 + seed % 2;
        config.sn.request_slots = 1;
        config.sn.service_latency = 1 + seed * 3;
        config.hn.grant_before_retry = seed % 2 != 0;
        config.sn.grant_before_retry = seed % 2 == 0;
        config.workload.seed = seed;
        config.workload.random_addresses = true;
        config.workload.memory_size = 1024;
        config.workload.requests = 192;
        config.workload.write_percent = 45;
        const auto result = Run(config);
        Expect(result.loads > 0 && result.stores > 0, "mixed workload missing operation class");
    }
    auto config = Settings();
    config.rn.count = 3;
    config.rn.cache_lines = 1;
    config.workload.memory_size = 256;
    config.workload.requests = 96;
    config.workload.write_percent = 100;
    const auto stores = Run(config);
    Expect(stores.loads == 0 && stores.stores == 96, "store-only workload accounting");
}
void DeterminismTest() {
    auto config = Settings();
    config.rn.cache_lines = 2;
    config.workload.memory_size = 512;
    config.workload.random_addresses = true;
    config.workload.write_percent = 40;
    std::ostringstream first_trace;
    std::ostringstream second_trace;
    std::ostringstream reverse_trace;
    const auto first = Run(config, &first_trace);
    const auto second = Run(config, &second_trace);
    Expect(first.JSON() == second.JSON() && first_trace.str() == second_trace.str(),
           "fixed seed is not reproducible");
    Expect(first_trace.str().find("vip.rn0") != std::string::npos &&
               first_trace.str().find("vip.hn") != std::string::npos &&
               first_trace.str().find("vip.sn") != std::string::npos,
           "trace omitted a CHI node");
    Expect(first_trace.str().find("axi.") == std::string::npos,
           "current system trace contains retired AXI bridge");
    config.reverse_traversal = true;
    auto reverse = Run(config, &reverse_trace);
    reverse.config.reverse_traversal = false;
    Expect(first.JSON() == reverse.JSON() && first_trace.str() == reverse_trace.str(),
           "reversing Work/Xfer traversal changed the result");
    config.reverse_traversal = false;
    config.log.level = esl::LogLevel::kDebug;
    config.log.directory = "vip_runner_logs";
    std::ostringstream logged_trace;
    auto logged = Run(config, &logged_trace);
    logged.config.log = first.config.log;
    Expect(first.JSON() == logged.JSON() && first_trace.str() == logged_trace.str(),
           "logging changed protocol execution");
}
void InvalidTest() {
    for (unsigned field = 0; field != 5; ++field) {
        auto config = Settings();
        if (field == 0)
            config.rn.count = 0;
        else if (field == 1)
            config.hn.request_slots = 0;
        else if (field == 2)
            config.sn.request_slots = 0;
        else if (field == 3)
            config.workload.write_percent = 101;
        else
            config.workload.start_address = 1;
        bool rejected = false;
        try {
            esl::chi_vip::System system(config);
            const auto result = system.Run();
            rejected = !result.success && !result.error.empty();
        } catch (const std::exception&) {
            rejected = true;
        }
        Expect(rejected, "invalid runner configuration was accepted");
    }
    auto config = Settings();
    config.max_cycles = 2;
    esl::chi_vip::System limited(config);
    const auto limit = limited.Run();
    Expect(!limit.success && !limit.error.empty() && !limit.diagnostics.empty(),
           "cycle limit failure omitted diagnostic state");
    Expect(limit.completed < config.workload.requests, "timeout fabricated completions");
    config.max_cycles = 10000;
    config.timeout_cycles = 3;
    config.sn.service_latency = 100;
    esl::chi_vip::System timed(config);
    const auto timeout = timed.Run();
    Expect(!timeout.success && !timeout.error.empty() && !timeout.diagnostics.empty(),
           "transaction timeout failure omitted diagnostic state");
}
}  // namespace
int main(int argc, char** argv) {
    try {
        Expect(argc == 2, "runner test case required");
        const std::string test(argv[1]);
        if (test == "runner_read")
            ReadTest();
        else if (test == "runner_mixed")
            MixedTest();
        else if (test == "runner_determinism")
            DeterminismTest();
        else if (test == "runner_invalid")
            InvalidTest();
        else
            throw std::runtime_error("unknown runner test case");
        std::cout << test << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
