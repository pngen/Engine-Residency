#include "engine_residency/authority.hpp"

namespace engine_residency {
// Monotonic, durable epoch advancement. Generation exhaustion throws.
CoordinatorEpoch advance_epoch_monotonic(CoordinatorEpoch current) {
  return current.next();
}
}  // namespace engine_residency
