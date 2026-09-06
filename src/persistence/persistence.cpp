#include "engine_residency/persistence.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "engine_residency/errors.hpp"

namespace engine_residency {
namespace {

constexpr std::uint64_t kMaxCount = 1'000'000;
constexpr std::uint64_t kMaxStr = 1'000'000;

std::uint64_t fnv1a(const std::uint8_t* d, std::size_t n) noexcept {
  std::uint64_t h = 1469598103934665603ULL;
  for (std::size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 1099511628211ULL; }
  return h;
}

class Writer {
 public:
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) buf_.push_back((std::uint8_t)((v >> (8*i)) & 0xFF)); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) buf_.push_back((std::uint8_t)((v >> (8*i)) & 0xFF)); }
  void str(const std::string& s) {
    if (s.size() > kMaxStr) throw DomainError(ErrorCode::BoundsExceeded, "string too long");
    u32((std::uint32_t)s.size());
    if (!s.empty()) buf_.insert(buf_.end(), s.begin(), s.end());
  }
  const std::string& bytes() const { return buf_; }
  void raw(const std::uint8_t* d, std::size_t n) { if (n) buf_.insert(buf_.end(), d, d + n); }
 private:
  std::string buf_;
};

class Reader {
 public:
  Reader(const std::uint8_t* d, std::size_t n) : d_(d), n_(n) {}
  bool u8(std::uint8_t& v) { if (pos_ + 1 > n_) return false; v = d_[pos_++]; return true; }
  bool u32(std::uint32_t& v) {
    if (pos_ + 4 > n_) return false; v = 0;
    for (int i = 0; i < 4; ++i) v |= (std::uint32_t)d_[pos_++] << (8*i); return true;
  }
  bool u64(std::uint64_t& v) {
    if (pos_ + 8 > n_) return false; v = 0;
    for (int i = 0; i < 8; ++i) v |= (std::uint64_t)d_[pos_++] << (8*i); return true;
  }
  bool strn(std::uint32_t len, std::string& out) {
    if (len > kMaxStr || pos_ + len > n_) return false;
    out.assign((const char*)(d_ + pos_), len); pos_ += len; return true;
  }
  std::size_t pos() const { return pos_; }
  std::size_t size() const { return n_; }
 private:
  const std::uint8_t* d_; std::size_t n_; std::size_t pos_{0};
};

// --- token helpers --------------------------------------------------------
#define W_STR(w,s) (w).str(s)
#define R_STR(r,out) do { std::uint32_t _l; if (!(r).u32(_l)) return false; if (!(r).strn(_l, (out))) return false; } while(0)
template <class T> void w_id(Writer& w, const T& v) { w.u64(v.raw()); }
template <class T> bool r_id(Reader& r, T& v) { std::uint64_t x; if (!r.u64(x)) return false; v = T(x); return true; }
void w_enum(Writer& w, int e) { w.u32((std::uint32_t)e); }
bool r_enum(Reader& r, int& e, int max) { std::uint32_t x; if (!r.u32(x)) return false; if (x > (std::uint32_t)max) return false; e = (int)x; return true; }

void w_comp(Writer& w, const CompatibilityKey& k) { w.str(k.ns); w.str(k.value); }
bool r_comp(Reader& r, CompatibilityKey& k) { R_STR(r, k.ns); R_STR(r, k.value); return true; }
void w_pid(Writer& w, const ProcessId& p) { w.u32(p.pid); w.u64(p.start_seconds); w.str(p.label); }
bool r_pid(Reader& r, ProcessId& p) { std::uint32_t x; if (!r.u32(x)) return false; p.pid = x; if (!r.u64(p.start_seconds)) return false; R_STR(r, p.label); return true; }

