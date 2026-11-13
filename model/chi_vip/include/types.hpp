#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "chi_common.h"

namespace esl::chi_vip {
using Line = std::array<std::uint8_t, 64>;
using Handle = std::uint64_t;
using Token = std::uint64_t;
using Flit = std::variant<CHIReqFlit, CHIRspFlit, CHISnpFlit, CHIDatFlit>;
inline constexpr std::uint64_t kAllBytes = ~std::uint64_t{0};

enum class CacheState { kI, kSC, kUC, kUD, kSD, kUCE, kUDP };
enum class Family { kRead, kImmediateWrite, kCopyBack, kDataless };
enum class MemoryType { kNormal, kDeviceNRnE };
enum class AccessError { kOK, kDataError, kNonDataError };
enum class ErrorKind {
    kConfiguration,
    kInvalidRequest,
    kUnsupportedFeature,
    kProtocolViolation,
    kRuleCoverage,
    kTimeout,
    kLifecycle
};
class VIPError : public std::runtime_error {
public:
    VIPError(ErrorKind kind, const std::string& message);
    ErrorKind Kind() const;

private:
    ErrorKind kind_;
};
void Check(bool condition, ErrorKind kind, const std::string& message);
const char* StateName(CacheState state);
bool IsDirty(CacheState state);
bool IsUnique(CacheState state);
std::uint64_t LineAddress(std::uint64_t address);
std::uint64_t ByteMask(unsigned offset, unsigned size);
unsigned BeatMask(std::uint64_t address, unsigned size);
unsigned SizeEncoding(unsigned size);
Line Unpack(const CHIDatFlit& flit);
CHIDatFlit Pack(const Line& data, std::uint64_t mask, unsigned data_id);

struct RNConfig {
    NodeID node_id = 0;
    NodeID home_id = 1;
    unsigned node_id_width = 7;
    unsigned address_width = 48;
    std::uint64_t memory_size = 1 << 20;
    std::uint64_t endpoint_size = 4096;
    unsigned cache_lines = 64;
    unsigned request_slots = 16;
    unsigned maintenance_slots = 1;
    unsigned snoop_slots = 8;
    unsigned pending_operations = 64;
    std::uint64_t timeout_cycles = 10000;
    void Validate() const;
};

struct Capabilities {
    static constexpr const char* kRevision = "CHI G.b";
    static constexpr const char* kProfile = "GbCore RN-F";
    static constexpr unsigned kLineBytes = 64;
    static constexpr unsigned kDataBits = 256;
    static const std::array<unsigned, 20>& Requests();
    static const std::array<unsigned, 8>& Snoops();
    static bool SupportsRequest(unsigned opcode);
    static bool SupportsSnoop(unsigned opcode);
};

// Data and byte_enable use cache-line-relative byte positions. Normal memory
// accesses address the Size-aligned window; address may select a critical chunk.
struct TxnRequest {
    unsigned opcode = CHI_REQ_Opcode_ReadShared;
    std::uint64_t address = 0;
    unsigned size = 64;
    unsigned order = 0;
    unsigned stream_id = 0;  // Local ordering stream, not optional wire StreamID.
    MemoryType memory_type = MemoryType::kNormal;
    std::optional<bool> exp_comp_ack;
    Line data{};
    std::uint64_t byte_enable = kAllBytes;
};
struct SubmitResult {
    bool accepted = false;
    Handle handle = 0;
};
struct CacheLine {
    CacheState state = CacheState::kI;
    Line data{};
    std::uint64_t valid = 0;
    std::uint64_t dirty = 0;
    std::uint64_t errors = 0;
    std::uint64_t lru = 0;
    bool cah = false;
};
struct Completion {
    Handle handle = 0;
    AccessError error = AccessError::kOK;
    Line data{};
    std::uint64_t valid = 0;
    CacheState state = CacheState::kI;
    std::uint64_t cycle = 0;
};
enum class EventKind { kTxAccepted, kTxSent, kReceive };
struct Event {
    EventKind kind = EventKind::kReceive;
    Token token = 0;
    Flit flit = CHIRspFlit{};
    std::optional<std::uint64_t> observed_cycle;
    static Event Received(Flit flit);
    static Event Accepted(Token token);
    static Event Sent(Token token, std::optional<std::uint64_t> cycle = {});
};
struct SendAction {
    Token token = 0;
    NodeID destination = 0;
    Flit flit;
};
struct Observation {
    std::uint64_t cycle = 0;
    Handle handle = 0;
    std::uint64_t address = 0;
    std::string action;
    std::string rule;
    CacheState before = CacheState::kI;
    CacheState after = CacheState::kI;
    std::optional<Flit> flit;
    Line data{};
    std::uint64_t mask = 0;
};
using Observer = std::function<void(const Observation&)>;
}  // namespace esl::chi_vip
