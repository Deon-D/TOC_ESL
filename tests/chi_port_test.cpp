#include "chi_port.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace esl;
void Check(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error("TEST: " + message);
}
template <class Function>
void Rejects(Function function, const std::string& fragment) {
    try {
        function();
    } catch (const std::runtime_error& error) {
        Check(std::string(error.what()).find(fragment) != std::string::npos, error.what());
        return;
    }
    throw std::runtime_error("expected failure: " + fragment);
}
CHIPortParams Channels(CHIChannelParams params) {
    return {params, params, params, params};
}

// A minimal owner: no platform context, clock, logger, kernel or config parser.
struct Endpoint {
    const std::string name;
    CHIPort port;
    CHIPort* extra = nullptr;
    std::function<void()> work;
    std::function<void()> xfer;
    bool finished = false;
    explicit Endpoint(std::string port_name, const CHIChannelParams& params = {})
        : name(std::move(port_name)), port(name + ".chi") {
        port.SetParams(Channels(params));
    }
    void Work() {
        port.Work();
        if (extra)
            extra->Work();
        if (work)
            work();
    }
    void Xfer() {
        port.Xfer();
        if (extra)
            extra->Xfer();
        if (xfer)
            xfer();
    }
    bool IsIdle() const {
        return finished && port.IsIdle() && (!extra || extra->IsIdle());
    }
};
struct Fixture {
    bool reverse;
    std::uint64_t cycle = 0;
    std::uint64_t executed = 0;
    std::ostringstream output;
    std::vector<Endpoint*> endpoints;
    explicit Fixture(bool reversed = false) : reverse(reversed) {}
    template <class Flit>
    void Record(const std::string& name, const CHIPort& port) {
        if (const auto* flit = port.Transmitted<Flit>())
            output << cycle << ',' << name << ",send," << flit->flit_txn << '\n';
    }
    void RecordPort(const std::string& name, const CHIPort& port) {
        Record<CHIReqFlit>(name + ".req", port);
        Record<CHIRspFlit>(name + ".rsp", port);
        Record<CHISnpFlit>(name + ".snp", port);
        Record<CHIDatFlit>(name + ".dat", port);
    }
    void Run(std::uint64_t max_cycles) {
        auto order = endpoints;
        if (reverse)
            std::reverse(order.begin(), order.end());
        for (cycle = 0; cycle < max_cycles; ++cycle) {
            for (auto* endpoint : order)
                endpoint->Work();
            for (auto* endpoint : order)
                endpoint->Xfer();
            ++executed;
            for (const auto* endpoint : endpoints) {
                RecordPort(endpoint->name + ".chi", endpoint->port);
                if (endpoint->extra)
                    RecordPort(endpoint->name + ".out", *endpoint->extra);
            }
            if (std::all_of(endpoints.begin(), endpoints.end(),
                            [](const auto* endpoint) { return endpoint->IsIdle(); }))
                return;
        }
        throw std::runtime_error("standalone CHI timeout");
    }
};
void Register(Fixture& f, Endpoint& a, Endpoint& b) {
    a.port.Connect(b.port);
    f.endpoints = {&a, &b};
}

void BindingTest() {
    Rejects([] { CHIPort unnamed(""); }, "name");
    CHIPort a("a");
    CHIPort b("b");
    CHIPort c("c");
    Rejects([&] { a.CheckBound(); }, "unbound");
    Rejects([&] { a.Work(); }, "unbound");
    Rejects([&] { a.Connect(a); }, "itself");
    Check(!a.Peer(), "self-binding failure must leave port unbound");
    a.Connect(b);
    Check(a.Peer() == &b && b.Peer() == &a, "reciprocal peer pointers");
    Rejects([&] { a.Connect(c); }, "duplicate");
    Rejects([&] { c.Connect(b); }, "duplicate");
    Check(!c.Peer() && a.Peer() == &b && b.Peer() == &a, "binding failure must be atomic");
    Rejects([&] { a.Send(CHIReqFlit{}); }, "Work");
    Rejects([&] { a.CanRecv<CHIRspFlit>(); }, "Work");
    Rejects([&] { a.Transmitted<CHIDatFlit>(); }, "Xfer");
    Rejects([&] { a.Xfer(); }, "Work");
    CHIPort duplicate_name("c");
    Rejects([&] { c.Connect(duplicate_name); }, "name");
    {
        auto transient = std::make_unique<CHIPort>("transient");
        c.Connect(*transient);
    }
    Check(!c.Peer(), "destroyed peer must not leave a dangling pointer");
    for (const auto& bad :
         {CHIChannelParams{0, 1, 1, 1}, {1, 0, 1, 1}, {1, 16, 1, 1}, {1, 1, 0, 1}, {1, 1, 1, 0}})
        Rejects([&] { c.SetParams<CHIReqFlit>(bad); }, "CHI");
    auto bad = Channels({2, 2, 1, 1});
    bad.dat.rx_depth = 0;
    Rejects([&] { c.SetParams(bad); }, "depth");
    Check(c.Params<CHIReqFlit>().tx_depth == 4 && c.Params<CHIReqFlit>().rx_depth == 4,
          "invalid parameters partially applied");
    a.Work();
    Rejects([&] { a.Work(); }, "duplicate");
    Rejects([&] { a.Xfer(); }, "peer");
    Rejects([&] { a.Transmitted<CHIReqFlit>(); }, "Xfer");
    Rejects([&] { b.SetParams<CHIRspFlit>({2, 2, 1, 1}); }, "startup");
    b.Work();
    a.Xfer();
    Rejects([&] { a.Send(CHIReqFlit{}); }, "Work");
    Rejects([&] { a.Work(); }, "cycle mismatch");
    b.Xfer();
    Rejects([&] { b.Xfer(); }, "Work");
    Rejects([&] { a.SetParams({}); }, "startup");
    a.Work();
    b.Work();
    b.Xfer();
    a.Xfer();
}

