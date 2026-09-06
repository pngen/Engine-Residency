#ifndef ENGINE_RESIDENCY_STANDBY_HPP
#define ENGINE_RESIDENCY_STANDBY_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "engine_residency/engine.hpp"
#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"

namespace engine_residency {

// A standby slot: one logical position that may hold at most one engine.
struct StandbySlot {
  EngineSlotId slot_id;
  EnginePoolId pool_id;
  std::string name;
  bool exclusive{true};                    // at most one activation.
  EngineIncarnationId assigned_incarnation;
  bool current{true};
};

// The pool policy supplied to local reconciliation.
struct StandbyPoolPolicy {
  EnginePoolId pool_id;
  EnginePoolGeneration pool_generation;
  EngineId engine_id;
  std::uint32_t target_standby{0};
  std::uint32_t target_active{0};
  bool require_current_incarnation{true};  // dead/stale never counts.
  bool allow_shared_capacity{false};
  std::string tie_break;                   // stable tie-break ("incarnation_id","worker_boot").
};

// Aggregate standby accounting for a pool. No double counting: shared capacity
// is never added twice behind multiple profiles/slots.
struct StandbyAccounting {
  EnginePoolId pool_id;
  std::uint32_t target{0};
  std::uint32_t eligible{0};
  std::uint32_t preparing{0};
  std::uint32_t blocked{0};
  std::uint32_t stale{0};
  std::uint32_t active{0};
  std::uint32_t deficit{0};
  std::vector<std::string> blockers;       // deterministic blockers.
  std::vector<EngineIncarnationId> candidates; // ordered, stable, eligible.
  std::vector<EngineSlotId> deferred_slots;
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_STANDBY_HPP
