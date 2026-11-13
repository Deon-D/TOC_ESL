#include <iostream>

#include "hn.hpp"

namespace {
using namespace esl;
using namespace esl::chi_vip;
void Expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F>
void Throws(ErrorKind kind, F fn) {
    try {
        fn();
    } catch (const VIPError& e) {
        if (e.Kind() != kind)
            throw std::runtime_error("wrong HN diagnostic: " + std::string(e.what()));
        return;
    }
    throw std::runtime_error("HN accepted an illegal sequence");
}
CHIReqFlit Request(unsigned op = CHI_REQ_Opcode_ReadNoSnp, TxnID txn = 1, NodeID source = 0) {
    CHIReqFlit q;
    q.flit_opcode = op;
    q.flit_tgt = 1;
    q.flit_src = source;
    q.flit_txn = txn;
    q.flit_size = 6;
    q.flit_ns = 1;
    q.flit_allow_retry = 1;
    q.flit_mem_attr = 5;
    if (op != CHI_REQ_Opcode_ReadNoSnp && op != CHI_REQ_Opcode_WriteNoSnpFull &&
        op != CHI_REQ_Opcode_WriteNoSnpPtl)
        q.flit_snp_attr = 1;
    if (op == CHI_REQ_Opcode_ReadShared || op == CHI_REQ_Opcode_ReadUnique) {
        q.flit_snp_attr = 1;
        q.flit_exp_comp_ack = 1;
        q.flit_mem_attr = 13;
    }
    return q;
}
CHIRspFlit Response(const CHIReqFlit& q, unsigned opcode) {
    CHIRspFlit f;
    f.flit_src = q.flit_tgt;
    f.flit_tgt = q.flit_src;
    f.flit_txn = q.flit_txn;
    f.flit_opcode = opcode;
    return f;
}
CHIDatFlit Data(NodeID source, NodeID target, TxnID txn, unsigned opcode, unsigned data_id,
                unsigned resp = 0) {
    CHIDatFlit f;
    f.flit_src = source;
    f.flit_tgt = target;
    f.flit_txn = txn;
    f.flit_opcode = opcode;
    f.flit_data_id = data_id;
    f.flit_resp = resp;
    f.flit_be = 0xffffffffu;
    for (unsigned n = 0; n < 32; ++n)
        f.flit_data[n / 4] |= std::uint32_t(data_id * 16 + n + 1) << (8 * (n % 4));
    if (opcode == CHI_DAT_Opcode_CompData)
        f.flit_home_nid = source;
    return f;
}
CHIDatFlit MaskedData(NodeID source, NodeID target, TxnID txn, unsigned opcode, unsigned data_id,
                      unsigned resp, const Line& line, std::uint64_t mask) {
    auto f = Data(source, target, txn, opcode, data_id, resp);
    f.flit_data.fill(0);
    f.flit_be = (mask >> (data_id * 16)) & 0xffffffffu;
    for (unsigned n = 0; n < 32; ++n)
        if ((f.flit_be >> n) & 1)
            f.flit_data[n / 4] |= std::uint32_t(line[data_id * 16 + n]) << (8 * (n % 4));
    return f;
}
struct Harness {
    HN hn;
    std::uint64_t cycle = 0;
    bool acknowledge = true;
    std::vector<SendAction> actions;
    explicit Harness(HNConfig config = {}) : hn(config) {}
    void Rx(Flit f) {
        hn.OnEvent(Event::Received(std::move(f)));
    }
    void Tick() {
        hn.BeginCycle(cycle++);
        auto pending = hn.TakeActions();
        hn.Commit();
        for (auto& a : pending) {
            if (acknowledge) {
                hn.OnEvent(Event::Accepted(a.token));
                hn.OnEvent(Event::Sent(a.token, cycle - 1));
            }
            actions.push_back(std::move(a));
        }
    }
    template <class T>
    T Last(NodeID destination) const {
        for (auto it = actions.rbegin(); it != actions.rend(); ++it)
            if (it->destination == destination)
                if (const auto* f = std::get_if<T>(&it->flit))
                    return *f;
        throw std::runtime_error("HN expected output absent");
    }
    CHIReqFlit BeginRead(unsigned op = CHI_REQ_Opcode_ReadNoSnp) {
        Rx(Request(op));
        Tick();
        return Last<CHIReqFlit>(3);
    }
    void ReturnRead(const CHIReqFlit& q) {
        Rx(Response(q, CHI_RSP_Opcode_ReadReceipt));
        Rx(Data(3, 1, q.flit_txn, CHI_DAT_Opcode_CompData, 2));
        Rx(Data(3, 1, q.flit_txn, CHI_DAT_Opcode_CompData, 0));
        Tick();
    }
    void GrantUnique() {
        const auto q = BeginRead(CHI_REQ_Opcode_ReadUnique);
        ReturnRead(q);
        const auto data = Last<CHIDatFlit>(0);
        CHIRspFlit ack;
        ack.flit_src = 0;
        ack.flit_tgt = 1;
        ack.flit_txn = data.flit_dbid;
        ack.flit_opcode = CHI_RSP_Opcode_CompAck;
        Rx(ack);
        Tick();
        Expect(hn.IsIdle(), "HN did not release completed allocating read");
    }
};
void ReadAndSnapshotTest() {
    Harness h;
    const auto request = h.BeginRead();
    Expect(request.flit_opcode == CHI_REQ_Opcode_ReadNoSnp && request.flit_return_nid == 1 &&
               request.flit_return_txn_id == request.flit_txn && request.flit_order == 1,
           "HN downstream request did not preserve ReturnNID/TxnID or Request Accepted");
    h.ReturnRead(request);
    Expect(!h.hn.IsIdle(), "HN released upstream data before actual transmission");
    const auto response = h.Last<CHIDatFlit>(0);
    Expect(response.flit_src == 1 && response.flit_home_nid == 1 && response.flit_txn == 1 &&
               response.flit_resp == 0,
           "HN did not remap the downstream read response");
    h.Tick();
    Expect(h.hn.IsIdle(), "HN did not drain read response sends");
    Harness unique;
    unique.GrantUnique();
    const auto directory = unique.hn.InspectDirectory(0);
    Expect(directory && directory->owner == 0 && directory->sharers.empty(),
           "HN unique directory permission was not committed");
}
void EarlyWriteDataTest() {
    Harness h;
    h.acknowledge = false;
    h.Rx(Request(CHI_REQ_Opcode_WriteNoSnpFull));
    h.Tick();
    const auto dbid = h.Last<CHIRspFlit>(0).flit_dbid;
    h.Rx(Data(0, 1, dbid, CHI_DAT_Opcode_NonCopyBackWriteData, 0));
    Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
}
void EarlyCompAckTest() {
    Harness h;
    const auto request = h.BeginRead(CHI_REQ_Opcode_ReadUnique);
    h.acknowledge = false;
    h.ReturnRead(request);
    const auto response = h.Last<CHIDatFlit>(0);
    CHIRspFlit ack;
    ack.flit_opcode = CHI_RSP_Opcode_CompAck;
    ack.flit_src = 0;
    ack.flit_tgt = 1;
    ack.flit_txn = response.flit_dbid;
    h.Rx(ack);
    Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
}
void EarlySnoopDataTest() {
    Harness h;
    h.GrantUnique();
    h.acknowledge = false;
    h.Rx(Request(CHI_REQ_Opcode_ReadUnique, 2, 2));
    h.Tick();
    const auto snoop = h.Last<CHISnpFlit>(0);
    h.Rx(Data(0, 1, snoop.flit_txn, CHI_DAT_Opcode_SnpRespData, 0, 4));
    Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
}
void EarlyCreditTest() {
    HNConfig config;
    config.request_slots = 1;
    Harness h(config);
    const auto first = h.BeginRead();
    auto second = Request(CHI_REQ_Opcode_ReadNoSnp, 2, 2);
    h.Rx(second);
    h.Tick();
    h.ReturnRead(first);
    h.acknowledge = false;
    h.Tick();
    const auto grant = h.Last<CHIRspFlit>(2);
    Expect(grant.flit_opcode == CHI_RSP_Opcode_PCrdGrant, "expected queued grant absent");
    second.flit_allow_retry = 0;
    second.flit_pcrd_type = grant.flit_pcrd_type;
    h.Rx(second);
    Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
}
void RetryLateResponseTest() {
    Harness h;
    const auto request = h.BeginRead();
    auto retry = Response(request, CHI_RSP_Opcode_RetryAck);
    retry.flit_pcrd_type = 1;
    h.Rx(retry);
    h.Tick();
    h.Rx(Response(request, CHI_RSP_Opcode_ReadReceipt));
    Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
}
void BadSnoopStateTest() {
    Harness h;
    h.GrantUnique();
    h.Rx(Request(CHI_REQ_Opcode_ReadShared, 2, 2));
    h.Tick();
    const auto snoop = h.Last<CHISnpFlit>(0);
    CHIRspFlit rsp;
    rsp.flit_src = 0;
    rsp.flit_tgt = 1;
    rsp.flit_txn = snoop.flit_txn;
    rsp.flit_opcode = CHI_RSP_Opcode_SnpResp;
    rsp.flit_resp = 2;  // SnpShared must not retain unique permission.
    h.Rx(rsp);
    Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
}
void SnoopOncePassDirtyTest(bool partial) {
    Harness h;
    h.GrantUnique();
    h.Rx(Request(CHI_REQ_Opcode_ReadOnce, 2, 2));
    h.Tick();
    const auto snoop = h.Last<CHISnpFlit>(0);
    Expect(snoop.flit_opcode == CHI_SNP_Opcode_SnpOnce, "ReadOnce did not use SnpOnce");
    Line initial{}, replacement{}, memory{};
    for (unsigned n = 0; n < 64; ++n) {
        initial[n] = static_cast<std::uint8_t>(n + 1);
        replacement[n] = static_cast<std::uint8_t>(n + 0xa0);
    }
    memory = initial;
    const auto mask = partial ? 0x8100010080040021ull : kAllBytes;
    const auto opcode = partial ? CHI_DAT_Opcode_SnpRespDataPtl : CHI_DAT_Opcode_SnpRespData;
    const auto resp =
        partial ? 4u : 5u;  // I_PD partial / SC_PD full are legal SnpOnce alternatives.
    h.Rx(MaskedData(0, 1, snoop.flit_txn, opcode, 2, resp, replacement, mask));
    h.Rx(MaskedData(0, 1, snoop.flit_txn, opcode, 0, resp, replacement, mask));
    h.Tick();
    const auto flush = h.Last<CHIReqFlit>(3);
    Expect(flush.flit_opcode ==
               (partial ? CHI_REQ_Opcode_WriteNoSnpPtl : CHI_REQ_Opcode_WriteNoSnpFull),
           "HN failed to preserve dirty responsibility using a downstream write");
    auto dbid = Response(flush, CHI_RSP_Opcode_DBIDResp);
    dbid.flit_dbid = 53;
    h.Rx(dbid);
    const auto offset = h.actions.size();
    h.Tick();
    unsigned packets = 0;
    for (std::size_t i = offset; i < h.actions.size(); ++i)
        if (h.actions[i].destination == 3)
            if (const auto* f = std::get_if<CHIDatFlit>(&h.actions[i].flit)) {
                Expect(f->flit_txn == 53 && f->flit_opcode == CHI_DAT_Opcode_NonCopyBackWriteData,
                       "HN dirty flush DAT mapping incorrect");
                for (unsigned n = 0; n < 32; ++n)
                    if ((f->flit_be >> n) & 1)
                        memory[f->flit_data_id * 16 + n] =
                            static_cast<std::uint8_t>(f->flit_data[n / 4] >> (8 * (n % 4)));
                ++packets;
            }
    Expect(packets == 2, "dirty responsibility was not fully packetized");
    auto comp = Response(flush, CHI_RSP_Opcode_Comp);
    comp.flit_dbid = 53;
    h.Rx(comp);
    h.Tick();
    const auto read = h.Last<CHIReqFlit>(3);
    Expect(read.flit_opcode == CHI_REQ_Opcode_ReadNoSnp,
           "HN did not obtain remaining memory bytes");
    h.Rx(Response(read, CHI_RSP_Opcode_ReadReceipt));
    h.Rx(MaskedData(3, 1, read.flit_txn, CHI_DAT_Opcode_CompData, 0, 0, memory, kAllBytes));
    h.Rx(MaskedData(3, 1, read.flit_txn, CHI_DAT_Opcode_CompData, 2, 0, memory, kAllBytes));
    const auto completion_offset = h.actions.size();
    h.Tick();
    packets = 0;
    for (std::size_t i = completion_offset; i < h.actions.size(); ++i)
        if (h.actions[i].destination == 2)
            if (const auto* f = std::get_if<CHIDatFlit>(&h.actions[i].flit)) {
                for (unsigned n = 0; n < 32; ++n) {
                    const auto index = f->flit_data_id * 16 + n;
                    const auto expected =
                        ((mask >> index) & 1) ? replacement[index] : initial[index];
                    Expect(
                        static_cast<std::uint8_t>(f->flit_data[n / 4] >> (8 * (n % 4))) == expected,
                        "SnpOnce PD read lost the latest bytes or overwrote absent bytes");
                }
                ++packets;
            }
    Expect(packets == 2, "ReadOnce completion did not provide the full line");
    h.Tick();
    Expect(h.hn.IsIdle(), "SnpOnce PD flow did not drain");
    const auto directory = h.hn.InspectDirectory(0);
    Expect(
        partial ? !directory : directory && directory->sharers.count(0) == 1 && !directory->owner,
        "SnpOnce PD final permission was not retained correctly");
}
void CopyBackResponseTest() {
    Harness h;
    h.GrantUnique();
    h.Rx(Request(CHI_REQ_Opcode_WriteBackFull, 2));
    h.Tick();
    const auto response = h.Last<CHIRspFlit>(0);
    Expect(response.flit_opcode == CHI_RSP_Opcode_CompDBIDResp,
           "copyback buffer was not allocated");
    h.Rx(Data(0, 1, response.flit_dbid, CHI_DAT_Opcode_CopyBackWriteData, 0, 5));
    h.Rx(Data(0, 1, response.flit_dbid, CHI_DAT_Opcode_CopyBackWriteData, 2, 5));
    Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
}
void CreditUseTest(bool grant_first, bool give_back) {
    HNConfig config;
    config.request_slots = 1;
    config.grant_before_retry = grant_first;
    Harness h(config);
    const auto first = h.BeginRead();
    auto second = Request(CHI_REQ_Opcode_ReadNoSnp, 2, 2);
    h.Rx(second);
    h.Tick();
    h.ReturnRead(first);
    h.Tick();
    h.Tick();  // Grant/RetryAck have now been actually transmitted.
    if (give_back) {
        CHIReqFlit returned;
        returned.flit_opcode = CHI_REQ_Opcode_PCrdReturn;
        returned.flit_src = 2;
        returned.flit_tgt = 1;
        returned.flit_pcrd_type = 1;
        h.Rx(returned);
        h.Tick();
    } else {
        second.flit_txn = 99;
        second.flit_allow_retry = 0;
        second.flit_pcrd_type = 1;
        h.Rx(second);
        h.Tick();
        const auto read = h.Last<CHIReqFlit>(3);
        h.ReturnRead(read);
        const auto response = h.Last<CHIDatFlit>(2);
        Expect(response.flit_txn == 99, "P-Credit was incorrectly bound to rejected TxnID");
        h.Tick();
    }
    Expect(h.hn.IsIdle(), "returned/consumed P-Credit did not release resources");
}
void NegativeTest() {
    for (unsigned variant = 0; variant < 7; ++variant) {
        Harness h;
        auto q = Request();
        auto expected = ErrorKind::kProtocolViolation;
        switch (variant) {
            case 0:
                q.flit_opcode = 0x06;
                break;
            case 1:
                q.flit_opcode = CHI_REQ_Opcode_AtomicStoreADD;
                expected = ErrorKind::kUnsupportedFeature;
                break;
            case 2:
                q.flit_src = 7;
                break;
            case 3:
                q.flit_return_nid = 1;
                break;
            case 4:
                q.flit_allow_retry = 0;
                break;
            case 5:
                q.flit_size = 7;
                break;
            case 6:
                h.Rx(q);
                break;
        }
        h.Rx(q);
        Throws(expected, [&] { h.Tick(); });
    }
    {
        Harness h;
        auto q = h.BeginRead();
        auto data = Data(3, 1, q.flit_txn, CHI_DAT_Opcode_CompData, 0);
        h.Rx(data);
        h.Rx(data);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        Harness h;
        CHIRspFlit unknown;
        unknown.flit_src = 3;
        unknown.flit_tgt = 1;
        unknown.flit_txn = 4095;
        unknown.flit_opcode = CHI_RSP_Opcode_Comp;
        h.Rx(unknown);
        Throws(ErrorKind::kProtocolViolation, [&] { h.Tick(); });
    }
    {
        HNConfig config;
        config.timeout_cycles = 5;
        Harness h(config);
        h.BeginRead();
        Throws(ErrorKind::kTimeout, [&] {
            for (unsigned n = 0; n < 8; ++n)
                h.Tick();
        });
    }
}
}  // namespace
int main() {
    try {
        ReadAndSnapshotTest();
        EarlyWriteDataTest();
        EarlyCompAckTest();
        EarlySnoopDataTest();
        EarlyCreditTest();
        RetryLateResponseTest();
        BadSnoopStateTest();
        SnoopOncePassDirtyTest(false);
        SnoopOncePassDirtyTest(true);
        CopyBackResponseTest();
        CreditUseTest(false, false);
        CreditUseTest(true, false);
        CreditUseTest(false, true);
        CreditUseTest(true, true);
        NegativeTest();
        std::cout << "HN contract tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
