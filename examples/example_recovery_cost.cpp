#include "engine_residency/runtime.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/economics.hpp"
#include <cstdio>
int main() {
  engine_residency::SteadyClock clock; engine_residency::EngineResidency rt(clock);
  std::vector<engine_residency::CostComponent> comp;
  engine_residency::CostComponent c; c.name = "warmup"; c.estimated_ns = 3000000; c.provenance = engine_residency::Provenance::ESTIMATED; c.unit = "ns";
  comp.push_back(c);
  auto cost = rt.aggregate_recovery_cost(comp, engine_residency::RecoveryAction::WARM_COLD, "SEQUENTIAL");
  std::printf("example: recovery cost total=%llu ns provenance=%s\n", (unsigned long long)cost.total_ns, engine_residency::to_string(cost.provenance));
  return 0;
}
