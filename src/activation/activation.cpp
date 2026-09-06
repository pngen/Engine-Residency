#include "engine_residency/activation.hpp"

namespace engine_residency {
bool activation_is_serving(const ActivationRecord& r) {
  return r.state == ActivationState::ACTIVE && r.current;
}
}  // namespace engine_residency
