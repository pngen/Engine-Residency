#ifndef ENGINE_RESIDENCY_PROFILES_HPP
#define ENGINE_RESIDENCY_PROFILES_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "engine_residency/compatibility.hpp"
#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"
#include "engine_residency/units.hpp"

namespace engine_residency {

// Which generation family a profile requirement must match against the engine
// definition. This lets the evaluator enforce that an evidence record's
// component generation equals the current authoritative definition generation.
// NONE means no cross-check (the evidence simply needs the required state).
enum class GenerationFamily {
  NONE,
  MODEL,
  ADAPTER,
  KV_CAPACITY,
  KV_STATE,
  KERNEL,
  GRAPH,
  ALLOCATION,
  RESOURCE_CLAIM,
  DEPENDENCY,
  COMPATIBILITY
};

inline const char* to_string(GenerationFamily v) noexcept {
  switch (v) {
    case GenerationFamily::NONE: return "NONE";
    case GenerationFamily::MODEL: return "MODEL";
    case GenerationFamily::ADAPTER: return "ADAPTER";
    case GenerationFamily::KV_CAPACITY: return "KV_CAPACITY";
    case GenerationFamily::KV_STATE: return "KV_STATE";
    case GenerationFamily::KERNEL: return "KERNEL";
    case GenerationFamily::GRAPH: return "GRAPH";
    case GenerationFamily::ALLOCATION: return "ALLOCATION";
    case GenerationFamily::RESOURCE_CLAIM: return "RESOURCE_CLAIM";
    case GenerationFamily::DEPENDENCY: return "DEPENDENCY";
    case GenerationFamily::COMPATIBILITY: return "COMPATIBILITY";
  }
  return "NONE";
}

// A single requirement on a component category within a readiness profile.
struct ProfileRequirement {
  ComponentCategory category{ComponentCategory::MODEL};
  RequirementKind kind{RequirementKind::REQUIRED};
  // The minimum component state that satisfies this requirement. An AVAILABLE
  // artifact therefore never satisfies a requirement whose min_state is
  // BOUND or VERIFIED: this is precisely the "cached artifact is not a loaded
  // model" doctrine, encoded as a type-level rule.
  ComponentState min_state{ComponentState::VERIFIED};
  // Optional subject selector (empty == any subject of that category).
  std::string subject;
  // Optional exact compatibility key that the evidence must match.
  CompatibilityKey compatibility;
  // Generation family to cross-check against the engine definition (if any).
  GenerationFamily generation_family{GenerationFamily::NONE};
  // Human-readable description for explanations.
  std::string description;
};

// A readiness profile binds the exact prerequisites for a specific shape of
// work. A single successful warmup for one profile does NOT mean every profile
// is ready; profiles are isolated.
struct ReadinessProfile {
  ReadinessProfileId profile_id;
  ReadinessProfileGeneration profile_generation;
  EngineId engine_id;
  EngineGeneration engine_generation;      // config generation this profile is bound to.
  std::string name;
  ServingRole role{ServingRole::GENERAL_INFERENCE};

  // Shape/layout limits.
  std::uint32_t max_batch{1};
  std::uint32_t max_seq_len{0};   // 0 == unbounded for the reference engine.
  std::string dtype;              // e.g. "fp32", "fp16" -- exact.
  std::string layout;             // e.g. "row-major". Exact.

  // Resource expectations.
  Bytes workspace_bytes{0};
  Bytes model_bytes{0};
  std::uint64_t kv_capacity_entries{0};   // logical KV capacity required.

  // Graph policy. These are precise and never silently relaxed.
  bool graph_required{false};     // graph replay is REQUIRED.
  bool graph_optional_fallback{false};  // graph replay optional, validated fallback allowed.

  bool warmup_required{true};     // a real warmup must have completed.
  std::uint32_t device_ordinal{0};       // only usable on this device ordinal.
  bool device_specific{false};           // if true, device_ordinal must match.
  bool profile_required{true};

  // Dependencies and policy generations the profile depends on.
  PolicyGeneration policy_generation;
  DependencyGeneration dependency_generation;

  // The requirements that must be satisfied for READY.
  std::vector<ProfileRequirement> requirements;

  // The compatibility key that characterizes this profile's work.
  CompatibilityKey compatibility;
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_PROFILES_HPP
