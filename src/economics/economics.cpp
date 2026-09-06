#include "detail/internal.hpp"

#include <algorithm>
#include <cstdint>

namespace engine_residency {
namespace detail {

RecoveryCost aggregate_recovery_cost(std::vector<CostComponent> components,
                                     RecoveryAction action,
                                     const std::string& aggregation_model) {
  RecoveryCost cost;
  cost.action = action;
  cost.components = std::move(components);
  cost.aggregation_model = aggregation_model;
  cost.provenance = Provenance::ESTIMATED;

  std::uint64_t total = 0;
  std::uint64_t critical_path = 0;
  bool any_measured = false;
  bool any_unknown = false;

  for (const auto& c : cost.components) {
    const std::uint64_t eff = c.effective_ns();
    if (c.provenance == Provenance::MEASURED) any_measured = true;
    if (c.effective_ns() == 0 && (c.estimated_ns == 0 && c.measured_ns == 0)) any_unknown = true;
    critical_path = std::max(critical_path, eff);
    // Guard against overflow.
    if (total > (static_cast<std::uint64_t>(-1) - eff)) {
      total = static_cast<std::uint64_t>(-1);
      break;
    }
    total += eff;
  }

  if (aggregation_model == "SEQUENTIAL") {
    cost.total_ns = total;
  } else if (aggregation_model == "PARALLEL_CRITICAL_PATH") {
    cost.total_ns = critical_path;
  } else {
    // Unknown aggregation model: leave total unset (0), signalled as invalid.
    cost.total_ns = 0;
    return cost;
  }
  cost.parallel_best_ns = critical_path;
  if (any_measured) {
    cost.provenance = Provenance::MEASURED;
  } else if (!any_unknown) {
    cost.provenance = Provenance::ESTIMATED;
  }
  return cost;
}

CostComparison compare_recovery_costs(const std::vector<RecoveryCost>& alternatives) {
  CostComparison cmp;
  cmp.alternatives = alternatives;
  std::size_t best = 0;
  bool any = !alternatives.empty();
  for (std::size_t i = 1; i < alternatives.size(); ++i) {
    const RecoveryCost& a = alternatives[best];
    const RecoveryCost& b = alternatives[i];
    // Prefer lower total; tie-break by lower measured value, then by action enum.
    if (b.total_ns < a.total_ns) {
      best = i;
    } else if (b.total_ns == a.total_ns) {
      const RecoveryAction& ra = b.action;
      const RecoveryAction& rbest = a.action;
      if (static_cast<int>(ra) < static_cast<int>(rbest)) best = i;
    }
  }
  if (any) {
    cmp.selected_index = static_cast<int>(best);
    cmp.selection_reason = "lowest total cost alternative at index " + std::to_string(best);
  } else {
    cmp.selected_index = -1;
    cmp.selection_reason = "no alternatives supplied";
  }
  return cmp;
}

bool validate_dependency_generation_is_monotonic(
    const std::vector<std::uint64_t>& generations) {
  for (std::size_t i = 1; i < generations.size(); ++i) {
    if (generations[i] < generations[i - 1]) return false;
  }
  return true;
}

}  // namespace detail
}  // namespace engine_residency
