#include <algorithm>
#include <limits>
#include <sstream>
#include <type_traits>

#include "rn_internal.hpp"

namespace esl::chi_vip::detail {
Runtime::Runtime(RNConfig c, Observer o)
    : config(c), observer(std::move(o)), request_engine(*this), snoop_engine(*this), lines(*this) {
    config.Validate();
    ValidateRuleTables();
    for (TxnID id = 0; id < 4096; ++id)
        ids.push_back(id);
}
Runtime::OrderKey Runtime::Key(const TxnRequest& req) const {
    return {req.stream_id, req.address / config.endpoint_size};
}
SubmitResult Runtime::Enqueue(JobKind kind, TxnRequest request, std::uint64_t mask) {
    Check(!working, ErrorKind::kLifecycle, "submit between Commit and BeginCycle only");
    if (external_count >= config.pending_operations)
        return {};
    const auto id = next_handle++;
    Job j{id, kind, request, LineAddress(request.address), mask, false};
    jobs.emplace(id, j);
    ++external_count;
    auto& q = line_jobs[j.line];
    q.push_back(id);
    if (q.size() == 1)
        ReadyJob(id);
    if (request.order)
        ordered[Key(request)].push_back(id);
    if (config.timeout_cycles)
        timers.emplace(cycle + config.timeout_cycles, id);
    return {true, id};
}
void Runtime::ReadyJob(Handle id) {
    if (jobs.count(id) && ready_job_set.insert(id).second)
        ready_jobs.push_back(id);
}
void Runtime::ReadyRequest(Handle id) {
    if (requests.count(id) && ready_request_set.insert(id).second)
        ready_requests.push_back(id);
}
void Runtime::ReadySnoop(Handle id) {
    if (snoops.count(id) && ready_snoop_set.insert(id).second)
        ready_snoops.push_back(id);
}
void Runtime::WaitResource(Handle id) {
    if (resource_set.insert(id).second)
        resource_waiters.push_back(id);
}
void Runtime::Observe(std::string action, Handle id, std::uint64_t address, std::string rule,
                      std::optional<Flit> flit) {
    Observation o;
    o.cycle = cycle;
    o.handle = id;
    o.address = address;
    o.action = std::move(action);
    o.rule = std::move(rule);
    o.flit = std::move(flit);
    observations.push_back(std::move(o));
}
void Runtime::Emit(Owner owner, Handle id, Flit flit, OutputKind kind, unsigned generation) {
    NodeID destination = std::visit(
        [](const auto& f) -> NodeID {
            using T = std::decay_t<decltype(f)>;
            if constexpr (std::is_same_v<T, CHISnpFlit>)
                return 0;
            else
                return f.flit_tgt;
        },
        flit);
    SendAction action{next_token++, destination, std::move(flit)};
    outputs.emplace(action.token, Output{action, owner, id, generation, kind, false});
    actions.push_back(action);
    if (owner == Owner::kRequest)
        ++requests.at(id).in_flight;
    if (owner == Owner::kSnoop)
        ++snoops.at(id).in_flight;
}
void Runtime::ProcessEvent(const Event& e) {
    if (e.kind != EventKind::kReceive) {
        auto it = outputs.find(e.token);
        Check(it != outputs.end(), ErrorKind::kProtocolViolation, "unknown/repeated send token");
        if (e.kind == EventKind::kTxAccepted) {
            Check(!it->second.accepted, ErrorKind::kProtocolViolation, "duplicate TX acceptance");
            it->second.accepted = true;
            Observe("tx_accepted", it->second.id, 0, {}, it->second.action.flit);
        } else {
            Check(it->second.accepted, ErrorKind::kProtocolViolation, "TX sent before acceptance");
            const auto output = it->second;
            outputs.erase(it);
            Observe("tx_sent", output.id, 0, {}, output.action.flit);
            if (e.observed_cycle)
                observations.back().cycle = *e.observed_cycle;
            if (output.owner == Owner::kRequest)
                request_engine.Sent(output);
            if (output.owner == Owner::kSnoop)
                snoop_engine.Sent(output);
        }
        return;
    }
    Observe("rx", 0, 0, {}, e.flit);
    std::visit(
        [this](const auto& f) {
            using T = std::decay_t<decltype(f)>;
            if constexpr (std::is_same_v<T, CHIRspFlit> || std::is_same_v<T, CHIDatFlit>)
                request_engine.Receive(f);
            else if constexpr (std::is_same_v<T, CHISnpFlit>) {
                --queued_snoops;
                snoop_engine.Receive(f);
            } else
                throw VIPError(ErrorKind::kProtocolViolation, "RN cannot receive REQ");
        },
        e.flit);
}
void Runtime::Run() {
    // Stable priority is independent of the platform's model traversal order.
    auto priority = [](const Event& e) {
        if (e.kind == EventKind::kTxAccepted)
            return 0;
        if (e.kind == EventKind::kTxSent)
            return 1;
        if (std::holds_alternative<CHIRspFlit>(e.flit))
            return 2;
        if (std::holds_alternative<CHISnpFlit>(e.flit))
            return 3;
        return 4;
    };
    std::stable_sort(events.begin(), events.end(),
                     [&](const Event& a, const Event& b) { return priority(a) < priority(b); });
    while (!events.empty()) {
        auto e = events.front();
        events.pop_front();
        ProcessEvent(e);
    }
    while (!ready_snoops.empty() || !ready_requests.empty() || !ready_jobs.empty()) {
        if (!ready_snoops.empty()) {
            auto id = ready_snoops.front();
            ready_snoops.pop_front();
            ready_snoop_set.erase(id);
            if (snoops.count(id))
                snoop_engine.Step(id);
        } else if (!ready_requests.empty()) {
            auto id = ready_requests.front();
            ready_requests.pop_front();
            ready_request_set.erase(id);
            if (requests.count(id))
                request_engine.Step(id);
        } else {
            auto id = ready_jobs.front();
            ready_jobs.pop_front();
            ready_job_set.erase(id);
            if (jobs.count(id))
                lines.Step(id);
        }
    }
    request_engine.ReturnCredits();
    CheckTimeouts();
}
void Runtime::CheckTimeouts() {
    while (!snoop_timers.empty()) {
        const auto [due, id] = snoop_timers.top();
        if (!snoops.count(id)) {
            snoop_timers.pop();
            continue;
        }
        if (due > cycle)
            break;
        snoop_timers.pop();
        throw VIPError(ErrorKind::kTimeout,
                       "diagnostic snoop timeout, context=" + std::to_string(id));
    }
    while (!timers.empty()) {
        auto [due, id] = timers.top();
        if (!jobs.count(id)) {
            timers.pop();
            continue;
        }
        if (due > cycle)
            break;
        timers.pop();
        throw VIPError(ErrorKind::kTimeout,
                       "diagnostic timeout, logical handle=" + std::to_string(id) + " line=" +
                           std::to_string(jobs.at(id).line) + "; transaction remains live");
    }
}
CacheLine LineCoordinator::Get(std::uint64_t line) const {
    const auto it = r_.cache.find(line);
    return it == r_.cache.end() ? CacheLine{} : it->second;
}
void LineCoordinator::Put(std::uint64_t line, const CacheLine& value, const std::string& rule,
                          Handle id) {
    const auto old = Get(line);
    Check(value.state != CacheState::kI || (!value.valid && !value.dirty),
          ErrorKind::kProtocolViolation, "invalid cache invariant I");
    Check(!(value.dirty & ~value.valid) && !(value.errors & ~value.valid),
          ErrorKind::kProtocolViolation, "invalid cache byte masks");
    Check(IsDirty(value.state) || !value.dirty, ErrorKind::kProtocolViolation,
          "clean state with dirty responsibility");
    if (value.state != CacheState::kI || r_.cache.count(line)) {
        r_.cache[line] = value;
        r_.changed.insert(line);
    }
    r_.Observe("cache", id, line, rule);
    auto& o = r_.observations.back();
    o.before = old.state;
    o.after = value.state;
    o.data = value.data;
    o.mask = value.valid;
}
void LineCoordinator::Invalidate(std::uint64_t line, const std::string& rule, Handle id) {
    Put(line, CacheLine{}, rule, id);
}
bool LineCoordinator::SnoopBusy(std::uint64_t line) const {
    return r_.line_snoops.count(line) != 0;
}
void LineCoordinator::WakeResources() {
    auto waiters = std::move(r_.resource_waiters);
    r_.resource_waiters.clear();
    r_.resource_set.clear();
    for (auto id : waiters)
        r_.ReadyJob(id);
}
bool LineCoordinator::Reserve(std::uint64_t line, Handle waiter) {
    if (r_.cache.count(line))
        return true;
    if (r_.cache.size() < r_.config.cache_lines) {
        r_.cache.emplace(line, CacheLine{});
        r_.changed.insert(line);
        return true;
    }
    auto victim = r_.cache.end();
    for (auto it = r_.cache.begin(); it != r_.cache.end(); ++it) {
        if (r_.line_jobs.count(it->first) || SnoopBusy(it->first))
            continue;
        if (victim == r_.cache.end() || it->second.lru < victim->second.lru)
            victim = it;
    }
    if (victim != r_.cache.end() && victim->second.state == CacheState::kI) {
        r_.changed.insert(victim->first);
        r_.cache.erase(victim);
        r_.cache.emplace(line, CacheLine{});
        r_.changed.insert(line);
        return true;
    }
    if (victim != r_.cache.end() && !r_.maintenance_count) {
        TxnRequest req;
        req.address = victim->first;
        const auto state = victim->second.state;
        req.opcode = state == CacheState::kUDP ? CHI_REQ_Opcode_WriteBackPtl
                     : IsDirty(state)          ? CHI_REQ_Opcode_WriteBackFull
                                               : CHI_REQ_Opcode_Evict;
        const auto id = r_.next_handle++;
        Job j{id, JobKind::kReplacement, req, req.address, 0, false};
        r_.jobs.emplace(id, j);
        r_.line_jobs[req.address].push_back(id);
        if (r_.config.timeout_cycles)
            r_.timers.emplace(r_.cycle + r_.config.timeout_cycles, id);
        r_.request_engine.Start(r_.jobs.at(id), req);
    }
    r_.WaitResource(waiter);
    return false;
}
void LineCoordinator::Step(Handle id) {
    auto& job = r_.jobs.at(id);
    if (job.wire || r_.line_jobs.at(job.line).front() != id || SnoopBusy(job.line))
        return;
    auto line = Get(job.line);
    auto req = job.request;
    if (job.kind == JobKind::kLoad && (line.valid & job.mask) == job.mask) {
        line.lru = ++r_.lru;
        Put(job.line, line, "RN.LOCAL.LOAD", id);
        Completion c;
        c.data = line.data;
        c.valid = job.mask;
        c.state = line.state;
        c.error = (line.errors & job.mask) ? AccessError::kDataError : AccessError::kOK;
        Finish(id, c);
        return;
    }
    if (job.kind == JobKind::kStore && IsUnique(line.state)) {
        for (unsigned i = 0; i < 64; ++i)
            if ((job.mask >> i) & 1)
                line.data[i] = req.data[i];
        line.valid |= job.mask;
        line.dirty |= job.mask;
        line.errors &= ~job.mask;
        line.state = line.valid == kAllBytes ? CacheState::kUD : CacheState::kUDP;
        line.cah = false;
        line.lru = ++r_.lru;
        Put(job.line, line, "RN.LOCAL.STORE", id);
        r_.Observe("store", id, job.line, "RN.LOCAL.STORE");
        r_.observations.back().data = req.data;
        r_.observations.back().mask = job.mask;
        Completion c;
        c.data = line.data;
        c.valid = job.mask;
        c.state = line.state;
        Finish(id, c);
        return;
    }
    if (job.kind == JobKind::kLoad) {
        req = TxnRequest{};
        req.address = job.line;
        req.opcode = line.state == CacheState::kUDP ? CHI_REQ_Opcode_WriteBackPtl
                                                    : CHI_REQ_Opcode_ReadShared;
    }
    if (job.kind == JobKind::kStore) {
        req = TxnRequest{};
        req.address = job.line;
        req.opcode =
            line.state == CacheState::kI ? CHI_REQ_Opcode_ReadUnique : CHI_REQ_Opcode_CleanUnique;
    }
    if (req.order && r_.ordered.at(r_.Key(req)).front() != id)
        return;
    if (job.kind != JobKind::kReplacement && r_.normal_count >= r_.config.request_slots) {
        r_.WaitResource(id);
        return;
    }
    ValidateStart(req.opcode, line.state);
    if (Traits(req.opcode).allocating && !Reserve(job.line, id))
        return;
    r_.request_engine.Start(job, req);
}
void LineCoordinator::Finish(Handle id, Completion result) {
    const auto job = r_.jobs.at(id);
    result.handle = id;
    result.cycle = r_.cycle;
    auto& queue = r_.line_jobs.at(job.line);
    Check(queue.front() == id, ErrorKind::kLifecycle, "line FIFO violated");
    queue.pop_front();
    if (queue.empty())
        r_.line_jobs.erase(job.line);
    else
        r_.ReadyJob(queue.front());
    if (job.kind == JobKind::kReplacement) {
        r_.cache.erase(job.line);
        r_.changed.insert(job.line);
    } else {
        --r_.external_count;
        r_.staged_completions.push_back(result);
        r_.Observe("complete", id, job.line);
    }
    r_.jobs.erase(id);
    WakeResources();
}
}  // namespace esl::chi_vip::detail

