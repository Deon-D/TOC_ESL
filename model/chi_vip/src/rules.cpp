#include "rules.hpp"

#include <set>

namespace esl::chi_vip {
namespace {
using S = CacheState;
using F = Family;
constexpr unsigned Bit(S s) {
    return 1u << static_cast<unsigned>(s);
}
constexpr unsigned I = Bit(S::kI), SC = Bit(S::kSC), UC = Bit(S::kUC), UD = Bit(S::kUD),
                   SD = Bit(S::kSD), UCE = Bit(S::kUCE), UDP = Bit(S::kUDP);
struct RequestRule {
    OpcodeTraits traits;
    unsigned start;
};
// RN.REQ.<opcode> -- G.b B4.7.1-.3. Start includes the local preparation
// preceding an on-wire invalidating CMO (B4.7.2, Table B4.42).
const RequestRule requests[] = {
    {{CHI_REQ_Opcode_ReadNoSnp, "ReadNoSnp", F::kRead, false, false, false, "B4.7.1"}, I},
    {{CHI_REQ_Opcode_ReadOnce, "ReadOnce", F::kRead, false, false, false, "B4.7.1"}, I},
    {{CHI_REQ_Opcode_ReadShared, "ReadShared", F::kRead, true, true, true, "B4.7.1"}, I | UCE},
    {{CHI_REQ_Opcode_ReadClean, "ReadClean", F::kRead, true, true, true, "B4.7.1"}, I | UCE},
    {{CHI_REQ_Opcode_ReadUnique, "ReadUnique", F::kRead, true, true, true, "B4.7.1"},
     I | SC | UC | UD | SD | UCE | UDP},
    {{CHI_REQ_Opcode_ReadNotSharedDirty, "ReadNotSharedDirty", F::kRead, true, true, true,
      "B4.7.1"},
     I | UCE},
    {{CHI_REQ_Opcode_WriteNoSnpFull, "WriteNoSnpFull", F::kImmediateWrite, false, false, true,
      "B4.7.3"},
     I},
    {{CHI_REQ_Opcode_WriteNoSnpPtl, "WriteNoSnpPtl", F::kImmediateWrite, false, false, false,
      "B4.7.3"},
     I},
    {{CHI_REQ_Opcode_WriteUniqueFull, "WriteUniqueFull", F::kImmediateWrite, false, false, true,
      "B4.7.3"},
     I},
    {{CHI_REQ_Opcode_WriteUniquePtl, "WriteUniquePtl", F::kImmediateWrite, false, false, false,
      "B4.7.3"},
     I},
    {{CHI_REQ_Opcode_WriteBackFull, "WriteBackFull", F::kCopyBack, false, false, true, "B4.7.3"},
     UD | SD},
    {{CHI_REQ_Opcode_WriteBackPtl, "WriteBackPtl", F::kCopyBack, false, false, true, "B4.7.3"},
     UDP},
    {{CHI_REQ_Opcode_WriteCleanFull, "WriteCleanFull", F::kCopyBack, false, false, true, "B4.7.3"},
     UD | SD},
    {{CHI_REQ_Opcode_WriteEvictFull, "WriteEvictFull", F::kCopyBack, false, false, true, "B4.7.3"},
     UC},
    {{CHI_REQ_Opcode_CleanUnique, "CleanUnique", F::kDataless, true, true, true, "B4.7.2"},
     I | SC | UC | SD | UD | UCE},
    {{CHI_REQ_Opcode_MakeUnique, "MakeUnique", F::kDataless, true, true, true, "B4.7.2"},
     I | SC | SD | UC | UCE},
    {{CHI_REQ_Opcode_Evict, "Evict", F::kDataless, false, false, true, "B4.7.2"},
     I | SC | UC | UCE},
    {{CHI_REQ_Opcode_CleanShared, "CleanShared", F::kDataless, false, false, true, "B4.7.2"},
     I | SC | UC},
    {{CHI_REQ_Opcode_CleanInvalid, "CleanInvalid", F::kDataless, false, false, true, "B4.7.2"},
     I | SC | UC | UCE},
    {{CHI_REQ_Opcode_MakeInvalid, "MakeInvalid", F::kDataless, false, false, true, "B4.7.2"},
     I | SC | UC | UD | SD | UCE | UDP},
};
// Each cell is an intentional policy choice among the permitted transitions.
// Data policy: 0=no data, 1=always, 2=RetToSrc. Dirty responsibility is explicit.
struct Cell {
    S next;
    unsigned data;
    bool partial;
    bool pd;
};
struct SnoopRule {
    unsigned opcode;
    const char* spec;
    std::array<Cell, 7> cells;
};
const SnoopRule snoops[] = {
    {CHI_SNP_Opcode_SnpOnce,
     "B4.8.1.1",
     {{{S::kI, 0, 0, 0},
       {S::kSC, 2, 0, 0},
       {S::kUC, 2, 0, 0},
       {S::kUD, 1, 0, 0},
       {S::kSD, 1, 0, 0},
       {S::kUCE, 0, 0, 0},
       {S::kUDP, 1, 1, 0}}}},
    {CHI_SNP_Opcode_SnpShared,
     "B4.8.1.2",
     {{{S::kI, 0, 0, 0},
       {S::kSC, 2, 0, 0},
       {S::kSC, 2, 0, 0},
       {S::kSD, 1, 0, 0},
       {S::kSD, 1, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 1, 1, 1}}}},
    {CHI_SNP_Opcode_SnpClean,
     "B4.8.1.2",
     {{{S::kI, 0, 0, 0},
       {S::kSC, 2, 0, 0},
       {S::kSC, 2, 0, 0},
       {S::kSD, 1, 0, 0},
       {S::kSD, 1, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 1, 1, 1}}}},
    {CHI_SNP_Opcode_SnpNotSharedDirty,
     "B4.8.1.2",
     {{{S::kI, 0, 0, 0},
       {S::kSC, 2, 0, 0},
       {S::kSC, 2, 0, 0},
       {S::kSD, 1, 0, 0},
       {S::kSD, 1, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 1, 1, 1}}}},
    {CHI_SNP_Opcode_SnpUnique,
     "B4.8.1.3",
     {{{S::kI, 0, 0, 0},
       {S::kI, 2, 0, 0},
       {S::kI, 2, 0, 0},
       {S::kI, 1, 0, 1},
       {S::kI, 1, 0, 1},
       {S::kI, 0, 0, 0},
       {S::kI, 1, 1, 1}}}},
    {CHI_SNP_Opcode_SnpCleanShared,
     "B4.8.1.4",
     {{{S::kI, 0, 0, 0},
       {S::kSC, 0, 0, 0},
       {S::kUC, 0, 0, 0},
       {S::kUC, 1, 0, 1},
       {S::kSC, 1, 0, 1},
       {S::kI, 0, 0, 0},
       {S::kI, 1, 1, 1}}}},
    {CHI_SNP_Opcode_SnpCleanInvalid,
     "B4.8.1.4",
     {{{S::kI, 0, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 1, 0, 1},
       {S::kI, 1, 0, 1},
       {S::kI, 0, 0, 0},
       {S::kI, 1, 1, 1}}}},
    {CHI_SNP_Opcode_SnpMakeInvalid,
     "B4.8.1.4",
     {{{S::kI, 0, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 0, 0, 0},
       {S::kI, 0, 0, 0}}}},
};
const RequestRule& Find(unsigned op) {
    for (const auto& row : requests)
        if (row.traits.opcode == op)
            return row;
    // B13.10.18: defined opcodes outside the profile differ from reserved values.
    static const std::set<unsigned> extensions = {
        0,    5,    0x11, 0x13, 0x14, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2a,
        0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x3a, 0x41, 0x42, 0x43, 0x44, 0x47, 0x48, 0x4c, 0x4d, 0x4e, 0x50, 0x51, 0x52, 0x54, 0x56,
        0x58, 0x59, 0x5a, 0x5c, 0x5e, 0x60, 0x61, 0x62, 0x64, 0x66, 0x70, 0x71, 0x79};
    throw VIPError(
        extensions.count(op) ? ErrorKind::kUnsupportedFeature : ErrorKind::kProtocolViolation,
        "request outside GbCore: " + std::to_string(op));
}
}  // namespace
const OpcodeTraits& Traits(unsigned op) {
    return Find(op).traits;
}
bool RequiresAck(const TxnRequest& r) {
    return r.exp_comp_ack.value_or(Traits(r.opcode).ack_required);
}
void ValidateRequest(const TxnRequest& r, const RNConfig& c) {
    const auto& t = Traits(r.opcode);
    SizeEncoding(r.size);
    Check(r.address < c.memory_size && (!t.line_size || r.size == 64), ErrorKind::kInvalidRequest,
          "RN.FIELD.SIZE: invalid address/size for opcode");
    Check(r.order == 0 || r.order == 2 || r.order == 3, ErrorKind::kInvalidRequest,
          "RN.ORDER: RN Order=01 is reserved");
    const bool ordered = r.opcode == CHI_REQ_Opcode_ReadNoSnp ||
                         r.opcode == CHI_REQ_Opcode_ReadOnce || t.family == F::kImmediateWrite;
    Check(!r.order || ordered, ErrorKind::kInvalidRequest, "RN.ORDER: ordering not applicable");
    if (r.memory_type == MemoryType::kDeviceNRnE) {
        Check(r.opcode == CHI_REQ_Opcode_ReadNoSnp || r.opcode == CHI_REQ_Opcode_WriteNoSnpFull ||
                  r.opcode == CHI_REQ_Opcode_WriteNoSnpPtl,
              ErrorKind::kInvalidRequest,
              "RN.ATTR: Device memory only permits non-snoopable accesses");
        Check(r.order == 3, ErrorKind::kInvalidRequest,
              "RN.ATTR: Device-nRnE requires Endpoint Order");
        Check(r.opcode != CHI_REQ_Opcode_WriteNoSnpFull || !(r.address & 63),
              ErrorKind::kInvalidRequest, "RN.ATTR: Device Full write must be line aligned");
    } else {
        Check(r.memory_type == MemoryType::kNormal, ErrorKind::kInvalidRequest,
              "invalid memory type");
        Check(r.order != 3, ErrorKind::kInvalidRequest,
              "RN.ATTR: Normal memory cannot use Endpoint Order (B2.12)");
    }
    Check(!t.ack_required || RequiresAck(r), ErrorKind::kInvalidRequest,
          "RN.ACK: CompAck required");
    Check(t.family == F::kRead || t.ack_required || !RequiresAck(r), ErrorKind::kUnsupportedFeature,
          "RN.ACK: optional write CompAck/OWO not enabled");
    if (t.family == F::kImmediateWrite) {
        const auto skip = r.memory_type == MemoryType::kDeviceNRnE ? (r.address & (r.size - 1)) : 0;
        const auto mask = ByteMask(static_cast<unsigned>(((r.address & 63) & ~(r.size - 1)) + skip),
                                   r.size - skip);
        Check(!(r.byte_enable & ~mask), ErrorKind::kInvalidRequest,
              "RN.BE: byte outside Size window");
        if (r.opcode == CHI_REQ_Opcode_WriteNoSnpFull || r.opcode == CHI_REQ_Opcode_WriteUniqueFull)
            Check(r.byte_enable == mask, ErrorKind::kInvalidRequest,
                  "RN.BE: Full write has a hole");
    }
    if (r.opcode == CHI_REQ_Opcode_MakeUnique)
        Check(r.byte_enable == kAllBytes, ErrorKind::kInvalidRequest,
              "RN.MAKE: MakeUnique requires complete replacement data");
}
void ValidateStart(unsigned op, S state) {
    Check(static_cast<unsigned>(state) < 7, ErrorKind::kProtocolViolation,
          "invalid initial cache state");
    Check((Find(op).start & Bit(state)) != 0, ErrorKind::kInvalidRequest,
          "RN.START." + std::to_string(op) + ": illegal initial state " + StateName(state));
}
S ReadResult(unsigned op, S current, unsigned resp) {
    bool legal = false;
    if (op == CHI_REQ_Opcode_ReadNoSnp || op == CHI_REQ_Opcode_ReadOnce)
        legal = resp == 0 || resp == 2;
    if (op == CHI_REQ_Opcode_ReadShared)
        legal = resp == 1 || resp == 2 || resp == 6 || resp == 7;
    if (op == CHI_REQ_Opcode_ReadClean)
        legal = resp == 1 || resp == 2;
    if (op == CHI_REQ_Opcode_ReadUnique)
        legal = resp == 2 || resp == 6;
    if (op == CHI_REQ_Opcode_ReadNotSharedDirty)
        legal = resp == 1 || resp == 2 || resp == 6;
    Check(legal, ErrorKind::kProtocolViolation, "RN.READ.RESP: illegal CompData Resp");
    if (!Traits(op).allocating)
        return S::kI;
    if (op == CHI_REQ_Opcode_ReadUnique && IsDirty(current))
        return S::kUD;
    if (op == CHI_REQ_Opcode_ReadClean && current == S::kUCE)
        return S::kUC;
    if (resp == 1)
        return S::kSC;
    if (resp == 2)
        return S::kUC;
    return resp == 6 ? S::kUD : S::kSD;
}
unsigned EncodeState(S s, bool pd) {
    Check(static_cast<unsigned>(s) < 7, ErrorKind::kProtocolViolation,
          "invalid encoded cache state");
    unsigned bits = 0;
    if (s == S::kSC)
        bits = 1;
    if (s == S::kUC || s == S::kUD || s == S::kUCE || s == S::kUDP)
        bits = 2;
    if (s == S::kSD)
        bits = 3;
    return bits | (pd ? 4u : 0u);
}
SnoopDecision DecideSnoop(unsigned op, S state, bool ret, bool no_sd) {
    Check(static_cast<unsigned>(state) < 7, ErrorKind::kProtocolViolation, "invalid snoop state");
    for (const auto& row : snoops) {
        if (row.opcode != op)
            continue;
        const bool must_not_go_sd =
            op == CHI_SNP_Opcode_SnpUnique || op == CHI_SNP_Opcode_SnpCleanShared ||
            op == CHI_SNP_Opcode_SnpCleanInvalid || op == CHI_SNP_Opcode_SnpMakeInvalid;
        Check(!must_not_go_sd || no_sd, ErrorKind::kProtocolViolation,
              "RN.SNP.FIELD: DoNotGoToSD must be 1 (B13.10.38)");
        Check(
            !(ret && (op == CHI_SNP_Opcode_SnpCleanShared || op == CHI_SNP_Opcode_SnpCleanInvalid ||
                      op == CHI_SNP_Opcode_SnpMakeInvalid)),
            ErrorKind::kProtocolViolation,
            "RN.SNP.FIELD: RetToSrc reserved for maintenance snoops");
        auto cell = row.cells[static_cast<unsigned>(state)];
        if (no_sd && cell.next == S::kSD && op != CHI_SNP_Opcode_SnpOnce) {
            cell.next = S::kSC;
            cell.pd = true;
        }
        return {cell.next,
                cell.data == 1 || (cell.data == 2 && ret),
                cell.partial,
                cell.pd,
                "RN.SNP." + std::to_string(op) + "." + StateName(state),
                row.spec};
    }
    static const std::set<unsigned> extensions = {0,  5,  6,  11, 12, 13, 16,
                                                  17, 18, 19, 20, 21, 22, 23};
    throw VIPError(
        extensions.count(op) ? ErrorKind::kUnsupportedFeature : ErrorKind::kProtocolViolation,
        "snoop outside GbCore: " + std::to_string(op));
}
HazardDecision DecideHazard(RequestPhase phase, bool allocating) {
    // Columns distinguish allocating and non-allocating reads. Each phase has
    // exactly one outcome; no priority matching or default-success rule.
    using A = HazardAction;
    static const std::array<std::array<HazardDecision, 2>, 4> table = {{
        {{{A::kProcess, "RN.HAZARD.PENDING", "B4.11.1"},
          {A::kProcess, "RN.HAZARD.PENDING", "B4.11.1"}}},
        {{{A::kWaitForData, "RN.HAZARD.FILL", "B4.11.1"},
          {A::kWaitForData, "RN.HAZARD.FILL", "B4.11.1"}}},
        {{{A::kProcess, "RN.HAZARD.NONALLOC", "B2.6.3"},
          {A::kViolation, "RN.HAZARD.AFTER_COMP", "B2.6.3; B4.11.2"}}},
        {{{A::kViolation, "RN.HAZARD.AFTER_WRITE", "B4.11.2"},
          {A::kViolation, "RN.HAZARD.AFTER_WRITE", "B4.11.2"}}},
    }};
    const auto index = static_cast<unsigned>(phase);
    Check(index < table.size(), ErrorKind::kRuleCoverage, "uncovered request hazard phase");
    return table[index][allocating ? 1 : 0];
}
const std::vector<RuleDefinition>& RuleCatalog() {
    static const auto catalog = [] {
        std::vector<RuleDefinition> result;
        for (const auto& row : requests) {
            const auto suffix = std::to_string(row.traits.opcode);
            result.push_back({"RN.REQ." + suffix, RuleClass::kOpcode, "B4.2; B13.10.18"});
            result.push_back({"RN.START." + suffix, RuleClass::kRequestStart, row.traits.spec});
            result.push_back({"RN.RSP." + suffix, RuleClass::kRequestResponse, row.traits.spec});
        }
        for (const auto& row : snoops)
            for (unsigned state = 0; state < 7; ++state)
                result.push_back({"RN.SNP." + std::to_string(row.opcode) + "." +
                                      StateName(static_cast<S>(state)),
                                  RuleClass::kSnoopTransition, row.spec});
        std::set<std::string> seen;
        for (unsigned phase = 0; phase < 4; ++phase)
            for (bool allocating : {false, true}) {
                const auto d = DecideHazard(static_cast<RequestPhase>(phase), allocating);
                if (seen.insert(d.rule).second)
                    result.push_back({d.rule, RuleClass::kHazard, d.spec});
            }
        return result;
    }();
    return catalog;
}
void ValidateRuleTables() {
    std::set<unsigned> seen;
    for (const auto& row : requests)
        Check(seen.insert(row.traits.opcode).second && row.start && row.traits.spec,
              ErrorKind::kRuleCoverage, "duplicate or empty request rule");
    for (auto op : Capabilities::Requests())
        Check(seen.count(op), ErrorKind::kRuleCoverage, "missing request rule");
    Check(seen.size() == Capabilities::Requests().size(), ErrorKind::kRuleCoverage,
          "extra request rule");
    seen.clear();
    for (const auto& row : snoops) {
        Check(seen.insert(row.opcode).second, ErrorKind::kRuleCoverage, "duplicate snoop rule");
        for (const auto& c : row.cells)
            Check(static_cast<unsigned>(c.next) < 7 && c.data <= 2 && (!c.pd || c.data == 1),
                  ErrorKind::kRuleCoverage, "invalid snoop cell");
    }
    for (auto op : Capabilities::Snoops())
        Check(seen.count(op), ErrorKind::kRuleCoverage, "missing snoop rule");
    Check(seen.size() == Capabilities::Snoops().size(), ErrorKind::kRuleCoverage,
          "extra snoop rule");
    std::set<std::string> ids;
    std::array<unsigned, 5> counts{};
    for (const auto& rule : RuleCatalog()) {
        Check(!rule.spec.empty() && ids.insert(rule.id).second, ErrorKind::kRuleCoverage,
              "duplicate rule ID or missing specification reference");
        ++counts[static_cast<unsigned>(rule.category)];
    }
    Check(counts == std::array<unsigned, 5>{20, 20, 20, 56, 5}, ErrorKind::kRuleCoverage,
          "incomplete rule catalog");
}
}  // namespace esl::chi_vip
