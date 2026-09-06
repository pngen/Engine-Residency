#ifndef ENGINE_RESIDENCY_SERVING_USE_HPP
#define ENGINE_RESIDENCY_SERVING_USE_HPP

#include <cstdint>
#include <string>

#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"

namespace engine_residency {

// A generation-bound serving-use token. It closes the query-to-execution race:
// the token binds the exact authority the backend execution adapter needs.
struct ServingUseToken {
  ServingUseId use_id;
  ServingUseGeneration use_generation;
  EngineId engine_id;
  EngineGeneration engine_generation;
  EngineIncarnationId incarnation_id;
  EngineIncarnationGeneration incarnation_generation;
  ReadinessProfileId profile_id;
  ReadinessGeneration readiness_generation;
  ActivationGeneration activation_generation;
  ExecutionId execution_id;
  WorkloadId workload_id;
  WorkloadGeneration workload_generation;

  [[nodiscard]] bool is_valid() const noexcept { return use_id.is_valid(); }
};

// Accounting record for an admitted serving use.
struct ActiveUse {
  ServingUseId use_id;
  ServingUseGeneration use_generation;
  EngineId engine_id;
  EngineGeneration engine_generation;
  EngineIncarnationId incarnation_id;
  EngineIncarnationGeneration incarnation_generation;
  ReadinessProfileId profile_id;
  ReadinessGeneration readiness_generation;
  ActivationGeneration activation_generation;
  ExecutionId execution_id;
  bool released{false};
  WorkOutcome outcome{WorkOutcome::COMPLETED};
  DurationNs admitted_at_ns{0};
  DurationNs released_at_ns{0};
  std::string detail;
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_SERVING_USE_HPP
