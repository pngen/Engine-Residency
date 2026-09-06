#ifndef ENGINE_RESIDENCY_RUNTIME_HPP
#define ENGINE_RESIDENCY_RUNTIME_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine_residency/activation.hpp"
#include "engine_residency/authority.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/components.hpp"
#include "engine_residency/drain.hpp"
#include "engine_residency/economics.hpp"
#include "engine_residency/engine.hpp"
#include "engine_residency/options.hpp"
#include "engine_residency/preparation.hpp"
#include "engine_residency/profiles.hpp"
#include "engine_residency/readiness.hpp"
#include "engine_residency/replacement.hpp"
#include "engine_residency/serving_use.hpp"
#include "engine_residency/standby.hpp"

namespace engine_residency {

class EngineResidencyImpl;

// ---------------------------------------------------------------------------
// Engine Residency runtime.
//
// This class owns the engine-residency state model at the engine-incarnation
// boundary. It is the backend-neutral core: it contains no TCP, no CUDA, and no
// request scheduling. It exposes the operations that enforce authority,
// preparedness, serving-use fencing, standby accounting, activation, drain,
// replacement, conservative recovery and recovery economics.
//
// All mutating operations acquire internal state locks. The runtime may be used
// ---
// (abridged; full body in runtime.cpp)
// ---------------------------------------------------------------------------
class EngineResidency {
 public:
  // A clock of a duration that is used for freshness. Ownership stays with the
  // caller; the runtime never deletes it.
  explicit EngineResidency(Clock& clock, RuntimeOptions options = RuntimeOptions());
  ~EngineResidency();
  EngineResidency(const EngineResidency&) = delete;
  EngineResidency& operator=(const EngineResidency&) = delete;

  // --- Engine definitions and profiles -------------------------------------
  // Registers a logical engine definition. Assigns a definition generation if
  // one is not supplied.
  EngineDefinition define_engine(EngineDefinition def);
  // Advance a configuration generation for an engine (supersedes old readiness).
  void update_engine_config(EngineId engine_id,
                            EngineConfigGeneration new_config_generation,
                            const std::string& reason);
  // Registers a readiness profile against an engine.
  ReadinessProfile define_profile(ReadinessProfile profile);

  // --- Registration / authority --------------------------------------------
  // Coordinator-issued registration permit. Only fresh boots may request one;
  // an arbitrary unseen boot is not accepted merely by sending HELLO.
  RegistrationPermit issue_registration_permit(WorkerId worker_id, WorkerBootId worker_boot,
                                               SourceId source_id, SourceBootId source_boot);
  // Registers a process incarnation. Requires a current, unconsumed permit that
  // matches the worker/boot. Returns the incarnation with assigned identity and
  // generation. A replayed REGISTER under a fenced boot is rejected.
  EngineIncarnation register_incarnation(EngineIncarnation inc, RegistrationPermit permit);
  // Transitions an incarnation to RUNNING (worker confirmed process start).
  void mark_running(EngineIncarnationId incarnation_id);
  // After a coordinator restart, re-authorizes a survivor that has re-verified its
  // current physical bindings under the current epoch.
  void revalidate(EngineIncarnationId incarnation_id);
  // Marks an incarnation (and all worker boots) fenced. Old traffic remains
  // rejected.
  void fence_worker(WorkerId worker_id, WorkerBootId worker_boot, const std::string& reason);
  // Advance the coordinator epoch durably (used after a coordinator restart).
  CoordinatorEpoch advance_epoch();

  // --- Component evidence ---------------------------------------------------
  // Publishes component evidence for an incarnation. Binds it to the current
  // authority. Rejects evidence from a fenced/old boot or old epoch.
  ComponentEvidence publish_component(ComponentEvidence ev);

  // --- Readiness ------------------------------------------------------------
  // Deterministically evaluates the current readiness of an incarnation for a
  // profile, producing an inspectable result.
  ReadinessResult evaluate_readiness(EngineIncarnationId incarnation_id, ReadinessProfileId profile_id);

