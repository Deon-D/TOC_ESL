#include "transport_adapter.hpp"

#include <sstream>
#include <type_traits>

namespace esl::chi_vip {
TransportAdapter::TransportAdapter(RN& core, CHIPort& port) : core_(core), port_(port) {}
template <class T>
void TransportAdapter::Receive() {
    T flit;
    while (port_.CanRecv<T>()) {
        if constexpr (std::is_same_v<T, CHISnpFlit>)
            if (!core_.CanReceiveSnoop())
                break;
        port_.Recv(flit);
        core_.OnEvent(Event::Received(flit));
    }
}
void TransportAdapter::Work(std::uint64_t cycle) {
    Check(!working_, ErrorKind::kLifecycle, "adapter Work called twice");
    working_ = true;
    cycle_ = cycle;
    port_.Work();
    for (const auto& event : confirmations_)
        core_.OnEvent(event);
    confirmations_.clear();
    Check(!port_.CanRecv<CHIReqFlit>(), ErrorKind::kProtocolViolation, "REQ delivered to RN port");
    Receive<CHIRspFlit>();
    Receive<CHISnpFlit>();
    Receive<CHIDatFlit>();
    core_.BeginCycle(cycle);
    for (auto& action : core_.TakeActions()) {
        Check(action.destination == core_.Config().home_id, ErrorKind::kProtocolViolation,
              "adapter has no route to destination");
        pending_[action.flit.index()].push_back(std::move(action));
    }
    // Independent channel queues: blocked REQ never head-of-line blocks RSP/DAT.
    for (unsigned channel = 0; channel < 4; ++channel) {
        auto& queue = pending_[channel];
        while (!queue.empty()) {
            const auto& action = queue.front();
            const bool accepted =
                std::visit([this](const auto& f) { return port_.Send(f); }, action.flit);
            if (!accepted)
                break;
            tokens_[channel].push_back(action.token);
            confirmations_.push_back(Event::Accepted(action.token));
            queue.pop_front();
        }
    }
}
template <class T>
void TransportAdapter::Transmitted(unsigned channel) {
    if (!port_.Transmitted<T>())
        return;
    Check(!tokens_[channel].empty(), ErrorKind::kProtocolViolation, "port sent an unowned flit");
    confirmations_.push_back(Event::Sent(tokens_[channel].front(), cycle_));
    tokens_[channel].pop_front();
}
void TransportAdapter::Xfer() {
    Check(working_, ErrorKind::kLifecycle, "adapter Xfer without Work");
    port_.Xfer();
    Transmitted<CHIReqFlit>(0);
    Transmitted<CHIRspFlit>(1);
    Transmitted<CHISnpFlit>(2);
    Transmitted<CHIDatFlit>(3);
    core_.Commit();
    working_ = false;
}
bool TransportAdapter::IsIdle() const {
    if (working_ || !confirmations_.empty() || !core_.IsIdle() || !port_.IsIdle())
        return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!pending_[i].empty() || !tokens_[i].empty())
            return false;
    return true;
}
std::string TransportAdapter::DescribeState() const {
    std::ostringstream out;
    out << core_.DescribeState() << ' ' << port_.DescribeState();
    for (unsigned i = 0; i < 4; ++i)
        out << " channel" << i << " queued=" << pending_[i].size()
            << " accepted=" << tokens_[i].size();
    return out.str();
}
}  // namespace esl::chi_vip