void IndependentParamsTest(bool reverse) {
    Fixture f(reverse);
    Endpoint a("a");
    Endpoint b("b");
    Register(f, a, b);
    a.port.SetParams<CHIReqFlit>({16, 1, 2, 1});
    a.port.SetParams<CHIRspFlit>({5, 2, 3, 1});
    a.port.SetParams<CHISnpFlit>({2, 3, 4, 1});
    a.port.SetParams<CHIDatFlit>({1, 4, 1, 1});
    b.port.SetParams<CHIReqFlit>({3, 2, 1, 3});
    b.port.SetParams<CHIRspFlit>({4, 3, 1, 1});
    b.port.SetParams<CHISnpFlit>({1, 4, 1, 2});
    b.port.SetParams<CHIDatFlit>({2, 5, 1, 4});
    Check(a.port.Params().req.tx_depth == 16 && a.port.Params().req.rx_depth == 1 &&
              a.port.Params().dat.tx_depth == 1 && a.port.Params().dat.rx_depth == 4,
          "per-channel depths must remain independent");
    std::array<std::uint64_t, 4> sends{};
    std::array<std::uint64_t, 4> receives{};
    unsigned received = 0;
    a.work = [&] {
        if (!a.finished) {
            Check(a.port.Send(CHIReqFlit{}) && a.port.Send(CHIRspFlit{}) &&
                      a.port.Send(CHISnpFlit{}) && a.port.Send(CHIDatFlit{}),
                  "initial queue acceptance");
            a.finished = true;
        }
    };
    a.xfer = [&] {
        if (a.port.Transmitted<CHIReqFlit>())
            sends[0] = f.cycle;
        if (a.port.Transmitted<CHIRspFlit>())
            sends[1] = f.cycle;
        if (a.port.Transmitted<CHISnpFlit>())
            sends[2] = f.cycle;
        if (a.port.Transmitted<CHIDatFlit>())
            sends[3] = f.cycle;
    };
    b.work = [&] {
        CHIReqFlit req;
        CHIRspFlit rsp;
        CHISnpFlit snp;
        CHIDatFlit dat;
        const std::array<bool, 4> accepted{b.port.Recv(req), b.port.Recv(rsp), b.port.Recv(snp),
                                           b.port.Recv(dat)};
        for (std::size_t i = 0; i < accepted.size(); ++i)
            if (accepted[i]) {
                receives[i] = f.cycle;
                ++received;
            }
        b.finished = received == 4;
    };
    f.Run(50);
    Check(sends == std::array<std::uint64_t, 4>{4, 2, 3, 5},
          "credit delay must use the peer's matching channel");
    Check(receives == std::array<std::uint64_t, 4>{6, 5, 7, 6},
          "flit delay must use the sender's matching channel");
    Check(a.port.Credits<CHIReqFlit>() == 2 && a.port.Credits<CHIDatFlit>() == 5 &&
              b.port.Credits<CHIReqFlit>() == 1 && b.port.Credits<CHIDatFlit>() == 4,
          "asymmetric channel capacities must fully drain");
}

void OverflowTest() {
    CHIPort a("a");
    CHIPort b("b");
    a.SetParams<CHIReqFlit>({2, 2, 1, std::numeric_limits<std::uint64_t>::max()});
    a.Connect(b);
    a.Work();
    b.Work();
    a.Xfer();
    b.Xfer();
    a.Work();
    b.Work();
    Rejects([&] { a.Xfer(); }, "overflow");
}

