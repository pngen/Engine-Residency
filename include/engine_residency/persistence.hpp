#ifndef ENGINE_RESIDENCY_PERSISTENCE_HPP
#define ENGINE_RESIDENCY_PERSISTENCE_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "engine_residency/components.hpp"
#include "engine_residency/economics.hpp"
#include "engine_residency/engine.hpp"
#include "engine_residency/preparation.hpp"
#include "engine_residency/profiles.hpp"
#include "engine_residency/replacement.hpp"
#include "engine_residency/state.hpp"

namespace engine_residency {

inline constexpr std::uint32_t kPersistenceMagic = 0x45524553;  // "ERES"
inline constexpr std::uint32_t kPersistenceVersion = 2;
inline constexpr std::uint32_t kMaxSerializedBytes = 256u * 1024u * 1024u;  // 256 MiB cap

struct PersistentState {
  std::uint32_t version{kPersistenceVersion};
  std::uint64_t coordinator_epoch{0};
  std::uint64_t authority_generation{0};
  std::uint64_t registration_counter{0};
  std::vector<EngineDefinition> engines;
  std::vector<ReadinessProfile> profiles;
  std::vector<EngineIncarnation> incarnations;
  std::vector<ComponentEvidence> evidence;
  std::vector<ReplacementPlan> replacements;
  std::vector<CostComponent> cost_history;
};

std::string serialize_persistent_state(const PersistentState& state);

bool deserialize_persistent_state(const std::string& blob,
                                  PersistentState& out,
                                  std::string& error);

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_PERSISTENCE_HPP