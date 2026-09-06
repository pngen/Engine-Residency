#include "engine_residency/runtime.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine_residency/persistence.hpp"
#include "detail/internal.hpp"

namespace engine_residency {

class EngineResidencyImpl {
 public:
  EngineResidencyImpl(Clock& c, RuntimeOptions o) : clock(c), opts(o) {}

  Clock& clock;
  RuntimeOptions opts;
  mutable std::shared_mutex mtx;

  AuthorityState authority;
  bool epoch_started{false};

  std::unordered_map<EngineId, EngineDefinition> engines;
  std::unordered_map<ReadinessProfileId, ReadinessProfile> profiles;
  std::unordered_map<EngineIncarnationId, EngineIncarnation> incarnations;
  std::unordered_map<EngineId, EngineIncarnationId> current_by_engine;
  std::unordered_map<EngineIncarnationId, std::vector<ComponentEvidence>> evidence;
  std::unordered_map<EngineIncarnationId, PreparationAttemptId> active_attempt;
  std::unordered_map<PreparationAttemptId, PreparationAttempt> attempts;
  std::unordered_map<PreparationPlanId, PreparationPlan> plans;
  std::unordered_map<ServingUseId, ActiveUse> active_uses;
  std::uint64_t active_use_count{0};
  std::unordered_map<EngineSlotId, StandbySlot> slots;
  std::unordered_map<EnginePoolId, StandbyPoolPolicy> pools;
  std::unordered_map<ActivationId, ActivationRecord> activations;
  std::unordered_map<EngineIncarnationId, DrainState> drains;
  std::unordered_map<ReplacementId, ReplacementPlan> replacement_plans;
  std::unordered_map<RegistrationPermitId, RegistrationPermit> permits;
  std::unordered_set<RegistrationPermitId> consumed;
  std::unordered_map<WorkerId, std::unordered_set<WorkerBootId>> fenced;
  std::vector<CostComponent> cost_history;

  std::uint64_t id_engine{0}, id_profile{0}, id_inc{0}, id_evid{0}, id_use{0};
  std::uint64_t id_attempt{0}, id_plan{0}, id_activation{0}, id_slot{0}, id_repl{0}, id_permit{0};

  CoordinatorEpoch ensure_epoch() {
    if (!epoch_started) { authority.epoch = CoordinatorEpoch(1); authority.authority = AuthorityGeneration(1); epoch_started = true; }
    return authority.epoch;
  }

