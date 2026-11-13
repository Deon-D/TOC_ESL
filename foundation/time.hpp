#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
namespace esl {
class PlatformError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
void Require(bool condition, const std::string& message);
struct CycleCount {
    std::uint64_t value = 0;
};
struct TimePs {
    std::uint64_t value = 0;
};
struct ModelID {
    unsigned value = 0;
};
enum class Phase { kStartup, kPrepare, kWork, kResolve, kXfer, kPublish, kFinalize };
const char* PhaseName(Phase phase);
CycleCount AddCycles(CycleCount cycle, std::uint64_t delay);
TimePs ToTimePs(CycleCount cycle, std::uint64_t period_ps);
class SimKernel;
class Clock {
public:
    explicit Clock(std::uint64_t period_ps = 1000);
    TimePs NowPs() const;
    CycleCount CurrentCycle() const;
    std::uint64_t PeriodPs() const;
    Phase CurrentPhase() const;

private:
    friend class SimKernel;
    void Advance();
    void SetPhase(Phase phase);
    std::uint64_t period_ps_;
    CycleCount cycle_;
    Phase phase_ = Phase::kStartup;
};
}  // namespace esl
