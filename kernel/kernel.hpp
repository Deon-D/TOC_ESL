#pragma once
#include <functional>
#include <map>
#include <queue>
#include <vector>

#include "kernel/model.hpp"
namespace esl {
class SimKernel {
public:
    SimKernel(Clock& clock, Logger& log, TraceSink& trace, bool reverse_traversal = false);
    void Add(ModelBase& model);
    void Add(ProtocolLink& link);
    // Infrastructure-only scheduling; zero-cycle delivery is forbidden.
    void Schedule(std::uint64_t delay_cycles, unsigned owner, std::function<void()> action);
    void Run(std::uint64_t max_cycles);
    std::string DescribeState() const;
    std::uint64_t ExecutedCycles() const;

private:
    struct Event {
        std::uint64_t due;
        unsigned owner;
        std::uint64_t sequence;
        std::function<void()> action;
        bool operator>(const Event& other) const;
    };
    Clock& clock_;
    Logger& log_;
    TraceSink& trace_;
    bool reverse_traversal_;
    bool started_ = false;
    std::uint64_t executed_cycles_ = 0;
    std::vector<ModelBase*> models_;
    std::vector<ProtocolLink*> links_;
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> events_;
    std::map<unsigned, std::uint64_t> sequence_;
};
}  // namespace esl
