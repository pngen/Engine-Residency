#ifndef ENGINE_RESIDENCY_SESSION_HPP
#define ENGINE_RESIDENCY_SESSION_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "engine_residency/net.hpp"
#include "engine_residency/runtime.hpp"

namespace engine_residency {

struct SessionShared;

// Result of routing a controller command to a worker.
struct CommandReply {
  bool ok{false};
  bool timed_out{false};
  std::string detail;
  std::map<std::string, std::string> fields;  // correlated result payload fields.
};

// A routed command queued to a worker. `promise` carries the correlated reply
// back to the controller thread that issued the command.
struct WorkerCommand {
  MessageType type{MessageType::EXECUTE};
  std::uint64_t command_id{0};
  std::uint32_t epoch{0};
  std::uint32_t authority{0};
  std::map<std::string, std::string> params;
  std::promise<CommandReply> promise;
};

// ---------------------------------------------------------------------------
// WorkerRouter
//
// Once a worker has completed setup and signalled COMMAND_READY, its session
// thread adopts the worker socket and runs the router dispatcher loop, which is
// the single reader/writer of that socket. Commands are processed strictly in
// order (one socket, one in-flight command), so a queued DRAIN is only sent to
// the worker after any in-flight EXECUTE has fully completed AND its serving use
// has been released. This guarantees the backend buffers/graph remain valid for
// the lifetime of an admitted use, and that physical cleanup never precedes use
// closure.
//
// Runtime authority stays in the runtime: the router invokes the guarded acquire/
// release/request_drain/acknowledge_backend_cleanup operations, never holding the
// runtime lock across a network or backend wait.
// ---------------------------------------------------------------------------
class WorkerRouter {
 public:
  WorkerRouter(std::uint64_t session_id, WorkerId w, WorkerBootId b,
               EngineIncarnationId inc, EngineId eid);
  ~WorkerRouter();

  WorkerId worker_id() const { return worker_id_; }
  WorkerBootId worker_boot() const { return worker_boot_; }
  EngineIncarnationId incarnation() const { return inc_; }
  EngineId engine_id() const { return engine_id_; }
  std::uint64_t session_id() const { return session_id_; }

  bool alive() const { return alive_.load(); }
  bool ready() const { return ready_.load(); }

  // Adopt the worker socket (called by the session thread right before entering
  // the dispatcher loop). Returns false if already adopted/dead.
  bool adopt(net::TcpSocket sock);

  // Controller-thread entry point: enqueue a command and return a future to the
  // correlated reply. Rejects (future immediately resolved with an error) if the
  // router is not ready or the bounded queue is full.
  std::future<CommandReply> enqueue(MessageType type, std::uint64_t command_id,
                                    std::uint32_t epoch, std::uint32_t authority,
                                    std::map<std::string, std::string> params);

  // The dispatcher loop. Blocks until the worker disconnects or the router is
  // failed. Runs on the worker's session thread.
  void run(SessionShared& shared);

  // Mark the router dead (worker disconnected/fenced) and fail every pending
  // command (and the in-flight one) with an error.
  void fail(const std::string& reason);

  // How many commands are currently pending.
  std::size_t pending_count() const;

 private:
  void process(WorkerCommand cmd, SessionShared& shared);
  bool send_raw(const ProtocolMessage& m);
  bool recv_raw(ProtocolMessage& out);
  void fulfill_error(std::uint64_t command_id, const std::string& msg);
  bool check_cycle(SessionShared& shared, std::uint32_t epoch, std::uint32_t authority,
                   std::string& err);

  std::uint64_t session_id_;
  WorkerId worker_id_;
  WorkerBootId worker_boot_;
  EngineIncarnationId inc_;
  EngineId engine_id_;

  std::atomic<bool> ready_{false};
  std::atomic<bool> alive_{true};

  mutable std::mutex sock_mtx_;   // protects socket ownership transition.
  net::TcpSocket sock_;           // owned once adopted.
  mutable std::mutex wmtx_;       // serializes complete-frame writes.

  mutable std::mutex qmtx_;
  std::condition_variable qcv_;
  std::deque<WorkerCommand> queue_;
  std::size_t max_queue_{64};
};

struct SessionShared {
  EngineResidency runtime;
  std::mutex mtx;
  std::map<std::uint64_t, std::string> session_owner;
  std::map<std::uint64_t, EngineIncarnationId> session_inc;
  std::map<std::uint64_t, ServingUseId> session_use;
  std::uint64_t next_session{1};

  // Worker command routing registry. An incarnation binds to at most one worker
  // router at a time; boot_to_inc validates worker-boot binding.
  std::map<EngineIncarnationId, std::shared_ptr<WorkerRouter>> worker_router;
  std::map<std::pair<std::uint64_t, std::uint64_t>, EngineIncarnationId> boot_to_inc;

  explicit SessionShared(Clock& c) : runtime(c) {}
};

void run_session(SessionShared& shared, net::TcpSocket sock);

ReadinessProfileId seed_reference_engine(EngineResidency& rt, EngineDefinition& out_def);

// Runs the reference coordinator accept loop; returns the bound port.
int run_coordinator(std::uint16_t port, const std::string& state_path = "");

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_SESSION_HPP
