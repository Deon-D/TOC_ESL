#include "HA.h"

#include <stdexcept>
#include <utility>

namespace toc::ha {

using namespace std;

HA::HA(string moduleName,
       const soc::EnvCfg& envCfg,
       uint32_t nodeId,
       uint32_t memoryNodeId)
    : BaseModule(move(moduleName), envCfg),
      nodeId(nodeId),
      memoryNodeId(memoryNodeId) {}

void HA::work() {
    nextState = state;
    nextRnReqCreditOutstanding = rnReqCreditOutstanding;
    nextMemoryDatCreditSent = memoryDatCreditSent;
    nextPendingRnRequest = pendingRnRequest;
    nextPendingMemoryData = pendingMemoryData;

    if (state == State::idle) {
        if (!rnReqCreditOutstanding && rnPort.sendReqCredit()) {
            nextRnReqCreditOutstanding = true;
        }

        if (rnPort.hasReqFlit()) {
            chi::CHIReqFlit request;
            if (!rnPort.getReqFlit(request)) {
                throw runtime_error("HA failed to consume its visible RN REQ flit");
            }
            if (request.opcode != CHI_REQ_READ_NO_SNP || request.tgtId != nodeId) {
                throw runtime_error("HA received an unsupported RN REQ flit");
            }
            nextPendingRnRequest = request;
            nextRnReqCreditOutstanding = false;
            nextMemoryDatCreditSent = false;
            nextState = State::waitMemoryReqCredit;
        }
    } else if (state == State::waitMemoryReqCredit) {
        if (!memoryDatCreditSent && memoryPort.sendDatCredit()) {
            nextMemoryDatCreditSent = true;
        }

        if (memoryPort.hasReqCredit()) {
            chi::CHIReqFlit request = pendingRnRequest;
            request.tgtId = memoryNodeId;
            request.srcId = nodeId;
            if (!memoryPort.sendReqFlit(request)) {
                throw runtime_error("HA failed to forward REQ flit with an available credit");
            }
            nextState = State::waitMemoryData;
        }
    } else if (state == State::waitMemoryData && memoryPort.hasDatFlit()) {
        chi::CHIDatFlit data;
        if (!memoryPort.getDatFlit(data)) {
            throw runtime_error("HA failed to consume its visible Memory DAT flit");
        }
        if (data.opcode != CHI_DAT_COMP_DATA ||
            data.tgtId != nodeId ||
            data.srcId != memoryNodeId ||
            data.txnId != pendingRnRequest.txnId) {
            throw runtime_error("HA received a DAT flit that does not match its Memory request");
        }
        nextPendingMemoryData = data;
        nextState = State::waitRnDatCredit;
    } else if (state == State::waitRnDatCredit && rnPort.hasDatCredit()) {
        chi::CHIDatFlit data = pendingMemoryData;
        data.tgtId = pendingRnRequest.srcId;
        data.srcId = nodeId;
        data.homeNid = nodeId;
        if (!rnPort.sendDatFlit(data)) {
            throw runtime_error("HA failed to return DAT flit with an available credit");
        }
        nextMemoryDatCreditSent = false;
        nextState = State::idle;
    }

    rnPort.work();
    memoryPort.work();
}

void HA::xfer() {
    rnPort.xfer();
    memoryPort.xfer();
    state = nextState;
    rnReqCreditOutstanding = nextRnReqCreditOutstanding;
    memoryDatCreditSent = nextMemoryDatCreditSent;
    pendingRnRequest = nextPendingRnRequest;
    pendingMemoryData = nextPendingMemoryData;
}

chi::CHIPort& HA::getRnPort() {
    return rnPort;
}

chi::CHIPort& HA::getMemoryPort() {
    return memoryPort;
}

}  // namespace toc::ha
