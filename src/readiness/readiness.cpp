#include "detail/internal.hpp"

#include <algorithm>
#include <cstdint>

namespace engine_residency {
namespace detail {

const ComponentEvidence* best_evidence(
    const std::vector<ComponentEvidence>& evidence,
    DurationNs now_ns,
    ComponentCategory category,
    const std::string& subject,
    const std::string& evidence_namespace) {
  (void)now_ns;
  const ComponentEvidence* best = nullptr;
  for (const auto& e : evidence) {
    if (e.category != category) continue;
    if (!subject.empty() && e.subject != subject) continue;
    if (!evidence_namespace.empty() && e.namespace_ != evidence_namespace) continue;
    if (best == nullptr || e.evidence_generation > best->evidence_generation) {
      best = &e;
    } else if (best != nullptr && e.evidence_generation == best->evidence_generation &&
               e.observed_at_ns > best->observed_at_ns) {
      best = &e;
    }
  }
  return best;
}

namespace {
bool state_satisfies(ComponentState actual, ComponentState required) {
  // Ordered strength: ABSENT < DISCOVERED < AVAILABLE < LOADING < BOUND < VERIFIED.
  // FAILED/STALE/UNKNOWN/REVALIDATION_REQUIRED never satisfy anything.
  auto rank = [](ComponentState s) -> int {
    switch (s) {
      case ComponentState::ABSENT: return 0;
      case ComponentState::DISCOVERED: return 1;
      case ComponentState::AVAILABLE: return 2;
      case ComponentState::LOADING: return 3;
      case ComponentState::BOUND: return 4;
      case ComponentState::VERIFIED: return 5;
      case ComponentState::FAILED: return -1;
      case ComponentState::STALE: return -2;
      case ComponentState::UNKNOWN: return -3;
      case ComponentState::REVALIDATION_REQUIRED: return -4;
    }
    return -5;
  };
  return rank(actual) >= rank(required) && rank(required) >= 0;
}
}  // namespace

ReadinessResult evaluate_readiness(const EngineDefinition& engine,
                                   const ReadinessProfile& profile,
                                   const EngineIncarnation& inc,
                                   const std::vector<ComponentEvidence>& evidence,
                                   DurationNs now_ns) {
  ReadinessResult result;
  result.engine_id = engine.engine_id;
  result.engine_generation = engine.engine_generation;
  result.incarnation_id = inc.incarnation_id;
  result.incarnation_generation = inc.incarnation_generation;
  result.profile_id = profile.profile_id;
  result.profile_generation = profile.profile_generation;
  result.readiness_generation = inc.readiness_generation;
  result.coherent_snapshot = true;  // evidence set is a coherent runtime snapshot.

  bool has_required_missing = false;
  bool has_required_stale = false;
  bool has_incompatible = false;
  bool fallback_used = false;

  for (const auto& req : profile.requirements) {
    RequirementOutcome o;
    o.category = req.category;
    o.subject = req.subject.empty() ? std::string(to_string(req.category)) : req.subject;
    o.kind = req.kind;
    o.min_state = req.min_state;
    o.actual_state = ComponentState::ABSENT;
    o.missing = false;
    o.stale = false;
    o.incompatible = false;

    if (req.kind == RequirementKind::UNSUPPORTED) {
      o.reason = "unsupported requirement for this profile";
      has_incompatible = true;
      o.incompatible = true;
      result.incompatible.push_back(o.subject);
      result.requirement_outcomes.push_back(std::move(o));
      continue;
    }

    const ComponentEvidence* e = best_evidence(evidence, now_ns, req.category, req.subject, "");
    if (e == nullptr) {
      o.reason = "missing evidence for required component";
      if (req.kind != RequirementKind::OPTIONAL) {
        o.missing = true;
        o.reason = "missing prerequisite: " + o.subject;
        has_required_missing = true;
        result.missing.push_back(o.subject);
      } else {
        // Optional component absent: fine, does not block, but note it.
        o.reason = "optional optimization absent";
        o.satisfied = true;  // treated as permitted-absent.
      }
      result.requirement_outcomes.push_back(std::move(o));
      continue;
    }

    o.actual_state = e->state;

    if (!state_satisfies(e->state, req.min_state)) {
      if (req.kind != RequirementKind::OPTIONAL) {
        o.missing = true;
        o.reason = std::string(to_string(e->state)) + " does not satisfy required " +
                   std::string(to_string(req.min_state));
        has_required_missing = true;
        result.missing.push_back(o.subject);
      } else {
        o.reason = "optional optimization not at required state";
        o.satisfied = true;
      }
      result.requirement_outcomes.push_back(std::move(o));
      continue;
    }

    // Compatibility cross-check.
    if (req.compatibility.is_valid() && req.compatibility != e->compatibility) {
      o.incompatible = true;
      o.reason = "compatibility mismatch";
      has_incompatible = true;
      result.incompatible.push_back(o.subject);
      result.requirement_outcomes.push_back(std::move(o));
      continue;
    }

    // Generation family cross-check against the engine definition.
    bool gen_mismatch = false;
    switch (req.generation_family) {
      case GenerationFamily::MODEL: gen_mismatch = e->model_generation != engine.model_generation; break;
      case GenerationFamily::ADAPTER: gen_mismatch = e->adapter_generation != engine.adapter_generation; break;
      // KV capacity generation must be valid (bound to a provisioned claim).
      case GenerationFamily::KV_CAPACITY: gen_mismatch = !e->kv_capacity_generation.is_valid(); break;
      case GenerationFamily::KERNEL: gen_mismatch = e->kernel_generation != engine.kernel_generation; break;
      case GenerationFamily::GRAPH: gen_mismatch = e->graph_generation != engine.graph_generation; break;
      case GenerationFamily::ALLOCATION: gen_mismatch = false; break;
      case GenerationFamily::RESOURCE_CLAIM: gen_mismatch = false; break;
      case GenerationFamily::DEPENDENCY: gen_mismatch = e->dependency_generation != engine.dependency_generation; break;
      case GenerationFamily::COMPATIBILITY: gen_mismatch = req.compatibility.is_valid() && req.compatibility != e->compatibility; break;
      case GenerationFamily::NONE: gen_mismatch = false; break;
    }
    if (req.generation_family != GenerationFamily::NONE && gen_mismatch) {
      o.incompatible = true;
      o.reason = "generation mismatch with engine definition";
      has_incompatible = true;
      result.incompatible.push_back(o.subject);
      result.requirement_outcomes.push_back(std::move(o));
      continue;
    }

    // Freshness.
    if (!e->is_fresh(now_ns)) {
      if (req.kind != RequirementKind::OPTIONAL) {
        o.stale = true;
        o.reason = "evidence exceeding freshness window";
        has_required_stale = true;
        result.stale_evidence.push_back(o.subject);
        result.requirement_outcomes.push_back(std::move(o));
        continue;
      }
    }

    o.satisfied = true;
    result.satisfied.push_back(o.subject);
    if (req.kind == RequirementKind::PERMITTED_FALLBACK) {
      o.fallback_used = true;
      fallback_used = true;
      result.selected_fallback = o.subject;
    }
    result.requirement_outcomes.push_back(std::move(o));
  }

  // Headroom from KV capacity evidence.
  {
    const ComponentEvidence* kv = best_evidence(evidence, now_ns, ComponentCategory::KV_CAPACITY, "", "");
    if (kv != nullptr) {
      result.headroom_kv_entries = kv->size_bytes;  // reinterpret as capacity entries (see note).
    }
  }

  if (has_incompatible) {
    result.outcome = ReadinessOutcome::BLOCKED;
    result.detail = "incompatible requirement(s) present";
  } else if (has_required_missing) {
    result.outcome = ReadinessOutcome::BLOCKED;
    result.detail = "required prerequisite(s) missing";
  } else if (has_required_stale) {
    result.outcome = ReadinessOutcome::STALE;
    result.detail = "required evidence is stale";
  } else if (fallback_used) {
    result.outcome = ReadinessOutcome::DEGRADED;
    result.detail = "serving under permitted fallback";
  } else {
    result.outcome = ReadinessOutcome::READY;
    result.detail = "all required prerequisites satisfied";
  }

  if (result.outcome != ReadinessOutcome::READY &&
      result.outcome != ReadinessOutcome::DEGRADED) {
    // Derive required preparation actions for missing/incompatible components.
    result.required_actions.push_back("ensure " + std::string("required components"));
  }

  return result;
}

}  // namespace detail
}  // namespace engine_residency
