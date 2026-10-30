#include "RN.h"

#include <stdexcept>
#include <utility>

namespace toc::rn {

using namespace std;

RN::RN(string moduleName,
       const soc::EnvCfg& envCfg,
       uint32_t nodeId,
       uint32_t homeNodeId)
    : BaseModule(move(moduleName), envCfg),
      nodeId(nodeId),
      homeNodeId(homeNodeId) {}

void RN::work() {
    nextState = state;
    nextDatCreditSent = datCreditSent;
    nextReadData = readData;

    if (state == State::waitReqCredit) {
        if (!datCreditSent && chiPort.sendDatCredit()) {
            nextDatCreditSent = true;
        }

        if (chiPort.hasReqCredit()) {
            chi::CHIReqFlit request;
            request.tgtId = homeNodeId;
            request.srcId = nodeId;
            request.txnId = readTxnId;
            request.opcode = CHI_REQ_READ_NO_SNP;
            request.size = 0x6;
            request.address = readAddress;
            if (!chiPort.sendReqFlit(request)) {
                throw runtime_error("RN failed to send REQ flit with an available credit");
            }
            nextState = State::waitData;
        }
    } else if (state == State::waitData && chiPort.hasDatFlit()) {
        chi::CHIDatFlit data;
        if (!chiPort.getDatFlit(data)) {
            throw runtime_error("RN failed to consume its visible DAT flit");
        }
        if (data.opcode != CHI_DAT_COMP_DATA ||
            data.tgtId != nodeId ||
            data.srcId != homeNodeId ||
            data.homeNid != homeNodeId ||
            data.txnId != readTxnId) {
            throw runtime_error("RN received a DAT flit that does not match its read request");
        }
        nextReadData = data;
        nextState = State::complete;
    }

    chiPort.work();
}

void RN::xfer() {
    chiPort.xfer();
    state = nextState;
    datCreditSent = nextDatCreditSent;
    readData = nextReadData;
}

chi::CHIPort& RN::getChiPort() {
    return chiPort;
}

bool RN::issueRead(uint64_t address, uint32_t txnId) {
    if (state != State::idle || address % chi::ChiCommon::cacheLineBytes != 0) {
        return false;
    }

    readAddress = address;
    readTxnId = txnId;
    datCreditSent = false;
    nextDatCreditSent = false;
    state = State::waitReqCredit;
    nextState = State::waitReqCredit;
    return true;
}

bool RN::isReadComplete() const {
    return state == State::complete;
}

const chi::CHIDatFlit& RN::getReadData() const {
    if (!isReadComplete()) {
        throw logic_error("RN read data requested before completion");
    }
    return readData;
}

}  // namespace toc::rn
