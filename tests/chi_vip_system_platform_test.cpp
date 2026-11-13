#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "esl_adapter.hpp"
#include "kernel/kernel.hpp"
#include "system_esl_adapter.hpp"

namespace {
using namespace esl;
using namespace esl::chi_vip;

void Expect(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
Line Pattern(unsigned seed) {
    Line result{};
    for (unsigned i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>(seed * 29 + i * 13);
    return result;
}

class ScriptRunner final : public ModelBase {
public:
    using Step = std::function<void()>;
    std::vector<Step> steps;
    std::vector<std::string> completions;

    ScriptRunner(ModelContext& context, SimKernel& kernel, RN& first, RN& second)
        : ModelBase({500}, "client", context), kernel_(kernel), nodes_{{&first, &second}} {}

    void ExpectOperation(unsigned node, SubmitResult result, std::optional<Line> expected = {}) {
        Expect(node < nodes_.size() && result.accepted, "platform operation was backpressured");
        Expect(pending_.emplace(std::make_pair(node, result.handle), expected).second,
               "platform operation handle was reused while live");
    }
    void Next() {
        Expect(!running_ && pending_.empty() && phase_ < steps.size(),
               "invalid platform scenario transition");
        running_ = true;
        steps.at(phase_++)();
        Expect(!pending_.empty(), "empty platform scenario step");
    }
    bool IsIdle() const override {
        return phase_ == steps.size() && !running_ && pending_.empty();
    }
    std::string DescribeState() const override {
        return "scenario=" + std::to_string(phase_) + " pending=" + std::to_string(pending_.size());
    }

protected:
    void OnInitialize() override {}
    void OnReset() override {}
    void OnWork() override {
        // Polling committed completions is safe in either model traversal order.
        // New submissions run in the kernel's next Prepare phase, before Work.
        for (unsigned node = 0; node < nodes_.size(); ++node) {
            Completion completion;
            while (nodes_[node]->PollCompletion(completion)) {
                const auto found = pending_.find({node, completion.handle});
                Expect(found != pending_.end(), "unexpected platform completion");
                Expect(completion.error == AccessError::kOK, "platform access error");
                if (found->second) {
                    Expect(completion.valid == kAllBytes,
                           "platform read did not return every cache-line byte");
                    Expect(completion.data == *found->second,
                           "platform read disagrees with independent byte oracle");
                }
                completions.push_back(std::to_string(node) + ':' +
                                      std::to_string(completion.handle) + ':' +
                                      std::to_string(completion.cycle));
                pending_.erase(found);
            }
        }
        if (running_ && pending_.empty()) {
            running_ = false;
            if (phase_ != steps.size())
                kernel_.Schedule(1, ID().value, [this] { Next(); });
        }
    }
    void OnXfer() override {}

private:
    SimKernel& kernel_;
    std::array<RN*, 2> nodes_;
    std::map<std::pair<unsigned, Handle>, std::optional<Line>> pending_;
    std::size_t phase_ = 0;
    bool running_ = false;
};

struct RunResult {
    std::map<NodeID, std::vector<std::string>> transmissions;
    std::vector<std::string> completions;
    std::uint64_t cycles = 0;
    bool operator==(const RunResult& other) const {
        return transmissions == other.transmissions && completions == other.completions &&
               cycles == other.cycles;
    }
};

RunResult Run(bool reverse) {
    Clock clock;
    LogConfig logging;
    logging.console = false;
    logging.level = LogLevel::kOff;
    logging.directory.clear();
    Logger logger(clock, logging);
    TraceSink trace(clock, nullptr);
    ModelContext context{clock, logger, trace};
    SimKernel kernel(clock, logger, trace, reverse);
    RunResult result;
    unsigned snoops = 0;
    unsigned downstream_reads = 0;
    unsigned downstream_writes = 0;
    const auto observe = [&](NodeID node) {
        return [&, node](const Observation& observation) {
            if (observation.action != "tx_sent" || !observation.flit)
                return;
            result.transmissions[node].push_back(
                std::to_string(observation.cycle) + ':' +
                std::visit([](const auto& flit) { return flit.to_string(); }, *observation.flit));
            if (std::holds_alternative<CHISnpFlit>(*observation.flit))
                ++snoops;
            if (const auto* request = std::get_if<CHIReqFlit>(&*observation.flit)) {
                if (request->flit_src == 6 && request->flit_tgt == 11) {
                    if (request->flit_opcode == CHI_REQ_Opcode_ReadNoSnp)
                        ++downstream_reads;
                    else if (request->flit_opcode == CHI_REQ_Opcode_WriteNoSnpFull ||
                             request->flit_opcode == CHI_REQ_Opcode_WriteNoSnpPtl)
                        ++downstream_writes;
                }
            }
        };
    };

    RNConfig rn_config;
    rn_config.node_id = 4;
    rn_config.home_id = 6;
    rn_config.cache_lines = 4;
    rn_config.request_slots = 2;
    rn_config.snoop_slots = 1;
    RNModel first({100}, "rn0", context, rn_config, observe(4));
    rn_config.node_id = 9;
    RNModel second({200}, "rn1", context, rn_config, observe(9));
    HNConfig hn_config;
    hn_config.node_id = 6;
    hn_config.slave_id = 11;
    hn_config.requester_ids = {4, 9};
    hn_config.request_slots = 2;
    HNModel home({300}, "hn", context, hn_config, observe(6));
    SNConfig sn_config;
    sn_config.node_id = 11;
    sn_config.home_id = 6;
    sn_config.request_slots = 1;
    sn_config.service_latency = 7;
    SNModel slave({400}, "sn", context, sn_config, observe(11));
    ScriptRunner client(context, kernel, first.Core(), second.Core());

    CHIPortParams params;
    params.req = {1, 1, 1, 2};
    params.rsp = {1, 1, 2, 1};
    params.snp = {1, 1, 4, 1};
    params.dat = {1, 1, 3, 2};
    const std::array<CHIPort*, 6> ports{{&first.Port(), &second.Port(), &home.Port(4),
                                         &home.Port(9), &home.Port(11), &slave.Port()}};
    for (auto* port : ports)
        port->SetParams(params);
    first.Port().Connect(home.Port(4));
    second.Port().Connect(home.Port(9));
    home.Port(11).Connect(slave.Port());
    for (unsigned line = 0; line < 4; ++line)
        slave.Core().Memory().Initialize(line * 64, Pattern(line));

    const auto initial = Pattern(0);
    auto partial = initial;
    const auto store_data = Pattern(8);
    constexpr std::uint64_t store_mask = 0x00ffff0000000000ULL;
    for (unsigned i = 0; i < 64; ++i)
        if ((store_mask >> i) & 1)
            partial[i] = store_data[i];
    const auto replacement = Pattern(9);
    auto noncoherent = Pattern(2);
    constexpr std::uint64_t write_mask = 0xff000000000000ffULL;
    const auto write_data = Pattern(10);
    for (unsigned i = 0; i < 64; ++i)
        if ((write_mask >> i) & 1)
            noncoherent[i] = write_data[i];

    client.steps = {
        [&] { client.ExpectOperation(0, first.Core().Load(0, 64), initial); },
        [&] { client.ExpectOperation(1, second.Core().Load(0, 64), initial); },
        [&] { client.ExpectOperation(0, first.Core().Store(0, store_data, store_mask)); },
        [&] { client.ExpectOperation(1, second.Core().Load(0, 64), partial); },
        [&] { client.ExpectOperation(1, second.Core().Store(0, replacement, kAllBytes)); },
        [&] { client.ExpectOperation(0, first.Core().Load(0, 64), replacement); },
        [&] {
            TxnRequest request;
            request.opcode = CHI_REQ_Opcode_CleanShared;
            client.ExpectOperation(1, second.Core().Submit(request));
        },
        [&] {
            TxnRequest request;
            request.opcode = CHI_REQ_Opcode_CleanInvalid;
            client.ExpectOperation(0, first.Core().Submit(request));
        },
        [&] {
            TxnRequest request;
            request.opcode = CHI_REQ_Opcode_ReadNoSnp;
            client.ExpectOperation(0, first.Core().Submit(request), replacement);
        },
        [&] {
            TxnRequest request;
            request.opcode = CHI_REQ_Opcode_WriteNoSnpPtl;
            request.address = 128;
            request.data = write_data;
            request.byte_enable = write_mask;
            client.ExpectOperation(0, first.Core().Submit(request));
        },
        [&] {
            TxnRequest request;
            request.opcode = CHI_REQ_Opcode_ReadNoSnp;
            request.address = 128;
            client.ExpectOperation(1, second.Core().Submit(request), noncoherent);
        },
        [&] {
            client.ExpectOperation(0, first.Core().Load(64, 64), Pattern(1));
            client.ExpectOperation(1, second.Core().Load(192, 64), Pattern(3));
        },
    };
    kernel.Add(first);
    kernel.Add(second);
    kernel.Add(home);
    kernel.Add(slave);
    kernel.Add(client);
    kernel.Schedule(1, client.ID().value, [&] { client.Next(); });
    kernel.Run(5000);

    Expect(first.IsIdle() && second.IsIdle() && home.IsIdle() && slave.IsIdle() && client.IsIdle(),
           "platform system did not completely drain");
    Expect(slave.Core().Memory().Read(0) == replacement, "dirty cache data was not preserved");
    Expect(slave.Core().Memory().Read(128) == noncoherent,
           "partial write changed bytes outside its enable mask");
    Expect(snoops != 0 && downstream_reads != 0 && downstream_writes != 0,
           "platform scenario did not exercise each protocol stage");
    Expect(result.transmissions.size() == 4, "missing actual-send observations from a node");
    result.completions = std::move(client.completions);
    result.cycles = kernel.ExecutedCycles();
    return result;
}
}  // namespace

int main() {
    try {
        Expect(Run(false) == Run(true), "reversing Work/Xfer changed the connected CHI system");
        std::cout << "RN/HN/SN ModelBase integration and traversal determinism passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
