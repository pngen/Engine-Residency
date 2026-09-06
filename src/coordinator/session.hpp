#ifndef ENGINE_RESIDENCY_SESSION_HPP
#define ENGINE_RESIDENCY_SESSION_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "engine_residency/net.hpp"
#include "engine_residency/runtime.hpp"

namespace engine_residency {

struct SessionShared {
  EngineResidency runtime;
  std::mutex mtx;
  std::map<std::uint64_t, std::string> session_owner;
  std::map<std::uint64_t, EngineIncarnationId> session_inc;
  std::map<std::uint64_t, ServingUseId> session_use;
  std::uint64_t next_session{1};
  explicit SessionShared(Clock& c) : runtime(c) {}
};

void run_session(SessionShared& shared, net::TcpSocket sock);

ReadinessProfileId seed_reference_engine(EngineResidency& rt, EngineDefinition& out_def);

// Runs the reference coordinator accept loop; returns the bound port.
int run_coordinator(std::uint16_t port);

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_SESSION_HPP
