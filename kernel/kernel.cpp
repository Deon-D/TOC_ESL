#include "kernel/kernel.hpp"

#include <algorithm>
#include <sstream>
#include <tuple>
#include <utility>
namespace esl {
bool SimKernel::Event::operator>(const Event& other) const {
    return std::tie(due, owner, sequence) > std::tie(other.due, other.owner, other.sequence);
}
SimKernel::SimKernel(Clock& clock, Logger& log, TraceSink& trace, bool reverse_traversal)
    : clock_(clock), log_(log), trace_(trace), reverse_traversal_(reverse_traversal) {}
void SimKernel::Add(ModelBase& model) {
    Require(!started_, "registration after startup");
    for (auto* item : models_)
        Require(item->ID().value != model.ID().value && item->Path() != model.Path(),
                "duplicate model identity");
    models_.push_back(&model);
}
void SimKernel::Add(ProtocolLink& link) {
    Require(!started_, "registration after startup");
    Require(std::find(links_.begin(), links_.end(), &link) == links_.end(), "duplicate link");
    links_.push_back(&link);
}
void SimKernel::Schedule(std::uint64_t delay_cycles, unsigned owner, std::function<void()> action) {
    Require(delay_cycles > 0, "zero-delay events are forbidden");
    auto due = AddCycles(clock_.CurrentCycle(), delay_cycles);
    ToTimePs(due, clock_.PeriodPs());
    events_.push({due.value, owner, sequence_[owner]++, std::move(action)});
}
void SimKernel::Run(std::uint64_t max_cycles) {
    Require(!started_, "kernel can only run once");
    Require(max_cycles > 0, "max_cycles must be positive");
    started_ = true;
    std::sort(models_.begin(), models_.end(),
              [](auto* a, auto* b) { return a->ID().value < b->ID().value; });
    if (reverse_traversal_)
        std::reverse(models_.begin(), models_.end());
    std::vector<ModelBase*> initialized;
    try {
        for (auto* link : links_)
            link->Validate();
        for (auto* model : models_) {
            model->Initialize();
            initialized.push_back(model);
        }
        for (auto* model : models_)
            model->Reset();
        bool done = false;
        while (executed_cycles_ < max_cycles) {
            clock_.SetPhase(Phase::kPrepare);
            while (!events_.empty() && events_.top().due == clock_.CurrentCycle().value) {
                auto event = events_.top();
                events_.pop();
                event.action();
            }
            for (auto* link : links_)
                link->Prepare();
            trace_.Flush();
            clock_.SetPhase(Phase::kWork);
            for (auto* model : models_) {
                try {
                    model->Work();
                } catch (const std::exception& error) {
                    throw PlatformError(model->Path() + " Work: " + error.what());
                }
            }
            trace_.Flush();
            clock_.SetPhase(Phase::kResolve);
            for (auto* link : links_)
                link->Resolve();
            trace_.Flush();
            clock_.SetPhase(Phase::kXfer);
            for (auto* model : models_) {
                try {
                    model->Xfer();
                } catch (const std::exception& error) {
                    throw PlatformError(model->Path() + " Xfer: " + error.what());
                }
            }
            trace_.Flush();
            clock_.SetPhase(Phase::kPublish);
            for (auto* link : links_)
                link->Publish();
            trace_.Flush();
            ++executed_cycles_;
            done =
                events_.empty() &&
                std::all_of(models_.begin(), models_.end(), [](auto* m) { return m->IsIdle(); }) &&
                std::all_of(links_.begin(), links_.end(), [](auto* l) { return l->IsIdle(); });
            if (done)
                break;
            if (executed_cycles_ < max_cycles)
                clock_.Advance();
        }
        Require(done, "simulation timeout");
        clock_.SetPhase(Phase::kFinalize);
        while (!initialized.empty()) {
            auto* model = initialized.back();
            initialized.pop_back();
            model->Finalize();
        }
        log_.Write(LogLevel::kInfo, "platform", "simulation completed");
        log_.Flush();
        trace_.Flush();
    } catch (const std::exception& error) {
        const std::string diagnostic = std::string(error.what()) + "\n" + DescribeState();
        // Diagnostics must not replace the original error if a log sink has failed.
        try {
            log_.Write(LogLevel::kFatal, "platform", diagnostic);
            log_.Flush();
        } catch (...) {
        }
        clock_.SetPhase(Phase::kFinalize);
        for (auto* model : initialized) {
            try {
                model->Finalize();
            } catch (...) {
            }
        }
        try {
            trace_.Flush();
        } catch (...) {
        }
        throw PlatformError(diagnostic);
    }
}
std::uint64_t SimKernel::ExecutedCycles() const {
    return executed_cycles_;
}
std::string SimKernel::DescribeState() const {
    std::ostringstream text;
    text << "cycle=" << clock_.CurrentCycle().value << " time_ps=" << clock_.NowPs().value
         << " phase=" << PhaseName(clock_.CurrentPhase()) << " events=" << events_.size();
    auto sorted = models_;
    std::sort(sorted.begin(), sorted.end(),
              [](auto* a, auto* b) { return a->ID().value < b->ID().value; });
    for (auto* model : sorted)
        text << '\n' << model->Path() << ' ' << model->DescribeState();
    for (auto* link : links_)
        text << '\n' << link->DescribeState();
    return text.str();
}
}  // namespace esl
