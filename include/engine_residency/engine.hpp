#ifndef ENGINE_RESIDENCY_ENGINE_HPP
#define ENGINE_RESIDENCY_ENGINE_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "engine_residency/compatibility.hpp"
#include "engine_residency/components.hpp"
#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"
#include "engine_residency/state.hpp"
#include "engine_residency/units.hpp"

namespace engine_residency {

// ---------------------------------------------------------------------------
// Engine definition: the LOGICAL engine, independent of any running process.
// ---------------------------------------------------------------------------
struct EngineDefinition {
  EngineId engine_id;
  EngineGeneration engine_generation;        // definition generation.
  EngineConfigGeneration config_generation;  // configuration generation.
  std::string name;
  ServingRole role{ServingRole::GENERAL_INFERENCE};

  // Backend requirements.
  BackendId backend_id;
  BackendGeneration backend_generation;
  bool backend_specific{true};

  // Model identity and generation.
  ModelId model_id;
  ModelGeneration model_generation;
  std::string model_name;
  Bytes model_bytes{0};

  // Adapter requirements.
  AdapterGeneration adapter_generation;
  bool requires_adapter{false};

  // Device/capability requirements.
  DeviceId device_id;
  DeviceGeneration device_generation;
  std::uint32_t device_ordinal{0};
  bool device_specific{false};

  // Memory requirements.
  Bytes workspace_bytes{0};

  // KV capacity requirements.
  std::uint64_t kv_capacity_entries{0};

  // Kernel requirements.
  std::string kernel_key;
  KernelGeneration kernel_generation;
  bool kernel_required{true};

  // Graph requirements. Precise: never silently relaxed.
  bool graph_required{false};
  bool graph_optional_fallback{false};
  GraphGeneration graph_generation;

  // Tokenizer/runtime assets.
  std::string tokenizer_key;
  ArtifactGeneration tokenizer_generation;
  bool tokenizer_required{false};

  // Compatibility requirements.
  CompatibilityKey compatibility;
  bool compatibility_required{true};

  // Preparation policy.
  std::string preparation_policy;   // e.g. "full" or "incremental".
  bool warmup_required{true};

  // Standby policy.
  bool standby_eligible{false};
  std::uint32_t standby_target{0};

  // Concurrency limits.
  std::uint32_t max_concurrency{1};

  // Resource claims / dependency references.
  ResourceClaimGeneration resource_claim_generation;
  std::string resource_claim_ref;
  std::vector<std::string> dependency_refs;
  DependencyGeneration dependency_generation;

  // Provenance of the definition.
  Provenance provenance{Provenance::REPORTED};

  // Readiness profiles supported by this engine (full definitions live in the
  // runtime's profile store referenced by these ids).
  std::vector<ReadinessProfileId> profile_ids;
};

// ---------------------------------------------------------------------------
// Engine incarnation: a particular process (or "boot") of an engine, bound to
// a precise authority tuple. A PID is diagnostic metadata, never durable
// identity.
// ---------------------------------------------------------------------------
struct EngineIncarnation {
  EngineIncarnationId incarnation_id;
  EngineIncarnationGeneration incarnation_generation;
  EngineId engine_id;
  EngineGeneration engine_generation;       // definition generation bound.
  EngineConfigGeneration config_generation; // configuration generation bound.
  WorkerId worker_id;
  WorkerBootId worker_boot;
  SourceId source_id;
  SourceBootId source_boot;
  CoordinatorEpoch coordinator_epoch;
  AuthorityGeneration authority_generation;
  RegistrationPermitId registration_permit;

  BackendId backend_id;
  BackendGeneration backend_generation;
  DeviceId device_id;
  DeviceGeneration device_generation;
  DeviceContextGeneration device_context_generation;
  std::uint32_t device_ordinal{0};

  ProcessId process;   // diagnostic metadata ONLY.

  RegistrationState registration{RegistrationState::NONE};
  StateSet state;

  // Current readiness authority generation for this incarnation.
  ReadinessGeneration readiness_generation;
  // Whether this is the current authoritative incarnation for its engine.
  bool is_current{false};

  DurationNs last_activity_ns{0};
  std::string nfc;   // "not for computation": human diagnostic provenance string.
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_ENGINE_HPP
