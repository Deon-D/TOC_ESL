#include "CHIPort.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace toc::chi {

using namespace std;

namespace {

template <typename Flit>
struct ChannelStatus {
    optional<Flit> incoming;
    optional<Flit> outgoing;
    optional<Flit> pendingIncoming;
    uint32_t transmitCredit = 0;
    uint32_t pendingCredit = 0;
    bool outgoingCredit = false;
};

template <typename Flit>
bool sendFlit(ChannelStatus<Flit>& local,
              const Flit& flit,
              const char* channelName) {
    if (local.transmitCredit == 0) {
        return false;
    }
    if (local.outgoing.has_value()) {
        throw runtime_error(string("CHI ") + channelName +
                            " channel already has a flit scheduled in this cycle");
    }

    --local.transmitCredit;
    local.outgoing = flit;
    return true;
}

template <typename Flit>
bool getFlit(ChannelStatus<Flit>& local, Flit& flit) {
    if (!local.incoming.has_value()) {
        return false;
    }

    flit = move(*local.incoming);
    local.incoming.reset();
    return true;
}

template <typename Flit>
bool hasFlit(const ChannelStatus<Flit>& local) {
    return local.incoming.has_value();
}

template <typename Flit>
uint32_t getCredit(const ChannelStatus<Flit>& local) {
    return local.transmitCredit;
}

template <typename Flit>
bool hasCredit(const ChannelStatus<Flit>& local) {
    return local.transmitCredit > 0;
}

template <typename Flit>
bool sendCredit(ChannelStatus<Flit>& local) {
    if (local.outgoingCredit) {
        return false;
    }

    local.outgoingCredit = true;
    return true;
}

template <typename Flit>
void workChannel(ChannelStatus<Flit>& local,
                 ChannelStatus<Flit>& remote,
                 const char* channelName) {
    if (local.outgoing.has_value()) {
        if (remote.pendingIncoming.has_value()) {
            throw runtime_error(string("CHI peer received more than one ") + channelName +
                                " flit in one cycle");
        }
        remote.pendingIncoming = move(local.outgoing);
        local.outgoing.reset();
    }

    if (local.outgoingCredit) {
        ++remote.pendingCredit;
        local.outgoingCredit = false;
    }
}

template <typename Flit>
void validateXfer(ChannelStatus<Flit>& local,
                  ChannelStatus<Flit>& remote,
                  const char* channelName) {
    if (local.incoming.has_value() && local.pendingIncoming.has_value()) {
        if (remote.transmitCredit < ChiCommon::protocolMaxLinkCredit) {
            ++remote.transmitCredit;
        }
        local.pendingIncoming.reset();
        throw runtime_error(string("CHI peer contains an unconsumed ") + channelName + " flit");
    }

    if (local.pendingCredit >
        ChiCommon::protocolMaxLinkCredit - local.transmitCredit) {
        local.pendingCredit = 0;
        throw runtime_error(string("CHI ") + channelName + " credit overflow");
    }
}

template <typename Flit>
void xferChannel(ChannelStatus<Flit>& local) {
    if (local.pendingIncoming.has_value()) {
        local.incoming = move(local.pendingIncoming);
        local.pendingIncoming.reset();
    }

    local.transmitCredit += local.pendingCredit;
    local.pendingCredit = 0;
}

}  // namespace

struct CHIPort::ChiLink {
    ChannelStatus<CHIReqFlit> req;
    ChannelStatus<CHIRspFlit> rsp;
    ChannelStatus<CHISnpFlit> snp;
    ChannelStatus<CHIDatFlit> dat;

    void reset() {
        req = ChannelStatus<CHIReqFlit>();
        rsp = ChannelStatus<CHIRspFlit>();
        snp = ChannelStatus<CHISnpFlit>();
        dat = ChannelStatus<CHIDatFlit>();
    }
};

CHIPort::CHIPort() : chiLink_(make_unique<ChiLink>()) {}

CHIPort::~CHIPort() {
    disconnect();
}

bool CHIPort::connect(CHIPort& peerPort) {
    if (&peerPort == this) {
        return false;
    }
    if (peerPort_ == &peerPort && peerPort.peerPort_ == this) {
        return true;
    }
    if (peerPort_ != nullptr || peerPort.peerPort_ != nullptr) {
        return false;
    }

    peerPort_ = &peerPort;
    peerPort.peerPort_ = this;
    return true;
}