void TimingTest() {
    Fixture f;
    CHIChannelParams config{1, 1, 2, 3};
    Endpoint a("a", config);
    Endpoint b("b", config);
    Register(f, a, b);
    unsigned queued = 0;
    unsigned received = 0;
    std::vector<std::uint64_t> sends;
    std::vector<std::uint64_t> receives;
    a.work = [&] {
        const auto cycle = f.cycle;
        if (cycle == 0 || cycle == 3)
            Check(a.port.Credits<CHIReqFlit>() == 0, "new credit used in its arrival cycle");
        if (queued < 3) {
            CHIReqFlit request;
            request.flit_opcode = CHI_REQ_Opcode_ReadNoSnp;
            request.flit_txn = queued;
            if (a.port.Send(request)) {
                ++queued;
                request.flit_txn = 999;  // Queued payload must already be a copy.
            }
        }
        if (cycle == 0) {
            Check(a.port.TxSize<CHIReqFlit>() == 1 && !a.port.CanSend<CHIReqFlit>(),
                  "TX FIFO must enforce capacity before credit is available");
            Check(!a.port.Send(CHIReqFlit{}), "full TX FIFO must return false");
            CHIRspFlit untouched;
            untouched.flit_txn = 99;
            Check(!a.port.Recv(untouched) && untouched.flit_txn == 99,
                  "empty RX must leave the output unchanged");
        }
        a.finished = queued == 3;
    };
    a.xfer = [&] {
        if (const auto* sent = a.port.Transmitted<CHIReqFlit>()) {
            Check(sent->flit_txn == sends.size(), "wire sends must be FIFO and immutable");
            sends.push_back(f.cycle);
        }
        Rejects([&] { a.port.Send(CHIReqFlit{}); }, "Work");
    };
    b.work = [&] {
        const auto cycle = f.cycle;
        if (cycle < 6)
            Check(!b.port.CanRecv<CHIReqFlit>(), "flit visible before its due cycle");
        if (cycle >= 6 && cycle < 10)
            Check(b.port.RxSize<CHIReqFlit>() == 1 && b.port.Peek<CHIReqFlit>()->flit_txn == 0,
                  "RX must retain a stalled flit");
        CHIReqFlit request;
        if (cycle >= 10 && b.port.Recv(request)) {
            Check(request.flit_txn == received++, "received transaction order");
            receives.push_back(cycle);
        }
        b.finished = received == 3;
    };
    f.Run(100);
    Check(sends == std::vector<std::uint64_t>({4, 14, 20}), "credit return timing");
    Check(receives == std::vector<std::uint64_t>({10, 16, 22}), "flit latency timing");
    Check(f.output.str().find("4,a.chi.req,send,0") != std::string::npos,
          "flits must leave the port in Xfer");
    Check(a.port.IsIdle() && b.port.IsIdle() && a.port.Credits<CHIReqFlit>() == 1,
          "port drain must include delayed flits and returned credits");
}

