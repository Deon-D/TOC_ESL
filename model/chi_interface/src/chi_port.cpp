#include "chi_port.h"

#include <limits>
#include <utility>

namespace esl {
CHIPort::CHIPort(std::string name)
    : name_(std::move(name)),
      req_(name_ + ".req"),
      rsp_(name_ + ".rsp"),
      snp_(name_ + ".snp"),
      dat_(name_ + ".dat"),
      channels_{&req_, &rsp_, &snp_, &dat_} {
    chi_detail::Require(!name_.empty(), "CHI port name must not be empty");
}
CHIPort::~CHIPort() {
    if (peer_) {
        peer_->req_.peer_ = nullptr;
        peer_->rsp_.peer_ = nullptr;
        peer_->snp_.peer_ = nullptr;
        peer_->dat_.peer_ = nullptr;
        peer_->peer_ = nullptr;
    }
}
void CHIPort::Connect(CHIPort& peer) {
    chi_detail::Require(&peer != this, "CHI port cannot connect to itself");
    chi_detail::Require(!peer_ && !peer.peer_, "duplicate CHI binding");
    chi_detail::Require(!started_ && !peer.started_, "CHI connection only allowed at startup");
    chi_detail::Require(name_ != peer.name_, "duplicate CHI port name");
    peer_ = &peer;
    peer.peer_ = this;
    req_.peer_ = &peer.req_;
    peer.req_.peer_ = &req_;
    rsp_.peer_ = &peer.rsp_;
    peer.rsp_.peer_ = &rsp_;
    snp_.peer_ = &peer.snp_;
    peer.snp_.peer_ = &snp_;
    dat_.peer_ = &peer.dat_;
    peer.dat_.peer_ = &dat_;
}
CHIPort* CHIPort::Peer() const {
    return peer_;
}
void CHIPort::CheckBound() const {
    chi_detail::Require(peer_ && peer_->peer_ == this, name_, "unbound CHI port");
}
void CHIPort::CheckSetup() const {
    chi_detail::Require(!started_ && (!peer_ || !peer_->started_), name_,
                        "parameters only allowed at startup");
}
void CHIPort::SetParams(const CHIPortParams& params) {
    CheckSetup();
    params.req.Validate();
    params.rsp.Validate();
    params.snp.Validate();
    params.dat.Validate();
    req_.SetParams(params.req);
    rsp_.SetParams(params.rsp);
    snp_.SetParams(params.snp);
    dat_.SetParams(params.dat);
}
CHIPortParams CHIPort::Params() const {
    return {req_.Params(), rsp_.Params(), snp_.Params(), dat_.Params()};
}
void CHIPort::Work() {
    CheckBound();
    chi_detail::Require(!in_work_, name_, "duplicate Work without Xfer");
    chi_detail::Require(peer_->cycle_ == cycle_, name_, "peer cycle mismatch in Work");
    for (auto* channel : channels_)
        channel->Work(cycle_);
    started_ = true;
    in_work_ = true;
}
void CHIPort::Xfer() {
    CheckBound();
    chi_detail::Require(in_work_, name_, "Xfer requires Work");
    chi_detail::Require(cycle_ < std::numeric_limits<std::uint64_t>::max(), name_,
                        "cycle overflow");
    // A peer may have transferred already in this cycle, but must not skip Work
    // or enter the next cycle. Neither order changes the local RX or credit view.
    chi_detail::Require((peer_->cycle_ == cycle_ && peer_->in_work_) ||
                            (peer_->cycle_ == cycle_ + 1 && !peer_->in_work_),
                        name_, "peer must complete Work before Xfer");
    for (auto* channel : channels_)
        channel->Xfer(cycle_);
    in_work_ = false;
    ++cycle_;
}
bool CHIPort::IsIdle() const {
    for (const auto* channel : channels_)
        if (!channel->IsIdle())
            return false;
    return true;
}
std::string CHIPort::DescribeState() const {
    std::string result = name_ + " peer=" + (peer_ ? peer_->name_ : "unbound") +
                         " cycle=" + std::to_string(cycle_) +
                         " stage=" + (in_work_ ? "work" : "ready");
    for (const auto* channel : channels_)
        result += "\n" + channel->DescribeState();
    return result;
}
}  // namespace esl