void w_state(Writer& w, const StateSet& s) {
  w_enum(w,(int)s.desired); w_enum(w,(int)s.observed); w_enum(w,(int)s.lifecycle);
  w_enum(w,(int)s.preparation); w_enum(w,(int)s.activation); w_enum(w,(int)s.health);
  w_enum(w,(int)s.recovery); w_enum(w,(int)s.registration); w_enum(w,(int)s.drain);
}
bool r_state(Reader& r, StateSet& s) {
  int e;
  if (!r_enum(r,e,(int)DesiredResidency::HOT)) return false; s.desired=(DesiredResidency)e;
  if (!r_enum(r,e,(int)DesiredResidency::HOT)) return false; s.observed=(DesiredResidency)e;
  if (!r_enum(r,e,(int)ProcessLifecycle::RETIRED)) return false; s.lifecycle=(ProcessLifecycle)e;
  if (!r_enum(r,e,(int)PreparationState::REVALIDATION_REQUIRED)) return false; s.preparation=(PreparationState)e;
  if (!r_enum(r,e,(int)ActivationState::FENCED)) return false; s.activation=(ActivationState)e;
  if (!r_enum(r,e,(int)HealthState::FENCED)) return false; s.health=(HealthState)e;
  if (!r_enum(r,e,(int)RecoveryState::AWAITING_AUTHORITY)) return false; s.recovery=(RecoveryState)e;
  if (!r_enum(r,e,(int)RegistrationState::REJECTED)) return false; s.registration=(RegistrationState)e;
  if (!r_enum(r,e,(int)DrainPhase::RETIRED)) return false; s.drain=(DrainPhase)e;
  return true;
}

void w_req(Writer& w, const ProfileRequirement& r) {
  w_enum(w,(int)r.category); w_enum(w,(int)r.kind); w_enum(w,(int)r.min_state);
  w.str(r.subject); w_comp(w,r.compatibility); w_enum(w,(int)r.generation_family); w.str(r.description);
}
bool r_req(Reader& r_, ProfileRequirement& o) {
  int e;
  if (!r_enum(r_,e,(int)ComponentCategory::WARMUP)) return false; o.category=(ComponentCategory)e;
  if (!r_enum(r_,e,(int)RequirementKind::UNSUPPORTED)) return false; o.kind=(RequirementKind)e;
  if (!r_enum(r_,e,(int)ComponentState::REVALIDATION_REQUIRED)) return false; o.min_state=(ComponentState)e;
  R_STR(r_, o.subject); if (!r_comp(r_, o.compatibility)) return false;
  if (!r_enum(r_,e,(int)GenerationFamily::COMPATIBILITY)) return false; o.generation_family=(GenerationFamily)e;
  R_STR(r_, o.description); return true;
}

