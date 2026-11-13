#pragma once

#include <deque>
#include <map>
#include <queue>
#include <set>
#include <tuple>

#include "rn.hpp"
#include "rules.hpp"

namespace esl::chi_vip::detail {
enum class JobKind { kSubmit, kLoad, kStore, kReplacement };
struct Job {
    Handle id = 0;
    JobKind kind = JobKind::kSubmit;
    TxnRequest request;
    std::uint64_t line = 0;
    std::uint64_t mask = 0;
    bool wire = false;
};
struct RequestContext {
    Handle id = 0;
    TxnRequest request;
    CHIReqFlit wire;
    unsigned generation = 0;
    unsigned received = 0;
    Line data{};
    std::uint64_t valid = 0, errors = 0;
    std::optional<CHIDatFlit> first_data;
    AccessError error = AccessError::kOK;
    DBID dbid = 0;
    std::optional<DBID> completion_dbid;
    NodeID responder = 0;
    unsigned resp = 0;
    unsigned in_flight = 0;
    bool req_sent = false, retry = false, credit_wait = false;
    bool comp = false, dbid_received = false, receipt = false;
    bool applied = false, terminal_issued = false, order_released = false;
    bool replacement = false;
    bool buffer_claimed = false;
};
struct SnoopContext {
    Handle id = 0;
    CHISnpFlit request;
    std::uint64_t line = 0;
    unsigned in_flight = 0;
    bool issued = false, waiting_fill = false;
};
enum class Owner { kRequest, kSnoop, kCredit };
enum class OutputKind { kRequest, kTerminal, kCredit };
struct Output {
    SendAction action;
    Owner owner;
    Handle id;
    unsigned generation;
    OutputKind kind;
    bool accepted = false;
};
struct Runtime;
class RequestEngine {
public:
    explicit RequestEngine(Runtime& runtime) : r_(runtime) {}
    void Start(Job& job, const TxnRequest& request);
    void Receive(const CHIRspFlit& flit);
    void Receive(const CHIDatFlit& flit);
    void Step(Handle id);
    void Sent(const Output& output);
    void Grant(NodeID node, unsigned type);
    void ReturnCredits();

private:
    Runtime& r_;
    RequestContext& Lookup(TxnID id);
    void EmitRequest(RequestContext& c);
    void ReleaseOrder(RequestContext& c);
    void Read(RequestContext& c);
    void ImmediateWrite(RequestContext& c);
    void CopyBack(RequestContext& c);
    void Dataless(RequestContext& c);
    void Ack(RequestContext& c, unsigned resp = 0);
    void Data(RequestContext& c, const CacheLine& snapshot, bool copy_back);
    void ClaimBuffer(RequestContext& c);
    void Finish(Handle id);
};
class SnoopEngine {
public:
    explicit SnoopEngine(Runtime& runtime) : r_(runtime) {}
    void Receive(const CHISnpFlit& flit);
    void Step(Handle id);
    void Sent(const Output& output);
    void WakeLine(std::uint64_t line);

private:
    Runtime& r_;
};
// All mutation of a cache line goes through this coordinator. Request ownership
// serializes local operations, not the independent snoop response path.
class LineCoordinator {
public:
    explicit LineCoordinator(Runtime& runtime) : r_(runtime) {}
    CacheLine Get(std::uint64_t line) const;
    void Put(std::uint64_t line, const CacheLine& value, const std::string& rule, Handle id = 0);
    void Invalidate(std::uint64_t line, const std::string& rule, Handle id = 0);
    bool Reserve(std::uint64_t line, Handle waiter);
    void Step(Handle id);
    void Finish(Handle id, Completion result);
    void WakeResources();
    bool SnoopBusy(std::uint64_t line) const;

private:
    Runtime& r_;
};
struct Runtime {
    RNConfig config;
    Observer observer;
    std::uint64_t cycle = 0, lru = 0;
    bool working = false, started = false;
    Handle next_handle = 1, next_snoop = 1;
    Token next_token = 1;
    unsigned normal_count = 0, maintenance_count = 0, external_count = 0, queued_snoops = 0;
    std::deque<Event> events;
    std::map<Handle, Job> jobs;
    std::map<Handle, RequestContext> requests;
    std::map<TxnID, Handle> txn_map;
    std::map<std::pair<NodeID, DBID>, Handle> remote_buffers;
    std::deque<TxnID> ids;
    std::map<Handle, SnoopContext> snoops;
    std::set<std::pair<NodeID, TxnID>> snoop_ids;
    std::map<std::uint64_t, std::deque<Handle>> line_jobs, line_snoops;
    std::map<std::uint64_t, CacheLine> cache, committed_cache;
    std::set<std::uint64_t> changed;
    std::map<Token, Output> outputs;
    std::vector<SendAction> actions;
    std::deque<Completion> completions;
    std::vector<Completion> staged_completions;
    std::vector<Observation> observations;
    std::deque<Handle> ready_jobs, ready_requests, ready_snoops, resource_waiters;
    std::set<Handle> ready_job_set, ready_request_set, ready_snoop_set, resource_set;
    // Endpoint order waits for request acceptance, not completion. Request Order
    // is already stronger within each line's operation FIFO.
    using OrderKey = std::pair<unsigned, std::uint64_t>;
    std::map<OrderKey, std::deque<Handle>> ordered;
    using CreditKey = std::pair<NodeID, unsigned>;
    std::map<CreditKey, unsigned> credits;
    std::map<CreditKey, std::deque<Handle>> credit_waiters;
    using Timer = std::pair<std::uint64_t, Handle>;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timers;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> snoop_timers;
    RequestEngine request_engine;
    SnoopEngine snoop_engine;
    LineCoordinator lines;
    explicit Runtime(RNConfig c, Observer o);
    SubmitResult Enqueue(JobKind kind, TxnRequest request, std::uint64_t mask);
    void ReadyJob(Handle id);
    void ReadyRequest(Handle id);
    void ReadySnoop(Handle id);
    void WaitResource(Handle id);
    void Observe(std::string action, Handle id, std::uint64_t address, std::string rule = {},
                 std::optional<Flit> flit = {});
    void Emit(Owner owner, Handle id, Flit flit, OutputKind kind, unsigned generation = 0);
    void ProcessEvent(const Event& event);
    void Run();
    void CheckTimeouts();
    OrderKey Key(const TxnRequest& req) const;
};
void ValidateResponseFields(const CHIRspFlit& f, const RNConfig& c);
void ValidateDataFields(const CHIDatFlit& f, const RNConfig& c);
void MergeError(AccessError& target, unsigned resp_err);
}  // namespace esl::chi_vip::detail
