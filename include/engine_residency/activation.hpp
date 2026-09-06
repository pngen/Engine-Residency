#ifndef ENGINE_RESIDENCY_ACTIVATION_HPP
#define ENGINE_RESIDENCY_ACTIVATION_HPP

#include <cstdint>
#include <string>

#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"

namespace engine_residency {

// A request to activate (promote) a local engine to ACTIVE authority.
struct ActivationRequest {
  EngineIncarnationId incarnation_id;
  EngineIncarnationGeneration incarnation_generation;
  EngineId engine_id;
  EngineGeneration engine_generation;
  ReadinessProfileId profile_id;
  ReadinessProfileGeneration profile_generation;
  ReadinessGeneration readiness_generation;   // must be current snapshot.
  StandbyGeneration standby_generation;       // standby authority at activation.
  AuthorityGeneration caller_authority;       // the caller's current authority.
  EngineSlotId slot_id;                       // exclusive slot (if any).
  std::string caller;                         // diagnostic caller identity.
};

// The committed activation record. For an exclusive slot, at most one is current.
struct ActivationRecord {
  ActivationId activation_id;
  ActivationGeneration activation_generation;
  EngineId engine_id;
  EngineGeneration engine_generation;
  EngineIncarnationId incarnation_id;
  EngineIncarnationGeneration incarnation_generation;
  ReadinessProfileId profile_id;
  ReadinessGeneration readiness_generation;
  StandbyGeneration standby_generation;
  EngineSlotId slot_id;
  ActivationState state{ActivationState::INACTIVE};
  ActivationState prior{ActivationState::INACTIVE};
  bool current{false};       // current for its (engine, slot).
  std::string detail;
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_ACTIVATION_HPP
