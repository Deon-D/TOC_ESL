#include "SoC.h"

#include <stdexcept>

namespace toc::soc {

using namespace std;

SoC::SoC()
    : rnModel("rn", envCfg, 0x01, 0x02),
      memoryModel("memory", envCfg, 0x02),
      topModules{&rnModel, &memoryModel} {
    if (!rnModel.getChiPort().connect(memoryModel.getChiPort())) {
        throw runtime_error("SoC failed to connect RN and Memory CHIPorts");
    }
}

void SoC::step() {
    for (auto* module : topModules) {
        if (!module->isClockActive()) {
            throw runtime_error("SoC top-level module missed a reference clock edge");
        }
        module->work();
    }
    for (auto* module : topModules) {
        module->xfer();
    }
    envCfg.advanceCycle();
}

bool SoC::runUntilReadComplete(uint64_t maxCycles) {
    if (rnModel.isReadComplete()) {
        return true;
    }
    for (uint64_t cycleCount = 0; cycleCount < maxCycles; ++cycleCount) {
        step();
        if (rnModel.isReadComplete()) {
            return true;
        }
    }
    return false;
}

uint64_t SoC::getCycle() const {
    return envCfg.getCycle();
}

bool SoC::issueRead(uint64_t address, uint32_t txnId) {
    return rnModel.issueRead(address, txnId);
}

void SoC::preloadMemory(uint64_t address, const memory::MemoryLine& line) {
    memoryModel.preloadLine(address, line);
}

const chi::CHIDatFlit& SoC::getReadData() const {
    return rnModel.getReadData();
}

const rn::RN& SoC::getRn() const {
    return rnModel;
}

const memory::Memory& SoC::getMemory() const {
    return memoryModel;
}

}  // namespace toc::soc