void CHIPort::disconnect() {
    if (peerPort_ == nullptr) {
        return;
    }

    auto* previousPeer = peerPort_;
    peerPort_ = nullptr;
    chiLink_->reset();
    if (previousPeer->peerPort_ == this) {
        previousPeer->peerPort_ = nullptr;
        previousPeer->chiLink_->reset();
    }
}

bool CHIPort::isConnected() const {
    return peerPort_ != nullptr;
}

CHIPort* CHIPort::peerPort() {
    return peerPort_;
}

const CHIPort* CHIPort::peerPort() const {
    return peerPort_;
}

void CHIPort::work() {
    if (peerPort_ == nullptr) {
        return;
    }

    workChannel(chiLink_->req, peerPort_->chiLink_->req, "REQ");
    workChannel(chiLink_->rsp, peerPort_->chiLink_->rsp, "RSP");
    workChannel(chiLink_->snp, peerPort_->chiLink_->snp, "SNP");
    workChannel(chiLink_->dat, peerPort_->chiLink_->dat, "DAT");
}

void CHIPort::xfer() {
    if (peerPort_ != nullptr) {
        validateXfer(chiLink_->req, peerPort_->chiLink_->req, "REQ");
        validateXfer(chiLink_->rsp, peerPort_->chiLink_->rsp, "RSP");
        validateXfer(chiLink_->snp, peerPort_->chiLink_->snp, "SNP");
        validateXfer(chiLink_->dat, peerPort_->chiLink_->dat, "DAT");
    }

    xferChannel(chiLink_->req);
    xferChannel(chiLink_->rsp);
    xferChannel(chiLink_->snp);
    xferChannel(chiLink_->dat);
}

bool CHIPort::sendReqFlit(const CHIReqFlit& flit) {
    return peerPort_ != nullptr && sendFlit(chiLink_->req, flit, "REQ");
}

bool CHIPort::getReqFlit(CHIReqFlit& flit) {
    return getFlit(chiLink_->req, flit);
}

bool CHIPort::hasReqFlit() const {
    return hasFlit(chiLink_->req);
}

uint32_t CHIPort::getReqCredit() const {
    return getCredit(chiLink_->req);
}

bool CHIPort::hasReqCredit() const {
    return hasCredit(chiLink_->req);
}

bool CHIPort::sendReqCredit() {
    return peerPort_ != nullptr && sendCredit(chiLink_->req);
}

bool CHIPort::sendRspFlit(const CHIRspFlit& flit) {
    return peerPort_ != nullptr && sendFlit(chiLink_->rsp, flit, "RSP");
}

bool CHIPort::getRspFlit(CHIRspFlit& flit) {
    return getFlit(chiLink_->rsp, flit);
}

bool CHIPort::hasRspFlit() const {
    return hasFlit(chiLink_->rsp);
}

uint32_t CHIPort::getRspCredit() const {
    return getCredit(chiLink_->rsp);
}

bool CHIPort::hasRspCredit() const {
    return hasCredit(chiLink_->rsp);
}

bool CHIPort::sendRspCredit() {
    return peerPort_ != nullptr && sendCredit(chiLink_->rsp);
}

bool CHIPort::sendSnpFlit(const CHISnpFlit& flit) {
    return peerPort_ != nullptr && sendFlit(chiLink_->snp, flit, "SNP");
}

bool CHIPort::getSnpFlit(CHISnpFlit& flit) {
    return getFlit(chiLink_->snp, flit);
}

bool CHIPort::hasSnpFlit() const {
    return hasFlit(chiLink_->snp);
}

uint32_t CHIPort::getSnpCredit() const {
    return getCredit(chiLink_->snp);
}

bool CHIPort::hasSnpCredit() const {
    return hasCredit(chiLink_->snp);
}

bool CHIPort::sendSnpCredit() {
    return peerPort_ != nullptr && sendCredit(chiLink_->snp);
}

bool CHIPort::sendDatFlit(const CHIDatFlit& flit) {
    return peerPort_ != nullptr && sendFlit(chiLink_->dat, flit, "DAT");
}

bool CHIPort::getDatFlit(CHIDatFlit& flit) {
    return getFlit(chiLink_->dat, flit);
}

bool CHIPort::hasDatFlit() const {
    return hasFlit(chiLink_->dat);
}

uint32_t CHIPort::getDatCredit() const {
    return getCredit(chiLink_->dat);
}

bool CHIPort::hasDatCredit() const {
    return hasCredit(chiLink_->dat);
}

bool CHIPort::sendDatCredit() {
    return peerPort_ != nullptr && sendCredit(chiLink_->dat);
}

}  // namespace toc::chi
