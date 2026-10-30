#include "BaseModule.h"

#include <stdexcept>
#include <string>
#include <type_traits>

namespace toc::baseModuleTest {

using namespace std;

class TestModule : public common::BaseModule {
public:
    TestModule(string moduleName,
               const soc::EnvCfg& envCfg,
               uint32_t clockDivider = 1,
               uint32_t clockPhase = 0)
        : BaseModule(moduleName, envCfg, clockDivider, clockPhase) {}

    void work() override {
        workCalled = true;
    }

    void xfer() override {
        xferCalled = true;
    }

    bool workCalled = false;
    bool xferCalled = false;
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

void verifyBaseModuleContract() {
    static_assert(is_abstract_v<common::BaseModule>);
    static_assert(!is_default_constructible_v<TestModule>);

    soc::EnvCfg envCfg;
    TestModule module("testModule", envCfg);
    common::BaseModule& baseModule = module;

    require(baseModule.getName() == "testModule", "module name changed during construction");
    require(baseModule.getCycle() == 0, "module did not observe the initial environment cycle");
    require(baseModule.isClockActive(), "divider-one module was not active on cycle zero");
    baseModule.work();
    baseModule.xfer();
    require(module.workCalled, "work did not dispatch to the derived module");
    require(module.xferCalled, "xfer did not dispatch to the derived module");

    envCfg.advanceCycle();
    require(baseModule.getCycle() == 1, "module did not observe the advanced environment cycle");
}

void verifyClockConfiguration() {
    soc::EnvCfg envCfg;
    TestModule dividedModule("dividedModule", envCfg, 3, 1);

    require(dividedModule.getClockDivider() == 3, "module clock divider changed");
    require(dividedModule.getClockPhase() == 1, "module clock phase changed");
    require(!dividedModule.isClockActive(), "divided module was active on the wrong phase");
    envCfg.advanceCycle();
    require(dividedModule.isClockActive(), "divided module missed its configured phase");
    envCfg.advanceCycle();
    require(!dividedModule.isClockActive(), "divided module remained active after its edge");
}

void verifyInvalidClockConfiguration() {
    soc::EnvCfg envCfg;
    bool zeroDividerRejected = false;
    try {
        TestModule invalidDivider("invalidDivider", envCfg, 0, 0);
        (void)invalidDivider;
    } catch (const invalid_argument&) {
        zeroDividerRejected = true;
    }
    require(zeroDividerRejected, "zero clock divider was accepted");

    bool invalidPhaseRejected = false;
    try {
        TestModule invalidPhase("invalidPhase", envCfg, 2, 2);
        (void)invalidPhase;
    } catch (const invalid_argument&) {
        invalidPhaseRejected = true;
    }
    require(invalidPhaseRejected, "clock phase outside the divider was accepted");
}

}  // namespace toc::baseModuleTest

int main() {
    toc::baseModuleTest::verifyBaseModuleContract();
    toc::baseModuleTest::verifyClockConfiguration();
    toc::baseModuleTest::verifyInvalidClockConfiguration();
    return 0;
}
