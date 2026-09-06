#ifndef ENGINE_RESIDENCY_PREPARATION_HPP
#define ENGINE_RESIDENCY_PREPARATION_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "engine_residency/components.hpp"
#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"
#include "engine_residency/profiles.hpp"
#include "engine_residency/units.hpp"

namespace engine_residency {

// A single ordered preparation step in a plan.
struct PreparationStep {
  PreparationAction action{PreparationAction::DISCOVER_BACKEND};
  ComponentCategory category{ComponentCategory::BACKEND};
  std::string subject;      // affected subject (empty == top-level).
  std::string description;  // bounded, deterministic explanation.
  bool rollback_required{false};
  std::string rollback_description;
};

// A bounded, inspectable preparation plan for a target incarnation/profile.
struct PreparationPlan {
  PreparationPlanId plan_id;
  PreparationPlanGeneration plan_generation;
  EngineIncarnationId incarnation_id;
  EngineIncarnationGeneration incarnation_generation;
  ReadinessProfileId profile_id;
  ReadinessProfileGeneration profile_generation;
  DesiredResidency target_residency{DesiredResidency::WARM};
  CoordinatorEpoch epoch;
  AuthorityGeneration authority;
  SourceBootId source_boot;

  std::vector<PreparationStep> steps;   // bounded, ordered.
  Bytes estimated_bytes{0};
  DurationNs estimated_ns{0};
  Provenance cost_provenance{Provenance::ESTIMATED};

  std::string cancellation_boundary;     // when future steps must stop.
  std::string rollback_action;           // what rollback does.
  std::string expected_readiness;        // expected READY outcome after success.
};

// A preparation attempt binds a plan to a single execution. Attempts have unique
// identities and current authority; a stale attempt may not publish READY.
struct PreparationAttempt {
  PreparationAttemptId attempt_id;
  PreparationAttemptGeneration attempt_generation;
  PreparationPlanId plan_id;
  PreparationPlanGeneration plan_generation;
  EngineIncarnationId incarnation_id;
  EngineIncarnationGeneration incarnation_generation;
  ReadinessProfileId profile_id;
  ReadinessProfileGeneration profile_generation;
  CoordinatorEpoch epoch;
  AuthorityGeneration authority;

  PreparationState state{PreparationState::PLANNED};
  std::size_t next_step_index{0};
  bool cancelled{false};
  bool published_ready{false};
  bool authority_revalidated{false};
  std::vector<std::string> completed_steps;
  std::string failure_stage;   // last failed step, if any.
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_PREPARATION_HPP
