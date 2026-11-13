#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>

// AMBA CHI Issue G.b, IHI 0050:
//   B13.9, Tables B13.6-B13.9: flit fields.
//   B13.10.18, Tables B13.12-B13.16: channel opcodes.
// Reference: doc/IHI0050G_b_amba_chi_architecture_spec.pdf.
//
// These value types carry one flit in a cycle-accurate interface shared by Fabric,
// RN and HN. They are not packed wire images: C++ storage widths can exceed the
// annotated protocol widths. Producers must respect the configured field widths
// and opcode-specific applicability. No masking or protocol validation is implicit.
//
// Wire-overlaid fields are separate semantic members, with their overlap noted
// below. Only the interpretation selected by the transaction is meaningful;
// inactive interpretations and disabled optional fields must remain zero.
// FLITV, FLITPEND, LCRDV, link activation and cycle timing belong to the interface,
// not the payload. A default-constructed flit does not imply a valid transfer.
// Every scalar and data word uses at least uint32_t, including one-bit flags.
// The wider storage does not change protocol widths or legal field values.

// REQ, 7 bits. Reserved values (including C2C-only encodings on other channels)
// are intentionally not named. Atomic sub-opcodes are expanded from Table B13.13.
#define CHI_REQ_Opcode_ReqLCrdReturn (0x00)
#define CHI_REQ_Opcode_ReadShared (0x01)
#define CHI_REQ_Opcode_ReadClean (0x02)
#define CHI_REQ_Opcode_ReadOnce (0x03)
#define CHI_REQ_Opcode_ReadNoSnp (0x04)
#define CHI_REQ_Opcode_PCrdReturn (0x05)
#define CHI_REQ_Opcode_ReadUnique (0x07)
#define CHI_REQ_Opcode_CleanShared (0x08)
#define CHI_REQ_Opcode_CleanInvalid (0x09)
#define CHI_REQ_Opcode_MakeInvalid (0x0A)
#define CHI_REQ_Opcode_CleanUnique (0x0B)
#define CHI_REQ_Opcode_MakeUnique (0x0C)
#define CHI_REQ_Opcode_Evict (0x0D)
#define CHI_REQ_Opcode_ReadNoSnpSep (0x11)
#define CHI_REQ_Opcode_CleanSharedPersistSep (0x13)
#define CHI_REQ_Opcode_DVMOp (0x14)
#define CHI_REQ_Opcode_WriteEvictFull (0x15)
#define CHI_REQ_Opcode_WriteCleanFull (0x17)
#define CHI_REQ_Opcode_WriteUniquePtl (0x18)
#define CHI_REQ_Opcode_WriteUniqueFull (0x19)
#define CHI_REQ_Opcode_WriteBackPtl (0x1A)
#define CHI_REQ_Opcode_WriteBackFull (0x1B)
#define CHI_REQ_Opcode_WriteNoSnpPtl (0x1C)
#define CHI_REQ_Opcode_WriteNoSnpFull (0x1D)
#define CHI_REQ_Opcode_WriteUniqueFullStash (0x20)
#define CHI_REQ_Opcode_WriteUniquePtlStash (0x21)
#define CHI_REQ_Opcode_StashOnceShared (0x22)
#define CHI_REQ_Opcode_StashOnceUnique (0x23)
#define CHI_REQ_Opcode_ReadOnceCleanInvalid (0x24)
#define CHI_REQ_Opcode_ReadOnceMakeInvalid (0x25)
#define CHI_REQ_Opcode_ReadNotSharedDirty (0x26)
#define CHI_REQ_Opcode_CleanSharedPersist (0x27)
#define CHI_REQ_Opcode_AtomicStoreADD (0x28)
#define CHI_REQ_Opcode_AtomicStoreCLR (0x29)
#define CHI_REQ_Opcode_AtomicStoreEOR (0x2A)
#define CHI_REQ_Opcode_AtomicStoreSET (0x2B)
#define CHI_REQ_Opcode_AtomicStoreSMAX (0x2C)
#define CHI_REQ_Opcode_AtomicStoreSMIN (0x2D)
#define CHI_REQ_Opcode_AtomicStoreUMAX (0x2E)
#define CHI_REQ_Opcode_AtomicStoreUMIN (0x2F)
#define CHI_REQ_Opcode_AtomicLoadADD (0x30)
#define CHI_REQ_Opcode_AtomicLoadCLR (0x31)
#define CHI_REQ_Opcode_AtomicLoadEOR (0x32)
#define CHI_REQ_Opcode_AtomicLoadSET (0x33)
#define CHI_REQ_Opcode_AtomicLoadSMAX (0x34)
#define CHI_REQ_Opcode_AtomicLoadSMIN (0x35)
#define CHI_REQ_Opcode_AtomicLoadUMAX (0x36)
#define CHI_REQ_Opcode_AtomicLoadUMIN (0x37)
#define CHI_REQ_Opcode_AtomicSwap (0x38)
#define CHI_REQ_Opcode_AtomicCompare (0x39)
#define CHI_REQ_Opcode_PrefetchTgt (0x3A)
#define CHI_REQ_Opcode_MakeReadUnique (0x41)
#define CHI_REQ_Opcode_WriteEvictOrEvict (0x42)
#define CHI_REQ_Opcode_WriteUniqueZero (0x43)
#define CHI_REQ_Opcode_WriteNoSnpZero (0x44)
#define CHI_REQ_Opcode_StashOnceSepShared (0x47)
#define CHI_REQ_Opcode_StashOnceSepUnique (0x48)
#define CHI_REQ_Opcode_ReadPreferUnique (0x4C)
#define CHI_REQ_Opcode_CleanInvalidPoPA (0x4D)
#define CHI_REQ_Opcode_WriteNoSnpDef (0x4E)
#define CHI_REQ_Opcode_WriteNoSnpFullCleanSh (0x50)
#define CHI_REQ_Opcode_WriteNoSnpFullCleanInv (0x51)
#define CHI_REQ_Opcode_WriteNoSnpFullCleanShPerSep (0x52)
#define CHI_REQ_Opcode_WriteUniqueFullCleanSh (0x54)
#define CHI_REQ_Opcode_WriteUniqueFullCleanShPerSep (0x56)
#define CHI_REQ_Opcode_WriteBackFullCleanSh (0x58)
#define CHI_REQ_Opcode_WriteBackFullCleanInv (0x59)
#define CHI_REQ_Opcode_WriteBackFullCleanShPerSep (0x5A)
#define CHI_REQ_Opcode_WriteCleanFullCleanSh (0x5C)
#define CHI_REQ_Opcode_WriteCleanFullCleanShPerSep (0x5E)
#define CHI_REQ_Opcode_WriteNoSnpPtlCleanSh (0x60)
#define CHI_REQ_Opcode_WriteNoSnpPtlCleanInv (0x61)
#define CHI_REQ_Opcode_WriteNoSnpPtlCleanShPerSep (0x62)
#define CHI_REQ_Opcode_WriteUniquePtlCleanSh (0x64)
#define CHI_REQ_Opcode_WriteUniquePtlCleanShPerSep (0x66)
#define CHI_REQ_Opcode_WriteNoSnpPtlCleanInvPoPA (0x70)
#define CHI_REQ_Opcode_WriteNoSnpFullCleanInvPoPA (0x71)
#define CHI_REQ_Opcode_WriteBackFullCleanInvPoPA (0x79)

