#include "BaseModule.h"
#include "CHIPort.h"
#include "EnvCfg.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace toc::socClockTest {

using namespace std;

void require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

class HiddenModule : public common::BaseModule {
public:
    HiddenModule(string moduleName,
                 const soc::EnvCfg& envCfg,
                 uint32_t clockDivider,
                 uint32_t clockPhase)
        : BaseModule(moduleName, envCfg, clockDivider, clockPhase) {}

    void work() override {
        ++workCount;
    }

    void xfer() override {
        ++xferCount;
    }

    uint32_t getWorkCount() const {
        return workCount;
    }

    uint32_t getXferCount() const {
        return xferCount;
    }

private:
    uint32_t workCount = 0;
    uint32_t xferCount = 0;
};

class SourceModule : public common::BaseModule {
public:
    explicit SourceModule(const soc::EnvCfg& envCfg)
        : BaseModule("sourceModule", envCfg),
          hiddenModule("sourceHidden", envCfg, 2, 0) {}

    void work() override {
        lastWorkCycle = getCycle();
        if (hiddenModule.isClockActive()) {
            hiddenModule.work();
        }

        if (!requestSent && chiPort.hasReqCredit()) {
            chi::CHIReqFlit request;
            request.txnId = 0x2A;
            request.opcode = CHI_REQ_READ_SHARED;
            request.address = 0x1000;
            if (!chiPort.sendReqFlit(request)) {
                throw runtime_error("source failed to send REQ flit with an available credit");
            }
            requestSent = true;
        }
        chiPort.work();
    }

    void xfer() override {
        chiPort.xfer();
        if (hiddenModule.isClockActive()) {
            hiddenModule.xfer();
        }
    }

    chi::CHIPort& getChiPort() {
        return chiPort;
    }

    uint32_t getHiddenWorkCount() const {
        return hiddenModule.getWorkCount();
    }

    uint32_t getHiddenXferCount() const {
        return hiddenModule.getXferCount();
    }

    uint64_t getLastWorkCycle() const {
        return lastWorkCycle;
    }

private:
    chi::CHIPort chiPort;
    HiddenModule hiddenModule;
    bool requestSent = false;
    uint64_t lastWorkCycle = 0;
};

class SinkModule : public common::BaseModule {
public:
    explicit SinkModule(const soc::EnvCfg& envCfg)
        : BaseModule("sinkModule", envCfg),
          hiddenModule("sinkHidden", envCfg, 3, 1) {}

    void work() override {
        lastWorkCycle = getCycle();
        if (hiddenModule.isClockActive()) {
            hiddenModule.work();
        }

        if (chiPort.hasReqFlit()) {
            chi::CHIReqFlit request;
            if (!chiPort.getReqFlit(request)) {
                throw runtime_error("sink failed to consume its visible REQ flit");
            }
            receivedTxnId = request.txnId;
            receiveCycle = getCycle();
            requestReceived = true;
        }

        if (!creditSent) {
            if (!chiPort.sendReqCredit()) {
                throw runtime_error("sink failed to send its initial REQ credit");
            }
            creditSent = true;
        }
        chiPort.work();
    }

    void xfer() override {
        chiPort.xfer();
        if (hiddenModule.isClockActive()) {
            hiddenModule.xfer();
        }
    }

    chi::CHIPort& getChiPort() {
        return chiPort;
    }

    bool hasReceivedRequest() const {
        return requestReceived;
    }

    uint32_t getReceivedTxnId() const {
        return receivedTxnId;
    }

    uint64_t getReceiveCycle() const {
        return receiveCycle;
    }

    uint32_t getHiddenWorkCount() const {
        return hiddenModule.getWorkCount();
    }

    uint32_t getHiddenXferCount() const {
        return hiddenModule.getXferCount();
    }

    uint64_t getLastWorkCycle() const {
        return lastWorkCycle;
    }

private:
    chi::CHIPort chiPort;
    HiddenModule hiddenModule;
    bool creditSent = false;
    bool requestReceived = false;
    uint32_t receivedTxnId = 0;
    uint64_t receiveCycle = 0;
    uint64_t lastWorkCycle = 0;
};

class TestSoC {
public:
    explicit TestSoC(bool reverseModuleOrder)
        : sourceModule(envCfg),
          sinkModule(envCfg),
          topModules{&sourceModule, &sinkModule} {
        if (reverseModuleOrder) {
            swap(topModules[0], topModules[1]);
        }
        if (!sourceModule.getChiPort().connect(sinkModule.getChiPort())) {
            throw runtime_error("SoC failed to connect module CHIPorts");
        }
    }

    void step() {
        for (auto* module : topModules) {
            if (!module->isClockActive()) {
                throw runtime_error("top-level module is not active on every reference cycle");
            }
            module->work();
        }
        for (auto* module : topModules) {
            module->xfer();
        }
        envCfg.advanceCycle();
    }

    void run(uint64_t cycleCount) {
        for (uint64_t index = 0; index < cycleCount; ++index) {
            step();
        }
    }

    uint64_t getCycle() const {
        return envCfg.getCycle();
    }

    const SourceModule& getSourceModule() const {
        return sourceModule;
    }

    const SinkModule& getSinkModule() const {
        return sinkModule;
    }

private:
    soc::EnvCfg envCfg;
    SourceModule sourceModule;
    SinkModule sinkModule;
    array<common::BaseModule*, 2> topModules;
};

void verifyReferenceClockAndHiddenModules() {
    TestSoC testSoC(false);
    require(testSoC.getCycle() == 0, "SoC reference cycle did not start at zero");

    testSoC.run(3);

    require(testSoC.getCycle() == 3, "SoC reference cycle advanced by the wrong amount");
    require(testSoC.getSourceModule().getCycle() == testSoC.getCycle(),
            "source module did not share the SoC EnvCfg");
    require(testSoC.getSinkModule().getCycle() == testSoC.getCycle(),
            "sink module did not share the SoC EnvCfg");
    require(testSoC.getSourceModule().getLastWorkCycle() == 2 &&
                testSoC.getSinkModule().getLastWorkCycle() == 2,
            "top-level modules observed different reference cycles");

    require(testSoC.getSourceModule().getHiddenWorkCount() == 2 &&
                testSoC.getSourceModule().getHiddenXferCount() == 2,
            "divider-two hidden module ran on the wrong cycles");
    require(testSoC.getSinkModule().getHiddenWorkCount() == 1 &&
                testSoC.getSinkModule().getHiddenXferCount() == 1,
            "phase-one divider-three hidden module ran on the wrong cycles");
}

void verifyPortTransferAndModuleOrder() {
    TestSoC forwardOrder(false);
    TestSoC reverseOrder(true);

    forwardOrder.run(3);
    reverseOrder.run(3);

    require(forwardOrder.getSinkModule().hasReceivedRequest(),
            "forward-order SoC did not deliver the REQ flit");
    require(reverseOrder.getSinkModule().hasReceivedRequest(),
            "reverse-order SoC did not deliver the REQ flit");
    require(forwardOrder.getSinkModule().getReceiveCycle() == 2 &&
                reverseOrder.getSinkModule().getReceiveCycle() == 2,
            "module order changed the REQ visibility cycle");
    require(forwardOrder.getSinkModule().getReceivedTxnId() == 0x2A &&
                reverseOrder.getSinkModule().getReceivedTxnId() == 0x2A,
            "module order changed the REQ payload");
}

}  // namespace toc::socClockTest

int main() {
    toc::socClockTest::verifyReferenceClockAndHiddenModules();
    toc::socClockTest::verifyPortTransferAndModuleOrder();
    return 0;
}
