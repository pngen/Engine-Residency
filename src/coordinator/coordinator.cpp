#include "coordinator/session.hpp"
#include "engine_residency/backend.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/protocol.hpp"
#include "engine_residency/net.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

namespace engine_residency {

// Runs the reference coordinator accepting loopback connections. Returns the
// bound port (so a caller can discover an ephemeral port).
int run_coordinator(std::uint16_t port) {
  if (!net::init()) return 2;
  SteadyClock clock;
  SessionShared shared(clock);
  EngineDefinition def;
  seed_reference_engine(shared.runtime, def);

  net::TcpListener listener;
  if (!listener.listen(port)) {
    net::shutdown();
    return 3;
  }
  std::atomic<bool> done{false};
  while (!done.load()) {
    net::TcpSocket sock;
    if (!listener.accept(sock)) {
      if (!listener.is_open()) break;   // listener closed.
      continue;
    }
    std::thread([&shared, s = std::move(sock)]() mutable {
      run_session(shared, std::move(s));
    }).detach();
  }
  listener.close();
  net::shutdown();
  return 0;
}

}  // namespace engine_residency
