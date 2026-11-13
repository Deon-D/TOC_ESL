#include "system.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <tuple>

#include "esl_adapter.hpp"
#include "kernel/kernel.hpp"
#include "system_esl_adapter.hpp"

namespace esl::chi_vip {
namespace {
std::string JsonString(const std::string& value) {
    std::ostringstream text;
    text << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\')
            text << '\\' << c;
        else if (c < 32)
            text << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c)
                 << std::dec;
        else
            text << c;
    }
    text << '"';
    return text.str();
}

Line Pattern(std::uint64_t seed) {
    Line data{};
    for (unsigned byte = 0; byte < data.size(); ++byte) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        data[byte] = static_cast<std::uint8_t>(seed >> 56);
    }
    return data;
}

// This driver is a testbench client, not part of any protocol core. Operations
// on different lines overlap; same-line dependencies have a defined happens-
// before relation, so the oracle never imposes an arbitrary interconnect order.
class Workload final : public ModelBase {
public:
    Workload(ModelContext& context, SimKernel& kernel, const SimConfig& config,
             std::vector<RN*> nodes, RunResult& result)
        : ModelBase({1000}, "vip.workload", context),
          kernel_(kernel),
          config_(config),
          nodes_(std::move(nodes)),
          result_(result),
          random_(config.workload.seed) {}

    void Start() {
        kernel_.Schedule(1, ID().value, [this] { Tick(); });
    }
    bool IsIdle() const override {
        return done_;
    }
    std::string DescribeState() const override {
        return "issued=" + std::to_string(issued_) +
               " completed=" + std::to_string(result_.completed) +
               " pending=" + std::to_string(pending_.size()) +
               " cleanup_node=" + std::to_string(cleanup_node_) + " done=" + std::to_string(done_);
    }
    void CheckMemory(const ByteMemoryBackend& memory) const {
        Require(done_ && pending_.empty(), "workload did not drain");
        Require(result_.completed == config_.workload.requests &&
                    result_.loads + result_.stores == result_.completed,
                "workload completion accounting mismatch");
        for (const auto& [address, expected] : oracle_) {
            Require(memory.Read(address) == expected && !memory.Errors(address),
                    "final memory differs from byte oracle at address=" + std::to_string(address));
            for (auto* node : nodes_) {
                const auto line = node->InspectLine(address);
                Require(!line || line->state == CacheState::kI,
                        "cache maintenance did not invalidate address=" + std::to_string(address));
            }
        }
    }

protected:
    void OnInitialize() override {}
    void OnReset() override {}
    void OnWork() override {}
    void OnXfer() override {}

private:
    enum class Kind { kLoad, kStore, kMaintenance };
    struct Operation {
        Kind kind = Kind::kLoad;
        unsigned node = 0;
        std::uint64_t address = 0;
        std::uint64_t mask = kAllBytes;
        Line data{};
    };
    SimKernel& kernel_;
    const SimConfig& config_;
    std::vector<RN*> nodes_;
    RunResult& result_;
    std::mt19937_64 random_;
    std::map<std::uint64_t, Line> oracle_;
    std::set<std::uint64_t> busy_;
    std::map<std::pair<unsigned, Handle>, Operation> pending_;
    std::optional<Operation> next_;
    std::uint64_t issued_ = 0;
    std::uint64_t next_issue_ = 0;
    std::vector<std::uint64_t> cleanup_lines_;
    std::size_t cleanup_line_ = 0;
    unsigned cleanup_node_ = 0;
    bool cleaning_ = false;
    bool done_ = false;

