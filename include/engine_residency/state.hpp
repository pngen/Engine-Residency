#ifndef ENGINE_RESIDENCY_STATE_HPP
#define ENGINE_RESIDENCY_STATE_HPP

#include <cstdint>

#include "engine_residency/enums.hpp"

namespace engine_residency {

// ---------------------------------------------------------------------------
// The orthogonal state dimensions carried by an engine incarnation. These are
// deliberately separate: no single axis can represent all of them.
// ---------------------------------------------------------------------------
struct StateSet {
  DesiredResidency desired{DesiredResidency::ABSENT};
  DesiredResidency observed{DesiredResidency::ABSENT};
  ProcessLifecycle lifecycle{ProcessLifecycle::REGISTERING};
  PreparationState preparation{PreparationState::UNPREPARED};
  ActivationState activation{ActivationState::INACTIVE};
  HealthState health{HealthState::UNKNOWN};
  RecoveryState recovery{RecoveryState::NOMINAL};
  RegistrationState registration{RegistrationState::NONE};
  // DrainPhase is tracked separately (it is a progress dimension, not a
  // lifecycle state) but surfaced here for inspection convenience.
  DrainPhase drain{DrainPhase::NONE};

  bool is_serving_eligible_lifecycle() const noexcept {
    return lifecycle == ProcessLifecycle::RUNNING;
  }
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_STATE_HPP
