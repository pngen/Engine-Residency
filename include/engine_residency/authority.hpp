#ifndef ENGINE_RESIDENCY_AUTHORITY_HPP
#define ENGINE_RESIDENCY_AUTHORITY_HPP

#include <cstdint>
#include <string>

#include "engine_residency/identities.hpp"

namespace engine_residency {

// ---------------------------------------------------------------------------
// Control-plane authority model.
//
// Reference model: one authoritative coordinator per managed domain, whose
// epoch advances monotonically and durably. Workers/sessions are admitted
// explicitly. Current authority is required before any mutation. A disconnected
// worker cannot obtain NEW serving authorization. Old-coordinator traffic
// cannot authorize fresh work after fencing.
//
// This is a single-coordinator reference model. It does NOT claim distributed
// consensus or partition-safe global leadership, and generation fencing is NOT
// cryptographic authentication.
// ---------------------------------------------------------------------------

// The authority state of the coordinator: the current epoch plus the latest
// authority generation handed out. Both are monotonic and never wrap.
struct AuthorityState {
  CoordinatorEpoch epoch;               // current durable epoch (>= 1 once started).
  AuthorityGeneration authority;        // latest authority generation assigned.
  std::uint64_t registration_counter{0};// monotonic permit counter.

  // Advance to the next authority generation after a mutation took effect.
  AuthorityGeneration advance_authority_generation() {
    authority = authority.next();
    return authority;
  }
};

// A registration permit is a coordinator-issued admission token. A replayed
// REGISTER bearing a permit from a fenced boot must remain rejected.
struct RegistrationPermit {
  RegistrationPermitId permit_id;
  CoordinatorEpoch epoch;                 // epoch that issued the permit.
  WorkerId worker_id;                     // worker the permit is for.
  WorkerBootId worker_boot;               // boot the permit is for.
  std::uint64_t sequence{0};              // monotonic sequence.
  AuthorityGeneration authority;          // authority generation at issue.
  bool consumed{false};                   // set when the worker completes REGISTER.

  [[nodiscard]] bool is_valid() const noexcept {
    return permit_id.is_valid() && epoch.is_valid();
  }
};

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_AUTHORITY_HPP
