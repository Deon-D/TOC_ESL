#pragma once

#include "EnvCfg.h"

#include <cstdint>
#include <string>

namespace toc::common {

using namespace std;

class BaseModule {
public:
    BaseModule(string moduleName,
               const soc::EnvCfg& envCfg,
               uint32_t clockDivider = 1,
               uint32_t clockPhase = 0);
    virtual ~BaseModule();

    BaseModule(const BaseModule&) = delete;
    BaseModule& operator=(const BaseModule&) = delete;
    BaseModule(BaseModule&&) = delete;
    BaseModule& operator=(BaseModule&&) = delete;

    virtual void work() = 0;
    virtual void xfer() = 0;

    const string& getName() const;
    bool isClockActive() const;
    uint64_t getCycle() const;
    uint32_t getClockDivider() const;
    uint32_t getClockPhase() const;

private:
    string name;
    const soc::EnvCfg& envCfg;
    uint32_t clockDivider = 1;
    uint32_t clockPhase = 0;
};

}  // namespace toc::common
