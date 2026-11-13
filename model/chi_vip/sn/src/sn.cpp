#include "sn.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <utility>

namespace esl::chi_vip {
namespace {
constexpr auto P = ErrorKind::kProtocolViolation;
constexpr auto U = ErrorKind::kUnsupportedFeature;
constexpr unsigned kCreditType = 1;
bool IsRead(const CHIReqFlit& q) {
    return q.flit_opcode == CHI_REQ_Opcode_ReadNoSnp;
}
void ValidateRequest(const CHIReqFlit& q, const SNConfig& config) {
    Check(q.flit_src == config.home_id && q.flit_tgt == config.node_id && q.flit_txn < 4096 &&
              q.flit_qos < 16 && q.flit_size <= 6 && q.flit_order < 4 && q.flit_allow_retry < 2 &&
              q.flit_pcrd_type < 16 && q.flit_ns < 2 && q.flit_nse < 2 && q.flit_mem_attr < 16 &&
              q.flit_trace_tag < 2 && q.flit_exp_comp_ack < 2,
          P, "SN.FIELD.WIDTH: invalid request routing or field width (B13)");
    if (q.flit_opcode == CHI_REQ_Opcode_PCrdReturn) {
        Check(!q.flit_allow_retry && q.flit_pcrd_type == kCreditType, P,
              "SN.CREDIT.RETURN: invalid PCrdReturn (B2.9.1)");
        return;
    }
    if (!IsRead(q) && q.flit_opcode != CHI_REQ_Opcode_WriteNoSnpFull &&
        q.flit_opcode != CHI_REQ_Opcode_WriteNoSnpPtl) {
        Check(!Capabilities::SupportsRequest(q.flit_opcode), P,
              "SN.ROLE: coherent RN request cannot terminate at SN-F (C3)");
        static const std::set<unsigned> extensions = {
            0x11, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
            0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
            0x44, 0x4e, 0x50, 0x51, 0x52, 0x60, 0x61, 0x62, 0x70, 0x71};
        throw VIPError(extensions.count(q.flit_opcode) ? U : P,
                       "SN.OPCODE: request outside GbCore SN-F subset");
    }
    Check(q.flit_address < config.memory_size &&
              (q.flit_opcode != CHI_REQ_Opcode_WriteNoSnpFull || q.flit_size == 6),
          P, "SN.FIELD.ADDRESS: invalid address or Full write Size (B2.8/B4.14)");
    Check(q.flit_ns == 1 && !q.flit_nse && !q.flit_exp_comp_ack, U,
          "SN.FEATURE: only Non-secure accesses without CompAck enabled");
    Check(q.flit_return_nid == config.home_id, U,
          "SN.FEATURE.DIRECT: DMT/DWT not enabled; ReturnNID must identify HN (B2.4)");
    Check(q.flit_return_txn_id == q.flit_txn, P,
          "SN.ID.RETURN: ReturnTxnID must equal HN TxnID without DMT/DWT (B2.4.4)");
    Check(!(q.flit_mem_attr & 2) && (q.flit_mem_attr & 1), U,
          "SN.ATTR: only Normal memory with EWA enabled");
    Check(!(q.flit_mem_attr & 8) || (q.flit_mem_attr & 4), P,
          "SN.ATTR: Allocate requires Cacheable (B2.7)");
    Check(q.flit_order != 3, P, "SN.ORDER: Endpoint Order is not valid for Normal memory");
    Check(!q.flit_allow_retry || !q.flit_pcrd_type, P,
          "SN.CREDIT.INITIAL: AllowRetry=1 requires PCrdType=0 (B2.9.2)");
    Check(!q.flit_snp_attr && !q.flit_snoop_me && !q.flit_cah && !q.flit_stash_nid &&
              !q.flit_data_target && !q.flit_stash_nid_valid && !q.flit_endian && !q.flit_deep &&
              !q.flit_prefetch_tgt_hint && !q.flit_stash_lpid_valid && !q.flit_stash_lpid &&
              !q.flit_p_group_id && !q.flit_stash_group_id && !q.flit_tag_group_id &&
              !q.flit_lpid && !q.flit_likely_shared,
          P, "SN.FIELD.INAPPLICABLE: nonzero inapplicable request field (B13)");
    Check(!q.flit_mpam && !q.flit_pbha && !q.flit_mecid && !q.flit_stream_id && !q.flit_sec_sid1 &&
              !q.flit_tag_op && !q.flit_rsvdc && !q.flit_do_dwt && !q.flit_excl,
          U, "SN.FEATURE: optional request extension is disabled");
}
}  // namespace

void SNConfig::Validate() const {
    Check(node_id < 2048 && home_id < 2048 && node_id != home_id && memory_size >= 64 &&
              memory_size % 64 == 0 && memory_size <= (std::uint64_t{1} << 52) && request_slots &&
              request_slots <= 1024 && service_latency < std::numeric_limits<std::uint64_t>::max(),
          ErrorKind::kConfiguration, "SN.CONFIG: invalid node IDs, address range or capacities");
}

struct SN::Impl {
    struct Request {
        Handle id;
        CHIReqFlit req;
        DBID dbid = 0;
        bool dbid_sent = false;
        bool memory_done = false;
        bool memory_started = false;
        bool nderr = false;
        unsigned data_resp = 0;
        unsigned received = 0;
        unsigned outputs = 0;
        Line data{};
        std::uint64_t mask = 0;
        std::uint64_t errors = 0;
    };
    struct Retry {
        Handle id;
        CHIReqFlit req;
        bool grant_issued = false;
        bool ack_issued = false;
        unsigned outputs = 0;
    };
    struct Output {
        SendAction action;
        Handle owner;
        bool retry;
        bool accepted = false;
    };
    struct Timer {
        std::uint64_t cycle;
        Handle id;
        bool operator>(const Timer& other) const {
            return std::tie(cycle, id) > std::tie(other.cycle, other.id);
        }
    };
    SNConfig config;
    Observer observer;
    ByteMemoryBackend memory;
    std::uint64_t cycle = 0;
    bool begun = false;
    bool had_cycle = false;
    Handle next_id = 1;
    Token next_token = 1;
    unsigned reserved_credits = 0;
    unsigned available_credits = 0;
    std::map<Handle, Request> requests;
    std::map<Handle, Retry> retries;
    std::map<TxnID, Handle> live_ids;
    std::map<DBID, Handle> buffers;
    std::deque<DBID> free_dbids;
    std::deque<Handle> credit_waiters;
    std::map<std::uint64_t, std::deque<Handle>> line_queues;
    std::map<Token, Output> outputs;
    std::vector<Event> events;
    std::vector<SendAction> actions;
    std::vector<Observation> observations;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timers;

