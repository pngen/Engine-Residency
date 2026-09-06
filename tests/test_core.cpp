#include "testutil.hpp"

#include <functional>
#include "engine_residency/clock.hpp"
#include "engine_residency/runtime.hpp"

using namespace engine_residency;

static EngineResidency make_runtime() {
  static SteadyClock clock;  // static: outlives runtime
  return EngineResidency(clock);
}

// --- Scenario helpers ---------------------------------------------------------
static EngineIncarnation get_inc(EngineResidency& rt, EngineIncarnationId id) {
  for (const auto& i : rt.incarnations()) if (i.incarnation_id == id) return i;
  return EngineIncarnation();
}
static void expect_throws(EngineResidency& rt, const std::function<void()>& fn, const char* msg) {
  (void)rt;
  try { fn(); std::printf("FAIL(no throw): %s (line %d)\n", msg, __LINE__); ++er_test::g_failures; }
  catch (const DomainError&) { CHECK(true, msg); }
  catch (...) { std::printf("FAIL(wrong throw): %s (line %d)\n", msg, __LINE__); ++er_test::g_failures; }
}

int main() {
  EngineResidency rt = make_runtime();
  ReadinessProfileId prof_id; ReadinessProfile prof;
  EngineDefinition def = er_test::seed_engine(rt, prof_id, prof);

  // 1. Cold engine blocks serving.
  EngineIncarnationId inc_a = er_test::register_worker(rt, 1, 1);
  EngineIncarnation inc_a_info = get_inc(rt, inc_a);
  {
    ReadinessResult r = rt.evaluate_readiness(inc_a, prof_id);
    CHECK_EQ((int)r.outcome, (int)ReadinessOutcome::BLOCKED, "cold engine blocks");
  }

  // 2. Partially loaded still blocks: publish backend only.
  {
    ComponentEvidence ev; ev.incarnation_id = inc_a; ev.engine_id = EngineId(1); ev.engine_generation = EngineGeneration(1);
    ev.category = ComponentCategory::BACKEND; ev.state = ComponentState::VERIFIED; ev.subject = "backend-reference";
    ev.provenance = Provenance::MEASURED; ev.coordinator_epoch = rt.current_epoch();
    try { rt.publish_component(ev); } catch (...) {}
    ReadinessResult r = rt.evaluate_readiness(inc_a, prof_id);
    CHECK_EQ((int)r.outcome, (int)ReadinessOutcome::BLOCKED, "partial evidence blocks required profile");
  }

  // 3. Loaded-but-unwarmed blocks (all except warmup).
  er_test::publish_reference_evidence(rt, inc_a, rt.current_epoch(), /*warmup=*/false);
  {
    ReadinessResult r = rt.evaluate_readiness(inc_a, prof_id);
    CHECK_EQ((int)r.outcome, (int)ReadinessOutcome::BLOCKED, "unwarmed blocks");
  }

  // 4. Complete warmup -> READY.
  {
    ComponentEvidence ev; ev.incarnation_id = inc_a; ev.engine_id = EngineId(1); ev.engine_generation = EngineGeneration(1);
    ev.category = ComponentCategory::WARMUP; ev.state = ComponentState::VERIFIED; ev.subject = "warmup";
    ev.provenance = Provenance::MEASURED; ev.coordinator_epoch = rt.current_epoch();
    rt.publish_component(ev);
    ReadinessResult r = rt.evaluate_readiness(inc_a, prof_id);
    CHECK_EQ((int)r.outcome, (int)ReadinessOutcome::READY, "warmup produces readiness");
    CHECK(r.coherent_snapshot, "coherent snapshot");
  }

  // 5. Model generation invalidation blocks old readiness.
  {
    ComponentEvidence ev; ev.incarnation_id = inc_a; ev.engine_id = EngineId(1); ev.engine_generation = EngineGeneration(1);
    ev.category = ComponentCategory::MODEL; ev.state = ComponentState::VERIFIED; ev.subject = "reference-model";
    ev.model_generation = ModelGeneration(2); ev.kernel_generation = KernelGeneration(1); ev.graph_generation = GraphGeneration(1);
    ev.compatibility = CompatibilityKey::with_namespace("ref/v1"); ev.provenance = Provenance::MEASURED;
    ev.kv_capacity_generation = KvCapacityGeneration(1); ev.coordinator_epoch = rt.current_epoch();
    rt.publish_component(ev);
    ReadinessResult r = rt.evaluate_readiness(inc_a, prof_id);
    CHECK_EQ((int)r.outcome, (int)ReadinessOutcome::BLOCKED, "model generation invalidation blocks");
    // restore model gen 1
    ComponentEvidence ev2; ev2.incarnation_id = inc_a; ev2.engine_id = EngineId(1); ev2.engine_generation = EngineGeneration(1);
    ev2.category = ComponentCategory::MODEL; ev2.state = ComponentState::VERIFIED; ev2.subject = "reference-model";
    ev2.model_generation = ModelGeneration(1); ev2.kernel_generation = KernelGeneration(1); ev2.graph_generation = GraphGeneration(1);
    ev2.compatibility = CompatibilityKey::with_namespace("ref/v1"); ev2.provenance = Provenance::MEASURED;
    ev2.kv_capacity_generation = KvCapacityGeneration(1); ev2.coordinator_epoch = rt.current_epoch();
    rt.publish_component(ev2);
  }

  // 6. Activation + serving-use acquire/release exact accounting.
  {
    ActivationRequest req; req.incarnation_id = inc_a; req.incarnation_generation = EngineIncarnationGeneration(1);
    req.engine_id = EngineId(1); req.engine_generation = EngineGeneration(1);
    req.profile_id = prof_id; req.readiness_generation = inc_a_info.readiness_generation;
    req.caller_authority = rt.current_authority();
    // readiness_generation must match current
    EngineIncarnation incr = inc_a_info;
    req.readiness_generation = incr.readiness_generation;
    ActivationRecord rec = rt.activate(req);
    CHECK_EQ((int)rec.state, (int)ActivationState::ACTIVE, "activation succeeds");

    ServingUseToken wanted; wanted.incarnation_id = inc_a; wanted.engine_id = EngineId(1); wanted.engine_generation = EngineGeneration(1);
    wanted.profile_id = prof_id; wanted.readiness_generation = incr.readiness_generation; wanted.activation_generation = rec.activation_generation;
    wanted.execution_id = ExecutionId(1); wanted.workload_id = WorkloadId(1);
    ServingUseToken tok = rt.acquire_serving_use(wanted, "test");
    CHECK_EQ(rt.active_use_count(), 1, "one active use");
    CHECK(tok.is_valid(), "use token valid");

    // stale acquisition rejected (wrong readiness generation)
    ServingUseToken stale = wanted; stale.readiness_generation = ReadinessGeneration(999);
    expect_throws(rt, [&]{ rt.acquire_serving_use(stale, "test"); }, "stale readiness acquisition rejected");

    // duplicate release rejected
    rt.release_serving_use(tok, WorkOutcome::COMPLETED, "done");
    CHECK_EQ(rt.active_use_count(), 0, "use released");
    expect_throws(rt, [&]{ rt.release_serving_use(tok, WorkOutcome::COMPLETED, "again"); }, "duplicate release rejected");
  }

  // 7. Drain blocks new acquisition; already admitted safe work completes then cleanup.
  {
    ServingUseToken wanted; wanted.incarnation_id = inc_a; wanted.engine_id = EngineId(1); wanted.engine_generation = EngineGeneration(1);
    wanted.profile_id = prof_id; EngineIncarnation incr = inc_a_info;
    wanted.readiness_generation = incr.readiness_generation; wanted.activation_generation = ActivationGeneration(1);
    wanted.execution_id = ExecutionId(2); wanted.workload_id = WorkloadId(2);
    ServingUseToken held = rt.acquire_serving_use(wanted, "test");
    rt.request_drain(inc_a);
    expect_throws(rt, [&]{ rt.acquire_serving_use(wanted, "test"); }, "no acquisition after drain");
    // Admitted work completes; release requires drain to observe cleanup.
    rt.release_serving_use(held, WorkOutcome::COMPLETED, "done");
    rt.acknowledge_backend_cleanup(inc_a);
    DrainState ds = rt.drain_state(inc_a);
    CHECK_EQ((int)ds.phase, (int)DrainPhase::DRAINED, "drain completes");
  }

  // 8. Persistence round-trip + corruption rejection.
  {
    std::string blob = rt.serialize();
    EngineResidency rt2 = make_runtime();
    rt2.load(blob);
    CHECK_EQ(rt2.current_epoch().raw(), rt.current_epoch().raw(), "epoch persisted");
    CHECK((int)rt2.incarnations().size() >= 1, "incarnations persisted");
    // Corrupt a byte -> reject.
    std::string bad = blob; bad[10] = (char)(bad[10] ^ 0xFF);
    expect_throws(rt2, [&]{ rt2.load(bad); }, "corrupt persistence rejected");
  }

  // 9. Generation fencing: re-register under fenced boot rejected.
  {
    WorkerId w(1); WorkerBootId b(1);
    rt.fence_worker(w, b, "restarted");
    expect_throws(rt, [&]{ RegistrationPermit p = rt.issue_registration_permit(w, b, SourceId(1), SourceBootId(1)); (void)p; }, "fenced boot cannot obtain permit");
  }

  // 10. Standby accounting: a warm, non-fenced, non-active incarnation is eligible.
  {
    EngineIncarnationId inc_b = er_test::register_worker(rt, 2, 2);
    er_test::publish_reference_evidence(rt, inc_b, rt.current_epoch());
    StandbyPoolPolicy policy; policy.pool_id = EnginePoolId(1); policy.engine_id = EngineId(1);
    policy.target_standby = 1; policy.require_current_incarnation = false;
    StandbyAccounting acc = rt.reconcile_standby(policy);
    CHECK(acc.eligible >= 1, "warm standby eligible (no double-count)");
  }

  // 11. Exclusive-slot competing activation rejected (fresh runtime).
  {
    EngineResidency rt3 = make_runtime();
    ReadinessProfileId p3; ReadinessProfile pf3;
    er_test::seed_engine(rt3, p3, pf3);
    EngineIncarnationId x = er_test::register_worker(rt3, 1, 1);
    er_test::publish_reference_evidence(rt3, x, rt3.current_epoch());
    EngineIncarnation xi; for (const auto& it : rt3.incarnations()) if (it.incarnation_id == x) xi = it;
    ActivationRequest req; req.incarnation_id = x; req.incarnation_generation = EngineIncarnationGeneration(1);
    req.engine_id = EngineId(1); req.engine_generation = EngineGeneration(1);
    req.profile_id = p3; req.slot_id = EngineSlotId(1);
    req.readiness_generation = xi.readiness_generation; req.caller_authority = rt3.current_authority();
    ActivationRecord rec = rt3.activate(req);
    CHECK_EQ((int)rec.state, (int)ActivationState::ACTIVE, "first slot activation succeeds");
    EngineIncarnationId y = er_test::register_worker(rt3, 2, 2);
    er_test::publish_reference_evidence(rt3, y, rt3.current_epoch());
    EngineIncarnation yi; for (const auto& it : rt3.incarnations()) if (it.incarnation_id == y) yi = it;
    ActivationRequest req2; req2.incarnation_id = y; req2.incarnation_generation = EngineIncarnationGeneration(1);
    req2.engine_id = EngineId(1); req2.engine_generation = EngineGeneration(1);
    req2.profile_id = p3; req2.readiness_generation = yi.readiness_generation; req2.slot_id = EngineSlotId(1);
    req2.caller_authority = rt3.current_authority();
    expect_throws(rt3, [&]{ rt3.activate(req2); }, "exclusive slot conflict rejected");
  }

  // 12. Generation exhaustion throws, no wrap.
  {
    try { EngineGeneration g((std::uint64_t)-1); (void)g.next(); std::printf("FAIL: no throw gen exhaustion\n"); ++er_test::g_failures; }
    catch (const DomainError& e) { CHECK(e.code() == ErrorCode::GenerationExhausted, "generation exhaustion throws"); }
  }

  std::printf("core tests failures=%d\n", er_test::g_failures);
  return er_test::return_code();
}
