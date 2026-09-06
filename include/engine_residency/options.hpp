#ifndef ENGINE_RESIDENCY_OPTIONS_HPP
#define ENGINE_RESIDENCY_OPTIONS_HPP

#include <cstdint>

namespace engine_residency {

// Explicit resource bounds. When a bound is reached, admission fails with a
// typed BoundsExceeded error; nothing is silently dropped.
struct RuntimeOptions {
  std::uint32_t max_engines{1'000};
  std::uint32_t max_profiles_per_engine{64};
  std::uint32_t max_incarnations{8'000};
  std::uint32_t max_component_evidence{200'000};
  std::uint32_t max_pending_preparation_attempts{4'000};
  std::uint32_t max_preparation_plan_steps{128};
  std::uint32_t max_active_uses{1'000'000};
  std::uint32_t max_standby_slots{4'096};
  std::uint32_t max_replacement_candidates{64};
  std::uint32_t max_history{100'000};
  std::uint32_t max_cost_samples{50'000};
  std::uint32_t max_explanation_bytes{8'192};
  std::uint32_t max_registration_permits{16'000};
  std::uint32_t max_activation_records{32'000};
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_OPTIONS_HPP
