#pragma once

#include <array>
#include <cstdint>
#include <ostream>
#include <string>

// AMBA CHI Issue G.b, Table B13.12: REQ channel opcodes.
#define CHI_REQ_LCRD_RETURN (0x00)
#define CHI_REQ_READ_SHARED (0x01)
#define CHI_REQ_READ_CLEAN (0x02)
#define CHI_REQ_READ_ONCE (0x03)
#define CHI_REQ_READ_NO_SNP (0x04)
#define CHI_REQ_PCRD_RETURN (0x05)
#define CHI_REQ_READ_UNIQUE (0x07)
#define CHI_REQ_CLEAN_SHARED (0x08)
#define CHI_REQ_CLEAN_INVALID (0x09)
#define CHI_REQ_MAKE_INVALID (0x0A)
#define CHI_REQ_CLEAN_UNIQUE (0x0B)
#define CHI_REQ_MAKE_UNIQUE (0x0C)
#define CHI_REQ_EVICT (0x0D)
#define CHI_REQ_READ_NO_SNP_SEP (0x11)
#define CHI_REQ_CLEAN_SHARED_PERSIST_SEP (0x13)
#define CHI_REQ_DVM_OP (0x14)
#define CHI_REQ_WRITE_EVICT_FULL (0x15)
#define CHI_REQ_WRITE_CLEAN_FULL (0x17)
#define CHI_REQ_WRITE_UNIQUE_PTL (0x18)
#define CHI_REQ_WRITE_UNIQUE_FULL (0x19)
#define CHI_REQ_WRITE_BACK_PTL (0x1A)
#define CHI_REQ_WRITE_BACK_FULL (0x1B)
#define CHI_REQ_WRITE_NO_SNP_PTL (0x1C)
#define CHI_REQ_WRITE_NO_SNP_FULL (0x1D)
#define CHI_REQ_WRITE_UNIQUE_FULL_STASH (0x20)
#define CHI_REQ_WRITE_UNIQUE_PTL_STASH (0x21)
#define CHI_REQ_STASH_ONCE_SHARED (0x22)
#define CHI_REQ_STASH_ONCE_UNIQUE (0x23)
#define CHI_REQ_READ_ONCE_CLEAN_INVALID (0x24)
#define CHI_REQ_READ_ONCE_MAKE_INVALID (0x25)
#define CHI_REQ_READ_NOT_SHARED_DIRTY (0x26)
#define CHI_REQ_CLEAN_SHARED_PERSIST (0x27)
#define CHI_REQ_ATOMIC_STORE_ADD (0x28)
#define CHI_REQ_ATOMIC_STORE_CLR (0x29)
#define CHI_REQ_ATOMIC_STORE_EOR (0x2A)
#define CHI_REQ_ATOMIC_STORE_SET (0x2B)
#define CHI_REQ_ATOMIC_STORE_SMAX (0x2C)
#define CHI_REQ_ATOMIC_STORE_SMIN (0x2D)
#define CHI_REQ_ATOMIC_STORE_UMAX (0x2E)
#define CHI_REQ_ATOMIC_STORE_UMIN (0x2F)
#define CHI_REQ_ATOMIC_LOAD_ADD (0x30)
#define CHI_REQ_ATOMIC_LOAD_CLR (0x31)
#define CHI_REQ_ATOMIC_LOAD_EOR (0x32)
#define CHI_REQ_ATOMIC_LOAD_SET (0x33)
#define CHI_REQ_ATOMIC_LOAD_SMAX (0x34)
#define CHI_REQ_ATOMIC_LOAD_SMIN (0x35)
#define CHI_REQ_ATOMIC_LOAD_UMAX (0x36)
#define CHI_REQ_ATOMIC_LOAD_UMIN (0x37)
#define CHI_REQ_ATOMIC_SWAP (0x38)
#define CHI_REQ_ATOMIC_COMPARE (0x39)
#define CHI_REQ_PREFETCH_TGT (0x3A)
#define CHI_REQ_MAKE_READ_UNIQUE (0x41)
#define CHI_REQ_WRITE_EVICT_OR_EVICT (0x42)
#define CHI_REQ_WRITE_UNIQUE_ZERO (0x43)
#define CHI_REQ_WRITE_NO_SNP_ZERO (0x44)
#define CHI_REQ_STASH_ONCE_SEP_SHARED (0x47)
#define CHI_REQ_STASH_ONCE_SEP_UNIQUE (0x48)
#define CHI_REQ_READ_PREFER_UNIQUE (0x4C)
#define CHI_REQ_CLEAN_INVALID_POPA (0x4D)
#define CHI_REQ_WRITE_NO_SNP_DEF (0x4E)
#define CHI_REQ_WRITE_NO_SNP_FULL_CLEAN_SH (0x50)
#define CHI_REQ_WRITE_NO_SNP_FULL_CLEAN_INV (0x51)
#define CHI_REQ_WRITE_NO_SNP_FULL_CLEAN_SH_PER_SEP (0x52)
#define CHI_REQ_WRITE_UNIQUE_FULL_CLEAN_SH (0x54)
#define CHI_REQ_WRITE_UNIQUE_FULL_CLEAN_SH_PER_SEP (0x56)
#define CHI_REQ_WRITE_BACK_FULL_CLEAN_SH (0x58)
#define CHI_REQ_WRITE_BACK_FULL_CLEAN_INV (0x59)
#define CHI_REQ_WRITE_BACK_FULL_CLEAN_SH_PER_SEP (0x5A)
#define CHI_REQ_WRITE_CLEAN_FULL_CLEAN_SH (0x5C)
#define CHI_REQ_WRITE_CLEAN_FULL_CLEAN_SH_PER_SEP (0x5E)
#define CHI_REQ_WRITE_NO_SNP_PTL_CLEAN_SH (0x60)
#define CHI_REQ_WRITE_NO_SNP_PTL_CLEAN_INV (0x61)
#define CHI_REQ_WRITE_NO_SNP_PTL_CLEAN_SH_PER_SEP (0x62)
#define CHI_REQ_WRITE_UNIQUE_PTL_CLEAN_SH (0x64)
#define CHI_REQ_WRITE_UNIQUE_PTL_CLEAN_SH_PER_SEP (0x66)
#define CHI_REQ_WRITE_NO_SNP_PTL_CLEAN_INV_POPA (0x70)
#define CHI_REQ_WRITE_NO_SNP_FULL_CLEAN_INV_POPA (0x71)
#define CHI_REQ_WRITE_BACK_FULL_CLEAN_INV_POPA (0x79)