// RSP, 5 bits (Table B13.14).
#define CHI_RSP_Opcode_RespLCrdReturn (0x00)
#define CHI_RSP_Opcode_SnpResp (0x01)
#define CHI_RSP_Opcode_CompAck (0x02)
#define CHI_RSP_Opcode_RetryAck (0x03)
#define CHI_RSP_Opcode_Comp (0x04)
#define CHI_RSP_Opcode_CompDBIDResp (0x05)
#define CHI_RSP_Opcode_DBIDResp (0x06)
#define CHI_RSP_Opcode_PCrdGrant (0x07)
#define CHI_RSP_Opcode_ReadReceipt (0x08)
#define CHI_RSP_Opcode_SnpRespFwded (0x09)
#define CHI_RSP_Opcode_TagMatch (0x0A)
#define CHI_RSP_Opcode_RespSepData (0x0B)
#define CHI_RSP_Opcode_Persist (0x0C)
#define CHI_RSP_Opcode_CompPersist (0x0D)
#define CHI_RSP_Opcode_DBIDRespOrd (0x0E)
#define CHI_RSP_Opcode_StashDone (0x10)
#define CHI_RSP_Opcode_CompStashDone (0x11)
#define CHI_RSP_Opcode_CompCMO (0x14)

// SNP, 5 bits (Table B13.15).
#define CHI_SNP_Opcode_SnpLCrdReturn (0x00)
#define CHI_SNP_Opcode_SnpShared (0x01)
#define CHI_SNP_Opcode_SnpClean (0x02)
#define CHI_SNP_Opcode_SnpOnce (0x03)
#define CHI_SNP_Opcode_SnpNotSharedDirty (0x04)
#define CHI_SNP_Opcode_SnpUniqueStash (0x05)
#define CHI_SNP_Opcode_SnpMakeInvalidStash (0x06)
#define CHI_SNP_Opcode_SnpUnique (0x07)
#define CHI_SNP_Opcode_SnpCleanShared (0x08)
#define CHI_SNP_Opcode_SnpCleanInvalid (0x09)
#define CHI_SNP_Opcode_SnpMakeInvalid (0x0A)
#define CHI_SNP_Opcode_SnpStashUnique (0x0B)
#define CHI_SNP_Opcode_SnpStashShared (0x0C)
#define CHI_SNP_Opcode_SnpDVMOp (0x0D)
#define CHI_SNP_Opcode_SnpQuery (0x10)
#define CHI_SNP_Opcode_SnpSharedFwd (0x11)
#define CHI_SNP_Opcode_SnpCleanFwd (0x12)
#define CHI_SNP_Opcode_SnpOnceFwd (0x13)
#define CHI_SNP_Opcode_SnpNotSharedDirtyFwd (0x14)
#define CHI_SNP_Opcode_SnpPreferUnique (0x15)
#define CHI_SNP_Opcode_SnpPreferUniqueFwd (0x16)
#define CHI_SNP_Opcode_SnpUniqueFwd (0x17)