    void Poll() {
        for (unsigned node = 0; node < nodes_.size(); ++node) {
            Completion completion;
            while (nodes_[node]->PollCompletion(completion)) {
                auto found = pending_.find({node, completion.handle});
                Require(found != pending_.end(), "unexpected workload completion");
                const auto& operation = found->second;
                Require(completion.error == AccessError::kOK,
                        "access error at address=" + std::to_string(operation.address));
                if (operation.kind == Kind::kLoad) {
                    Require(completion.valid == kAllBytes && completion.data == operation.data,
                            "load differs from byte oracle at address=" +
                                std::to_string(operation.address));
                    ++result_.loads;
                } else if (operation.kind == Kind::kStore) {
                    auto& expected = oracle_.at(operation.address);
                    for (unsigned byte = 0; byte < 64; ++byte)
                        if ((operation.mask >> byte) & 1)
                            expected[byte] = operation.data[byte];
                    ++result_.stores;
                }
                if (operation.kind != Kind::kMaintenance) {
                    ++result_.completed;
                    Require(busy_.erase(operation.address) == 1,
                            "workload line dependency accounting mismatch");
                }
                pending_.erase(found);
            }
        }
    }
    void Issue() {
        const auto cycle = context_.clock.CurrentCycle().value;
        if (cycle < next_issue_)
            return;
        if (!next_) {
            Operation operation;
            operation.node = static_cast<unsigned>(issued_ % nodes_.size());
            const auto lines = (config_.workload.memory_size - config_.workload.start_address) / 64;
            const auto index = config_.workload.random_addresses
                                   ? random_() % lines
                                   : (issued_ / nodes_.size()) % lines;
            operation.address = config_.workload.start_address + index * 64;
            operation.kind =
                random_() % 100 < config_.workload.write_percent ? Kind::kStore : Kind::kLoad;
            if (operation.kind == Kind::kStore) {
                operation.data = Pattern(random_());
                // Exercise both whole lines and partial byte enables.
                operation.mask = random_() % 4 == 0 ? kAllBytes : (random_() | 1);
            }
            next_ = operation;
        }
        auto& operation = *next_;
        if (busy_.count(operation.address))
            return;
        auto [expected, inserted] =
            oracle_.emplace(operation.address, Pattern(operation.address ^ config_.workload.seed));
        (void)inserted;
        if (operation.kind == Kind::kLoad)
            operation.data = expected->second;
        auto& node = *nodes_[operation.node];
        const auto submitted = operation.kind == Kind::kLoad
                                   ? node.Load(operation.address, 64)
                                   : node.Store(operation.address, operation.data, operation.mask);
        if (!submitted.accepted)
            return;
        Require(
            pending_.emplace(std::make_pair(operation.node, submitted.handle), operation).second,
            "workload handle reused while pending");
        busy_.insert(operation.address);
        ++issued_;
        next_.reset();
        // No future issue is needed after the final workload operation.
        if (issued_ < config_.workload.requests)
            next_issue_ = AddCycles({cycle}, config_.workload.issue_interval_cycles).value;
    }
    void Clean() {
        if (!pending_.empty())
            return;
        if (!cleaning_) {
            for (const auto& [address, data] : oracle_) {
                (void)data;
                cleanup_lines_.push_back(address);
            }
            cleaning_ = true;
        }
        while (cleanup_node_ < nodes_.size()) {
            if (cleanup_line_ == cleanup_lines_.size()) {
                cleanup_line_ = 0;
                ++cleanup_node_;
                continue;
            }
            const auto address = cleanup_lines_[cleanup_line_];
            // Only the testbench uses the public cache inspection interface to
            // choose a legal maintenance command. Node-to-node state/data still
            // travels entirely over CHI, including every dirty writeback byte.
            const auto line = nodes_[cleanup_node_]->InspectLine(address);
            if (!line || line->state == CacheState::kI) {
                ++cleanup_line_;
                continue;
            }
            TxnRequest request;
            request.address = address;
            request.opcode = line->state == CacheState::kUDP ? CHI_REQ_Opcode_WriteBackPtl
                             : IsDirty(line->state)          ? CHI_REQ_Opcode_WriteBackFull
                                                             : CHI_REQ_Opcode_Evict;
            const auto submitted = nodes_[cleanup_node_]->Submit(request);
            if (!submitted.accepted)
                return;
            Require(
                pending_
                    .emplace(std::make_pair(cleanup_node_, submitted.handle),
                             Operation{Kind::kMaintenance, cleanup_node_, address, kAllBytes, {}})
                    .second,
                "maintenance handle reused while pending");
            ++cleanup_line_;
            return;
        }
        done_ = true;
    }
    void Tick() {
        // Prepare runs before every node's Work. Submission and completion
        // polling therefore cannot depend on model registration/traversal order.
        Poll();
        if (issued_ < config_.workload.requests)
            Issue();
        else
            Clean();
        if (!done_)
            kernel_.Schedule(1, ID().value, [this] { Tick(); });
    }
};
}  // namespace

