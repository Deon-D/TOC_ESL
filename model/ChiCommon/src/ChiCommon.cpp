#include "ChiCommon.h"

#include <sstream>

namespace toc::chi {

using namespace std;

bool ChiCommon::isReqOpcode(uint32_t opcode) {
    if ((opcode >= CHI_REQ_ATOMIC_STORE_ADD && opcode <= CHI_REQ_ATOMIC_STORE_UMIN) ||
        (opcode >= CHI_REQ_ATOMIC_LOAD_ADD && opcode <= CHI_REQ_ATOMIC_LOAD_UMIN)) {
        return true;
    }

    switch (opcode) {
    case CHI_REQ_LCRD_RETURN:
    case CHI_REQ_READ_SHARED:
    case CHI_REQ_READ_CLEAN:
    case CHI_REQ_READ_ONCE:
    case CHI_REQ_READ_NO_SNP:
    case CHI_REQ_PCRD_RETURN:
    case CHI_REQ_READ_UNIQUE:
    case CHI_REQ_CLEAN_SHARED:
    case CHI_REQ_CLEAN_INVALID:
    case CHI_REQ_MAKE_INVALID:
    case CHI_REQ_CLEAN_UNIQUE:
    case CHI_REQ_MAKE_UNIQUE:
    case CHI_REQ_EVICT:
    case CHI_REQ_READ_NO_SNP_SEP:
    case CHI_REQ_CLEAN_SHARED_PERSIST_SEP:
    case CHI_REQ_DVM_OP:
    case CHI_REQ_WRITE_EVICT_FULL:
    case CHI_REQ_WRITE_CLEAN_FULL:
    case CHI_REQ_WRITE_UNIQUE_PTL:
    case CHI_REQ_WRITE_UNIQUE_FULL:
    case CHI_REQ_WRITE_BACK_PTL:
    case CHI_REQ_WRITE_BACK_FULL:
    case CHI_REQ_WRITE_NO_SNP_PTL:
    case CHI_REQ_WRITE_NO_SNP_FULL:
    case CHI_REQ_WRITE_UNIQUE_FULL_STASH:
    case CHI_REQ_WRITE_UNIQUE_PTL_STASH:
    case CHI_REQ_STASH_ONCE_SHARED:
    case CHI_REQ_STASH_ONCE_UNIQUE:
    case CHI_REQ_READ_ONCE_CLEAN_INVALID:
    case CHI_REQ_READ_ONCE_MAKE_INVALID:
    case CHI_REQ_READ_NOT_SHARED_DIRTY:
    case CHI_REQ_CLEAN_SHARED_PERSIST:
    case CHI_REQ_ATOMIC_SWAP:
    case CHI_REQ_ATOMIC_COMPARE:
    case CHI_REQ_PREFETCH_TGT:
    case CHI_REQ_MAKE_READ_UNIQUE:
    case CHI_REQ_WRITE_EVICT_OR_EVICT:
    case CHI_REQ_WRITE_UNIQUE_ZERO:
    case CHI_REQ_WRITE_NO_SNP_ZERO:
    case CHI_REQ_STASH_ONCE_SEP_SHARED:
    case CHI_REQ_STASH_ONCE_SEP_UNIQUE:
    case CHI_REQ_READ_PREFER_UNIQUE:
    case CHI_REQ_CLEAN_INVALID_POPA:
    case CHI_REQ_WRITE_NO_SNP_DEF:
    case CHI_REQ_WRITE_NO_SNP_FULL_CLEAN_SH:
    case CHI_REQ_WRITE_NO_SNP_FULL_CLEAN_INV:
    case CHI_REQ_WRITE_NO_SNP_FULL_CLEAN_SH_PER_SEP:
    case CHI_REQ_WRITE_UNIQUE_FULL_CLEAN_SH:
    case CHI_REQ_WRITE_UNIQUE_FULL_CLEAN_SH_PER_SEP:
    case CHI_REQ_WRITE_BACK_FULL_CLEAN_SH:
    case CHI_REQ_WRITE_BACK_FULL_CLEAN_INV:
    case CHI_REQ_WRITE_BACK_FULL_CLEAN_SH_PER_SEP:
    case CHI_REQ_WRITE_CLEAN_FULL_CLEAN_SH:
    case CHI_REQ_WRITE_CLEAN_FULL_CLEAN_SH_PER_SEP:
    case CHI_REQ_WRITE_NO_SNP_PTL_CLEAN_SH:
    case CHI_REQ_WRITE_NO_SNP_PTL_CLEAN_INV:
    case CHI_REQ_WRITE_NO_SNP_PTL_CLEAN_SH_PER_SEP:
    case CHI_REQ_WRITE_UNIQUE_PTL_CLEAN_SH:
    case CHI_REQ_WRITE_UNIQUE_PTL_CLEAN_SH_PER_SEP:
    case CHI_REQ_WRITE_NO_SNP_PTL_CLEAN_INV_POPA:
    case CHI_REQ_WRITE_NO_SNP_FULL_CLEAN_INV_POPA:
    case CHI_REQ_WRITE_BACK_FULL_CLEAN_INV_POPA:
        return true;
    default:
        return false;
    }
}

bool ChiCommon::isRspOpcode(uint32_t opcode) {
    switch (opcode) {
    case CHI_RSP_LCRD_RETURN:
    case CHI_RSP_SNP_RESP:
    case CHI_RSP_COMP_ACK:
    case CHI_RSP_RETRY_ACK:
    case CHI_RSP_COMP:
    case CHI_RSP_COMP_DBID_RESP:
    case CHI_RSP_DBID_RESP:
    case CHI_RSP_PCRD_GRANT:
    case CHI_RSP_READ_RECEIPT:
    case CHI_RSP_SNP_RESP_FWDED:
    case CHI_RSP_TAG_MATCH:
    case CHI_RSP_RESP_SEP_DATA:
    case CHI_RSP_PERSIST:
    case CHI_RSP_COMP_PERSIST:
    case CHI_RSP_DBID_RESP_ORD:
    case CHI_RSP_STASH_DONE:
    case CHI_RSP_COMP_STASH_DONE:
    case CHI_RSP_COMP_CMO:
        return true;
    default:
        return false;
    }
}

bool ChiCommon::isSnpOpcode(uint32_t opcode) {
    switch (opcode) {
    case CHI_SNP_LCRD_RETURN:
    case CHI_SNP_SHARED:
    case CHI_SNP_CLEAN:
    case CHI_SNP_ONCE:
    case CHI_SNP_NOT_SHARED_DIRTY:
    case CHI_SNP_UNIQUE_STASH:
    case CHI_SNP_MAKE_INVALID_STASH:
    case CHI_SNP_UNIQUE:
    case CHI_SNP_CLEAN_SHARED:
    case CHI_SNP_CLEAN_INVALID:
    case CHI_SNP_MAKE_INVALID:
    case CHI_SNP_STASH_UNIQUE:
    case CHI_SNP_STASH_SHARED:
    case CHI_SNP_DVM_OP:
    case CHI_SNP_QUERY:
    case CHI_SNP_SHARED_FWD:
    case CHI_SNP_CLEAN_FWD:
    case CHI_SNP_ONCE_FWD:
    case CHI_SNP_NOT_SHARED_DIRTY_FWD:
    case CHI_SNP_PREFER_UNIQUE:
    case CHI_SNP_PREFER_UNIQUE_FWD:
    case CHI_SNP_UNIQUE_FWD:
        return true;
    default:
        return false;
    }
}

bool ChiCommon::isDatOpcode(uint32_t opcode) {
    switch (opcode) {
    case CHI_DAT_LCRD_RETURN:
    case CHI_DAT_SNP_RESP_DATA:
    case CHI_DAT_COPY_BACK_WRITE_DATA:
    case CHI_DAT_NON_COPY_BACK_WRITE_DATA:
    case CHI_DAT_COMP_DATA:
    case CHI_DAT_SNP_RESP_DATA_PTL:
    case CHI_DAT_SNP_RESP_DATA_FWDED:
    case CHI_DAT_WRITE_DATA_CANCEL:
    case CHI_DAT_DATA_SEP_RESP:
    case CHI_DAT_NCB_WR_DATA_COMP_ACK:
        return true;
    default:
        return false;
    }
}

template <size_t Size>
void printHexArray(ostream& stream, const array<uint32_t, Size>& values) {
    stream << '[';
    for (size_t index = 0; index < Size; ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << "0x" << hex << values[index];
    }
    stream << ']';
}

ostream& operator<<(ostream& stream, const CHIReqFlit& flit) {
    ios_base::fmtflags previousFlags = stream.flags();
    stream << hex << "CHIReqFlit{qos=0x" << flit.qos << ", tgtId=0x" << flit.tgtId
           << ", srcId=0x" << flit.srcId << ", txnId=0x" << flit.txnId
           << ", returnNid=0x" << flit.returnNid << ", returnTxnId=0x" << flit.returnTxnId
           << ", opcode=0x" << flit.opcode << ", size=0x" << flit.size
           << ", address=0x" << flit.address << ", nonSecure=0x" << flit.nonSecure
           << ", likelyShared=0x" << flit.likelyShared << ", allowRetry=0x" << flit.allowRetry
           << ", order=0x" << flit.order << ", pCrdType=0x" << flit.pCrdType
           << ", memAttr=0x" << flit.memAttr << ", snoopAttr=0x" << flit.snoopAttr
           << ", expectCompAck=0x" << flit.expectCompAck << ", traceTag=0x" << flit.traceTag
           << '}';
    stream.flags(previousFlags);
    return stream;
}

ostream& operator<<(ostream& stream, const CHIRspFlit& flit) {
    ios_base::fmtflags previousFlags = stream.flags();
    stream << hex << "CHIRspFlit{qos=0x" << flit.qos << ", tgtId=0x" << flit.tgtId
           << ", srcId=0x" << flit.srcId << ", txnId=0x" << flit.txnId
           << ", opcode=0x" << flit.opcode << ", respErr=0x" << flit.respErr
           << ", resp=0x" << flit.resp << ", fwdState=0x" << flit.fwdState
           << ", dbid=0x" << flit.dbid << ", pCrdType=0x" << flit.pCrdType
           << ", traceTag=0x" << flit.traceTag << '}';
    stream.flags(previousFlags);
    return stream;
}

ostream& operator<<(ostream& stream, const CHIDatFlit& flit) {
    ios_base::fmtflags previousFlags = stream.flags();
    stream << hex << "CHIDatFlit{qos=0x" << flit.qos << ", tgtId=0x" << flit.tgtId
           << ", srcId=0x" << flit.srcId << ", txnId=0x" << flit.txnId
           << ", homeNid=0x" << flit.homeNid << ", opcode=0x" << flit.opcode
           << ", respErr=0x" << flit.respErr << ", resp=0x" << flit.resp
           << ", dataSource=0x" << flit.dataSource << ", dbid=0x" << flit.dbid
           << ", dataId=0x" << flit.dataId << ", ccId=0x" << flit.ccId
           << ", byteEnable=";
    printHexArray(stream, flit.byteEnable);
    stream << ", data=";
    printHexArray(stream, flit.data);
    stream << ", traceTag=0x" << flit.traceTag << '}';
    stream.flags(previousFlags);
    return stream;
}

ostream& operator<<(ostream& stream, const CHISnpFlit& flit) {
    ios_base::fmtflags previousFlags = stream.flags();
    stream << hex << "CHISnpFlit{qos=0x" << flit.qos << ", srcId=0x" << flit.srcId
           << ", txnId=0x" << flit.txnId << ", fwdNid=0x" << flit.fwdNid
           << ", fwdTxnId=0x" << flit.fwdTxnId << ", opcode=0x" << flit.opcode
           << ", address=0x" << flit.address << ", nonSecure=0x" << flit.nonSecure
           << ", doNotGoToSharedDirty=0x" << flit.doNotGoToSharedDirty
           << ", returnToSource=0x" << flit.returnToSource
           << ", traceTag=0x" << flit.traceTag << '}';
    stream.flags(previousFlags);
    return stream;
}

string CHIReqFlit::toString() const {
    ostringstream stream;
    stream << *this;
    return stream.str();
}

string CHIRspFlit::toString() const {
    ostringstream stream;
    stream << *this;
    return stream.str();
}

string CHIDatFlit::toString() const {
    ostringstream stream;
    stream << *this;
    return stream.str();
}

string CHISnpFlit::toString() const {
    ostringstream stream;
    stream << *this;
    return stream.str();
}

}  // namespace toc::chi
