#include "CHIPort.h"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

namespace toc::chiPortTest {

using namespace std;

class TestPort : public chi::CHIPort {
public:
    using chi::CHIPort::CHIPort;
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw runtime_error(message);
    }
}

void advancePorts(chi::CHIPort& firstPort, chi::CHIPort& secondPort) {
    firstPort.work();
    secondPort.work();
    firstPort.xfer();
    secondPort.xfer();
}

void verifyOpcodeEncoding() {
    static_assert(CHI_REQ_READ_SHARED == 0x01);
    static_assert(CHI_REQ_WRITE_BACK_FULL == 0x1B);
    static_assert(CHI_REQ_MAKE_READ_UNIQUE == 0x41);
    static_assert(CHI_RSP_COMP == 0x04);
    static_assert(CHI_SNP_UNIQUE_FWD == 0x17);
    static_assert(CHI_DAT_COMP_DATA == 0x04);

    require(chi::ChiCommon::isReqOpcode(CHI_REQ_READ_SHARED), "valid REQ opcode rejected");
    require(chi::ChiCommon::isRspOpcode(CHI_RSP_COMP), "valid RSP opcode rejected");
    require(chi::ChiCommon::isSnpOpcode(CHI_SNP_UNIQUE), "valid SNP opcode rejected");
    require(chi::ChiCommon::isDatOpcode(CHI_DAT_COMP_DATA), "valid DAT opcode rejected");
    require(!chi::ChiCommon::isReqOpcode(0x06), "reserved REQ opcode accepted");
    require(!chi::ChiCommon::isRspOpcode(0x0F), "reserved RSP opcode accepted");
}

void verifyHaL3Connection() {
    chi::CHIPort haPort;
    chi::CHIPort l3Port;

    require(haPort.connect(l3Port), "HA and L3 ports did not connect");
    require(haPort.peerPort() == &l3Port, "HA did not retain the L3 port pointer");
    require(l3Port.peerPort() == &haPort, "L3 did not retain the HA port pointer");
    require(!haPort.hasReqCredit(), "HA reported a REQ credit before receiving one");
    require(!l3Port.hasReqFlit(), "L3 reported a REQ flit before receiving one");

    require(l3Port.sendReqCredit(), "L3 failed to provide a REQ credit");
    require(!haPort.hasReqCredit(), "REQ credit became visible before xfer");
    advancePorts(haPort, l3Port);
    require(haPort.getReqCredit() == 1, "HA did not receive the REQ credit");
    require(haPort.hasReqCredit(), "HA did not report its available REQ credit");

    chi::CHIReqFlit request;
    request.srcId = 1;
    request.tgtId = 2;
    request.txnId = 7;
    request.opcode = CHI_REQ_READ_SHARED;
    request.address = 0x1000;
    require(haPort.sendReqFlit(request), "HA failed to send a REQ flit");
    require(haPort.getReqCredit() == 0, "REQ send did not consume its credit");
    require(!haPort.hasReqCredit(), "HA reported a consumed REQ credit");
    require(!l3Port.hasReqFlit(), "REQ flit became visible before xfer");
    advancePorts(haPort, l3Port);
    require(l3Port.hasReqFlit(), "L3 did not report its pending REQ flit");
    require(!haPort.sendReqFlit(request), "REQ send succeeded without credit");

    chi::CHIReqFlit receivedRequest;
    require(l3Port.getReqFlit(receivedRequest), "L3 failed to get the REQ flit");
    require(receivedRequest.txnId == 7 && receivedRequest.address == 0x1000,
            "REQ flit payload changed in transit");
    require(!l3Port.hasReqFlit(), "L3 reported a consumed REQ flit");
    require(!l3Port.getReqFlit(receivedRequest), "L3 received the same REQ flit twice");
    require(l3Port.sendReqCredit(), "L3 failed to return the REQ credit");
    advancePorts(haPort, l3Port);

    require(haPort.sendRspCredit(), "HA failed to provide an RSP credit");
    require(!l3Port.hasRspCredit(), "RSP credit became visible before xfer");
    advancePorts(haPort, l3Port);
    require(l3Port.hasRspCredit(), "L3 did not report its available RSP credit");
    chi::CHIRspFlit response;
    response.srcId = 2;
    response.tgtId = 1;
    response.txnId = 7;
    response.opcode = CHI_RSP_COMP;
    require(l3Port.sendRspFlit(response), "L3 failed to send an RSP flit");
    require(!haPort.hasRspFlit(), "RSP flit became visible before xfer");
    advancePorts(haPort, l3Port);
    require(haPort.hasRspFlit(), "HA did not report its pending RSP flit");
    chi::CHIRspFlit receivedResponse;
    require(haPort.getRspFlit(receivedResponse), "HA failed to get the RSP flit");
    require(!haPort.hasRspFlit(), "HA reported a consumed RSP flit");
    require(receivedResponse.opcode == CHI_RSP_COMP && receivedResponse.txnId == 7,
            "RSP flit payload changed in transit");

    require(l3Port.sendSnpCredit(), "L3 failed to provide an SNP credit");
    advancePorts(haPort, l3Port);
    require(haPort.hasSnpCredit(), "HA did not report its available SNP credit");
    chi::CHISnpFlit snoop;
    snoop.txnId = 9;
    snoop.opcode = CHI_SNP_SHARED;
    snoop.address = 0x2000;
    require(haPort.sendSnpFlit(snoop), "HA failed to send an SNP flit");
    advancePorts(haPort, l3Port);
    require(l3Port.hasSnpFlit(), "L3 did not report its pending SNP flit");
    chi::CHISnpFlit receivedSnoop;
    require(l3Port.getSnpFlit(receivedSnoop), "L3 failed to get the SNP flit");
    require(!l3Port.hasSnpFlit(), "L3 reported a consumed SNP flit");
    require(receivedSnoop.opcode == CHI_SNP_SHARED, "SNP flit payload changed in transit");

    require(haPort.sendDatCredit(), "HA failed to provide a DAT credit");
    advancePorts(haPort, l3Port);
    require(l3Port.hasDatCredit(), "L3 did not report its available DAT credit");
    chi::CHIDatFlit data;
    data.txnId = 7;
    data.opcode = CHI_DAT_COMP_DATA;
    data.data[0] = 0x12345678;
    require(l3Port.sendDatFlit(data), "L3 failed to send a DAT flit");
    advancePorts(haPort, l3Port);
    require(haPort.hasDatFlit(), "HA did not report its pending DAT flit");
    chi::CHIDatFlit receivedData;
    require(haPort.getDatFlit(receivedData), "HA failed to get the DAT flit");
    require(!haPort.hasDatFlit(), "HA reported a consumed DAT flit");
    require(receivedData.data[0] == 0x12345678, "DAT flit payload changed in transit");
}

