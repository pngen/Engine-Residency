#ifndef ENGINE_RESIDENCY_DRAIN_HPP
#define ENGINE_RESIDENCY_DRAIN_HPP

#include <cstdint>
#include <string>

#include "engine_residency/enums.hpp"
#include "engine_residency/identities.hpp"

namespace engine_residency {

// Drain progress for an incarnation. Drain is a progress dimension with an
// explicit ordered lifecycle, distinct from ProcessLifecycle.
struct DrainState {
  DrainPhase phase{DrainPhase::NONE};
  DrainGeneration generation;      // advanced on each drain request.
  std::uint32_t active_use{0};     // currently admitted (not yet released) uses.
  bool admission_fenced{false};    // no new serving-use acquisition possible.
  bool backend_cleanup_ack{false};// backend acknowledged cleanup.
  std::string detail;

  [[nodiscard]] bool is_drained() const noexcept {
    return phase == DrainPhase::DRAINED || phase == DrainPhase::RETIRED;
  }
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_DRAIN_HPP
