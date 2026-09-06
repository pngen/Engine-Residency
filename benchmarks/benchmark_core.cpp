#include "engine_residency/runtime.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/components.hpp"
#include "engine_residency/serving_use.hpp"
#include <chrono>
#include <cstdio>
#include <cstdint>
using namespace engine_residency;
int main() {
  SteadyClock clock; EngineResidency rt(clock);
  EngineDefinition def; def.engine_id = EngineId(1); def.engine_generation = EngineGeneration(1);
  def.config_generation = EngineConfigGeneration(1); def.model_id = ModelId(1); def.model_generation = ModelGeneration(1);
  def.graph_required = true; def.compatibility = CompatibilityKey::with_namespace("ref/v1");
  rt.define_engine(def);
  auto permit = rt.issue_registration_permit(WorkerId(1), WorkerBootId(1), SourceId(1), SourceBootId(1));
  EngineIncarnation inc; inc.engine_id = def.engine_id; inc.worker_id = WorkerId(1); inc.worker_boot = WorkerBootId(1);
  auto reg = rt.register_incarnation(inc, permit); rt.mark_running(reg.incarnation_id);
  const int N = 20000;
  auto t0 = std::chrono::steady_clock::now();
  for (int i=0;i<N;++i) {
    ComponentEvidence ev; ev.incarnation_id = reg.incarnation_id; ev.engine_id = EngineId(1); ev.engine_generation = EngineGeneration(1);
    ev.category = ComponentCategory::BACKEND; ev.state = ComponentState::VERIFIED; ev.subject = "b";
    ev.provenance = Provenance::MEASURED; ev.coordinator_epoch = rt.current_epoch();
    rt.publish_component(ev);
  }
  auto t1 = std::chrono::steady_clock::now();
  double pub_s = (double)N / std::chrono::duration<double>(t1-t0).count();
  std::printf("bench: component publish %.0f ops/s\n", pub_s);
  return 0;
}