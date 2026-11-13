#pragma once

#include <map>
#include <memory>

#include "chi_port.h"
#include "types.hpp"

namespace esl::chi_vip {
class HN;
class SN;

// The HN has one physical CHI link per peer. Routing metadata carries the
// destination for SNP, whose protocol payload deliberately has no TgtID.
// Neither the connected node nor its ports are owned by this adapter.
class NodeTransportAdapter final {
public:
    NodeTransportAdapter(HN& core, std::map<NodeID, CHIPort*> ports);
    NodeTransportAdapter(SN& core, CHIPort& port);
    ~NodeTransportAdapter();
    NodeTransportAdapter(const NodeTransportAdapter&) = delete;
    NodeTransportAdapter& operator=(const NodeTransportAdapter&) = delete;

    void Work(std::uint64_t cycle);
    void Xfer();
    bool IsIdle() const;
    std::string DescribeState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace esl::chi_vip
