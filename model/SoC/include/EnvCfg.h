#pragma once

#include <cstdint>

namespace toc::soc {

class EnvCfg {
public:
    uint64_t getCycle() const {
        return cycle;
    }

    void advanceCycle() {
        ++cycle;
    }

private:
    uint64_t cycle = 0;
};

}  // namespace toc::soc
