#ifndef ENGINE_RESIDENCY_ERRORS_HPP
#define ENGINE_RESIDENCY_ERRORS_HPP

#include <cstdint>
#include <stdexcept>
#include <string>

namespace engine_residency {

// Domain error categories. A domain error represents a rejected state
// transition or an invalid operation against the engine-residency state model.
// These are distinct from programming errors (std::logic_error) and from the
// ordinary success/failure of an individual operation (which is returned as a
// typed result).
enum class ErrorCode {
  None = 0,
  InvalidArgument,
  InvalidState,
  StaleAuthority,
  StaleIncarnation,
  StaleGeneration,
  StaleEpoch,
  UnknownIncarnation,
  UnknownEngine,
  UnknownProfile,
  UnknownSlot,
  UnknownUse,
  UnknownAttempt,
  NotCurrentAuthority,
  RegistrationRejected,
  FencedWorker,
  DrainInProgress,
  ActivationConflict,
  ActivationRequired,
  UseConflict,
  UseAfterDrain,
  UseOverflow,
  DuplicateRelease,
  StandbyConflict,
  CapacityExhausted,
  PlanStale,
  AttemptCancelled,
  AttemptFailed,
  RollbackRequired,
  IncarnationGone,
  ProcessLost,
  BackendMissing,
  BackendError,
  GraphInvalid,
  KernelMissing,
  ModelMissing,
  AdapterMismatch,
  KvCapacityInsufficient,
  IncompatibleProfile,
  DependencyInvalidated,
  PersistenceCorrupt,
  PersistenceUnsupportedVersion,
  PersistenceRejected,
  GenerationExhausted,
  BoundsExceeded,
  ProtocolError,
  FrameMalformed,
  ChecksumMismatch,
  NotPermitted,
  NotSupported,
  NotFound,
  Internal,
};

inline const char* to_string(ErrorCode c) noexcept {
  switch (c) {
    case ErrorCode::None: return "None";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::InvalidState: return "InvalidState";
    case ErrorCode::StaleAuthority: return "StaleAuthority";
    case ErrorCode::StaleIncarnation: return "StaleIncarnation";
    case ErrorCode::StaleGeneration: return "StaleGeneration";
    case ErrorCode::StaleEpoch: return "StaleEpoch";
    case ErrorCode::UnknownIncarnation: return "UnknownIncarnation";
    case ErrorCode::UnknownEngine: return "UnknownEngine";
    case ErrorCode::UnknownProfile: return "UnknownProfile";
    case ErrorCode::UnknownSlot: return "UnknownSlot";
    case ErrorCode::UnknownUse: return "UnknownUse";
    case ErrorCode::UnknownAttempt: return "UnknownAttempt";
    case ErrorCode::NotCurrentAuthority: return "NotCurrentAuthority";
    case ErrorCode::RegistrationRejected: return "RegistrationRejected";
    case ErrorCode::FencedWorker: return "FencedWorker";
    case ErrorCode::DrainInProgress: return "DrainInProgress";
    case ErrorCode::ActivationConflict: return "ActivationConflict";
    case ErrorCode::ActivationRequired: return "ActivationRequired";
    case ErrorCode::UseConflict: return "UseConflict";
    case ErrorCode::UseAfterDrain: return "UseAfterDrain";
    case ErrorCode::UseOverflow: return "UseOverflow";
    case ErrorCode::DuplicateRelease: return "DuplicateRelease";
    case ErrorCode::StandbyConflict: return "StandbyConflict";
    case ErrorCode::CapacityExhausted: return "CapacityExhausted";
    case ErrorCode::PlanStale: return "PlanStale";
    case ErrorCode::AttemptCancelled: return "AttemptCancelled";
    case ErrorCode::AttemptFailed: return "AttemptFailed";
    case ErrorCode::RollbackRequired: return "RollbackRequired";
    case ErrorCode::IncarnationGone: return "IncarnationGone";
    case ErrorCode::ProcessLost: return "ProcessLost";
    case ErrorCode::BackendMissing: return "BackendMissing";
    case ErrorCode::BackendError: return "BackendError";
    case ErrorCode::GraphInvalid: return "GraphInvalid";
    case ErrorCode::KernelMissing: return "KernelMissing";
    case ErrorCode::ModelMissing: return "ModelMissing";
    case ErrorCode::AdapterMismatch: return "AdapterMismatch";
    case ErrorCode::KvCapacityInsufficient: return "KvCapacityInsufficient";
    case ErrorCode::IncompatibleProfile: return "IncompatibleProfile";
    case ErrorCode::DependencyInvalidated: return "DependencyInvalidated";
    case ErrorCode::PersistenceCorrupt: return "PersistenceCorrupt";
    case ErrorCode::PersistenceUnsupportedVersion: return "PersistenceUnsupportedVersion";
    case ErrorCode::PersistenceRejected: return "PersistenceRejected";
    case ErrorCode::GenerationExhausted: return "GenerationExhausted";
    case ErrorCode::BoundsExceeded: return "BoundsExceeded";
    case ErrorCode::ProtocolError: return "ProtocolError";
    case ErrorCode::FrameMalformed: return "FrameMalformed";
    case ErrorCode::ChecksumMismatch: return "ChecksumMismatch";
    case ErrorCode::NotPermitted: return "NotPermitted";
    case ErrorCode::NotSupported: return "NotSupported";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::Internal: return "Internal";
  }
  return "Unknown";
}

// Domain error carrying a category and an explanatory message.
class DomainError final : public std::runtime_error {
 public:
  DomainError(ErrorCode code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}
  ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

inline void throw_error(ErrorCode code, const std::string& msg) {
  throw DomainError(code, msg);
}

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_ERRORS_HPP
