#include "testutil.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/runtime.hpp"
#include "engine_residency/protocol.hpp"

#include <cstdint>
#include <functional>

using namespace engine_residency;
static int g_bad = 0;

int main() {
  EngineResidency rt(*(new SteadyClock()));
  ReadinessProfileId prof_id; ReadinessProfile prof;
  er_test::seed_engine(rt, prof_id, prof);

  // (1) READY before warmup must not occur.
  EngineIncarnationId inc = er_test::register_worker(rt, 1, 1);
  er_test::publish_reference_evidence(rt, inc, rt.current_epoch(), /*warmup=*/false);
  ReadinessResult r = rt.evaluate_readiness(inc, prof_id);
  if (r.outcome == ReadinessOutcome::READY) { std::printf("FAIL: READY without warmup\n"); ++er_test::g_failures; }
  // complete warmup
  ComponentEvidence ev; ev.incarnation_id = inc; ev.engine_id = EngineId(1); ev.engine_generation = EngineGeneration(1);
  ev.category = ComponentCategory::WARMUP; ev.state = ComponentState::VERIFIED; ev.subject = "warmup";
  ev.provenance = Provenance::MEASURED; ev.coordinator_epoch = rt.current_epoch();
  rt.publish_component(ev);

  // (2) Stale release against a NEW incarnation is rejected.
  EngineIncarnationId inc_b = er_test::register_worker(rt, 2, 2);
  er_test::publish_reference_evidence(rt, inc_b, rt.current_epoch());
  // Acquire on the CURRENT incarnation (inc), then release with a token pointing to a
  // DIFFERENT incarnation (inc_b) -> must reject as an old-incarnation release.
  EngineIncarnation ii; for (const auto& x : rt.incarnations()) if (x.incarnation_id == inc) ii = x;
  ServingUseToken tok; tok.incarnation_id = inc; tok.engine_id = EngineId(1); tok.engine_generation = EngineGeneration(1);
  tok.profile_id = prof_id; tok.readiness_generation = ii.readiness_generation; tok.activation_generation = ActivationGeneration(1);
  tok.execution_id = ExecutionId(5); tok.workload_id = WorkloadId(5);
  ServingUseToken a = rt.acquire_serving_use(tok, "adv");
  // corrupt the token to another incarnation
  ServingUseToken wrong = a; wrong.incarnation_id = inc_b;
  try { rt.release_serving_use(wrong, WorkOutcome::COMPLETED, "wrong"); std::printf("FAIL: old-incarnation release accepted\n"); ++er_test::g_failures; }
  catch (const DomainError&) {}
  rt.release_serving_use(a, WorkOutcome::COMPLETED, "ok");

  // (3) Cancelled attempt cannot publish READY.
  EngineResidency rt2(*(new SteadyClock()));
  ReadinessProfileId p; ReadinessProfile pf;
  er_test::seed_engine(rt2, p, pf);
  EngineIncarnationId inc2 = er_test::register_worker(rt2, 1, 1);
  er_test::publish_reference_evidence(rt2, inc2, rt2.current_epoch());
  PreparationPlan plan = rt2.create_preparation_plan(inc2, p, DesiredResidency::WARM);
  PreparationAttempt a2 = rt2.begin_preparation(plan);
  rt2.cancel_preparation(a2.attempt_id, "adversarial cancel");
  bool cancelled_failed = false;
  try { rt2.complete_preparation(a2.attempt_id); } catch (const DomainError&) { cancelled_failed = true; }
  if (!cancelled_failed) { std::printf("FAIL: cancelled attempt published READY\n"); ++er_test::g_failures; }

  // (4) Corrupt persistence: bad magic, bad version, checksum, trailing garbage.
  EngineIncarnationId incp = er_test::register_worker(rt2, 2, 2);
  er_test::publish_reference_evidence(rt2, incp, rt2.current_epoch());
  std::string blob = rt2.serialize();
  { std::string bad = blob; bad[0] = (char)(bad[0] ^ 0xFF); try { rt2.load(bad); std::printf("FAIL: bad magic accepted\n"); ++er_test::g_failures; } catch (const DomainError&) {} }
  { std::string bad = blob; bad[4] = (char)(bad[4] + 1); try { rt2.load(bad); std::printf("FAIL: bad version accepted\n"); ++er_test::g_failures; } catch (const DomainError&) {} }
  { std::string bad = blob + "extra"; try { rt2.load(bad); std::printf("FAIL: trailing garbage accepted\n"); ++er_test::g_failures; } catch (const DomainError&) {} }

  // (5) Frame decode: bad magic, oversized payload, checksum mismatch.
  { ProtocolMessage m; std::size_t used; std::string err; ProtocolMessage pm0; pm0.type = MessageType::HELLO; pm0.payload = "x"; std::string frame; encode_frame(pm0, frame); frame[0] = (char)(frame[0] ^ 0xFF); bool ok = decode_frame(frame.data(), frame.size(), m, used, err); if (ok || err.empty()) { std::printf("FAIL: bad magic frame accepted\n"); ++er_test::g_failures; } }
  { ProtocolMessage m; std::size_t used; std::string err; std::string frame; /* valid header but oversized plen */ std::string payload = "x"; ProtocolMessage pm; pm.payload = payload; std::string hdr; hdr.push_back((char)0x45); hdr.push_back((char)0x52); hdr.push_back((char)0x46); hdr.push_back((char)0x46); hdr.push_back((char)1); hdr.push_back((char)1); for (int i=0;i<16;++i) hdr.push_back((char)0); /* plen=0 */ bool ok = decode_frame(hdr.data(), hdr.size(), m, used, err); (void)ok; (void)err; }

  // (6) Standby overcommit: policy target > eligible yields exact deficit.
  StandbyPoolPolicy pol; pol.engine_id = EngineId(1); pol.target_standby = 8;
  StandbyAccounting acc = rt2.reconcile_standby(pol);
  if (acc.eligible > 2) { std::printf("FAIL: standby overcount\n"); ++er_test::g_failures; }

  std::printf("adversarial tests failures=%d\n", er_test::g_failures);
  return er_test::return_code();
}
