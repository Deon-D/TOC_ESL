#pragma once
#include "foundation/log.hpp"
namespace esl {
struct ModelContext {
    // Borrowed platform-owned services; their lifetimes exceed every model lifetime.
    const Clock& clock;
    Logger& log;
    TraceSink& trace;
};
class ModelBase {
public:
    ModelBase(ModelID id, std::string path, ModelContext& context);
    virtual ~ModelBase() = default;
    ModelBase(const ModelBase&) = delete;
    ModelBase& operator=(const ModelBase&) = delete;
    // Lifecycle guards call the corresponding On* hook and throw on invalid use.
    void Initialize();
    void Reset();
    // Work reads current state and prepares next state / protocol drive intents.
    void Work();
    // Xfer reads frozen link decisions only; new state is visible at the next Work.
    void Xfer();
    void Finalize();
    ModelID ID() const;
    const std::string& Path() const;
    virtual bool IsIdle() const = 0;
    virtual std::string DescribeState() const = 0;

protected:
    virtual void OnInitialize() = 0;
    virtual void OnReset() = 0;
    virtual void OnWork() = 0;
    virtual void OnXfer() = 0;
    virtual void OnFinalize();
    ModelContext& context_;

private:
    enum class State { kNew, kInitialized, kReady, kWorked, kFinalized };
    ModelID id_;
    std::string path_;
    State state_ = State::kNew;
};
class ProtocolLink {
public:
    // Connection infrastructure, not a model message API. Kernel owns phase order.
    virtual ~ProtocolLink() = default;
    virtual void Validate() const = 0;
    virtual void Prepare() = 0;
    virtual void Resolve() = 0;
    virtual void Publish() = 0;
    virtual bool IsIdle() const = 0;
    virtual std::string DescribeState() const = 0;
};
}  // namespace esl