// DAT, 4 bits (Table B13.16).
#define CHI_DAT_Opcode_DataLCrdReturn (0x0)
#define CHI_DAT_Opcode_SnpRespData (0x1)
#define CHI_DAT_Opcode_CopyBackWriteData (0x2)
#define CHI_DAT_Opcode_NonCopyBackWriteData (0x3)
#define CHI_DAT_Opcode_CompData (0x4)
#define CHI_DAT_Opcode_SnpRespDataPtl (0x5)
#define CHI_DAT_Opcode_SnpRespDataFwded (0x6)
#define CHI_DAT_Opcode_WriteDataCancel (0x7)
#define CHI_DAT_Opcode_DataSepResp (0xB)
#define CHI_DAT_Opcode_NCBWrDataCompAck (0xC)

namespace esl {

inline constexpr unsigned kCHINodeIDMinWidth = 7;
inline constexpr unsigned kCHINodeIDMaxWidth = 11;
inline constexpr unsigned kCHITxnIDWidth = 12;
inline constexpr unsigned kCHIDBIDWidth = 12;
inline constexpr unsigned kCHIReqAddrMinWidth = 44;
inline constexpr unsigned kCHIReqAddrMaxWidth = 52;
inline constexpr unsigned kCHIReqOpcodeWidth = 7;
inline constexpr unsigned kCHIRspOpcodeWidth = 5;
inline constexpr unsigned kCHISnpOpcodeWidth = 5;
inline constexpr unsigned kCHIDatOpcodeWidth = 4;
inline constexpr std::size_t kCHIDefaultDataWidth = 256;

using NodeID = std::uint32_t;  // 7..11 significant bits, selected per interface.
using TxnID = std::uint32_t;   // 12 significant bits.
using DBID = std::uint32_t;    // 12 significant bits, including on DAT.

// Table B13.6. Routing and transaction defaults are independent of node role.
struct CHIReqFlit {
    std::uint32_t flit_qos = 0;  // 4 bits.
    NodeID flit_tgt = 0;
    NodeID flit_src = 0;
    TxnID flit_txn = 0;

