#pragma once

#include "ChiCommon.h"

#include <cstdint>
#include <memory>

namespace toc::chi {

using namespace std;

class CHIPort {
public:
    CHIPort();
    virtual ~CHIPort();

    CHIPort(const CHIPort&) = delete;
    CHIPort& operator=(const CHIPort&) = delete;
    CHIPort(CHIPort&&) = delete;
    CHIPort& operator=(CHIPort&&) = delete;

    bool connect(CHIPort& peerPort);
    void disconnect();
    bool isConnected() const;
    CHIPort* peerPort();
    const CHIPort* peerPort() const;

    void work();
    void xfer();

    bool sendReqFlit(const CHIReqFlit& flit);
    bool getReqFlit(CHIReqFlit& flit);
    bool hasReqFlit() const;
    uint32_t getReqCredit() const;
    bool hasReqCredit() const;
    bool sendReqCredit();

    bool sendRspFlit(const CHIRspFlit& flit);
    bool getRspFlit(CHIRspFlit& flit);
    bool hasRspFlit() const;
    uint32_t getRspCredit() const;
    bool hasRspCredit() const;
    bool sendRspCredit();

    bool sendSnpFlit(const CHISnpFlit& flit);
    bool getSnpFlit(CHISnpFlit& flit);
    bool hasSnpFlit() const;
    uint32_t getSnpCredit() const;
    bool hasSnpCredit() const;
    bool sendSnpCredit();

    bool sendDatFlit(const CHIDatFlit& flit);
    bool getDatFlit(CHIDatFlit& flit);
    bool hasDatFlit() const;
    uint32_t getDatCredit() const;
    bool hasDatCredit() const;
    bool sendDatCredit();

private:
    struct ChiLink;

    unique_ptr<ChiLink> chiLink_;
    CHIPort* peerPort_ = nullptr;
};

}  // namespace toc::chi
