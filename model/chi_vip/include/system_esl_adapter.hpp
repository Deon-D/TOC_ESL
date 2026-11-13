#pragma once

#include <map>
#include <memory>

#include "hn.hpp"
#include "kernel/model.hpp"
#include "node_transport_adapter.hpp"
#include "sn.hpp"

namespace esl::chi_vip {
class HNModel final : public ModelBase {
public:
    HNModel(ModelID id, std::string path, ModelContext& context, HNConfig config = {},
            Observer observer = {});
    HN& Core();
    CHIPort& Port(NodeID peer);
    bool IsIdle() const override;
    std::string DescribeState() const override;

protected:
    void OnInitialize() override;
    void OnReset() override;
    void OnWork() override;
    void OnXfer() override;

private:
    using Ports = std::map<NodeID, std::unique_ptr<CHIPort>>;
    static Ports MakePorts(const HNConfig& config, const std::string& path);
    static std::map<NodeID, CHIPort*> Routes(const Ports& ports);
    HN core_;
    Ports ports_;
    NodeTransportAdapter adapter_;
};

class SNModel final : public ModelBase {
public:
    SNModel(ModelID id, std::string path, ModelContext& context, SNConfig config = {},
            Observer observer = {});
    SN& Core();
    CHIPort& Port();
    bool IsIdle() const override;
    std::string DescribeState() const override;

protected:
    void OnInitialize() override;
    void OnReset() override;
    void OnWork() override;
    void OnXfer() override;

private:
    SN core_;
    CHIPort port_;
    NodeTransportAdapter adapter_;
};
}  // namespace esl::chi_vip
