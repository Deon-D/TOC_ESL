#include "esl_adapter.hpp"

namespace esl::chi_vip {
RNModel::RNModel(ModelID id, std::string path, ModelContext& context, RNConfig config,
                 Observer observer)
    : ModelBase(id, path, context),
      core_(config, std::move(observer)),
      port_(path + ".chi"),
      adapter_(core_, port_) {}
RN& RNModel::Core() {
    return core_;
}
CHIPort& RNModel::Port() {
    return port_;
}
bool RNModel::IsIdle() const {
    return adapter_.IsIdle();
}
std::string RNModel::DescribeState() const {
    return adapter_.DescribeState();
}
void RNModel::OnInitialize() {
    port_.CheckBound();
}
void RNModel::OnReset() {
    Check(core_.IsIdle(), ErrorKind::kLifecycle, "RN supports startup reset only");
}
void RNModel::OnWork() {
    adapter_.Work(context_.clock.CurrentCycle().value);
}
void RNModel::OnXfer() {
    adapter_.Xfer();
}
}  // namespace esl::chi_vip
