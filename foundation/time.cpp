#include "foundation/time.hpp"

#include <limits>
namespace esl {
void Require(bool condition, const std::string& message) {
    if (!condition)
        throw PlatformError(message);
}
const char* PhaseName(Phase phase) {
    switch (phase) {
        case Phase::kStartup:
            return "startup";
        case Phase::kPrepare:
            return "prepare";
        case Phase::kWork:
            return "work";
        case Phase::kResolve:
            return "resolve";
        case Phase::kXfer:
            return "xfer";
        case Phase::kPublish:
            return "publish";
        case Phase::kFinalize:
            return "finalize";
    }
    return "unknown";
}
CycleCount AddCycles(CycleCount cycle, std::uint64_t delay) {
    Require(delay <= std::numeric_limits<std::uint64_t>::max() - cycle.value, "cycle overflow");
    return {cycle.value + delay};
}
TimePs ToTimePs(CycleCount cycle, std::uint64_t period_ps) {
    Require(period_ps > 0, "period_ps must be positive");
    Require(cycle.value <= std::numeric_limits<std::uint64_t>::max() / period_ps,
            "physical time overflow");
    return {cycle.value * period_ps};
}
Clock::Clock(std::uint64_t period_ps) : period_ps_(period_ps) {
    ToTimePs({}, period_ps);
}
TimePs Clock::NowPs() const {
    return ToTimePs(cycle_, period_ps_);
}
CycleCount Clock::CurrentCycle() const {
    return cycle_;
}
std::uint64_t Clock::PeriodPs() const {
    return period_ps_;
}
Phase Clock::CurrentPhase() const {
    return phase_;
}
void Clock::Advance() {
    auto next = AddCycles(cycle_, 1);
    ToTimePs(next, period_ps_);
    cycle_ = next;
}
void Clock::SetPhase(Phase phase) {
    phase_ = phase;
}
}  // namespace esl
