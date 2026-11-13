#include "hn.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <sstream>
#include <type_traits>

#include "rules.hpp"

namespace esl::chi_vip {
namespace {
constexpr auto P = ErrorKind::kProtocolViolation;
constexpr auto U = ErrorKind::kUnsupportedFeature;
using Key = std::pair<NodeID, TxnID>;
struct Policy {
    unsigned request;
    unsigned snoop;
    bool include_requester;
    const char* rule;
};
// Explicit HN policy choices from B4.3/B4.8 and B5. No LLC: dirty responsibility
// passed by a snoop is written to SN before a clean completion is exposed.
constexpr Policy policies[] = {
    {CHI_REQ_Opcode_ReadNoSnp, 0, false, "HN.READ.NOSNP.B5.2"},
    {CHI_REQ_Opcode_ReadOnce, CHI_SNP_Opcode_SnpOnce, false, "HN.READ.ONCE.B5.2"},
    {CHI_REQ_Opcode_ReadShared, CHI_SNP_Opcode_SnpShared, false, "HN.READ.SHARED.B5.2"},
    {CHI_REQ_Opcode_ReadClean, CHI_SNP_Opcode_SnpClean, false, "HN.READ.CLEAN.B5.2"},
    {CHI_REQ_Opcode_ReadUnique, CHI_SNP_Opcode_SnpUnique, false, "HN.READ.UNIQUE.B5.2"},
    {CHI_REQ_Opcode_ReadNotSharedDirty, CHI_SNP_Opcode_SnpNotSharedDirty, false,
     "HN.READ.NSD.B5.2"},
    {CHI_REQ_Opcode_WriteNoSnpFull, 0, false, "HN.WRITE.NOSNP.B5.3"},
    {CHI_REQ_Opcode_WriteNoSnpPtl, 0, false, "HN.WRITE.NOSNP.PTL.B5.3"},
    {CHI_REQ_Opcode_WriteUniqueFull, CHI_SNP_Opcode_SnpUnique, false, "HN.WRITE.UNIQUE.B5.3"},
    {CHI_REQ_Opcode_WriteUniquePtl, CHI_SNP_Opcode_SnpUnique, false, "HN.WRITE.UNIQUE.PTL.B5.3"},
    {CHI_REQ_Opcode_WriteBackFull, 0, false, "HN.COPYBACK.B5.3"},
    {CHI_REQ_Opcode_WriteBackPtl, 0, false, "HN.COPYBACK.PTL.B5.3"},
    {CHI_REQ_Opcode_WriteCleanFull, 0, false, "HN.COPYBACK.CLEAN.B5.3"},
    {CHI_REQ_Opcode_WriteEvictFull, 0, false, "HN.COPYBACK.EVICT.B5.3"},
    {CHI_REQ_Opcode_CleanUnique, CHI_SNP_Opcode_SnpUnique, false, "HN.CMO.UNIQUE.B5.4"},
    {CHI_REQ_Opcode_MakeUnique, CHI_SNP_Opcode_SnpMakeInvalid, false, "HN.CMO.MAKE.B5.4"},
    {CHI_REQ_Opcode_Evict, 0, false, "HN.EVICT.B5.4"},
    {CHI_REQ_Opcode_CleanShared, CHI_SNP_Opcode_SnpCleanShared, true, "HN.CMO.CLEAN.B5.4"},
    {CHI_REQ_Opcode_CleanInvalid, CHI_SNP_Opcode_SnpCleanInvalid, false, "HN.CMO.CI.B5.4"},
    {CHI_REQ_Opcode_MakeInvalid, CHI_SNP_Opcode_SnpMakeInvalid, false, "HN.CMO.MI.B5.4"},
};
const Policy& Select(unsigned opcode) {
    for (const auto& p : policies)
        if (p.request == opcode)
            return p;
    static const std::set<unsigned> extensions = {
        0x11, 0x13, 0x14, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2a, 0x2b,
        0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x3a, 0x41, 0x42, 0x43, 0x44, 0x47, 0x48, 0x4c, 0x4d, 0x4e, 0x50, 0x51, 0x52, 0x54,
        0x56, 0x58, 0x59, 0x5a, 0x5c, 0x5e, 0x60, 0x61, 0x62, 0x64, 0x66, 0x70, 0x71, 0x79};
    throw VIPError(extensions.count(opcode) ? U : P, "HN.REQ.FEATURE: unsupported/reserved opcode");
}
struct Assembly {
    Line data{};
    std::uint64_t valid = 0, errors = 0;
    unsigned beats = 0;
    std::optional<CHIDatFlit> first;
    void Add(const CHIDatFlit& f, unsigned expected, bool read, std::uint64_t window) {
        Check(f.flit_data_id == 0 || f.flit_data_id == 2, P, "HN.DAT: invalid DataID");
        const auto bit = 1u << (f.flit_data_id / 2);
        Check((expected & bit) && !(beats & bit), P, "HN.DAT: duplicate/unexpected beat");
        Check(f.flit_be <= 0xffffffffULL, P, "HN.DAT: BE exceeds 256-bit payload");
        if (first)
            Check(first->flit_opcode == f.flit_opcode && first->flit_resp == f.flit_resp &&
                      first->flit_ccid == f.flit_ccid && first->flit_cah == f.flit_cah &&
                      ((first->flit_resp_err == 3) == (f.flit_resp_err == 3)),
                  P, "HN.DAT: inconsistent multi-packet message");
        else
            first = f;
        const auto offset = f.flit_data_id * 16;
        const auto enabled = read ? ByteMask(offset, 32) : (f.flit_be << offset);
        Check(read || !(enabled & ~window), P, "HN.DAT: byte enable outside request window");
        const auto bytes = Unpack(f);
        for (unsigned i = offset; i < offset + 32; ++i) {
            if ((enabled >> i) & 1)
                data[i] = bytes[i];
            else if (!read)
                Check(bytes[i] == 0, P, "HN.DAT: disabled byte must be zero");
        }
        valid |= enabled & window;
        if (f.flit_resp_err == 2)
            errors |= enabled & window;
        beats |= bit;
    }
};
void Merge(Line& into, const Line& from, std::uint64_t mask) {
    for (unsigned i = 0; i < 64; ++i)
        if ((mask >> i) & 1)
            into[i] = from[i];
}
std::uint64_t Window(const CHIReqFlit& q) {
    const unsigned size = 1u << q.flit_size;
    const auto offset = static_cast<unsigned>(q.flit_address & 63);
    const auto start = q.flit_mem_attr & 2 ? offset : offset & ~(size - 1);
    return ByteMask(start, size - (start & (size - 1)));
}
}  // namespace

void HNConfig::Validate() const {
    Check(node_id < 128 && slave_id < 128 && node_id != slave_id && !requester_ids.empty(),
          ErrorKind::kConfiguration, "HN.CONFIG: invalid NodeIDs");
    std::set<NodeID> nodes{node_id, slave_id};
    for (auto id : requester_ids)
        Check(id < 128 && nodes.insert(id).second, ErrorKind::kConfiguration,
              "HN.CONFIG: duplicate/invalid requester NodeID");
    Check(memory_size >= 64 && memory_size % 64 == 0 && request_slots > 0 &&
              request_slots <= 1024 && request_slots * requester_ids.size() <= 4096 &&
              memory_size <= (std::uint64_t{1} << 48) && timeout_cycles > 0,
          ErrorKind::kConfiguration, "HN.CONFIG: invalid capacity or memory range");
}

struct HN::Impl {
    struct Context {
        Handle id = 0;
        CHIReqFlit req;
        DBID dbid = 0;
        std::uint64_t line = 0, deadline = 0;
        bool started = false, snoops_issued = false, response = false, ack = false;
        bool write_buffer = false, buffer_sent = false, write_done = false;
        bool flush_done = false, read_done = false;
        bool non_data_error = false;
        unsigned pending_snoops = 0, in_flight = 0, completion_pending = 0, result_resp = 0;
        std::optional<TxnID> downstream;
        std::optional<NodeID> data_provider;
        Line latest{};
        std::uint64_t latest_valid = 0, latest_errors = 0, flush_mask = 0;
        Assembly input;
    };
    enum class Purpose { kRead, kFlush, kWrite };
    struct Downstream {
        Handle parent = 0;
        CHIReqFlit req;
        Purpose purpose = Purpose::kRead;
        Line data{};
        std::uint64_t mask = 0, errors = 0;
        bool sent = false, retry = false, receipt = false, dbid = false, comp = false;
        bool data_issued = false, non_data_error = false;
        DBID buffer = 0;
        unsigned in_flight = 0;
        Assembly input;
    };
    struct Snoop {
        Handle parent = 0;
        NodeID target = 0;
        unsigned opcode = 0;
        bool sent = false;
        Assembly input;
    };
    struct RetryWait {
        CHIReqFlit req;
        std::uint64_t deadline;
    };
    enum class OutputKind { kPlain, kDownReq, kDownData, kSnoop, kCompletion, kBuffer, kGrant };
    struct Output {
        SendAction action;
        Handle owner = 0;
        OutputKind kind = OutputKind::kPlain;
        TxnID child = 0;
        bool accepted = false;
    };
    HNConfig config;
    Observer observer;
    bool working = false, cycled = false;
    std::uint64_t cycle = 0;
    Handle next_handle = 1;
    Token next_token = 1;
    std::map<Handle, Context> contexts;
    std::map<Key, Handle> requests, buffers;
    std::map<std::uint64_t, std::deque<Handle>> line_queue;
    std::map<NodeID, std::deque<Handle>> order_queue;
    std::map<std::uint64_t, DirectoryEntry> directory, committed_directory;
    std::set<std::uint64_t> changed;
    std::map<TxnID, Snoop> snoops;
    std::map<TxnID, Downstream> downstream;
    std::map<DBID, TxnID> slave_buffers;
    std::deque<unsigned> free_dbids, free_down, free_snoops;
    std::map<Token, Output> outputs;
    std::vector<SendAction> actions;
    std::vector<Event> events;
    std::vector<Observation> observations;
    std::deque<Handle> ready;
    std::set<Handle> ready_set;
    std::map<std::pair<std::uint64_t, Handle>, bool> timers;
    std::deque<RetryWait> rejected;
    std::set<Key> retry_wait_ids;
    std::map<NodeID, std::deque<std::pair<std::uint64_t, Handle>>> credit_expiry;
    std::map<std::pair<std::uint64_t, Handle>, NodeID> credit_timers;
    std::map<NodeID, unsigned> credits, available_credits;
    unsigned reserved = 0, slave_credits = 0;
    std::deque<TxnID> retry_down;

