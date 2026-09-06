#include "engine_residency/engine.hpp"
#include <string>

namespace engine_residency {
// A small diagnostic describing the current incarnation authority of an engine.
std::string describe_incarnation_authority(const EngineIncarnation& i) {
  std::string s = "incarnation=" + std::to_string(i.incarnation_id.raw());
  if (i.is_current) s += " CURRENT";
  s += " worker=" + std::to_string(i.worker_id.raw()) + " boot=" + std::to_string(i.worker_boot.raw());
  s += " epoch=" + std::to_string(i.coordinator_epoch.raw());
  return s;
}
}  // namespace engine_residency
