#include "types.hpp"

#include <algorithm>
#include <utility>

namespace esl::chi_vip {
VIPError::VIPError(ErrorKind kind, const std::string& message)
    : std::runtime_error(message), kind_(kind) {}
ErrorKind VIPError::Kind() const {
    return kind_;
}
void Check(bool condition, ErrorKind kind, const std::string& message) {
    if (!condition)
        throw VIPError(kind, message);
}
const char* StateName(CacheState state) {
    static const char* names[] = {"I", "SC", "UC", "UD", "SD", "UCE", "UDP"};
    const auto index = static_cast<unsigned>(state);
    Check(index < 7, ErrorKind::kProtocolViolation, "invalid cache state");
    return names[index];
}
bool IsDirty(CacheState s) {
    return s == CacheState::kUD || s == CacheState::kSD || s == CacheState::kUDP;
}
bool IsUnique(CacheState s) {
    return s == CacheState::kUC || s == CacheState::kUD || s == CacheState::kUCE ||
           s == CacheState::kUDP;
}
std::uint64_t LineAddress(std::uint64_t a) {
    return a & ~std::uint64_t{63};
}
std::uint64_t ByteMask(unsigned offset, unsigned size) {
    Check(size > 0 && size <= 64 && offset <= 64 - size, ErrorKind::kInvalidRequest,
          "access must fit in one 64-byte line");
    return (size == 64 ? kAllBytes : ((std::uint64_t{1} << size) - 1)) << offset;
}
unsigned SizeEncoding(unsigned size) {
    Check(size && size <= 64 && !(size & (size - 1)), ErrorKind::kInvalidRequest,
          "CHI size must be a power of two in 1..64");
    unsigned result = 0;
    while ((1u << result) != size)
        ++result;
    return result;
}
unsigned BeatMask(std::uint64_t address, unsigned size) {
    SizeEncoding(size);
    return size == 64 ? 3u : (1u << ((address & 63) / 32));
}
Line Unpack(const CHIDatFlit& flit) {
    Check(flit.flit_data_id == 0 || flit.flit_data_id == 2, ErrorKind::kProtocolViolation,
          "invalid 256-bit DataID");
    Line data{};
    for (unsigned i = 0; i < 32; ++i)
        data[flit.flit_data_id * 16 + i] =
            static_cast<std::uint8_t>(flit.flit_data[i / 4] >> (8 * (i % 4)));
    return data;
}
CHIDatFlit Pack(const Line& data, std::uint64_t mask, unsigned data_id) {
    Check(data_id == 0 || data_id == 2, ErrorKind::kProtocolViolation, "invalid transmit DataID");
    CHIDatFlit flit;
    flit.flit_data_id = data_id;
    flit.flit_be = (mask >> (data_id * 16)) & 0xffffffffu;
    for (unsigned i = 0; i < 32; ++i)
        if ((flit.flit_be >> i) & 1)
            flit.flit_data[i / 4] |= static_cast<std::uint32_t>(data[data_id * 16 + i])
                                     << (8 * (i % 4));
    return flit;
}
void RNConfig::Validate() const {
    static_assert(CHIDatFlit::kDataWidth == Capabilities::kDataBits, "GbCore requires 256-bit DAT");
    Check(node_id_width >= 7 && node_id_width <= 11 && node_id < (1u << node_id_width) &&
              home_id < (1u << node_id_width) && node_id != home_id,
          ErrorKind::kConfiguration, "invalid RN/HN NodeID configuration");
    Check(address_width >= 44 && address_width <= 52 && memory_size >= 64 &&
              memory_size % 64 == 0 && memory_size <= (std::uint64_t{1} << address_width),
          ErrorKind::kConfiguration, "invalid memory address range");
    Check(endpoint_size >= 64 && !(endpoint_size & (endpoint_size - 1)), ErrorKind::kConfiguration,
          "endpoint size must be a power of two >=64");
    Check(cache_lines && request_slots && maintenance_slots == 1 && request_slots <= 1023 &&
              snoop_slots && pending_operations,
          ErrorKind::kConfiguration, "invalid resource capacities or outstanding limit");
}
const std::array<unsigned, 20>& Capabilities::Requests() {
    static const std::array<unsigned, 20> ops = {
        CHI_REQ_Opcode_ReadNoSnp,       CHI_REQ_Opcode_ReadOnce,
        CHI_REQ_Opcode_ReadShared,      CHI_REQ_Opcode_ReadClean,
        CHI_REQ_Opcode_ReadUnique,      CHI_REQ_Opcode_ReadNotSharedDirty,
        CHI_REQ_Opcode_WriteNoSnpFull,  CHI_REQ_Opcode_WriteNoSnpPtl,
        CHI_REQ_Opcode_WriteUniqueFull, CHI_REQ_Opcode_WriteUniquePtl,
        CHI_REQ_Opcode_WriteBackFull,   CHI_REQ_Opcode_WriteBackPtl,
        CHI_REQ_Opcode_WriteCleanFull,  CHI_REQ_Opcode_WriteEvictFull,
        CHI_REQ_Opcode_CleanUnique,     CHI_REQ_Opcode_MakeUnique,
        CHI_REQ_Opcode_Evict,           CHI_REQ_Opcode_CleanShared,
        CHI_REQ_Opcode_CleanInvalid,    CHI_REQ_Opcode_MakeInvalid};
    return ops;
}
const std::array<unsigned, 8>& Capabilities::Snoops() {
    static const std::array<unsigned, 8> ops = {
        CHI_SNP_Opcode_SnpOnce,         CHI_SNP_Opcode_SnpShared,
        CHI_SNP_Opcode_SnpClean,        CHI_SNP_Opcode_SnpNotSharedDirty,
        CHI_SNP_Opcode_SnpUnique,       CHI_SNP_Opcode_SnpCleanShared,
        CHI_SNP_Opcode_SnpCleanInvalid, CHI_SNP_Opcode_SnpMakeInvalid};
    return ops;
}
bool Capabilities::SupportsRequest(unsigned op) {
    const auto& v = Requests();
    return std::find(v.begin(), v.end(), op) != v.end();
}
bool Capabilities::SupportsSnoop(unsigned op) {
    const auto& v = Snoops();
    return std::find(v.begin(), v.end(), op) != v.end();
}
Event Event::Received(Flit flit) {
    return {EventKind::kReceive, 0, std::move(flit), {}};
}
Event Event::Accepted(Token token) {
    return {EventKind::kTxAccepted, token, CHIRspFlit{}, {}};
}
Event Event::Sent(Token token, std::optional<std::uint64_t> cycle) {
    return {EventKind::kTxSent, token, CHIRspFlit{}, cycle};
}
}  // namespace esl::chi_vip
