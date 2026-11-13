#include "system_esl_adapter.hpp"

namespace esl::chi_vip {
HNModel::Ports HNModel::MakePorts(const HNConfig& config, const std::string& path) {
    Ports ports;
    for (const auto peer : config.requester_ids)
        ports.emplace(peer, std::make_unique<CHIPort>(path + ".chi." + std::to_string(peer)));
    ports.emplace(config.slave_id,
                  std::make_unique<CHIPort>(path + ".chi." + std::to_string(config.slave_id)));
    return ports;
}
std::map<NodeID, CHIPort*> HNModel::Routes(const Ports& ports) {
    std::map<NodeID, CHIPort*> routes;
    for (const auto& [peer, port] : ports)
        routes.emplace(peer, port.get());
    return routes;
}
HNModel::HNModel(ModelID id, std::string path, ModelContext& context, HNConfig config,
                 Observer observer)
    : ModelBase(id, path, context),
      core_(config, std::move(observer)),
      ports_(MakePorts(config, path)),
      adapter_(core_, Routes(ports_)) {}
HN& HNModel::Core() {
    return core_;
}
CHIPort& HNModel::Port(NodeID peer) {
    const auto found = ports_.find(peer);
    Check(found != ports_.end(), ErrorKind::kConfiguration, "HNModel port peer is not configured");
    return *found->second;
}
bool HNModel::IsIdle() const {
    return adapter_.IsIdle();
}
std::string HNModel::DescribeState() const {
    return adapter_.DescribeState();
}
void HNModel::OnInitialize() {
    for (const auto& [peer, port] : ports_) {
        (void)peer;
        port->CheckBound();
    }
}
void HNModel::OnReset() {
    Check(core_.IsIdle(), ErrorKind::kLifecycle, "HN supports startup reset only");
}
void HNModel::OnWork() {
    adapter_.Work(context_.clock.CurrentCycle().value);
}
void HNModel::OnXfer() {
    adapter_.Xfer();
}

SNModel::SNModel(ModelID id, std::string path, ModelContext& context, SNConfig config,
                 Observer observer)
    : ModelBase(id, path, context),
      core_(config, std::move(observer)),
      port_(path + ".chi"),
      adapter_(core_, port_) {}
SN& SNModel::Core() {
    return core_;
}
CHIPort& SNModel::Port() {
    return port_;
}
bool SNModel::IsIdle() const {
    return adapter_.IsIdle();
}
std::string SNModel::DescribeState() const {
    return adapter_.DescribeState();
}
void SNModel::OnInitialize() {
    port_.CheckBound();
}
void SNModel::OnReset() {
    Check(core_.IsIdle(), ErrorKind::kLifecycle, "SN supports startup reset only");
}
void SNModel::OnWork() {
    adapter_.Work(context_.clock.CurrentCycle().value);
}
void SNModel::OnXfer() {
    adapter_.Xfer();
}
}  // namespace esl::chi_vip
