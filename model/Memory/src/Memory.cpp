#include "Memory.h"

#include <stdexcept>
#include <utility>

namespace toc::memory {

using namespace std;

Memory::Memory(string moduleName,
               const soc::EnvCfg& envCfg,
               uint32_t nodeId)
    : BaseModule(move(moduleName), envCfg), nodeId(nodeId) {}

void Memory::work() {
    nextState = state;
    nextReqCreditOutstanding = reqCreditOutstanding;
    nextPendingRequest = pendingRequest;

    if (state == State::idle) {
        if (!reqCreditOutstanding && chiPort.sendReqCredit()) {
            nextReqCreditOutstanding = true;
        }

        if (chiPort.hasReqFlit()) {
            chi::CHIReqFlit request;
            if (!chiPort.getReqFlit(request)) {
                throw runtime_error("Memory failed to consume its visible REQ flit");
            }
            if (request.opcode != CHI_REQ_READ_NO_SNP || request.tgtId != nodeId) {
                throw runtime_error("Memory received an unsupported REQ flit");
            }
            validateLineAddress(request.address);
            nextPendingRequest = request;
            nextReqCreditOutstanding = false;
            nextState = State::responsePending;
        }
    } else if (state == State::responsePending && chiPort.hasDatCredit()) {
        chi::CHIDatFlit data;
        data.tgtId = pendingRequest.srcId;
        data.srcId = nodeId;
        data.txnId = pendingRequest.txnId;
        data.homeNid = nodeId;
        data.opcode = CHI_DAT_COMP_DATA;
        data.data = readLine(pendingRequest.address);
        data.byteEnable[0] = 0xFFFFFFFF;
        data.byteEnable[1] = 0xFFFFFFFF;
        if (!chiPort.sendDatFlit(data)) {
            throw runtime_error("Memory failed to send DAT flit with an available credit");
        }
        nextState = State::idle;
    }

    chiPort.work();
}

void Memory::xfer() {
    chiPort.xfer();
    state = nextState;
    reqCreditOutstanding = nextReqCreditOutstanding;
    pendingRequest = nextPendingRequest;
}

chi::CHIPort& Memory::getChiPort() {
    return chiPort;
}

void Memory::preloadLine(uint64_t address, const MemoryLine& line) {
    validateLineAddress(address);
    lines[lineAddress(address)] = line;
}

MemoryLine Memory::readLine(uint64_t address) const {
    validateLineAddress(address);
    auto entry = lines.find(lineAddress(address));
    if (entry == lines.end()) {
        MemoryLine emptyLine = {0};
        return emptyLine;
    }
    return entry->second;
}

uint64_t Memory::lineAddress(uint64_t address) const {
    return address / chi::ChiCommon::cacheLineBytes;
}

void Memory::validateLineAddress(uint64_t address) const {
    if (address % chi::ChiCommon::cacheLineBytes != 0) {
        throw invalid_argument("Memory line address must be 64-byte aligned");
    }
}

}  // namespace toc::memory
