#ifndef ENGINE_RESIDENCY_READINESS_HPP
#define ENGINE_RESIDENCY_READINESS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "engine_residency/components.hpp"
#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"
#include "engine_residency/profiles.hpp"

namespace engine_residency {

// Outcome per individual requirement in a profile evaluation.
struct RequirementOutcome {
  ComponentCategory category{ComponentCategory::MODEL};
  std::string subject;
  RequirementKind kind{RequirementKind::REQUIRED};
  ComponentState min_state{ComponentState::VERIFIED};
  ComponentState actual_state{ComponentState::UNKNOWN};
  bool satisfied{false};
  bool fallback_used{false};
  bool incompatible{false};
  bool stale{false};
  bool missing{false};
  std::string reason;   // deterministic human-readable reason when not satisfied.
};

// A deterministic, inspectable readiness evaluation for one profile.
struct ReadinessResult {
  EngineId engine_id;
  EngineGeneration engine_generation;
  EngineIncarnationId incarnation_id;
  EngineIncarnationGeneration incarnation_generation;
  ReadinessProfileId profile_id;
  ReadinessProfileGeneration profile_generation;
  ReadinessGeneration readiness_generation;    // the current authority snapshot.
  ActivationGeneration activation_generation;  // current activation, if any.

  ReadinessOutcome outcome{ReadinessOutcome::UNKNOWN};
  bool coherent_snapshot{false};   // whether the result derives from one snapshot.

  std::vector<RequirementOutcome> requirement_outcomes;
  std::vector<std::string> satisfied;      // satisfied requirement labels
  std::vector<std::string> missing;        // missing prerequisite labels
  std::vector<std::string> incompatible;   // incompatible prerequisites
  std::vector<std::string> stale_evidence; // stale evidence labels
  std::vector<std::string> required_actions; // preparation actions to reach READY
  std::string selected_fallback;            // fallback selected, if any.

  // Resource headroom at evaluation time.
  Bytes headroom_bytes{0};
  std::uint64_t headroom_kv_entries{0};

  // Provenance of the evaluation.
  std::string detail;
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_READINESS_HPP
