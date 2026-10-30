#include "BaseModule.h"

#include <stdexcept>
#include <utility>

namespace toc::common {

using namespace std;

BaseModule::BaseModule(string moduleName,
                       const soc::EnvCfg& envCfg,
                       uint32_t clockDivider,
                       uint32_t clockPhase)
    : name(move(moduleName)),
      envCfg(envCfg),
      clockDivider(clockDivider),
      clockPhase(clockPhase) {
    if (clockDivider == 0) {
        throw invalid_argument("module clock divider must be greater than zero");
    }
    if (clockPhase >= clockDivider) {
        throw invalid_argument("module clock phase must be less than clock divider");
    }
}

BaseModule::~BaseModule() = default;

const string& BaseModule::getName() const {
    return name;
}

bool BaseModule::isClockActive() const {
    return envCfg.getCycle() % clockDivider == clockPhase;
}

uint64_t BaseModule::getCycle() const {
    return envCfg.getCycle();
}

uint32_t BaseModule::getClockDivider() const {
    return clockDivider;
}

uint32_t BaseModule::getClockPhase() const {
    return clockPhase;
}

}  // namespace toc::common
