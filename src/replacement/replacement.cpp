#include "engine_residency/replacement.hpp"

namespace engine_residency {
bool replacement_is_complete(const ReplacementPlan& p) {
  return p.phase == ReplacementPlan::Phase::COMPLETE;
}
}  // namespace engine_residency
