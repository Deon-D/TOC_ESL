#pragma once
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "foundation/time.hpp"
namespace esl {
enum class LogLevel { kTrace, kDebug, kInfo, kWarn, kError, kFatal, kOff };
LogLevel ParseLogLevel(const std::string& text);
std::string LogLevelName(LogLevel level);
struct LogConfig {
    LogLevel level = LogLevel::kInfo;
    bool console = true;
    std::string directory = "log";
    std::map<std::string, LogLevel> models;
};
class Logger {
public:
    // Owns synchronous buffered sinks. No process-global logger is registered.
    Logger(const Clock& clock, const LogConfig& config);
    ~Logger();
    // Creates a separate model file in the configured directory; an empty directory disables files.
    void RegisterModel(const std::string& model);
    bool Enabled(LogLevel level, const std::string& model) const;
    // Diagnostic-only except Fatal, which flushes and throws even when logging is disabled.
    void Write(LogLevel level, const std::string& model, const std::string& message);
    // Flush diagnostics, then throw PlatformError; never resume the failed simulation.
    [[noreturn]] void Fail(const std::string& model, const std::string& message);
    void Flush();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class TraceSink {
public:
    // The stream is borrowed and must outlive this sink; nullptr disables tracing.
    TraceSink(const Clock& clock, std::ostream* output);
    void Record(const std::string& model, const std::string& action, unsigned id, int beat = -1);
    // Flush once per phase to retain phase order and remove model traversal order.
    void Flush();

private:
    const Clock& clock_;
    std::ostream* output_;
    std::vector<std::string> pending_;
};
}  // namespace esl
