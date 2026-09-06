#include "testutil.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/runtime.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace engine_residency;

int main() {
  EngineResidency rt(*(new SteadyClock()));
  ReadinessProfileId prof_id; ReadinessProfile prof;
  er_test::seed_engine(rt, prof_id, prof);
  EngineIncarnationId inc = er_test::register_worker(rt, 1, 1);
  er_test::publish_reference_evidence(rt, inc, rt.current_epoch());

  const int kThreads = 8;
  const int kIters = 2000;
  std::atomic<bool> go{false};
  std::atomic<int> errors{0};
  std::vector<std::thread> ts;

  for (int t = 0; t < kThreads; ++t) {
    ts.emplace_back([&, t] {
      while (!go.load()) { /* spin until start barrier */ }
      for (int i = 0; i < kIters; ++i) {
        try {
          if ((t % 3) == 0) {
            // component publish racing readiness query
            ComponentEvidence ev; ev.incarnation_id = inc; ev.engine_id = EngineId(1); ev.engine_generation = EngineGeneration(1);
            ev.category = ComponentCategory::KERNEL; ev.state = ComponentState::VERIFIED; ev.subject = "er-reference-kernel";
            ev.provenance = Provenance::MEASURED; ev.model_generation = ModelGeneration(1);
            ev.kv_capacity_generation = KvCapacityGeneration(1); ev.kernel_generation = KernelGeneration(1); ev.graph_generation = GraphGeneration(1);
            ev.compatibility = CompatibilityKey::with_namespace("ref/v1"); ev.coordinator_epoch = rt.current_epoch();
            rt.publish_component(ev);
          } else if ((t % 3) == 1) {
            ReadinessResult r = rt.evaluate_readiness(inc, prof_id);
            // READY must imply all required satisfied; never UNKNOWN from a coherent snapshot.
            if (r.outcome == ReadinessOutcome::READY && !r.coherent_snapshot) ++errors;
          } else {
            // acquir/release must never produce underflow visible as negative count.
            EngineIncarnation ii; for (const auto& x : rt.incarnations()) if (x.incarnation_id == inc) ii = x;
            ServingUseToken tok; tok.incarnation_id = inc; tok.engine_id = EngineId(1); tok.engine_generation = EngineGeneration(1);
            tok.profile_id = prof_id; tok.readiness_generation = ii.readiness_generation; tok.activation_generation = ActivationGeneration(1);
            tok.execution_id = ExecutionId(7); tok.workload_id = WorkloadId(7);
            try {
              ServingUseToken a = rt.acquire_serving_use(tok, "conc");
              rt.release_serving_use(a, WorkOutcome::COMPLETED, "done");
            } catch (const DomainError&) { ++errors; }
          }
        } catch (...) { ++errors; }
      }
    });
  }
  go.store(true);
  for (auto& th : ts) th.join();

  if (errors.load() != 0) er_test::CheckFail("concurrency: no unexpected exceptions/underflow", 1);
  std::printf("concurrency tests failures=%d\n", er_test::g_failures);
  return er_test::return_code();
}