void w_evid(Writer& w, const ComponentEvidence& e) {
  w_id(w,e.evidence_id); w_id(w,e.evidence_generation);
  w_enum(w,(int)e.category); w_enum(w,(int)e.state); w_enum(w,(int)e.provenance);
  w.str(e.subject); w.str(e.namespace_);
  w_id(w,e.engine_id); w_id(w,e.engine_generation); w_id(w,e.incarnation_id); w_id(w,e.incarnation_generation);
  w_id(w,e.worker_id); w_id(w,e.worker_boot); w_id(w,e.source_id); w_id(w,e.source_boot);
  w_id(w,e.backend_id); w_id(w,e.backend_generation); w_id(w,e.device_id); w_id(w,e.device_generation);
  w_id(w,e.device_context_generation); w.u32(e.device_ordinal);
  w_comp(w,e.compatibility);
  w_id(w,e.model_generation); w_id(w,e.adapter_generation); w_id(w,e.kv_capacity_generation);
  w_id(w,e.kv_state_generation); w_id(w,e.kernel_generation); w_id(w,e.graph_generation);
  w_id(w,e.artifact_generation); w_id(w,e.allocation_generation); w_id(w,e.dependency_generation);
  w_id(w,e.resource_claim_generation); w_id(w,e.warmup_generation);
  w_id(w,e.profile_id); w_id(w,e.profile_generation);
  w.u64(e.size_bytes); w.u64(e.elapsed_ns); w.u64(e.observed_at_ns); w.u64(e.max_age_ns);
  w.str(e.detail); w.str(e.invalidation_reason);
  w_id(w,e.coordinator_epoch);
}
bool r_evid(Reader& r, ComponentEvidence& e) {
  int x;
  if (!r_id(r,e.evidence_id)) return false; if (!r_id(r,e.evidence_generation)) return false;
  if (!r_enum(r,x,(int)ComponentCategory::WARMUP)) return false; e.category=(ComponentCategory)x;
  if (!r_enum(r,x,(int)ComponentState::REVALIDATION_REQUIRED)) return false; e.state=(ComponentState)x;
  if (!r_enum(r,x,(int)Provenance::UNKNOWN)) return false; e.provenance=(Provenance)x;
  R_STR(r, e.subject); R_STR(r, e.namespace_);
  if (!r_id(r,e.engine_id)) return false; if (!r_id(r,e.engine_generation)) return false;
  if (!r_id(r,e.incarnation_id)) return false; if (!r_id(r,e.incarnation_generation)) return false;
  if (!r_id(r,e.worker_id)) return false; if (!r_id(r,e.worker_boot)) return false;
  if (!r_id(r,e.source_id)) return false; if (!r_id(r,e.source_boot)) return false;
  if (!r_id(r,e.backend_id)) return false; if (!r_id(r,e.backend_generation)) return false;
  if (!r_id(r,e.device_id)) return false; if (!r_id(r,e.device_generation)) return false;
  if (!r_id(r,e.device_context_generation)) return false; if (!r.u32(e.device_ordinal)) return false;
  if (!r_comp(r,e.compatibility)) return false;
  if (!r_id(r,e.model_generation)) return false; if (!r_id(r,e.adapter_generation)) return false;
  if (!r_id(r,e.kv_capacity_generation)) return false; if (!r_id(r,e.kv_state_generation)) return false;
  if (!r_id(r,e.kernel_generation)) return false; if (!r_id(r,e.graph_generation)) return false;
  if (!r_id(r,e.artifact_generation)) return false; if (!r_id(r,e.allocation_generation)) return false;
  if (!r_id(r,e.dependency_generation)) return false; if (!r_id(r,e.resource_claim_generation)) return false;
  if (!r_id(r,e.warmup_generation)) return false; if (!r_id(r,e.profile_id)) return false; if (!r_id(r,e.profile_generation)) return false;
  if (!r.u64(e.size_bytes)) return false; if (!r.u64(e.elapsed_ns)) return false;
  if (!r.u64(e.observed_at_ns)) return false; if (!r.u64(e.max_age_ns)) return false;
  R_STR(r, e.detail); R_STR(r, e.invalidation_reason);
  if (!r_id(r,e.coordinator_epoch)) return false;
  return true;
}

}  // namespace