// AMBA CHI Issue G.b, Table B13.14: RSP channel opcodes.
#define CHI_RSP_LCRD_RETURN (0x00)
#define CHI_RSP_SNP_RESP (0x01)
#define CHI_RSP_COMP_ACK (0x02)
#define CHI_RSP_RETRY_ACK (0x03)
#define CHI_RSP_COMP (0x04)
#define CHI_RSP_COMP_DBID_RESP (0x05)
#define CHI_RSP_DBID_RESP (0x06)
#define CHI_RSP_PCRD_GRANT (0x07)
#define CHI_RSP_READ_RECEIPT (0x08)
#define CHI_RSP_SNP_RESP_FWDED (0x09)
#define CHI_RSP_TAG_MATCH (0x0A)
#define CHI_RSP_RESP_SEP_DATA (0x0B)
#define CHI_RSP_PERSIST (0x0C)
#define CHI_RSP_COMP_PERSIST (0x0D)
#define CHI_RSP_DBID_RESP_ORD (0x0E)
#define CHI_RSP_STASH_DONE (0x10)
#define CHI_RSP_COMP_STASH_DONE (0x11)
#define CHI_RSP_COMP_CMO (0x14)

// AMBA CHI Issue G.b, Table B13.15: SNP channel opcodes.
#define CHI_SNP_LCRD_RETURN (0x00)
#define CHI_SNP_SHARED (0x01)
#define CHI_SNP_CLEAN (0x02)
#define CHI_SNP_ONCE (0x03)
#define CHI_SNP_NOT_SHARED_DIRTY (0x04)
#define CHI_SNP_UNIQUE_STASH (0x05)
#define CHI_SNP_MAKE_INVALID_STASH (0x06)
#define CHI_SNP_UNIQUE (0x07)
#define CHI_SNP_CLEAN_SHARED (0x08)
#define CHI_SNP_CLEAN_INVALID (0x09)
#define CHI_SNP_MAKE_INVALID (0x0A)
#define CHI_SNP_STASH_UNIQUE (0x0B)
#define CHI_SNP_STASH_SHARED (0x0C)
#define CHI_SNP_DVM_OP (0x0D)
#define CHI_SNP_QUERY (0x10)
#define CHI_SNP_SHARED_FWD (0x11)
#define CHI_SNP_CLEAN_FWD (0x12)
#define CHI_SNP_ONCE_FWD (0x13)
#define CHI_SNP_NOT_SHARED_DIRTY_FWD (0x14)
#define CHI_SNP_PREFER_UNIQUE (0x15)
#define CHI_SNP_PREFER_UNIQUE_FWD (0x16)
#define CHI_SNP_UNIQUE_FWD (0x17)

