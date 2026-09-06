#include "engine_residency/readiness.hpp"
#include "engine_residency/components.hpp"

namespace engine_residency {
// A readiness snapshot is coherent if its incarnation and generations were all
// collected under one authoritative configuration.
bool readiness_snapshot_is_coherent(const ReadinessResult& r) {
  return r.coherent_snapshot;
}
}  // namespace engine_residency
