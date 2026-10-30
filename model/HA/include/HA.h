#pragma once

#include "BaseModule.h"
#include "CHIPort.h"

#include <cstdint>
#include <string>

namespace toc::ha {

using namespace std;

class HA : public common::BaseModule {
public:
    HA(string moduleName,
       const soc::EnvCfg& envCfg,
       uint32_t nodeId,
       uint32_t memoryNodeId);

    void work() override;
    void xfer() override;

    chi::CHIPort& getRnPort();
    chi::CHIPort& getMemoryPort();

private:
    enum class State {
        idle,
        waitMemoryReqCredit,
        waitMemoryData,
        waitRnDatCredit
    };

    chi::CHIPort rnPort;
    chi::CHIPort memoryPort;
    uint32_t nodeId = 0;
    uint32_t memoryNodeId = 0;
    State state = State::idle;
    State nextState = State::idle;
    bool rnReqCreditOutstanding = false;
    bool nextRnReqCreditOutstanding = false;
    bool memoryDatCreditSent = false;
    bool nextMemoryDatCreditSent = false;
    chi::CHIReqFlit pendingRnRequest;
    chi::CHIReqFlit nextPendingRnRequest;
    chi::CHIDatFlit pendingMemoryData;
    chi::CHIDatFlit nextPendingMemoryData;
};

}  // namespace toc::ha
