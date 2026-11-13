#include "chi_vip_test_support.hpp"
#include "rules.hpp"

namespace vip_test {
void Contract();
void Requests() {
    ValidateRuleTables();
    // G.b Tables B4.37, B4.42 and B4.43, independent expected initial-state masks.
    const std::array<std::pair<unsigned, unsigned>, 20> cases = {
        {{4, 1},     {3, 1},     {1, 33},    {2, 33},    {7, 127},   {0x26, 33}, {0x1d, 1},
         {0x1c, 1},  {0x19, 1},  {0x18, 1},  {0x1b, 24}, {0x1a, 64}, {0x17, 24}, {0x15, 4},
         {0x0b, 63}, {0x0c, 55}, {0x0d, 39}, {8, 7},     {9, 39},    {10, 127}}};
    unsigned executed = 0;
    std::set<std::string> hits;
    for (const auto& [op, mask] : cases)
        for (unsigned state = 0; state < 7; ++state) {
            if (!(mask & (1u << state))) {
                Harness invalid;
                invalid.State(static_cast<CacheState>(state));
                TxnRequest q;
                q.opcode = op;
                invalid.rn.Submit(q);
                Throws(ErrorKind::kInvalidRequest, [&] { invalid.Tick(); });
                continue;
            }
            std::vector<unsigned> responses{0};
            if (ReadOp(op))
                responses = op == 1      ? std::vector<unsigned>{1, 2, 6, 7}
                            : op == 0x26 ? std::vector<unsigned>{1, 2, 6}
                            : op == 7    ? std::vector<unsigned>{2, 6}
                            : op == 2    ? std::vector<unsigned>{1, 2}
                                         : std::vector<unsigned>{0, 2};
            for (const auto resp : responses) {
                const unsigned styles = Immediate(op) ? 3 : CopyBack(op) && op != 0x1a ? 2 : 1;
                for (unsigned style = 0; style < styles; ++style) {
                    Harness h;
                    h.State(static_cast<CacheState>(state));
                    const auto first = h.sent.size();
                    TxnRequest req;
                    req.opcode = op;
                    req.data = Pattern();
                    const auto result = h.Execute(req, resp, style);
                    unsigned expected = state;
                    if (ReadOp(op)) {
                        expected = op == 4 || op == 3 ? 0
                                   : resp == 1        ? 1
                                   : resp == 2        ? 2
                                   : resp == 6        ? 3
                                                      : 4;
                        if (op == 7 && (state == 3 || state == 4 || state == 6))
                            expected = 3;
                        if (op == 2 && state == 5)
                            expected = 2;
                        Expect(result.data == Pattern(), "read data differs");
                    }
                    if (op == 0x0b)
                        expected = state == 0 || state == 5 ? 5 : state == 3 || state == 4 ? 3 : 2;
                    if (op == 0x0c)
                        expected = 3;
                    if (op == 0x1b || op == 0x1a || op == 0x15 || op == 0x0d || op == 9 || op == 10)
                        expected = 0;
                    if (op == 0x17)
                        expected = state == 4 ? 1 : 2;
                    Expect(static_cast<unsigned>(result.state) == expected,
                           "wrong final state for opcode=" + std::to_string(op));
                    unsigned packets = 0, acks = 0;
                    for (auto i = first; i < h.sent.size(); ++i) {
                        if (const auto* q = std::get_if<CHIReqFlit>(&h.sent[i].flit)) {
                            Expect(q->flit_allow_retry == 1 && q->flit_ns == 1 && q->flit_size == 6,
                                   "request fields");
                        }
                        if (const auto* d = std::get_if<CHIDatFlit>(&h.sent[i].flit)) {
                            ++packets;
                            Expect(d->flit_txn == 777 && d->flit_tgt == 1, "write DBID mapping");
                            Expect(d->flit_opcode == (CopyBack(op) ? 2u : 3u),
                                   "wrong write opcode");
                            const auto expected_be =
                                state == 6 && d->flit_data_id == 2 ? 0ull : 0xffffffffull;
                            Expect(d->flit_be == expected_be, "write BE mismatch");
                        }
                        if (const auto* a = std::get_if<CHIRspFlit>(&h.sent[i].flit)) {
                            if (a->flit_opcode == CHI_RSP_Opcode_CompAck) {
                                ++acks;
                                Expect(a->flit_txn == 777, "CompAck DBID");
                            }
                        }
                    }
                    const bool ack = (ReadOp(op) && op != 3 && op != 4) || op == 0x0b ||
                                     op == 0x0c || (CopyBack(op) && style);
                    Expect(acks == (ack ? 1u : 0u), "wrong CompAck count");
                    Expect(packets == (Immediate(op) || (CopyBack(op) && !style) ? 2u : 0u),
                           "wrong write packet count");
                    h.Drain();
                    for (const auto& observation : h.observed)
                        hits.insert(observation.rule);
                    ++executed;
                }
            }
        }
    for (unsigned size : {1u, 2u, 4u, 8u, 16u, 32u, 64u})
        for (unsigned base : {0u, 32u}) {
            Harness h;
            TxnRequest q;
            q.opcode = CHI_REQ_Opcode_ReadNoSnp;
            q.size = size;
            q.address = base + 7;
            q.exp_comp_ack = true;
            const auto c = h.Execute(q);
            const auto offset = (q.address & 63) & ~(size - 1);
            const auto mask = size == 64 ? ~0ull : ((1ull << size) - 1) << offset;
            Expect(c.valid == mask, "Normal memory Size-aligned window");
            for (unsigned i = 0; i < 64; ++i)
                if ((mask >> i) & 1)
                    Expect(c.data[i] == Pattern()[i], "narrow read byte");
            q.opcode = CHI_REQ_Opcode_WriteNoSnpPtl;
            q.exp_comp_ack = false;
            q.byte_enable = mask;
            q.data = Pattern();
            h.Execute(q);
            h.Drain();
        }
    // Sparse immediate writes preserve disabled bytes and still send both beats.
    for (unsigned op : {0x18u, 0x1cu})
        for (auto mask : {0ull, 1ull, 0x8000000000000001ull}) {
            Harness h;
            TxnRequest q;
            q.opcode = op;
            q.byte_enable = mask;
            q.data.fill(0xa5);
            h.Execute(q);
            for (const auto& a : h.sent)
                if (const auto* d = std::get_if<CHIDatFlit>(&a.flit)) {
                    Expect(d->flit_be == ((mask >> (16 * d->flit_data_id)) & 0xffffffffull),
                           "sparse BE");
                    for (unsigned i = 0; i < 32; ++i)
                        Expect(((d->flit_data[i / 4] >> (8 * (i % 4))) & 255) ==
                                   (((d->flit_be >> i) & 1) ? 0xa5 : 0),
                               "sparse byte");
                }
        }
    for (const auto& rule : RuleCatalog())
        if (rule.category == RuleClass::kOpcode || rule.category == RuleClass::kRequestStart ||
            rule.category == RuleClass::kRequestResponse)
            Expect(hits.count(rule.id), "unexercised request rule: " + rule.id);
    std::cout << "request state/response cases=" << executed << '\n';
}
void Snoops() {
    std::set<std::string> hits;
    const unsigned ops[] = {3, 1, 2, 4, 7, 8, 9, 10};
    // Expected next states and data policy, transcribed from B4.45-.48.
    const unsigned next[8][7] = {
        {0, 1, 2, 3, 4, 5, 6}, {0, 1, 1, 4, 4, 0, 0}, {0, 1, 1, 4, 4, 0, 0}, {0, 1, 1, 4, 4, 0, 0},
        {0, 0, 0, 0, 0, 0, 0}, {0, 1, 2, 2, 1, 0, 0}, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}};
    unsigned count = 0;
    for (unsigned row = 0; row < 8; ++row)
        for (unsigned state = 0; state < 7; ++state)
            for (unsigned ret = 0; ret < (row < 5 ? 2u : 1u); ++ret)
                for (unsigned no_sd = row < 4 ? 0 : 1; no_sd < 2; ++no_sd) {
                    Harness h;
                    h.State(static_cast<CacheState>(state));
                    auto initial = h.rn.InspectLine(0);
                    const auto first = h.sent.size();
                    h.Rx(Snoop(ops[row], 0, 999, ret, no_sd));
                    h.Tick();
                    h.Drain();
                    for (const auto& observation : h.observed)
                        hits.insert(observation.rule);
                    auto expected = next[row][state];
                    if (no_sd && row >= 1 && row <= 3 && expected == 4)
                        expected = 1;
                    const bool dirty = state == 3 || state == 4 || state == 6;
                    const bool data =
                        row != 7 && (dirty || (row < 5 && ret && (state == 1 || state == 2)));
                    const bool pd = dirty && row != 0 && row != 7 && expected != 4;
                    const unsigned low = expected == 0   ? 0
                                         : expected == 1 ? 1
                                         : expected == 4 ? 3
                                                         : 2;
                    const unsigned resp = low | (pd ? 4u : 0u);
                    const auto line = h.rn.InspectLine(0);
                    Expect((line ? static_cast<unsigned>(line->state) : 0) == expected,
                           "snoop final state");
                    Expect(h.sent.size() - first == (data ? 2u : 1u), "snoop packet count");
                    for (auto i = first; i < h.sent.size(); ++i) {
                        if (data) {
                            const auto& d = std::get<CHIDatFlit>(h.sent[i].flit);
                            Expect(d.flit_resp == resp && d.flit_txn == 999 && d.flit_tgt == 1,
                                   "snoop DAT response");
                            Expect(d.flit_opcode == (state == 6 ? 5u : 1u), "snoop partial opcode");
                            const auto mask = state == 6 ? 0xffffffffull : ~0ull;
                            Expect(d.flit_be == ((mask >> (16 * d.flit_data_id)) & 0xffffffffull),
                                   "snoop byte enable");
                            for (unsigned b = 0; b < 32; ++b) {
                                const auto byte = (d.flit_data[b / 4] >> (8 * (b % 4))) & 255;
                                Expect(byte == (((d.flit_be >> b) & 1)
                                                    ? initial->data[d.flit_data_id * 16 + b]
                                                    : 0),
                                       "snoop snapshot byte");
                            }
                        } else {
                            const auto& f = std::get<CHIRspFlit>(h.sent[i].flit);
                            Expect(f.flit_opcode == 1 && f.flit_resp == resp && f.flit_txn == 999,
                                   "snoop response");
                        }
                    }
                    ++count;
                }
    for (const auto& rule : RuleCatalog())
        if (rule.category == RuleClass::kSnoopTransition)
            Expect(hits.count(rule.id), "unexercised snoop rule: " + rule.id);
    std::cout << "snoop matrix cases=" << count << '\n';
}
void Hazards() {
    {
        Harness h;
        TxnRequest req;
        Handle handle;
        auto q = h.Issue(req, &handle);
        h.Rx(ReadData(q, Pattern(), 0, 6));
        h.Tick();
        const auto before = h.sent.size();
        h.Rx(Snoop(7));
        h.Tick();
        Expect(h.sent.size() == before, "snoop failed to wait for remaining read data");
        h.Rx(ReadData(q, Pattern(), 2, 6));
        h.Tick();
        Expect(h.sent.size() == before + 2, "deferred snoop did not wake");
        Expect(std::get<CHIDatFlit>(h.sent.back().flit).flit_resp == 4,
               "deferred snoop lost dirty data");
        const auto result = h.Done(handle);
        Expect(result.state == CacheState::kI && result.data == Pattern(),
               "refill/snoop completion");
        Expect(std::get<CHIRspFlit>(h.sent.back().flit).flit_opcode == 2, "CompAck after snoop");
    }
    for (unsigned op : {0x1bu, 0x17u, 0x15u}) {
        Harness h;
        h.State(op == 0x15 ? CacheState::kUC : CacheState::kUD);
        TxnRequest req;
        req.opcode = op;
        Handle handle;
        auto q = h.Issue(req, &handle);
        h.Rx(Snoop(7));
        h.Tick();
        h.Tick();
        auto before = h.sent.size();
        h.Reply(q);
        h.Done(handle);
        unsigned beats = 0;
        for (auto i = before; i < h.sent.size(); ++i)
            if (auto d = std::get_if<CHIDatFlit>(&h.sent[i].flit)) {
                ++beats;
                Expect(d->flit_resp == 0 && d->flit_be == 0,
                       "CopyBack did not reflect invalidation");
                for (auto word : d->flit_data)
                    Expect(word == 0, "canceled CopyBack data not zero");
            }
        Expect(beats == 2, "canceled CopyBack elided packets");
    }
    {
        Harness h;
        h.State(CacheState::kSC);
        TxnRequest req;
        req.opcode = 0x0b;
        Handle handle;
        auto q = h.Issue(req, &handle);
        h.Rx(Snoop(7));
        h.Tick();
        h.Tick();
        h.Reply(q);
        Expect(h.Done(handle).state == CacheState::kUCE,
               "CleanUnique resurrected invalidated data");
        auto data = Pattern();
        data[17] = 0xab;
        h.Done(h.rn.Store(0, data, 1ull << 17).handle);
        Expect(h.rn.InspectLine(0)->state == CacheState::kUDP &&
                   h.rn.InspectLine(0)->valid == (1ull << 17),
               "Store from UCE");
    }
    {
        Harness h;
        h.State(CacheState::kUD);
        h.hold = true;
        h.Rx(Snoop(3));
        h.Tick();
        const auto id = h.rn.Store(0, Line{}, ~0ull).handle;
        h.Tick();
        Expect(!h.completed.count(id), "local store overtook snoop response");
        h.hold = false;
        h.Transmit();
        h.Done(id);
        for (const auto& a : h.sent)
            if (const auto* d = std::get_if<CHIDatFlit>(&a.flit))
                if (d->flit_opcode == 1)
                    Expect((d->flit_data[0] & 255) == Pattern()[d->flit_data_id * 16],
                           "snoop snapshot mutated");
    }
}
void Retry() {
    for (bool grant_first : {false, true}) {
        Harness h;
        TxnRequest req;
        req.opcode = 4;
        req.order = 3;
        req.memory_type = MemoryType::kDeviceNRnE;
        Handle handle;
        auto original = h.Issue(req, &handle);
        req.address = 64;
        auto later = h.rn.Submit(req);
        Expect(later.accepted, "later ordered request acceptance");
        auto retry = Response(original, CHI_RSP_Opcode_RetryAck);
        retry.flit_pcrd_type = 5;
        auto grant = Response(original, CHI_RSP_Opcode_PCrdGrant);
        grant.flit_pcrd_type = 5;
        grant.flit_txn = 4095;
        h.Rx(grant_first ? grant : retry);
        h.Tick();
        Expect(h.sent.size() == 1, "ordered successor/retry issued too early");
        h.Rx(grant_first ? retry : grant);
        h.Tick();
        Expect(h.sent.size() == 2, "retry did not consume credit");
        const auto resent = std::get<CHIReqFlit>(h.sent.back().flit);
        Expect(resent.flit_txn == original.flit_txn && resent.flit_address == 0 &&
                   resent.flit_order == 3 && resent.flit_allow_retry == 0 &&
                   resent.flit_pcrd_type == 5,
               "retry attributes/history");
        h.Rx(Response(resent, CHI_RSP_Opcode_ReadReceipt));
        h.Tick();
        Expect(h.sent.size() == 3, "Endpoint Order did not release on receipt");
        auto successor = std::get<CHIReqFlit>(h.sent.back().flit);
        Expect(successor.flit_address == 64, "wrong ordered successor");
        h.Rx(ReadData(resent, Pattern(), 2));
        h.Rx(ReadData(resent, Pattern(), 0));
        h.Reply(successor);
        h.Done(handle);
        h.Done(later.handle);
        h.Drain();
    }
    {
        Harness h;
        CHIRspFlit f;
        f.flit_opcode = 7;
        f.flit_src = 1;
        f.flit_pcrd_type = 3;
        h.Rx(f);
        h.Rx(f);
        h.Tick();
        Expect(h.sent.size() == 2, "unused credits not returned");
        for (auto& a : h.sent) {
            auto q = std::get<CHIReqFlit>(a.flit);
            Expect(q.flit_opcode == 5 && q.flit_pcrd_type == 3 && q.flit_tgt == 1,
                   "PCrdReturn accounting");
        }
        h.Drain();
    }
}
void Resources() {
    RNConfig cfg;
    cfg.cache_lines = 1;
    cfg.request_slots = 1;
    cfg.pending_operations = 4;
    Harness h(cfg);
    MemoryPeer peer;
    peer.deliver = [&](Flit f) { h.Rx(std::move(f)); };
    h.peer = [&](const SendAction& a) { peer.Receive(a.flit); };
    auto data = Pattern();
    data[5] = 0x91;
    h.Done(h.rn.Store(0, data, 1ull << 5).handle);
    h.Done(h.rn.Load(64, 64).handle);  // Must use the reserved maintenance slot.
    Expect(peer.At(0)[5] == 0x91, "dirty LRU replacement lost data");
    Expect(h.Done(h.rn.Load(0, 64).handle).data[5] == 0x91, "replacement reload stale");
    h.Drain();
    {
        Harness g(cfg);
        TxnRequest req;
        req.opcode = 4;
        Handle id;
        auto q = g.Issue(req, &id);
        for (unsigned i = 0; i < 3; ++i) {
            req.address = (i + 1) * 64;
            Expect(g.rn.Submit(req).accepted, "pending capacity");
        }
        Expect(!g.rn.Submit(req).accepted, "pending limit did not backpressure");
        g.Rx(Snoop(7, 0));
        g.Tick();
        Expect(std::holds_alternative<CHIRspFlit>(g.sent.back().flit),
               "full requests blocked snoop");
        g.Reply(q);
        g.Done(id);
        g.peer = [&](const SendAction& a) {
            if (auto f = std::get_if<CHIReqFlit>(&a.flit))
                g.Reply(*f);
        };
        // A successor may have issued in the completion cycle before enabling the peer.
        for (const auto& a : std::vector<SendAction>(g.sent))
            if (auto f = std::get_if<CHIReqFlit>(&a.flit))
                if (f->flit_txn != q.flit_txn)
                    g.Reply(*f);
        g.Drain();
    }
}
void Negative() {
    {
        RN rn;
        TxnRequest q;
        q.opcode = CHI_REQ_Opcode_AtomicSwap;
        Throws(ErrorKind::kUnsupportedFeature, [&] { rn.Submit(q); });
    }
    {
        RN rn;
        TxnRequest q;
        q.order = 1;
        Throws(ErrorKind::kInvalidRequest, [&] { rn.Submit(q); });
    }
    {
        RN rn;
        TxnRequest q;
        q.opcode = 0x0c;
        q.byte_enable = 1;
        Throws(ErrorKind::kInvalidRequest, [&] { rn.Submit(q); });
    }
    {
        Harness h;
        h.State(CacheState::kUD);
        TxnRequest q;
        q.opcode = 8;
        h.rn.Submit(q);
        Throws(ErrorKind::kInvalidRequest, [&] { h.Tick(); });
    }
    {
        Harness h;
        CHIRspFlit f;
        f.flit_src = 1;
        f.flit_opcode = 4;
        h.Rx(f);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    for (unsigned variant = 0; variant < 8; ++variant) {
        Harness h;
        TxnRequest req;
        auto q = h.Issue(req);
        auto f = ReadData(q, Pattern(), 0);
        if (variant == 0)
            f.flit_tgt = 3;
        if (variant == 1)
            f.flit_data_id = 1;
        if (variant == 2)
            f.flit_ccid = 1;
        if (variant == 3)
            f.flit_resp = 5;
        if (variant == 4)
            f.flit_resp_err = 1;
        if (variant == 5)
            f.flit_txn = 200;
        if (variant == 6)
            f.flit_be = 1ull << 32;
        if (variant == 7)
            f.flit_num_dat = 1;
        h.Rx(f);
        Throws(variant == 7 ? ErrorKind::kUnsupportedFeature : ErrorKind::kProtocolViolation,
               [&] { h.Tick(); });
    }
    {
        Harness h;
        TxnRequest req;
        auto q = h.Issue(req);
        auto d = ReadData(q, Pattern(), 0);
        h.Rx(d);
        h.Rx(d);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        TxnRequest req;
        req.opcode = 0x1c;
        auto q = h.Issue(req);
        auto f = Response(q, 6);
        h.Rx(f);
        h.Rx(f);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        h.Rx(Snoop(8, 0, 999, true));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        h.Rx(Snoop(CHI_SNP_Opcode_SnpSharedFwd));
        Throws(ErrorKind::kUnsupportedFeature, [&] { h.Tick(); });
    }
    {
        Harness h;
        h.rn.OnEvent(Event::Sent(100));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        h.hold = true;
        TxnRequest req;
        h.rn.Submit(req);
        h.Tick();
        h.rn.OnEvent(Event::Sent(h.pending.front().token));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        RNConfig c;
        c.timeout_cycles = 4;
        Harness h(c);
        TxnRequest req;
        auto q = h.Issue(req);
        h.Rx(ReadData(q, Pattern(), 0));
        h.Tick();
        Throws(ErrorKind::kTimeout, [&] {
            for (unsigned i = 0; i < 5; ++i)
                h.Tick();
        });
    }
    for (unsigned error : {2u, 3u}) {
        Harness h;
        TxnRequest req;
        auto c = h.Execute(req, error == 3 ? 5 : 2, 0, error);
        Expect(c.error == (error == 3 ? AccessError::kNonDataError : AccessError::kDataError),
               "error not delivered");
        Expect(h.rn.InspectLine(0)->state == (error == 3 ? CacheState::kI : CacheState::kUC),
               "error allocation rule");
        if (error == 2) {
            auto local = h.Done(h.rn.Load(0, 64).handle);
            Expect(local.error == AccessError::kDataError, "cached DERR lost");
            h.Rx(Snoop(7, 0, 999, true));
            h.Tick();
            Expect(std::get<CHIDatFlit>(h.sent.back().flit).flit_resp_err == 2, "snoop DERR lost");
        }
        h.Drain();
    }
    {
        Harness h;
        TxnRequest req;
        auto q = h.Issue(req);
        h.Rx(ReadData(q, Pattern(), 0, 2, 3));
        h.Rx(ReadData(q, Pattern(), 2, 2, 0));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
}
std::vector<std::string> StressRun(unsigned seed) {
    RNConfig cfg;
    cfg.cache_lines = 3;
    cfg.request_slots = 2;
    cfg.pending_operations = 16;
    Harness h(cfg);
    MemoryPeer peer;
    peer.deliver = [&](Flit f) { h.Rx(std::move(f)); };
    h.peer = [&](const SendAction& a) { peer.Receive(a.flit); };
    std::mt19937 rng(seed);
    std::map<std::uint64_t, Line> latest;
    std::map<Handle, std::pair<Line, std::uint64_t>> expected;
    for (unsigned step = 0; step < 1200; ++step) {
        const std::uint64_t address = (rng() % 12) * 64;
        auto& data = latest.try_emplace(address, Pattern(address)).first->second;
        if ((rng() & 3) != 0) {
            const unsigned byte = rng() % 64;
            auto changed = data;
            changed[byte] = static_cast<std::uint8_t>(rng());
            auto result = h.rn.Store(address, changed, 1ull << byte);
            if (result.accepted)
                data = changed;
        } else {
            auto result = h.rn.Load(address, 64);
            if (result.accepted)
                expected.emplace(result.handle, std::make_pair(data, address));
        }
        h.Tick();
    }
    h.Drain();
    for (const auto& [id, value] : expected)
        Expect(
            h.completed.at(id).data == value.first,
            "random load violated same-line causal data order at " + std::to_string(value.second));
    for (const auto& [address, data] : latest)
        Expect(h.Done(h.rn.Load(address, 64).handle).data == data, "final random data mismatch");
    h.Drain();
    Expect(peer.writes.empty() && peer.beats.empty(), "test peer resources leaked");
    std::vector<std::string> trace;
    for (const auto& o : h.observed)
        if (o.flit)
            trace.push_back(std::to_string(o.cycle) + o.action +
                            std::visit([](const auto& f) { return f.to_string(); }, *o.flit));
    return trace;
}
void Stress() {
    Expect(StressRun(0x51a7) == StressRun(0x51a7), "seed not deterministic");
}
std::vector<std::string> AdapterRun(bool reverse) {
    RNConfig cfg;
    cfg.request_slots = 2;
    cfg.cache_lines = 2;
    RN rn(cfg);
    CHIPort rn_port("rn"), hn_port("hn");
    rn_port.Connect(hn_port);
    CHIChannelParams params;
    params.tx_depth = 1;
    params.rx_depth = 1;
    rn_port.SetParams<CHIReqFlit>(params);
    rn_port.SetParams<CHIRspFlit>(params);
    rn_port.SetParams<CHIDatFlit>(params);
    hn_port.SetParams<CHIReqFlit>(params);
    hn_port.SetParams<CHIRspFlit>(params);
    hn_port.SetParams<CHIDatFlit>(params);
    TransportAdapter adapter(rn, rn_port);
    std::array<std::deque<Flit>, 4> pending;
    MemoryPeer peer;
    peer.deliver = [&](Flit f) {
        const auto index = f.index();
        pending[index].push_back(std::move(f));
    };
    auto first = rn.Load(0, 64);
    auto second = rn.Load(64, 64);
    std::map<Handle, Completion> done;
    std::vector<std::string> trace;
    for (unsigned cycle = 0; cycle < 300; ++cycle) {
        auto hn_work = [&] {
            hn_port.Work();
            CHIReqFlit req;
            CHIRspFlit rsp;
            CHIDatFlit dat;
            // Intermittent receive stalls exercise backpressure without changing traversal
            // dependence.
            if (cycle % 4 == 0) {
                if (hn_port.Recv(req))
                    peer.Receive(req);
                if (hn_port.Recv(rsp))
                    peer.Receive(rsp);
                if (hn_port.Recv(dat))
                    peer.Receive(dat);
            }
            for (auto& queue : pending)
                while (!queue.empty()) {
                    if (!std::visit([&](const auto& f) { return hn_port.Send(f); }, queue.front()))
                        break;
                    queue.pop_front();
                }
        };
        if (reverse) {
            hn_work();
            adapter.Work(cycle);
        } else {
            adapter.Work(cycle);
            hn_work();
        }
        if (reverse) {
            hn_port.Xfer();
            adapter.Xfer();
        } else {
            adapter.Xfer();
            hn_port.Xfer();
        }
        if (auto q = rn_port.Transmitted<CHIReqFlit>())
            trace.push_back(std::to_string(cycle) + q->to_string());
        if (auto q = rn_port.Transmitted<CHIRspFlit>())
            trace.push_back(std::to_string(cycle) + q->to_string());
        Completion c;
        while (rn.PollCompletion(c))
            done[c.handle] = c;
        if (done.size() == 2 && adapter.IsIdle() && hn_port.IsIdle())
            break;
    }
    Expect(done.size() == 2 && done.at(first.handle).data == Pattern() &&
               done.at(second.handle).data == Pattern(64),
           "adapter reads");
    Expect(adapter.IsIdle() && hn_port.IsIdle(), "adapter/credit drain");
    return trace;
}
void Adapter() {
    Expect(AdapterRun(false) == AdapterRun(true), "Work/Xfer traversal changed trace");
}
}  // namespace vip_test
int main(int argc, char** argv) {
    try {
        vip_test::Expect(argc == 2, "expected test case");
        const std::string name = argv[1];
        if (name == "requests")
            vip_test::Requests();
        else if (name == "snoops")
            vip_test::Snoops();
        else if (name == "hazards")
            vip_test::Hazards();
        else if (name == "retry")
            vip_test::Retry();
        else if (name == "resources")
            vip_test::Resources();
        else if (name == "negative")
            vip_test::Negative();
        else if (name == "stress")
            vip_test::Stress();
        else if (name == "adapter")
            vip_test::Adapter();
        else if (name == "contract")
            vip_test::Contract();
        else
            throw std::runtime_error("unknown test case");
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
