#include "memory_backend.hpp"

#include <deque>
#include <limits>
#include <map>
#include <set>

namespace esl::chi_vip {
struct ByteMemoryBackend::Impl {
    struct Block {
        Line data{};
        std::uint64_t errors = 0;
    };
    struct Operation {
        Handle handle;
        std::uint64_t address;
        std::uint64_t due;
        bool write;
        Line data;
        std::uint64_t mask;
        std::uint64_t errors;
    };
    std::uint64_t size;
    std::uint64_t latency;
    std::uint64_t last_submission = 0;
    std::uint64_t last_advance = 0;
    std::vector<std::uint8_t> bytes;
    std::map<std::uint64_t, std::uint64_t> errors;
    std::map<std::uint64_t, Block> staged;
    std::deque<Operation> operations;
    std::set<Handle> handles;
    Impl(std::uint64_t s, std::uint64_t l) : size(s), latency(l) {
        Check(size >= 64 && size % 64 == 0 &&
                  size <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()),
              ErrorKind::kConfiguration, "SN.MEM.SIZE: invalid byte memory capacity");
        bytes.resize(static_cast<std::size_t>(size));
    }
    void Address(std::uint64_t address) const {
        Check(address % 64 == 0 && address < size, ErrorKind::kInvalidRequest,
              "SN.MEM.ADDRESS: expected an in-range cache-line address");
    }
    Block Get(std::uint64_t address, bool working) const {
        if (working) {
            auto i = staged.find(address);
            if (i != staged.end())
                return i->second;
        }
        Block b;
        for (unsigned n = 0; n < 64; ++n)
            b.data[n] = bytes[static_cast<std::size_t>(address + n)];
        auto i = errors.find(address);
        if (i != errors.end())
            b.errors = i->second;
        return b;
    }
    void Submit(Operation op, std::uint64_t cycle) {
        Address(op.address);
        Check(cycle >= last_submission && cycle >= last_advance &&
                  cycle <= std::numeric_limits<std::uint64_t>::max() - latency,
              ErrorKind::kLifecycle, "SN.MEM.TIME: non-monotonic or overflowing submission time");
        Check(handles.insert(op.handle).second, ErrorKind::kLifecycle,
              "SN.MEM.ID: duplicate asynchronous operation handle");
        last_submission = cycle;
        op.due = cycle + latency;
        operations.push_back(std::move(op));
    }
};

ByteMemoryBackend::ByteMemoryBackend(std::uint64_t size, std::uint64_t latency)
    : impl_(std::make_unique<Impl>(size, latency)) {}
ByteMemoryBackend::~ByteMemoryBackend() = default;
void ByteMemoryBackend::Initialize(std::uint64_t address, const Line& data, std::uint64_t mask) {
    impl_->Address(address);
    Check(IsIdle(), ErrorKind::kLifecycle, "SN.MEM.INIT: initialization requires idle memory");
    for (unsigned n = 0; n < 64; ++n)
        if ((mask >> n) & 1)
            impl_->bytes[static_cast<std::size_t>(address + n)] = data[n];
    auto i = impl_->errors.find(address);
    if (i != impl_->errors.end()) {
        i->second &= ~mask;
        if (!i->second)
            impl_->errors.erase(i);
    }
}
Line ByteMemoryBackend::Read(std::uint64_t address, unsigned size) const {
    Check(address < impl_->size && size && size <= 64 && (address & 63) <= 64 - size,
          ErrorKind::kInvalidRequest, "SN.MEM.READ: management read crosses a cache line");
    Line result{};
    for (unsigned n = 0; n < size; ++n)
        result[static_cast<unsigned>(address & 63) + n] =
            impl_->bytes[static_cast<std::size_t>(address + n)];
    return result;
}
std::uint64_t ByteMemoryBackend::Errors(std::uint64_t address) const {
    impl_->Address(address);
    return impl_->Get(address, false).errors;
}
void ByteMemoryBackend::SubmitRead(Handle handle, std::uint64_t address, std::uint64_t cycle) {
    impl_->Submit({handle, address, 0, false, {}, 0, 0}, cycle);
}
void ByteMemoryBackend::SubmitWrite(Handle handle, std::uint64_t address, const Line& data,
                                    std::uint64_t mask, std::uint64_t errors, std::uint64_t cycle) {
    Check(!(errors & ~mask), ErrorKind::kInvalidRequest,
          "SN.MEM.ERROR: error bytes must be written bytes");
    impl_->Submit({handle, address, 0, true, data, mask, errors}, cycle);
}
std::vector<MemoryCompletion> ByteMemoryBackend::Advance(std::uint64_t cycle) {
    Check(cycle >= impl_->last_advance, ErrorKind::kLifecycle,
          "SN.MEM.TIME: memory service time cannot move backwards");
    impl_->last_advance = cycle;
    std::vector<MemoryCompletion> completed;
    while (!impl_->operations.empty() && impl_->operations.front().due <= cycle) {
        auto op = impl_->operations.front();
        impl_->operations.pop_front();
        auto block = impl_->Get(op.address, true);
        if (op.write) {
            for (unsigned n = 0; n < 64; ++n)
                if ((op.mask >> n) & 1)
                    block.data[n] = op.data[n];
            block.errors = (block.errors & ~op.mask) | op.errors;
            impl_->staged[op.address] = block;
        }
        impl_->handles.erase(op.handle);
        completed.push_back({op.handle, block.data, block.errors});
    }
    return completed;
}
void ByteMemoryBackend::Commit() {
    for (const auto& [address, block] : impl_->staged) {
        for (unsigned n = 0; n < 64; ++n)
            impl_->bytes[static_cast<std::size_t>(address + n)] = block.data[n];
        if (block.errors)
            impl_->errors[address] = block.errors;
        else
            impl_->errors.erase(address);
    }
    impl_->staged.clear();
}
bool ByteMemoryBackend::IsIdle() const {
    return impl_->operations.empty() && impl_->staged.empty();
}
}  // namespace esl::chi_vip
