#include <algorithm>
#include <deque>
#include <iostream>
#include <random>
#include <set>

#include "sn.hpp"

namespace {
using namespace esl;
using namespace esl::chi_vip;
void Expect(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F>
void Throws(ErrorKind kind, F fn) {
    try {
        fn();
    } catch (const VIPError& e) {
        Expect(e.Kind() == kind, "wrong diagnostic: " + std::string(e.what()));
        return;
    }
    throw std::runtime_error("expected diagnostic missing");
}
Line Pattern(unsigned seed) {
    Line line{};
    for (unsigned n = 0; n < 64; ++n)
        line[n] = static_cast<std::uint8_t>(seed * 13 + n * 7);
    return line;
}
CHIReqFlit Request(unsigned op, TxnID txn = 1, std::uint64_t address = 0, unsigned size = 6) {
    CHIReqFlit f;
    f.flit_opcode = op;
    f.flit_src = 1;
    f.flit_tgt = 3;
    f.flit_txn = txn;
    f.flit_return_nid = 1;
    f.flit_return_txn_id = txn;
    f.flit_address = address;
    f.flit_size = size;
    f.flit_ns = 1;
    f.flit_mem_attr = 5;
    f.flit_order = 1;
    f.flit_allow_retry = 1;
    return f;
}
// Independent wire encoder/decoder: no production Pack/Unpack or SN state query.
CHIDatFlit Data(DBID dbid, const Line& line, std::uint64_t mask, unsigned id, unsigned ccid = 0,
                unsigned error = 0) {
    CHIDatFlit f;
    f.flit_opcode = CHI_DAT_Opcode_NonCopyBackWriteData;
    f.flit_src = 1;
    f.flit_tgt = 3;
    f.flit_txn = dbid;
    f.flit_data_id = id;
    f.flit_ccid = ccid;
    f.flit_resp_err = error;
    f.flit_be = (mask >> (id * 16)) & 0xffffffffu;
    for (unsigned n = 0; n < 32; ++n)
        if ((f.flit_be >> n) & 1)
            f.flit_data[n / 4] |= std::uint32_t(line[id * 16 + n]) << ((n % 4) * 8);
    return f;
}
struct Harness {
    SN sn;
    std::uint64_t cycle = 0;
    std::vector<SendAction> sent;
    std::deque<SendAction> pending;
    std::set<Token> accepted;
    bool hold = false;
    explicit Harness(SNConfig config = {}) : sn(config) {}
    void Rx(Flit flit) {
        sn.OnEvent(Event::Received(std::move(flit)));
    }
    void Transmit() {
        while (!pending.empty()) {
            auto action = std::move(pending.front());
            pending.pop_front();
            sent.push_back(action);
            if (accepted.insert(action.token).second)
                sn.OnEvent(Event::Accepted(action.token));
            sn.OnEvent(Event::Sent(action.token, cycle - 1));
        }
    }
    void AcceptPending() {
        for (const auto& action : pending)
            if (accepted.insert(action.token).second)
                sn.OnEvent(Event::Accepted(action.token));
    }
    void Tick() {
        sn.BeginCycle(cycle++);
        for (auto& action : sn.TakeActions())
            pending.push_back(std::move(action));
        sn.Commit();
        if (!hold)
            Transmit();
    }
    template <class F>
    void Until(F condition, unsigned limit = 500) {
        for (unsigned n = 0; n < limit && !condition(); ++n)
            Tick();
        Expect(condition(), "SN made no progress: " + sn.DescribeState());
    }
    unsigned Count(unsigned opcode) const {
        unsigned result = 0;
        for (const auto& action : sent)
            if (const auto* f = std::get_if<CHIRspFlit>(&action.flit))
                result += f->flit_opcode == opcode;
        return result;
    }
    CHIRspFlit Last(unsigned opcode) const {
        for (auto i = sent.rbegin(); i != sent.rend(); ++i)
            if (const auto* f = std::get_if<CHIRspFlit>(&i->flit))
                if (f->flit_opcode == opcode)
                    return *f;
        throw std::runtime_error("response absent");
    }
    DBID WriteRequest(CHIReqFlit q) {
        const auto previous = Count(CHI_RSP_Opcode_DBIDResp);
        Rx(q);
        Until([&] { return Count(CHI_RSP_Opcode_DBIDResp) != previous; });
        return Last(CHI_RSP_Opcode_DBIDResp).flit_dbid;
    }
    void Drain() {
        Until([&] { return sn.IsIdle(); });
    }
};
void MemoryBackendTest() {
    ByteMemoryBackend memory(128, 4);
    const auto initial = Pattern(1);
    const auto changed = Pattern(2);
    memory.Initialize(0, initial);
    memory.SubmitWrite(4, 0, changed, 0x8000000000000001ull, 1, 2);
    Expect(memory.Advance(5).empty(), "memory completed before configured latency");
    auto done = memory.Advance(6);
    Expect(done.size() == 1 && done[0].handle == 4, "async completion handle mismatch");
    Expect(memory.Read(0) == initial, "memory mutation escaped before Commit");
    memory.Commit();
    auto expected = initial;
    expected[0] = changed[0];
    expected[63] = changed[63];
    Expect(memory.Read(0) == expected && memory.Errors(0) == 1, "masked write/error merge failed");
    memory.SubmitRead(5, 0, 7);
    done = memory.Advance(11);
    Expect(done.size() == 1 && done[0].data == expected && done[0].errors == 1,
           "asynchronous read lost data or errors");
    memory.Commit();
    memory.Initialize(0, initial, 1);
    Expect(memory.Errors(0) == 0 && memory.IsIdle(),
           "initialization did not clear selected errors");
}
void BasicTest() {
    Harness h;
    auto expected = Pattern(4);
    h.sn.Memory().Initialize(0, expected);
    h.Rx(Request(CHI_REQ_Opcode_ReadNoSnp));
    h.Drain();
    Expect(h.Count(CHI_RSP_Opcode_ReadReceipt) == 1, "Request Accepted read missed ReadReceipt");
    Line received{};
    unsigned beats = 0;
    for (const auto& a : h.sent) {
        if (const auto* f = std::get_if<CHIDatFlit>(&a.flit)) {
            Expect(f->flit_src == 3 && f->flit_home_nid == 3 && f->flit_tgt == 1 &&
                       f->flit_txn == 1 && f->flit_resp == 0 && f->flit_resp_err == 0,
                   "read response mapping incorrect");
            for (unsigned n = 0; n < 32; ++n)
                received[f->flit_data_id * 16 + n] =
                    static_cast<std::uint8_t>(f->flit_data[n / 4] >> ((n % 4) * 8));
            ++beats;
        }
    }
    Expect(beats == 2 && received == expected, "read data did not reconstruct initial memory");
    const auto replacement = Pattern(7);
    const auto dbid = h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpFull, 2));
    h.Rx(Data(dbid, replacement, kAllBytes, 2));
    h.Rx(Data(dbid, replacement, kAllBytes, 0));
    h.Drain();
    Expect(h.sn.Memory().Read(0) == replacement, "out of order Full write data failed");
    const auto comp = h.Last(CHI_RSP_Opcode_Comp);
    Expect(comp.flit_txn == 2 && comp.flit_dbid == dbid && !comp.flit_resp_err,
           "write Comp mapping incorrect");
    const auto partial = Pattern(9);
    const std::uint64_t mask = 0x80aa000010000001ull;
    const auto partial_dbid = h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpPtl, 3));
    h.Rx(Data(partial_dbid, partial, mask, 0));
    h.Rx(Data(partial_dbid, partial, mask, 2));
    h.Drain();
    expected = replacement;
    for (unsigned n = 0; n < 64; ++n)
        if ((mask >> n) & 1)
            expected[n] = partial[n];
    Expect(h.sn.Memory().Read(0) == expected, "Partial write damaged disabled bytes");
    const auto narrow = h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpPtl, 4, 39, 2));
    h.Rx(Data(narrow, partial, std::uint64_t{1} << 38, 2, 2));
    h.Drain();
    expected[38] = partial[38];
    Expect(h.sn.Memory().Read(0) == expected, "Normal narrow access did not align its data window");
    const auto critical = h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpFull, 5, 19));
    h.Rx(Data(critical, replacement, kAllBytes, 0, 1));
    h.Rx(Data(critical, replacement, kAllBytes, 2, 1));
    h.Drain();
    Expect(h.sn.Memory().Read(0) == replacement,
           "Normal Full write incorrectly treated critical-chunk address as a byte offset");
}
void ErrorTest() {
    Harness h;
    const auto initial = Pattern(1);
    const auto replacement = Pattern(2);
    h.sn.Memory().Initialize(0, initial);
    auto dbid = h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpPtl));
    h.Rx(Data(dbid, replacement, 0x8000000100000001ull, 0, 0, 2));
    h.Rx(Data(dbid, replacement, 0x8000000100000001ull, 2));
    h.Drain();
    Expect(h.sn.Memory().Errors(0) == 1 && h.Last(CHI_RSP_Opcode_Comp).flit_resp_err == 2,
           "DERR write did not retain byte error information");
    h.sent.clear();
    h.Rx(Request(CHI_REQ_Opcode_ReadNoSnp, 2));
    h.Drain();
    unsigned derr = 0, okay = 0;
    for (const auto& action : h.sent)
        if (const auto* f = std::get_if<CHIDatFlit>(&action.flit)) {
            derr += f->flit_resp_err == 2;
            okay += f->flit_resp_err == 0;
        }
    Expect(derr == 1 && okay == 1, "read did not preserve per-packet DERR");
    const auto before = h.sn.Memory().Read(0);
    dbid = h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpFull, 3));
    h.Rx(Data(dbid, initial, kAllBytes, 0, 0, 3));
    h.Rx(Data(dbid, initial, kAllBytes, 2, 0, 3));
    h.Drain();
    Expect(h.sn.Memory().Read(0) == before && h.Last(CHI_RSP_Opcode_Comp).flit_resp_err == 3,
           "NDERR wrote invalid data or did not complete");
}
void RetryTest(bool grant_first, bool return_credit) {
    SNConfig config;
    config.request_slots = 1;
    config.grant_before_retry = grant_first;
    Harness h(config);
    h.hold = true;
    h.Rx(Request(CHI_REQ_Opcode_ReadNoSnp));
    for (unsigned n = 0; n < 10; ++n)
        h.Tick();
    h.AcceptPending();
    h.Tick();
    Expect(!h.sn.IsIdle(), "request retired before actual DAT transmission");
    auto second = Request(CHI_REQ_Opcode_ReadNoSnp, 2, 64);
    h.Rx(second);
    h.Tick();
    Expect(h.Count(CHI_RSP_Opcode_PCrdGrant) == 0, "credit granted over an occupied request slot");
    h.hold = false;
    h.Transmit();
    h.Until([&] {
        return h.Count(CHI_RSP_Opcode_PCrdGrant) == 1 && h.Count(CHI_RSP_Opcode_RetryAck) == 1;
    });
    std::vector<unsigned> credit_order;
    for (const auto& action : h.sent)
        if (const auto* f = std::get_if<CHIRspFlit>(&action.flit))
            if (f->flit_opcode == CHI_RSP_Opcode_PCrdGrant ||
                f->flit_opcode == CHI_RSP_Opcode_RetryAck) {
                Expect(f->flit_pcrd_type == 1, "retry and grant credit types differ");
                credit_order.push_back(f->flit_opcode);
            }
    Expect(
        credit_order.front() == (grant_first ? CHI_RSP_Opcode_PCrdGrant : CHI_RSP_Opcode_RetryAck),
        "configured retry/grant order was not emitted");
    Expect(!h.sn.IsIdle(), "unconsumed protocol credit incorrectly considered idle");
    if (return_credit) {
        CHIReqFlit returned;
        returned.flit_src = 1;
        returned.flit_tgt = 3;
        returned.flit_opcode = CHI_REQ_Opcode_PCrdReturn;
        returned.flit_pcrd_type = 1;
        h.Rx(returned);
    } else {
        second.flit_txn = 17;  // P-Credit is not tied to the original wire TxnID.
        second.flit_return_txn_id = 17;
        second.flit_allow_retry = 0;
        second.flit_pcrd_type = 1;
        h.Rx(second);
    }
    h.Drain();
    Expect(h.Count(CHI_RSP_Opcode_RetryAck) == 1, "reserved retry was retried again");
}
void OrderingTest() {
    SNConfig config;
    config.request_slots = 3;
    Harness h(config);
    const auto data = Pattern(18);
    h.sn.Memory().Initialize(0, Pattern(1));
    const auto dbid = h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpFull));
    h.Rx(Request(CHI_REQ_Opcode_ReadNoSnp, 2));
    h.Rx(Request(CHI_REQ_Opcode_ReadNoSnp, 3, 64));
    for (unsigned n = 0; n < 12; ++n)
        h.Tick();
    bool other_line_done = false;
    for (const auto& action : h.sent)
        if (const auto* f = std::get_if<CHIDatFlit>(&action.flit)) {
            Expect(f->flit_txn != 2, "same-line read passed an earlier incomplete write");
            other_line_done |= f->flit_txn == 3;
        }
    Expect(other_line_done, "blocked line prevented unrelated memory service");
    h.Rx(Data(dbid, data, kAllBytes, 2));
    h.Rx(Data(dbid, data, kAllBytes, 0));
    h.Drain();
    unsigned received = 0;
    for (const auto& action : h.sent)
        if (const auto* f = std::get_if<CHIDatFlit>(&action.flit))
            if (f->flit_txn == 2) {
                for (unsigned n = 0; n < 32; ++n)
                    Expect(static_cast<std::uint8_t>(f->flit_data[n / 4] >> ((n % 4) * 8)) ==
                               data[f->flit_data_id * 16 + n],
                           "ordered read saw stale memory");
                ++received;
            }
    Expect(received == 2, "ordered read did not complete");
}
void NegativeTest() {
    for (unsigned variant = 0; variant < 8; ++variant) {
        Harness h;
        auto q = Request(CHI_REQ_Opcode_ReadNoSnp);
        ErrorKind expected = ErrorKind::kProtocolViolation;
        switch (variant) {
            case 0:
                q.flit_opcode = CHI_REQ_Opcode_ReadShared;
                break;
            case 1:
                q.flit_opcode = CHI_REQ_Opcode_ReadNoSnpSep;
                expected = ErrorKind::kUnsupportedFeature;
                break;
            case 2:
                q.flit_return_txn_id = 7;
                break;
            case 3:
                q.flit_allow_retry = 0;
                break;
            case 4:
                q.flit_size = 7;
                break;
            case 5:
                q.flit_snp_attr = 1;
                break;
            case 6:
                q.flit_pcrd_type = 3;
                break;
            case 7:
                q.flit_order = 3;
                break;
        }
        h.Rx(q);
        Throws(expected, [&] { h.Tick(); });
    }
    for (unsigned variant = 0; variant < 8; ++variant) {
        Harness h;
        const auto dbid = h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpFull));
        auto f = Data(dbid, Pattern(3), kAllBytes, 0);
        switch (variant) {
            case 0:
                f.flit_txn = 4000;
                break;
            case 1:
                f.flit_be = 0;
                break;
            case 2:
                f.flit_data_id = 1;
                break;
            case 3:
                f.flit_ccid = 1;
                break;
            case 4:
                f.flit_resp_err = 1;
                break;
            case 5:
                h.Rx(f);
                break;
            case 6:
                h.Rx(Data(dbid, Pattern(3), kAllBytes, 2, 0, 3));
                break;
            case 7:
                h.Rx(Data(dbid, Pattern(3), kAllBytes, 2, 0, 3));
                f.flit_resp_err = 3;
                f.flit_resp = 7;
                break;
        }
        h.Rx(f);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness early;
        early.hold = true;
        early.Rx(Request(CHI_REQ_Opcode_WriteNoSnpFull));
        early.Tick();
        const auto dbid = std::get<CHIRspFlit>(early.pending.front().flit).flit_dbid;
        early.AcceptPending();
        early.Rx(Data(dbid, Pattern(1), kAllBytes, 0));
        Throws(ErrorKind::kProtocolViolation, [&] { early.Tick(); });
    }
    SNConfig config;
    config.timeout_cycles = 8;
    Harness h(config);
    const auto dbid = h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpFull));
    h.Rx(Data(dbid, Pattern(1), kAllBytes, 0));
    Throws(ErrorKind::kTimeout, [&] {
        for (unsigned n = 0; n < 12; ++n)
            h.Tick();
    });
}
void StressTest() {
    SNConfig config;
    config.memory_size = 256;
    config.service_latency = 2;
    Harness h(config);
    std::array<Line, 4> expected{};
    std::mt19937 rng(0x5317);
    for (unsigned iteration = 0; iteration < 500; ++iteration) {
        const auto index = rng() % 4;
        const auto mask = (std::uint64_t(rng()) << 32) | rng();
        const auto data = Pattern(rng());
        const auto dbid =
            h.WriteRequest(Request(CHI_REQ_Opcode_WriteNoSnpPtl, iteration % 4096, index * 64));
        const auto first = (rng() % 2) * 2;
        h.Rx(Data(dbid, data, mask, first));
        h.Tick();
        h.Rx(Data(dbid, data, mask, 2 - first));
        h.Drain();
        for (unsigned n = 0; n < 64; ++n)
            if ((mask >> n) & 1)
                expected[index][n] = data[n];
        Expect(h.sn.Memory().Read(index * 64) == expected[index],
               "fixed-seed memory oracle mismatch");
    }
}
}  // namespace

int main() {
    try {
        MemoryBackendTest();
        BasicTest();
        ErrorTest();
        RetryTest(false, false);
        RetryTest(true, false);
        RetryTest(false, true);
        RetryTest(true, true);
        OrderingTest();
        NegativeTest();
        StressTest();
        std::cout << "SN tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
