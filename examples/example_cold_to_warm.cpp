#include "engine_residency/runtime.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/components.hpp"
#include <cstdio>
int main() {
  engine_residency::SteadyClock clock;
  engine_residency::EngineResidency rt(clock);
  engine_residency::EngineDefinition def;
  def.engine_id = engine_residency::EngineId(1); def.engine_generation = engine_residency::EngineGeneration(1);
  def.config_generation = engine_residency::EngineConfigGeneration(1);
  def.name = "example-engine"; def.role = engine_residency::ServingRole::GENERAL_INFERENCE;
  def.model_id = engine_residency::ModelId(1); def.model_generation = engine_residency::ModelGeneration(1);
  def.model_bytes = 1u<<20; def.workspace_bytes = 1u<<16; def.kv_capacity_entries = 1024;
  def.kernel_key = "k"; def.kernel_generation = engine_residency::KernelGeneration(1);
  def.graph_required = true; def.graph_generation = engine_residency::GraphGeneration(1);
  def.compatibility = engine_residency::CompatibilityKey::with_namespace("ref/v1");
  rt.define_engine(def);
  // A worker registers and is cold (no evidence) -> BLOCKED.
  engine_residency::WorkerId w(1); engine_residency::WorkerBootId b(1);
  auto permit = rt.issue_registration_permit(w, b, engine_residency::SourceId(1), engine_residency::SourceBootId(1));
  engine_residency::EngineIncarnation inc; inc.engine_id = def.engine_id;
  inc.engine_generation = def.engine_generation; inc.config_generation = def.config_generation;
  inc.worker_id = w; inc.worker_boot = b;
  auto reg = rt.register_incarnation(inc, permit);
  std::printf("example: cold engine registered incarnation=%llu\n", (unsigned long long)reg.incarnation_id.raw());
  return 0;
}