struct System::Impl {
    struct Transmission {
        std::uint64_t cycle;
        NodeID node;
        std::uint64_t sequence;
        std::string row;
    };
    SimConfig config;
    RunResult result;
    Clock clock;
    Logger logger;
    TraceSink phase_trace;
    ModelContext context;
    SimKernel kernel;
    std::ostream* trace;
    std::vector<Transmission> transmissions;
    std::map<NodeID, std::uint64_t> sequence;
    std::vector<std::unique_ptr<RNModel>> requesters;
    std::unique_ptr<HNModel> home;
    std::unique_ptr<SNModel> slave;
    std::unique_ptr<Workload> workload;
    bool started = false;

    static SimConfig Validated(const SimConfig& value) {
        value.Validate();
        return value;
    }
    Impl(const SimConfig& value, std::ostream* output)
        : config(Validated(value)),
          clock(config.period_ps),
          logger(clock, config.log),
          phase_trace(clock, nullptr),
          context{clock, logger, phase_trace},
          kernel(clock, logger, phase_trace, config.reverse_traversal),
          trace(output) {
        result.config = config;
        std::vector<NodeID> ids;
        for (NodeID id = 0; ids.size() < config.rn.count; ++id)
            if (id != 1 && id != 3)
                ids.push_back(id);
        HNConfig hn;
        hn.requester_ids = ids;
        hn.memory_size = config.workload.memory_size;
        hn.request_slots = config.hn.request_slots;
        hn.timeout_cycles = config.timeout_cycles;
        hn.grant_before_retry = config.hn.grant_before_retry;
        home = std::make_unique<HNModel>(ModelID{1}, "vip.hn", context, hn, Observe(1, "vip.hn"));
        SNConfig sn;
        sn.memory_size = config.workload.memory_size;
        sn.request_slots = config.sn.request_slots;
        sn.service_latency = config.sn.service_latency;
        sn.timeout_cycles = config.timeout_cycles;
        sn.grant_before_retry = config.sn.grant_before_retry;
        slave = std::make_unique<SNModel>(ModelID{3}, "vip.sn", context, sn, Observe(3, "vip.sn"));
        home->Port(3).Connect(slave->Port());
        std::vector<RN*> cores;
        for (unsigned index = 0; index < ids.size(); ++index) {
            RNConfig rn;
            rn.node_id = ids[index];
            rn.memory_size = config.workload.memory_size;
            rn.cache_lines = config.rn.cache_lines;
            rn.request_slots = config.rn.request_slots;
            rn.snoop_slots = config.rn.snoop_slots;
            rn.pending_operations = config.rn.pending_operations;
            rn.timeout_cycles = config.timeout_cycles;
            const auto path = "vip.rn" + std::to_string(index);
            auto model = std::make_unique<RNModel>(ModelID{ids[index]}, path, context, rn,
                                                   Observe(ids[index], path));
            model->Port().Connect(home->Port(ids[index]));
            cores.push_back(&model->Core());
            kernel.Add(*model);
            requesters.push_back(std::move(model));
        }
        // Management initialization is strictly before protocol execution.
        for (std::uint64_t address = 0; address < config.workload.memory_size; address += 64)
            slave->Core().Memory().Initialize(address, Pattern(address ^ config.workload.seed));
        workload = std::make_unique<Workload>(context, kernel, config, std::move(cores), result);
        kernel.Add(*home);
        kernel.Add(*slave);
        kernel.Add(*workload);
    }
    Observer Observe(NodeID node, const std::string& path) {
        return [this, node, path](const Observation& observation) {
            if (observation.action != "tx_sent" || !observation.flit)
                return;
            const char* channel = nullptr;
            int data_id = -1;
            if (std::holds_alternative<CHIReqFlit>(*observation.flit)) {
                ++result.req_flits;
                channel = "REQ";
            } else if (std::holds_alternative<CHIRspFlit>(*observation.flit)) {
                ++result.rsp_flits;
                channel = "RSP";
            } else if (std::holds_alternative<CHISnpFlit>(*observation.flit)) {
                ++result.snp_flits;
                channel = "SNP";
            } else {
                ++result.dat_flits;
                channel = "DAT";
                data_id = std::get<CHIDatFlit>(*observation.flit).flit_data_id;
            }
            if (trace) {
                std::ostringstream row;
                row << ToTimePs({observation.cycle}, config.period_ps).value << ','
                    << observation.cycle << ',' << path << ',' << channel << ',';
                std::visit(
                    [&](const auto& flit) { row << flit.flit_txn << ',' << flit.flit_opcode; },
                    *observation.flit);
                row << ',' << data_id << '\n';
                transmissions.push_back({observation.cycle, node, sequence[node]++, row.str()});
            }
        };
    }
    void WriteTrace() {
        if (!trace)
            return;
        std::stable_sort(transmissions.begin(), transmissions.end(),
                         [](const auto& a, const auto& b) {
                             return std::tie(a.cycle, a.node, a.sequence) <
                                    std::tie(b.cycle, b.node, b.sequence);
                         });
        *trace << "time_ps,cycle,model,channel,txn_id,opcode,data_id\n";
        for (const auto& transmission : transmissions)
            *trace << transmission.row;
        trace->flush();
        Require(bool(*trace), "trace output failure");
    }
};

