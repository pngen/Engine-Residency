#include "engine_residency/drain.hpp"

namespace engine_residency {
bool drain_admission_fenced(const DrainState& d) {
  return d.admission_fenced;
}
}  // namespace engine_residency