void w_engine(Writer& w, const EngineDefinition& e) {
  w_id(w,e.engine_id); w_id(w,e.engine_generation); w_id(w,e.config_generation);
  w.str(e.name); w_enum(w,(int)e.role);
  w_id(w,e.backend_id); w_id(w,e.backend_generation); w.u8(e.backend_specific?1:0);
  w_id(w,e.model_id); w_id(w,e.model_generation); w.str(e.model_name); w.u64(e.model_bytes);
  w_id(w,e.adapter_generation); w.u8(e.requires_adapter?1:0);
  w_id(w,e.device_id); w_id(w,e.device_generation); w.u32(e.device_ordinal); w.u8(e.device_specific?1:0);
  w.u64(e.workspace_bytes); w.u64(e.kv_capacity_entries);
  w.str(e.kernel_key); w_id(w,e.kernel_generation); w.u8(e.kernel_required?1:0);
  w.u8(e.graph_required?1:0); w.u8(e.graph_optional_fallback?1:0); w_id(w,e.graph_generation);
  w.str(e.tokenizer_key); w_id(w,e.tokenizer_generation); w.u8(e.tokenizer_required?1:0);
  w_comp(w,e.compatibility); w.u8(e.compatibility_required?1:0);
  w.str(e.preparation_policy); w.u8(e.warmup_required?1:0);
  w.u8(e.standby_eligible?1:0); w.u32(e.standby_target);
  w.u32(e.max_concurrency);
  w_id(w,e.resource_claim_generation); w.str(e.resource_claim_ref);
  w.u32((std::uint32_t)e.dependency_refs.size()); for (const auto& s : e.dependency_refs) w.str(s);
  w_id(w,e.dependency_generation); w_enum(w,(int)e.provenance);
  w.u32((std::uint32_t)e.profile_ids.size()); for (const auto& p : e.profile_ids) w_id(w,p);
}
bool r_engine(Reader& r, EngineDefinition& e) {
  int x;
  if (!r_id(r,e.engine_id)) return false; if (!r_id(r,e.engine_generation)) return false; if (!r_id(r,e.config_generation)) return false;
  R_STR(r, e.name); if (!r_enum(r,x,(int)ServingRole::UNKNOWN)) return false; e.role=(ServingRole)x;
  if (!r_id(r,e.backend_id)) return false; if (!r_id(r,e.backend_generation)) return false; std::uint8_t b; if(!r.u8(b)) return false; e.backend_specific=b!=0;
  if (!r_id(r,e.model_id)) return false; if (!r_id(r,e.model_generation)) return false; R_STR(r, e.model_name); if (!r.u64(e.model_bytes)) return false;
  if (!r_id(r,e.adapter_generation)) return false; if(!r.u8(b)) return false; e.requires_adapter=b!=0;
  if (!r_id(r,e.device_id)) return false; if (!r_id(r,e.device_generation)) return false; if (!r.u32(e.device_ordinal)) return false; if(!r.u8(b)) return false; e.device_specific=b!=0;
  if (!r.u64(e.workspace_bytes)) return false; if (!r.u64(e.kv_capacity_entries)) return false;
  R_STR(r, e.kernel_key); if (!r_id(r,e.kernel_generation)) return false; if(!r.u8(b)) return false; e.kernel_required=b!=0;
  if(!r.u8(b)) return false; e.graph_required=b!=0; if(!r.u8(b)) return false; e.graph_optional_fallback=b!=0; if (!r_id(r,e.graph_generation)) return false;
  R_STR(r, e.tokenizer_key); if (!r_id(r,e.tokenizer_generation)) return false; if(!r.u8(b)) return false; e.tokenizer_required=b!=0;
  if (!r_comp(r,e.compatibility)) return false; if(!r.u8(b)) return false; e.compatibility_required=b!=0;
  R_STR(r, e.preparation_policy); if(!r.u8(b)) return false; e.warmup_required=b!=0;
  if(!r.u8(b)) return false; e.standby_eligible=b!=0; if (!r.u32(e.standby_target)) return false;
  if (!r.u32(e.max_concurrency)) return false;
  if (!r_id(r,e.resource_claim_generation)) return false; R_STR(r, e.resource_claim_ref);
  std::uint32_t n; if (!r.u32(n)) return false; if (n > kMaxCount) return false; for (std::uint32_t i=0;i<n;++i){ std::string s; R_STR(r,s); e.dependency_refs.push_back(std::move(s)); }
  if (!r_id(r,e.dependency_generation)) return false; if (!r_enum(r,x,(int)Provenance::UNKNOWN)) return false; e.provenance=(Provenance)x;
  if (!r.u32(n)) return false; if (n > kMaxCount) return false; for (std::uint32_t i=0;i<n;++i){ ReadinessProfileId p; if(!r_id(r,p)) return false; e.profile_ids.push_back(p); }
  return true;
}