    // ReturnNID / StashNID / zero-extended DataTarget share a wire field.
    NodeID flit_return_nid = 0;
    NodeID flit_stash_nid = 0;
    std::uint32_t flit_data_target = 0;  // 7 bits.
    // StashNIDValid / Endian / Deep / PrefetchTgtHint share one wire bit.
    std::uint32_t flit_stash_nid_valid = 0;
    std::uint32_t flit_endian = 0;
    std::uint32_t flit_deep = 0;
    std::uint32_t flit_prefetch_tgt_hint = 0;
    // ReturnTxnID / {6'b0, StashLPIDValid, StashLPID} share 12 wire bits.
    TxnID flit_return_txn_id = 0;
    std::uint32_t flit_stash_lpid_valid = 0;
    std::uint32_t flit_stash_lpid = 0;  // 5 bits.

    std::uint32_t flit_opcode = CHI_REQ_Opcode_ReqLCrdReturn;
    std::uint32_t flit_size = 0;     // 3 bits, log2(number of bytes).
    std::uint64_t flit_address = 0;  // Req_Addr_Width bits; byte address or DVM payload.
    std::uint32_t flit_ns = 0;
    std::uint32_t flit_nse = 0;
    std::uint32_t flit_likely_shared = 0;
    std::uint32_t flit_allow_retry = 0;
    std::uint32_t flit_order = 0;      // 2 bits.
    std::uint32_t flit_pcrd_type = 0;  // 4 bits.
    std::uint32_t flit_mem_attr = 0;   // 4 bits: Allocate, Cacheable, Device, EWA [3:0].
    // SnpAttr / DoDWT share one wire bit.
    std::uint32_t flit_snp_attr = 0;
    std::uint32_t flit_do_dwt = 0;
    // PGroupID / StashGroupID / TagGroupID / {3'b0, LPID} share 8 wire bits.
    std::uint32_t flit_p_group_id = 0;
    std::uint32_t flit_stash_group_id = 0;
    std::uint32_t flit_tag_group_id = 0;
    std::uint32_t flit_lpid = 0;  // 5 bits.
    // Excl / SnoopMe / CAH share one wire bit.
    std::uint32_t flit_excl = 0;
    std::uint32_t flit_snoop_me = 0;
    std::uint32_t flit_cah = 0;
    std::uint32_t flit_exp_comp_ack = 0;
    std::uint32_t flit_tag_op = 0;  // 2 bits.
    std::uint32_t flit_trace_tag = 0;

    // Optional protocol fields. MECID / StreamID share the same wire field.
    std::uint32_t flit_mpam = 0;       // 0, 12 or 15 bits.
    std::uint32_t flit_pbha = 0;       // 0 or 4 bits.
    std::uint32_t flit_mecid = 0;      // 0 or 16 bits.
    std::uint32_t flit_stream_id = 0;  // 0 or 16 bits.
    std::uint32_t flit_sec_sid1 = 0;   // 0 or 1 bit.
    std::uint32_t flit_rsvdc = 0;      // 0, 4, 8, 12, 16, 24 or 32 bits.

