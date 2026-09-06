#include "engine_residency/runtime.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/serving_use.hpp"
#include <cstdio>
int main() {
  engine_residency::SteadyClock clock; engine_residency::EngineResidency rt(clock);
  engine_residency::EngineDefinition def; def.engine_id = engine_residency::EngineId(1);
  rt.define_engine(def);
  auto permit = rt.issue_registration_permit(engine_residency::WorkerId(1), engine_residency::WorkerBootId(1), engine_residency::SourceId(1), engine_residency::SourceBootId(1));
  engine_residency::EngineIncarnation inc; inc.engine_id = def.engine_id; inc.worker_id = engine_residency::WorkerId(1); inc.worker_boot = engine_residency::WorkerBootId(1);
  auto reg = rt.register_incarnation(inc, permit);
  rt.mark_running(reg.incarnation_id);
  // Acquire a serving-use token (will fail until readiness satisfied in a complete config).
  try {
    engine_residency::ServingUseToken tok; tok.incarnation_id = reg.incarnation_id; tok.incarnation_generation = reg.incarnation_generation;
    tok.engine_id = def.engine_id; tok.engine_generation = def.engine_generation;
    tok.readiness_generation = reg.readiness_generation;
    rt.acquire_serving_use(tok, "example");
    std::printf("example: serving use acquired (unexpected)\n");
  } catch (const engine_residency::DomainError& e) {
    std::printf("example: serving use correctly fenced: %s\n", e.what());
  }
  return 0;
}