namespace esl::chi_vip {
struct RN::Impl {
    detail::Runtime runtime;
    Impl(RNConfig c, Observer o) : runtime(c, std::move(o)) {}
};
RN::RN(RNConfig c, Observer o) : impl_(std::make_unique<Impl>(c, std::move(o))) {}
RN::~RN() = default;
SubmitResult RN::Submit(const TxnRequest& request) {
    ValidateRequest(request, impl_->runtime.config);
    return impl_->runtime.Enqueue(detail::JobKind::kSubmit, request, request.byte_enable);
}
SubmitResult RN::Load(std::uint64_t address, unsigned size) {
    auto& r = impl_->runtime;
    Check(address < r.config.memory_size, ErrorKind::kInvalidRequest, "Load address out of range");
    auto mask = ByteMask(static_cast<unsigned>(address & 63), size);
    TxnRequest req;
    req.address = address;
    return r.Enqueue(detail::JobKind::kLoad, req, mask);
}
SubmitResult RN::Store(std::uint64_t address, const Line& data, std::uint64_t mask) {
    auto& r = impl_->runtime;
    Check(address < r.config.memory_size && mask, ErrorKind::kInvalidRequest,
          "invalid Store range/mask");
    TxnRequest req;
    req.address = address;
    req.data = data;
    req.byte_enable = mask;
    return r.Enqueue(detail::JobKind::kStore, req, mask);
}
void RN::OnEvent(const Event& e) {
    auto& r = impl_->runtime;
    Check(!r.working, ErrorKind::kLifecycle, "enqueue events between Commit and BeginCycle");
    if (e.kind == EventKind::kReceive && std::holds_alternative<CHISnpFlit>(e.flit)) {
        Check(CanReceiveSnoop(), ErrorKind::kProtocolViolation, "snoop receive capacity exceeded");
        ++r.queued_snoops;
    }
    r.events.push_back(e);
}
bool RN::CanReceiveSnoop() const {
    const auto& r = impl_->runtime;
    return r.snoops.size() + r.queued_snoops < r.config.snoop_slots;
}
void RN::BeginCycle(std::uint64_t cycle) {
    auto& r = impl_->runtime;
    Check(!r.working && (!r.started || cycle > r.cycle), ErrorKind::kLifecycle,
          "invalid RN cycle/phase");
    r.cycle = cycle;
    r.started = true;
    r.working = true;
    r.Run();
}
std::vector<SendAction> RN::TakeActions() {
    auto& r = impl_->runtime;
    Check(r.working, ErrorKind::kLifecycle, "TakeActions outside Work");
    auto result = std::move(r.actions);
    r.actions.clear();
    return result;
}
void RN::Commit() {
    auto& r = impl_->runtime;
    Check(r.working, ErrorKind::kLifecycle, "Commit without BeginCycle");
    for (auto line : r.changed) {
        auto it = r.cache.find(line);
        if (it == r.cache.end())
            r.committed_cache.erase(line);
        else
            r.committed_cache[line] = it->second;
    }
    r.changed.clear();
    for (const auto& c : r.staged_completions)
        r.completions.push_back(c);
    r.staged_completions.clear();
    r.working = false;
    auto observations = std::move(r.observations);
    r.observations.clear();
    if (r.observer)
        for (const auto& o : observations)
            r.observer(o);
}
bool RN::PollCompletion(Completion& c) {
    auto& q = impl_->runtime.completions;
    if (q.empty())
        return false;
    c = q.front();
    q.pop_front();
    return true;
}
std::optional<CacheLine> RN::InspectLine(std::uint64_t address) const {
    const auto& cache = impl_->runtime.committed_cache;
    auto it = cache.find(LineAddress(address));
    if (it == cache.end())
        return {};
    return it->second;
}
bool RN::IsIdle() const {
    const auto& r = impl_->runtime;
    return !r.working && r.jobs.empty() && r.snoops.empty() && r.events.empty() &&
           r.outputs.empty() && r.actions.empty() && r.credits.empty() &&
           r.remote_buffers.empty() && r.completions.empty() && r.staged_completions.empty();
}
std::string RN::DescribeState() const {
    const auto& r = impl_->runtime;
    std::ostringstream out;
    out << "RN " << r.config.node_id << " jobs=" << r.jobs.size()
        << " requests=" << r.requests.size() << " snoops=" << r.snoops.size()
        << " sends=" << r.outputs.size() << " completions=" << r.completions.size();
    for (const auto& [id, c] : r.requests)
        out << " [handle=" << id << " txn=" << c.wire.flit_txn
            << " line=" << LineAddress(c.request.address) << " generation=" << c.generation
            << " sent=" << c.req_sent << " credit_wait=" << c.credit_wait << " comp=" << c.comp
            << " dbid=" << c.dbid_received << " beats=" << c.received
            << " tx_pending=" << c.in_flight << ']';
    for (const auto& [id, job] : r.jobs) {
        if (job.wire)
            continue;
        const char* wait = r.line_jobs.at(job.line).front() != id ? "same-line predecessor"
                           : r.lines.SnoopBusy(job.line)          ? "snoop response"
                           : job.request.order && r.ordered.count(r.Key(job.request)) &&
                                   r.ordered.at(r.Key(job.request)).front() != id
                               ? "ordering acceptance"
                               : "ready/cache/transaction slot";
        out << " [handle=" << id << " line=" << job.line << " wait=" << wait << ']';
    }
    return out.str();
}
const RNConfig& RN::Config() const {
    return impl_->runtime.config;
}
}  // namespace esl::chi_vip