    // Restore every field, including optional controls and data, to its default.
    void reset() noexcept {
        *this = CHIReqFlit{};
    }

    // Control summary only; numeric values always have a 0x prefix.
    std::string to_string() const {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::hex << "CHIReqFlit{"
               << "flit_opcode=0x" << flit_opcode << ", flit_src=0x" << flit_src << ", flit_tgt=0x"
               << flit_tgt << ", flit_txn=0x" << flit_txn << ", flit_address=0x" << flit_address
               << ", flit_size=0x" << flit_size << ", flit_qos=0x" << flit_qos << ", flit_ns=0x"
               << flit_ns << ", flit_nse=0x" << flit_nse << ", flit_allow_retry=0x"
               << flit_allow_retry << ", flit_order=0x" << flit_order << ", flit_pcrd_type=0x"
               << flit_pcrd_type << ", flit_mem_attr=0x" << flit_mem_attr << ", flit_snp_attr=0x"
               << flit_snp_attr << ", flit_exp_comp_ack=0x" << flit_exp_comp_ack
               << ", flit_return_nid=0x" << flit_return_nid << ", flit_return_txn_id=0x"
               << flit_return_txn_id << "}";
        return stream.str();
    }

    // Format in a private stream so caller flags, fill and precision are preserved.
    friend std::ostream& operator<<(std::ostream& stream, const CHIReqFlit& flit) {
        return stream << flit.to_string();
    }
};

// Table B13.7.
struct CHIRspFlit {
    std::uint32_t flit_qos = 0;  // 4 bits.
    NodeID flit_tgt = 0;
    NodeID flit_src = 0;
    TxnID flit_txn = 0;
    std::uint32_t flit_opcode = CHI_RSP_Opcode_RespLCrdReturn;
    std::uint32_t flit_resp_err = 0;  // 2 bits.
    std::uint32_t flit_resp = 0;      // 3 bits, interpretation depends on opcode.
    // FwdState / {2'b0, DataPull} share 3 wire bits.
    std::uint32_t flit_fwd_state = 0;  // 3 bits.
    std::uint32_t flit_data_pull = 0;
    std::uint32_t flit_c_busy = 0;  // 3 bits.
    // DBID / zero-extended PGroupID / StashGroupID / TagGroupID share 12 bits.
    DBID flit_dbid = 0;
    std::uint32_t flit_p_group_id = 0;
    std::uint32_t flit_stash_group_id = 0;
    std::uint32_t flit_tag_group_id = 0;
    std::uint32_t flit_pcrd_type = 0;  // 4 bits.
    std::uint32_t flit_tag_op = 0;     // 2 bits.
    std::uint32_t flit_trace_tag = 0;

    // Restore every field, including optional controls and data, to its default.
    void reset() noexcept {
        *this = CHIRspFlit{};
    }

    // Control summary only; numeric values always have a 0x prefix.
    std::string to_string() const {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::hex << "CHIRspFlit{"
               << "flit_opcode=0x" << flit_opcode << ", flit_src=0x" << flit_src << ", flit_tgt=0x"
               << flit_tgt << ", flit_txn=0x" << flit_txn << ", flit_qos=0x" << flit_qos
               << ", flit_resp_err=0x" << flit_resp_err << ", flit_resp=0x" << flit_resp
               << ", flit_fwd_state=0x" << flit_fwd_state << ", flit_data_pull=0x" << flit_data_pull
               << ", flit_c_busy=0x" << flit_c_busy << ", flit_dbid=0x" << flit_dbid
               << ", flit_pcrd_type=0x" << flit_pcrd_type << ", flit_tag_op=0x" << flit_tag_op
               << "}";
        return stream.str();
    }

