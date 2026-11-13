#include "chi_vip_test_support.hpp"

namespace vip_test {
void Contract() {
    {
        Harness h;
        auto f = Snoop(7);
        f.flit_fwd_nid = 2;
        h.Rx(f);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        TxnRequest q;
        auto sent = h.Issue(q);
        auto d = ReadData(sent, Pattern(), 0);
        d.flit_data_pull = 1;
        h.Rx(d);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        TxnRequest req;
        Handle id;
        auto q = h.Issue(req, &id);
        h.hold = true;
        h.Reply(q);
        h.Tick();
        h.Rx(Snoop(7));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        TxnRequest req;
        req.opcode = 0x19;
        auto q = h.Issue(req);
        h.hold = true;
        h.Reply(q);
        h.Tick();
        h.Rx(Snoop(7));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        TxnRequest req;
        req.opcode = 4;
        req.order = 2;
        Handle id;
        auto q = h.Issue(req, &id);
        h.Rx(ReadData(q, Pattern(), 0));
        h.Rx(ReadData(q, Pattern(), 2));
        h.Tick();
        h.Rx(Snoop(7));
        h.Tick();
        h.Rx(Response(q, 8));
        h.Done(id);
        h.Drain();
    }
    {
        RNConfig config;
        config.snoop_slots = 1;
        Harness h(config);
        h.hold = true;
        h.Rx(Snoop(7));
        h.Tick();
        Expect(!h.rn.CanReceiveSnoop(), "snoop slot recycled before send");
        Throws(ErrorKind::kProtocolViolation, [&] { h.Rx(Snoop(7, 64, 998)); });
        h.hold = false;
        h.Transmit();
        h.Drain();
        Expect(h.rn.CanReceiveSnoop(), "snoop slot leaked");
    }
    // A Home DBID is independent of the RN TxnID and may only be reused after
    // the associated terminal response has actually left the RN.
    {
        Harness h;
        TxnRequest q;
        q.opcode = 0x1c;
        auto first = h.Issue(q);
        q.address = 64;
        auto second = h.Issue(q);
        h.Rx(Response(first, 6));
        h.Rx(Response(second, 6));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        TxnRequest q;
        q.opcode = 0x1c;
        Handle one, two;
        auto first = h.Issue(q, &one);
        h.Rx(Response(first, 6));
        h.Tick();
        h.Tick();
        q.address = 64;
        auto second = h.Issue(q, &two);
        h.Rx(Response(second, 6));
        h.Tick();
        h.Tick();
        h.Rx(Response(second, 4));
        h.Rx(Response(first, 4));
        h.Done(one);
        h.Done(two);
        h.Drain();
    }
    for (bool comp_first : {false, true}) {
        Harness h;
        TxnRequest q;
        q.opcode = 0x1c;
        auto sent = h.Issue(q);
        h.Rx(Response(sent, comp_first ? 4 : 6, 0, 0, 777));
        h.Tick();
        h.Rx(Response(sent, comp_first ? 6 : 4, 0, 0, 778));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    // Actual transmission, not queue admission, releases a CompAck's resources.
    {
        RNConfig config;
        config.request_slots = 1;
        Harness h(config);
        TxnRequest q;
        Handle id;
        auto sent = h.Issue(q, &id);
        h.hold = true;
        h.Reply(sent);
        h.Tick();
        Expect(!h.completed.count(id) && h.pending.size() == 1, "completed before CompAck sent");
        const auto ack = h.pending.front();
        h.pending.pop_front();
        h.rn.OnEvent(Event::Accepted(ack.token));
        h.Tick();
        Expect(!h.completed.count(id), "TX acceptance released transaction");
        TxnRequest next;
        next.address = 64;
        auto second = h.rn.Submit(next);
        h.Tick();
        Expect(h.pending.empty(), "ordinary slot recycled before actual send");
        h.rn.OnEvent(Event::Sent(ack.token));
        h.Tick();
        Expect(h.completed.count(id) && h.pending.size() == 1,
               "send confirmation did not release slot");
        h.hold = false;
        h.Transmit();
        h.Reply(std::get<CHIReqFlit>(h.sent.back().flit));
        h.Done(second.handle);
        h.Drain();
    }
    // Commit boundary for local updates and completions.
    {
        Harness h;
        h.State(CacheState::kUC);
        auto changed = Pattern();
        changed[1] = 0xbb;
        const auto id = h.rn.Store(0, changed, 2).handle;
        h.rn.BeginCycle(h.cycle++);
        Expect(h.rn.InspectLine(0)->data[1] == Pattern()[1], "cache published during Work");
        Completion c;
        Expect(!h.rn.PollCompletion(c), "completion published during Work");
        Expect(h.rn.TakeActions().empty(), "local Store sent a request");
        h.rn.Commit();
        Expect(h.rn.InspectLine(0)->data[1] == 0xbb && h.rn.PollCompletion(c) && c.handle == id,
               "Commit failed to publish");
    }
    // Request Order survives retry for one line, while independent lines progress.
    {
        Harness h;
        TxnRequest q;
        q.opcode = 4;
        q.order = 2;
        Handle first;
        auto sent = h.Issue(q, &first);
        const auto second = h.rn.Submit(q);
        q.address = 4096;
        const auto independent = h.rn.Submit(q);
        auto retry = Response(sent, 3);
        retry.flit_pcrd_type = 4;
        h.Rx(retry);
        h.Tick();
        Expect(h.sent.size() == 2 && std::get<CHIReqFlit>(h.sent.back().flit).flit_address == 4096,
               "independent line stalled");
        h.Reply(std::get<CHIReqFlit>(h.sent.back().flit));
        h.Done(independent.handle);
        auto grant = Response(sent, 7);
        grant.flit_pcrd_type = 4;
        h.Rx(grant);
        h.Tick();
        auto resent = std::get<CHIReqFlit>(h.sent.back().flit);
        h.Reply(resent);
        h.Done(first);
        auto successor = std::get<CHIReqFlit>(h.sent.back().flit);
        Expect(successor.flit_txn != resent.flit_txn && successor.flit_allow_retry == 1,
               "same-line retry reordered jobs");
        h.Reply(successor);
        h.Done(second.handle);
        h.Drain();
    }
    // Unordered writes can receive DBIDRespOrd; completion alone does not send data.
    for (unsigned order : {0u, 2u, 3u}) {
        Harness h;
        TxnRequest q;
        q.opcode = 0x1c;
        q.order = order;
        if (order == 3)
            q.memory_type = MemoryType::kDeviceNRnE;
        q.data = Pattern();
        Handle id;
        auto sent = h.Issue(q, &id);
        h.Rx(Response(sent, 4));
        h.Tick();
        Expect(h.sent.size() == 1, "write data sent without DBID");
        h.Rx(Response(sent, 14));
        h.Done(id);
        h.Drain();
    }
    // Wire attribute oracle is independent of the production opcode property table.
    {
        Harness h;
        h.State(CacheState::kUC);
        TxnRequest q;
        q.opcode = 0x15;
        auto f = h.Issue(q);
        Expect(f.flit_mem_attr == 13 && f.flit_snp_attr == 1 && !f.flit_exp_comp_ack,
               "WriteEvictFull attributes");
    }
    for (unsigned op : {4u, 3u, 1u, 2u, 7u, 0x26u, 0x1cu, 0x1du, 0x18u, 0x19u, 0x0bu, 0x0cu, 8u, 9u,
                        10u, 0x0du}) {
        Harness h;
        TxnRequest q;
        q.opcode = op;
        auto f = h.Issue(q);
        const bool allocate =
            op == 1 || op == 2 || op == 7 || op == 0x26 || op == 0x0b || op == 0x0c;
        Expect(f.flit_mem_attr == (allocate ? 13u : 5u), "Normal memory attribute combination");
        Expect(f.flit_snp_attr == (op == 4 || op == 0x1c || op == 0x1d ? 0u : 1u), "SnpAttr");
    }
    // Device data window begins at Addr; Normal begins at the Size boundary.
    for (unsigned size : {1u, 2u, 4u, 8u, 16u, 32u, 64u}) {
        Harness h;
        TxnRequest q;
        q.opcode = 4;
        q.memory_type = MemoryType::kDeviceNRnE;
        q.order = 3;
        q.size = size;
        q.address = 39;
        auto c = h.Execute(q);
        const unsigned count = size - (39 & (size - 1));
        const auto mask = ((1ull << count) - 1) << 39;
        Expect(c.valid == mask, "Device Size window");
        q.opcode = 0x1c;
        q.byte_enable = mask;
        q.data = Pattern();
        h.Execute(q);
        h.Drain();
    }
    // ReadUnique merges UDP and discards stale downstream data when already dirty.
    for (auto state : {CacheState::kUD, CacheState::kSD, CacheState::kUDP}) {
        Harness h;
        h.State(state);
        TxnRequest q;
        q.opcode = 7;
        Handle id;
        auto sent = h.Issue(q, &id);
        Line stale{};
        h.Rx(ReadData(sent, stale, 2));
        h.Rx(ReadData(sent, stale, 0));
        const auto c = h.Done(id);
        Expect(c.state == CacheState::kUD, "ReadUnique dirty state");
        for (unsigned i = 0; i < 64; ++i)
            Expect(c.data[i] == (state == CacheState::kUDP && i >= 32 ? 0 : Pattern()[i]),
                   "ReadUnique merge/drop");
    }
    // Completion error is a result, not a fake successful allocation. Existing
    // state is preserved even when an intervening snoop changed that state.
    for (auto op : {7u, 0x0bu, 0x0cu}) {
        Harness h;
        h.State(CacheState::kSC);
        TxnRequest q;
        q.opcode = op;
        Handle id;
        auto sent = h.Issue(q, &id);
        h.Rx(Snoop(7));
        h.Tick();
        h.Tick();
        h.Reply(sent, 5, 0, 3);
        Expect(h.Done(id).error == AccessError::kNonDataError &&
                   h.rn.InspectLine(0)->state == CacheState::kI,
               "NDERR restored a snoop-invalidated line");
    }
    {
        Harness h;
        auto id = h.rn.Load(0, 8).handle;
        h.Tick();
        auto q = std::get<CHIReqFlit>(h.sent.front().flit);
        h.Rx(ReadData(q, Pattern(), 0, 2, 0));
        h.Rx(ReadData(q, Pattern(), 2, 2, 2));
        auto c = h.Done(id);
        Expect(c.error == AccessError::kOK && c.valid == 255,
               "unrequested DERR leaked into narrow load");
        c = h.Done(h.rn.Load(32, 8).handle);
        Expect(c.error == AccessError::kDataError, "per-byte DERR not retained");
    }
    {
        Harness h;
        h.State(CacheState::kUD);
        TxnRequest q;
        q.opcode = 0x1b;
        Handle id;
        auto sent = h.Issue(q, &id);
        h.Rx(Snoop(8));
        h.Tick();
        h.Tick();  // Dirty data responsibility transferred first.
        h.Reply(sent, 0, 1);
        h.Done(id);
        const auto ack = std::get<CHIRspFlit>(h.sent.back().flit);
        Expect(ack.flit_resp == 2, "CopyBack CompAck ignored snoop downgrade");
    }
    {
        Harness h;
        h.State(CacheState::kUC);
        auto req = Snoop(7, 48, 999, true);
        h.Rx(req);
        h.Tick();
        for (const auto& a : h.sent)
            if (auto f = std::get_if<CHIDatFlit>(&a.flit))
                Expect(f->flit_ccid == 3, "snoop critical chunk lost");
        h.Drain();
    }
    // Negative encodings, mandatory controls and response resource identities.
    {
        Harness h;
        h.Rx(Snoop(7, 0, 999, false, false));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        h.Rx(Snoop(15));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        RN rn;
        TxnRequest q;
        q.opcode = 6;
        Throws(ErrorKind::kProtocolViolation, [&] { rn.Submit(q); });
    }
    {
        RN rn;
        TxnRequest q;
        q.opcode = 0x1d;
        q.size = 32;
        Throws(ErrorKind::kInvalidRequest, [&] { rn.Submit(q); });
    }
    {
        RN rn;
        TxnRequest q;
        q.opcode = 4;
        q.order = 3;
        Throws(ErrorKind::kInvalidRequest, [&] { rn.Submit(q); });
    }
    for (unsigned variant = 0; variant < 4; ++variant) {
        Harness h;
        TxnRequest q;
        q.opcode = 0x1c;
        auto sent = h.Issue(q);
        auto response = Response(sent, variant == 0 ? 6 : 5);
        if (variant == 0)
            response.flit_resp_err = 2;
        if (variant == 1)
            response.flit_resp = 2;
        if (variant == 2)
            response.flit_dbid = 4096;
        if (variant == 3)
            response.flit_src = 2;
        h.Rx(response);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        TxnRequest q;
        q.opcode = 4;
        auto sent = h.Issue(q);
        h.Rx(Response(sent, 3));
        h.Rx(Response(sent, 7));
        h.Tick();
        auto retry = std::get<CHIReqFlit>(h.sent.back().flit);
        h.Rx(Response(retry, 3));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        TxnRequest q;
        auto sent = h.Issue(q);
        auto a = ReadData(sent, Pattern(), 0);
        auto b = ReadData(sent, Pattern(), 2);
        b.flit_dbid++;
        h.Rx(a);
        h.Rx(b);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        h.State(CacheState::kUD);
        TxnRequest q;
        q.opcode = 0x1b;
        auto sent = h.Issue(q);
        h.Rx(Response(sent, 4, 0, 2));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        h.State(CacheState::kUD);
        h.hold = true;
        h.Rx(Snoop(3));
        h.Tick();
        h.Rx(Snoop(3));
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        RNConfig config;
        config.timeout_cycles = 3;
        Harness h(config);
        h.hold = true;
        h.Rx(Snoop(7));
        h.Tick();
        Throws(ErrorKind::kTimeout, [&] {
            for (unsigned i = 0; i < 4; ++i)
                h.Tick();
        });
    }
    // Wire TxnIDs wrap while logical handles/timers never alias a later request.
    {
        RNConfig config;
        config.request_slots = 1;
        config.timeout_cycles = 100;
        Harness h(config);
        TxnRequest q;
        q.opcode = 4;
        for (unsigned i = 0; i < 4200; ++i) {
            Handle id;
            const auto sent = h.Issue(q, &id);
            h.Reply(sent);
            h.Done(id);
            Expect(sent.flit_txn == i % 4096, "TxnID allocator failed to recycle");
        }
        h.Drain();
    }
}
}  // namespace vip_test
