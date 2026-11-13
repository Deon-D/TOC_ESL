#include "foundation/log.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace esl {
LogLevel ParseLogLevel(const std::string& text) {
    const std::vector<std::string> names{"trace", "debug", "info", "warn", "error", "fatal", "off"};
    for (unsigned i = 0; i < names.size(); ++i)
        if (names[i] == text)
            return static_cast<LogLevel>(i);
    throw PlatformError("unknown log level: " + text);
}
std::string LogLevelName(LogLevel level) {
    return std::vector<std::string>{"trace", "debug", "info", "warn", "error", "fatal", "off"}.at(
        static_cast<unsigned>(level));
}
struct Logger::Impl {
    const Clock& clock;
    LogConfig config;
    std::shared_ptr<spdlog::sinks::ostream_sink_st> console;
    std::map<std::string, std::shared_ptr<spdlog::logger>> loggers;
};
Logger::Logger(const Clock& clock, const LogConfig& config)
    : impl_(std::make_unique<Impl>(Impl{clock, config, {}, {}})) {
    if (config.console)
        impl_->console = std::make_shared<spdlog::sinks::ostream_sink_st>(std::clog, false);
    RegisterModel("platform");
}
void Logger::RegisterModel(const std::string& model) {
    Require(!model.empty() && model != "." && model != ".." &&
                model.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
                    std::string::npos,
            "invalid model log path: " + model);
    if (impl_->loggers.count(model))
        return;
    std::vector<spdlog::sink_ptr> sinks;
    if (impl_->console)
        sinks.push_back(impl_->console);
    if (!impl_->config.directory.empty()) {
        const auto path = std::filesystem::path(impl_->config.directory) / (model + ".log");
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_st>(path.string(), true));
    }
    auto logger = std::make_shared<spdlog::logger>(model, sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->set_pattern("%v");
    logger->set_error_handler(
        [](const std::string& message) { throw PlatformError("log output failure: " + message); });
    impl_->loggers.emplace(model, std::move(logger));
}
Logger::~Logger() = default;
bool Logger::Enabled(LogLevel level, const std::string& model) const {
    auto threshold = impl_->config.level;
    auto found = impl_->config.models.find(model);
    if (found != impl_->config.models.end())
        threshold = found->second;
    return level != LogLevel::kOff && threshold != LogLevel::kOff && level >= threshold;
}
void Logger::Write(LogLevel level, const std::string& model, const std::string& message) {
    if (Enabled(level, model)) {
        RegisterModel(model);
        std::ostringstream text;
        text << "time_ps=" << impl_->clock.NowPs().value
             << " cycle=" << impl_->clock.CurrentCycle().value
             << " phase=" << PhaseName(impl_->clock.CurrentPhase()) << " model=" << model
             << " level=" << LogLevelName(level) << " " << message;
        impl_->loggers.at(model)->log(static_cast<spdlog::level::level_enum>(level), text.str());
    }
    if (level == LogLevel::kFatal) {
        Flush();
        throw PlatformError(model + ": " + message);
    }
}
void Logger::Fail(const std::string& model, const std::string& message) {
    Write(LogLevel::kFatal, model, message);
    Flush();
    throw PlatformError(model + ": " + message);
}
void Logger::Flush() {
    for (const auto& entry : impl_->loggers)
        entry.second->flush();
}
TraceSink::TraceSink(const Clock& clock, std::ostream* output) : clock_(clock), output_(output) {
    if (output_)
        *output_ << "time_ps,cycle,phase,model,action,txn_id,beat\n";
}
void TraceSink::Record(const std::string& model, const std::string& action, unsigned id, int beat) {
    if (!output_)
        return;
    std::ostringstream row;
    row << clock_.NowPs().value << ',' << clock_.CurrentCycle().value << ','
        << PhaseName(clock_.CurrentPhase()) << ',' << model << ',' << action << ',' << id << ','
        << beat << '\n';
    pending_.push_back(row.str());
}
void TraceSink::Flush() {
    if (!output_)
        return;
    std::stable_sort(pending_.begin(), pending_.end());
    for (const auto& row : pending_)
        *output_ << row;
    pending_.clear();
    Require(bool(*output_), "trace output failure");
}
}  // namespace esl
