#pragma once

#include <memory>

#include "types.hpp"

namespace esl::chi_vip {
struct MemoryCompletion {
    Handle handle = 0;
    Line data{};
    std::uint64_t errors = 0;
};

// Transaction service is asynchronous. Management reads and initialization are
// separate from the protocol path; reads expose only committed memory contents.
class MemoryBackend {
public:
    virtual ~MemoryBackend() = default;
    virtual void SubmitRead(Handle handle, std::uint64_t address, std::uint64_t cycle) = 0;
    virtual void SubmitWrite(Handle handle, std::uint64_t address, const Line& data,
                             std::uint64_t mask, std::uint64_t errors, std::uint64_t cycle) = 0;
    virtual std::vector<MemoryCompletion> Advance(std::uint64_t cycle) = 0;
    virtual void Commit() = 0;
    virtual bool IsIdle() const = 0;
};

class ByteMemoryBackend final : public MemoryBackend {
public:
    explicit ByteMemoryBackend(std::uint64_t size = 1 << 20, std::uint64_t latency = 5);
    ~ByteMemoryBackend();
    ByteMemoryBackend(const ByteMemoryBackend&) = delete;
    ByteMemoryBackend& operator=(const ByteMemoryBackend&) = delete;

    void Initialize(std::uint64_t address, const Line& data, std::uint64_t mask = kAllBytes);
    Line Read(std::uint64_t address, unsigned size = 64) const;
    std::uint64_t Errors(std::uint64_t address) const;
    void SubmitRead(Handle handle, std::uint64_t address, std::uint64_t cycle) override;
    void SubmitWrite(Handle handle, std::uint64_t address, const Line& data, std::uint64_t mask,
                     std::uint64_t errors, std::uint64_t cycle) override;
    std::vector<MemoryCompletion> Advance(std::uint64_t cycle) override;
    void Commit() override;
    bool IsIdle() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace esl::chi_vip