void w_profile(Writer& w, const ReadinessProfile& p) {
  w_id(w,p.profile_id); w_id(w,p.profile_generation); w_id(w,p.engine_id); w_id(w,p.engine_generation);
  w.str(p.name); w_enum(w,(int)p.role);
  w.u32(p.max_batch); w.u32(p.max_seq_len); w.str(p.dtype); w.str(p.layout);
  w.u64(p.workspace_bytes); w.u64(p.model_bytes); w.u64(p.kv_capacity_entries);
  w.u8(p.graph_required?1:0); w.u8(p.graph_optional_fallback?1:0);
  w.u8(p.warmup_required?1:0); w.u32(p.device_ordinal); w.u8(p.device_specific?1:0); w.u8(p.profile_required?1:0);
  w_id(w,p.policy_generation); w_id(w,p.dependency_generation);
  w.u32((std::uint32_t)p.requirements.size()); for (const auto& r : p.requirements) w_req(w,r);
  w_comp(w,p.compatibility);
}
bool r_profile(Reader& r, ReadinessProfile& p) {
  int x; std::uint8_t b;
  if (!r_id(r,p.profile_id)) return false; if (!r_id(r,p.profile_generation)) return false;
  if (!r_id(r,p.engine_id)) return false; if (!r_id(r,p.engine_generation)) return false;
  R_STR(r, p.name); if (!r_enum(r,x,(int)ServingRole::UNKNOWN)) return false; p.role=(ServingRole)x;
  if (!r.u32(p.max_batch)) return false; if (!r.u32(p.max_seq_len)) return false; R_STR(r, p.dtype); R_STR(r, p.layout);
  if (!r.u64(p.workspace_bytes)) return false; if (!r.u64(p.model_bytes)) return false; if (!r.u64(p.kv_capacity_entries)) return false;
  if(!r.u8(b)) return false; p.graph_required=b!=0; if(!r.u8(b)) return false; p.graph_optional_fallback=b!=0;
  if(!r.u8(b)) return false; p.warmup_required=b!=0; if (!r.u32(p.device_ordinal)) return false; if(!r.u8(b)) return false; p.device_specific=b!=0; if(!r.u8(b)) return false; p.profile_required=b!=0;
  if (!r_id(r,p.policy_generation)) return false; if (!r_id(r,p.dependency_generation)) return false;
  std::uint32_t n; if (!r.u32(n)) return false; if (n > kMaxCount) return false; for (std::uint32_t i=0;i<n;++i){ ProfileRequirement q; if(!r_req(r,q)) return false; p.requirements.push_back(std::move(q)); }
  if (!r_comp(r,p.compatibility)) return false;
  return true;
}

void w_inc(Writer& w, const EngineIncarnation& i) {
  w_id(w,i.incarnation_id); w_id(w,i.incarnation_generation); w_id(w,i.engine_id); w_id(w,i.engine_generation); w_id(w,i.config_generation);
  w_id(w,i.worker_id); w_id(w,i.worker_boot); w_id(w,i.source_id); w_id(w,i.source_boot);
  w_id(w,i.coordinator_epoch); w_id(w,i.authority_generation); w_id(w,i.registration_permit);
  w_id(w,i.backend_id); w_id(w,i.backend_generation); w_id(w,i.device_id); w_id(w,i.device_generation); w_id(w,i.device_context_generation); w.u32(i.device_ordinal);
  w_pid(w,i.process);
  w_enum(w,(int)i.registration); w_state(w,i.state);
  w_id(w,i.readiness_generation); w.u8(i.is_current?1:0);
  w.u64(i.last_activity_ns); w.str(i.nfc);
}
bool r_inc(Reader& r, EngineIncarnation& i) {
  int x; std::uint8_t b;
  if (!r_id(r,i.incarnation_id)) return false; if (!r_id(r,i.incarnation_generation)) return false;
  if (!r_id(r,i.engine_id)) return false; if (!r_id(r,i.engine_generation)) return false; if (!r_id(r,i.config_generation)) return false;
  if (!r_id(r,i.worker_id)) return false; if (!r_id(r,i.worker_boot)) return false; if (!r_id(r,i.source_id)) return false; if (!r_id(r,i.source_boot)) return false;
  if (!r_id(r,i.coordinator_epoch)) return false; if (!r_id(r,i.authority_generation)) return false; if (!r_id(r,i.registration_permit)) return false;
  if (!r_id(r,i.backend_id)) return false; if (!r_id(r,i.backend_generation)) return false;
  if (!r_id(r,i.device_id)) return false; if (!r_id(r,i.device_generation)) return false; if (!r_id(r,i.device_context_generation)) return false; if (!r.u32(i.device_ordinal)) return false;
  if (!r_pid(r,i.process)) return false;
  if (!r_enum(r,x,(int)RegistrationState::REJECTED)) return false; i.registration=(RegistrationState)x;
  if (!r_state(r,i.state)) return false;
  if (!r_id(r,i.readiness_generation)) return false; if(!r.u8(b)) return false; i.is_current=b!=0;
  if (!r.u64(i.last_activity_ns)) return false; R_STR(r, i.nfc);
  return true;
}

