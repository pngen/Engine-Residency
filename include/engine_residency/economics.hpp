#ifndef ENGINE_RESIDENCY_ECONOMICS_HPP
#define ENGINE_RESIDENCY_ECONOMICS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "engine_residency/enums.hpp"
#include "engine_residency/units.hpp"

namespace engine_residency {

// A named cost component. Exact units and provenance are required. Unknown costs
// stay unknown -- we never fabricate dollar, energy, or SLO values.
struct CostComponent {
  std::string name;                       // e.g. "process_startup".
  DurationNs estimated_ns{0};
  DurationNs measured_ns{0};
  Provenance provenance{Provenance::UNKNOWN};
  Bytes bytes{0};
  std::string unit;                       // "ns", "bytes"; explicit.

  [[nodiscard]] DurationNs effective_ns() const noexcept {
    if (measured_ns != 0) return measured_ns;
    return estimated_ns;
  }
};

// A recovery/preparation cost for one named alternative.
struct RecoveryCost {
  RecoveryAction action{RecoveryAction::REPLACE_LOST};
  std::vector<CostComponent> components;
  DurationNs total_ns{0};         // aggregated over the critical path.
  DurationNs parallel_best_ns{0}; // longest single stage if stages run in parallel.
  std::string aggregation_model;  // "SEQUENTIAL" or "PARALLEL_CRITICAL_PATH".
  Provenance provenance{Provenance::ESTIMATED};

  [[nodiscard]] bool is_valid() const noexcept { return aggregation_model == "SEQUENTIAL" || aggregation_model == "PARALLEL_CRITICAL_PATH"; }
};

// Deterministic comparison of supplied alternatives.
struct CostComparison {
  std::vector<RecoveryCost> alternatives;
  int selected_index{-1};         // index into alternatives, -1 if none.
  std::string selection_reason;   // deterministic reason.
  std::string detail;
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_ECONOMICS_HPP
