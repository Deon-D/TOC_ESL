#include "SoC.h"

#include <iostream>

using namespace std;

int main() {
    toc::memory::MemoryLine line = {0};
    line[0] = 0x12345678;
    line[1] = 0x9ABCDEF0;

    toc::soc::SoC soc;
    soc.preloadMemory(0x1000, line);
    if (!soc.issueRead(0x1000, 0x2A)) {
        cerr << "failed to issue RN read" << endl;
        return 1;
    }
    if (!soc.runUntilReadComplete(16)) {
        cerr << "RN read did not complete" << endl;
        return 1;
    }

    const auto& data = soc.getReadData();
    cout << "cycle=" << soc.getCycle()
         << " txnId=0x" << hex << data.txnId
         << " data[0]=0x" << data.data[0]
         << " data[1]=0x" << data.data[1] << endl;
    return 0;
}
