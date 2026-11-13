#pragma once

#include <array>
#include <deque>

#include "chi_port.h"
#include "rn.hpp"

namespace esl::chi_vip {
// One point-to-point RN/HN link. HN selection is transport metadata. No TgtID is
// added to CHISnpFlit. This adapter owns neither the core nor the connected port.
class TransportAdapter {
public:
    TransportAdapter(RN& core, CHIPort& port);
    void Work(std::uint64_t cycle);
    void Xfer();
    bool IsIdle() const;
    std::string DescribeState() const;

private:
    RN& core_;
    CHIPort& port_;
    bool working_ = false;
    std::uint64_t cycle_ = 0;
    std::array<std::deque<SendAction>, 4> pending_;
    std::array<std::deque<Token>, 4> tokens_;
    std::vector<Event> confirmations_;
    template <class T>
    void Receive();
    template <class T>
    void Transmitted(unsigned channel);
};
}  // namespace esl::chi_vip
