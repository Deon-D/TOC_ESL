#include <memory>
#include <sstream>
#include <tuple>

#include "chi_vip_test_support.hpp"
#include "hn.hpp"
#include "node_transport_adapter.hpp"
#include "sn.hpp"

namespace vip_system_test {
using namespace vip_test;

struct SystemConfig {
    unsigned cache_lines = 8;
    unsigned rn_slots = 4;
    unsigned hn_slots = 8;
    unsigned sn_slots = 8;
    unsigned latency = 5;
    bool grant_first = false;
    bool reverse = false;
    bool narrow_links = true;
};

// Passive association checker built from actual transmission records. Its
// transaction state is independent of all three active implementations.
class WireAudit {
public:
    void Receive(const CHIReqFlit& flit) {
        const Credit credit{flit.flit_tgt, flit.flit_src, flit.flit_pcrd_type};
        if (flit.flit_opcode == CHI_REQ_Opcode_PCrdReturn) {
            Expect(credits_[credit] != 0, "wire audit: returned an ungranted protocol credit");
            --credits_[credit];
            return;
        }
        const ID id{flit.flit_tgt, flit.flit_src, flit.flit_txn};
        const auto found = requests_.find(id);
        if (found != requests_.end()) {
            Expect(found->second.retried && !flit.flit_allow_retry,
                   "wire audit: live request TxnID reused");
            Expect(found->second.request.flit_address == flit.flit_address &&
                       found->second.request.flit_opcode == flit.flit_opcode,
                   "wire audit: retry changed request identity");
            requests_.erase(found);
        }
        if (!flit.flit_allow_retry) {
            Expect(credits_[credit] != 0, "wire audit: retry without protocol credit");
            --credits_[credit];
        }
        requests_.emplace(id, Request{flit});
    }
    void Receive(const CHIRspFlit& flit) {
        if (flit.flit_opcode == CHI_RSP_Opcode_PCrdGrant) {
            ++credits_[{flit.flit_src, flit.flit_tgt, flit.flit_pcrd_type}];
            return;
        }
        if (flit.flit_opcode == CHI_RSP_Opcode_SnpResp) {
            Expect(snoops_.erase({flit.flit_tgt, flit.flit_src, flit.flit_txn}) == 1,
                   "wire audit: unknown/duplicate snoop response");
            return;
        }
        if (flit.flit_opcode == CHI_RSP_Opcode_CompAck) {
            const ID buffer{flit.flit_tgt, flit.flit_src, flit.flit_txn};
            const auto found = buffers_.find(buffer);
            Expect(found != buffers_.end(), "wire audit: CompAck has no live DBID");
            const auto id = found->second;
            auto& request = requests_.at(id);
            Expect(!request.ack, "wire audit: duplicate CompAck");
            request.ack = true;
            buffers_.erase(found);
            Retire(id);
            return;
        }
        const ID id{flit.flit_src, flit.flit_tgt, flit.flit_txn};
        const auto found = requests_.find(id);
        Expect(found != requests_.end(), "wire audit: response has no live request");
        auto& request = found->second;
        if (flit.flit_opcode == CHI_RSP_Opcode_RetryAck) {
            Expect(request.request.flit_allow_retry && !request.retried && !request.comp &&
                       !request.dbid && !request.beats,
                   "wire audit: illegal RetryAck after acceptance");
            request.retried = true;
            return;
        }
        if (flit.flit_opcode == CHI_RSP_Opcode_ReadReceipt) {
            Expect(!request.receipt, "wire audit: duplicate ReadReceipt");
            request.receipt = true;
            Retire(id);
            return;
        }
        const bool dbid = flit.flit_opcode == CHI_RSP_Opcode_DBIDResp ||
                          flit.flit_opcode == CHI_RSP_Opcode_DBIDRespOrd ||
                          flit.flit_opcode == CHI_RSP_Opcode_CompDBIDResp;
        const bool comp = flit.flit_opcode == CHI_RSP_Opcode_Comp ||
                          flit.flit_opcode == CHI_RSP_Opcode_CompDBIDResp;
        Expect(dbid || comp, "wire audit: unexpected response opcode");
        if (dbid) {
            Expect(!request.dbid, "wire audit: duplicate write DBID response");
            request.dbid = true;
            Claim({flit.flit_src, flit.flit_tgt, flit.flit_dbid}, id);
        }
        if (comp) {
            Expect(!request.comp, "wire audit: duplicate completion response");
            request.comp = true;
            if (request.request.flit_exp_comp_ack && !dbid)
                Claim({flit.flit_src, flit.flit_tgt, flit.flit_dbid}, id);
        }
        Retire(id);
    }
    void Receive(const CHISnpFlit& flit, NodeID destination) {
        Expect(snoops_.emplace(ID{flit.flit_src, destination, flit.flit_txn}, 0).second,
               "wire audit: live snoop TxnID reused");
    }
    void Receive(const CHIDatFlit& flit) {
        Expect(flit.flit_data_id == 0 || flit.flit_data_id == 2,
               "wire audit: invalid 256-bit DataID");
        const unsigned bit = 1u << (flit.flit_data_id / 2);
        if (flit.flit_opcode == CHI_DAT_Opcode_SnpRespData ||
            flit.flit_opcode == CHI_DAT_Opcode_SnpRespDataPtl) {
            const auto found = snoops_.find({flit.flit_tgt, flit.flit_src, flit.flit_txn});
            Expect(found != snoops_.end(), "wire audit: snoop DAT has no live request");
            Expect(!(found->second & bit), "wire audit: duplicate snoop data beat");
            found->second |= bit;
            if (found->second == 3)
                snoops_.erase(found);
            return;
        }
        ID id;
        if (flit.flit_opcode == CHI_DAT_Opcode_CompData) {
            id = {flit.flit_src, flit.flit_tgt, flit.flit_txn};
            const auto found = requests_.find(id);
            Expect(found != requests_.end(), "wire audit: read DAT has no live request");
            if (found->second.request.flit_exp_comp_ack && !found->second.beats)
                Claim({flit.flit_src, flit.flit_tgt, flit.flit_dbid}, id);
        } else {
            Expect(flit.flit_opcode == CHI_DAT_Opcode_CopyBackWriteData ||
                       flit.flit_opcode == CHI_DAT_Opcode_NonCopyBackWriteData,
                   "wire audit: unsupported DAT opcode");
            const auto found = buffers_.find({flit.flit_tgt, flit.flit_src, flit.flit_txn});
            Expect(found != buffers_.end(), "wire audit: write DAT has no live DBID");
            id = found->second;
        }
        auto& request = requests_.at(id);
        Expect(!(request.beats & bit), "wire audit: duplicate data beat");
        request.beats |= bit;
        const auto required = RequiredBeats(request.request);
        Expect((request.beats & ~required) == 0, "wire audit: data beat outside request window");
        if (flit.flit_opcode == CHI_DAT_Opcode_CompData)
            request.comp = request.beats == required;
        else if (request.beats == required)
            Expect(buffers_.erase({flit.flit_tgt, flit.flit_src, flit.flit_txn}) == 1,
                   "wire audit: write DBID missing on last packet");
        Retire(id);
    }
    void CheckDrained() const {
        Expect(requests_.empty() && buffers_.empty() && snoops_.empty(),
               "wire audit: protocol transactions did not drain");
        for (const auto& credit : credits_)
            Expect(credit.second == 0,
                   "wire audit: protocol credit was neither consumed nor returned");
    }

private:
    using ID = std::tuple<NodeID, NodeID, TxnID>;
    using Credit = std::tuple<NodeID, NodeID, unsigned>;
    struct Request {
        CHIReqFlit request;
        bool retried = false;
        bool receipt = false;
        bool comp = false;
        bool dbid = false;
        bool ack = false;
        unsigned beats = 0;
    };
    static unsigned RequiredBeats(const CHIReqFlit& request) {
        if (request.flit_size == 6)
            return 3;
        return request.flit_address & 32 ? 2 : 1;
    }
    void Claim(const ID& buffer, const ID& request) {
        Expect(buffers_.emplace(buffer, request).second, "wire audit: live DBID reused");
    }
    void Retire(const ID& id) {
        auto& request = requests_.at(id);
        const auto opcode = request.request.flit_opcode;
        const bool write = opcode == 0x15 || opcode == 0x17 || opcode == 0x18 || opcode == 0x19 ||
                           opcode == 0x1a || opcode == 0x1b || opcode == 0x1c || opcode == 0x1d;
        const bool read = opcode == 1 || opcode == 2 || opcode == 3 || opcode == 4 || opcode == 7 ||
                          opcode == 0x26;
        if (request.comp && (!write || request.beats == RequiredBeats(request.request)) &&
            (!read || !request.request.flit_order || request.receipt) &&
            (!request.request.flit_exp_comp_ack || request.ack))
            requests_.erase(id);
    }
    std::map<ID, Request> requests_;
    std::map<ID, ID> buffers_;
    std::map<ID, unsigned> snoops_;
    std::map<Credit, unsigned> credits_;
};

// All active agents communicate through six real point-to-point CHIPorts.
// The test driver sees only RN submissions/completions and actual wire flits.
class System {
public:
    explicit System(SystemConfig config = {})
        : config_(config),
          rn0_(RNConfiguration(0, config)),
          rn1_(RNConfiguration(2, config)),
          hn_(HNConfiguration(config)),
          sn_(SNConfiguration(config)),
          rn0_port_("rn0.hn"),
          rn1_port_("rn1.hn"),
          hn0_port_("hn.rn0"),
          hn1_port_("hn.rn1"),
          hn_sn_port_("hn.sn"),
          sn_port_("sn.hn"),
          rn0_adapter_(rn0_, rn0_port_),
          rn1_adapter_(rn1_, rn1_port_),
          hn_adapter_(hn_, {{0, &hn0_port_}, {2, &hn1_port_}, {3, &hn_sn_port_}}),
          sn_adapter_(sn_, sn_port_) {
        rn0_port_.Connect(hn0_port_);
        rn1_port_.Connect(hn1_port_);
        hn_sn_port_.Connect(sn_port_);
        if (config.narrow_links)
            for (auto* port : Ports()) {
                port->SetParams<CHIReqFlit>({1, 1, 1, 2});
                port->SetParams<CHIRspFlit>({1, 1, 2, 2});
                port->SetParams<CHISnpFlit>({1, 1, 3, 2});
                port->SetParams<CHIDatFlit>({1, 1, 1, 2});
            }
        for (std::uint64_t address = 0; address < 8192; address += 64)
            sn_.Memory().Initialize(address, Pattern(address));
    }