    Impl(SNConfig c, Observer o)
        : config(c), observer(std::move(o)), memory(c.memory_size, c.service_latency) {
        config.Validate();
        for (DBID n = 0; n < 4096; ++n)
            free_dbids.push_back(n);
    }
    void Observe(const char* action, Handle id, std::uint64_t address, const char* rule = "",
                 std::optional<Flit> flit = {}) {
        Observation o;
        o.cycle = cycle;
        o.handle = id;
        o.address = address;
        o.action = action;
        o.rule = rule;
        o.flit = std::move(flit);
        observations.push_back(std::move(o));
    }
    CHIRspFlit Response(const CHIReqFlit& q, unsigned opcode, DBID dbid = 0) const {
        CHIRspFlit f;
        f.flit_opcode = opcode;
        f.flit_src = config.node_id;
        f.flit_tgt = q.flit_src;
        f.flit_txn = q.flit_txn;
        f.flit_dbid = dbid;
        return f;
    }
    void Emit(Handle id, Flit f, bool retry = false) {
        SendAction action{next_token++, config.home_id, std::move(f)};
        outputs.emplace(action.token, Output{action, id, retry, false});
        actions.push_back(std::move(action));
        if (retry)
            ++retries.at(id).outputs;
        else
            ++requests.at(id).outputs;
    }
    void RetryAck(Retry& retry) {
        auto f = Response(retry.req, CHI_RSP_Opcode_RetryAck);
        f.flit_pcrd_type = kCreditType;
        Emit(retry.id, f, true);
        retry.ack_issued = true;
    }
    void Grant() {
        while (!credit_waiters.empty() &&
               requests.size() + reserved_credits < config.request_slots) {
            const auto id = credit_waiters.front();
            credit_waiters.pop_front();
            auto& retry = retries.at(id);
            auto f = Response(retry.req, CHI_RSP_Opcode_PCrdGrant);
            f.flit_txn = 0;  // P-Credits are not associated with a wire TxnID.
            f.flit_pcrd_type = kCreditType;
            Emit(id, f, true);
            retry.grant_issued = true;
            ++reserved_credits;
            if (!retry.ack_issued)
                RetryAck(retry);
        }
    }
    void StartMemory(Handle id) {
        auto& r = requests.at(id);
        if (r.memory_started || line_queues.at(LineAddress(r.req.flit_address)).front() != id)
            return;
        if (IsRead(r.req))
            memory.SubmitRead(id, LineAddress(r.req.flit_address), cycle);
        else {
            if (r.received != BeatMask(r.req.flit_address, 1u << r.req.flit_size))
                return;
            memory.SubmitWrite(id, LineAddress(r.req.flit_address), r.data, r.nderr ? 0 : r.mask,
                               r.nderr ? 0 : r.errors, cycle);
        }
        r.memory_started = true;
    }
    void Receive(const CHIReqFlit& q) {
        ValidateRequest(q, config);
        if (q.flit_opcode == CHI_REQ_Opcode_PCrdReturn) {
            Check(available_credits && reserved_credits, P,
                  "SN.CREDIT.RETURN: no matching granted credit (B2.9)");
            --available_credits;
            --reserved_credits;
            Grant();
            return;
        }
        Check(!live_ids.count(q.flit_txn), P, "SN.ID.REQUEST: duplicate live request TxnID (B2.4)");
        const auto id = next_id++;
        if (config.timeout_cycles) {
            Check(cycle <= std::numeric_limits<std::uint64_t>::max() - config.timeout_cycles,
                  ErrorKind::kLifecycle, "SN.TIME: timeout overflow");
            timers.push({cycle + config.timeout_cycles, id});
        }
        live_ids[q.flit_txn] = id;
        if (!q.flit_allow_retry) {
            Check(q.flit_pcrd_type == kCreditType && available_credits && reserved_credits, P,
                  "SN.CREDIT.CONSUME: AllowRetry=0 requires an issued P-Credit (B2.9)");
            --available_credits;
            --reserved_credits;
        } else if (requests.size() + reserved_credits >= config.request_slots ||
                   !credit_waiters.empty()) {
            Check(retries.size() < 1024, P,
                  "SN.CREDIT.CAPACITY: more than 1024 pending retry records");
            auto [it, inserted] = retries.emplace(id, Retry{id, q});
            (void)inserted;
            credit_waiters.push_back(id);
            if (!config.grant_before_retry)
                RetryAck(it->second);
            Grant();
            return;
        }
        Check(requests.size() < config.request_slots, P,
              "SN.CREDIT.RESERVATION: granted resource guarantee was lost");
        auto [it, inserted] = requests.emplace(id, Request{id, q});
        (void)inserted;
        auto& r = it->second;
        line_queues[LineAddress(q.flit_address)].push_back(id);
        Observe("rule", id, q.flit_address, "SN.ACCEPT.B2.6.B2.9.C3");
        if (IsRead(q)) {
            if (q.flit_order)
                Emit(id, Response(q, CHI_RSP_Opcode_ReadReceipt));
        } else {
            Check(!free_dbids.empty(), P, "SN.DBID: guaranteed write buffer unavailable");
            r.dbid = free_dbids.front();
            free_dbids.pop_front();
            buffers.emplace(r.dbid, id);
            Emit(id, Response(q, CHI_RSP_Opcode_DBIDResp, r.dbid));
        }
        StartMemory(id);
    }
    void Receive(const CHIDatFlit& f) {
        Check(f.flit_src == config.home_id && f.flit_tgt == config.node_id && f.flit_txn < 4096 &&
                  f.flit_opcode == CHI_DAT_Opcode_NonCopyBackWriteData && f.flit_qos < 16 &&
                  f.flit_resp_err < 4 && f.flit_resp < 8 && f.flit_trace_tag < 2 &&
                  f.flit_c_busy < 8 && (f.flit_data_id == 0 || f.flit_data_id == 2) &&
                  !(f.flit_be >> 32),
              P, "SN.DAT.FIELD: invalid write data field or routing (B13)");
        Check(!f.flit_home_nid && !f.flit_dbid && !f.flit_fwd_state && !f.flit_data_pull &&
                  !f.flit_data_source && !f.flit_cah && !f.flit_num_dat && !f.flit_replicate,
              P, "SN.DAT.INAPPLICABLE: nonzero inapplicable write field");
        Check(!f.flit_pbha && !f.flit_mecid && !f.flit_tag_op && !f.flit_tag && !f.flit_tu &&
                  !f.flit_rsvdc && !f.flit_data_check && !f.flit_poison,
              U, "SN.DAT.FEATURE: optional DAT extension is disabled");
        Check(f.flit_resp_err != 1 && (!f.flit_resp || f.flit_resp_err == 3), P,
              "SN.DAT.RESP: non-exclusive NonCopyBackWriteData requires I or NDERR (B4.5/B9)");
        auto found = buffers.find(f.flit_txn);
        Check(found != buffers.end(), P,
              "SN.DBID.UNKNOWN: unknown, duplicate or recycled write DBID");
        auto& r = requests.at(found->second);
        Check(r.dbid_sent, P, "SN.DBID.EARLY: write data arrived before DBIDResp was transmitted");
        const auto bit = 1u << (f.flit_data_id / 2);
        const auto expected = BeatMask(r.req.flit_address, 1u << r.req.flit_size);
        Check((expected & bit) && !(r.received & bit), P,
              "SN.DAT.BEAT: unexpected or duplicate data beat (B2.8)");
        Check(f.flit_ccid == ((r.req.flit_address >> 4) & 3), P,
              "SN.DAT.CCID: critical chunk does not match request");
        Check(!r.received || r.nderr == (f.flit_resp_err == 3), P,
              "SN.DAT.NDERR: all packets must agree on NDERR (B9)");
        Check(!r.received || r.data_resp == f.flit_resp, P,
              "SN.DAT.RESP: all data packets must carry the same Resp (B9)");
        r.nderr = f.flit_resp_err == 3;
        r.data_resp = f.flit_resp;
        const auto mask = f.flit_be << (f.flit_data_id * 16);
        const auto size = 1u << r.req.flit_size;
        const auto window =
            ByteMask(static_cast<unsigned>((r.req.flit_address & 63) & ~(size - 1)), size);
        Check(!(mask & ~window), P, "SN.DAT.BE: enabled bytes outside request Size window (B2.8)");
        if (r.req.flit_opcode == CHI_REQ_Opcode_WriteNoSnpFull)
            Check(f.flit_be == 0xffffffffu, P, "SN.DAT.FULL: Full write must enable all bytes");
        const auto data = Unpack(f);
        for (unsigned n = f.flit_data_id * 16; n < f.flit_data_id * 16 + 32; ++n) {
            Check(((mask >> n) & 1) || !data[n], P,
                  "SN.DAT.ZERO: disabled data bytes must be zero (B2.8)");
            if ((mask >> n) & 1)
                r.data[n] = data[n];
        }
        r.mask |= mask;
        if (f.flit_resp_err == 2)
            r.errors |= mask;
        r.received |= bit;
        if (r.received == expected) {
            const auto id = r.id;
            free_dbids.push_back(r.dbid);
            buffers.erase(found);
            StartMemory(id);
        }
    }
    void Finish(Handle id) {
        auto found = requests.find(id);
        if (found == requests.end() || !found->second.memory_done || found->second.outputs)
            return;
        const auto r = found->second;
        live_ids.erase(r.req.flit_txn);
        requests.erase(found);
        Observe("complete", id, r.req.flit_address, "SN.COMPLETE.B2.4.B2.6");
        Grant();
    }
    void Sent(const Event& e) {
        auto found = outputs.find(e.token);
        Check(found != outputs.end(), P, "SN.TOKEN: unknown or duplicate send token");
        auto& out = found->second;
        if (e.kind == EventKind::kTxAccepted) {
            Check(!out.accepted, P, "SN.TOKEN: duplicate TX acceptance");
            out.accepted = true;
            Observe("tx_accepted", out.owner, 0, "", out.action.flit);
            return;
        }
        Check(out.accepted, P, "SN.TOKEN: actual send precedes local TX acceptance");
        const auto completed = out;
        Observe("tx_sent", completed.owner, 0, "", completed.action.flit);
        if (e.observed_cycle)
            observations.back().cycle = *e.observed_cycle;
        outputs.erase(found);
        if (completed.retry) {
            auto& retry = retries.at(completed.owner);
            const auto& f = std::get<CHIRspFlit>(completed.action.flit);
            if (f.flit_opcode == CHI_RSP_Opcode_PCrdGrant)
                ++available_credits;
            else {
                auto live = live_ids.find(retry.req.flit_txn);
                if (live != live_ids.end() && live->second == retry.id)
                    live_ids.erase(live);
            }
            --retry.outputs;
            if (!retry.outputs && retry.grant_issued && retry.ack_issued)
                retries.erase(completed.owner);
        } else {
            auto& r = requests.at(completed.owner);
            if (const auto* f = std::get_if<CHIRspFlit>(&completed.action.flit))
                if (f->flit_opcode == CHI_RSP_Opcode_DBIDResp)
                    r.dbid_sent = true;
            --r.outputs;
            Finish(completed.owner);
        }
    }
    void MemoryDone(const MemoryCompletion& done) {
        auto& r = requests.at(done.handle);
        Check(r.memory_started && !r.memory_done, P,
              "SN.MEM.COMPLETE: unexpected memory completion");
        r.memory_done = true;
        if (IsRead(r.req)) {
            const auto beats = BeatMask(r.req.flit_address, 1u << r.req.flit_size);
            for (unsigned beat = 0; beat < 2; ++beat) {
                if (!(beats & (1u << beat)))
                    continue;
                auto f = Pack(done.data, kAllBytes, beat * 2);
                f.flit_opcode = CHI_DAT_Opcode_CompData;
                f.flit_src = config.node_id;
                f.flit_tgt = config.home_id;
                f.flit_txn = r.req.flit_return_txn_id;
                f.flit_home_nid = config.node_id;
                f.flit_ccid = (r.req.flit_address >> 4) & 3;
                f.flit_resp_err = (done.errors & ByteMask(beat * 32, 32)) ? 2 : 0;
                Emit(r.id, f);
            }
        } else {
            auto f = Response(r.req, CHI_RSP_Opcode_Comp, r.dbid);
            f.flit_resp_err = r.nderr ? 3 : (r.errors ? 2 : 0);
            Emit(r.id, f);
        }
        auto& queue = line_queues.at(LineAddress(r.req.flit_address));
        Check(queue.front() == r.id, P, "SN.ORDER: line service order was lost");
        queue.pop_front();
        if (queue.empty())
            line_queues.erase(LineAddress(r.req.flit_address));
        else
            StartMemory(queue.front());
    }
};

SN::SN(SNConfig config, Observer observer)
    : impl_(std::make_unique<Impl>(config, std::move(observer))) {}
SN::~SN() = default;
const SNConfig& SN::Config() const {
    return impl_->config;
}
ByteMemoryBackend& SN::Memory() {
    return impl_->memory;
}
const ByteMemoryBackend& SN::Memory() const {
    return impl_->memory;
}
void SN::OnEvent(Event event) {
    Check(!impl_->begun, ErrorKind::kLifecycle, "SN.EVENT: deliver events outside Work/Commit");
    impl_->events.push_back(std::move(event));
}
void SN::BeginCycle(std::uint64_t cycle) {
    Check(!impl_->begun && (!impl_->had_cycle || cycle > impl_->cycle), ErrorKind::kLifecycle,
          "SN.CYCLE: cycle must increase and previous cycle must be committed");
    impl_->begun = true;
    impl_->had_cycle = true;
    impl_->cycle = cycle;
    std::stable_sort(impl_->events.begin(), impl_->events.end(),
                     [](const Event& a, const Event& b) {
                         auto priority = [](const Event& e) {
                             if (e.kind == EventKind::kTxAccepted)
                                 return 0;
                             if (e.kind == EventKind::kTxSent)
                                 return 1;
                             return std::holds_alternative<CHIDatFlit>(e.flit) ? 2 : 3;
                         };
                         return priority(a) < priority(b);
                     });
    auto events = std::move(impl_->events);
    impl_->events.clear();
    for (const auto& e : events) {
        if (e.kind != EventKind::kReceive) {
            impl_->Sent(e);
            continue;
        }
        impl_->Observe("rx", 0, 0, "", e.flit);
        if (const auto* f = std::get_if<CHIReqFlit>(&e.flit))
            impl_->Receive(*f);
        else if (const auto* f = std::get_if<CHIDatFlit>(&e.flit))
            impl_->Receive(*f);
        else
            throw VIPError(P, "SN.CHANNEL: SN-F expects only REQ and DAT in this profile");
    }
    for (const auto& done : impl_->memory.Advance(cycle))
        impl_->MemoryDone(done);
    while (!impl_->timers.empty() && impl_->timers.top().cycle <= cycle) {
        const auto id = impl_->timers.top().id;
        impl_->timers.pop();
        Check(!impl_->requests.count(id) && !impl_->retries.count(id), ErrorKind::kTimeout,
              "SN.TIMEOUT: transaction waiting for data, memory, credit or actual send; " +
                  DescribeState());
    }
    if (impl_->requests.empty() && impl_->retries.empty())
        impl_->timers = {};
}
std::vector<SendAction> SN::TakeActions() {
    Check(impl_->begun, ErrorKind::kLifecycle, "SN.ACTION: TakeActions requires Work phase");
    auto result = std::move(impl_->actions);
    impl_->actions.clear();
    return result;
}
void SN::Commit() {
    Check(impl_->begun && impl_->actions.empty(), ErrorKind::kLifecycle,
          "SN.COMMIT: no active cycle or generated actions were not taken");
    impl_->memory.Commit();
    impl_->begun = false;
    if (impl_->observer)
        for (const auto& o : impl_->observations)
            impl_->observer(o);
    impl_->observations.clear();
}
bool SN::IsIdle() const {
    return !impl_->begun && impl_->requests.empty() && impl_->retries.empty() &&
           impl_->outputs.empty() && impl_->events.empty() && impl_->actions.empty() &&
           impl_->buffers.empty() && impl_->line_queues.empty() && impl_->credit_waiters.empty() &&
           !impl_->reserved_credits && !impl_->available_credits && impl_->memory.IsIdle();
}
std::string SN::DescribeState() const {
    std::ostringstream out;
    out << "SN " << impl_->config.node_id << " active=" << impl_->requests.size()
        << " retry=" << impl_->retries.size() << " DBID=" << impl_->buffers.size()
        << " pending_tx=" << impl_->outputs.size() << " reserved_credit=" << impl_->reserved_credits
        << " available_credit=" << impl_->available_credits;
    return out.str();
}
}  // namespace esl::chi_vip
