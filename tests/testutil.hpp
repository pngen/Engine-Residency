#ifndef ENGINE_RESIDENCY_TESTUTIL_HPP
#define ENGINE_RESIDENCY_TESTUTIL_HPP

#include "engine_residency/runtime.hpp"

#include <cstdio>
#include <cstdlib>

namespace er_test {
inline int g_failures = 0;

#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s (line %d)\n", (msg), __LINE__); ++er_test::g_failures; } } while(0)
#define CHECK_EQ(a, b, msg) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { std::printf("FAIL: %s (line %d)\n", (msg), __LINE__); ++er_test::g_failures; } } while(0)

inline int return_code() { return g_failures == 0 ? 0 : 1; }
inline void CheckFail(const char* msg, std::uint64_t seed) {
  std::printf("FAIL: %s (seed=%llu)\n", msg, (unsigned long long)seed); ++g_failures;
}

inline engine_residency::EngineDefinition seed_engine(engine_residency::EngineResidency& rt, engine_residency::ReadinessProfileId& prof_id, engine_residency::ReadinessProfile& prof_out) {
  using namespace engine_residency;
  EngineDefinition def;
  def.engine_id = EngineId(1);
  def.engine_generation = EngineGeneration(1);
  def.config_generation = EngineConfigGeneration(1);
  def.name = "reference-engine";
  def.role = ServingRole::GENERAL_INFERENCE;
  def.backend_id = BackendId(1);
  def.backend_generation = BackendGeneration(1);
  def.model_id = ModelId(1);
  def.model_generation = ModelGeneration(1);
  def.model_name = "reference-model";
  def.model_bytes = 1u << 20;
  def.workspace_bytes = 1u << 16;
  def.kv_capacity_entries = 1024;
  def.kernel_key = "er-reference-kernel";
  def.kernel_generation = KernelGeneration(1);
  def.kernel_required = true;
  def.graph_required = true;
  def.graph_optional_fallback = false;
  def.graph_generation = GraphGeneration(1);
  def.compatibility = CompatibilityKey::with_namespace("ref/v1");
  def.standby_eligible = true;
  def.standby_target = 1;
  def.max_concurrency = 8;
  def.warmup_required = true;
  EngineDefinition stored = rt.define_engine(def);

  ReadinessProfile prof;
  prof.profile_id = ReadinessProfileId(1);
  prof.profile_generation = ReadinessProfileGeneration(1);
  prof.engine_id = def.engine_id;
  prof.engine_generation = def.engine_generation;
  prof.name = "reference-bs1";
  prof.max_batch = 1;
  prof.max_seq_len = 4096;
  prof.dtype = "fp32";
  prof.layout = "row-major";
  prof.workspace_bytes = 1u << 16;
  prof.model_bytes = 1u << 20;
  prof.kv_capacity_entries = 1024;
  prof.graph_required = true;
  prof.graph_optional_fallback = false;
  prof.warmup_required = true;
  prof.compatibility = CompatibilityKey::with_namespace("ref/v1");
  ProfileRequirement backend; backend.category = ComponentCategory::BACKEND; backend.kind = RequirementKind::REQUIRED; backend.min_state = ComponentState::VERIFIED;
  ProfileRequirement dctx; dctx.category = ComponentCategory::DEVICE_CONTEXT; dctx.kind = RequirementKind::REQUIRED; dctx.min_state = ComponentState::BOUND;
  ProfileRequirement model; model.category = ComponentCategory::MODEL; model.kind = RequirementKind::REQUIRED; model.min_state = ComponentState::VERIFIED; model.subject = "reference-model"; model.generation_family = GenerationFamily::MODEL; model.compatibility = prof.compatibility;
  ProfileRequirement kv; kv.category = ComponentCategory::KV_CAPACITY; kv.kind = RequirementKind::REQUIRED; kv.min_state = ComponentState::BOUND; kv.subject = "kv-main"; kv.generation_family = GenerationFamily::KV_CAPACITY;
  ProfileRequirement kernel; kernel.category = ComponentCategory::KERNEL; kernel.kind = RequirementKind::REQUIRED; kernel.min_state = ComponentState::VERIFIED; kernel.subject = "er-reference-kernel"; kernel.generation_family = GenerationFamily::KERNEL; kernel.compatibility = prof.compatibility;
  ProfileRequirement graph; graph.category = ComponentCategory::GRAPH; graph.kind = RequirementKind::REQUIRED; graph.min_state = ComponentState::VERIFIED; graph.subject = "er-reference-graph"; graph.generation_family = GenerationFamily::GRAPH;
  ProfileRequirement warmup; warmup.category = ComponentCategory::WARMUP; warmup.kind = RequirementKind::REQUIRED; warmup.min_state = ComponentState::VERIFIED; warmup.subject = "warmup";
  prof.requirements = {backend, dctx, model, kv, kernel, graph, warmup};
  rt.define_profile(prof);
  prof_id = prof.profile_id;
  prof_out = prof;
  return stored;
}

inline engine_residency::EngineIncarnationId register_worker(engine_residency::EngineResidency& rt, std::uint64_t worker_id, std::uint64_t boot) {
  using namespace engine_residency;
  WorkerId w(worker_id);
  WorkerBootId b(boot);
  RegistrationPermit permit = rt.issue_registration_permit(w, b, SourceId(1), SourceBootId(boot));
  EngineIncarnation inc;
  inc.engine_id = EngineId(1);
  inc.engine_generation = EngineGeneration(1);
  inc.config_generation = EngineConfigGeneration(1);
  inc.worker_id = w; inc.worker_boot = b; inc.source_id = SourceId(1); inc.source_boot = SourceBootId(boot);
  inc.process.pid = (std::uint32_t)(1000 + worker_id);
  inc.process.label = "worker";
  inc.backend_id = BackendId(1); inc.backend_generation = BackendGeneration(1);
  EngineIncarnation reg = rt.register_incarnation(inc, permit);
  return reg.incarnation_id;
}

inline void publish_reference_evidence(engine_residency::EngineResidency& rt, engine_residency::EngineIncarnationId inc,
                                       engine_residency::CoordinatorEpoch epoch, bool warmup = true) {
  using namespace engine_residency;
  auto pub = [&](ComponentCategory cat, ComponentState st, const std::string& subj, const std::string& detail) {
    ComponentEvidence ev;
    ev.incarnation_id = inc; ev.engine_id = EngineId(1); ev.engine_generation = EngineGeneration(1);
    ev.category = cat; ev.state = st; ev.subject = subj; ev.namespace_ = "er";
    ev.provenance = Provenance::MEASURED; ev.detail = detail;
    ev.model_generation = ModelGeneration(1); ev.kv_capacity_generation = KvCapacityGeneration(1);
    ev.kernel_generation = KernelGeneration(1); ev.graph_generation = GraphGeneration(1);
    ev.compatibility = CompatibilityKey::with_namespace("ref/v1");
    ev.coordinator_epoch = epoch;
    rt.publish_component(ev);
  };
  pub(ComponentCategory::BACKEND, ComponentState::VERIFIED, "backend-reference", "verified");
  pub(ComponentCategory::DEVICE_CONTEXT, ComponentState::BOUND, "device-context-0", "bound");
  pub(ComponentCategory::MODEL, ComponentState::VERIFIED, "reference-model", "verified");
  pub(ComponentCategory::KV_CAPACITY, ComponentState::BOUND, "kv-main", "provisioned");
  pub(ComponentCategory::KERNEL, ComponentState::VERIFIED, "er-reference-kernel", "verified");
  pub(ComponentCategory::GRAPH, ComponentState::VERIFIED, "er-reference-graph", "replayed");
  if (warmup) pub(ComponentCategory::WARMUP, ComponentState::VERIFIED, "warmup", "verified");
}
}  // namespace er_test


#endif  // ENGINE_RESIDENCY_TESTUTIL_HPP