void w_repl(Writer& w, const ReplacementPlan& p) {
  w_id(w,p.replacement_id); w_id(w,p.replacement_generation); w_id(w,p.engine_id); w_id(w,p.engine_generation);
  w_id(w,p.old_incarnation); w_id(w,p.old_incarnation_generation);
  w_id(w,p.candidate_incarnation); w_id(w,p.candidate_incarnation_generation);
  w_id(w,p.epoch); w_id(w,p.authority);
  w_enum(w,(int)p.phase); w.u8(p.make_before_break?1:0); w.u8(p.rollback_required?1:0); w.str(p.detail);
  w.u64(p.overlap_bytes); w.u8(p.resource_overlap_available?1:0);
}
bool r_repl(Reader& r, ReplacementPlan& p) {
  int x; std::uint8_t b;
  if (!r_id(r,p.replacement_id)) return false; if (!r_id(r,p.replacement_generation)) return false;
  if (!r_id(r,p.engine_id)) return false; if (!r_id(r,p.engine_generation)) return false;
  if (!r_id(r,p.old_incarnation)) return false; if (!r_id(r,p.old_incarnation_generation)) return false;
  if (!r_id(r,p.candidate_incarnation)) return false; if (!r_id(r,p.candidate_incarnation_generation)) return false;
  if (!r_id(r,p.epoch)) return false; if (!r_id(r,p.authority)) return false;
  if (!r_enum(r,x,(int)ReplacementPlan::Phase::COMPLETE)) return false; p.phase=(ReplacementPlan::Phase)x;
  if(!r.u8(b)) return false; p.make_before_break=b!=0; if(!r.u8(b)) return false; p.rollback_required=b!=0; R_STR(r, p.detail);
  if (!r.u64(p.overlap_bytes)) return false; if(!r.u8(b)) return false; p.resource_overlap_available=b!=0;
  return true;
}

void w_cost(Writer& w, const CostComponent& c) {
  w.str(c.name); w.u64(c.estimated_ns); w.u64(c.measured_ns); w_enum(w,(int)c.provenance); w.u64(c.bytes); w.str(c.unit);
}
bool r_cost(Reader& r, CostComponent& c) {
  int x;
  R_STR(r, c.name); if (!r.u64(c.estimated_ns)) return false; if (!r.u64(c.measured_ns)) return false;
  if (!r_enum(r,x,(int)Provenance::UNKNOWN)) return false; c.provenance=(Provenance)x; if (!r.u64(c.bytes)) return false; R_STR(r, c.unit);
  if (std::isnan((double)c.measured_ns) || std::isinf((double)c.measured_ns)) return false;
  return true;
}

