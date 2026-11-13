#include "model/chi_interface/include/chi_common.h"

#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

// Verify that the opcodes are also available to preprocessor clients.
#if CHI_REQ_Opcode_ReadNoSnp != 0x04 || CHI_RSP_Opcode_CompCMO != 0x14 || \
    CHI_SNP_Opcode_SnpUniqueFwd != 0x17 || CHI_DAT_Opcode_CompData != 0x04
#error "CHI opcode macros must carry protocol encodings"
#endif

namespace {
using namespace esl;

// Include gaps, bit 6, atomic sub-opcodes and G.b additions.
static_assert(CHI_REQ_Opcode_AtomicStoreADD == 0x28);
static_assert(CHI_REQ_Opcode_AtomicStoreUMIN == 0x2F);
static_assert(CHI_REQ_Opcode_AtomicLoadADD == 0x30);
static_assert(CHI_REQ_Opcode_AtomicLoadUMIN == 0x37);
static_assert(CHI_REQ_Opcode_MakeReadUnique == 0x41);
static_assert(CHI_REQ_Opcode_WriteNoSnpDef == 0x4E);
static_assert(CHI_REQ_Opcode_WriteBackFullCleanInvPoPA == 0x79);
static_assert(CHI_RSP_Opcode_CompDBIDResp == 0x05);
static_assert(CHI_SNP_Opcode_SnpDVMOp == 0x0D);
static_assert(CHI_SNP_Opcode_SnpPreferUniqueFwd == 0x16);
static_assert(CHI_DAT_Opcode_DataSepResp == 0x0B);
static_assert(CHI_DAT_Opcode_NCBWrDataCompAck == 0x0C);

template <class Flit>
constexpr bool kFlitValueType =
    std::is_aggregate<Flit>::value && std::is_standard_layout<Flit>::value &&
    std::is_trivially_copyable<Flit>::value;
static_assert(kFlitValueType<CHIReqFlit> && kFlitValueType<CHIRspFlit> &&
              kFlitValueType<CHISnpFlit>);
static_assert(kFlitValueType<CHIDatFlitT<128>> && kFlitValueType<CHIDatFlitT<256>> &&
              kFlitValueType<CHIDatFlitT<512>>);
static_assert(std::is_same<CHIDatFlit, CHIDatFlitT<256>>::value);
static_assert(std::is_same<decltype(CHIReqFlit::flit_opcode), std::uint32_t>::value);
static_assert(std::is_same<decltype(CHIReqFlit::flit_allow_retry), std::uint32_t>::value);
static_assert(std::is_same<decltype(CHIReqFlit::flit_address), std::uint64_t>::value);
static_assert(std::is_same<NodeID, std::uint32_t>::value);
static_assert(std::is_same<TxnID, std::uint32_t>::value);
static_assert(std::is_same<DBID, std::uint32_t>::value);
static_assert(std::is_same<decltype(CHIDatFlit::flit_data)::value_type, std::uint32_t>::value);
static_assert(CHIReqFlit{}.flit_opcode == CHI_REQ_Opcode_ReqLCrdReturn);
static_assert(CHIRspFlit{}.flit_opcode == CHI_RSP_Opcode_RespLCrdReturn);
static_assert(CHISnpFlit{}.flit_opcode == CHI_SNP_Opcode_SnpLCrdReturn);
static_assert(CHIDatFlit{}.flit_opcode == CHI_DAT_Opcode_DataLCrdReturn);

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <class Flit>
void CheckText(const Flit& flit, const char* type, std::initializer_list<const char*> controls) {
    const auto text = flit.to_string();
    Check(text.rfind(std::string(type) + "{", 0) == 0 && text.back() == '}',
          "flit summary must identify the channel");
    for (const auto* control : controls)
        Check(text.find(control) != std::string::npos, "missing hexadecimal control value");

    // The caller deliberately requests a different base, case and fill character.
    std::ostringstream stream;
    stream << std::oct << std::showbase << std::uppercase << std::boolalpha;
    stream.fill('#');
    stream.precision(3);
    const auto flags = stream.flags();
    const auto* returned = &(stream << flit);
    Check(returned == &stream && stream.str() == text, "operator<< must match to_string");
    Check(stream.flags() == flags && stream.fill() == '#' && stream.precision() == 3,
          "flit output must preserve the caller's stream formatting");
    stream << ' ' << 42;
    Check(stream.str() == text + " 052", "subsequent stream output must retain octal formatting");
}

void CheckControlFlits() {
    CHIReqFlit req;
    req.flit_opcode = CHI_REQ_Opcode_ReadNoSnp;
    req.flit_txn = 0xABC;
    req.flit_src = 0x123;
    req.flit_tgt = 0x456;
    req.flit_address = 0xFFFFFFFFFFFFFULL;
    req.flit_size = 6;
    req.flit_allow_retry = 1;
    req.flit_mpam = 0x7FFF;
    req.flit_stream_id = 0xFFFF;
    req.flit_rsvdc = 0xFFFFFFFF;
    CheckText(req, "CHIReqFlit",
              {"flit_opcode=0x4,", "flit_txn=0xabc,", "flit_src=0x123,", "flit_tgt=0x456,",
               "flit_address=0xfffffffffffff,", "flit_size=0x6,", "flit_allow_retry=0x1,"});
    req.reset();
    Check(req.flit_opcode == 0 && req.flit_txn == 0 && req.flit_src == 0 && req.flit_tgt == 0 &&
              req.flit_address == 0 && req.flit_size == 0 && req.flit_allow_retry == 0 &&
              req.flit_mpam == 0 && req.flit_stream_id == 0 && req.flit_rsvdc == 0,
          "REQ reset must clear routing, attributes and optional fields");
    CheckText(req, "CHIReqFlit", {"flit_opcode=0x0,", "flit_address=0x0,"});

    CHIRspFlit rsp;
    rsp.flit_opcode = CHI_RSP_Opcode_CompDBIDResp;
    rsp.flit_txn = 0xABC;
    rsp.flit_dbid = 0xFED;
    rsp.flit_resp = 5;
    rsp.flit_resp_err = 2;
    rsp.flit_pcrd_type = 0xA;
    rsp.flit_tag_group_id = 0xFF;
    CheckText(rsp, "CHIRspFlit",
              {"flit_opcode=0x5,", "flit_dbid=0xfed,", "flit_resp=0x5,", "flit_resp_err=0x2,",
               "flit_pcrd_type=0xa,"});
    rsp.reset();
    Check(rsp.flit_opcode == 0 && rsp.flit_txn == 0 && rsp.flit_dbid == 0 && rsp.flit_resp == 0 &&
              rsp.flit_resp_err == 0 && rsp.flit_pcrd_type == 0 && rsp.flit_tag_group_id == 0,
          "RSP reset must clear response and group identifiers");

    CHISnpFlit snp;
    snp.flit_opcode = CHI_SNP_Opcode_SnpUniqueFwd;
    snp.flit_txn = 0xABC;
    snp.flit_fwd_nid = 0x7FF;
    snp.flit_fwd_txn_id = 0xFED;
    snp.flit_address = 0x1FFFFFFFFFFFFULL;
    snp.flit_ret_to_src = 1;
    snp.flit_mecid = 0xFFFF;
    snp.flit_vmid_ext = 0xFF;
    CheckText(snp, "CHISnpFlit",
              {"flit_opcode=0x17,", "flit_fwd_nid=0x7ff,", "flit_fwd_txn_id=0xfed,",
               "flit_address=0x1ffffffffffff,", "flit_ret_to_src=0x1}"});
    snp.reset();
    Check(snp.flit_opcode == 0 && snp.flit_txn == 0 && snp.flit_fwd_nid == 0 &&
              snp.flit_fwd_txn_id == 0 && snp.flit_address == 0 && snp.flit_ret_to_src == 0 &&
              snp.flit_mecid == 0 && snp.flit_vmid_ext == 0,
          "SNP reset must clear forwarding and optional fields");
}

template <std::size_t Width>
void CheckDataFlit() {
    CHIDatFlitT<Width> flit;
    Check(flit.flit_data.size() * 32 == Width, "DAT words must match the interface width");
    Check(flit.flit_be == 0 && flit.flit_data_check == 0 && flit.flit_poison == 0,
          "inactive byte lanes and optional checks must start clear");

    // Exercise the highest word, byte mask, check and tag bits at each supported width.
    flit.flit_opcode = CHI_DAT_Opcode_CompData;
    flit.flit_txn = 0xABC;
    flit.flit_dbid = 0xFFF;
    flit.flit_home_nid = 0x7FF;
    flit.flit_data_source = 0xFF;
    flit.flit_mecid = 0xFFFF;
    flit.flit_data_id = Width == 512 ? 0 : 2;
    flit.flit_data.fill(0x89ABCDEF);
    flit.flit_data.back() = 0xFEDCBA98;
    flit.flit_be = std::uint64_t{1} << (Width / 8 - 1);
    flit.flit_data_check = flit.flit_be;
    flit.flit_poison = std::uint32_t{1} << (Width / 64 - 1);
    flit.flit_tag = std::uint32_t{1} << (Width / 32 - 1);
    flit.flit_tu = std::uint32_t{1} << (Width / 128 - 1);
    CheckText(flit, "CHIDatFlit",
              {"flit_opcode=0x4,", "flit_txn=0xabc,", "flit_dbid=0xfff,", "flit_home_nid=0x7ff,",
               "flit_data_source=0xff,"});
    if constexpr (Width == 512)
        CheckText(flit, "CHIDatFlit",
                  {"flit_be=0x8000000000000000,", "flit_data_check=0x8000000000000000,",
                   "flit_poison=0x80}"});

    const auto snapshot = flit;
    flit.reset();
    Check(snapshot.flit_data.back() == 0xFEDCBA98 && snapshot.flit_data.front() == 0x89ABCDEF &&
              snapshot.flit_be == (std::uint64_t{1} << (Width / 8 - 1)),
          "cycle snapshots must own the complete DAT payload");
    Check(flit.flit_opcode == 0 && flit.flit_txn == 0 && flit.flit_dbid == 0 &&
              flit.flit_home_nid == 0 && flit.flit_data_source == 0 && flit.flit_mecid == 0 &&
              flit.flit_data_id == 0 && flit.flit_be == 0 && flit.flit_data_check == 0 &&
              flit.flit_poison == 0 && flit.flit_tag == 0 && flit.flit_tu == 0,
          "DAT reset must clear identifiers and every data mask");
    for (auto word : flit.flit_data)
        Check(word == 0, "DAT reset must clear every payload word");
}
}  // namespace

int main() {
    try {
        CheckControlFlits();
        CheckDataFlit<128>();
        CheckDataFlit<256>();
        CheckDataFlit<512>();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