template <class Flit>
void DepthStatsTest(bool reverse) {
    Fixture f(reverse);
    Endpoint a("a");
    Endpoint b("b");
    a.port.SetParams<Flit>({16, 1, 2, 3});
    b.port.SetParams<Flit>({2, 3, 3, 2});
    Register(f, a, b);
    unsigned a_received = 0;
    unsigned b_received = 0;
    a.work = [&] {
        if (f.cycle == 0) {
            for (unsigned i = 0; i < 16; ++i) {
                Flit flit;
                flit.flit_txn = i;
                Check(a.port.Send(flit), "TX depth must be independent of RX credit capacity");
            }
            Check(!a.port.Send(Flit{}) && !a.port.Send(Flit{}), "full TX must reject each call");
            Check(!a.port.CanSend<Flit>() && !a.port.CanSend<Flit>(), "full TX must remain full");
            Check(a.port.Stats<Flit>().send_rejects == 2,
                  "CanSend must not count as a rejected Send");
        }
        Flit flit;
        if (a.port.Recv(flit))
            Check(flit.flit_txn == 100 + a_received++, "reverse traffic order");
        a.finished = a_received == 2;
    };
    b.work = [&] {
        if (f.cycle == 0) {
            for (unsigned i = 0; i < 2; ++i) {
                Flit flit;
                flit.flit_txn = 100 + i;
                Check(b.port.Send(flit), "reverse TX acceptance");
            }
            Check(!b.port.CanSend<Flit>(), "TX capacity must not use the larger RX depth");
        }
        if (f.cycle == 19) {
            Check(b_received == 0 && b.port.RxSize<Flit>() == 3 &&
                      b.port.Stats<Flit>().rx_flits == 3 && b.port.Stats<Flit>().rx_peak == 3,
                  "RX statistics must count arrival before application consumption");
            Check(a.port.TxSize<Flit>() == 13 && a.port.Credits<Flit>() == 0,
                  "peer RX depth must bound outstanding flits independently of TX depth");
        }
        Flit flit;
        if (f.cycle >= 20 && b.port.Recv(flit))
            Check(flit.flit_txn == b_received++, "forward traffic order");
        b.finished = b_received == 16;
    };
    f.Run(500);
    const auto& a_stats = a.port.Stats<Flit>();
    const auto& b_stats = b.port.Stats<Flit>();
    Check(a_stats.tx_flits == 16 && a_stats.rx_flits == 2 && a_stats.tx_peak == 16 &&
              a_stats.rx_peak == 1 && a_stats.send_rejects == 2 && a_stats.credit_stalls > 0,
          "A statistics must describe its own endpoint");
    Check(b_stats.tx_flits == 2 && b_stats.rx_flits == 16 && b_stats.tx_peak == 2 &&
              b_stats.rx_peak == 3 && b_stats.send_rejects == 0,
          "B statistics must describe its own endpoint");
    Check(a.port.Credits<Flit>() == 3 && b.port.Credits<Flit>() == 1,
          "drained credit counts must match peer RX depths");
}

struct Counts {
    unsigned queued = 0;
    unsigned received = 0;
};
constexpr unsigned kMessages = 20;
template <class Flit>
void Traffic(CHIPort& port, Counts& counts, unsigned local_base, unsigned remote_base,
             std::uint64_t cycle, bool hold_receiver) {
    while (counts.queued < kMessages && port.CanSend<Flit>()) {
        Flit flit;
        flit.flit_txn = local_base + counts.queued;
        if constexpr (std::is_same<Flit, CHIReqFlit>::value)
            flit.flit_opcode = CHI_REQ_Opcode_ReadNoSnp;
        else if constexpr (std::is_same<Flit, CHIRspFlit>::value)
            flit.flit_opcode = CHI_RSP_Opcode_Comp;
        else if constexpr (std::is_same<Flit, CHISnpFlit>::value)
            flit.flit_opcode = CHI_SNP_Opcode_SnpShared;
        else {
            flit.flit_opcode = CHI_DAT_Opcode_CompData;
            flit.flit_data.fill(0x80000000U | flit.flit_txn);
        }
        Check(port.Send(flit), "CanSend and Send disagree");
        ++counts.queued;
    }
    if ((!hold_receiver || cycle >= 20) && cycle % 2 == 0) {
        Flit received;
        if (port.Recv(received)) {
            Check(received.flit_txn == remote_base + counts.received,
                  "bidirectional channel lost or reordered data");
            if constexpr (std::is_same<Flit, CHIDatFlit>::value)
                for (const auto word : received.flit_data)
                    Check(word == (0x80000000U | received.flit_txn), "DAT payload corruption");
            ++counts.received;
        }
    }
}
bool Finished(const std::array<Counts, 4>& counts) {
    for (const auto& channel : counts)
        if (channel.queued != kMessages || channel.received != kMessages)
            return false;
    return true;
}
template <class Flit>
void CheckDrained(const CHIPort& port, unsigned local_depth, unsigned peer_depth) {
    Check(port.Credits<Flit>() == peer_depth, "TX credits must use the peer's RX depth");
    Check(port.TxSize<Flit>() == 0 && port.RxSize<Flit>() == 0, "undrained FIFOs");
    const auto& stats = port.Stats<Flit>();
    Check(stats.tx_flits == kMessages && stats.rx_flits == kMessages &&
              stats.rx_peak <= local_depth && stats.tx_peak <= local_depth &&
              stats.send_rejects == 0,
          "per-channel packet accounting or FIFO bound failed");
}
std::pair<std::uint64_t, std::string> AllChannelsTest(bool reverse, unsigned a_depth,
                                                      unsigned b_depth) {
    Fixture f(reverse);
    Endpoint a("a", {a_depth, a_depth, 2, 3});
    Endpoint b("b", {b_depth, b_depth, 3, 2});
    Register(f, a, b);
    std::array<Counts, 4> ac{};
    std::array<Counts, 4> bc{};
    a.work = [&] {
        const auto cycle = f.cycle;
        Traffic<CHIReqFlit>(a.port, ac[0], 100, 200, cycle, false);
        Traffic<CHIRspFlit>(a.port, ac[1], 300, 400, cycle, false);
        Traffic<CHISnpFlit>(a.port, ac[2], 500, 600, cycle, false);
        Traffic<CHIDatFlit>(a.port, ac[3], 700, 800, cycle, false);
        if (cycle == 18 && b_depth == 1) {
            Check(a.port.Stats<CHIReqFlit>().tx_flits == 1 &&
                      a.port.Stats<CHIReqFlit>().credit_stalls > 0 &&
                      a.port.Stats<CHIRspFlit>().tx_flits > 1 &&
                      a.port.Stats<CHISnpFlit>().tx_flits > 1 &&
                      a.port.Stats<CHIDatFlit>().tx_flits > 1,
                  "REQ backpressure must not block the other three channels");
        }
        a.finished = Finished(ac);
    };
    b.work = [&] {
        const auto cycle = f.cycle;
        Traffic<CHIReqFlit>(b.port, bc[0], 200, 100, cycle, true);
        Traffic<CHIRspFlit>(b.port, bc[1], 400, 300, cycle, false);
        Traffic<CHISnpFlit>(b.port, bc[2], 600, 500, cycle, false);
        Traffic<CHIDatFlit>(b.port, bc[3], 800, 700, cycle, false);
        b.finished = Finished(bc);
    };
    f.Run(1000);
    CheckDrained<CHIReqFlit>(a.port, a_depth, b_depth);
    CheckDrained<CHIRspFlit>(a.port, a_depth, b_depth);
    CheckDrained<CHISnpFlit>(a.port, a_depth, b_depth);
    CheckDrained<CHIDatFlit>(a.port, a_depth, b_depth);
    CheckDrained<CHIReqFlit>(b.port, b_depth, a_depth);
    CheckDrained<CHIRspFlit>(b.port, b_depth, a_depth);
    CheckDrained<CHISnpFlit>(b.port, b_depth, a_depth);
    CheckDrained<CHIDatFlit>(b.port, b_depth, a_depth);
    return {f.executed, f.output.str()};
}

