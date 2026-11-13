#pragma once

#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>

#include "rn.hpp"
#include "transport_adapter.hpp"

namespace vip_test {
using namespace esl;
using namespace esl::chi_vip;
inline void Expect(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F>
void Throws(ErrorKind kind, F function) {
    try {
        function();
    } catch (const VIPError& e) {
        Expect(e.Kind() == kind, "wrong diagnostic kind: " + std::string(e.what()));
        return;
    }
    throw std::runtime_error("expected diagnostic was not reported");
}
inline Line Pattern(std::uint64_t address = 0) {
    Line data{};
    for (unsigned i = 0; i < 64; ++i)
        data[i] = static_cast<std::uint8_t>((address / 64) * 19 + i * 7 + 3);
    return data;
}
inline CHIRspFlit Response(const CHIReqFlit& q, unsigned op, unsigned resp = 0, unsigned error = 0,
                           DBID dbid = 777) {
    CHIRspFlit f;
    f.flit_src = q.flit_tgt;
    f.flit_tgt = q.flit_src;
    f.flit_txn = q.flit_txn;
    f.flit_opcode = op;
    f.flit_resp = resp;
    f.flit_resp_err = error;
    f.flit_dbid = dbid;
    return f;
}
// Deliberately does not use production Pack/Unpack, response rules, or state helpers.
inline CHIDatFlit ReadData(const CHIReqFlit& q, const Line& data, unsigned id, unsigned resp = 2,
                           unsigned error = 0, bool cah = true) {
    CHIDatFlit f;
    f.flit_opcode = CHI_DAT_Opcode_CompData;
    f.flit_src = q.flit_tgt;
    f.flit_tgt = q.flit_src;
    f.flit_home_nid = q.flit_tgt;
    f.flit_txn = q.flit_txn;
    f.flit_dbid = 777;
    f.flit_resp = resp;
    f.flit_resp_err = error;
    f.flit_cah = cah;
    f.flit_data_id = id;
    f.flit_ccid = (q.flit_address >> 4) & 3;
    for (unsigned i = 0; i < 32; ++i)
        f.flit_data[i / 4] |= std::uint32_t(data[id * 16 + i]) << (8 * (i % 4));
    return f;  // BE=0 intentionally: read BE is inapplicable.
}
inline CHISnpFlit Snoop(unsigned op, std::uint64_t address = 0, TxnID id = 999, bool ret = false,
                        bool no_sd = true) {
    CHISnpFlit f;
    f.flit_src = 1;
    f.flit_txn = id;
    f.flit_opcode = op;
    f.flit_address = address >> 3;
    f.flit_ns = 1;
    f.flit_ret_to_src = ret;
    f.flit_do_not_go_to_sd = no_sd;
    return f;
}
inline bool ReadOp(unsigned op) {
    return op == 1 || op == 2 || op == 3 || op == 4 || op == 7 || op == 0x26;
}
inline bool Immediate(unsigned op) {
    return op == 0x18 || op == 0x19 || op == 0x1c || op == 0x1d;
}
inline bool CopyBack(unsigned op) {
    return op == 0x15 || op == 0x17 || op == 0x1a || op == 0x1b;
}
struct Harness {
    std::vector<Observation> observed;
    RN rn;
    std::uint64_t cycle = 0;
    bool hold = false;
    std::deque<SendAction> pending;
    std::vector<SendAction> sent;
    std::map<Handle, Completion> completed;
    std::function<void(const SendAction&)> peer;
    explicit Harness(RNConfig c = {})
        : rn(c, [this](const Observation& o) { observed.push_back(o); }) {}
    void Rx(Flit f) {
        rn.OnEvent(Event::Received(std::move(f)));
    }
    void Transmit() {
        while (!pending.empty()) {
            auto action = pending.front();
            pending.pop_front();
            sent.push_back(action);
            rn.OnEvent(Event::Accepted(action.token));
            rn.OnEvent(Event::Sent(action.token, cycle - 1));
            if (peer)
                peer(action);
        }
    }
    void Tick() {
        rn.BeginCycle(cycle++);
        auto actions = rn.TakeActions();
        for (auto& a : actions)
            pending.push_back(std::move(a));
        rn.Commit();
        Completion c;
        while (rn.PollCompletion(c))
            Expect(completed.emplace(c.handle, c).second, "duplicate completion");
        if (!hold)
            Transmit();
    }
    template <class F>
    void Until(F condition, unsigned limit = 1000) {
        for (unsigned i = 0; i < limit && !condition(); ++i)
            Tick();
        Expect(condition(), "no progress: " + rn.DescribeState());
    }
    Completion Done(Handle h) {
        Until([&] { return completed.count(h) != 0; });
        return completed.at(h);
    }
    void Drain() {
        Until([&] { return rn.IsIdle(); });
    }
    CHIReqFlit Issue(const TxnRequest& request, Handle* handle = nullptr) {
        auto accepted = rn.Submit(request);
        Expect(accepted.accepted, "Submit backpressured");
        if (handle)
            *handle = accepted.handle;
        const auto start = sent.size();
        Until([&] { return sent.size() > start; });
        for (auto i = start; i < sent.size(); ++i)
            if (auto f = std::get_if<CHIReqFlit>(&sent[i].flit))
                return *f;
        throw std::runtime_error("missing request");
    }
    void Reply(const CHIReqFlit& q, unsigned resp = 2, unsigned style = 0, unsigned error = 0) {
        if (ReadOp(q.flit_opcode)) {
            if (q.flit_order)
                Rx(Response(q, CHI_RSP_Opcode_ReadReceipt));
            const auto data = Pattern(q.flit_address & ~std::uint64_t{63});
            if (q.flit_size == 6) {
                Rx(ReadData(q, data, 2, resp, error));
                Rx(ReadData(q, data, 0, resp, error));
            } else
                Rx(ReadData(q, data, (q.flit_address & 32) ? 2 : 0, resp, error));
        } else if (Immediate(q.flit_opcode)) {
            if (style == 0)
                Rx(Response(q, CHI_RSP_Opcode_CompDBIDResp, 0, error));
            else if (style == 1) {
                Rx(Response(q, CHI_RSP_Opcode_DBIDResp));
                Tick();
                Rx(Response(q, CHI_RSP_Opcode_Comp, 0, error));
            } else {
                Rx(Response(q, CHI_RSP_Opcode_Comp, 0, error));
                Tick();
                Rx(Response(q, CHI_RSP_Opcode_DBIDResp));
            }
        } else if (CopyBack(q.flit_opcode)) {
            Rx(Response(q, style ? CHI_RSP_Opcode_Comp : CHI_RSP_Opcode_CompDBIDResp, 0, error));
        } else
            Rx(Response(q, CHI_RSP_Opcode_Comp,
                        (q.flit_opcode == 0x0b || q.flit_opcode == 0x0c) ? 2 : 0, error));
    }
    Completion Execute(TxnRequest req, unsigned resp = 2, unsigned style = 0, unsigned error = 0) {
        Handle id;
        const auto q = Issue(req, &id);
        Reply(q, resp, style, error);
        return Done(id);
    }
    void State(CacheState state, std::uint64_t address = 0) {
        if (state == CacheState::kI)
            return;
        TxnRequest q;
        q.address = address;
        if (state == CacheState::kUCE || state == CacheState::kUDP) {
            q.opcode = CHI_REQ_Opcode_CleanUnique;
            Execute(q);
            if (state == CacheState::kUDP)
                Done(rn.Store(address, Pattern(address), 0x00000000ffffffffull).handle);
        } else {
            q.opcode = CHI_REQ_Opcode_ReadShared;
            Execute(q, state == CacheState::kSC   ? 1
                       : state == CacheState::kUC ? 2
                       : state == CacheState::kUD ? 6
                                                  : 7);
        }
        Expect(rn.InspectLine(address)->state == state, "failed to establish initial state");
    }
};
// Independently implemented single-home test fixture. It can reorder separate
// DBID/Comp, reads the wire only, and never consults RN cache or rules.
struct MemoryPeer {
    std::map<std::uint64_t, Line> memory;
    std::map<DBID, CHIReqFlit> writes;
    std::map<DBID, unsigned> beats;
    std::set<DBID> acknowledgments;
    DBID next_dbid = 1024;
    std::function<void(Flit)> deliver;
    DBID Allocate() {
        for (unsigned i = 0; i < 4096; ++i) {
            const DBID candidate = next_dbid++ % 4096;
            if (!writes.count(candidate) && !acknowledgments.count(candidate))
                return candidate;
        }
        throw std::runtime_error("test peer DBID exhausted");
    }
    Line& At(std::uint64_t a) {
        a &= ~std::uint64_t{63};
        return memory.try_emplace(a, Pattern(a)).first->second;
    }
    void Receive(const Flit& flit) {
        if (const auto* q = std::get_if<CHIReqFlit>(&flit)) {
            if (q->flit_opcode == CHI_REQ_Opcode_PCrdReturn)
                return;
            if (ReadOp(q->flit_opcode)) {
                const DBID id = q->flit_exp_comp_ack ? Allocate() : 0;
                if (q->flit_exp_comp_ack)
                    acknowledgments.insert(id);
                auto reply = [&](unsigned data_id) {
                    auto f = ReadData(*q, At(q->flit_address), data_id);
                    f.flit_dbid = id;
                    deliver(f);
                };
                if (q->flit_order)
                    deliver(Response(*q, CHI_RSP_Opcode_ReadReceipt));
                if (q->flit_size == 6) {
                    reply(2);
                    reply(0);
                } else
                    reply((q->flit_address & 32) ? 2 : 0);
            } else if (Immediate(q->flit_opcode) || CopyBack(q->flit_opcode)) {
                const DBID id = Allocate();
                writes[id] = *q;
                deliver(Response(*q, CHI_RSP_Opcode_CompDBIDResp, 0, 0, id));
            } else {
                const DBID id = q->flit_exp_comp_ack ? Allocate() : 0;
                if (q->flit_exp_comp_ack)
                    acknowledgments.insert(id);
                deliver(Response(*q, CHI_RSP_Opcode_Comp,
                                 q->flit_opcode == 0x0b || q->flit_opcode == 0x0c ? 2 : 0, 0, id));
            }
        } else if (const auto* r = std::get_if<CHIRspFlit>(&flit)) {
            if (r->flit_opcode == CHI_RSP_Opcode_CompAck)
                Expect(acknowledgments.erase(r->flit_txn) == 1,
                       "test peer received unknown CompAck DBID");
        } else if (const auto* d = std::get_if<CHIDatFlit>(&flit)) {
            if (d->flit_opcode != CHI_DAT_Opcode_CopyBackWriteData &&
                d->flit_opcode != CHI_DAT_Opcode_NonCopyBackWriteData)
                return;
            Expect(writes.count(d->flit_txn), "test peer received unknown write DBID");
            const auto q = writes.at(d->flit_txn);
            auto& data = At(q.flit_address);
            for (unsigned i = 0; i < 32; ++i) {
                const auto value = static_cast<std::uint8_t>(d->flit_data[i / 4] >> (8 * (i % 4)));
                if ((d->flit_be >> i) & 1)
                    data[d->flit_data_id * 16 + i] = value;
                else
                    Expect(value == 0, "disabled write byte is not zero");
            }
            auto& count = beats[d->flit_txn];
            if (++count == (q.flit_size == 6 ? 2u : 1u)) {
                writes.erase(d->flit_txn);
                beats.erase(d->flit_txn);
            }
        }
    }
};
}  // namespace vip_test
