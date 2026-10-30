#pragma once

#include "BaseModule.h"
#include "CHIPort.h"

#include <cstdint>
#include <string>

namespace toc::rn {

using namespace std;

class RN : public common::BaseModule {
public:
    RN(string moduleName,
       const soc::EnvCfg& envCfg,
       uint32_t nodeId,
       uint32_t memoryNodeId);

    void work() override;
    void xfer() override;

    chi::CHIPort& getChiPort();

    bool issueRead(uint64_t address, uint32_t txnId);
    bool isReadComplete() const;
    const chi::CHIDatFlit& getReadData() const;

private:
    enum class State {
        idle,
        waitReqCredit,
        waitData,
        complete
    };

    chi::CHIPort chiPort;
    uint32_t nodeId = 0;
    uint32_t memoryNodeId = 0;
    State state = State::idle;
    State nextState = State::idle;
    uint64_t readAddress = 0;
    uint32_t readTxnId = 0;
    bool datCreditSent = false;
    bool nextDatCreditSent = false;
    chi::CHIDatFlit readData;
    chi::CHIDatFlit nextReadData;
};

}  // namespace toc::rn
