#include "testutil.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/runtime.hpp"

#include <cstdint>
#include <functional>

using namespace engine_residency;

int main() {
  EngineResidency rt(*(new SteadyClock()));
  ReadinessProfileId prof_id; ReadinessProfile prof;
  er_test::seed_engine(rt, prof_id, prof);

  std::uint64_t seed = 4242;
  // One authoritative incarnation; loop acquire/release and verify exact accounting.
  EngineIncarnationId inc = er_test::register_worker(rt, 1, 1);
  er_test::publish_reference_evidence(rt, inc, rt.current_epoch());
  ReadinessResult r0 = rt.evaluate_readiness(inc, prof_id);
  if (r0.outcome != ReadinessOutcome::READY) er_test::CheckFail("property: READY invariant", seed);
  EngineIncarnation ii; for (const auto& x : rt.incarnations()) if (x.incarnation_id == inc) ii = x;

  int acquired = 0;
  for (int i = 0; i < 1000; ++i) {
    ServingUseToken tok; tok.incarnation_id = inc; tok.engine_id = EngineId(1); tok.engine_generation = EngineGeneration(1);
    tok.profile_id = prof_id; tok.readiness_generation = ii.readiness_generation; tok.activation_generation = ActivationGeneration(1);
    tok.execution_id = ExecutionId((std::uint64_t)i); tok.workload_id = WorkloadId((std::uint64_t)i);
    ServingUseToken a = rt.acquire_serving_use(tok, "prop");
    ++acquired;
    if (rt.active_use_count() < 1) { er_test::CheckFail("property: use tracked", seed + i); break; }
    rt.release_serving_use(a, WorkOutcome::COMPLETED, "done");
    if (rt.active_use_count() != 0) { er_test::CheckFail("property: no underflow/leak", seed + i); break; }
  }
  if (acquired != 1000) er_test::CheckFail("property: acquire loop completed", seed);

  // Generation monotonicity: config update cannot regress.
  EngineResidency rt2(*(new SteadyClock()));
  ReadinessProfileId p; ReadinessProfile pf;
  er_test::seed_engine(rt2, p, pf);
  er_test::register_worker(rt2, 1, 1);
  bool threw = false;
  try { rt2.update_engine_config(EngineId(1), EngineConfigGeneration(0), "regress"); } catch (const DomainError&) { threw = true; }
  if (!threw) er_test::CheckFail("property: config generation regression rejected", 1);

  // Standby never double-counts: two warm incarnations <= 2 eligible under one policy.
  EngineResidency rt3(*(new SteadyClock()));
  ReadinessProfileId p3; ReadinessProfile pf3;
  er_test::seed_engine(rt3, p3, pf3);
  EngineIncarnationId a = er_test::register_worker(rt3, 1, 1);
  er_test::publish_reference_evidence(rt3, a, rt3.current_epoch());
  EngineIncarnationId b = er_test::register_worker(rt3, 2, 2);
  er_test::publish_reference_evidence(rt3, b, rt3.current_epoch());
  StandbyPoolPolicy pol; pol.engine_id = EngineId(1); pol.target_standby = 4;
  StandbyAccounting acc = rt3.reconcile_standby(pol);
  if (acc.eligible > 2) er_test::CheckFail("property: standby no double-count", seed);

  std::printf("property tests failures=%d\n", er_test::g_failures);
  return er_test::return_code();
}