    RN& Requester(unsigned index) {
        Expect(index < 2, "invalid test requester index");
        return index ? rn1_ : rn0_;
    }
    void Tick() {
        if (config_.reverse) {
            sn_adapter_.Work(cycle_);
            hn_adapter_.Work(cycle_);
            rn1_adapter_.Work(cycle_);
            rn0_adapter_.Work(cycle_);
            sn_adapter_.Xfer();
            hn_adapter_.Xfer();
            rn1_adapter_.Xfer();
            rn0_adapter_.Xfer();
        } else {
            rn0_adapter_.Work(cycle_);
            rn1_adapter_.Work(cycle_);
            hn_adapter_.Work(cycle_);
            sn_adapter_.Work(cycle_);
            rn0_adapter_.Xfer();
            rn1_adapter_.Xfer();
            hn_adapter_.Xfer();
            sn_adapter_.Xfer();
        }
        unsigned index = 0;
        for (auto* port : Ports()) {
            Record<CHIReqFlit>(*port, index);
            Record<CHIRspFlit>(*port, index);
            Record<CHISnpFlit>(*port, index);
            Record<CHIDatFlit>(*port, index);
            ++index;
        }
        for (unsigned rn = 0; rn < 2; ++rn) {
            Completion completion;
            while (Requester(rn).PollCompletion(completion)) {
                Expect(completed_[rn].emplace(completion.handle, completion).second,
                       "duplicate application completion");
            }
        }
        ++cycle_;
    }
    template <class Predicate>
    void Until(Predicate predicate, unsigned limit = 50000) {
        for (unsigned i = 0; i < limit && !predicate(); ++i)
            Tick();
        Expect(predicate(), "system made no progress: " + DescribeState());
    }
    Completion Result(unsigned rn, SubmitResult result) {
        Expect(result.accepted, "application submission was backpressured");
        Until([&] { return completed_[rn].count(result.handle) != 0; });
        return completed_[rn].at(result.handle);
    }
    Completion Done(unsigned rn, SubmitResult result) {
        const auto completion = Result(rn, result);
        Expect(completion.error == AccessError::kOK, "unexpected access error");
        return completion;
    }
    Completion Execute(unsigned rn, unsigned opcode, std::uint64_t address = 0) {
        TxnRequest request;
        request.opcode = opcode;
        request.address = address;
        return Execute(rn, request);
    }
    Completion Execute(unsigned rn, const TxnRequest& request) {
        return Done(rn, Requester(rn).Submit(request));
    }
    Completion Load(unsigned rn, std::uint64_t address = 0) {
        return Done(rn, Requester(rn).Load(address, 64));
    }
    void Store(unsigned rn, std::uint64_t address, const Line& value, std::uint64_t mask) {
        Done(rn, Requester(rn).Store(address, value, mask));
    }
    bool Idle() const {
        return rn0_adapter_.IsIdle() && rn1_adapter_.IsIdle() && hn_adapter_.IsIdle() &&
               sn_adapter_.IsIdle() && rn0_.IsIdle() && rn1_.IsIdle() && hn_.IsIdle() &&
               sn_.IsIdle();
    }
    void Drain() {
        Until([&] { return Idle(); });
        for (auto* port : Ports())
            Expect(port->IsIdle(), "link credit/data remained after system drain");
        audit_.CheckDrained();
    }
    Line Memory(std::uint64_t address) {
        Expect(Idle(), "final memory inspection requires a drained system");
        return sn_.Memory().Read(address, 64);
    }
    std::uint64_t MemoryErrors(std::uint64_t address) {
        Expect(Idle(), "final error inspection requires a drained system");
        return sn_.Memory().Errors(address);
    }
    void InitializeErrors(std::uint64_t address, std::uint64_t errors) {
        Expect(cycle_ == 0, "error initialization is startup-only");
        auto& memory = sn_.Memory();
        memory.SubmitWrite(1, address, Pattern(address), kAllBytes, errors, 0);
        const auto completed = memory.Advance(config_.latency);
        Expect(completed.size() == 1, "management error initialization did not finish");
        memory.Commit();
        // Initialization consumed backend time. Every core starts together at
        // the next cycle so subsequent service never moves backward.
        cycle_ = std::uint64_t{config_.latency} + 1;
    }
    std::string DescribeState() const {
        return rn0_.DescribeState() + " | " + rn1_.DescribeState() + " | " + hn_.DescribeState() +
               " | " + sn_.DescribeState();
    }
    const std::vector<std::string>& Trace() const {
        return trace_;
    }
    unsigned CountRequest(unsigned opcode) const {
        const auto i = requests_.find(opcode);
        return i == requests_.end() ? 0 : i->second;
    }
    unsigned CountResponse(NodeID sender, unsigned opcode) const {
        const auto i = responses_.find({sender, opcode});
        return i == responses_.end() ? 0 : i->second;
    }
    unsigned AcceptedOrders() const {
        return accepted_orders_;
    }
    unsigned SnoopCount() const {
        return snoops_;
    }

private:
    static RNConfig RNConfiguration(NodeID id, const SystemConfig& config) {
        RNConfig result;
        result.node_id = id;
        result.cache_lines = config.cache_lines;
        result.request_slots = config.rn_slots;
        result.timeout_cycles = 40000;
        return result;
    }
    static HNConfig HNConfiguration(const SystemConfig& config) {
        HNConfig result;
        result.request_slots = config.hn_slots;
        result.grant_before_retry = config.grant_first;
        result.timeout_cycles = 40000;
        return result;
    }
    static SNConfig SNConfiguration(const SystemConfig& config) {
        SNConfig result;
        result.request_slots = config.sn_slots;
        result.service_latency = config.latency;
        result.grant_before_retry = config.grant_first;
        result.timeout_cycles = 40000;
        return result;
    }
    std::array<CHIPort*, 6> Ports() {
        return {&rn0_port_, &rn1_port_, &hn0_port_, &hn1_port_, &hn_sn_port_, &sn_port_};
    }
    template <class F>
    void Record(const CHIPort& port, unsigned index) {
        const auto* flit = port.Transmitted<F>();
        if (!flit)
            return;
        // The link endpoints determine routing, including SNP's absent TgtID.
        constexpr NodeID source[] = {0, 2, 1, 1, 1, 3};
        constexpr NodeID target[] = {1, 1, 0, 2, 3, 1};
        Expect(flit->flit_src == source[index], "wire source does not match link endpoint");
        if constexpr (!std::is_same<F, CHISnpFlit>::value)
            Expect(flit->flit_tgt == target[index], "wire target does not match link endpoint");
        if constexpr (std::is_same<F, CHISnpFlit>::value)
            audit_.Receive(*flit, target[index]);
        else
            audit_.Receive(*flit);
        if constexpr (std::is_same<F, CHIReqFlit>::value) {
            if (index < 2)
                ++requests_[flit->flit_opcode];
            if (index == 4 && flit->flit_order == 1)
                ++accepted_orders_;
        } else if constexpr (std::is_same<F, CHIRspFlit>::value) {
            ++responses_[{flit->flit_src, flit->flit_opcode}];
        } else if constexpr (std::is_same<F, CHISnpFlit>::value) {
            ++snoops_;
        }
        trace_.push_back(std::to_string(cycle_) + ":" + std::to_string(index) + ":" +
                         flit->to_string());
    }
    SystemConfig config_;
    RN rn0_, rn1_;
    HN hn_;
    SN sn_;
    CHIPort rn0_port_, rn1_port_, hn0_port_, hn1_port_, hn_sn_port_, sn_port_;
    TransportAdapter rn0_adapter_, rn1_adapter_;
    NodeTransportAdapter hn_adapter_, sn_adapter_;
    std::uint64_t cycle_ = 0;
    std::array<std::map<Handle, Completion>, 2> completed_;
    std::vector<std::string> trace_;
    std::map<unsigned, unsigned> requests_;
    std::map<std::pair<NodeID, unsigned>, unsigned> responses_;
    unsigned accepted_orders_ = 0;
    unsigned snoops_ = 0;
    WireAudit audit_;
};

Line Changed(Line initial, const Line& replacement, std::uint64_t mask) {
    for (unsigned i = 0; i < 64; ++i)
        if ((mask >> i) & 1)
            initial[i] = replacement[i];
    return initial;
}

void Basic() {
    System system;
    Expect(system.Execute(0, CHI_REQ_Opcode_ReadNoSnp).data == Pattern(), "initial SN read");
    TxnRequest request;
    request.opcode = CHI_REQ_Opcode_WriteNoSnpFull;
    request.data = Pattern(0x3000);
    auto expected = request.data;
    system.Execute(0, request);
    Expect(system.Execute(1, CHI_REQ_Opcode_ReadNoSnp).data == expected, "full write/read");
    request.opcode = CHI_REQ_Opcode_WriteNoSnpPtl;
    request.data = Pattern(0x3400);
    request.byte_enable = 0x8000000180000081ull;
    expected = Changed(expected, request.data, request.byte_enable);
    system.Execute(1, request);
    request = {};
    request.opcode = CHI_REQ_Opcode_ReadNoSnp;
    request.exp_comp_ack = true;
    Expect(system.Execute(0, request).data == expected, "partial write/read with CompAck");
    request.address = 12;
    request.size = 4;
    const auto narrow = system.Execute(1, request);
    Expect(narrow.valid == (std::uint64_t{15} << 12), "narrow read valid-byte mask");
    for (unsigned i = 12; i < 16; ++i)
        Expect(narrow.data[i] == expected[i], "narrow read data");
    system.Drain();
    Expect(system.Memory(0) == expected, "SN byte storage final value");
}

void Coherence() {
    System system;
    auto expected = Pattern();
    Expect(system.Load(0).data == expected, "first shared load");
    Expect(system.Load(1).data == expected, "second shared load");
    const auto first = Pattern(0x4000);
    system.Store(0, 0, first, 0x000000000000ff00ull);
    expected = Changed(expected, first, 0x000000000000ff00ull);
    Expect(system.Load(1).data == expected, "snoop missed silent UC to UD store");
    const auto second = Pattern(0x4400);
    system.Store(1, 0, second, 0x8000000100000001ull);
    expected = Changed(expected, second, 0x8000000100000001ull);
    Expect(system.Execute(0, CHI_REQ_Opcode_ReadUnique).data == expected,
           "unique permission handoff lost dirty data");
    const auto partial = Pattern(0x4800);
    system.Execute(0, CHI_REQ_Opcode_CleanUnique, 64);
    system.Store(0, 64, partial, 0x0000ffff00000000ull);
    const auto merged = Changed(Pattern(64), partial, 0x0000ffff00000000ull);
    Expect(system.Load(1, 64).data == merged, "partial dirty owner merge");
    system.Drain();
    Expect(system.SnoopCount() >= 4, "coherence scenario did not exercise snoop channel");
    Expect(system.Memory(0) == expected, "dirty responsibility lost during unique transfer");
    Expect(system.Memory(64) == merged, "partial dirty responsibility lost");
}

void Opcodes() {
    // Literal expected set: independent of the production capability/rule tables.
    const unsigned reads[] = {0x04, 0x03, 0x01, 0x02, 0x07, 0x26};
    for (const auto opcode : reads) {
        System system;
        auto expected = Pattern();
        if (opcode != CHI_REQ_Opcode_ReadNoSnp) {
            expected = Pattern(0x5000);
            system.Store(1, 0, expected, kAllBytes);
        }
        Expect(system.Execute(0, opcode).data == expected,
               "read opcode " + std::to_string(opcode) + " returned stale data");
        system.Drain();
        Expect(system.CountRequest(opcode) != 0, "read opcode not transmitted");
    }
    const unsigned writes[] = {0x1d, 0x1c, 0x19, 0x18};
    for (const auto opcode : writes) {
        System system;
        auto expected = Pattern();
        if (opcode == CHI_REQ_Opcode_WriteUniqueFull || opcode == CHI_REQ_Opcode_WriteUniquePtl) {
            expected = Pattern(0x5400);
            system.Store(1, 0, expected, kAllBytes);
        }
        TxnRequest request;
        request.opcode = opcode;
        request.data = Pattern(0x5800);
        if (opcode == CHI_REQ_Opcode_WriteNoSnpPtl || opcode == CHI_REQ_Opcode_WriteUniquePtl)
            request.byte_enable = 0x8000000000000001ull;
        expected = Changed(expected, request.data, request.byte_enable);
        system.Execute(0, request);
        Expect(system.Execute(0, CHI_REQ_Opcode_ReadOnce).data == expected,
               "immediate write merge for opcode " + std::to_string(opcode));
        system.Drain();
        Expect(system.Memory(0) == expected, "immediate write did not reach SN");
    }
    const unsigned copybacks[] = {0x1b, 0x1a, 0x17, 0x15};
    for (const auto opcode : copybacks) {
        System system;
        auto expected = Pattern();
        if (opcode == CHI_REQ_Opcode_WriteEvictFull) {
            system.Execute(0, CHI_REQ_Opcode_ReadUnique);
        } else {
            const auto changed = Pattern(0x5c00);
            const auto mask =
                opcode == CHI_REQ_Opcode_WriteBackPtl ? 0x00000000ffffffffull : kAllBytes;
            if (opcode == CHI_REQ_Opcode_WriteBackPtl)
                system.Execute(0, CHI_REQ_Opcode_CleanUnique);
            system.Store(0, 0, changed, mask);
            expected = Changed(expected, changed, mask);
        }
        system.Execute(0, opcode);
        system.Drain();
        Expect(system.Memory(0) == expected,
               "CopyBack failed to preserve bytes for opcode " + std::to_string(opcode));
        Expect(system.CountRequest(opcode) != 0, "CopyBack opcode not transmitted");
    }
    const unsigned dataless[] = {0x0b, 0x0c, 0x0d, 0x08, 0x09, 0x0a};
    for (const auto opcode : dataless) {
        System system;
        auto expected = Pattern();
        if (opcode == CHI_REQ_Opcode_Evict)
            system.Execute(0, CHI_REQ_Opcode_ReadUnique);
        else {
            const auto dirty = Pattern(0x6000);
            system.Store(1, 0, dirty, kAllBytes);
            if (opcode != CHI_REQ_Opcode_MakeInvalid && opcode != CHI_REQ_Opcode_MakeUnique)
                expected = dirty;
        }
        TxnRequest request;
        request.opcode = opcode;
        request.data = Pattern(0x6400);
        system.Execute(0, request);
        if (opcode == CHI_REQ_Opcode_MakeUnique) {
            expected = request.data;
            Expect(system.Load(1).data == expected, "MakeUnique replacement data not visible");
        }
        system.Drain();
        Expect(system.Memory(0) == expected,
               "maintenance memory semantics for opcode " + std::to_string(opcode));
        Expect(system.CountRequest(opcode) != 0, "dataless opcode not transmitted");
    }
}

std::vector<std::string> FlowRun(bool grant_first, bool reverse, bool pressure_sn) {
    SystemConfig config;
    config.grant_first = grant_first;
    config.reverse = reverse;
    config.hn_slots = pressure_sn ? 4 : 1;
    config.sn_slots = 1;
    config.latency = 11;
    System system(config);
    struct Pending {
        unsigned rn;
        SubmitResult result;
        std::uint64_t address;
    };
    std::vector<Pending> pending;
    for (unsigned i = 0; i < 10; ++i) {
        TxnRequest request;
        request.opcode = CHI_REQ_Opcode_ReadNoSnp;
        request.address = i * 64;
        // Two ordered streams compete for one acceptance resource.
        request.order = pressure_sn ? 0 : 2;
        const unsigned rn = i & 1;
        pending.push_back({rn, system.Requester(rn).Submit(request), request.address});
    }
    for (const auto& operation : pending)
        Expect(system.Done(operation.rn, operation.result).data == Pattern(operation.address),
               "retry/ordered flow returned wrong line");
    system.Drain();
    const NodeID issuer = pressure_sn ? 3 : 1;
    Expect(system.CountResponse(issuer, CHI_RSP_Opcode_RetryAck) != 0,
           "capacity pressure did not exercise RetryAck");
    Expect(system.CountResponse(issuer, CHI_RSP_Opcode_PCrdGrant) != 0,
           "capacity pressure did not exercise PCrdGrant");
    if (!pressure_sn) {
        Expect(system.CountResponse(1, CHI_RSP_Opcode_ReadReceipt) >= 10,
               "ordered reads lacked acceptance responses");
        Expect(system.AcceptedOrders() != 0, "HN to SN Request Accepted order not exercised");
    }
    return system.Trace();
}

void Flow() {
    for (const bool grant_first : {false, true})
        for (const bool pressure_sn : {false, true})
            Expect(
                FlowRun(grant_first, false, pressure_sn) == FlowRun(grant_first, true, pressure_sn),
                "Work/Xfer traversal changed retry/ordering trace");
}

std::vector<std::string> StressRun(unsigned seed, bool reverse) {
    SystemConfig config;
    config.cache_lines = 3;
    config.rn_slots = 2;
    config.hn_slots = 2;
    config.sn_slots = 1;
    config.reverse = reverse;
    System system(config);
    std::mt19937 random(seed);
    std::array<Line, 12> latest{};
    for (unsigned line = 0; line < latest.size(); ++line)
        latest[line] = Pattern(line * 64);
    struct Pending {
        unsigned rn;
        SubmitResult result;
        std::optional<Line> expected;
    };
    for (unsigned batch = 0; batch < 80; ++batch) {
        std::vector<Pending> pending;
        std::set<unsigned> used_lines;
        // Independent lines overlap in time. Same-line causal order is explicit
        // between batches, so the oracle never imposes an arbitrary race winner.
        while (pending.size() < 4) {
            const unsigned line = random() % latest.size();
            if (!used_lines.insert(line).second)
                continue;
            const unsigned rn = random() & 1;
            if ((random() & 3) == 0) {
                pending.push_back({rn, system.Requester(rn).Load(line * 64, 64), latest[line]});
            } else {
                auto replacement = latest[line];
                const unsigned first = random() % 64;
                const unsigned second = random() % 64;
                const auto mask = (std::uint64_t{1} << first) | (std::uint64_t{1} << second);
                replacement[first] = static_cast<std::uint8_t>(random());
                replacement[second] = static_cast<std::uint8_t>(random());
                pending.push_back(
                    {rn, system.Requester(rn).Store(line * 64, replacement, mask), {}});
                latest[line] = replacement;
            }
            Expect(pending.back().result.accepted, "stress pending operation capacity");
        }
        for (const auto& operation : pending) {
            const auto completion = system.Done(operation.rn, operation.result);
            if (operation.expected)
                Expect(completion.data == *operation.expected, "random load violated byte oracle");
        }
    }
    for (unsigned line = 0; line < latest.size(); ++line) {
        Expect(system.Load(0, line * 64).data == latest[line], "final RN0 load mismatch");
        Expect(system.Load(1, line * 64).data == latest[line], "final RN1 load mismatch");
        // Both loads leave the current clean policy in a legal CMO start state.
        // This also forces the final dirty responsibility into the SN backend.
        system.Execute(0, CHI_REQ_Opcode_CleanInvalid, line * 64);
    }
    system.Drain();
    for (unsigned line = 0; line < latest.size(); ++line)
        Expect(system.Memory(line * 64) == latest[line], "final SN byte oracle mismatch");
    Expect(system.CountRequest(CHI_REQ_Opcode_WriteBackFull) != 0,
           "small cache stress did not exercise automatic dirty replacement");
    return system.Trace();
}

void Stress() {
    const auto forward = StressRun(0x84c11, false);
    Expect(forward == StressRun(0x84c11, false), "fixed seed trace is not reproducible");
    Expect(forward == StressRun(0x84c11, true), "traversal changed coherent stress trace");
}

void ErrorPropagation() {
    {
        SystemConfig config;
        config.cache_lines = 1;
        System system(config);
        system.InitializeErrors(0, std::uint64_t{1} << 2);
        const auto first = system.Result(0, system.Requester(0).Load(0, 64));
        Expect(first.error == AccessError::kDataError && first.data == Pattern(),
               "SN DERR was not propagated through HN to RN");
        Expect(system.Result(0, system.Requester(0).Load(2, 1)).error == AccessError::kDataError,
               "local load lost cached DERR");
        Expect(system.Result(0, system.Requester(0).Load(40, 1)).error == AccessError::kOK,
               "DERR leaked into the other DAT beat");
        auto expected = Pattern();
        expected[63] ^= 0xa5;
        system.Store(0, 0, expected, std::uint64_t{1} << 63);
        system.Load(0, 64);  // Capacity one forces the faulty dirty line to write back.
        system.Drain();
        Expect(system.Memory(0) == expected, "DERR writeback altered data bytes");
        Expect(system.MemoryErrors(0) == 0xffffffffull,
               "writeback did not preserve packet-granularity data errors");
        const auto retained = system.Result(1, system.Requester(1).Load(0, 64));
        Expect(retained.error == AccessError::kDataError && retained.data == expected,
               "persisted error was lost on a new requester");
        const auto repair = Pattern(0x7000);
        system.Store(1, 0, repair, 0xffffffffull);
        expected = Changed(expected, repair, 0xffffffffull);
        Expect(system.Load(0).data == expected, "overwriting faulty bytes did not repair the line");
        system.Drain();
        Expect(system.Memory(0) == expected && system.MemoryErrors(0) == 0,
               "repaired data/error mask did not reach SN");
    }
    {
        System system;
        constexpr std::uint64_t address = 128;
        system.InitializeErrors(address, std::uint64_t{1} << 40);
        system.Execute(0, CHI_REQ_Opcode_CleanUnique, address);
        const auto changed = Pattern(0x7400);
        constexpr std::uint64_t mask = 0xff00000000000000ull;
        system.Store(0, address, changed, mask);
        auto expected = Changed(Pattern(address), changed, mask);
        const auto merged = system.Result(1, system.Requester(1).Load(address, 64));
        Expect(merged.error == AccessError::kDataError && merged.data == expected,
               "partial dirty merge erased an uncovered memory error");
        system.Drain();
        Expect(system.MemoryErrors(address) == (std::uint64_t{1} << 40),
               "partial write changed errors outside its byte enable mask");
        const auto repair = Pattern(0x7800);
        system.Store(1, address, repair, 0xffffffff00000000ull);
        expected = Changed(expected, repair, 0xffffffff00000000ull);
        Expect(system.Load(0, address).data == expected, "partial merge error repair");
        system.Drain();
        Expect(system.MemoryErrors(address) == 0, "partial merge retained repaired error bytes");
    }
}
}  // namespace vip_system_test

int main(int argc, char** argv) {
    try {
        vip_test::Expect(argc == 2, "expected test case");
        const std::string name = argv[1];
        if (name == "basic")
            vip_system_test::Basic();
        else if (name == "coherence")
            vip_system_test::Coherence();
        else if (name == "opcodes")
            vip_system_test::Opcodes();
        else if (name == "flow")
            vip_system_test::Flow();
        else if (name == "stress")
            vip_system_test::Stress();
        else if (name == "error")
            vip_system_test::ErrorPropagation();
        else
            throw std::runtime_error("unknown system test case");
        std::cout << "system " << name << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