    // Format in a private stream so caller flags, fill and precision are preserved.
    friend std::ostream& operator<<(std::ostream& stream, const CHIRspFlit& flit) {
        return stream << flit.to_string();
    }
};

// Table B13.8. SNP has no TgtID field; the fabric selects receiving snoop ports.
struct CHISnpFlit {
    std::uint32_t flit_qos = 0;  // 4 bits.
    NodeID flit_src = 0;
    TxnID flit_txn = 0;
    // FwdNID / zero-extended PBHA share a wire field.
    NodeID flit_fwd_nid = 0;
    std::uint32_t flit_pbha = 0;  // 4 bits.
    // FwdTxnID / {6'b0, StashLPIDValid, StashLPID} / zero-extended VMIDExt.
    TxnID flit_fwd_txn_id = 0;
    std::uint32_t flit_stash_lpid_valid = 0;
    std::uint32_t flit_stash_lpid = 0;  // 5 bits.
    std::uint32_t flit_vmid_ext = 0;    // 8 bits.
    std::uint32_t flit_opcode = CHI_SNP_Opcode_SnpLCrdReturn;
    // Encoded Addr[Req_Addr_Width-1:3], NOT a byte address. For an ordinary snoop,
    // assign byte_address >> 3; SnpDVMOp carries its encoded DVM payload here.
    std::uint64_t flit_address = 0;  // 41..49 bits.
    std::uint32_t flit_ns = 0;
    std::uint32_t flit_nse = 0;
    std::uint32_t flit_do_not_go_to_sd = 0;
    std::uint32_t flit_ret_to_src = 0;
    std::uint32_t flit_trace_tag = 0;
    std::uint32_t flit_mpam = 0;   // Optional: 0, 12 or 15 bits.
    std::uint32_t flit_mecid = 0;  // Optional: 0 or 16 bits.

    // Restore every field, including optional controls and data, to its default.
    void reset() noexcept {
        *this = CHISnpFlit{};
    }

    // Control summary only; numeric values always have a 0x prefix.
    std::string to_string() const {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::hex << "CHISnpFlit{"
               << "flit_opcode=0x" << flit_opcode << ", flit_src=0x" << flit_src << ", flit_txn=0x"
               << flit_txn << ", flit_qos=0x" << flit_qos << ", flit_fwd_nid=0x" << flit_fwd_nid
               << ", flit_fwd_txn_id=0x" << flit_fwd_txn_id << ", flit_address=0x" << flit_address
               << ", flit_ns=0x" << flit_ns << ", flit_nse=0x" << flit_nse
               << ", flit_do_not_go_to_sd=0x" << flit_do_not_go_to_sd << ", flit_ret_to_src=0x"
               << flit_ret_to_src << "}";
        return stream.str();
    }

    // Format in a private stream so caller flags, fill and precision are preserved.
    friend std::ostream& operator<<(std::ostream& stream, const CHISnpFlit& flit) {
        return stream << flit.to_string();
    }
};

// Table B13.9. DataWidth is the DAT bus width in bits, not the cache line size.
template <std::size_t DataWidth = kCHIDefaultDataWidth>
struct CHIDatFlitT {
    static_assert(DataWidth == 128 || DataWidth == 256 || DataWidth == 512,
                  "CHI DAT width must be 128, 256 or 512 bits");
    static constexpr std::size_t kDataWidth = DataWidth;
    static constexpr std::size_t kDataBytes = DataWidth / 8;
    static constexpr std::size_t kDataWords = DataWidth / 32;

