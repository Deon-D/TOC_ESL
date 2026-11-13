#include <algorithm>

#include "rn_internal.hpp"

namespace esl::chi_vip::detail {
namespace {
constexpr auto P = ErrorKind::kProtocolViolation;
constexpr auto U = ErrorKind::kUnsupportedFeature;
std::uint64_t Window(const TxnRequest& r) {
    const auto skip = r.memory_type == MemoryType::kDeviceNRnE ? (r.address & (r.size - 1)) : 0;
    return ByteMask(static_cast<unsigned>(((r.address & 63) & ~(r.size - 1)) + skip),
                    r.size - skip);
}
bool IsNonAllocRead(unsigned op) {
    return op == CHI_REQ_Opcode_ReadNoSnp || op == CHI_REQ_Opcode_ReadOnce;
}
}  // namespace
void MergeError(AccessError& target, unsigned error) {
    Check(error != 1 && error <= 3, P, "RN.ERR: invalid/exclusive RespErr");
    if (error == 3)
        target = AccessError::kNonDataError;
    else if (error == 2 && target == AccessError::kOK)
        target = AccessError::kDataError;
}
void ValidateResponseFields(const CHIRspFlit& f, const RNConfig& c) {
    Check(f.flit_tgt == c.node_id && f.flit_src == c.home_id && f.flit_txn < 4096 &&
              f.flit_dbid < 4096 && f.flit_qos < 16 && f.flit_resp < 8 && f.flit_resp_err < 4 &&
              f.flit_pcrd_type < 16 && f.flit_c_busy < 8 && f.flit_trace_tag < 2,
          P, "RN.RSP.FIELD: invalid RSP field or routing");
    Check(!f.flit_fwd_state && !f.flit_data_pull, P,
          "RN.RSP.FIELD: inapplicable snoop response fields");
    Check(!f.flit_tag_op && !f.flit_p_group_id && !f.flit_stash_group_id && !f.flit_tag_group_id, U,
          "RN.RSP.FEATURE: optional response feature");
}
void ValidateDataFields(const CHIDatFlit& f, const RNConfig& c) {
    Check(f.flit_tgt == c.node_id && f.flit_txn < 4096 && f.flit_dbid < 4096 && f.flit_qos < 16 &&
              f.flit_src < (1u << c.node_id_width) && f.flit_home_nid < (1u << c.node_id_width) &&
              f.flit_resp < 8 && f.flit_resp_err < 4 && f.flit_c_busy < 8 && f.flit_ccid < 4 &&
              (f.flit_data_id == 0 || f.flit_data_id == 2) && f.flit_cah < 2 &&
              f.flit_trace_tag < 2 && !(f.flit_be >> 32) && f.flit_data_source < 256,
          P, "RN.DAT.FIELD: invalid DAT field");
    Check(f.flit_src == c.home_id && f.flit_home_nid == c.home_id, U,
          "RN.DAT.ROUTE: DMT/DCT disabled");
    if (f.flit_opcode == CHI_DAT_Opcode_CompData)
        Check(!f.flit_fwd_state && !f.flit_data_pull && !f.flit_pbha, P,
              "RN.DAT.FIELD: inapplicable CompData fields");
    Check(!f.flit_tag_op && !f.flit_tag && !f.flit_tu && !f.flit_num_dat && !f.flit_replicate &&
              !f.flit_data_check && !f.flit_poison && !f.flit_mecid && !f.flit_pbha &&
              !f.flit_rsvdc,
          U, "RN.DAT.FEATURE: optional DAT feature");
}
RequestContext& RequestEngine::Lookup(TxnID id) {
    auto it = r_.txn_map.find(id);
    Check(it != r_.txn_map.end(), P,
          "RN.ID: unknown/reclaimed response TxnID=" + std::to_string(id));
    auto& c = r_.requests.at(it->second);
    Check(c.req_sent && !c.credit_wait, P,
          "RN.RESPONSE: response before request/while waiting for credit");
    return c;
}
void RequestEngine::Start(Job& job, const TxnRequest& req) {
    r_.Observe("rule", job.id, job.line, "RN.START." + std::to_string(req.opcode));
    const auto& traits = Traits(req.opcode);
    const bool maintenance = job.kind == JobKind::kReplacement;
    Check(!r_.ids.empty(), ErrorKind::kLifecycle, "TxnID allocator exhausted");
    RequestContext c;
    c.id = job.id;
    c.request = req;
    c.replacement = maintenance;
    c.wire.flit_src = r_.config.node_id;
    c.wire.flit_tgt = r_.config.home_id;
    c.wire.flit_txn = r_.ids.front();
    r_.ids.pop_front();
    c.wire.flit_opcode = req.opcode;
    c.wire.flit_address = req.address;
    c.wire.flit_size = SizeEncoding(req.size);
    c.wire.flit_ns = 1;
    c.wire.flit_allow_retry = 1;
    c.wire.flit_order = req.order;
    c.wire.flit_exp_comp_ack = RequiresAck(req);
    // Table B2.12: Cacheable Normal must permit EWA. WriteEvictFull additionally
    // requires Allocate even though the requester itself is deallocating.
    c.wire.flit_mem_attr = req.memory_type == MemoryType::kDeviceNRnE                         ? 2
                           : traits.allocating || req.opcode == CHI_REQ_Opcode_WriteEvictFull ? 13
                                                                                              : 5;
    c.wire.flit_snp_attr = req.opcode != CHI_REQ_Opcode_ReadNoSnp &&
                           req.opcode != CHI_REQ_Opcode_WriteNoSnpFull &&
                           req.opcode != CHI_REQ_Opcode_WriteNoSnpPtl;
    const auto line = LineAddress(req.address);
    c.wire.flit_cah = traits.family == Family::kCopyBack &&
                      req.opcode != CHI_REQ_Opcode_WriteBackPtl && r_.lines.Get(line).cah;
    if (req.opcode == CHI_REQ_Opcode_Evict || req.opcode == CHI_REQ_Opcode_CleanInvalid ||
        req.opcode == CHI_REQ_Opcode_MakeInvalid)
        r_.lines.Invalidate(line, "RN.START.INVALIDATE", job.id);
    job.wire = true;
    r_.txn_map.emplace(c.wire.flit_txn, c.id);
    if (maintenance)
        ++r_.maintenance_count;
    else
        ++r_.normal_count;
    r_.requests.emplace(c.id, c);
    r_.Observe("rule", c.id, line, "RN.REQ." + std::to_string(req.opcode));
    EmitRequest(r_.requests.at(c.id));
}
void RequestEngine::EmitRequest(RequestContext& c) {
    c.req_sent = false;
    r_.Emit(Owner::kRequest, c.id, c.wire, OutputKind::kRequest, c.generation);
}
void RequestEngine::Sent(const Output& o) {
    auto& c = r_.requests.at(o.id);
    Check(c.generation == o.generation && c.in_flight, P,
          "RN.TOKEN: stale request send generation");
    --c.in_flight;
    if (o.kind == OutputKind::kRequest)
        c.req_sent = true;
    if (c.terminal_issued && !c.in_flight && c.buffer_claimed) {
        r_.remote_buffers.erase({c.responder, c.dbid});
        c.buffer_claimed = false;
    }
    r_.ReadyRequest(o.id);
}
void RequestEngine::ClaimBuffer(RequestContext& c) {
    Check(!c.buffer_claimed &&
              r_.remote_buffers.emplace(std::make_pair(c.responder, c.dbid), c.id).second,
          P, "RN.DBID: Home reused a live DBID before WriteData/CompAck was sent");
    c.buffer_claimed = true;
}
void RequestEngine::ReleaseOrder(RequestContext& c) {
    if (!c.request.order || c.order_released)
        return;
    const auto key = r_.Key(c.request);
    auto& queue = r_.ordered.at(key);
    Check(queue.front() == c.id, ErrorKind::kLifecycle,
          "RN.ORDER: acceptance overtook predecessor");
    queue.pop_front();
    c.order_released = true;
    if (queue.empty())
        r_.ordered.erase(key);
    else
        r_.ReadyJob(queue.front());
    r_.Observe("rule", c.id, LineAddress(c.request.address), "RN.ORDER.ACCEPT");
}
void RequestEngine::Grant(NodeID node, unsigned type) {
    const Runtime::CreditKey key{node, type};
    ++r_.credits[key];
    auto it = r_.credit_waiters.find(key);
    if (it != r_.credit_waiters.end() && !it->second.empty())
        r_.ReadyRequest(it->second.front());
}
void RequestEngine::Receive(const CHIRspFlit& f) {
    ValidateResponseFields(f, r_.config);
    if (f.flit_opcode == CHI_RSP_Opcode_PCrdGrant) {
        Check(f.flit_resp_err == 0, P, "PCrdGrant with RespErr");
        Grant(f.flit_src, f.flit_pcrd_type);
        return;
    }
    if (f.flit_opcode == CHI_RSP_Opcode_RespSepData || f.flit_opcode == CHI_RSP_Opcode_Persist ||
        f.flit_opcode == CHI_RSP_Opcode_CompPersist || f.flit_opcode == CHI_RSP_Opcode_TagMatch ||
        f.flit_opcode == CHI_RSP_Opcode_StashDone ||
        f.flit_opcode == CHI_RSP_Opcode_CompStashDone || f.flit_opcode == CHI_RSP_Opcode_CompCMO)
        throw VIPError(U, "RN.RSP.FEATURE: response outside GbCore");
    auto& c = Lookup(f.flit_txn);
    r_.Observe("rule", c.id, LineAddress(c.request.address),
               "RN.RSP." + std::to_string(c.request.opcode));
    const auto family = Traits(c.request.opcode).family;
    const auto op = f.flit_opcode;
    if (op == CHI_RSP_Opcode_RetryAck) {
        Check(c.wire.flit_allow_retry && !c.comp && !c.dbid_received && !c.received && !c.receipt &&
                  !c.retry && f.flit_resp_err == 0,
              P, "RN.RETRY: illegal/repeated RetryAck");
        c.retry = true;
        c.credit_wait = true;
        c.wire.flit_pcrd_type = f.flit_pcrd_type;
        r_.credit_waiters[{f.flit_src, f.flit_pcrd_type}].push_back(c.id);
        r_.Observe("rule", c.id, LineAddress(c.request.address), "RN.RETRY.WAIT");
        r_.ReadyRequest(c.id);
        return;
    }
    if (op == CHI_RSP_Opcode_ReadReceipt) {
        Check(IsNonAllocRead(c.request.opcode) && c.request.order && !c.receipt &&
                  f.flit_resp_err == 0,
              P, "RN.ORDER: unexpected/duplicate ReadReceipt");
        c.receipt = true;
        ReleaseOrder(c);
        r_.ReadyRequest(c.id);
        return;
    }
    if (family == Family::kCopyBack)
        Check(!r_.lines.SnoopBusy(LineAddress(c.request.address)), P,
              "RN.HAZARD: CopyBack completed before snoop response");
    Check(op == CHI_RSP_Opcode_Comp || op == CHI_RSP_Opcode_CompDBIDResp ||
              op == CHI_RSP_Opcode_DBIDResp || op == CHI_RSP_Opcode_DBIDRespOrd,
          P, "RN.RSP: unexpected response opcode");
    Check(family != Family::kRead, P, "RN.READ: expected CompData");
    const bool has_comp = op == CHI_RSP_Opcode_Comp || op == CHI_RSP_Opcode_CompDBIDResp;
    const bool has_dbid = op != CHI_RSP_Opcode_Comp;
    if (has_comp && (family == Family::kCopyBack || family == Family::kImmediateWrite) &&
        f.flit_resp_err != 3)
        Check(f.flit_resp == 0, P, "RN.WRITE.RESP: write completion Resp must be zero");
    if (family == Family::kDataless)
        Check(op == CHI_RSP_Opcode_Comp, P, "RN.DATALESS: unexpected DBID");
    if (family == Family::kCopyBack) {
        Check(op == CHI_RSP_Opcode_CompDBIDResp ||
                  (op == CHI_RSP_Opcode_Comp && c.request.opcode != CHI_REQ_Opcode_WriteBackPtl &&
                   c.wire.flit_cah),
              P, "RN.COPYBACK: illegal completion form/CAH");
    }
    if (op == CHI_RSP_Opcode_DBIDRespOrd)
        Check(family == Family::kImmediateWrite, P, "RN.ORDER: unexpected DBIDRespOrd");
    if (!has_comp)
        Check(f.flit_resp_err == 0, P, "RN.ERR: DBIDResp must be OK");
    if (family == Family::kCopyBack && op == CHI_RSP_Opcode_Comp)
        Check(f.flit_resp_err != 2, P, "RN.ERR: CopyBack Comp cannot carry DERR");
    Check(!(has_comp && c.comp) && !(has_dbid && c.dbid_received), P,
          "RN.RSP: duplicate Comp/DBID");
    MergeError(c.error, f.flit_resp_err);
    if (family == Family::kImmediateWrite) {
        if (has_comp) {
            Check(!c.dbid_received || c.dbid == f.flit_dbid, P,
                  "RN.DBID: Comp and DBIDResp disagree");
            c.completion_dbid = f.flit_dbid;
        }
        if (has_dbid)
            Check(!c.completion_dbid || *c.completion_dbid == f.flit_dbid, P,
                  "RN.DBID: DBIDResp and Comp disagree");
    }
    if (has_comp) {
        c.comp = true;
        c.resp = f.flit_resp;
    }
    if (has_dbid || family == Family::kDataless || family == Family::kCopyBack) {
        c.dbid = f.flit_dbid;
        c.responder = f.flit_src;
    }
    if (has_dbid) {
        c.dbid_received = true;
        ReleaseOrder(c);
    }
    if (has_dbid || (has_comp && (RequiresAck(c.request) || family == Family::kCopyBack)))
        ClaimBuffer(c);
    if (family == Family::kDataless && Traits(c.request.opcode).ack_required &&
        f.flit_resp_err != 3)
        Check(f.flit_resp == 2, P, "RN.DATALESS: CleanUnique/MakeUnique requires Comp_UC");
    if (c.request.opcode == CHI_REQ_Opcode_Evict)
        Check(f.flit_resp_err != 2, P, "RN.EVICT: DERR not permitted");
    if (c.request.opcode == CHI_REQ_Opcode_Evict && f.flit_resp_err != 3)
        Check(f.flit_resp == 0, P, "RN.EVICT: expected Comp_I");
    r_.ReadyRequest(c.id);
}
void RequestEngine::Receive(const CHIDatFlit& f) {
    ValidateDataFields(f, r_.config);
    if (f.flit_opcode == CHI_DAT_Opcode_DataSepResp)
        throw VIPError(U, "separate read response disabled");
    Check(f.flit_opcode == CHI_DAT_Opcode_CompData, P, "RN.DAT: expected CompData");
    auto& c = Lookup(f.flit_txn);
    r_.Observe("rule", c.id, LineAddress(c.request.address),
               "RN.RSP." + std::to_string(c.request.opcode));
    Check(Traits(c.request.opcode).family == Family::kRead, P, "RN.DAT: CompData for non-read");
    const unsigned bit = 1u << (f.flit_data_id / 2);
    Check((BeatMask(c.request.address, c.request.size) & bit) && !(c.received & bit), P,
          "RN.DATA.BEAT: unexpected/duplicate data packet");
    Check(f.flit_ccid == ((c.request.address >> 4) & 3), P,
          "RN.DATA.CCID: incorrect critical chunk");
    if (c.first_data) {
        const auto& first = *c.first_data;
        Check(f.flit_resp == first.flit_resp && f.flit_cah == first.flit_cah &&
                  f.flit_data_source == first.flit_data_source &&
                  (!RequiresAck(c.request) || f.flit_dbid == first.flit_dbid) &&
                  ((f.flit_resp_err == 3) == (first.flit_resp_err == 3)),
              P, "RN.DATA.CONSISTENT: inconsistent multi-packet message");
    } else {
        c.first_data = f;
        c.resp = f.flit_resp;
        c.dbid = f.flit_dbid;
        c.responder = f.flit_home_nid;
        if (RequiresAck(c.request))
            ClaimBuffer(c);
    }
    if (f.flit_resp_err != 3)
        ReadResult(c.request.opcode, r_.lines.Get(LineAddress(c.request.address)).state,
                   f.flit_resp);
    MergeError(c.error, f.flit_resp_err);
    const auto data = Unpack(f);
    const auto mask = ByteMask(f.flit_data_id * 16, 32) & Window(c.request);
    for (unsigned i = 0; i < 64; ++i)
        if ((mask >> i) & 1)
            c.data[i] = data[i];
    if (f.flit_resp_err == 2)
        c.errors |= mask;
    c.valid |= mask;
    c.received |= bit;
    // Install only after all packets. A snoop held by RN.HAZARD.FILL is woken
    // before the ready queue attempts the request's CompAck.
    if (c.received == BeatMask(c.request.address, c.request.size))
        Read(c);
    r_.ReadyRequest(c.id);
}
void RequestEngine::Ack(RequestContext& c, unsigned resp) {
    CHIRspFlit f;
    f.flit_src = r_.config.node_id;
    f.flit_tgt = c.responder;
    f.flit_txn = c.dbid;
    f.flit_opcode = CHI_RSP_Opcode_CompAck;
    f.flit_resp = resp;
    r_.Emit(Owner::kRequest, c.id, f, OutputKind::kTerminal, c.generation);
}
void RequestEngine::Data(RequestContext& c, const CacheLine& snapshot, bool copy_back) {
    const auto& req = c.request;
    const auto mask = copy_back ? snapshot.valid : req.byte_enable;
    for (unsigned beat = 0; beat < 2; ++beat) {
        if (!(BeatMask(req.address, req.size) & (1u << beat)))
            continue;
        auto f = Pack(copy_back ? snapshot.data : req.data, mask, beat * 2);
        f.flit_opcode =
            copy_back ? CHI_DAT_Opcode_CopyBackWriteData : CHI_DAT_Opcode_NonCopyBackWriteData;
        f.flit_src = r_.config.node_id;
        f.flit_tgt = c.responder;
        f.flit_txn = c.dbid;
        f.flit_ccid = (req.address >> 4) & 3;
        f.flit_resp = copy_back ? EncodeState(snapshot.state, IsDirty(snapshot.state)) : 0;
        f.flit_resp_err = (snapshot.errors & ByteMask(beat * 32, 32)) ? 2 : 0;
        r_.Emit(Owner::kRequest, c.id, f, OutputKind::kTerminal, c.generation);
    }
}
void RequestEngine::Read(RequestContext& c) {
    if (c.applied || c.received != BeatMask(c.request.address, c.request.size))
        return;
    const auto line_address = LineAddress(c.request.address);
    if (Traits(c.request.opcode).allocating && c.error != AccessError::kNonDataError) {
        auto line = r_.lines.Get(line_address);
        const auto next = ReadResult(c.request.opcode, line.state, c.resp);
        for (unsigned i = 0; i < 64; ++i) {
            const auto bit = std::uint64_t{1} << i;
            if ((line.state == CacheState::kSC || line.state == CacheState::kUC) &&
                !(c.errors & bit) && !(line.errors & bit))
                Check(line.data[i] == c.data[i], P,
                      "RN.READ.UNIQUE: clean cached data differs from returned data");
            if (!(IsDirty(line.state) && (line.valid & bit))) {
                line.data[i] = c.data[i];
                line.errors = (line.errors & ~bit) | (c.errors & bit);
            }
        }
        line.valid = kAllBytes;
        line.state = next;
        line.dirty = IsDirty(next) ? kAllBytes : 0;
        line.cah = c.first_data->flit_cah && !IsDirty(r_.lines.Get(line_address).state);
        line.lru = ++r_.lru;
        r_.lines.Put(line_address, line, "RN.READ.INSTALL", c.id);
        c.data = line.data;
        c.errors = line.errors;
        if (line.errors && c.error == AccessError::kOK)
            c.error = AccessError::kDataError;
    }
    c.applied = true;
    r_.snoop_engine.WakeLine(line_address);
}
void RequestEngine::ImmediateWrite(RequestContext& c) {
    if (!c.dbid_received || c.terminal_issued)
        return;
    Data(c, CacheLine{}, false);
    c.terminal_issued = true;
}
void RequestEngine::CopyBack(RequestContext& c) {
    if (!c.comp || c.terminal_issued || r_.lines.SnoopBusy(LineAddress(c.request.address)))
        return;
    const auto address = LineAddress(c.request.address);
    auto line = r_.lines.Get(address);
    if (c.dbid_received)
        Data(c, line, true);
    else
        Ack(c, EncodeState(line.state, IsDirty(line.state)));
    if (c.request.opcode == CHI_REQ_Opcode_WriteCleanFull && line.state != CacheState::kI) {
        line.state = IsUnique(line.state) ? CacheState::kUC : CacheState::kSC;
        line.dirty = 0;
        r_.lines.Put(address, line, "RN.COPYBACK.CLEAN", c.id);
    } else
        r_.lines.Invalidate(address, "RN.COPYBACK.INVALIDATE", c.id);
    c.applied = true;
    c.terminal_issued = true;
}
void RequestEngine::Dataless(RequestContext& c) {
    if (!c.comp || c.applied || r_.lines.SnoopBusy(LineAddress(c.request.address)))
        return;
    const auto address = LineAddress(c.request.address);
    auto line = r_.lines.Get(address);
    if (c.error != AccessError::kNonDataError && c.request.opcode == CHI_REQ_Opcode_CleanUnique) {
        line.state = IsDirty(line.state)       ? CacheState::kUD
                     : line.valid == kAllBytes ? CacheState::kUC
                                               : CacheState::kUCE;
        r_.lines.Put(address, line, "RN.DATALESS.UNIQUE", c.id);
    }
    if (c.error != AccessError::kNonDataError && c.request.opcode == CHI_REQ_Opcode_MakeUnique) {
        line.state = CacheState::kUD;
        line.data = c.request.data;
        line.valid = kAllBytes;
        line.dirty = kAllBytes;
        line.errors = 0;
        line.cah = false;
        line.lru = ++r_.lru;
        r_.lines.Put(address, line, "RN.DATALESS.REPLACE", c.id);
        r_.Observe("store", c.id, address, "RN.DATALESS.REPLACE");
        r_.observations.back().data = line.data;
        r_.observations.back().mask = kAllBytes;
    }
    if (RequiresAck(c.request))
        Ack(c);
    c.applied = true;
    c.terminal_issued = true;
}
void RequestEngine::Step(Handle id) {
    auto& c = r_.requests.at(id);
    if (c.credit_wait) {
        const Runtime::CreditKey key{r_.config.home_id, c.wire.flit_pcrd_type};
        auto credit = r_.credits.find(key);
        auto& waiters = r_.credit_waiters.at(key);
        if (credit == r_.credits.end() || !credit->second || waiters.front() != id)
            return;
        if (--credit->second == 0)
            r_.credits.erase(credit);
        waiters.pop_front();
        if (waiters.empty())
            r_.credit_waiters.erase(key);
        else
            r_.ReadyRequest(waiters.front());
        c.credit_wait = false;
        c.wire.flit_allow_retry = 0;
        ++c.generation;
        r_.Observe("rule", id, LineAddress(c.request.address), "RN.RETRY.REISSUE");
        EmitRequest(c);
        return;
    }
    if (!c.req_sent)
        return;
    const auto family = Traits(c.request.opcode).family;
    if (family == Family::kRead) {
        Read(c);
        if (c.applied && !c.terminal_issued &&
            !r_.lines.SnoopBusy(LineAddress(c.request.address))) {
            if (RequiresAck(c.request))
                Ack(c);
            c.terminal_issued = true;
        }
    }
    if (family == Family::kImmediateWrite)
        ImmediateWrite(c);
    if (family == Family::kCopyBack)
        CopyBack(c);
    if (family == Family::kDataless)
        Dataless(c);
    const bool response_done =
        family == Family::kRead ? c.applied && (!c.request.order || c.receipt) : c.comp;
    if (c.terminal_issued && response_done && !c.in_flight)
        Finish(id);
}
void RequestEngine::Finish(Handle id) {
    const auto c = r_.requests.at(id);
    Check(!c.request.order || c.order_released, P, "RN.ORDER: completion before acceptance");
    r_.txn_map.erase(c.wire.flit_txn);
    r_.ids.push_back(c.wire.flit_txn);
    if (c.replacement)
        --r_.maintenance_count;
    else
        --r_.normal_count;
    r_.requests.erase(id);
    auto& job = r_.jobs.at(id);
    job.wire = false;
    if ((job.kind == JobKind::kLoad && c.error != AccessError::kNonDataError) ||
        (job.kind == JobKind::kStore && c.error == AccessError::kOK)) {
        r_.ReadyJob(id);
        r_.lines.WakeResources();
        return;
    }
    Completion result;
    result.error = c.error;
    result.data = c.data;
    result.valid = c.valid;
    result.state = r_.lines.Get(job.line).state;
    if (c.error == AccessError::kNonDataError)
        result.valid = 0;
    r_.lines.Finish(id, result);
}
void RequestEngine::ReturnCredits() {
    if (!r_.jobs.empty())
        return;
    for (const auto& [key, count] : r_.credits) {
        for (unsigned i = 0; i < count; ++i) {
            CHIReqFlit f;
            f.flit_opcode = CHI_REQ_Opcode_PCrdReturn;
            f.flit_src = r_.config.node_id;
            f.flit_tgt = key.first;
            f.flit_pcrd_type = key.second;
            r_.Emit(Owner::kCredit, 0, f, OutputKind::kCredit);
        }
    }
    r_.credits.clear();
}
}  // namespace esl::chi_vip::detail
