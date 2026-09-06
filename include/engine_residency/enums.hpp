#ifndef ENGINE_RESIDENCY_ENUMS_HPP
#define ENGINE_RESIDENCY_ENUMS_HPP

namespace engine_residency {

// ---------------------------------------------------------------------------
// State dimensions. Engine Residency keeps these ORTHOGONAL. A single
// overloaded enum cannot express, for example, an engine that is at desired
// residency HOT, in lifecycle RUNNING, preparation PREPARED, readiness READY,
// activation STANDBY and health HEALTHY. Forcing these into one axis destroys
// the distinctions the runtime exists to make.
// ---------------------------------------------------------------------------

// Desired residency: what the controlling caller WANTS the engine to be.
enum class DesiredResidency { ABSENT, COLD, WARM, HOT };

// Process lifecycle: what the underlying OS process incarnation is doing.
enum class ProcessLifecycle {
  REGISTERING,
  STARTING,
  RUNNING,
  DRAINING,
  STOPPING,
  STOPPED,
  LOST,
  FAILED,
  RETIRED
};

// Preparation state: has a preparation plan been carried out for the target
// configuration and verified?
enum class PreparationState {
  UNPREPARED,
  PLANNED,
  PREPARING,
  PREPARED,
  INVALIDATED,
  FAILED,
  REVALIDATION_REQUIRED
};

// Readiness outcome of a profile-specific evaluation.
enum class ReadinessOutcome {
  UNKNOWN,
  BLOCKED,
  PREPARING,
  READY,
  DEGRADED,
  STALE,
  REVALIDATION_REQUIRED
};

// Activation/role authority state.
enum class ActivationState {
  INACTIVE,
  STANDBY,
  ACTIVATING,
  ACTIVE,
  DRAINING,
  FENCED
};

// Health state of the underlying process/incarnation.
enum class HealthState {
  UNKNOWN,
  HEALTHY,
  DEGRADED,
  UNHEALTHY,
  LOST,
  FENCED
};

// Recovery state of an engine after a failure or coordinator restart.
enum class RecoveryState {
  NOMINAL,
  REVALIDATING,
  RECOVERING,
  REPLACED,
  FAILED,
  AWAITING_AUTHORITY
};

// Registration state of a worker with the authority.
enum class RegistrationState {
  NONE,
  PENDING,
  REGISTERED,
  FENCED,
  REJECTED
};

// Serving role of a logical engine definition.
enum class ServingRole {
  GENERAL_INFERENCE,
  PREFILL,
  DECODE,
  EMBEDDING,
  MULTIMODAL,
  REFERENCE_COMPUTE,
  CUSTOM,
  UNKNOWN
};

// Component category for prepared state pieces.
enum class ComponentCategory {
  BACKEND,
  DEVICE_CONTEXT,
  MODEL,
  ADAPTER,
  KV_CAPACITY,
  KV_STATE,
  KERNEL,
  GRAPH,
  TOKENIZER_ASSET,
  RUNTIME_ASSET,
  WORKSPACE,
  RESOURCE_CLAIM,
  DEPENDENCY,
  WARMUP
};

// Component evidence state.
enum class ComponentState {
  ABSENT,
  DISCOVERED,
  AVAILABLE,
  LOADING,
  BOUND,
  VERIFIED,
  FAILED,
  STALE,
  UNKNOWN,
  REVALIDATION_REQUIRED
};

// How a profile treats a requirement.
enum class RequirementKind {
  REQUIRED,
  OPTIONAL,
  PERMITTED_FALLBACK,
  UNSUPPORTED
};

// Provenance of evidence. Coordinator bookkeeping is NOT physical device
// telemetry; estimated time is NOT measured latency.
enum class Provenance {
  MEASURED,
  REPORTED,
  DERIVED,
  ESTIMATED,
  SYNTHETIC,
  RECONSTRUCTED,
  UNKNOWN
};

// Preparation action steps in a plan.
enum class PreparationAction {
  DISCOVER_BACKEND,
  BIND_DEVICE_CONTEXT,
  ACQUIRE_AUTHORIZED_RESOURCE,
  BIND_MODEL,
  BIND_ADAPTER,
  PROVISION_KV_CAPACITY,
  RESTORE_COMPATIBLE_KV_STATE,
  BIND_KERNEL,
  INSTANTIATE_GRAPH,
  WARM_UP,
  VERIFY_OUTPUT,
  PUBLISH_READINESS
};

// Recovery/economic actions compared by the economics model.
enum class RecoveryAction {
  REUSE_WARM,
  WARM_COLD,
  REBIND_SURVIVING,
  REPLACE_LOST,
  RELOAD_MODEL_STATE,
  RESTORE_COMPATIBLE_KV,
  INSTANTIATE_GRAPH,
  RERUN_WARMUP
};

// Drain phase, an ordered progress dimension distinct from lifecycle.
enum class DrainPhase {
  NONE,
  REQUESTED,
  ADMISSION_FENCED,
  ACTIVE_USE_REMAINING,
  BACKEND_CLEANUP_PENDING,
  DRAINED,
  RETIRED
};

// Reliability outcome for a unit of admitted work after a failure.
enum class WorkOutcome {
  COMPLETED,
  FAILED,
  UNKNOWN,
  INVALIDATED
};

// ---------------------------------------------------------------------------
// to_string helpers
// ---------------------------------------------------------------------------
inline const char* to_string(DesiredResidency v) noexcept {
  switch (v) {
    case DesiredResidency::ABSENT: return "ABSENT";
    case DesiredResidency::COLD: return "COLD";
    case DesiredResidency::WARM: return "WARM";
    case DesiredResidency::HOT: return "HOT";
  }
  return "UNKNOWN";
}
inline const char* to_string(ProcessLifecycle v) noexcept {
  switch (v) {
    case ProcessLifecycle::REGISTERING: return "REGISTERING";
    case ProcessLifecycle::STARTING: return "STARTING";
    case ProcessLifecycle::RUNNING: return "RUNNING";
    case ProcessLifecycle::DRAINING: return "DRAINING";
    case ProcessLifecycle::STOPPING: return "STOPPING";
    case ProcessLifecycle::STOPPED: return "STOPPED";
    case ProcessLifecycle::LOST: return "LOST";
    case ProcessLifecycle::FAILED: return "FAILED";
    case ProcessLifecycle::RETIRED: return "RETIRED";
  }
  return "UNKNOWN";
}
inline const char* to_string(PreparationState v) noexcept {
  switch (v) {
    case PreparationState::UNPREPARED: return "UNPREPARED";
    case PreparationState::PLANNED: return "PLANNED";
    case PreparationState::PREPARING: return "PREPARING";
    case PreparationState::PREPARED: return "PREPARED";
    case PreparationState::INVALIDATED: return "INVALIDATED";
    case PreparationState::FAILED: return "FAILED";
    case PreparationState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}
inline const char* to_string(ReadinessOutcome v) noexcept {
  switch (v) {
    case ReadinessOutcome::UNKNOWN: return "UNKNOWN";
    case ReadinessOutcome::BLOCKED: return "BLOCKED";
    case ReadinessOutcome::PREPARING: return "PREPARING";
    case ReadinessOutcome::READY: return "READY";
    case ReadinessOutcome::DEGRADED: return "DEGRADED";
    case ReadinessOutcome::STALE: return "STALE";
    case ReadinessOutcome::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}
inline const char* to_string(ActivationState v) noexcept {
  switch (v) {
    case ActivationState::INACTIVE: return "INACTIVE";
    case ActivationState::STANDBY: return "STANDBY";
    case ActivationState::ACTIVATING: return "ACTIVATING";
    case ActivationState::ACTIVE: return "ACTIVE";
    case ActivationState::DRAINING: return "DRAINING";
    case ActivationState::FENCED: return "FENCED";
  }
  return "UNKNOWN";
}
inline const char* to_string(HealthState v) noexcept {
  switch (v) {
    case HealthState::UNKNOWN: return "UNKNOWN";
    case HealthState::HEALTHY: return "HEALTHY";
    case HealthState::DEGRADED: return "DEGRADED";
    case HealthState::UNHEALTHY: return "UNHEALTHY";
    case HealthState::LOST: return "LOST";
    case HealthState::FENCED: return "FENCED";
  }
  return "UNKNOWN";
}
inline const char* to_string(RecoveryState v) noexcept {
  switch (v) {
    case RecoveryState::NOMINAL: return "NOMINAL";
    case RecoveryState::REVALIDATING: return "REVALIDATING";
    case RecoveryState::RECOVERING: return "RECOVERING";
    case RecoveryState::REPLACED: return "REPLACED";
    case RecoveryState::FAILED: return "FAILED";
    case RecoveryState::AWAITING_AUTHORITY: return "AWAITING_AUTHORITY";
  }
  return "UNKNOWN";
}
inline const char* to_string(RegistrationState v) noexcept {
  switch (v) {
    case RegistrationState::NONE: return "NONE";
    case RegistrationState::PENDING: return "PENDING";
    case RegistrationState::REGISTERED: return "REGISTERED";
    case RegistrationState::FENCED: return "FENCED";
    case RegistrationState::REJECTED: return "REJECTED";
  }
  return "UNKNOWN";
}
inline const char* to_string(ServingRole v) noexcept {
  switch (v) {
    case ServingRole::GENERAL_INFERENCE: return "GENERAL_INFERENCE";
    case ServingRole::PREFILL: return "PREFILL";
    case ServingRole::DECODE: return "DECODE";
    case ServingRole::EMBEDDING: return "EMBEDDING";
    case ServingRole::MULTIMODAL: return "MULTIMODAL";
    case ServingRole::REFERENCE_COMPUTE: return "REFERENCE_COMPUTE";
    case ServingRole::CUSTOM: return "CUSTOM";
    case ServingRole::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}
inline const char* to_string(ComponentCategory v) noexcept {
  switch (v) {
    case ComponentCategory::BACKEND: return "BACKEND";
    case ComponentCategory::DEVICE_CONTEXT: return "DEVICE_CONTEXT";
    case ComponentCategory::MODEL: return "MODEL";
    case ComponentCategory::ADAPTER: return "ADAPTER";
    case ComponentCategory::KV_CAPACITY: return "KV_CAPACITY";
    case ComponentCategory::KV_STATE: return "KV_STATE";
    case ComponentCategory::KERNEL: return "KERNEL";
    case ComponentCategory::GRAPH: return "GRAPH";
    case ComponentCategory::TOKENIZER_ASSET: return "TOKENIZER_ASSET";
    case ComponentCategory::RUNTIME_ASSET: return "RUNTIME_ASSET";
    case ComponentCategory::WORKSPACE: return "WORKSPACE";
    case ComponentCategory::RESOURCE_CLAIM: return "RESOURCE_CLAIM";
    case ComponentCategory::DEPENDENCY: return "DEPENDENCY";
    case ComponentCategory::WARMUP: return "WARMUP";
  }
  return "UNKNOWN";
}
inline const char* to_string(ComponentState v) noexcept {
  switch (v) {
    case ComponentState::ABSENT: return "ABSENT";
    case ComponentState::DISCOVERED: return "DISCOVERED";
    case ComponentState::AVAILABLE: return "AVAILABLE";
    case ComponentState::LOADING: return "LOADING";
    case ComponentState::BOUND: return "BOUND";
    case ComponentState::VERIFIED: return "VERIFIED";
    case ComponentState::FAILED: return "FAILED";
    case ComponentState::STALE: return "STALE";
    case ComponentState::UNKNOWN: return "UNKNOWN";
    case ComponentState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}
inline const char* to_string(RequirementKind v) noexcept {
  switch (v) {
    case RequirementKind::REQUIRED: return "REQUIRED";
    case RequirementKind::OPTIONAL: return "OPTIONAL";
    case RequirementKind::PERMITTED_FALLBACK: return "PERMITTED_FALLBACK";
    case RequirementKind::UNSUPPORTED: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}
inline const char* to_string(Provenance v) noexcept {
  switch (v) {
    case Provenance::MEASURED: return "MEASURED";
    case Provenance::REPORTED: return "REPORTED";
    case Provenance::DERIVED: return "DERIVED";
    case Provenance::ESTIMATED: return "ESTIMATED";
    case Provenance::SYNTHETIC: return "SYNTHETIC";
    case Provenance::RECONSTRUCTED: return "RECONSTRUCTED";
    case Provenance::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}
inline const char* to_string(PreparationAction v) noexcept {
  switch (v) {
    case PreparationAction::DISCOVER_BACKEND: return "DISCOVER_BACKEND";
    case PreparationAction::BIND_DEVICE_CONTEXT: return "BIND_DEVICE_CONTEXT";
    case PreparationAction::ACQUIRE_AUTHORIZED_RESOURCE: return "ACQUIRE_AUTHORIZED_RESOURCE";
    case PreparationAction::BIND_MODEL: return "BIND_MODEL";
    case PreparationAction::BIND_ADAPTER: return "BIND_ADAPTER";
    case PreparationAction::PROVISION_KV_CAPACITY: return "PROVISION_KV_CAPACITY";
    case PreparationAction::RESTORE_COMPATIBLE_KV_STATE: return "RESTORE_COMPATIBLE_KV_STATE";
    case PreparationAction::BIND_KERNEL: return "BIND_KERNEL";
    case PreparationAction::INSTANTIATE_GRAPH: return "INSTANTIATE_GRAPH";
    case PreparationAction::WARM_UP: return "WARM_UP";
    case PreparationAction::VERIFY_OUTPUT: return "VERIFY_OUTPUT";
    case PreparationAction::PUBLISH_READINESS: return "PUBLISH_READINESS";
  }
  return "UNKNOWN";
}
inline const char* to_string(RecoveryAction v) noexcept {
  switch (v) {
    case RecoveryAction::REUSE_WARM: return "REUSE_WARM";
    case RecoveryAction::WARM_COLD: return "WARM_COLD";
    case RecoveryAction::REBIND_SURVIVING: return "REBIND_SURVIVING";
    case RecoveryAction::REPLACE_LOST: return "REPLACE_LOST";
    case RecoveryAction::RELOAD_MODEL_STATE: return "RELOAD_MODEL_STATE";
    case RecoveryAction::RESTORE_COMPATIBLE_KV: return "RESTORE_COMPATIBLE_KV";
    case RecoveryAction::INSTANTIATE_GRAPH: return "INSTANTIATE_GRAPH";
    case RecoveryAction::RERUN_WARMUP: return "RERUN_WARMUP";
  }
  return "UNKNOWN";
}
inline const char* to_string(DrainPhase v) noexcept {
  switch (v) {
    case DrainPhase::NONE: return "NONE";
    case DrainPhase::REQUESTED: return "REQUESTED";
    case DrainPhase::ADMISSION_FENCED: return "ADMISSION_FENCED";
    case DrainPhase::ACTIVE_USE_REMAINING: return "ACTIVE_USE_REMAINING";
    case DrainPhase::BACKEND_CLEANUP_PENDING: return "BACKEND_CLEANUP_PENDING";
    case DrainPhase::DRAINED: return "DRAINED";
    case DrainPhase::RETIRED: return "RETIRED";
  }
  return "NONE";
}
inline const char* to_string(WorkOutcome v) noexcept {
  switch (v) {
    case WorkOutcome::COMPLETED: return "COMPLETED";
    case WorkOutcome::FAILED: return "FAILED";
    case WorkOutcome::UNKNOWN: return "UNKNOWN";
    case WorkOutcome::INVALIDATED: return "INVALIDATED";
  }
  return "UNKNOWN";
}

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_ENUMS_HPP