  const EngineIncarnation* find_incc(EngineIncarnationId id) const {
    auto it = incarnations.find(id); return it == incarnations.end() ? nullptr : &it->second;
  }
  EngineIncarnation* find_inc(EngineIncarnationId id) {
    auto it = incarnations.find(id); return it == incarnations.end() ? nullptr : &it->second;
  }
};

// ---- EngineResidency facade ------------------------------------------------
EngineResidency::EngineResidency(Clock& clock, RuntimeOptions options)
    : impl_(std::make_unique<EngineResidencyImpl>(clock, options)) {
  impl_->ensure_epoch();
}
EngineResidency::~EngineResidency() = default;

EngineDefinition EngineResidency::define_engine(EngineDefinition def) {
  std::unique_lock lk(impl_->mtx);
  if (!def.engine_id.is_valid()) throw_error(ErrorCode::InvalidArgument, "engine id required");
  if (!def.engine_generation.is_valid()) def.engine_generation = EngineGeneration(++impl_->id_engine + 1);
  if (!def.config_generation.is_valid()) def.config_generation = EngineConfigGeneration(1);
  if (impl_->engines.size() >= impl_->opts.max_engines) throw_error(ErrorCode::BoundsExceeded, "max engines");
  EngineDefinition& stored = impl_->engines[def.engine_id] = def;
  return stored;
}

void EngineResidency::update_engine_config(EngineId engine_id, EngineConfigGeneration new_gen, const std::string& reason) {
  std::unique_lock lk(impl_->mtx);
  auto it = impl_->engines.find(engine_id);
  if (it == impl_->engines.end()) throw_error(ErrorCode::UnknownEngine, "unknown engine");
  if (new_gen.is_at_or_before(it->second.config_generation)) throw_error(ErrorCode::StaleGeneration, "config generation regression");
  (void)reason;
  it->second.config_generation = new_gen;
  // Invalidate all incarnations and evidence of this engine (config change).
  for (auto& kv : impl_->incarnations) {
    if (kv.second.engine_id == engine_id) {
      kv.second.state.preparation = PreparationState::INVALIDATED;
      kv.second.readiness_generation = kv.second.readiness_generation.next();
    }
  }
}

ReadinessProfile EngineResidency::define_profile(ReadinessProfile profile) {
  std::unique_lock lk(impl_->mtx);
  if (!profile.profile_id.is_valid()) {
    profile.profile_id = ReadinessProfileId(++impl_->id_profile + 1);
  }
  if (!profile.profile_generation.is_valid()) profile.profile_generation = ReadinessProfileGeneration(1);
  if (impl_->engines.find(profile.engine_id) == impl_->engines.end()) throw_error(ErrorCode::UnknownEngine, "engine not defined");
  ReadinessProfile& stored = impl_->profiles[profile.profile_id] = profile;
  return stored;
}

// ---- Registration/authority ----------------------------------------------
RegistrationPermit EngineResidency::issue_registration_permit(WorkerId w, WorkerBootId b, SourceId s, SourceBootId sb) {
  std::unique_lock lk(impl_->mtx);
  impl_->ensure_epoch();
  if (b.is_before(WorkerBootId(1))) throw_error(ErrorCode::InvalidArgument, "worker boot required");
  // Fenced boots may not obtain a fresh permit.
  auto fi = impl_->fenced.find(w);
  if (fi != impl_->fenced.end() && fi->second.count(b)) throw_error(ErrorCode::FencedWorker, "fenced worker boot");
  if (impl_->permits.size() >= impl_->opts.max_registration_permits) throw_error(ErrorCode::BoundsExceeded, "max permits");
  RegistrationPermit p;
  p.permit_id = RegistrationPermitId(++impl_->id_permit + 1);
  p.epoch = impl_->authority.epoch;
  (void)s; (void)sb;
  p.worker_id = w; p.worker_boot = b;
  p.sequence = ++impl_->authority.registration_counter;
  p.authority = impl_->authority.authority;
  p.consumed = false;
  impl_->permits[p.permit_id] = p;
  return p;
}

EngineIncarnation EngineResidency::register_incarnation(EngineIncarnation inc, RegistrationPermit permit) {
  std::unique_lock lk(impl_->mtx);
  impl_->ensure_epoch();
  auto pit = impl_->permits.find(permit.permit_id);
  if (pit == impl_->permits.end()) throw_error(ErrorCode::RegistrationRejected, "unknown permit");
  if (pit->second.consumed) throw_error(ErrorCode::RegistrationRejected, "permit already consumed");
  if (pit->second.worker_id != inc.worker_id || pit->second.worker_boot != inc.worker_boot)
    throw_error(ErrorCode::RegistrationRejected, "permit/worker mismatch");
  if (pit->second.epoch != impl_->authority.epoch) throw_error(ErrorCode::StaleEpoch, "permit epoch stale");
  auto fi = impl_->fenced.find(inc.worker_id);
  if (fi != impl_->fenced.end() && fi->second.count(inc.worker_boot)) throw_error(ErrorCode::FencedWorker, "fenced boot");
  if (impl_->engines.find(inc.engine_id) == impl_->engines.end()) throw_error(ErrorCode::UnknownEngine, "unknown engine");
  // Reject an already-registered live incarnation for this worker+boot (duplicate).
  for (const auto& kv : impl_->incarnations) {
    if (kv.second.worker_id == inc.worker_id && kv.second.worker_boot == inc.worker_boot &&
        kv.second.state.lifecycle != ProcessLifecycle::LOST && kv.second.state.lifecycle != ProcessLifecycle::FAILED &&
        kv.second.state.lifecycle != ProcessLifecycle::RETIRED) {
      throw_error(ErrorCode::RegistrationRejected, "duplicate incarnation for boot");
    }
  }
  if (impl_->incarnations.size() >= impl_->opts.max_incarnations) throw_error(ErrorCode::BoundsExceeded, "max incarnations");
  inc.incarnation_id = EngineIncarnationId(++impl_->id_inc + 1);
  inc.incarnation_generation = EngineIncarnationGeneration(1);
  inc.coordinator_epoch = impl_->authority.epoch;
  inc.authority_generation = impl_->authority.authority;
  inc.registration = RegistrationState::REGISTERED;
  inc.state.lifecycle = ProcessLifecycle::STARTING;
  inc.state.recovery = RecoveryState::NOMINAL;
  inc.state.registration = RegistrationState::REGISTERED;
  inc.readiness_generation = ReadinessGeneration(1);
  inc.last_activity_ns = impl_->clock.now_ns();
  // First incarnation for an engine becomes current; otherwise a candidate.
  auto cur = impl_->current_by_engine.find(inc.engine_id);
  bool has_current = false;
  if (cur != impl_->current_by_engine.end()) {
    auto curinc = impl_->incarnations.find(cur->second);
    if (curinc != impl_->incarnations.end() && curinc->second.state.lifecycle != ProcessLifecycle::LOST &&
        curinc->second.state.lifecycle != ProcessLifecycle::FAILED && curinc->second.state.lifecycle != ProcessLifecycle::RETIRED) {
      has_current = true;
    }
  }
  inc.is_current = !has_current;
  if (inc.is_current) impl_->current_by_engine[inc.engine_id] = inc.incarnation_id;
  EngineIncarnation& stored = impl_->incarnations[inc.incarnation_id] = inc;
  pit->second.consumed = true;
  impl_->consumed.insert(pit->first);
  return stored;
}

void EngineResidency::mark_running(EngineIncarnationId inc_id) {
  std::unique_lock lk(impl_->mtx);
  EngineIncarnation* inc = impl_->find_inc(inc_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  inc->state.lifecycle = ProcessLifecycle::RUNNING;
  inc->state.health = HealthState::HEALTHY;
  inc->last_activity_ns = impl_->clock.now_ns();
}

void EngineResidency::revalidate(EngineIncarnationId inc_id) {
  std::unique_lock lk(impl_->mtx);
  EngineIncarnation* inc = impl_->find_inc(inc_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  inc->state.recovery = RecoveryState::NOMINAL;
  inc->state.preparation = PreparationState::PREPARED;
  inc->is_current = true;
  inc->state.lifecycle = ProcessLifecycle::RUNNING;
  impl_->current_by_engine[inc->engine_id] = inc->incarnation_id;
  inc->readiness_generation = inc->readiness_generation.next();
}

void EngineResidency::fence_worker(WorkerId w, WorkerBootId b, const std::string& /*reason*/) {
  std::unique_lock lk(impl_->mtx);
  impl_->fenced[w].insert(b);
  for (auto& kv : impl_->incarnations) {
    if (kv.second.worker_id == w && kv.second.worker_boot == b) {
      kv.second.state.health = HealthState::FENCED;
      kv.second.state.lifecycle = ProcessLifecycle::LOST;
      kv.second.state.activation = ActivationState::FENCED;
      kv.second.state.registration = RegistrationState::FENCED;
      kv.second.is_current = false;
    }
  }
}

CoordinatorEpoch EngineResidency::advance_epoch() {
  std::unique_lock lk(impl_->mtx);
  impl_->ensure_epoch();
  impl_->authority.epoch = impl_->authority.epoch.next();
  impl_->authority.authority = impl_->authority.authority.next();
  // Conservative: all surviving incarnations must revalidate; no dynamic READY survives.
  for (auto& kv : impl_->incarnations) {
    if (kv.second.state.lifecycle == ProcessLifecycle::RUNNING) {
      kv.second.state.recovery = RecoveryState::REVALIDATING;
      kv.second.is_current = false;
    }
  }
  // Epoch advance resets serving authority: previously-assigned standby slots are
  // released so revalidated incarnations can be re-authorized under the new epoch.
  impl_->slots.clear();
  return impl_->authority.epoch;
}
// ---- Component evidence ----------------------------------------------------
ComponentEvidence EngineResidency::publish_component(ComponentEvidence ev) {
  std::unique_lock lk(impl_->mtx);
  impl_->ensure_epoch();
  EngineIncarnation* inc = impl_->find_inc(ev.incarnation_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  if (inc->state.registration == RegistrationState::FENCED || inc->state.health == HealthState::FENCED) throw_error(ErrorCode::FencedWorker, "fenced incarnation");
  auto fi = impl_->fenced.find(inc->worker_id);
  if (fi != impl_->fenced.end() && fi->second.count(inc->worker_boot)) throw_error(ErrorCode::FencedWorker, "fenced boot");
  if (ev.coordinator_epoch != impl_->authority.epoch) throw_error(ErrorCode::StaleEpoch, "evidence epoch is not current");
  std::size_t total = 0;
  for (const auto& kv : impl_->evidence) total += kv.second.size();
  if (total >= impl_->opts.max_component_evidence) throw_error(ErrorCode::BoundsExceeded, "max component evidence");
  ev.evidence_id = EvidenceId(++impl_->id_evid + 1);
  ev.evidence_generation = EvidenceGeneration(1);
  ev.observed_at_ns = impl_->clock.now_ns();
  impl_->evidence[ev.incarnation_id].push_back(ev);
  inc->last_activity_ns = ev.observed_at_ns;
  // Binding a backend/device context confirms startup; transition to RUNNING.
  if ((ev.category == ComponentCategory::BACKEND || ev.category == ComponentCategory::DEVICE_CONTEXT) &&
      inc->state.lifecycle == ProcessLifecycle::STARTING) {
    inc->state.lifecycle = ProcessLifecycle::RUNNING;
    inc->state.health = HealthState::HEALTHY;
  }
  return ev;
}

// ---- Readiness -------------------------------------------------------------
ReadinessResult EngineResidency::evaluate_readiness(EngineIncarnationId inc_id, ReadinessProfileId prof_id) {
  std::shared_lock lk(impl_->mtx);
  const EngineIncarnation* inc = impl_->find_incc(inc_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  auto pit = impl_->profiles.find(prof_id);
  if (pit == impl_->profiles.end()) throw_error(ErrorCode::UnknownProfile, "unknown profile");
  if (pit->second.engine_id != inc->engine_id) throw_error(ErrorCode::IncompatibleProfile, "profile belongs to another engine");
  auto eit = impl_->engines.find(inc->engine_id);
  if (eit == impl_->engines.end()) throw_error(ErrorCode::UnknownEngine, "unknown engine");
  if (inc->state.recovery == RecoveryState::REVALIDATING || inc->state.recovery == RecoveryState::AWAITING_AUTHORITY) {
    ReadinessResult r; r.engine_id = inc->engine_id; r.engine_generation = inc->engine_generation;
    r.incarnation_id = inc->incarnation_id; r.incarnation_generation = inc->incarnation_generation;
    r.profile_id = prof_id; r.profile_generation = pit->second.profile_generation;
    r.readiness_generation = inc->readiness_generation; r.outcome = ReadinessOutcome::REVALIDATION_REQUIRED;
    r.coherent_snapshot = false; r.detail = "incarnation requires revalidation under current authority";
    return r;
  }
  static const std::vector<ComponentEvidence> kEmpty;
  const auto& ev = impl_->evidence.count(inc_id) ? impl_->evidence.at(inc_id) : kEmpty;
  return detail::evaluate_readiness(eit->second, pit->second, *inc, ev, impl_->clock.now_ns());
}

// ---- Preparation -----------------------------------------------------------
PreparationPlan EngineResidency::create_preparation_plan(EngineIncarnationId inc_id, ReadinessProfileId prof_id, DesiredResidency target) {
  std::unique_lock lk(impl_->mtx);
  EngineIncarnation* inc = impl_->find_inc(inc_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  if (!inc->is_current) throw_error(ErrorCode::StaleIncarnation, "not current");
  auto pit = impl_->profiles.find(prof_id);
  if (pit == impl_->profiles.end()) throw_error(ErrorCode::UnknownProfile, "unknown profile");
  if (impl_->plans.size() >= impl_->opts.max_pending_preparation_attempts) throw_error(ErrorCode::BoundsExceeded, "max plans");
  ReadinessResult rr = detail::evaluate_readiness(impl_->engines.at(inc->engine_id), pit->second, *inc,
      impl_->evidence.count(inc_id) ? impl_->evidence.at(inc_id) : std::vector<ComponentEvidence>{}, impl_->clock.now_ns());
  PreparationPlan plan;
  plan.plan_id = PreparationPlanId(++impl_->id_plan + 1);
  plan.plan_generation = PreparationPlanGeneration(1);
  plan.incarnation_id = inc_id; plan.incarnation_generation = inc->incarnation_generation;
  plan.profile_id = prof_id; plan.profile_generation = pit->second.profile_generation;
  plan.target_residency = target; plan.epoch = impl_->authority.epoch; plan.authority = impl_->authority.authority;
  plan.source_boot = inc->source_boot;
  std::uint32_t steps = 0;
  for (const auto& m : rr.missing) {
    if (steps >= impl_->opts.max_preparation_plan_steps) break;
    PreparationStep st; st.action = PreparationAction::BIND_MODEL;
    st.category = ComponentCategory::MODEL; st.subject = m;
    st.description = "prepare missing component " + m; st.rollback_required = false;
    plan.steps.push_back(std::move(st)); ++steps;
  }
  if (plan.steps.empty()) { PreparationStep wp; wp.action = PreparationAction::WARM_UP; wp.category = ComponentCategory::WARMUP; wp.subject = "warmup"; wp.description = "run warmup"; plan.steps.push_back(wp); }
  plan.cancellation_boundary = "before readiness publication";
  plan.rollback_action = "release attempt-owned resources; keep verified state";
  plan.expected_readiness = "READY after all steps complete";
  impl_->plans[plan.plan_id] = plan;
  inc->state.preparation = PreparationState::PLANNED;
  return plan;
}

PreparationAttempt EngineResidency::begin_preparation(PreparationPlan plan) {
  std::unique_lock lk(impl_->mtx);
  auto pit = impl_->plans.find(plan.plan_id);
  if (pit == impl_->plans.end()) throw_error(ErrorCode::UnknownAttempt, "unknown plan");
  if (pit->second.epoch != impl_->authority.epoch) throw_error(ErrorCode::PlanStale, "plan epoch stale");
  EngineIncarnation* inc = impl_->find_inc(plan.incarnation_id);
  if (inc == nullptr || !inc->is_current) throw_error(ErrorCode::StaleIncarnation, "incarnation not current");
  auto prev = impl_->active_attempt.find(plan.incarnation_id);
  if (prev != impl_->active_attempt.end()) throw_error(ErrorCode::InvalidState, "attempt already active");
  if (impl_->active_attempt.size() >= impl_->opts.max_pending_preparation_attempts) throw_error(ErrorCode::BoundsExceeded, "max attempts");
  PreparationAttempt a;
  a.attempt_id = PreparationAttemptId(++impl_->id_attempt + 1);
  a.attempt_generation = PreparationAttemptGeneration(1);
  a.plan_id = plan.plan_id; a.plan_generation = plan.plan_generation;
  a.incarnation_id = plan.incarnation_id; a.incarnation_generation = plan.incarnation_generation;
  a.profile_id = plan.profile_id; a.profile_generation = plan.profile_generation;
  a.epoch = impl_->authority.epoch; a.authority = impl_->authority.authority;
  a.state = PreparationState::PLANNED; a.cancelled = false; a.published_ready = false;
  impl_->attempts[a.attempt_id] = a;
  impl_->active_attempt[plan.incarnation_id] = a.attempt_id;
  inc->state.preparation = PreparationState::PREPARING;
  return impl_->attempts[a.attempt_id];
}

PreparationAttempt EngineResidency::step_preparation(PreparationAttemptId attempt_id) {
  std::unique_lock lk(impl_->mtx);
  auto it = impl_->attempts.find(attempt_id);
  if (it == impl_->attempts.end()) throw_error(ErrorCode::UnknownAttempt, "unknown attempt");
  PreparationAttempt& a = it->second;
  if (a.cancelled) throw_error(ErrorCode::AttemptCancelled, "attempt cancelled");
  if (a.epoch != impl_->authority.epoch) throw_error(ErrorCode::StaleEpoch, "attempt epoch stale");
  auto pit = impl_->plans.find(a.plan_id);
  if (pit == impl_->plans.end()) throw_error(ErrorCode::UnknownAttempt, "unknown plan");
  const PreparationPlan& plan = pit->second;
  if (a.next_step_index < plan.steps.size()) {
    a.completed_steps.push_back(plan.steps[a.next_step_index].description);
    ++a.next_step_index;
  }
  if (a.next_step_index >= plan.steps.size()) a.state = PreparationState::PREPARING;
  return a;
}

PreparationAttempt EngineResidency::complete_preparation(PreparationAttemptId attempt_id) {
  std::unique_lock lk(impl_->mtx);
  auto it = impl_->attempts.find(attempt_id);
  if (it == impl_->attempts.end()) throw_error(ErrorCode::UnknownAttempt, "unknown attempt");
  PreparationAttempt& a = it->second;
  if (a.cancelled) throw_error(ErrorCode::AttemptCancelled, "attempt cancelled");
  if (a.epoch != impl_->authority.epoch) throw_error(ErrorCode::StaleEpoch, "attempt epoch stale");
  if (a.authority != impl_->authority.authority) throw_error(ErrorCode::StaleAuthority, "attempt authority stale");
  EngineIncarnation* inc = impl_->find_inc(a.incarnation_id);
  if (inc == nullptr || !inc->is_current) throw_error(ErrorCode::StaleIncarnation, "incarnation not current");
  auto pit = impl_->profiles.find(a.profile_id);
  if (pit == impl_->profiles.end()) throw_error(ErrorCode::UnknownProfile, "unknown profile");
  auto eit = impl_->engines.find(inc->engine_id);
  if (eit == impl_->engines.end()) throw_error(ErrorCode::UnknownEngine, "unknown engine");
  ReadinessResult rr = detail::evaluate_readiness(eit->second, pit->second, *inc,
      impl_->evidence.count(a.incarnation_id) ? impl_->evidence.at(a.incarnation_id) : std::vector<ComponentEvidence>{}, impl_->clock.now_ns());
  a.authority_revalidated = true;
  if (rr.outcome == ReadinessOutcome::READY || rr.outcome == ReadinessOutcome::DEGRADED) {
    a.state = PreparationState::PREPARED; a.published_ready = true;
    inc->state.preparation = PreparationState::PREPARED;
    inc->readiness_generation = inc->readiness_generation.next();
    inc->state.observed = DesiredResidency::WARM;
  } else {
    a.state = PreparationState::FAILED; a.failure_stage = "readiness not achieved";
    inc->state.preparation = PreparationState::FAILED;
  }
  impl_->active_attempt.erase(a.incarnation_id);
  return a;
}

void EngineResidency::cancel_preparation(PreparationAttemptId attempt_id, const std::string& /*reason*/) {
  std::unique_lock lk(impl_->mtx);
  auto it = impl_->attempts.find(attempt_id);
  if (it == impl_->attempts.end()) throw_error(ErrorCode::UnknownAttempt, "unknown attempt");
  PreparationAttempt& a = it->second;
  if (a.published_ready) throw_error(ErrorCode::InvalidState, "cannot cancel committed attempt");
  a.cancelled = true; a.state = PreparationState::FAILED;
  impl_->active_attempt.erase(a.incarnation_id);
}

void EngineResidency::rollback_preparation(PreparationAttemptId attempt_id, const std::string& /*reason*/) {
  std::unique_lock lk(impl_->mtx);
  auto it = impl_->attempts.find(attempt_id);
  if (it == impl_->attempts.end()) throw_error(ErrorCode::UnknownAttempt, "unknown attempt");
  PreparationAttempt& a = it->second;
  a.state = PreparationState::FAILED; a.failure_stage = "rolled back";
  impl_->active_attempt.erase(a.incarnation_id);
}

// ---- Standby ---------------------------------------------------------------
StandbyAccounting EngineResidency::reconcile_standby(const StandbyPoolPolicy& policy) {
  std::shared_lock lk(impl_->mtx);
  std::vector<const EngineIncarnation*> eligible;
  std::vector<const EngineIncarnation*> blocked;
  std::unordered_map<EngineIncarnationId, ReadinessOutcome> readiness;
  std::uint32_t active = 0;
  for (const auto& kv : impl_->incarnations) {
    const EngineIncarnation& ir = kv.second;
    if (ir.engine_id != policy.engine_id) continue;
    if (ir.state.health == HealthState::LOST || ir.state.health == HealthState::FENCED ||
        ir.state.lifecycle == ProcessLifecycle::LOST || ir.state.lifecycle == ProcessLifecycle::FAILED ||
        ir.state.lifecycle == ProcessLifecycle::RETIRED) {
      blocked.push_back(&ir); continue;
    }
    if (ir.state.preparation != PreparationState::PREPARED && ir.state.preparation != PreparationState::PLANNED) {}
    if (ir.state.activation == ActivationState::ACTIVE) { ++active; continue; }
    ReadinessOutcome best = ReadinessOutcome::UNKNOWN;
    for (const auto& pk : impl_->profiles) {
      if (pk.second.engine_id != policy.engine_id) continue;
      ReadinessResult r = detail::evaluate_readiness(impl_->engines.at(ir.engine_id), pk.second, ir,
          impl_->evidence.count(ir.incarnation_id) ? impl_->evidence.at(ir.incarnation_id) : std::vector<ComponentEvidence>{}, impl_->clock.now_ns());
      if (r.outcome == ReadinessOutcome::READY) { best = ReadinessOutcome::READY; break; }
      if (r.outcome == ReadinessOutcome::DEGRADED) best = ReadinessOutcome::DEGRADED;
    }
    readiness[ir.incarnation_id] = best;
    if (best == ReadinessOutcome::READY || best == ReadinessOutcome::DEGRADED) eligible.push_back(&ir);
    else blocked.push_back(&ir);
  }
  std::vector<EngineSlotId> slot_ids;
  for (const auto& kv : impl_->slots) slot_ids.push_back(kv.first);
  return detail::reconcile_standby(policy, slot_ids, eligible, blocked, readiness, active);
}

// ---- Activation ------------------------------------------------------------
ActivationRecord EngineResidency::activate(const ActivationRequest& req) {
  std::unique_lock lk(impl_->mtx);
  if (req.caller_authority != impl_->authority.authority) throw_error(ErrorCode::StaleAuthority, "caller authority stale");
  EngineIncarnation* inc = impl_->find_inc(req.incarnation_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  if (inc->state.health == HealthState::FENCED || inc->state.registration == RegistrationState::FENCED) throw_error(ErrorCode::FencedWorker, "fenced incarnation");
  if (inc->state.lifecycle != ProcessLifecycle::RUNNING) throw_error(ErrorCode::InvalidState, "incarnation not running");
  if (inc->readiness_generation != req.readiness_generation) throw_error(ErrorCode::StaleGeneration, "readiness generation stale");
  if (req.profile_id.is_valid()) {
    auto pit = impl_->profiles.find(req.profile_id);
    if (pit == impl_->profiles.end()) throw_error(ErrorCode::UnknownProfile, "unknown profile");
    ReadinessResult rr = detail::evaluate_readiness(impl_->engines.at(inc->engine_id), pit->second, *inc,
        impl_->evidence.count(inc->incarnation_id) ? impl_->evidence.at(inc->incarnation_id) : std::vector<ComponentEvidence>{}, impl_->clock.now_ns());
    if (rr.outcome != ReadinessOutcome::READY && rr.outcome != ReadinessOutcome::DEGRADED)
      throw_error(ErrorCode::InvalidState, "activation requires current readiness");
  }
  // Exclusive slot: at most one current activation.
  if (req.slot_id.is_valid()) {
    auto sit = impl_->slots.find(req.slot_id);
    if (sit != impl_->slots.end()) {
      if (sit->second.assigned_incarnation.is_valid() && sit->second.assigned_incarnation != inc->incarnation_id) {
        // A fenced/dead holder must not block takeover by standby/replacement.
        const EngineIncarnation* holder = impl_->find_incc(sit->second.assigned_incarnation);
        bool holder_dead = (holder == nullptr) || holder->state.health == HealthState::FENCED ||
                           holder->state.health == HealthState::LOST ||
                           holder->state.lifecycle == ProcessLifecycle::LOST ||
                           holder->state.lifecycle == ProcessLifecycle::RETIRED ||
                           holder->state.lifecycle == ProcessLifecycle::FAILED ||
                           holder->state.recovery == RecoveryState::REVALIDATING ||
                           holder->state.recovery == RecoveryState::AWAITING_AUTHORITY;
        if (!holder_dead) throw_error(ErrorCode::ActivationConflict, "exclusive slot already assigned");
      }
      sit->second.assigned_incarnation = inc->incarnation_id;
    } else {
      StandbySlot s; s.slot_id = req.slot_id; s.pool_id = EnginePoolId(1); s.name = "default";
      s.assigned_incarnation = inc->incarnation_id;
      impl_->slots[req.slot_id] = s;
    }
  }
  ActivationRecord rec;
  rec.activation_id = ActivationId(++impl_->id_activation + 1);
  rec.activation_generation = ActivationGeneration(1);
  rec.engine_id = inc->engine_id; rec.engine_generation = inc->engine_generation;
  rec.incarnation_id = inc->incarnation_id; rec.incarnation_generation = inc->incarnation_generation;
  rec.profile_id = req.profile_id;
  rec.readiness_generation = inc->readiness_generation; rec.standby_generation = req.standby_generation;
  rec.slot_id = req.slot_id;
  rec.prior = inc->state.activation;
  rec.state = ActivationState::ACTIVE; rec.current = true;
  rec.detail = "authorized local activation";
  inc->state.activation = ActivationState::ACTIVE;
  inc->is_current = true;
  impl_->current_by_engine[inc->engine_id] = inc->incarnation_id;
  impl_->activations[rec.activation_id] = rec;
  return rec;
}

// ---- Serving use -----------------------------------------------------------
ServingUseToken EngineResidency::acquire_serving_use(const ServingUseToken& wanted, const std::string& /*caller*/) {
  std::unique_lock lk(impl_->mtx);
  EngineIncarnation* inc = impl_->find_inc(wanted.incarnation_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  if (!inc->is_current) throw_error(ErrorCode::StaleIncarnation, "not current");
  if (inc->state.lifecycle != ProcessLifecycle::RUNNING) throw_error(ErrorCode::InvalidState, "not running");
  auto dit = impl_->drains.find(wanted.incarnation_id);
  if (dit != impl_->drains.end() && dit->second.admission_fenced) throw_error(ErrorCode::UseAfterDrain, "admission fenced by drain");
  if (wanted.readiness_generation != inc->readiness_generation) throw_error(ErrorCode::StaleGeneration, "stale readiness acquisition");
  // Atomically re-validate that the current snapshot still satisfies the profile.
  auto pit = impl_->profiles.find(wanted.profile_id);
  if (pit == impl_->profiles.end()) throw_error(ErrorCode::UnknownProfile, "unknown profile");
  auto eit = impl_->engines.find(inc->engine_id);
  if (eit == impl_->engines.end()) throw_error(ErrorCode::UnknownEngine, "unknown engine");
  ReadinessResult rr = detail::evaluate_readiness(eit->second, pit->second, *inc,
      impl_->evidence.count(inc->incarnation_id) ? impl_->evidence.at(inc->incarnation_id) : std::vector<ComponentEvidence>{}, impl_->clock.now_ns());
  if (rr.outcome != ReadinessOutcome::READY && rr.outcome != ReadinessOutcome::DEGRADED) {
    throw_error(ErrorCode::InvalidState, std::string("readiness no longer satisfied: ") + std::string(to_string(rr.outcome)));
  }
  if (impl_->active_use_count >= impl_->opts.max_active_uses) throw_error(ErrorCode::BoundsExceeded, "max active uses");
  ServingUseToken tok = wanted;
  tok.use_id = ServingUseId(++impl_->id_use + 1);
  tok.use_generation = ServingUseGeneration(1);
  ActiveUse au;
  au.use_id = tok.use_id; au.use_generation = tok.use_generation;
  au.engine_id = inc->engine_id; au.engine_generation = inc->engine_generation;
  au.incarnation_id = inc->incarnation_id; au.incarnation_generation = inc->incarnation_generation;
  au.profile_id = wanted.profile_id; au.readiness_generation = wanted.readiness_generation;
  au.activation_generation = wanted.activation_generation; au.execution_id = wanted.execution_id;
  au.released = false; au.outcome = WorkOutcome::COMPLETED;
  au.admitted_at_ns = impl_->clock.now_ns();
  au.detail = "admitted";
  impl_->active_uses[au.use_id] = au;
  ++impl_->active_use_count;
  inc->last_activity_ns = au.admitted_at_ns;
  return tok;
}

void EngineResidency::release_serving_use(const ServingUseToken& token, WorkOutcome outcome, const std::string& detail) {
  std::unique_lock lk(impl_->mtx);
  auto it = impl_->active_uses.find(token.use_id);
  if (it == impl_->active_uses.end()) throw_error(ErrorCode::UnknownUse, "unknown serving use");
  ActiveUse& au = it->second;
  if (au.released) throw_error(ErrorCode::DuplicateRelease, "duplicate release has no effect");
  if (au.incarnation_id != token.incarnation_id) throw_error(ErrorCode::StaleIncarnation, "release from old incarnation");
  au.released = true; au.outcome = outcome; au.released_at_ns = impl_->clock.now_ns(); au.detail = detail;
  if (impl_->active_use_count == 0) throw_error(ErrorCode::InvalidState, "active use underflow");
  --impl_->active_use_count;
  std::uint32_t remaining_for_this = 0;
  for (const auto& kv : impl_->active_uses) {
    if (kv.second.incarnation_id == token.incarnation_id && !kv.second.released) ++remaining_for_this;
  }
  auto dit = impl_->drains.find(token.incarnation_id);
  if (dit != impl_->drains.end() && dit->second.admission_fenced && remaining_for_this == 0) {
    if (dit->second.backend_cleanup_ack) {
      dit->second.phase = DrainPhase::DRAINED;
    } else {
      dit->second.phase = DrainPhase::BACKEND_CLEANUP_PENDING;
    }
  }
}

// ---- Drain ----------------------------------------------------------------
void EngineResidency::request_drain(EngineIncarnationId inc_id) {
  std::unique_lock lk(impl_->mtx);
  EngineIncarnation* inc = impl_->find_inc(inc_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  auto& d = impl_->drains[inc_id];
  d.generation = d.generation.next();
  d.admission_fenced = true;
  d.detail = "drain requested";
  inc->state.lifecycle = ProcessLifecycle::DRAINING;
  inc->state.activation = ActivationState::DRAINING;
  if (d.phase == DrainPhase::NONE) d.phase = DrainPhase::REQUESTED;
  // If no active use remains, move to backend-cleanup-pending.
  std::uint32_t remaining = 0;
  for (const auto& kv : impl_->active_uses) if (kv.second.incarnation_id == inc_id && !kv.second.released) ++remaining;
  if (remaining == 0) { d.phase = DrainPhase::BACKEND_CLEANUP_PENDING; } 
  else { d.phase = DrainPhase::ACTIVE_USE_REMAINING; }
}

void EngineResidency::acknowledge_backend_cleanup(EngineIncarnationId inc_id) {
  std::unique_lock lk(impl_->mtx);
  auto it = impl_->drains.find(inc_id);
  if (it == impl_->drains.end()) throw_error(ErrorCode::InvalidState, "no drain in progress");
  it->second.backend_cleanup_ack = true;
  std::uint32_t remaining = 0;
  for (const auto& kv : impl_->active_uses) if (kv.second.incarnation_id == inc_id && !kv.second.released) ++remaining;
  if (it->second.admission_fenced && remaining == 0) {
    it->second.phase = DrainPhase::DRAINED;
  }
}

DrainState EngineResidency::drain_state(EngineIncarnationId inc_id) const {
  std::shared_lock lk(impl_->mtx);
  auto it = impl_->drains.find(inc_id);
  if (it == impl_->drains.end()) { DrainState d; d.phase = DrainPhase::NONE; return d; }
  return it->second;
}

// ---- Replacement ----------------------------------------------------------
ReplacementPlan EngineResidency::begin_replacement(const ReplacementPlan& plan) {
  std::unique_lock lk(impl_->mtx);
  if (plan.epoch != impl_->authority.epoch) throw_error(ErrorCode::StaleEpoch, "replacement epoch stale");
  if (plan.authority != impl_->authority.authority) throw_error(ErrorCode::StaleAuthority, "replacement authority stale");
  EngineIncarnation* cand = impl_->find_inc(plan.candidate_incarnation);
  // Make-before-break: the candidate may be a warm non-current incarnation that
  // is RUNNING and READY; it does not need to hold current serving authority yet.
  if (cand == nullptr || cand->state.lifecycle != ProcessLifecycle::RUNNING) throw_error(ErrorCode::StaleIncarnation, "candidate not running");
  EngineIncarnation* oldi = impl_->find_inc(plan.old_incarnation);
  if (oldi == nullptr) throw_error(ErrorCode::UnknownIncarnation, "old incarnation unknown");
  if (cand->incarnation_id == oldi->incarnation_id) throw_error(ErrorCode::InvalidArgument, "candidate and old identical");
  ReplacementPlan stored = plan;
  stored.replacement_id = ReplacementId(++impl_->id_repl + 1);
  stored.replacement_generation = ReplacementGeneration(1);
  stored.phase = ReplacementPlan::Phase::PREPARING;
  stored.resource_overlap_available = (plan.overlap_bytes == 0);
  impl_->replacement_plans[stored.replacement_id] = stored;
  return stored;
}

ReplacementPlan EngineResidency::commit_cutover(ReplacementId rid) {
  std::unique_lock lk(impl_->mtx);
  auto it = impl_->replacement_plans.find(rid);
  if (it == impl_->replacement_plans.end()) throw_error(ErrorCode::NotFound, "replacement not found");
  ReplacementPlan& p = it->second;
  if (p.phase != ReplacementPlan::Phase::PREPARING) throw_error(ErrorCode::InvalidState, "not in preparing phase");
  EngineIncarnation* cand = impl_->find_inc(p.candidate_incarnation);
  if (cand == nullptr || cand->state.lifecycle != ProcessLifecycle::RUNNING) throw_error(ErrorCode::StaleIncarnation, "candidate not running");
  EngineIncarnation* oldi = impl_->find_inc(p.old_incarnation);
  // Make-before-break: old retains authority until cutover; fence its admission now.
  if (oldi != nullptr) {
    impl_->drains[oldi->incarnation_id].admission_fenced = true;
    oldi->state.activation = ActivationState::FENCED;
    oldi->is_current = false;
  }
  cand->is_current = true;
  cand->state.activation = ActivationState::STANDBY;
  cand->state.lifecycle = ProcessLifecycle::RUNNING;
  impl_->current_by_engine[cand->engine_id] = cand->incarnation_id;
  p.phase = ReplacementPlan::Phase::CUTOVER;
  return p;
}

ReplacementPlan EngineResidency::retire_old(ReplacementId rid) {
  std::unique_lock lk(impl_->mtx);
  auto it = impl_->replacement_plans.find(rid);
  if (it == impl_->replacement_plans.end()) throw_error(ErrorCode::NotFound, "replacement not found");
  ReplacementPlan& p = it->second;
  EngineIncarnation* oldi = impl_->find_inc(p.old_incarnation);
  if (oldi != nullptr) {
    oldi->state.lifecycle = ProcessLifecycle::RETIRED;
    oldi->state.health = HealthState::LOST;
    oldi->is_current = false;
  }
  p.phase = ReplacementPlan::Phase::COMPLETE;
  return p;
}

// ---- Invalidation ---------------------------------------------------------
void EngineResidency::invalidate_incarnation_components(EngineIncarnationId inc_id, const std::string& reason) {
  std::unique_lock lk(impl_->mtx);
  EngineIncarnation* inc = impl_->find_inc(inc_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  auto it = impl_->evidence.find(inc_id);
  if (it != impl_->evidence.end()) for (auto& e : it->second) { e.state = ComponentState::REVALIDATION_REQUIRED; e.invalidation_reason = reason; }
  inc->state.preparation = PreparationState::INVALIDATED;
  inc->readiness_generation = inc->readiness_generation.next();
  // Invalidate dependent serving uses? Existing admitted uses preserve their bindings.
}

// ---- Recovery economics ---------------------------------------------------
RecoveryCost EngineResidency::aggregate_recovery_cost(std::vector<CostComponent> components, RecoveryAction action, const std::string& aggregation_model) {
  std::unique_lock lk(impl_->mtx);
  if (impl_->cost_history.size() + components.size() > impl_->opts.max_cost_samples) throw_error(ErrorCode::BoundsExceeded, "max cost samples");
  impl_->cost_history.insert(impl_->cost_history.end(), components.begin(), components.end());
  return detail::aggregate_recovery_cost(std::move(components), action, aggregation_model);
}

CostComparison EngineResidency::compare_recovery_costs(const std::vector<RecoveryCost>& alternatives) {
  return detail::compare_recovery_costs(alternatives);
}

// ---- Persistence ----------------------------------------------------------
std::string EngineResidency::serialize() const {
  std::shared_lock lk(impl_->mtx);
  PersistentState s;
  s.version = kPersistenceVersion;
  s.coordinator_epoch = impl_->authority.epoch.raw();
  s.authority_generation = impl_->authority.authority.raw();
  s.registration_counter = impl_->authority.registration_counter;
  for (const auto& kv : impl_->engines) s.engines.push_back(kv.second);
  for (const auto& kv : impl_->profiles) s.profiles.push_back(kv.second);
  for (const auto& kv : impl_->incarnations) s.incarnations.push_back(kv.second);
  for (const auto& kv : impl_->evidence) for (const auto& e : kv.second) s.evidence.push_back(e);
  for (const auto& kv : impl_->replacement_plans) s.replacements.push_back(kv.second);
  s.cost_history = impl_->cost_history;
  return serialize_persistent_state(s);
}

void EngineResidency::load(const std::string& blob) {
  PersistentState s;
  std::string err;
  if (!deserialize_persistent_state(blob, s, err)) throw_error(ErrorCode::PersistenceCorrupt, err);
  if (s.version != kPersistenceVersion) throw_error(ErrorCode::PersistenceUnsupportedVersion, "unsupported version");
  std::unique_lock lk(impl_->mtx);
  // Validation completed in temporary structure; apply only now (conservative).
  impl_->engines.clear(); impl_->profiles.clear(); impl_->incarnations.clear();
  impl_->evidence.clear(); impl_->replacement_plans.clear(); impl_->cost_history.clear();
  impl_->authority.epoch = CoordinatorEpoch(s.coordinator_epoch > 0 ? s.coordinator_epoch : 1);
  impl_->authority.authority = AuthorityGeneration(s.authority_generation > 0 ? s.authority_generation : 1);
  impl_->authority.registration_counter = s.registration_counter;
  impl_->epoch_started = true;
  for (auto& e : s.engines) impl_->engines[e.engine_id] = e;
  for (auto& p : s.profiles) impl_->profiles[p.profile_id] = p;
  for (auto& inc : s.incarnations) {
    inc.state.recovery = RecoveryState::REVALIDATING;
    inc.is_current = false;
    inc.state.activation = ActivationState::INACTIVE;
    inc.state.preparation = PreparationState::REVALIDATION_REQUIRED;
    impl_->incarnations[inc.incarnation_id] = inc;
  }
  for (auto& e : s.evidence) {
    e.state = ComponentState::REVALIDATION_REQUIRED;
    impl_->evidence[e.incarnation_id].push_back(e);
  }
  for (auto& p : s.replacements) impl_->replacement_plans[p.replacement_id] = p;
  impl_->cost_history = std::move(s.cost_history);
  impl_->current_by_engine.clear();
  // Resume id counters above the recovered range so a fresh registration after
  // recovery can never collide with a restored identity.
  impl_->id_inc = 1000000; impl_->id_evid = 1000000; impl_->id_use = 1000000;
  impl_->id_attempt = 1000000; impl_->id_plan = 1000000; impl_->id_activation = 1000000;
  impl_->id_slot = 1000000; impl_->id_repl = 1000000; impl_->id_permit = 1000000;
}

// ---- Inspection -----------------------------------------------------------
std::vector<EngineDefinition> EngineResidency::engines() const {
  std::shared_lock lk(impl_->mtx);
  std::vector<EngineDefinition> out; out.reserve(impl_->engines.size());
  for (const auto& kv : impl_->engines) out.push_back(kv.second);
  return out;
}

std::vector<EngineIncarnation> EngineResidency::incarnations() const {
  std::shared_lock lk(impl_->mtx);
  std::vector<EngineIncarnation> out; out.reserve(impl_->incarnations.size());
  for (const auto& kv : impl_->incarnations) out.push_back(kv.second);
  return out;
}

std::vector<ReadinessProfile> EngineResidency::profiles() const {
  std::shared_lock lk(impl_->mtx);
  std::vector<ReadinessProfile> out; out.reserve(impl_->profiles.size());
  for (const auto& kv : impl_->profiles) out.push_back(kv.second);
  return out;
}

int EngineResidency::active_use_count() const {
  std::shared_lock lk(impl_->mtx);
  return static_cast<int>(impl_->active_use_count);
}

CoordinatorEpoch EngineResidency::current_epoch() const {
  std::shared_lock lk(impl_->mtx);
  return impl_->authority.epoch;
}

AuthorityGeneration EngineResidency::current_authority() const {
  std::shared_lock lk(impl_->mtx);
  return impl_->authority.authority;
}

ReadinessGeneration EngineResidency::current_readiness_generation(EngineIncarnationId inc_id) const {
  std::shared_lock lk(impl_->mtx);
  const EngineIncarnation* inc = impl_->find_incc(inc_id);
  if (inc == nullptr) throw_error(ErrorCode::UnknownIncarnation, "unknown incarnation");
  return inc->readiness_generation;
}

bool EngineResidency::incarnation_is_fenced(EngineIncarnationId inc_id) const {
  std::shared_lock lk(impl_->mtx);
  const EngineIncarnation* inc = impl_->find_incc(inc_id);
  if (inc == nullptr) return true;
  return inc->state.health == HealthState::FENCED || inc->state.registration == RegistrationState::FENCED ||
         inc->state.lifecycle == ProcessLifecycle::LOST || inc->state.lifecycle == ProcessLifecycle::RETIRED;
}

}  // namespace engine_residency
