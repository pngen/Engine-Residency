#include "coordinator/session.hpp"
#include "engine_residency/backend.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/persistence.hpp"
#include "engine_residency/protocol.hpp"
#include "engine_residency/net.hpp"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>

namespace engine_residency {

int run_coordinator(std::uint16_t port, const std::string& state_path) {
  if (!net::init()) return 2;
  SteadyClock clock;
  auto shared = std::make_shared<SessionShared>(clock);
  EngineDefinition def;
  seed_reference_engine(shared->runtime, def);

  if (!state_path.empty()) {
    std::ifstream in(state_path, std::ios::binary);
    if (in) {
      std::string blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      try {
        shared->runtime.load(blob);
        shared->runtime.advance_epoch();
        std::printf("coordinator: recovered durable state; advanced epoch\n");
      } catch (const DomainError& e) {
        std::printf("coordinator: refused corrupt state: %s\n", e.what());
      }
    }
  }

  net::TcpListener listener;
  if (!listener.listen(port)) { net::shutdown(); return 3; }
  std::atomic<bool> done{false};
  while (!done.load()) {
    net::TcpSocket sock;
    if (!listener.accept(sock)) { if (!listener.is_open()) break; continue; }
    // The session threads own a shared_ptr to the shared state so that no
    // detached thread ever dereferences a destroyed SessionShared on shutdown.
    std::thread([shared, s = std::move(sock)]() mutable { run_session(*shared, std::move(s)); }).detach();
  }
  listener.close();
  net::shutdown();
  return 0;
}

}  // namespace engine_residency