    Impl(HNConfig c, Observer o) : config(std::move(c)), observer(std::move(o)) {
        config.Validate();
        for (unsigned i = 0; i < 4096; ++i) {
            free_dbids.push_back(i);
            free_down.push_back(i);
            free_snoops.push_back(i);
        }
    }
    bool IsRequester(NodeID id) const {
        return std::find(config.requester_ids.begin(), config.requester_ids.end(), id) !=
               config.requester_ids.end();
    }
    void Observe(const std::string& action, Handle id, const std::string& rule,
                 std::optional<Flit> flit = {}, std::optional<std::uint64_t> at = {}) {
        Observation o;
        o.cycle = at.value_or(cycle);
        o.handle = id;
        o.action = action;
        o.rule = rule;
        o.flit = std::move(flit);
        if (contexts.count(id))
            o.address = contexts.at(id).line;
        observations.push_back(std::move(o));
    }
    void Ready(Handle id) {
        if (contexts.count(id) && ready_set.insert(id).second)
            ready.push_back(id);
    }
    void Emit(Handle owner, Flit flit, NodeID destination, OutputKind kind = OutputKind::kPlain,
              TxnID child = 0) {
        SendAction action{next_token++, destination, std::move(flit)};
        if (owner)
            ++contexts.at(owner).in_flight;
        if (kind == OutputKind::kCompletion)
            ++contexts.at(owner).completion_pending;
        if (kind == OutputKind::kDownReq || kind == OutputKind::kDownData)
            ++downstream.at(child).in_flight;
        outputs.emplace(action.token, Output{action, owner, kind, child, false});
        actions.push_back(std::move(action));
    }
    CHIRspFlit Response(const CHIReqFlit& q, unsigned opcode, unsigned resp = 0,
                        DBID dbid = 0) const {
        CHIRspFlit f;
        f.flit_src = config.node_id;
        f.flit_tgt = q.flit_src;
        f.flit_txn = q.flit_txn;
        f.flit_opcode = opcode;
        f.flit_resp = resp;
        f.flit_dbid = dbid;
        return f;
    }
    void Validate(const CHIReqFlit& q) const {
        Check(IsRequester(q.flit_src) && q.flit_tgt == config.node_id && q.flit_txn < 4096 &&
                  q.flit_qos < 16 && q.flit_pcrd_type < 16 && q.flit_allow_retry < 2,
              P, "HN.REQ.FIELD: invalid routing/width");
        if (q.flit_opcode == CHI_REQ_Opcode_PCrdReturn)
            return;
        Select(q.flit_opcode);
        Check(q.flit_ns == 1 && q.flit_nse == 0 && !q.flit_do_dwt && !q.flit_excl && !q.flit_mpam &&
                  !q.flit_pbha && !q.flit_mecid && !q.flit_stream_id && !q.flit_sec_sid1 &&
                  !q.flit_tag_op && !q.flit_stash_nid_valid && !q.flit_stash_lpid_valid,
              U, "HN.REQ.FEATURE: security/extension outside GbCore");
        Check(q.flit_size <= 6 && q.flit_order <= 3 && q.flit_cah < 2 && q.flit_exp_comp_ack < 2 &&
                  q.flit_likely_shared < 2 && q.flit_trace_tag < 2 && !q.flit_return_nid &&
                  !q.flit_return_txn_id && !q.flit_stash_nid && !q.flit_data_target &&
                  !q.flit_endian && !q.flit_deep && !q.flit_prefetch_tgt_hint &&
                  !q.flit_stash_lpid && !q.flit_p_group_id && !q.flit_stash_group_id &&
                  !q.flit_tag_group_id && !q.flit_lpid && !q.flit_snoop_me && !q.flit_rsvdc,
              P, "HN.REQ.FIELD: invalid/inapplicable field");
        const auto& t = Traits(q.flit_opcode);
        Check(q.flit_cah == 0 ||
                  (t.family == Family::kCopyBack && q.flit_opcode != CHI_REQ_Opcode_WriteBackPtl),
              P, "HN.REQ.CAH: CAH not applicable");
        const bool coherent = q.flit_opcode != CHI_REQ_Opcode_ReadNoSnp &&
                              q.flit_opcode != CHI_REQ_Opcode_WriteNoSnpFull &&
                              q.flit_opcode != CHI_REQ_Opcode_WriteNoSnpPtl;
        Check(q.flit_snp_attr == unsigned(coherent), P, "HN.REQ.SNPATTR: wrong SnpAttr");
        Check(q.flit_mem_attr == 5 || q.flit_mem_attr == 13 || (q.flit_mem_attr == 2 && !coherent),
              U, "HN.REQ.MEMATTR: unsupported memory attributes");
        Check(q.flit_opcode != CHI_REQ_Opcode_WriteEvictFull || (q.flit_mem_attr & 8), P,
              "HN.REQ.MEMATTR: WriteEvictFull requires Allocate");
        RNConfig validator;
        validator.node_id = q.flit_src;
        validator.home_id = config.node_id;
        validator.memory_size = config.memory_size;
        TxnRequest request;
        request.opcode = q.flit_opcode;
        request.address = q.flit_address;
        request.size = 1u << q.flit_size;
        request.order = q.flit_order;
        request.exp_comp_ack = bool(q.flit_exp_comp_ack);
        request.memory_type = q.flit_mem_attr == 2 ? MemoryType::kDeviceNRnE : MemoryType::kNormal;
        request.byte_enable = Window(q);
        try {
            ValidateRequest(request, validator);
        } catch (const VIPError& e) {
            throw VIPError(e.Kind() == U ? U : P, std::string("HN.REQ: ") + e.what());
        }
    }
    void Receive(const CHIReqFlit& q) {
        Validate(q);
        Observe("rx", 0, "HN.REQ.B2.4", q);
        if (q.flit_opcode == CHI_REQ_Opcode_PCrdReturn) {
            Check(!q.flit_allow_retry && q.flit_pcrd_type == 1 && available_credits[q.flit_src] > 0,
                  P, "HN.CREDIT: return without a grant");
            --credits[q.flit_src];
            --available_credits[q.flit_src];
            --reserved;
            ConsumeCreditTimer(q.flit_src);
            Grant();
            return;
        }
        const Key key{q.flit_src, q.flit_txn};
        Check(!requests.count(key) && !retry_wait_ids.count(key), P,
              "HN.TXN: duplicate live requester TxnID");
        if (!q.flit_allow_retry) {
            Check(q.flit_pcrd_type == 1 && available_credits[q.flit_src] > 0, P,
                  "HN.CREDIT: AllowRetry=0 without reserved protocol credit");
            --credits[q.flit_src];
            --available_credits[q.flit_src];
            --reserved;
            ConsumeCreditTimer(q.flit_src);
        } else {
            Check(q.flit_pcrd_type == 0, P, "HN.CREDIT: initial request PCrdType must be zero");
            if (contexts.size() + reserved >= config.request_slots || !rejected.empty()) {
                rejected.push_back({q, Deadline()});
                retry_wait_ids.insert(key);
                if (!config.grant_before_retry) {
                    auto retry = Response(q, CHI_RSP_Opcode_RetryAck);
                    retry.flit_pcrd_type = 1;
                    Emit(0, retry, q.flit_src);
                }
                Grant();
                return;
            }
        }
        Check(!free_dbids.empty(), P, "HN.DBID: pool exhausted");
        Context c;
        c.id = next_handle++;
        c.req = q;
        c.line = LineAddress(q.flit_address);
        c.dbid = free_dbids.front();
        free_dbids.pop_front();
        c.deadline = Deadline();
        requests.emplace(key, c.id);
        buffers.emplace(Key{q.flit_src, c.dbid}, c.id);
        timers.emplace(std::make_pair(c.deadline, c.id), true);
        line_queue[c.line].push_back(c.id);
        if (q.flit_order)
            order_queue[q.flit_src].push_back(c.id);
        contexts.emplace(c.id, c);
        Observe("rule", c.id, Select(q.flit_opcode).rule);
        Ready(c.id);
    }
    std::uint64_t Deadline() const {
        Check(cycle <= std::numeric_limits<std::uint64_t>::max() - config.timeout_cycles,
              ErrorKind::kLifecycle, "HN.TIME: diagnostic deadline overflow");
        return cycle + config.timeout_cycles;
    }
    void ConsumeCreditTimer(NodeID node) {
        auto& queue = credit_expiry.at(node);
        credit_timers.erase(queue.front());
        queue.pop_front();
        if (queue.empty())
            credit_expiry.erase(node);
    }
    void Grant() {
        while (!rejected.empty() && contexts.size() + reserved < config.request_slots) {
            const auto waiting = rejected.front();
            const auto q = waiting.req;
            rejected.pop_front();
            retry_wait_ids.erase({q.flit_src, q.flit_txn});
            const auto timer = std::make_pair(waiting.deadline, next_handle++);
            credit_expiry[q.flit_src].push_back(timer);
            credit_timers.emplace(timer, q.flit_src);
            ++reserved;
            ++credits[q.flit_src];
            auto grant = Response(q, CHI_RSP_Opcode_PCrdGrant);
            grant.flit_txn = 0;
            grant.flit_pcrd_type = 1;
            Emit(0, grant, q.flit_src, OutputKind::kGrant);
            if (config.grant_before_retry) {
                auto retry = Response(q, CHI_RSP_Opcode_RetryAck);
                retry.flit_pcrd_type = 1;
                Emit(0, retry, q.flit_src);
            }
        }
    }
    void SetPermission(std::uint64_t line, NodeID node, unsigned state) {
        auto& d = directory[line];
        d.sharers.erase(node);
        if (d.owner == node)
            d.owner.reset();
        if (state == 2) {
            Check(!d.owner || *d.owner == node, P, "HN.DIR: two unique owners");
            Check(d.sharers.empty(), P, "HN.DIR: unique owner with other sharers");
            d.owner = node;
        } else if (state == 1 || state == 3) {
            Check(!d.owner, P, "HN.DIR: shared permission while a unique owner remains");
            d.sharers.insert(node);
        }
        changed.insert(line);
    }
    std::set<NodeID> Holders(std::uint64_t line) const {
        auto it = directory.find(line);
        if (it == directory.end())
            return {};
        auto result = it->second.sharers;
        if (it->second.owner)
            result.insert(*it->second.owner);
        return result;
    }
    void Start(Context& c) {
        c.started = true;
        const auto& p = Select(c.req.flit_opcode);
        if (p.request == CHI_REQ_Opcode_Evict || p.request == CHI_REQ_Opcode_CleanInvalid ||
            p.request == CHI_REQ_Opcode_MakeInvalid)
            SetPermission(c.line, c.req.flit_src, 0);
        if (p.snoop) {
            for (auto node : Holders(c.line)) {
                if (!p.include_requester && node == c.req.flit_src)
                    continue;
                Check(!free_snoops.empty(), P, "HN.SNP: identifier pool exhausted");
                const auto id = free_snoops.front();
                free_snoops.pop_front();
                CHISnpFlit f;
                f.flit_src = config.node_id;
                f.flit_txn = id;
                f.flit_opcode = p.snoop;
                f.flit_address = c.req.flit_address >> 3;
                f.flit_ns = 1;
                f.flit_do_not_go_to_sd = 1;
                snoops.emplace(id, Snoop{c.id, node, p.snoop, false, {}});
                ++c.pending_snoops;
                Emit(c.id, f, node, OutputKind::kSnoop, id);
            }
        }
        c.snoops_issued = true;
    }
    void SnoopDone(TxnID id, unsigned resp, unsigned error, const Assembly* data) {
        const auto s = snoops.at(id);
        Check(s.sent, P, "HN.SNP: response before snoop was actually transmitted");
        auto& c = contexts.at(s.parent);
        if (error == 3)
            c.non_data_error = true;
        else {
            const auto state = resp & 3;
            const bool partial = data && data->first->flit_opcode == CHI_DAT_Opcode_SnpRespDataPtl;
            unsigned allowed = 0;
            switch (s.opcode) {
                case CHI_SNP_Opcode_SnpOnce:
                    allowed = partial ? (1u << 2) | (1u << 4) : data ? 0x3f : 0x07;
                    break;
                case CHI_SNP_Opcode_SnpShared:
                case CHI_SNP_Opcode_SnpClean:
                case CHI_SNP_Opcode_SnpNotSharedDirty:
                    allowed = partial ? (1u << 4) : data ? 0x33 : 0x03;
                    break;
                case CHI_SNP_Opcode_SnpUnique:
                    allowed = partial ? (1u << 4) : data ? 0x11 : 0x01;
                    break;
                case CHI_SNP_Opcode_SnpCleanShared:
                    allowed = partial ? (1u << 4) : data ? 0x70 : 0x07;
                    break;
                case CHI_SNP_Opcode_SnpCleanInvalid:
                    allowed = data ? (1u << 4) : 0x01;
                    break;
                case CHI_SNP_Opcode_SnpMakeInvalid:
                    allowed = data ? 0 : 0x01;
                    break;
            }
            Check(allowed & (1u << resp), P,
                  "HN.SNP: response state illegal for snoop opcode/form");
            // Once may retain dirty responsibility or pass it while cleaning.
            // Other selected snoops have DoNotGoToSD set (B4.8/B4.10).
            Check(s.opcode == CHI_SNP_Opcode_SnpOnce || state != 3, P,
                  "HN.SNP: DoNotGoToSD violated");
            if (s.opcode == CHI_SNP_Opcode_SnpUnique ||
                s.opcode == CHI_SNP_Opcode_SnpCleanInvalid ||
                s.opcode == CHI_SNP_Opcode_SnpMakeInvalid)
                Check(state == 0, P, "HN.SNP: invalidating snoop retained a copy");
            Check(!(resp & 4) || data, P, "HN.SNP: dirty responsibility without data");
            SetPermission(c.line, s.target, state);
            if (data) {
                Check(!c.data_provider || *c.data_provider == s.target, P,
                      "HN.SNP: multiple data owners for one line");
                c.data_provider = s.target;
                Merge(c.latest, data->data, data->valid);
                c.latest_valid |= data->valid;
                c.latest_errors = (c.latest_errors & ~data->valid) | data->errors;
                if (resp & 4)
                    c.flush_mask |= data->valid;
            }
        }
        --c.pending_snoops;
        free_snoops.push_back(id);
        snoops.erase(id);
        Ready(c.id);
    }
    void Common(const CHIRspFlit& f) const {
        Check(f.flit_tgt == config.node_id && f.flit_txn < 4096 && f.flit_dbid < 4096 &&
                  f.flit_resp < 8 && f.flit_resp_err < 4 && f.flit_qos < 16 && f.flit_c_busy < 8 &&
                  f.flit_pcrd_type < 16 && f.flit_trace_tag < 2 && !f.flit_fwd_state &&
                  !f.flit_data_pull && !f.flit_p_group_id && !f.flit_stash_group_id &&
                  !f.flit_tag_group_id,
              P, "HN.RSP.FIELD: invalid routing/field");
        Check(!f.flit_tag_op && f.flit_resp_err != 1, U, "HN.RSP.FEATURE: unsupported response");
    }
    void Receive(const CHIRspFlit& f) {
        Common(f);
        if (f.flit_src == config.slave_id) {
            ReceiveSlave(f);
            return;
        }
        Check(IsRequester(f.flit_src), P, "HN.RSP: unknown source");
        if (f.flit_opcode == CHI_RSP_Opcode_SnpResp) {
            auto it = snoops.find(f.flit_txn);
            Check(it != snoops.end() && it->second.target == f.flit_src && !it->second.input.beats,
                  P, "HN.SNP: unknown/duplicate snoop response");
            Observe("rx", it->second.parent, "HN.SNP.RESPONSE.B4.8", f);
            SnoopDone(f.flit_txn, f.flit_resp, f.flit_resp_err, nullptr);
        } else if (f.flit_opcode == CHI_RSP_Opcode_CompAck) {
            auto it = buffers.find({f.flit_src, f.flit_txn});
            Check(it != buffers.end(), P, "HN.ACK: unknown DBID");
            auto& c = contexts.at(it->second);
            Check(c.req.flit_exp_comp_ack && c.response && !c.completion_pending && !c.ack &&
                      f.flit_resp_err == 0 && f.flit_resp == 0,
                  P, "HN.ACK: unexpected/duplicate CompAck");
            c.ack = true;
            Observe("rx", c.id, "HN.ACK.B2.6", f);
            Ready(c.id);
        } else
            throw VIPError(P, "HN.RSP: unexpected requester response");
    }
    void Common(const CHIDatFlit& f) const {
        Check(f.flit_tgt == config.node_id && f.flit_txn < 4096 && f.flit_dbid < 4096 &&
                  f.flit_resp < 8 && f.flit_resp_err < 4 && f.flit_qos < 16 && f.flit_ccid < 4 &&
                  f.flit_cah < 2 && f.flit_c_busy < 8 && f.flit_trace_tag < 2 &&
                  !f.flit_fwd_state && !f.flit_data_pull && !f.flit_num_dat && !f.flit_replicate &&
                  !f.flit_rsvdc,
              P, "HN.DAT.FIELD: invalid routing/field");
        Check(!f.flit_tag_op && !f.flit_tag && !f.flit_tu && !f.flit_mecid && !f.flit_pbha &&
                  !f.flit_poison && !f.flit_data_check && f.flit_resp_err != 1,
              U, "HN.DAT.FEATURE: unsupported data extension");
    }
    void Receive(const CHIDatFlit& f) {
        Common(f);
        if (f.flit_src == config.slave_id) {
            ReceiveSlave(f);
            return;
        }
        Check(IsRequester(f.flit_src), P, "HN.DAT: unknown source");
        if (f.flit_opcode == CHI_DAT_Opcode_SnpRespData ||
            f.flit_opcode == CHI_DAT_Opcode_SnpRespDataPtl) {
            auto it = snoops.find(f.flit_txn);
            Check(it != snoops.end() && it->second.target == f.flit_src, P,
                  "HN.SNP: unknown/duplicate snoop data");
            auto& s = it->second;
            Check(s.sent, P, "HN.SNP: data before snoop actually transmitted");
            const auto& c = contexts.at(s.parent);
            Check(f.flit_ccid == ((c.req.flit_address >> 4) & 3), P, "HN.SNP: incorrect CCID");
            Check(s.opcode != CHI_SNP_Opcode_SnpMakeInvalid, P,
                  "HN.SNP: MakeInvalid cannot return data");
            if (f.flit_opcode == CHI_DAT_Opcode_SnpRespData)
                Check(f.flit_be == 0xffffffffULL, P, "HN.SNP: incomplete full snoop data");
            else
                Check(f.flit_cah == 0, P, "HN.SNP: partial data CAH must be zero");
            Observe("rx", c.id, "HN.SNP.DATA.B4.8", f);
            s.input.Add(f, 3, false, kAllBytes);
            if (s.input.beats == 3)
                SnoopDone(f.flit_txn, f.flit_resp, f.flit_resp_err, &s.input);
            return;
        }
        auto it = buffers.find({f.flit_src, f.flit_txn});
        Check(it != buffers.end(), P, "HN.WRITE: unknown DBID");
        auto& c = contexts.at(it->second);
        const auto family = Traits(c.req.flit_opcode).family;
        const bool cb = family == Family::kCopyBack;
        Check(c.write_buffer && c.buffer_sent && !c.write_done &&
                  f.flit_opcode ==
                      (cb ? CHI_DAT_Opcode_CopyBackWriteData : CHI_DAT_Opcode_NonCopyBackWriteData),
              P, "HN.WRITE: unexpected data opcode/phase");
        Check(f.flit_ccid == ((c.req.flit_address >> 4) & 3), P, "HN.WRITE: incorrect CCID");
        if (f.flit_resp_err != 3) {
            if (cb) {
                unsigned allowed = 0xc7;  // I, SC, UC, UD_PD, SD_PD (B4.5/B4.7.3).
                if (c.req.flit_opcode == CHI_REQ_Opcode_WriteBackPtl)
                    allowed = 0x41;
                if (c.req.flit_opcode == CHI_REQ_Opcode_WriteEvictFull)
                    allowed = 0x07;
                Check(allowed & (1u << f.flit_resp), P,
                      "HN.COPYBACK.RESP: response state illegal for CopyBack opcode");
            } else
                Check(f.flit_resp == 0, P, "HN.WRITE: non-CopyBack Resp must be zero");
        }
        Observe("rx", c.id, "HN.WRITE.DATA.B2.8", f);
        c.input.Add(f, BeatMask(c.req.flit_address, 1u << c.req.flit_size), false, Window(c.req));
        if (f.flit_resp_err == 3)
            c.non_data_error = true;
        if (c.input.beats != BeatMask(c.req.flit_address, 1u << c.req.flit_size))
            return;
        c.write_done = true;
        if (!c.non_data_error) {
            const bool partial = c.req.flit_opcode == CHI_REQ_Opcode_WriteBackPtl ||
                                 c.req.flit_opcode == CHI_REQ_Opcode_WriteUniquePtl ||
                                 c.req.flit_opcode == CHI_REQ_Opcode_WriteNoSnpPtl;
            if (cb && f.flit_resp == 0)
                Check(c.input.valid == 0, P, "HN.COPYBACK.I: invalid data must have zero BE");
            else if (!partial)
                Check(c.input.valid == kAllBytes, P, "HN.WRITE.FULL: missing enabled bytes");
            Merge(c.latest, c.input.data, c.input.valid);
            c.latest_valid |= c.input.valid;
            c.latest_errors = (c.latest_errors & ~c.input.valid) | c.input.errors;
            c.flush_mask |= c.input.valid;
        }
        if (cb) {
            const unsigned retained = c.req.flit_opcode == CHI_REQ_Opcode_WriteCleanFull
                                          ? ((f.flit_resp & 3) == 2 ? 2 : (f.flit_resp & 1))
                                          : 0;
            SetPermission(c.line, c.req.flit_src, retained);
        }
        Ready(c.id);
    }
    void StartDownstream(Context& c, Purpose purpose) {
        Check(!c.downstream && !free_down.empty(), P, "HN.DOWN: resource unavailable");
        const auto id = free_down.front();
        free_down.pop_front();
        Downstream d;
        d.parent = c.id;
        d.purpose = purpose;
        d.data = c.latest;
        d.mask = c.flush_mask;
        d.errors = c.latest_errors;
        d.req.flit_src = config.node_id;
        d.req.flit_tgt = config.slave_id;
        d.req.flit_txn = id;
        d.req.flit_return_nid = config.node_id;
        d.req.flit_return_txn_id = id;
        d.req.flit_opcode = purpose == Purpose::kRead ? CHI_REQ_Opcode_ReadNoSnp
                            : d.mask == kAllBytes     ? CHI_REQ_Opcode_WriteNoSnpFull
                                                      : CHI_REQ_Opcode_WriteNoSnpPtl;
        d.req.flit_address = c.line;
        d.req.flit_size = 6;
        d.req.flit_ns = 1;
        d.req.flit_mem_attr = 5;
        d.req.flit_order = 1;  // Request Accepted, HN->SN only (B2.6).
        d.req.flit_allow_retry = 1;
        downstream.emplace(id, d);
        c.downstream = id;
        Emit(c.id, d.req, config.slave_id, OutputKind::kDownReq, id);
    }
    Downstream& Down(TxnID id) {
        auto it = downstream.find(id);
        Check(it != downstream.end(), P, "HN.DOWN: unknown/retired TxnID");
        return it->second;
    }
    void ReceiveSlave(const CHIRspFlit& f) {
        if (f.flit_opcode == CHI_RSP_Opcode_PCrdGrant) {
            Check(f.flit_pcrd_type == 1 && f.flit_resp_err == 0, P,
                  "HN.DOWN.CREDIT: invalid grant");
            ++slave_credits;
            Observe("rx", 0, "HN.DOWN.GRANT.B2.9", f);
            RetryDownstream();
            return;
        }
        auto& d = Down(f.flit_txn);
        Observe("rx", d.parent, "HN.DOWN.RSP.B2.4", f);
        Check(d.sent && !(d.retry && d.req.flit_allow_retry), P,
              "HN.DOWN: response before request sent or after RetryAck");
        switch (f.flit_opcode) {
            case CHI_RSP_Opcode_RetryAck:
                Check(d.req.flit_allow_retry && !d.retry && !d.receipt && !d.dbid && !d.comp &&
                          !d.input.beats && f.flit_pcrd_type == 1 && f.flit_resp_err == 0,
                      P, "HN.DOWN.RETRY: invalid RetryAck");
                d.retry = true;
                retry_down.push_back(f.flit_txn);
                RetryDownstream();
                return;
            case CHI_RSP_Opcode_ReadReceipt:
                Check(d.purpose == Purpose::kRead && !d.receipt && f.flit_resp_err == 0, P,
                      "HN.DOWN.ORDER: unexpected/duplicate ReadReceipt");
                d.receipt = true;
                break;
            case CHI_RSP_Opcode_DBIDResp:
            case CHI_RSP_Opcode_DBIDRespOrd:
            case CHI_RSP_Opcode_CompDBIDResp:
                Check(d.purpose != Purpose::kRead && !d.dbid &&
                          (f.flit_opcode == CHI_RSP_Opcode_CompDBIDResp || f.flit_resp_err == 0),
                      P, "HN.DOWN.DBID: unexpected/duplicate DBIDResp");
                Check(!d.comp || d.buffer == f.flit_dbid, P, "HN.DOWN.DBID: inconsistent DBID");
                Check(slave_buffers.emplace(f.flit_dbid, f.flit_txn).second, P,
                      "HN.DOWN.DBID: SN reused live DBID");
                d.dbid = true;
                d.buffer = f.flit_dbid;
                if (f.flit_opcode != CHI_RSP_Opcode_CompDBIDResp)
                    break;
                [[fallthrough]];
            case CHI_RSP_Opcode_Comp:
                Check(
                    d.purpose != Purpose::kRead && !d.comp && (!d.dbid || d.buffer == f.flit_dbid),
                    P, "HN.DOWN.COMP: duplicate/unexpected Comp or DBID mismatch");
                Check(f.flit_resp_err == 3 || f.flit_resp == 0, P,
                      "HN.DOWN.COMP: invalid response state");
                d.buffer = f.flit_dbid;
                d.comp = true;
                d.non_data_error = f.flit_resp_err == 3;
                break;
            default:
                throw VIPError(P, "HN.DOWN.RSP: unexpected response opcode");
        }
        ProgressDownstream(f.flit_txn);
    }
    void ReceiveSlave(const CHIDatFlit& f) {
        auto& d = Down(f.flit_txn);
        Check(d.sent && !(d.retry && d.req.flit_allow_retry) && d.purpose == Purpose::kRead &&
                  f.flit_opcode == CHI_DAT_Opcode_CompData && f.flit_home_nid == config.slave_id &&
                  !f.flit_ccid && (f.flit_resp_err == 3 || f.flit_resp == 0 || f.flit_resp == 2),
              P, "HN.DOWN.DAT: unexpected data/response field");
        Observe("rx", d.parent, "HN.DOWN.DATA.B2.8", f);
        d.input.Add(f, 3, true, kAllBytes);
        d.non_data_error = f.flit_resp_err == 3;
        ProgressDownstream(f.flit_txn);
    }
    void RetryDownstream() {
        while (slave_credits && !retry_down.empty()) {
            const auto id = retry_down.front();
            retry_down.pop_front();
            auto& d = Down(id);
            Check(d.retry && d.sent && !d.in_flight, P, "HN.DOWN.RETRY: premature reissue");
            --slave_credits;
            d.req.flit_allow_retry = 0;
            d.req.flit_pcrd_type = 1;
            d.sent = false;
            Emit(d.parent, d.req, config.slave_id, OutputKind::kDownReq, id);
        }
    }
    void ProgressDownstream(TxnID id) {
        auto& d = Down(id);
        if (!d.sent)
            return;
        if (d.purpose != Purpose::kRead && d.dbid && !d.data_issued) {
            for (unsigned beat = 0; beat < 2; ++beat) {
                auto f = Pack(d.data, d.mask, beat * 2);
                f.flit_src = config.node_id;
                f.flit_tgt = config.slave_id;
                f.flit_txn = d.buffer;
                f.flit_opcode = CHI_DAT_Opcode_NonCopyBackWriteData;
                f.flit_resp_err = (d.errors & ByteMask(beat * 32, 32)) ? 2 : 0;
                Emit(d.parent, f, config.slave_id, OutputKind::kDownData, id);
            }
            d.data_issued = true;
        }
        if (d.data_issued && !d.in_flight)
            slave_buffers.erase(d.buffer);
        const bool done =
            d.purpose == Purpose::kRead ? d.input.beats == 3 && d.receipt : d.data_issued && d.comp;
        if (!done || d.in_flight)
            return;
        auto& c = contexts.at(d.parent);
        c.non_data_error |= d.non_data_error;
        if (d.purpose == Purpose::kRead) {
            // Retained dirty SnpOnce bytes override memory; clean/unique reads
            // have already committed all transferred dirty bytes to SN.
            Line result = d.input.data;
            Merge(result, c.latest, c.latest_valid);
            c.latest = result;
            c.latest_errors = (d.input.errors & ~c.latest_valid) | c.latest_errors;
            c.latest_valid = kAllBytes;
            c.read_done = true;
        } else {
            c.flush_done = true;
        }
        c.downstream.reset();
        free_down.push_back(id);
        downstream.erase(id);
        Ready(c.id);
    }
    void Complete(Context& c) {
        const auto& q = c.req;
        const auto& t = Traits(q.flit_opcode);
        if (t.family == Family::kRead) {
            if (q.flit_order)
                Emit(c.id, Response(q, CHI_RSP_Opcode_ReadReceipt), q.flit_src);
            unsigned resp = 0;
            if (t.allocating && !c.non_data_error) {
                auto holders = Holders(c.line);
                holders.erase(q.flit_src);
                resp = q.flit_opcode == CHI_REQ_Opcode_ReadUnique || holders.empty() ? 2 : 1;
                SetPermission(c.line, q.flit_src, resp);
            }
            c.result_resp = resp;
            for (unsigned beat = 0; beat < 2; ++beat) {
                if (!(BeatMask(q.flit_address, 1u << q.flit_size) & (1u << beat)))
                    continue;
                auto f = Pack(c.latest, kAllBytes, beat * 2);
                f.flit_opcode = CHI_DAT_Opcode_CompData;
                f.flit_src = config.node_id;
                f.flit_tgt = q.flit_src;
                f.flit_txn = q.flit_txn;
                f.flit_home_nid = config.node_id;
                f.flit_dbid = c.dbid;
                f.flit_ccid = (q.flit_address >> 4) & 3;
                f.flit_resp = resp;
                f.flit_resp_err = c.non_data_error                                          ? 3
                                  : (c.latest_errors & ByteMask(beat * 32, 32) & Window(q)) ? 2
                                                                                            : 0;
                Emit(c.id, f, q.flit_src, OutputKind::kCompletion);
            }
        } else {
            const bool unique = q.flit_opcode == CHI_REQ_Opcode_CleanUnique ||
                                q.flit_opcode == CHI_REQ_Opcode_MakeUnique;
            if (unique && !c.non_data_error)
                SetPermission(c.line, q.flit_src, 2);
            auto f = Response(q, CHI_RSP_Opcode_Comp, unique ? 2 : 0, c.dbid);
            f.flit_resp_err = c.non_data_error ? 3 : c.latest_errors ? 2 : 0;
            Emit(c.id, f, q.flit_src, OutputKind::kCompletion);
        }
        c.response = true;
    }
    void Step(Handle id) {
        auto& c = contexts.at(id);
        if (line_queue.at(c.line).front() != id ||
            (c.req.flit_order && order_queue.at(c.req.flit_src).front() != id))
            return;
        if (!c.started)
            Start(c);
        if (c.pending_snoops || c.downstream)
            return;
        const auto family = Traits(c.req.flit_opcode).family;
        if (family == Family::kCopyBack || family == Family::kImmediateWrite) {
            if (!c.write_buffer) {
                const auto op = family == Family::kCopyBack ? CHI_RSP_Opcode_CompDBIDResp
                                                            : CHI_RSP_Opcode_DBIDResp;
                Emit(c.id, Response(c.req, op, 0, c.dbid), c.req.flit_src, OutputKind::kBuffer);
                c.write_buffer = true;
                if (family == Family::kCopyBack)
                    c.response = true;
            }
            if (!c.write_done)
                return;
        }
        if (!c.flush_done && c.flush_mask) {
            StartDownstream(c, family == Family::kRead || family == Family::kDataless
                                   ? Purpose::kFlush
                                   : Purpose::kWrite);
            return;
        }
        if (family == Family::kRead && !c.read_done) {
            StartDownstream(c, Purpose::kRead);
            return;
        }
        if (!c.response)
            Complete(c);
        if (c.req.flit_exp_comp_ack && !c.ack)
            return;
        if (!c.in_flight)
            Finish(id);
    }
    void Finish(Handle id) {
        const auto c = contexts.at(id);
        Observe("complete", id, "HN.COMPLETE.B2.4.B4.11");
        auto& queue = line_queue.at(c.line);
        Check(queue.front() == id, P, "HN.LINE: completion out of acceptance order");
        queue.pop_front();
        if (queue.empty())
            line_queue.erase(c.line);
        else
            Ready(queue.front());
        if (c.req.flit_order) {
            auto& order = order_queue.at(c.req.flit_src);
            order.pop_front();
            if (order.empty())
                order_queue.erase(c.req.flit_src);
            else
                Ready(order.front());
        }
        requests.erase({c.req.flit_src, c.req.flit_txn});
        buffers.erase({c.req.flit_src, c.dbid});
        free_dbids.push_back(c.dbid);
        timers.erase({c.deadline, id});
        contexts.erase(id);
        Grant();
    }
    void Sent(const Event& e) {
        auto it = outputs.find(e.token);
        Check(it != outputs.end(), P, "HN.TOKEN: unknown/duplicate token");
        auto& o = it->second;
        if (e.kind == EventKind::kTxAccepted) {
            Check(!o.accepted, P, "HN.TOKEN: duplicate TX acceptance");
            o.accepted = true;
            Observe("tx_accepted", o.owner, "HN.TX.B2.4", o.action.flit);
            return;
        }
        Check(o.accepted, P, "HN.TOKEN: actual transmission before TX acceptance");
        Observe("tx_sent", o.owner, "HN.SEND.B2.4", o.action.flit, e.observed_cycle);
        if (o.kind == OutputKind::kGrant)
            ++available_credits[o.action.destination];
        if (o.owner) {
            auto& c = contexts.at(o.owner);
            --c.in_flight;
            if (o.kind == OutputKind::kCompletion)
                --c.completion_pending;
            if (o.kind == OutputKind::kBuffer)
                c.buffer_sent = true;
            Ready(c.id);
        }
        if (o.kind == OutputKind::kSnoop)
            snoops.at(o.child).sent = true;
        if (o.kind == OutputKind::kDownReq || o.kind == OutputKind::kDownData) {
            auto& d = Down(o.child);
            --d.in_flight;
            if (o.kind == OutputKind::kDownReq)
                d.sent = true;
            ProgressDownstream(o.child);
        }
        outputs.erase(it);
    }
    void Begin(std::uint64_t now) {
        Check(!working && (!cycled || now > cycle), ErrorKind::kLifecycle,
              "HN.LIFECYCLE: non-monotonic/double BeginCycle");
        working = true;
        cycled = true;
        cycle = now;
        auto rank = [](const Event& e) {
            if (e.kind == EventKind::kTxAccepted)
                return 0;
            if (e.kind == EventKind::kTxSent)
                return 1;
            return e.flit.index() == 0 ? 4 : e.flit.index() == 1 ? 2 : 3;
        };
        std::stable_sort(events.begin(), events.end(),
                         [&](const Event& a, const Event& b) { return rank(a) < rank(b); });
        for (const auto& event : events) {
            if (event.kind != EventKind::kReceive) {
                Sent(event);
                continue;
            }
            std::visit(
                [&](const auto& f) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(f)>, CHISnpFlit>)
                        throw VIPError(P, "HN.SNP: snoop delivered to Home");
                    else
                        Receive(f);
                },
                event.flit);
        }
        events.clear();
        while (!ready.empty()) {
            const auto id = ready.front();
            ready.pop_front();
            ready_set.erase(id);
            if (contexts.count(id))
                Step(id);
        }
        if (contexts.empty() && downstream.empty() && retry_down.empty()) {
            while (slave_credits) {
                CHIReqFlit f;
                f.flit_src = config.node_id;
                f.flit_tgt = config.slave_id;
                f.flit_opcode = CHI_REQ_Opcode_PCrdReturn;
                f.flit_pcrd_type = 1;
                Emit(0, f, config.slave_id);
                --slave_credits;
            }
        }
        Check(timers.empty() || timers.begin()->first.first > cycle, ErrorKind::kTimeout,
              "HN.TIMEOUT: diagnostic deadline exceeded; " + Describe());
        Check((rejected.empty() || rejected.front().deadline > cycle) &&
                  (credit_timers.empty() || credit_timers.begin()->first.first > cycle),
              ErrorKind::kTimeout,
              "HN.TIMEOUT: protocol credit/retry did not progress; " + Describe());
    }
    std::string Describe() const {
        std::ostringstream out;
        out << "HN(" << config.node_id << ") requests=" << contexts.size()
            << " snoops=" << snoops.size() << " downstream=" << downstream.size()
            << " rejected=" << rejected.size() << " reserved=" << reserved
            << " outputs=" << outputs.size();
        for (const auto& [id, c] : contexts)
            out << " [" << id << " line=" << c.line << " op=" << c.req.flit_opcode
                << " snoops=" << c.pending_snoops << " write=" << c.write_done << " ack=" << c.ack
                << " response=" << c.response << ']';
        return out.str();
    }
};

