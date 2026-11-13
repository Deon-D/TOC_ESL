#pragma once

#include "kernel/model.hpp"
#include "transport_adapter.hpp"

namespace esl::chi_vip {
// Startup reset only. The standalone protocol library has no ModelBase dependency.
class RNModel final : public ModelBase {
public:
    RNModel(ModelID id, std::string path, ModelContext& context, RNConfig config = {},
            Observer observer = {});
    RN& Core();
    CHIPort& Port();
    bool IsIdle() const override;
    std::string DescribeState() const override;

protected:
    void OnInitialize() override;
    void OnReset() override;
    void OnWork() override;
    void OnXfer() override;

private:
    RN core_;
    CHIPort port_;
    TransportAdapter adapter_;
};
}  // namespace esl::chi_vip
