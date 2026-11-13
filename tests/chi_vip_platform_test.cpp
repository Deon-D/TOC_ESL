#include "chi_vip_test_support.hpp"
#include "esl_adapter.hpp"
#include "kernel/kernel.hpp"

namespace {
class HomeFixture final : public esl::ModelBase {
public:
    esl::CHIPort port{"test.home"};
    vip_test::MemoryPeer peer;
    std::array<std::deque<esl::chi_vip::Flit>, 4> pending;
    explicit HomeFixture(esl::ModelContext& context) : ModelBase({300}, "home", context) {
        peer.deliver = [this](esl::chi_vip::Flit f) {
            const auto channel = f.index();
            pending[channel].push_back(std::move(f));
        };
    }
    bool IsIdle() const override {
        for (const auto& q : pending)
            if (!q.empty())
                return false;
        return port.IsIdle() && peer.writes.empty() && peer.acknowledgments.empty();
    }
    std::string DescribeState() const override {
        return port.DescribeState();
    }

protected:
    void OnInitialize() override {
        port.CheckBound();
    }
    void OnReset() override {}
    void OnWork() override {
        port.Work();
        esl::CHIReqFlit req;
        esl::CHIRspFlit rsp;
        esl::CHIDatFlit dat;
        if (port.Recv(req))
            peer.Receive(req);
        if (port.Recv(rsp))
            peer.Receive(rsp);
        if (port.Recv(dat))
            peer.Receive(dat);
        for (auto& q : pending)
            while (!q.empty()) {
                if (!std::visit([this](const auto& f) { return port.Send(f); }, q.front()))
                    break;
                q.pop_front();
            }
    }
    void OnXfer() override {
        port.Xfer();
    }
};
class CompletionSink final : public esl::ModelBase {
public:
    esl::chi_vip::RN& core;
    unsigned count = 0;
    CompletionSink(esl::ModelContext& context, esl::chi_vip::RN& rn)
        : ModelBase({400}, "client", context), core(rn) {}
    bool IsIdle() const override {
        return count == 2;
    }
    std::string DescribeState() const override {
        return "completions=" + std::to_string(count);
    }

protected:
    void OnInitialize() override {}
    void OnReset() override {}
    void OnWork() override {
        esl::chi_vip::Completion c;
        while (core.PollCompletion(c)) {
            vip_test::Expect(c.handle == 1 || c.handle == 2, "unknown platform handle");
            vip_test::Expect(c.data == vip_test::Pattern((c.handle - 1) * 64),
                             "platform data mismatch");
            ++count;
        }
    }
    void OnXfer() override {}
};
std::vector<std::string> Run(bool reverse) {
    esl::Clock clock;
    esl::LogConfig logging;
    logging.console = false;
    logging.level = esl::LogLevel::kOff;
    logging.directory.clear();
    esl::Logger logger(clock, logging);
    esl::TraceSink trace(clock, nullptr);
    esl::ModelContext context{clock, logger, trace};
    esl::SimKernel kernel(clock, logger, trace, reverse);
    std::vector<std::string> result;
    esl::chi_vip::RNModel rn({200}, "rn", context, {}, [&](const esl::chi_vip::Observation& o) {
        if (o.action == "tx_sent" && o.flit)
            result.push_back(std::to_string(o.cycle) +
                             std::visit([](const auto& f) { return f.to_string(); }, *o.flit));
    });
    HomeFixture home(context);
    CompletionSink client(context, rn.Core());
    rn.Port().Connect(home.port);
    kernel.Add(rn);
    kernel.Add(home);
    kernel.Add(client);
    kernel.Schedule(1, 400, [&] {
        vip_test::Expect(rn.Core().Load(0, 64).accepted && rn.Core().Load(64, 64).accepted,
                         "platform admission");
    });
    kernel.Run(300);
    vip_test::Expect(client.count == 2 && rn.IsIdle() && home.IsIdle(), "platform did not drain");
    result.push_back(std::to_string(kernel.ExecutedCycles()));
    return result;
}
}  // namespace
int main() {
    try {
        vip_test::Expect(Run(false) == Run(true), "platform traversal changed RN behavior");
        std::cout << "RN platform passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
