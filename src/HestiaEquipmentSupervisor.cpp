#include "HestiaEquipmentSupervisor.h"

namespace HestiaEquipment {

Supervisor::Supervisor(uint32_t settleDelayMs) : settleDelayMs_(settleDelayMs) {}

void Supervisor::setSettleDelayMs(uint32_t delayMs) { settleDelayMs_ = delayMs; }
uint32_t Supervisor::settleDelayMs() const { return settleDelayMs_; }

void Supervisor::reset() {
  state_ = State::Unknown;
  stateChanged_ = false;
  mismatchActive_ = false;
  mismatchStartedMs_ = 0;
}

State Supervisor::update(bool commandedOn, bool sampleValid, bool observedOn, uint32_t nowMs) {
  commandedOn_ = commandedOn;
  observedOn_ = observedOn;
  const State previous = state_;

  if (!sampleValid) {
    mismatchActive_ = false;
    state_ = State::InvalidSample;
  } else if (commandedOn == observedOn) {
    mismatchActive_ = false;
    state_ = State::Matching;
  } else {
    if (!mismatchActive_) {
      mismatchActive_ = true;
      mismatchStartedMs_ = nowMs;
    }
    state_ = static_cast<uint32_t>(nowMs - mismatchStartedMs_) >= settleDelayMs_
                 ? State::Fault
                 : State::Settling;
  }

  stateChanged_ = state_ != previous;
  return state_;
}

State Supervisor::state() const { return state_; }
bool Supervisor::hasFault() const { return state_ == State::Fault; }
bool Supervisor::stateChanged() const { return stateChanged_; }
bool Supervisor::command() const { return commandedOn_; }
bool Supervisor::observation() const { return observedOn_; }

}  // namespace HestiaEquipment