System::System(const SimConfig& config, std::ostream* trace)
    : impl_(std::make_unique<Impl>(config, trace)) {}
System::~System() = default;
RunResult System::Run() {
    auto& r = *impl_;
    Require(!r.started, "system can only run once");
    r.started = true;
    try {
        r.workload->Start();
        r.kernel.Run(r.config.max_cycles);
        Require(r.home->IsIdle() && r.slave->IsIdle() &&
                    std::all_of(r.requesters.begin(), r.requesters.end(),
                                [](const auto& node) { return node->IsIdle(); }),
                "CHI nodes or ports did not drain");
        r.workload->CheckMemory(r.slave->Core().Memory());
        r.result.success = true;
    } catch (const std::exception& error) {
        r.result.error = error.what();
        r.result.diagnostics = r.kernel.DescribeState();
    }
    r.result.cycles = r.kernel.ExecutedCycles();
    r.result.last_time_ps = r.clock.NowPs().value;
    try {
        r.WriteTrace();
    } catch (const std::exception& error) {
        r.result.success = false;
        r.result.error += std::string(r.result.error.empty() ? "" : "\n") + error.what();
    }
    return r.result;
}
double RunResult::Throughput() const {
    return cycles ? static_cast<double>(completed) / static_cast<double>(cycles) : 0;
}
std::string RunResult::JSON() const {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(12)
         << "{\"model\":\"chi-vip-gbcore\",\"success\":" << (success ? "true" : "false")
         << ",\"error\":" << JsonString(error) << ",\"diagnostics\":" << JsonString(diagnostics)
         << ",\"completed\":" << completed << ",\"loads\":" << loads << ",\"stores\":" << stores
         << ",\"cycles\":" << cycles << ",\"last_time_ps\":" << last_time_ps
         << ",\"throughput\":" << Throughput() << ",\"nodes\":{\"rn\":" << config.rn.count
         << ",\"hn\":1,\"sn\":1},\"flits\":{\"req\":" << req_flits << ",\"rsp\":" << rsp_flits
         << ",\"snp\":" << snp_flits << ",\"dat\":" << dat_flits
         << "},\"configuration_toml\":" << JsonString(config.ToTOML()) << "}\n";
    return text.str();
}
}  // namespace esl::chi_vip
