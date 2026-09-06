#include "engine_residency/engine.hpp"
#include "engine_residency/profiles.hpp"
#include <string>

namespace engine_residency {
// Validates that an engine definition is internally consistent.
bool validate_engine_definition(const EngineDefinition& d) {
  if (!d.engine_id.is_valid()) return false;
  if (d.standby_target > 0 && !d.standby_eligible) return false;
  if (d.graph_required && d.graph_optional_fallback) return false;  // contradictory policy
  return true;
}
// Validates that a readiness profile binds a supported engine.
bool validate_profile_definition(const ReadinessProfile& p, const EngineDefinition& d) {
  return p.engine_id == d.engine_id;
}
}  // namespace engine_residency