// AMBA CHI Issue G.b, Table B13.16: DAT channel opcodes.
#define CHI_DAT_LCRD_RETURN (0x00)
#define CHI_DAT_SNP_RESP_DATA (0x01)
#define CHI_DAT_COPY_BACK_WRITE_DATA (0x02)
#define CHI_DAT_NON_COPY_BACK_WRITE_DATA (0x03)
#define CHI_DAT_COMP_DATA (0x04)
#define CHI_DAT_SNP_RESP_DATA_PTL (0x05)
#define CHI_DAT_SNP_RESP_DATA_FWDED (0x06)
#define CHI_DAT_WRITE_DATA_CANCEL (0x07)
#define CHI_DAT_DATA_SEP_RESP (0x0B)
#define CHI_DAT_NCB_WR_DATA_COMP_ACK (0x0C)

namespace toc::chi {

using namespace std;

class ChiCommon final {
public:
    static constexpr uint32_t protocolMaxLinkCredit = 15;
    static constexpr uint32_t cacheLineBytes = 64;
    static constexpr uint32_t dataWordCount = cacheLineBytes / sizeof(uint32_t);

    static bool isReqOpcode(uint32_t opcode);
    static bool isRspOpcode(uint32_t opcode);
    static bool isSnpOpcode(uint32_t opcode);
    static bool isDatOpcode(uint32_t opcode);

    ChiCommon() = delete;
};

struct CHIReqFlit {
    uint32_t qos = 0;
    uint32_t tgtId = 0;
    uint32_t srcId = 0;
    uint32_t txnId = 0;
    uint32_t returnNid = 0;
    uint32_t returnTxnId = 0;
    uint32_t opcode = 0;
    uint32_t size = 0;
    uint64_t address = 0;
    uint32_t nonSecure = 0;
    uint32_t likelyShared = 0;
    uint32_t allowRetry = 0;
    uint32_t order = 0;
    uint32_t pCrdType = 0;
    uint32_t memAttr = 0;
    uint32_t snoopAttr = 0;
    uint32_t expectCompAck = 0;
    uint32_t traceTag = 0;

    string toString() const;
};

struct CHIRspFlit {
    uint32_t qos = 0;
    uint32_t tgtId = 0;
    uint32_t srcId = 0;
    uint32_t txnId = 0;
    uint32_t opcode = 0;
    uint32_t respErr = 0;
    uint32_t resp = 0;
    uint32_t fwdState = 0;
    uint32_t dbid = 0;
    uint32_t pCrdType = 0;
    uint32_t traceTag = 0;

    string toString() const;
};

struct CHIDatFlit {
    uint32_t qos = 0;
    uint32_t tgtId = 0;
    uint32_t srcId = 0;
    uint32_t txnId = 0;
    uint32_t homeNid = 0;
    uint32_t opcode = 0;
    uint32_t respErr = 0;
    uint32_t resp = 0;
    uint32_t dataSource = 0;
    uint32_t dbid = 0;
    uint32_t dataId = 0;
    uint32_t ccId = 0;
    array<uint32_t, 2> byteEnable = {0};
    array<uint32_t, ChiCommon::dataWordCount> data = {0};
    uint32_t traceTag = 0;

    string toString() const;
};

struct CHISnpFlit {
    uint32_t qos = 0;
    uint32_t srcId = 0;
    uint32_t txnId = 0;
    uint32_t fwdNid = 0;
    uint32_t fwdTxnId = 0;
    uint32_t opcode = 0;
    uint64_t address = 0;
    uint32_t nonSecure = 0;
    uint32_t doNotGoToSharedDirty = 0;
    uint32_t returnToSource = 0;
    uint32_t traceTag = 0;

    string toString() const;
};

ostream& operator<<(ostream& stream, const CHIReqFlit& flit);
ostream& operator<<(ostream& stream, const CHIRspFlit& flit);
ostream& operator<<(ostream& stream, const CHIDatFlit& flit);
ostream& operator<<(ostream& stream, const CHISnpFlit& flit);

static_assert(sizeof(uint32_t) * 8 >= 32, "CHI base fields require at least 32 bits");

}  // namespace toc::chi
