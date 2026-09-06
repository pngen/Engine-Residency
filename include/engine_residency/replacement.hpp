#ifndef ENGINE_RESIDENCY_REPLACEMENT_HPP
#define ENGINE_RESIDENCY_REPLACEMENT_HPP

#include <cstdint>
#include <string>

#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"
#include "engine_residency/units.hpp"

namespace engine_residency {

// Replacement is a first-class operation: an old and a candidate incarnation
// are kept physically distinct until an authorized cutover.
struct ReplacementPlan {
  ReplacementId replacement_id;
  ReplacementGeneration replacement_generation;
  EngineId engine_id;
  EngineGeneration engine_generation;
  EngineIncarnationId old_incarnation;
  EngineIncarnationGeneration old_incarnation_generation;
  EngineIncarnationId candidate_incarnation;
  EngineIncarnationGeneration candidate_incarnation_generation;
  CoordinatorEpoch epoch;
  AuthorityGeneration authority;

  // Current phase of the replacement.
  enum class Phase {
    PLANNED,          // candidate identified.
    PREPARING,        // candidate preparing (old retains authority).
    CANDIDATE_READY,  // candidate proved ready.
    CUTOVER,          // authoritative cutover committed.
    OLD_DRAIN,        // old admitted work drains.
    RETIRE_OLD,       // old retired.
    COMPLETE
  };
  Phase phase{Phase::PLANNED};

  bool make_before_break{true};   // false means break-before-make (no zero downtime claim).
  bool rollback_required{false};
  std::string detail;

  // Resource overlap requirement: candidate must be able to hold its own state;
  // if insufficient, replacement returns a typed BLOCKED result.
  Bytes overlap_bytes{0};
  bool resource_overlap_available{false};
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_REPLACEMENT_HPP