void verifyInheritanceAndConnectionRules() {
    TestPort sender;
    TestPort receiver;
    TestPort thirdPort;
    require(sender.connect(receiver), "derived ports did not connect");
    require(!sender.connect(thirdPort), "a connected port accepted a second peer");
    require(!thirdPort.connect(thirdPort), "a port connected to itself");
    sender.disconnect();
    require(!sender.isConnected() && !receiver.isConnected(), "disconnect left a stale peer pointer");
}

void verifyUnconsumedFlitError() {
    chi::CHIPort sender;
    chi::CHIPort receiver;
    require(sender.connect(receiver), "ports did not connect for overwrite test");
    require(receiver.sendReqCredit(), "first REQ credit failed");
    advancePorts(sender, receiver);
    require(receiver.sendReqCredit(), "second REQ credit failed");
    advancePorts(sender, receiver);

    chi::CHIReqFlit firstFlit;
    firstFlit.txnId = 1;
    chi::CHIReqFlit secondFlit;
    secondFlit.txnId = 2;
    require(sender.sendReqFlit(firstFlit), "first REQ flit send failed");
    advancePorts(sender, receiver);

    bool errorReported = false;
    try {
        require(sender.sendReqFlit(secondFlit), "second REQ flit send failed");
        advancePorts(sender, receiver);
    } catch (const runtime_error& error) {
        errorReported = string(error.what()).find("unconsumed") != string::npos;
    }
    require(errorReported, "unconsumed peer flit did not report an error");
    require(sender.getReqCredit() == 1, "failed overwrite consumed a credit");

    chi::CHIReqFlit received;
    require(receiver.getReqFlit(received) && received.txnId == 1,
            "overwrite error changed the first flit");
    require(sender.sendReqFlit(secondFlit), "send did not recover after peer consumed the flit");
    advancePorts(sender, receiver);
    require(receiver.getReqFlit(received) && received.txnId == 2,
            "recovered send did not deliver the second flit");
}

void verifyFlitLogging() {
    chi::CHIReqFlit request;
    request.txnId = 0x2A;
    request.opcode = CHI_REQ_READ_SHARED;
    request.address = 0xABC;
    string requestText = request.toString();
    require(requestText.find("txnId=0x2a") != string::npos &&
                requestText.find("address=0xabc") != string::npos,
            "REQ toString did not print hexadecimal fields");

    ostringstream requestStream;
    requestStream << request << ' ' << 10;
    require(requestStream.str() == requestText + " 10",
            "REQ stream output differed from toString or changed stream formatting");

    chi::CHIRspFlit response;
    response.resp = 0xB;
    require(response.toString().find("resp=0xb") != string::npos,
            "RSP toString did not print hexadecimal fields");

    chi::CHISnpFlit snoop;
    snoop.address = 0xDEF;
    require(snoop.toString().find("address=0xdef") != string::npos,
            "SNP toString did not print hexadecimal fields");

    chi::CHIDatFlit data;
    data.byteEnable[1] = 0xF;
    data.data[0] = 0x1234;
    string dataText = data.toString();
    require(dataText.find("byteEnable=[0x0, 0xf]") != string::npos &&
                dataText.find("data=[0x1234,") != string::npos,
            "DAT toString did not print hexadecimal arrays");
}

}  // namespace toc::chiPortTest

int main() {
    toc::chiPortTest::verifyOpcodeEncoding();
    toc::chiPortTest::verifyHaL3Connection();
    toc::chiPortTest::verifyInheritanceAndConnectionRules();
    toc::chiPortTest::verifyUnconsumedFlitError();
    toc::chiPortTest::verifyFlitLogging();
    return 0;
}
