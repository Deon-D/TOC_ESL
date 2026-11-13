#include "node_transport_adapter.hpp"

#include <array>
#include <deque>
#include <set>
#include <sstream>
#include <type_traits>

#include "hn.hpp"
#include "sn.hpp"

namespace esl::chi_vip {
struct NodeTransportAdapter::Impl {
    struct Link {
        CHIPort* port = nullptr;
        std::array<std::deque<SendAction>, 4> pending;
        std::array<std::deque<Token>, 4> tokens;
    };

    std::variant<HN*, SN*> core;
    NodeID node;
    bool home;
    bool working = false;
    std::uint64_t cycle = 0;
    std::map<NodeID, Link> links;
    std::vector<Event> confirmations;

    Impl(HN& value, std::map<NodeID, CHIPort*> ports)
        : core(&value), node(value.Config().node_id), home(true) {
        std::set<NodeID> expected(value.Config().requester_ids.begin(),
                                  value.Config().requester_ids.end());
        expected.insert(value.Config().slave_id);
        Check(ports.size() == expected.size(), ErrorKind::kConfiguration,
              "HN adapter requires exactly one port for each configured peer");
        for (const auto peer : expected)
            Check(ports.count(peer) == 1, ErrorKind::kConfiguration,
                  "HN adapter is missing a configured peer");
        SetPorts(ports);
    }

    Impl(SN& value, CHIPort& port) : core(&value), node(value.Config().node_id), home(false) {
        SetPorts({{value.Config().home_id, &port}});
    }

    void SetPorts(const std::map<NodeID, CHIPort*>& ports) {
        std::set<CHIPort*> unique;
        for (const auto& [peer, port] : ports) {
            Check(port != nullptr && peer != node && unique.insert(port).second,
                  ErrorKind::kConfiguration, "invalid or duplicate CHI port route");
            links[peer].port = port;
        }
    }

    void OnEvent(const Event& event) {
        std::visit([&event](auto* value) { value->OnEvent(event); }, core);
    }

    template <class T>
    void Receive(NodeID peer, Link& link) {
        T flit;
        while (link.port->Recv(flit)) {
            Check(flit.flit_src == peer && flit.flit_tgt == node, ErrorKind::kProtocolViolation,
                  "CHI receive route disagrees with payload SrcID or TgtID");
            OnEvent(Event::Received(flit));
        }
    }

    void Work(std::uint64_t next_cycle) {
        Check(!working, ErrorKind::kLifecycle, "node adapter Work called twice");
        working = true;
        cycle = next_cycle;
        for (auto& [peer, link] : links) {
            (void)peer;
            link.port->Work();
        }
        for (const auto& event : confirmations)
            OnEvent(event);
        confirmations.clear();
        // Map traversal makes input order independent of ModelBase registration.
        for (auto& [peer, link] : links) {
            Check(!link.port->CanRecv<CHISnpFlit>(), ErrorKind::kProtocolViolation,
                  "SNP delivered to HN or SN");
            if (!home)
                Check(!link.port->CanRecv<CHIRspFlit>(), ErrorKind::kProtocolViolation,
                      "RSP delivered to SN in the non-DMT profile");
            Receive<CHIRspFlit>(peer, link);
            Receive<CHIDatFlit>(peer, link);
            Receive<CHIReqFlit>(peer, link);
        }
        std::visit([this](auto* value) { value->BeginCycle(cycle); }, core);
        auto actions = std::visit([](auto* value) { return value->TakeActions(); }, core);
        for (auto& action : actions) {
            const auto found = links.find(action.destination);
            Check(found != links.end(), ErrorKind::kProtocolViolation,
                  "node adapter has no route to destination");
            std::visit(
                [this, &action](const auto& flit) {
                    Check(flit.flit_src == node, ErrorKind::kProtocolViolation,
                          "outgoing flit has incorrect SrcID");
                    if constexpr (!std::is_same_v<std::decay_t<decltype(flit)>, CHISnpFlit>)
                        Check(flit.flit_tgt == action.destination, ErrorKind::kProtocolViolation,
                              "outgoing flit TgtID disagrees with route");
                },
                action.flit);
            found->second.pending[action.flit.index()].push_back(std::move(action));
        }
        // A full request channel cannot block snoop responses or write data.
        for (auto& [peer, link] : links) {
            (void)peer;
            for (unsigned channel = 0; channel < 4; ++channel) {
                auto& queue = link.pending[channel];
                while (!queue.empty()) {
                    const auto& action = queue.front();
                    if (!std::visit([&link](const auto& flit) { return link.port->Send(flit); },
                                    action.flit))
                        break;
                    link.tokens[channel].push_back(action.token);
                    confirmations.push_back(Event::Accepted(action.token));
                    queue.pop_front();
                }
            }
        }
    }

    template <class T>
    void Transmitted(Link& link, unsigned channel) {
        if (!link.port->Transmitted<T>())
            return;
        auto& tokens = link.tokens[channel];
        Check(!tokens.empty(), ErrorKind::kProtocolViolation,
              "node adapter port sent an unowned flit");
        confirmations.push_back(Event::Sent(tokens.front(), cycle));
        tokens.pop_front();
    }

    void Xfer() {
        Check(working, ErrorKind::kLifecycle, "node adapter Xfer without Work");
        for (auto& [peer, link] : links) {
            (void)peer;
            link.port->Xfer();
            Transmitted<CHIReqFlit>(link, 0);
            Transmitted<CHIRspFlit>(link, 1);
            Transmitted<CHISnpFlit>(link, 2);
            Transmitted<CHIDatFlit>(link, 3);
        }
        std::visit([](auto* value) { value->Commit(); }, core);
        working = false;
    }

    bool IsIdle() const {
        if (working || !confirmations.empty() ||
            !std::visit([](const auto* value) { return value->IsIdle(); }, core))
            return false;
        for (const auto& [peer, link] : links) {
            (void)peer;
            if (!link.port->IsIdle())
                return false;
            for (unsigned channel = 0; channel < 4; ++channel)
                if (!link.pending[channel].empty() || !link.tokens[channel].empty())
                    return false;
        }
        return true;
    }

    std::string DescribeState() const {
        std::ostringstream out;
        out << std::visit([](const auto* value) { return value->DescribeState(); }, core);
        for (const auto& [peer, link] : links) {
            out << " peer=" << peer << ' ' << link.port->DescribeState();
            for (unsigned channel = 0; channel < 4; ++channel)
                out << " channel" << channel << " queued=" << link.pending[channel].size()
                    << " accepted=" << link.tokens[channel].size();
        }
        return out.str();
    }
};

NodeTransportAdapter::NodeTransportAdapter(HN& core, std::map<NodeID, CHIPort*> ports)
    : impl_(std::make_unique<Impl>(core, std::move(ports))) {}
NodeTransportAdapter::NodeTransportAdapter(SN& core, CHIPort& port)
    : impl_(std::make_unique<Impl>(core, port)) {}
NodeTransportAdapter::~NodeTransportAdapter() = default;
void NodeTransportAdapter::Work(std::uint64_t cycle) {
    impl_->Work(cycle);
}
void NodeTransportAdapter::Xfer() {
    impl_->Xfer();
}
bool NodeTransportAdapter::IsIdle() const {
    return impl_->IsIdle();
}
std::string NodeTransportAdapter::DescribeState() const {
    return impl_->DescribeState();
}
}  // namespace esl::chi_vip
