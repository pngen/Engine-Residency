#include "engine_residency/preparation.hpp"

namespace engine_residency {
std::size_t preparation_remaining_steps(const PreparationAttempt& a, const PreparationPlan& p) {
  return a.next_step_index < p.steps.size() ? (p.steps.size() - a.next_step_index) : 0;
}
}  // namespace engine_residency
