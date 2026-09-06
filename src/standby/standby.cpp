#include "detail/internal.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>

namespace engine_residency {
namespace detail {

StandbyAccounting reconcile_standby(
    const StandbyPoolPolicy& policy,
    const std::vector<EngineSlotId>& slots,
    const std::vector<const EngineIncarnation*>& eligible_candidates,
    const std::vector<const EngineIncarnation*>& blocked,
    const std::unordered_map<EngineIncarnationId, ReadinessOutcome>& readiness,
    std::uint32_t active_count) {
  StandbyAccounting acc;
  acc.pool_id = policy.pool_id;
  acc.target = policy.target_standby;

  // Sort eligible candidates stably (by incarnation id, then process pid).
  std::vector<const EngineIncarnation*> ordered = eligible_candidates;
  std::sort(ordered.begin(), ordered.end(),
            [](const EngineIncarnation* a, const EngineIncarnation* b) {
              if (a->incarnation_id != b->incarnation_id)
                return a->incarnation_id < b->incarnation_id;
              return a->process.pid < b->process.pid;
            });

  // Apply hard exclusions: only current, running, warm/ready incarnations count
  // as eligible standby capacity.
  std::vector<const EngineIncarnation*> eligible;
  for (const auto* c : ordered) {
    // A warm, non-fenced, non-active incarnation counts even if it is not the
    // current serving authority; that is precisely what standby capacity is.
    if (c->state.lifecycle != ProcessLifecycle::RUNNING) continue;
    if (c->state.activation == ActivationState::ACTIVE) continue;
    // Eligibility is determined by current preparedness (readiness), not the
    // transient preparation-state field; a warm engine at READY counts.
    // Must be READY (or DEGRADED under permitted fallback) for its profile.
    auto it = readiness.find(c->incarnation_id);
    if (it == readiness.end() ||
        (it->second != ReadinessOutcome::READY && it->second != ReadinessOutcome::DEGRADED)) {
      acc.stale += 1;
      continue;
    }
    eligible.push_back(c);
  }

  // Slot assignment: at most one eligible incarnation per slot.
  if (!slots.empty()) {
    std::size_t assigned = 0;
    for (const auto& slot : slots) {
      if (assigned < eligible.size()) {
        acc.candidates.push_back(eligible[assigned]->incarnation_id);
        ++assigned;
      } else {
        acc.deferred_slots.push_back(slot);
      }
    }
    acc.eligible = static_cast<std::uint32_t>(std::min(eligible.size(), slots.size()));
  } else {
    acc.eligible = static_cast<std::uint32_t>(eligible.size());
  }

  for (const auto* b : blocked) {
    acc.blockers.push_back("blocked:" + std::to_string(b->incarnation_id.raw()) + ":" +
                           std::string(to_string(b->state.preparation)));
  }

  acc.active = active_count;
  if (acc.target > acc.eligible) {
    acc.deficit = acc.target - acc.eligible;
  } else {
    acc.deficit = 0;
  }

  if (acc.deficit > 0) {
    for (std::uint32_t i = 0; i < acc.deficit && i < 8; ++i) {
      acc.blockers.push_back("deficit:need " + std::to_string(acc.deficit) + " standby");
    }
  }

  return acc;
}

}  // namespace detail
}  // namespace engine_residency
