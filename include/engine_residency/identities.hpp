#ifndef ENGINE_RESIDENCY_IDENTITIES_HPP
#define ENGINE_RESIDENCY_IDENTITIES_HPP

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <type_traits>

#include "engine_residency/errors.hpp"

namespace engine_residency {

// ---------------------------------------------------------------------------
// Strong identity and generation wrappers.
//
// Identity<Tag> identifies an object of a class; Generation<Tag> counts how
// many times that class has been superseded/advanced. Both are opaque 64-bit
// values, but each Tag produces a DISTINCT C++ type, so an EngineGeneration and
// a ModelGeneration can never be silently interchanged, and an identity can
// never be compared to a generation. Semantically independent generations are
// therefore never collapsed into a single integer type.
// ---------------------------------------------------------------------------

template <typename Tag>
struct Identity {
  using ValueType = std::uint64_t;
  ValueType value{0};

  Identity() noexcept = default;
  constexpr explicit Identity(ValueType v) noexcept : value(v) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0; }
  [[nodiscard]] constexpr ValueType raw() const noexcept { return value; }

  friend constexpr bool operator==(Identity a, Identity b) noexcept { return a.value == b.value; }
  friend constexpr bool operator!=(Identity a, Identity b) noexcept { return a.value != b.value; }
  friend constexpr bool operator<(Identity a, Identity b) noexcept { return a.value < b.value; }
};

template <typename Tag>
struct Generation {
  using ValueType = std::uint64_t;
  ValueType value{0};

  Generation() noexcept = default;
  constexpr explicit Generation(ValueType v) noexcept : value(v) {}

  [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0; }
  [[nodiscard]] constexpr ValueType raw() const noexcept { return value; }

  // Advance to the next generation. Generation exhaustion is handled
  // EXPLICITLY: when the counter reaches the maximum representable value, we
  // throw instead of wrapping into a previously valid authority value.
  [[nodiscard]] Generation next() const {
    if (value == static_cast<ValueType>(-1)) {
      throw_error(ErrorCode::GenerationExhausted,
                  "generation exhausted; refusing to wrap into a previously valid value");
    }
    return Generation(value + 1);
  }

  [[nodiscard]] constexpr bool is_before(Generation other) const noexcept {
    return value < other.value;
  }
  [[nodiscard]] constexpr bool is_at_or_before(Generation other) const noexcept {
    return value <= other.value;
  }
  [[nodiscard]] constexpr bool is_at_or_after(Generation other) const noexcept {
    return value >= other.value;
  }

  friend constexpr bool operator==(Generation a, Generation b) noexcept { return a.value == b.value; }
  friend constexpr bool operator!=(Generation a, Generation b) noexcept { return a.value != b.value; }
  friend constexpr bool operator<(Generation a, Generation b) noexcept { return a.value < b.value; }
  friend constexpr bool operator<=(Generation a, Generation b) noexcept { return a.value <= b.value; }
  friend constexpr bool operator>(Generation a, Generation b) noexcept { return a.value > b.value; }
  friend constexpr bool operator>=(Generation a, Generation b) noexcept { return a.value >= b.value; }
};

// ---------------------------------------------------------------------------
// ProcessId: diagnostic metadata ONLY. A PID is not durable identity and PID
// reuse must not revive an old incarnation. It is intentionally a plain struct
// carrying diagnostic facts, not an Identity<Tag>.
// ---------------------------------------------------------------------------
struct ProcessId {
  std::uint32_t pid{0};              // OS process id (diagnostic only)
  std::uint64_t start_seconds{0};    // Best-effort process start time, if known.
  std::string label;                 // Human-readable diagnostic label.

  [[nodiscard]] bool is_valid() const noexcept { return pid != 0; }
  friend bool operator==(const ProcessId& a, const ProcessId& b) noexcept {
    return a.pid == b.pid && a.start_seconds == b.start_seconds && a.label == b.label;
  }
  friend bool operator!=(const ProcessId& a, const ProcessId& b) noexcept { return !(a == b); }
};

// ---------------------------------------------------------------------------
// Tag definitions and concrete identity/generation aliases.
// ---------------------------------------------------------------------------
#define ER_TAG(n) struct n##Tag {};
#define ER_ID(n) using n = Identity<n##Tag>;
#define ER_GEN(n) using n = Generation<n##Tag>;