std::string FabricTest(bool reverse) {
    Fixture f(reverse);
    const CHIChannelParams config{1, 1, 1, 1};
    Endpoint rn("rn", config);
    Endpoint fabric("fabric", config);
    CHIPort fabric_out("fabric.out");
    fabric_out.SetParams(Channels(config));
    fabric.extra = &fabric_out;
    Endpoint hn("hn", config);
    rn.port.Connect(fabric.port);
    fabric_out.Connect(hn.port);
    f.endpoints = {&rn, &fabric, &hn};
    rn.work = [&] {
        if (!rn.finished) {
            CHIReqFlit request;
            request.flit_opcode = CHI_REQ_Opcode_ReadNoSnp;
            request.flit_txn = 0xABC;
            rn.finished = rn.port.Send(request);
        }
    };
    fabric.work = [&] {
        if (fabric.port.CanRecv<CHIReqFlit>() && fabric_out.CanSend<CHIReqFlit>()) {
            CHIReqFlit request;
            Check(fabric.port.Recv(request) && fabric_out.Send(request), "fabric forward failed");
            Check(f.cycle == 3, "first hop visibility");
            fabric.finished = true;
        }
    };
    hn.work = [&] {
        CHIReqFlit request;
        if (hn.port.Recv(request)) {
            Check(request.flit_txn == 0xABC && f.cycle == 4,
                  "a flit must not cross two components in one cycle");
            hn.finished = true;
        }
    };
    f.Run(50);
    Check(f.output.str().find("3,fabric.out.req,send,2748") != std::string::npos,
          "fabric output must transfer in Xfer");
    return f.output.str();
}
}  // namespace

int main() {
    try {
        BindingTest();
        TimingTest();
        IndependentParamsTest(false);
        IndependentParamsTest(true);
        OverflowTest();
        for (bool reverse : {false, true}) {
            DepthStatsTest<CHIReqFlit>(reverse);
            DepthStatsTest<CHIRspFlit>(reverse);
            DepthStatsTest<CHISnpFlit>(reverse);
            DepthStatsTest<CHIDatFlit>(reverse);
        }
        for (const auto& depths : {std::pair<unsigned, unsigned>{1, 1}, {2, 3}, {15, 15}})
            Check(AllChannelsTest(false, depths.first, depths.second) ==
                      AllChannelsTest(true, depths.first, depths.second),
                  "model and port traversal order changed CA behavior");
        Check(FabricTest(false) == FabricTest(true), "fabric traversal order changed trace");
        std::cout << "chi_port passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
