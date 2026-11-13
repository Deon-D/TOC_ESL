#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <type_traits>

#include "foundation/config.hpp"
#include "kernel/kernel.hpp"
using namespace esl;
namespace {
void Check(bool condition, const std::string& message) {
    Require(condition, "TEST: " + message);
}
template <class Function>
void Rejects(Function function, const std::string& fragment) {
    try {
        function();
    } catch (const std::exception& error) {
        Check(std::string(error.what()).find(fragment) != std::string::npos, error.what());
        return;
    }
    throw PlatformError("expected failure: " + fragment);
}
LogConfig Silent() {
    LogConfig config;
    config.console = false;
    config.level = LogLevel::kOff;
    config.directory.clear();
    return config;
}
SimConfig Settings() {
    SimConfig config;
    config.log = Silent();
    return config;
}
struct Fixture {
    Clock clock;
    Logger logger;
    std::ostringstream output;
    TraceSink trace;
    ModelContext context;
    SimKernel kernel;
    explicit Fixture(bool reverse = false)
        : clock(1000),
          logger(clock, Silent()),
          trace(clock, &output),
          context{clock, logger, trace},
          kernel(clock, logger, trace, reverse) {}
};
void ClockTest() {
    static_assert(!std::is_convertible<CycleCount, TimePs>::value);
    Check(ToTimePs({9}, 1250).value == 11250, "time conversion");
    Rejects([] { Clock clock(0); }, "positive");
    Rejects([] { ToTimePs({std::numeric_limits<std::uint64_t>::max()}, 2); }, "overflow");
    Rejects([] { AddCycles({std::numeric_limits<std::uint64_t>::max()}, 1); }, "overflow");
    for (const auto period : {1000ULL, 2500ULL}) {
        Clock clock(period);
        Logger logger(clock, Silent());
        TraceSink trace(clock, nullptr);
        SimKernel kernel(clock, logger, trace);
        std::vector<std::uint64_t> times;
        kernel.Schedule(3, 10, [&] {
            Check(clock.CurrentCycle().value == 3, "scheduled cycle");
            Check(clock.CurrentPhase() == Phase::kPrepare, "scheduled phase");
            times.push_back(clock.NowPs().value);
        });
        kernel.Run(6);
        Check(times == std::vector<std::uint64_t>{3 * period}, "clock scaling");
        Check(kernel.ExecutedCycles() == 4, "clock scaling changed cycle count");
    }
}
struct LifecycleProbe : ModelBase {
    unsigned work_count = 0;
    unsigned final_count = 0;
    LifecycleProbe(ModelContext& context) : ModelBase({50}, "probe", context) {}
    void OnInitialize() override {}
    void OnReset() override {}
    void OnWork() override {
        ++work_count;
    }
    void OnXfer() override {}
    void OnFinalize() override {
        ++final_count;
    }
    bool IsIdle() const override {
        return work_count > 0;
    }
    std::string DescribeState() const override {
        return "probe";
    }
};
void LifecycleTest() {
    Fixture fixture;
    LifecycleProbe probe(fixture.context);
    Rejects([&] { probe.Work(); }, "lifecycle");
    Rejects([&] { probe.Reset(); }, "startup");
    fixture.kernel.Add(probe);
    Rejects([&] { fixture.kernel.Add(probe); }, "identity");
    fixture.kernel.Run(4);
    Check(probe.work_count == 1 && probe.final_count == 1, "lifecycle invocation count");
    Rejects([&] { probe.Reset(); }, "startup");
    Rejects([&] { probe.Initialize(); }, "initialization");
    Rejects([&] { fixture.kernel.Run(3); }, "once");
    Rejects([&] { fixture.kernel.Schedule(0, 1, [] {}); }, "zero-delay");
    Fixture event_fixture;
    std::vector<int> order;
    event_fixture.kernel.Schedule(2, 20, [&] { order.push_back(3); });
    event_fixture.kernel.Schedule(2, 10, [&] { order.push_back(1); });
    event_fixture.kernel.Schedule(2, 10, [&] { order.push_back(2); });
    event_fixture.kernel.Run(5);
    Check(order == std::vector<int>({1, 2, 3}), "event ordering");
}
void LoggingTest() {
    Clock clock;
    Logger disabled(clock, Silent());
    Rejects([&] { disabled.Write(LogLevel::kFatal, "platform", "must stop"); }, "must stop");
    LogConfig settings;
    settings.console = false;
    settings.directory = "logging_test/log";
    settings.level = LogLevel::kWarn;
    settings.models["vip.hn"] = LogLevel::kDebug;
    Logger logger(clock, settings);
    logger.RegisterModel("vip.rn0");
    logger.Write(LogLevel::kInfo, "vip.rn0", "hidden");
    logger.Write(LogLevel::kWarn, "vip.rn0", "rn only");
    logger.Write(LogLevel::kDebug, "vip.hn", "visible");
    Rejects([&] { logger.Fail("vip.hn", "fatal marker"); }, "fatal marker");
    std::ifstream stream(settings.directory + "/vip.hn.log");
    std::string text((std::istreambuf_iterator<char>(stream)), {});
    Check(text.find("hidden") == std::string::npos && text.find("visible") != std::string::npos &&
              text.find("time_ps=0 cycle=0 phase=startup model=vip.hn") != std::string::npos &&
              text.find("fatal marker") != std::string::npos,
          "log filtering or flush");
    Check(text.find("rn only") == std::string::npos, "RN log leaked into HN file");
    std::ifstream rn_stream(settings.directory + "/vip.rn0.log");
    std::string rn_text((std::istreambuf_iterator<char>(rn_stream)), {});
    Check(rn_text.find("rn only") != std::string::npos &&
              rn_text.find("hidden") == std::string::npos &&
              rn_text.find("visible") == std::string::npos,
          "model file isolation or filtering");
    Rejects([&] { logger.RegisterModel("../escape"); }, "invalid model log path");
    auto collision = Settings();
    collision.log.directory = "log";
    collision.json_path = "log/vip.hn.log";
    Rejects([&] { collision.Validate(); }, "model log conflicts");
}
void ConfigurationTest() {
    auto extreme = Settings();
    extreme.workload.seed = std::numeric_limits<std::uint64_t>::max();
    Rejects([&] { extreme.Validate(); }, "TOML range");
    auto config = ParseTOML(
        "[clock]\nperiod_ps=1250\n[rn]\ncount=3\ncache_lines=4\nrequest_slots=2\n"
        "[hn]\nrequest_slots=3\n[sn]\nservice_latency=11\n[workload]\nwrite_percent=40\n"
        "[log]\nlevel='off'\n[log.models]\n'vip.sn'='debug'\n");
    Check(config.period_ps == 1250 && config.hn.request_slots == 3 && config.rn.count == 3 &&
              config.rn.cache_lines == 4 && config.rn.request_slots == 2 &&
              config.sn.service_latency == 11 && config.workload.write_percent == 40,
          "VIP TOML fields");
    Check(ParseTOML(config.ToTOML()).ToTOML() == config.ToTOML(), "expanded config roundtrip");
    for (const auto& text :
         {"[hn]\nentries=3", "[sn]\nentries=4", "[ddr]\nlatency_cycles=40", "[rn]\noutstanding=3",
          "[rn]\nreceive_interval_cycles=1", "[unknown]", "[hn]\nrequest_slots='3'",
          "[hn]\nrequest_slots=-1", "[workload]\nwrite_percent=101"})
        Rejects([&] { ParseTOML(text); }, "");
    Rejects([] { ParseTOML("[clock]\nperiod_ps=0"); }, "positive");
    Rejects([] { ParseTOML("[log.models]\n'soc.ddr0'='info'"); }, "unknown model");
}
}  // namespace
int main(int argc, char** argv) {
    try {
        Check(argc == 2, "test case required");
        const std::string test(argv[1]);
        if (test == "clock")
            ClockTest();
        else if (test == "lifecycle")
            LifecycleTest();
        else if (test == "logging")
            LoggingTest();
        else if (test == "configuration")
            ConfigurationTest();
        else
            throw PlatformError("unknown test");
        std::cout << test << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
