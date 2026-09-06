#include "engine_residency/runtime.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/components.hpp"
#include <cstdio>
int main() {
  engine_residency::SteadyClock clock;
  engine_residency::EngineResidency rt(clock);
  engine_residency::ReadinessProfile prof; engine_residency::ReadinessProfileId prof_id;
  engine_residency::EngineDefinition def;
  def.engine_id = engine_residency::EngineId(1); def.engine_generation = engine_residency::EngineGeneration(1);
  def.config_generation = engine_residency::EngineConfigGeneration(1); def.name = "example-engine";
  def.model_id = engine_residency::ModelId(1); def.model_generation = engine_residency::ModelGeneration(1);
  def.model_bytes = 1u<<20; def.workspace_bytes = 1u<<16; def.kv_capacity_entries = 1024;
  def.kernel_key = "k"; def.kernel_generation = engine_residency::KernelGeneration(1);
  def.graph_required = true; def.graph_generation = engine_residency::GraphGeneration(1);
  def.compatibility = engine_residency::CompatibilityKey::with_namespace("ref/v1");
  rt.define_engine(def);
  engine_residency::ReadinessProfile prof; prof.profile_id = engine_residency::ReadinessProfileId(1);
  prof.profile_generation = engine_residency::ReadinessProfileGeneration(1); prof.engine_id = def.engine_id;
  prof.name = "example-profile"; prof.graph_required = true; prof.warmup_required = true;
  prof.compatibility = def.compatibility;
  engine_residency::ProfileRequirement req; req.category = engine_residency::ComponentCategory::MODEL; req.min_state = engine_residency::ComponentState::VERIFIED; req.subject = "m";
  prof.requirements.push_back(req);
  rt.define_profile(prof);
  std::printf("example: profile defined\n");
  return 0;
}
