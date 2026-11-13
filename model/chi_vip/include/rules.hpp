#pragma once

#include "types.hpp"

namespace esl::chi_vip {
enum class RuleClass { kOpcode, kRequestStart, kRequestResponse, kSnoopTransition, kHazard };
struct RuleDefinition {
    std::string id;
    RuleClass category;
    std::string spec;
};
const std::vector<RuleDefinition>& RuleCatalog();
struct OpcodeTraits {
    unsigned opcode;
    const char* name;
    Family family;
    bool allocating;
    bool ack_required;
    bool line_size;
    const char* spec;
};
const OpcodeTraits& Traits(unsigned opcode);
void ValidateRequest(const TxnRequest& request, const RNConfig& config);
void ValidateStart(unsigned opcode, CacheState state);
bool RequiresAck(const TxnRequest& request);
CacheState ReadResult(unsigned opcode, CacheState current, unsigned resp);
unsigned EncodeState(CacheState state, bool pass_dirty);
struct SnoopDecision {
    CacheState next;
    bool data;
    bool partial;
    bool pass_dirty;
    std::string rule;
    const char* spec;
};
SnoopDecision DecideSnoop(unsigned opcode, CacheState state, bool ret_to_src, bool do_not_go_sd);
enum class RequestPhase { kBeforeResponse, kPartialRead, kCompletedRead, kTerminalWrite };
enum class HazardAction { kProcess, kWaitForData, kViolation };
struct HazardDecision {
    HazardAction action;
    const char* rule;
    const char* spec;
};
HazardDecision DecideHazard(RequestPhase phase, bool allocating);
void ValidateRuleTables();
}  // namespace esl::chi_vip
