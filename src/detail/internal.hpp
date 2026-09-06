#ifndef ENGINE_RESIDENCY_DETAIL_INTERNAL_HPP
#define ENGINE_RESIDENCY_DETAIL_INTERNAL_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine_residency/components.hpp"
#include "engine_residency/drain.hpp"
#include "engine_residency/economics.hpp"
#include "engine_residency/engine.hpp"
#include "engine_residency/profiles.hpp"
#include "engine_residency/readiness.hpp"
#include "engine_residency/standby.hpp"

namespace engine_residency {
namespace detail {

// ---- readiness ------------------------------------------------------------
// The best (most recent, fresh, non-invalidated) evidence for a category and,
// if non-empty, exact subject. Returns nullptr if none is usable.
const ComponentEvidence* best_evidence(
    const std::vector<ComponentEvidence>& evidence,
    DurationNs now_ns,
    ComponentCategory category,
    const std::string& subject,
    const std::string& evidence_namespace);

// Deterministic readiness evaluation for one (incarnation, profile) against an
// evidence set, using the engine definition for generation/compat cross-checks.
ReadinessResult evaluate_readiness(const EngineDefinition& engine,
                                   const ReadinessProfile& profile,
                                   const EngineIncarnation& inc,
                                   const std::vector<ComponentEvidence>& evidence,
                                   DurationNs now_ns);

// ---- standby --------------------------------------------------------------
// Deterministic local reconciliation for a pool policy. Hard compatibility and
// authority exclusions are applied first; stable tie-breaking orders candidates.
StandbyAccounting reconcile_standby(
    const StandbyPoolPolicy& policy,
    const std::vector<EngineSlotId>& slots,
    const std::vector<const EngineIncarnation*>& eligible_candidates,
    const std::vector<const EngineIncarnation*>& blocked,
    const std::unordered_map<EngineIncarnationId, ReadinessOutcome>& readiness,
    std::uint32_t active_count);

// ---- economics ------------------------------------------------------------
// Aggregates named cost components over the given aggregation model. Does not
// fabricate values: unknown components remain unknown.
RecoveryCost aggregate_recovery_cost(std::vector<CostComponent> components,
                                     RecoveryAction action,
                                     const std::string& aggregation_model);
// Deterministically selects the lowest total cost among supplied alternatives.
CostComparison compare_recovery_costs(const std::vector<RecoveryCost>& alternatives);

// ---- validation -----------------------------------------------------------
// Validates a recovered persistence blob. Returns true and fills the output
// structs only if the blob is fully consistent. Corrupt input returns false
// without mutating any output.
bool validate_dependency_generation_is_monotonic(
    const std::vector<std::uint64_t>& generations);
}  // namespace detail
}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_DETAIL_INTERNAL_HPP
