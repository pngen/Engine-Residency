#ifndef ENGINE_RESIDENCY_COMPONENTS_HPP
#define ENGINE_RESIDENCY_COMPONENTS_HPP

#include <cstdint>
#include <string>

#include "engine_residency/clock.hpp"
#include "engine_residency/compatibility.hpp"
#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"
#include "engine_residency/units.hpp"

namespace engine_residency {

// ---------------------------------------------------------------------------
// Component evidence: what a worker/backend claims about a single prepared
// component, bound to the precise incarnation/authority that produced it.
//
// Every evidence record binds:
//   * component identity + category + state;
//   * source identity and boot (the worker/source that produced it);
//   * engine incarnation;
//   * backend/device context and generations;
//   * compatibility key;
//   * evidence sequence/generation;
//   * measured or reported quantities;
//   * provenance;
//   * invalidation reason;
//   * observation time and freshness policy.
//
// An AVAILABLE artifact must never satisfy a requirement for a VERIFIED
// process-local binding; that distinction is enforced by the evaluator, not by
// this struct.
// ---------------------------------------------------------------------------
struct ComponentEvidence {
  // Primary identity
  EvidenceId evidence_id;                    // unique record id (assigned by runtime)
  EvidenceGeneration evidence_generation;    // sequence for this evidence subject
  ComponentCategory category{ComponentCategory::BACKEND};
  ComponentState state{ComponentState::UNKNOWN};
  Provenance provenance{Provenance::UNKNOWN};

  // Subject identity (the logical component being evidenced).
  std::string subject;    // e.g. "model:llama-3.1-8b", "kv-capacity:main"
  std::string namespace_;// evidence namespace, scopes subject collision.

  // Engine/incarnation binding.
  EngineId engine_id;
  EngineGeneration engine_generation;
  EngineIncarnationId incarnation_id;
  EngineIncarnationGeneration incarnation_generation;
  WorkerId worker_id;
  WorkerBootId worker_boot;
  SourceId source_id;
  SourceBootId source_boot;

  // Backend/device/context binding.
  BackendId backend_id;
  BackendGeneration backend_generation;
  DeviceId device_id;
  DeviceGeneration device_generation;
  DeviceContextGeneration device_context_generation;
  std::uint32_t device_ordinal{0};

  // Compatibility.
  CompatibilityKey compatibility;

  // Relevant generations for the component.
  ModelGeneration model_generation;
  AdapterGeneration adapter_generation;
  KvCapacityGeneration kv_capacity_generation;
  KvStateGeneration kv_state_generation;
  KernelGeneration kernel_generation;
  GraphGeneration graph_generation;
  ArtifactGeneration artifact_generation;
  AllocationGeneration allocation_generation;
  DependencyGeneration dependency_generation;
  ResourceClaimGeneration resource_claim_generation;
  WarmupGeneration warmup_generation;
  ReadinessProfileId profile_id;                 // optional profile binding
  ReadinessProfileGeneration profile_generation;

  // Measured / reported quantities.
  Bytes size_bytes{0};
  DurationNs elapsed_ns{0};
  DurationNs observed_at_ns{0};   // clock reading when evidence was recorded
  DurationNs max_age_ns{0};       // 0 == never expires (static), else TTL

  // Explanation / status text.
  std::string detail;
  std::string invalidation_reason;    // why it was invalidated/staled, if any.

  // Authority under which this evidence was produced.
  CoordinatorEpoch coordinator_epoch;

  // Convenience: is the evidence currently fresh given a clock?
  [[nodiscard]] bool is_fresh(DurationNs now_ns) const noexcept {
    if (max_age_ns == 0) return true;  // static evidence never expires.
    if (observed_at_ns == 0) return false;  // unknown observation time.
    return now_ns >= observed_at_ns && (now_ns - observed_at_ns) <= max_age_ns;
  }
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_COMPONENTS_HPP