std::string serialize_persistent_state(const PersistentState& state) {
  Writer w;
  w.u32(kPersistenceMagic);
  w.u32(kPersistenceVersion);
  w.u64(state.coordinator_epoch); w.u64(state.authority_generation); w.u64(state.registration_counter);
  w.u32((std::uint32_t)state.engines.size()); for (const auto& e : state.engines) w_engine(w,e);
  w.u32((std::uint32_t)state.profiles.size()); for (const auto& p : state.profiles) w_profile(w,p);
  w.u32((std::uint32_t)state.incarnations.size()); for (const auto& i : state.incarnations) w_inc(w,i);
  w.u32((std::uint32_t)state.evidence.size()); for (const auto& e : state.evidence) w_evid(w,e);
  w.u32((std::uint32_t)state.replacements.size()); for (const auto& p : state.replacements) w_repl(w,p);
  w.u32((std::uint32_t)state.cost_history.size()); for (const auto& c : state.cost_history) w_cost(w,c);
  const std::string& body = w.bytes();
  std::uint64_t chk = fnv1a((const std::uint8_t*)body.data(), body.size());
  std::string out = body;
  for (int i = 0; i < 8; ++i) out.push_back((char)((chk >> (8*i)) & 0xFF));
  return out;
}

bool deserialize_persistent_state(const std::string& blob, PersistentState& out, std::string& error) {
  if (blob.size() < 8 + 8) { error = "blob too short"; return false; }
  if (blob.size() > kMaxSerializedBytes) { error = "blob oversized"; return false; }
  std::uint64_t chk = 0;
  for (int i = 0; i < 8; ++i) chk |= (std::uint64_t)(std::uint8_t)blob[blob.size()-8+i] << (8*i);
  const std::string& body = blob.substr(0, blob.size()-8);
  if (fnv1a((const std::uint8_t*)body.data(), body.size()) != chk) { error = "checksum mismatch"; return false; }
  Reader r((const std::uint8_t*)body.data(), body.size());
  auto bad = [&](const char* m) { error = m; return false; };
  std::uint32_t magic; if (!r.u32(magic) || magic != kPersistenceMagic) { error = "bad magic"; return false; }
  std::uint32_t version; if (!r.u32(version) || version != kPersistenceVersion) { error = "unsupported version"; return false; }
  PersistentState s; s.version = version;
  if (!r.u64(s.coordinator_epoch)) return bad("reading epoch");
  if (!r.u64(s.authority_generation)) return bad("reading authority");
  if (!r.u64(s.registration_counter)) return bad("reading reg counter");
  std::uint32_t n;
  if (!r.u32(n) || n > kMaxCount) return bad("engine count"); for (std::uint32_t i=0;i<n;++i){ EngineDefinition e; if(!r_engine(r,e)) return bad("engine record"); if (s.engines.size() >= kMaxCount) return bad("engine overflow"); s.engines.push_back(std::move(e)); }
  if (!r.u32(n) || n > kMaxCount) return bad("profile count"); for (std::uint32_t i=0;i<n;++i){ ReadinessProfile p; if(!r_profile(r,p)) return bad("profile record"); s.profiles.push_back(std::move(p)); }
  if (!r.u32(n) || n > kMaxCount) return bad("incarnation count"); for (std::uint32_t i=0;i<n;++i){ EngineIncarnation in; if(!r_inc(r,in)) return bad("incarnation record"); s.incarnations.push_back(std::move(in)); }
  if (!r.u32(n) || n > kMaxCount) return bad("evidence count"); for (std::uint32_t i=0;i<n;++i){ ComponentEvidence e; if(!r_evid(r,e)) return bad("evidence record"); s.evidence.push_back(std::move(e)); }
  if (!r.u32(n) || n > kMaxCount) return bad("replacement count"); for (std::uint32_t i=0;i<n;++i){ ReplacementPlan p; if(!r_repl(r,p)) return bad("replacement record"); s.replacements.push_back(std::move(p)); }
  if (!r.u32(n) || n > kMaxCount) return bad("cost count"); for (std::uint32_t i=0;i<n;++i){ CostComponent c; if(!r_cost(r,c)) return bad("cost record"); s.cost_history.push_back(std::move(c)); }
  // Integrity: no trailing garbage (all bytes consumed).
  if (r.pos() != r.size()) return bad("trailing garbage");
  out = std::move(s);
  return true;
}
}  // namespace engine_residency