// Engine and configuration
ER_TAG(EngineId) ER_ID(EngineId)
ER_TAG(EngineGeneration) ER_GEN(EngineGeneration)
ER_TAG(EngineConfigGeneration) ER_GEN(EngineConfigGeneration)
ER_TAG(EngineIncarnationId) ER_ID(EngineIncarnationId)
ER_TAG(EngineIncarnationGeneration) ER_GEN(EngineIncarnationGeneration)
ER_TAG(EnginePoolId) ER_ID(EnginePoolId)
ER_TAG(EnginePoolGeneration) ER_GEN(EnginePoolGeneration)
ER_TAG(EngineSlotId) ER_ID(EngineSlotId)
ER_TAG(WorkerId) ER_ID(WorkerId)
ER_TAG(WorkerBootId) ER_GEN(WorkerBootId)
ER_TAG(SourceId) ER_ID(SourceId)
ER_TAG(SourceBootId) ER_GEN(SourceBootId)
ER_TAG(CoordinatorEpoch) ER_GEN(CoordinatorEpoch)
ER_TAG(AuthorityGeneration) ER_GEN(AuthorityGeneration)
ER_TAG(RegistrationPermitId) ER_ID(RegistrationPermitId)
ER_TAG(BackendId) ER_ID(BackendId)
ER_TAG(BackendGeneration) ER_GEN(BackendGeneration)
ER_TAG(DeviceId) ER_ID(DeviceId)
ER_TAG(DeviceGeneration) ER_GEN(DeviceGeneration)
ER_TAG(DeviceContextGeneration) ER_GEN(DeviceContextGeneration)
ER_TAG(ReadinessProfileId) ER_ID(ReadinessProfileId)
ER_TAG(ReadinessProfileGeneration) ER_GEN(ReadinessProfileGeneration)
ER_TAG(ReadinessGeneration) ER_GEN(ReadinessGeneration)
ER_TAG(EvidenceId) ER_ID(EvidenceId)
ER_TAG(EvidenceGeneration) ER_GEN(EvidenceGeneration)
ER_TAG(PreparationPlanId) ER_ID(PreparationPlanId)
ER_TAG(PreparationPlanGeneration) ER_GEN(PreparationPlanGeneration)
ER_TAG(PreparationAttemptId) ER_ID(PreparationAttemptId)
ER_TAG(PreparationAttemptGeneration) ER_GEN(PreparationAttemptGeneration)
ER_TAG(WarmupGeneration) ER_GEN(WarmupGeneration)
ER_TAG(StandbyGeneration) ER_GEN(StandbyGeneration)
ER_TAG(ActivationId) ER_ID(ActivationId)
ER_TAG(ActivationGeneration) ER_GEN(ActivationGeneration)
ER_TAG(ServingUseId) ER_ID(ServingUseId)
ER_TAG(ServingUseGeneration) ER_GEN(ServingUseGeneration)
ER_TAG(DrainGeneration) ER_GEN(DrainGeneration)
ER_TAG(ReplacementId) ER_ID(ReplacementId)
ER_TAG(ReplacementGeneration) ER_GEN(ReplacementGeneration)
ER_TAG(RecoveryGeneration) ER_GEN(RecoveryGeneration)
ER_TAG(RevalidationGeneration) ER_GEN(RevalidationGeneration)
ER_TAG(ModelId) ER_ID(ModelId)
ER_TAG(ModelGeneration) ER_GEN(ModelGeneration)
ER_TAG(ModelResidencyGeneration) ER_GEN(ModelResidencyGeneration)
ER_TAG(AdapterGeneration) ER_GEN(AdapterGeneration)
ER_TAG(KvCapacityGeneration) ER_GEN(KvCapacityGeneration)
ER_TAG(KvStateGeneration) ER_GEN(KvStateGeneration)
ER_TAG(KernelGeneration) ER_GEN(KernelGeneration)
ER_TAG(GraphGeneration) ER_GEN(GraphGeneration)
ER_TAG(ArtifactGeneration) ER_GEN(ArtifactGeneration)
ER_TAG(DependencyGeneration) ER_GEN(DependencyGeneration)
ER_TAG(AllocationGeneration) ER_GEN(AllocationGeneration)
ER_TAG(ResourceClaimGeneration) ER_GEN(ResourceClaimGeneration)
ER_TAG(ReservationGeneration) ER_GEN(ReservationGeneration)
ER_TAG(CapacityGeneration) ER_GEN(CapacityGeneration)
ER_TAG(CompatibilityGeneration) ER_GEN(CompatibilityGeneration)
ER_TAG(PolicyGeneration) ER_GEN(PolicyGeneration)
ER_TAG(WorkloadId) ER_ID(WorkloadId)
ER_TAG(WorkloadGeneration) ER_GEN(WorkloadGeneration)
ER_TAG(ExecutionId) ER_ID(ExecutionId)
ER_TAG(ExecutionGeneration) ER_GEN(ExecutionGeneration)

#undef ER_TAG
#undef ER_ID
#undef ER_GEN

// A uniquely-identifying name that renders an authority tuple for logging.
template <typename Tag>
inline std::string to_string(const Identity<Tag>& id) {
  return std::to_string(id.raw());
}
template <typename Tag>
inline std::string to_string(const Generation<Tag>& g) {
  return std::to_string(g.raw());
}

}  // namespace engine_residency

namespace std {
template <typename Tag>
struct hash<engine_residency::Identity<Tag>> {
  size_t operator()(const engine_residency::Identity<Tag>& id) const noexcept {
    return std::hash<std::uint64_t>{}(id.raw());
  }
};
template <typename Tag>
struct hash<engine_residency::Generation<Tag>> {
  size_t operator()(const engine_residency::Generation<Tag>& g) const noexcept {
    return std::hash<std::uint64_t>{}(g.raw());
  }
};
}  // namespace std

#endif  // ENGINE_RESIDENCY_IDENTITIES_HPP
