#include "kernel/model.hpp"

#include <utility>
namespace esl {
ModelBase::ModelBase(ModelID id, std::string path, ModelContext& context)
    : context_(context), id_(id), path_(std::move(path)) {
    context_.log.RegisterModel(path_);
}
void ModelBase::Initialize() {
    Require(state_ == State::kNew, path_ + ": duplicate initialization");
    OnInitialize();
    state_ = State::kInitialized;
    context_.log.Write(LogLevel::kInfo, path_, "model initialized");
}
void ModelBase::Reset() {
    Require(state_ == State::kInitialized, path_ + ": reset only allowed at startup");
    OnReset();
    state_ = State::kReady;
}
void ModelBase::Work() {
    Require(state_ == State::kReady, path_ + ": Work lifecycle violation");
    Require(context_.clock.CurrentPhase() == Phase::kWork, path_ + ": wrong Work phase");
    OnWork();
    state_ = State::kWorked;
}
void ModelBase::Xfer() {
    Require(state_ == State::kWorked, path_ + ": Xfer without Work");
    Require(context_.clock.CurrentPhase() == Phase::kXfer, path_ + ": wrong Xfer phase");
    OnXfer();
    state_ = State::kReady;
}
void ModelBase::Finalize() {
    Require(state_ != State::kNew && state_ != State::kFinalized, path_ + ": invalid finalization");
    OnFinalize();
    state_ = State::kFinalized;
    context_.log.Write(LogLevel::kInfo, path_, "model finalized");
}
void ModelBase::OnFinalize() {}
ModelID ModelBase::ID() const {
    return id_;
}
const std::string& ModelBase::Path() const {
    return path_;
}
}  // namespace esl