  // --- Preparation ----------------------------------------------------------
  PreparationPlan create_preparation_plan(EngineIncarnationId incarnation_id,
                                          ReadinessProfileId profile_id,
                                          DesiredResidency target_residency);
  PreparationAttempt begin_preparation(PreparationPlan plan);
  // Advance one step of a preparation attempt. May be called repeatedly; each
  // call advances the attempt by one step. Returns the attempt state.
  PreparationAttempt step_preparation(PreparationAttemptId attempt_id);
  // Cancels a preparation attempt before commit; fences it and prevents READY
  // publication. Cleans attempt-owned resources.
  void cancel_preparation(PreparationAttemptId attempt_id, const std::string& reason);
  // Publishes an attempt as PREPARED and (if the profile is fully satisfied)
  // advances readiness. Rejects stale authority or cancelled attempts.
  PreparationAttempt complete_preparation(PreparationAttemptId attempt_id);
  // Rolls back an attempt, releasing attempt-owned resources and never clearing
  // newer state.
  void rollback_preparation(PreparationAttemptId attempt_id, const std::string& reason);

  // --- Standby --------------------------------------------------------------
  // Deterministic local reconciliation against an explicitly supplied policy.
  StandbyAccounting reconcile_standby(const StandbyPoolPolicy& policy);

  // --- Activation -----------------------------------------------------------
  // Local standby-to-active transition, compare-and-commit. Rejects stale or
  // competing activation (for an exclusive slot at most one is current).
  ActivationRecord activate(const ActivationRequest& request);

  // --- Serving use ----------------------------------------------------------
  // Generation-bound acquisition. Atomically validates incarnation currency,
  // profile, readiness, activation, drain, capacity and generations.
  ServingUseToken acquire_serving_use(const ServingUseToken& wanted,
                                      const std::string& caller);
  // Releases a serving use exactly once. Old incarnation release is rejected.
  void release_serving_use(const ServingUseToken& token, WorkOutcome outcome,
                           const std::string& detail);

  // --- Drain ----------------------------------------------------------------
  void request_drain(EngineIncarnationId incarnation_id);
  void acknowledge_backend_cleanup(EngineIncarnationId incarnation_id);
  DrainState drain_state(EngineIncarnationId incarnation_id) const;

  // --- Replacement ----------------------------------------------------------
  ReplacementPlan begin_replacement(const ReplacementPlan& plan);
  ReplacementPlan commit_cutover(ReplacementId replacement_id);
  ReplacementPlan retire_old(ReplacementId replacement_id);

  // --- Invalidation ---------------------------------------------------------
  void invalidate_incarnation_components(EngineIncarnationId incarnation_id,
                                         const std::string& reason);

  // --- Recovery economics ---------------------------------------------------
  RecoveryCost aggregate_recovery_cost(std::vector<CostComponent> components,
                                       RecoveryAction action,
                                       const std::string& aggregation_model);
  CostComparison compare_recovery_costs(const std::vector<RecoveryCost>& alternatives);

  // --- Persistence ----------------------------------------------------------
  std::string serialize() const;
  // Replaces live state ONLY after the serialized blob validates completely.
  // Corrupt input leaves the existing runtime unchanged.
  void load(const std::string& blob);

  // --- Inspection -----------------------------------------------------------
  std::vector<EngineDefinition> engines() const;
  std::vector<EngineIncarnation> incarnations() const;
  std::vector<ReadinessProfile> profiles() const;
  int active_use_count() const;
  CoordinatorEpoch current_epoch() const;
  AuthorityGeneration current_authority() const;
  ReadinessGeneration current_readiness_generation(EngineIncarnationId incarnation_id) const;
  bool incarnation_is_fenced(EngineIncarnationId incarnation_id) const;

 private:
  std::unique_ptr<EngineResidencyImpl> impl_;
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_RUNTIME_HPP
