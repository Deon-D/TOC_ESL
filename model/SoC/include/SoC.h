#pragma once

#include "BaseModule.h"
#include "EnvCfg.h"
#include "HA.h"
#include "Memory.h"
#include "RN.h"

#include <array>
#include <cstdint>

namespace toc::soc {

using namespace std;

class SoC {
public:
    SoC();

    void step();
    bool runUntilReadComplete(uint64_t maxCycles);

    uint64_t getCycle() const;
    bool issueRead(uint64_t address, uint32_t txnId);
    void preloadMemory(uint64_t address, const memory::MemoryLine& line);
    const chi::CHIDatFlit& getReadData() const;

    const rn::RN& getRn() const;
    const ha::HA& getHa() const;
    const memory::Memory& getMemory() const;

private:
    EnvCfg envCfg;
    rn::RN rnModel;
    ha::HA haModel;
    memory::Memory memoryModel;
    array<common::BaseModule*, 3> topModules;
};

}  // namespace toc::soc
