#include "engine_residency/runtime.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/components.hpp"
#include <cstdio>
int main() {
  engine_residency::SteadyClock clock;
  engine_residency::EngineResidency rt(clock);
  engine_residency::EngineDefinition def;
  def.engine_id = engine_residency::EngineId(1);
  def.engine_generation = engine_residency::EngineGeneration(1);
  def.config_generation = engine_residency::EngineConfigGeneration(1);
  def.model_id = engine_residency::ModelId(1);
  def.model_generation = engine_residency::ModelGeneration(1);
  def.graph_required = true;
  def.compatibility = engine_residency::CompatibilityKey::with_namespace("ref/v1");
  rt.define_engine(def);
  auto permit = rt.issue_registration_permit(engine_residency::WorkerId(1), engine_residency::WorkerBootId(1), engine_residency::SourceId(1), engine_residency::SourceBootId(1));
  engine_residency::EngineIncarnation inc; inc.engine_id = def.engine_id; inc.worker_id = engine_residency::WorkerId(1); inc.worker_boot = engine_residency::WorkerBootId(1);
  auto reg = rt.register_incarnation(inc, permit);
  std::printf("consumer: registered incarnation=%llu\n", (unsigned long long)reg.incarnation_id.raw());
  rt.invalidate_incarnation_components(reg.incarnation_id, "consumer invalidation");
  std::printf("consumer: invalidate applied, active uses=%d\n", rt.active_use_count());
  return 0;
}