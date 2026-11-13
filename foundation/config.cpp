#include "foundation/config.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <toml++/toml.hpp>
namespace esl {
namespace {
std::uint64_t Unsigned(const std::string& value) {
    std::uint64_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    Require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(),
            "invalid unsigned integer: " + value);
    Require(result <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
            "integer exceeds TOML range");
    return result;
}
unsigned Small(std::uint64_t value) {
    Require(value <= std::numeric_limits<unsigned>::max(), "integer out of range");
    return static_cast<unsigned>(value);
}
void Assign(SimConfig& config, const std::string& key, const toml::node& node) {
    auto number = [&] {
        Require(node.is_integer(), key + ": expected integer");
        auto value = node.value<std::int64_t>().value();
        Require(value >= 0, key + ": expected unsigned integer");
        return static_cast<std::uint64_t>(value);
    };
    auto text = [&] {
        Require(node.is_string(), key + ": expected string");
        return node.value<std::string>().value();
    };
    auto boolean = [&] {
        Require(node.is_boolean(), key + ": expected bool");
        return node.value<bool>().value();
    };
    if (key == "clock.period_ps")
        config.period_ps = number();
    else if (key == "platform.max_cycles")
        config.max_cycles = number();
    else if (key == "platform.timeout_cycles")
        config.timeout_cycles = number();
    else if (key == "platform.reverse_traversal")
        config.reverse_traversal = boolean();
    else if (key == "rn.count")
        config.rn.count = Small(number());
    else if (key == "rn.cache_lines")
        config.rn.cache_lines = Small(number());
    else if (key == "rn.request_slots")
        config.rn.request_slots = Small(number());
    else if (key == "rn.snoop_slots")
        config.rn.snoop_slots = Small(number());
    else if (key == "rn.pending_operations")
        config.rn.pending_operations = Small(number());
    else if (key == "hn.request_slots")
        config.hn.request_slots = Small(number());
    else if (key == "hn.grant_before_retry")
        config.hn.grant_before_retry = boolean();
    else if (key == "sn.request_slots")
        config.sn.request_slots = Small(number());
    else if (key == "sn.service_latency")
        config.sn.service_latency = number();
    else if (key == "sn.grant_before_retry")
        config.sn.grant_before_retry = boolean();
    else if (key == "workload.requests")
        config.workload.requests = number();
    else if (key == "workload.seed")
        config.workload.seed = number();
    else if (key == "workload.memory_size")
        config.workload.memory_size = number();
    else if (key == "workload.start_address")
        config.workload.start_address = number();
    else if (key == "workload.issue_interval_cycles")
        config.workload.issue_interval_cycles = number();
    else if (key == "workload.write_percent")
        config.workload.write_percent = Small(number());
    else if (key == "workload.address_mode") {
        auto mode = text();
        Require(mode == "sequential" || mode == "random", "invalid address mode");
        config.workload.random_addresses = mode == "random";
    } else if (key == "log.level")
        config.log.level = ParseLogLevel(text());
    else if (key == "log.console")
        config.log.console = boolean();
    else if (key == "log.directory")
        config.log.directory = text();
    else if (key == "output.json")
        config.json_path = text();
    else if (key == "output.trace")
        config.trace_path = text();
    else
        throw PlatformError("unknown configuration field: " + key);
}
SimConfig Parse(const toml::table& table) {
    SimConfig config;
    const std::set<std::string> groups{"clock", "platform", "rn",  "hn",
                                       "sn",    "workload", "log", "output"};
    for (const auto& [group, node] : table) {
        std::string name(group.str());
        Require(groups.count(name) && node.is_table(), "unknown configuration table: " + name);
        for (const auto& [key, value] : *node.as_table()) {
            if (name == "log" && key.str() == "models") {
                Require(value.is_table(), "log.models must be a table");
                for (const auto& [path, level] : *value.as_table()) {
                    Require(level.is_string(), "model log level must be a string");
                    config.log.models[std::string(path.str())] =
                        ParseLogLevel(level.value<std::string>().value());
                }
            } else
                Assign(config, name + "." + std::string(key.str()), value);
        }
    }
    return config;
}
}  // namespace
void SimConfig::Validate() const {
    for (auto value : {period_ps, max_cycles, timeout_cycles, workload.requests, workload.seed,
                       workload.memory_size, workload.start_address, workload.issue_interval_cycles,
                       sn.service_latency}) {
        Require(value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
                "configuration integer exceeds TOML range");
    }
    Require(max_cycles > 0, "max_cycles must be positive");
    Require(timeout_cycles > 0, "timeout_cycles must be positive");
    ToTimePs({max_cycles - 1}, period_ps);
    Require(workload.requests > 0, "requests must be positive");
    Require(workload.memory_size >= 64 && workload.memory_size % 64 == 0,
            "memory size must be a multiple of 64");
    Require(workload.memory_size <= (std::uint64_t{1} << 48),
            "memory size exceeds the 48-bit address space");
    Require(workload.start_address % 64 == 0 && workload.start_address <= workload.memory_size - 64,
            "invalid start address");
    Require(rn.count > 0 && rn.count <= 126, "RN count must be 1..126");
    Require(rn.request_slots > 0 && rn.request_slots <= 1023, "RN request slots must be 1..1023");
    for (auto value : {hn.request_slots, sn.request_slots})
        Require(value > 0 && value <= 1024, "HN/SN request slots must be 1..1024");
    Require(hn.request_slots <= 4096 / rn.count,
            "HN request slots times RN count must not exceed 4096");
    Require(rn.cache_lines > 0, "RN cache lines must be positive");
    Require(rn.snoop_slots > 0, "RN snoop slots must be positive");
    Require(rn.pending_operations > 0, "RN pending operations must be positive");
    Require(workload.issue_interval_cycles > 0, "issue interval must be positive");
    Require(workload.write_percent <= 100, "write percent must be 0..100");
    std::set<std::string> paths{"platform", "vip.hn", "vip.sn", "vip.workload"};
    for (unsigned index = 0; index < rn.count; ++index)
        paths.insert("vip.rn" + std::to_string(index));
    for (const auto& entry : log.models)
        Require(paths.count(entry.first), "unknown model log path: " + entry.first);
    Require(!json_path.empty(), "JSON output path must not be empty");
    std::set<std::filesystem::path> outputs;
    for (const auto& path : {json_path, trace_path}) {
        if (!path.empty())
            Require(
                outputs.insert(std::filesystem::weakly_canonical(std::filesystem::absolute(path)))
                    .second,
                "output paths must differ");
    }
    if (!log.directory.empty()) {
        const auto directory =
            std::filesystem::weakly_canonical(std::filesystem::absolute(log.directory));
        Require(!outputs.count(directory), "log directory conflicts with output path");
        for (const auto& model : paths)
            Require(!outputs.count(std::filesystem::weakly_canonical(directory / (model + ".log"))),
                    "model log conflicts with output path");
    }
}
std::string SimConfig::ToTOML() const {
    toml::table table;
    table.insert("clock", toml::table{{"period_ps", static_cast<std::int64_t>(period_ps)}});
    table.insert("platform",
                 toml::table{{"max_cycles", static_cast<std::int64_t>(max_cycles)},
                             {"timeout_cycles", static_cast<std::int64_t>(timeout_cycles)},
                             {"reverse_traversal", reverse_traversal}});
    table.insert("rn", toml::table{{"count", rn.count},
                                   {"cache_lines", rn.cache_lines},
                                   {"request_slots", rn.request_slots},
                                   {"snoop_slots", rn.snoop_slots},
                                   {"pending_operations", rn.pending_operations}});
    table.insert("hn", toml::table{{"request_slots", hn.request_slots},
                                   {"grant_before_retry", hn.grant_before_retry}});
    table.insert("sn",
                 toml::table{{"request_slots", sn.request_slots},
                             {"service_latency", static_cast<std::int64_t>(sn.service_latency)},
                             {"grant_before_retry", sn.grant_before_retry}});
    table.insert(
        "workload",
        toml::table{
            {"requests", static_cast<std::int64_t>(workload.requests)},
            {"seed", static_cast<std::int64_t>(workload.seed)},
            {"memory_size", static_cast<std::int64_t>(workload.memory_size)},
            {"start_address", static_cast<std::int64_t>(workload.start_address)},
            {"issue_interval_cycles", static_cast<std::int64_t>(workload.issue_interval_cycles)},
            {"write_percent", workload.write_percent},
            {"address_mode", workload.random_addresses ? "random" : "sequential"}});
    toml::table levels;
    for (const auto& item : log.models)
        levels.insert(item.first, LogLevelName(item.second));
    table.insert("log", toml::table{{"level", LogLevelName(log.level)},
                                    {"console", log.console},
                                    {"directory", log.directory},
                                    {"models", levels}});
    table.insert("output", toml::table{{"json", json_path}, {"trace", trace_path}});
    std::ostringstream output;
    output << table;
    return output.str();
}
SimConfig ParseTOML(const std::string& text) {
    try {
        auto config = Parse(toml::parse(text));
        config.Validate();
        return config;
    } catch (const toml::parse_error& error) {
        throw PlatformError(std::string("TOML parse error: ") + std::string(error.description()));
    }
}
SimConfig LoadConfig(int argc, char** argv) {
    SimConfig config;
    std::string file;
    std::vector<std::pair<std::string, std::string>> overrides;
    for (int i = 1; i < argc; ++i) {
        std::string key(argv[i]);
        if (key == "--reverse-registration") {
            overrides.emplace_back("platform.reverse_traversal", "true");
            continue;
        }
        Require(i + 1 < argc, "missing value for " + key);
        std::string value(argv[++i]);
        if (key == "--config") {
            Require(file.empty(), "duplicate --config");
            file = value;
        } else
            overrides.emplace_back(key, value);
    }
    try {
        if (!file.empty())
            config = Parse(toml::parse_file(file));
    } catch (const toml::parse_error& error) {
        throw PlatformError(std::string("TOML parse error: ") + std::string(error.description()));
    }
    const std::map<std::string, std::string> numeric{
        {"--requests", "workload.requests"},
        {"--seed", "workload.seed"},
        {"--memory-size", "workload.memory_size"},
        {"--start-address", "workload.start_address"},
        {"--issue-interval", "workload.issue_interval_cycles"},
        {"--write-percent", "workload.write_percent"},
        {"--rn-count", "rn.count"},
        {"--cache-lines", "rn.cache_lines"},
        {"--rn-slots", "rn.request_slots"},
        {"--snoop-slots", "rn.snoop_slots"},
        {"--pending-operations", "rn.pending_operations"},
        {"--hn-slots", "hn.request_slots"},
        {"--sn-slots", "sn.request_slots"},
        {"--sn-latency", "sn.service_latency"},
        {"--timeout-cycles", "platform.timeout_cycles"},
        {"--max-cycles", "platform.max_cycles"},
        {"--period-ps", "clock.period_ps"}};
    const std::map<std::string, std::string> strings{{"--json", "output.json"},
                                                     {"--trace", "output.trace"},
                                                     {"--log-level", "log.level"},
                                                     {"--log-dir", "log.directory"},
                                                     {"--address-mode", "workload.address_mode"}};
    for (const auto& [key, value] : overrides) {
        if (numeric.count(key))
            Assign(config, numeric.at(key),
                   toml::value<std::int64_t>(static_cast<std::int64_t>(Unsigned(value))));
        else if (strings.count(key))
            Assign(config, strings.at(key), toml::value<std::string>(value));
        else if (key == "platform.reverse_traversal")
            config.reverse_traversal = true;
        else
            throw PlatformError("unknown option: " + key);
    }
    config.Validate();
    return config;
}
}  // namespace esl