HN::HN(HNConfig config, Observer observer)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(observer))) {}
HN::~HN() = default;
const HNConfig& HN::Config() const {
    return impl_->config;
}
void HN::OnEvent(Event event) {
    Check(!impl_->working, ErrorKind::kLifecycle, "HN.OnEvent during Work/Commit");
    impl_->events.push_back(std::move(event));
}
void HN::BeginCycle(std::uint64_t cycle) {
    impl_->Begin(cycle);
}
std::vector<SendAction> HN::TakeActions() {
    Check(impl_->working, ErrorKind::kLifecycle, "HN.TakeActions outside Work");
    std::vector<SendAction> result;
    result.swap(impl_->actions);
    return result;
}
void HN::Commit() {
    auto& r = *impl_;
    Check(r.working, ErrorKind::kLifecycle, "HN.Commit without Work");
    for (const auto line : r.changed) {
        const auto& d = r.directory.at(line);
        if (d.sharers.empty() && !d.owner) {
            r.committed_directory.erase(line);
            r.directory.erase(line);
        } else
            r.committed_directory[line] = d;
    }
    r.changed.clear();
    r.working = false;
    if (r.observer)
        for (const auto& o : r.observations)
            r.observer(o);
    r.observations.clear();
}
std::optional<DirectoryEntry> HN::InspectDirectory(std::uint64_t address) const {
    auto it = impl_->committed_directory.find(LineAddress(address));
    if (it == impl_->committed_directory.end())
        return {};
    return it->second;
}
bool HN::IsIdle() const {
    const auto& r = *impl_;
    return !r.working && r.contexts.empty() && r.downstream.empty() && r.snoops.empty() &&
           r.events.empty() && r.outputs.empty() && r.actions.empty() && r.rejected.empty() &&
           !r.reserved && !r.slave_credits && r.retry_down.empty() && r.timers.empty() &&
           r.credit_timers.empty();
}
std::string HN::DescribeState() const {
    return impl_->Describe();
}
}  // namespace esl::chi_vip