    std::uint32_t flit_qos = 0;  // 4 bits.
    NodeID flit_tgt = 0;
    NodeID flit_src = 0;
    TxnID flit_txn = 0;
    // HomeNID / zero-extended PBHA share a wire field.
    NodeID flit_home_nid = 0;
    std::uint32_t flit_pbha = 0;  // 4 bits.
    std::uint32_t flit_opcode = CHI_DAT_Opcode_DataLCrdReturn;
    std::uint32_t flit_resp_err = 0;  // 2 bits.
    std::uint32_t flit_resp = 0;      // 3 bits, interpretation depends on opcode.
    // DataSource / {5'b0, FwdState} share 8 wire bits in Issue G.b.
    std::uint32_t flit_data_source = 0;
    std::uint32_t flit_fwd_state = 0;  // 3 bits.
    std::uint32_t flit_data_pull = 0;  // Separate bit in DAT, unlike RSP.
    std::uint32_t flit_c_busy = 0;     // 3 bits.
    std::uint32_t flit_mecid = 0;      // Optional: 0 or 16 bits.
    DBID flit_dbid = 0;                // 16 wire bits: {4'b0, DBID[11:0]}.
    std::uint32_t flit_ccid = 0;       // 2 bits.
    // 2 bits, indexes 16-byte chunks: a full line uses 0/1/2/3 at 128 bits,
    // 0/2 at 256 bits, and 0 at 512 bits (before any critical-chunk wrapping).
    std::uint32_t flit_data_id = 0;
    std::uint32_t flit_tag_op = 0;  // 2 bits.
    std::uint32_t flit_tag = 0;     // DataWidth/32 bits: four bits per 16-byte granule.
    std::uint32_t flit_tu = 0;      // DataWidth/128 bits.
    std::uint32_t flit_trace_tag = 0;
    std::uint32_t flit_cah = 0;
    std::uint32_t flit_num_dat = 0;  // 2 bits.
    std::uint32_t flit_replicate = 0;
    std::uint32_t flit_rsvdc = 0;  // Optional: 0, 4, 8, 12, 16, 24 or 32 bits.

    // Word i stores Data[32*i+31:32*i]. Byte j is in word j/4 at bits
    // [8*(j%4)+7:8*(j%4)], independent of host byte order. BE/DataCheck
    // bit j describes byte j; unused mask bits must be zero.
    // BE is initially clear: the sender explicitly marks valid byte lanes.
    std::uint64_t flit_be = 0;  // Low kDataBytes bits are meaningful.
    std::array<std::uint32_t, kDataWords> flit_data{};
    std::uint64_t flit_data_check = 0;  // Optional: 0 or DataWidth/8 bits.
    std::uint32_t flit_poison = 0;      // Optional: one bit per 64 data bits.

    // Restore every field, including optional controls and data, to its default.
    void reset() noexcept {
        *this = CHIDatFlitT{};
    }

    // Control summary only; numeric values always have a 0x prefix.
    std::string to_string() const {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::hex << "CHIDatFlit{"
               << "flit_opcode=0x" << flit_opcode << ", flit_src=0x" << flit_src << ", flit_tgt=0x"
               << flit_tgt << ", flit_txn=0x" << flit_txn << ", flit_home_nid=0x" << flit_home_nid
               << ", flit_qos=0x" << flit_qos << ", flit_resp_err=0x" << flit_resp_err
               << ", flit_resp=0x" << flit_resp << ", flit_data_source=0x" << flit_data_source
               << ", flit_fwd_state=0x" << flit_fwd_state << ", flit_data_pull=0x" << flit_data_pull
               << ", flit_c_busy=0x" << flit_c_busy << ", flit_dbid=0x" << flit_dbid
               << ", flit_ccid=0x" << flit_ccid << ", flit_data_id=0x" << flit_data_id
               << ", flit_tag_op=0x" << flit_tag_op << ", flit_tag=0x" << flit_tag << ", flit_tu=0x"
               << flit_tu << ", flit_num_dat=0x" << flit_num_dat << ", flit_replicate=0x"
               << flit_replicate << ", flit_be=0x" << flit_be << ", flit_data_check=0x"
               << flit_data_check << ", flit_poison=0x" << flit_poison << "}";
        return stream.str();
    }

    // Format in a private stream so caller flags, fill and precision are preserved.
    friend std::ostream& operator<<(std::ostream& stream, const CHIDatFlitT& flit) {
        return stream << flit.to_string();
    }
};

// The current RN/HN data path uses a 256-bit interface.
using CHIDatFlit = CHIDatFlitT<>;

}  // namespace esl
