#include "rn_internal.hpp"

namespace esl::chi_vip::detail {
void SnoopEngine::Receive(const CHISnpFlit& f) {
    constexpr auto p = ErrorKind::kProtocolViolation;
    Check(f.flit_src == r_.config.home_id && f.flit_txn < 4096 && f.flit_qos < 16 &&
              f.flit_address < (r_.config.memory_size >> 3) && f.flit_ns < 2 && f.flit_nse < 2 &&
              f.flit_ret_to_src < 2 && f.flit_do_not_go_to_sd < 2 && f.flit_trace_tag < 2,
          p, "RN.SNP.FIELD: invalid SNP field");
    const auto address = LineAddress(f.flit_address << 3);
    DecideSnoop(f.flit_opcode, r_.lines.Get(address).state, f.flit_ret_to_src,
                f.flit_do_not_go_to_sd);
    Check(!f.flit_fwd_nid && !f.flit_fwd_txn_id && !f.flit_pbha && !f.flit_stash_lpid_valid &&
              !f.flit_stash_lpid && !f.flit_vmid_ext,
          p, "RN.SNP.FIELD: inapplicable forwarding/stash fields");
    Check(f.flit_ns && !f.flit_nse && !f.flit_mpam && !f.flit_mecid, ErrorKind::kUnsupportedFeature,
          "RN.SNP.FEATURE: snoop outside configured feature/security domain");
    Check(!r_.line_snoops.count(address), p,
          "RN.SNP.ORDER: Home must await the previous same-line snoop response");
    Check(r_.snoop_ids.insert({f.flit_src, f.flit_txn}).second, p,
          "RN.SNP.ID: duplicate live snoop");
    SnoopContext c;
    c.id = r_.next_snoop++;
    c.request = f;
    c.line = address;
    const auto q = r_.line_jobs.find(address);
    if (q != r_.line_jobs.end()) {
        const auto request = r_.requests.find(q->second.front());
        if (request != r_.requests.end()) {
            const auto& req = request->second;
            const auto family = Traits(req.request.opcode).family;
            auto phase = RequestPhase::kBeforeResponse;
            if (family == Family::kRead) {
                if (req.applied)
                    phase = RequestPhase::kCompletedRead;
                else if (req.received)
                    phase = RequestPhase::kPartialRead;
            } else if (req.terminal_issued && req.wire.flit_snp_attr)
                phase = RequestPhase::kTerminalWrite;
            const auto decision = DecideHazard(phase, Traits(req.request.opcode).allocating);
            Check(decision.action != HazardAction::kViolation, p,
                  std::string(decision.rule) + ": illegal snoop after completion");
            c.waiting_fill = decision.action == HazardAction::kWaitForData;
            r_.Observe("rule", c.id, c.line, decision.rule);
        }
    }
    r_.snoops.emplace(c.id, c);
    if (r_.config.timeout_cycles)
        r_.snoop_timers.emplace(r_.cycle + r_.config.timeout_cycles, c.id);
    r_.line_snoops[address].push_back(c.id);
    r_.ReadySnoop(c.id);
}
void SnoopEngine::WakeLine(std::uint64_t line) {
    auto it = r_.line_snoops.find(line);
    if (it != r_.line_snoops.end())
        r_.ReadySnoop(it->second.front());
}
void SnoopEngine::Step(Handle id) {
    auto& c = r_.snoops.at(id);
    if (c.issued || r_.line_snoops.at(c.line).front() != id)
        return;
    if (c.waiting_fill) {
        const auto q = r_.line_jobs.find(c.line);
        if (q != r_.line_jobs.end()) {
            const auto req = r_.requests.find(q->second.front());
            if (req != r_.requests.end() && !req->second.applied) {
                r_.Observe("rule", id, c.line, "RN.HAZARD.FILL");
                return;
            }
        }
        c.waiting_fill = false;
    }
    const auto snapshot = r_.lines.Get(c.line);
    const auto decision = DecideSnoop(c.request.flit_opcode, snapshot.state,
                                      c.request.flit_ret_to_src, c.request.flit_do_not_go_to_sd);
    r_.Observe("rule", id, c.line, decision.rule);
    const auto resp = EncodeState(decision.next, decision.pass_dirty);
    if (decision.data) {
        for (unsigned beat = 0; beat < 2; ++beat) {
            auto f = Pack(snapshot.data, decision.partial ? snapshot.valid : kAllBytes, beat * 2);
            f.flit_opcode =
                decision.partial ? CHI_DAT_Opcode_SnpRespDataPtl : CHI_DAT_Opcode_SnpRespData;
            f.flit_src = r_.config.node_id;
            f.flit_tgt = c.request.flit_src;
            f.flit_txn = c.request.flit_txn;
            f.flit_resp = resp;
            f.flit_cah = !decision.partial && snapshot.cah;
            f.flit_ccid = (c.request.flit_address >> 1) & 3;
            f.flit_resp_err = (snapshot.errors & ByteMask(beat * 32, 32)) ? 2 : 0;
            r_.Emit(Owner::kSnoop, id, f, OutputKind::kTerminal);
        }
    } else {
        CHIRspFlit f;
        f.flit_opcode = CHI_RSP_Opcode_SnpResp;
        f.flit_src = r_.config.node_id;
        f.flit_tgt = c.request.flit_src;
        f.flit_txn = c.request.flit_txn;
        f.flit_resp = resp;
        r_.Emit(Owner::kSnoop, id, f, OutputKind::kTerminal);
    }
    auto next = snapshot;
    next.state = decision.next;
    if (!IsDirty(next.state))
        next.dirty = 0;
    if (next.state == CacheState::kI)
        next = CacheLine{};
    r_.lines.Put(c.line, next, decision.rule, id);
    c.issued = true;
}
void SnoopEngine::Sent(const Output& output) {
    auto& c = r_.snoops.at(output.id);
    Check(c.in_flight != 0, ErrorKind::kProtocolViolation, "RN.SNP.TOKEN: repeated send");
    if (--c.in_flight)
        return;
    const auto line = c.line;
    r_.snoop_ids.erase({c.request.flit_src, c.request.flit_txn});
    auto& queue = r_.line_snoops.at(line);
    Check(queue.front() == output.id, ErrorKind::kLifecycle, "snoop FIFO violated");
    queue.pop_front();
    r_.snoops.erase(output.id);
    if (queue.empty())
        r_.line_snoops.erase(line);
    else
        r_.ReadySnoop(queue.front());
    auto job = r_.line_jobs.find(line);
    if (job != r_.line_jobs.end()) {
        r_.ReadyJob(job->second.front());
        r_.ReadyRequest(job->second.front());
    }
    r_.lines.WakeResources();
}
}  // namespace esl::chi_vip::detail
