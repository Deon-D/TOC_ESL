#pragma once

#include "BaseModule.h"
#include "CHIPort.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace toc::memory {

using namespace std;

using MemoryLine = array<uint32_t, chi::ChiCommon::dataWordCount>;

class Memory : public common::BaseModule {
public:
    Memory(string moduleName,
           const soc::EnvCfg& envCfg,
           uint32_t nodeId);

    void work() override;
    void xfer() override;

    chi::CHIPort& getChiPort();

    void preloadLine(uint64_t address, const MemoryLine& line);
    MemoryLine readLine(uint64_t address) const;

private:
    enum class State {
        idle,
        responsePending
    };

    uint64_t lineAddress(uint64_t address) const;
    void validateLineAddress(uint64_t address) const;

    chi::CHIPort chiPort;
    uint32_t nodeId = 0;
    State state = State::idle;
    State nextState = State::idle;
    bool reqCreditOutstanding = false;
    bool nextReqCreditOutstanding = false;
    chi::CHIReqFlit pendingRequest;
    chi::CHIReqFlit nextPendingRequest;
    unordered_map<uint64_t, MemoryLine> lines;
};

}  // namespace toc::memory
