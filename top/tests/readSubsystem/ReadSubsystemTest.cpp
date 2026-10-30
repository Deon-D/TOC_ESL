#include "SoC.h"

#include <cstddef>
#include <stdexcept>

namespace toc::readSubsystemTest {

using namespace std;

void require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

void verifyPreloadedRead() {
    memory::MemoryLine expectedLine = {0};
    for (size_t index = 0; index < expectedLine.size(); ++index) {
        expectedLine[index] = 0x1000 + static_cast<uint32_t>(index);
    }

    soc::SoC soc;
    soc.preloadMemory(0x1000, expectedLine);
    require(soc.issueRead(0x1000, 0x35), "RN rejected an aligned read request");
    require(soc.runUntilReadComplete(16), "RN read did not complete within 16 cycles");

    const auto& data = soc.getReadData();
    require(soc.getCycle() == 5, "minimal read completed on an unexpected cycle");
    require(data.opcode == CHI_DAT_COMP_DATA, "Memory returned the wrong DAT opcode");
    require(data.srcId == 0x02 && data.tgtId == 0x01,
            "Memory returned incorrect source or target node IDs");
    require(data.txnId == 0x35, "Memory did not preserve the request transaction ID");
    require(data.data == expectedLine, "Memory returned incorrect cache-line data");
    require(data.byteEnable[0] == 0xFFFFFFFF && data.byteEnable[1] == 0xFFFFFFFF,
            "Memory did not mark the full cache line valid");
}

void verifyUninitializedReadReturnsZero() {
    soc::SoC soc;
    require(soc.issueRead(0x2000, 0x44), "RN rejected an uninitialized aligned read");
    require(soc.runUntilReadComplete(16), "zero-filled read did not complete");

    memory::MemoryLine emptyLine = {0};
    require(soc.getReadData().data == emptyLine,
            "uninitialized memory did not return a zero-filled cache line");
}

void verifyAlignmentChecks() {
    soc::SoC soc;
    require(!soc.issueRead(0x1004, 0x01), "RN accepted an unaligned cache-line read");

    memory::MemoryLine line = {0};
    bool preloadRejected = false;
    try {
        soc.preloadMemory(0x1004, line);
    } catch (const invalid_argument&) {
        preloadRejected = true;
    }
    require(preloadRejected, "Memory accepted an unaligned preload address");
}

}  // namespace toc::readSubsystemTest

int main() {
    toc::readSubsystemTest::verifyPreloadedRead();
    toc::readSubsystemTest::verifyUninitializedReadReturnsZero();
    toc::readSubsystemTest::verifyAlignmentChecks();
    return 0;
}
