#include "coordinator/session.hpp"
#include "engine_residency/protocol.hpp"
#include "engine_residency/components.hpp"
#include "engine_residency/activation.hpp"
#include "engine_residency/serving_use.hpp"
#include "engine_residency/engine.hpp"
#include "engine_residency/state.hpp"
#include "engine_residency/replacement.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

using engine_residency::detail::parse_payload;
using engine_residency::detail::get;
using engine_residency::detail::make_payload;

namespace {
std::uint64_t u(const std::map<std::string, std::string>& f, const std::string& k, std::uint64_t d = 0) {
  auto it = f.find(k); if (it == f.end()) return d;
  try { return std::stoull(it->second); } catch (...) { return d; }
}
}  // namespace

namespace engine_residency {

inline const char* to_string_phase(ReplacementPlan::Phase p) noexcept {
  switch (p) {
    case ReplacementPlan::Phase::PLANNED: return "PLANNED";
    case ReplacementPlan::Phase::PREPARING: return "PREPARING";
    case ReplacementPlan::Phase::CANDIDATE_READY: return "CANDIDATE_READY";
    case ReplacementPlan::Phase::CUTOVER: return "CUTOVER";
    case ReplacementPlan::Phase::OLD_DRAIN: return "OLD_DRAIN";
    case ReplacementPlan::Phase::RETIRE_OLD: return "RETIRE_OLD";
    case ReplacementPlan::Phase::COMPLETE: return "COMPLETE";
  }
  return "UNKNOWN";
}

ReadinessProfileId seed_reference_engine(EngineResidency& rt, EngineDefinition& out_def) {
  EngineDefinition def;
  def.engine_id = EngineId(1);
  def.engine_generation = EngineGeneration(1);
  def.config_generation = EngineConfigGeneration(1);
  def.name = "reference-engine";
  def.role = ServingRole::GENERAL_INFERENCE;
  def.backend_id = BackendId(1);
  def.backend_generation = BackendGeneration(1);
  def.model_id = ModelId(1);
  def.model_generation = ModelGeneration(1);
  def.model_name = "reference-model";
  def.model_bytes = 1u << 20;
  def.workspace_bytes = 1u << 16;
  def.kv_capacity_entries = 1024;
  def.kernel_key = "er-reference-kernel";
  def.kernel_generation = KernelGeneration(1);
  def.kernel_required = true;
  def.graph_required = true;
  def.graph_optional_fallback = false;
  def.graph_generation = GraphGeneration(1);
  def.compatibility = CompatibilityKey::with_namespace("ref/v1");
  def.standby_eligible = true;
  def.standby_target = 1;
  def.max_concurrency = 8;
  def.warmup_required = true;
  out_def = rt.define_engine(def);
  ReadinessProfile prof;
  prof.profile_id = ReadinessProfileId(1);
  prof.profile_generation = ReadinessProfileGeneration(1);
  prof.engine_id = def.engine_id;
  prof.engine_generation = def.engine_generation;
  prof.name = "reference-bs1";
  prof.role = ServingRole::GENERAL_INFERENCE;
  prof.max_batch = 1;
  prof.max_seq_len = 4096;
  prof.dtype = "fp32";
  prof.layout = "row-major";
  prof.workspace_bytes = 1u << 16;
  prof.model_bytes = 1u << 20;
  prof.kv_capacity_entries = 1024;
  prof.graph_required = true;
  prof.graph_optional_fallback = false;
  prof.warmup_required = true;
  prof.compatibility = CompatibilityKey::with_namespace("ref/v1");
  ProfileRequirement backend; backend.category = ComponentCategory::BACKEND; backend.kind = RequirementKind::REQUIRED; backend.min_state = ComponentState::VERIFIED; backend.description = "backend verified";
  ProfileRequirement dctx; dctx.category = ComponentCategory::DEVICE_CONTEXT; dctx.kind = RequirementKind::REQUIRED; dctx.min_state = ComponentState::BOUND; dctx.description = "device context bound";
  ProfileRequirement model; model.category = ComponentCategory::MODEL; model.kind = RequirementKind::REQUIRED; model.min_state = ComponentState::VERIFIED; model.subject = "reference-model"; model.generation_family = GenerationFamily::MODEL; model.compatibility = prof.compatibility; model.description = "model bound+verified";
  ProfileRequirement kv; kv.category = ComponentCategory::KV_CAPACITY; kv.kind = RequirementKind::REQUIRED; kv.min_state = ComponentState::BOUND; kv.subject = "kv-main"; kv.generation_family = GenerationFamily::KV_CAPACITY; kv.description = "kv capacity provisioned";
  ProfileRequirement kernel; kernel.category = ComponentCategory::KERNEL; kernel.kind = RequirementKind::REQUIRED; kernel.min_state = ComponentState::VERIFIED; kernel.subject = "er-reference-kernel"; kernel.generation_family = GenerationFamily::KERNEL; kernel.compatibility = prof.compatibility; kernel.description = "kernel bound+verified";
  ProfileRequirement graph; graph.category = ComponentCategory::GRAPH; graph.kind = RequirementKind::REQUIRED; graph.min_state = ComponentState::VERIFIED; graph.subject = "er-reference-graph"; graph.generation_family = GenerationFamily::GRAPH; graph.description = "graph instantiated+verified";
  ProfileRequirement warmup; warmup.category = ComponentCategory::WARMUP; warmup.kind = RequirementKind::REQUIRED; warmup.min_state = ComponentState::VERIFIED; warmup.subject = "warmup"; warmup.description = "warmup verified";
  prof.requirements = {backend, dctx, model, kv, kernel, graph, warmup};
  rt.define_profile(prof);
  return prof.profile_id;
}

// ---------------------------------------------------------------------------
// WorkerRouter
// ---------------------------------------------------------------------------
WorkerRouter::WorkerRouter(std::uint64_t session_id, WorkerId w, WorkerBootId b,
                           EngineIncarnationId inc, EngineId eid)
    : session_id_(session_id), worker_id_(w), worker_boot_(b), inc_(inc), engine_id_(eid) {}
WorkerRouter::~WorkerRouter() {
  if (sock_.is_open()) sock_.close();
}

std::size_t WorkerRouter::pending_count() const {
  std::lock_guard<std::mutex> lk(qmtx_);
  return queue_.size();
}

bool WorkerRouter::adopt(net::TcpSocket sock) {
  std::lock_guard<std::mutex> lk(sock_mtx_);
  if (sock_.is_open() || !alive_.load()) return false;
  sock_ = std::move(sock);
  return true;
}

std::future<CommandReply> WorkerRouter::enqueue(MessageType type, std::uint64_t command_id,
                                                std::uint32_t epoch, std::uint32_t authority,
                                                std::map<std::string, std::string> params) {
  std::promise<CommandReply> p;
  std::future<CommandReply> fut = p.get_future();
  bool accepted = false;
  {
    std::lock_guard<std::mutex> lk(qmtx_);
    if (ready_.load() && alive_.load() && queue_.size() < max_queue_) {
      WorkerCommand cmd;
      cmd.type = type;
      cmd.command_id = command_id;
      cmd.epoch = epoch;
      cmd.authority = authority;
      cmd.params = std::move(params);
      cmd.promise = std::move(p);
      queue_.push_back(std::move(cmd));
      accepted = true;
    }
  }
  if (accepted) {
    qcv_.notify_one();
  } else {
    CommandReply rep;
    rep.ok = false;
    rep.detail = "router not ready or pending command queue full";
    rep.fields["msg"] = rep.detail;
    p.set_value(rep);
  }
  return fut;
}

bool WorkerRouter::send_raw(const ProtocolMessage& m) {
  std::lock_guard<std::mutex> lk(wmtx_);
  return sock_.send_frame(m);
}

bool WorkerRouter::recv_raw(ProtocolMessage& out) {
  // Only the dispatcher thread reads; this is the single socket reader.
  return sock_.recv_frame(out);
}

bool WorkerRouter::check_cycle(SessionShared& shared, std::uint32_t epoch,
                               std::uint32_t authority, std::string& err) {
  if (epoch != 0 && epoch != shared.runtime.current_epoch().raw()) {
    err = std::string("stale epoch: ") + std::to_string(epoch);
    return false;
  }
  if (authority != 0 && authority != shared.runtime.current_authority().raw()) {
    err = std::string("stale authority: ") + std::to_string(authority);
    return false;
  }
  return true;
}

void WorkerRouter::run(SessionShared& shared) {
  {
    std::lock_guard<std::mutex> lk(qmtx_);
    if (!alive_.load()) return;
    ready_.store(true);
  }
  qcv_.notify_all();
  while (alive_.load()) {
    WorkerCommand cmd;
    {
      std::unique_lock<std::mutex> lk(qmtx_);
      qcv_.wait(lk, [&]() { return !queue_.empty() || !alive_.load(); });
      if (!alive_.load() && queue_.empty()) break;
      cmd = std::move(queue_.front());
      queue_.pop_front();
    }
    process(std::move(cmd), shared);
  }
  // Worker disconnected or failed: settle anything remaining.
  fail("worker session ended");
}

void WorkerRouter::fail(const std::string& reason) {
  alive_.store(false);
  std::deque<WorkerCommand> q;
  {
    std::lock_guard<std::mutex> lk(qmtx_);
    q.swap(queue_);
  }
  CommandReply rep;
  rep.ok = false;
  rep.detail = "worker failed: " + reason;
  rep.fields["msg"] = rep.detail;
  for (auto& c : q) {
    try { c.promise.set_value(rep); } catch (...) { /* already set */ }
  }
  qcv_.notify_all();
  {
    std::lock_guard<std::mutex> lk(sock_mtx_);
    if (sock_.is_open()) sock_.close();
  }
}

namespace {
bool acquire_use_for(WorkerRouter& r, SessionShared& shared,
                     const std::map<std::string, std::string>& params,
                     ServingUseId& use_id, std::string& err) {
  ServingUseToken tok;
  tok.incarnation_id = r.incarnation();
  tok.incarnation_generation = EngineIncarnationGeneration(1);
  tok.engine_id = r.engine_id();
  tok.engine_generation = EngineGeneration(1);
  tok.profile_id = ReadinessProfileId(1);
  tok.readiness_generation = ReadinessGeneration((std::uint64_t)u(params, "readiness_gen", 1));
  tok.activation_generation = ActivationGeneration((std::uint64_t)u(params, "activation_gen", 1));
  tok.execution_id = ExecutionId((std::uint64_t)u(params, "execution_id", 1));
  tok.workload_id = WorkloadId((std::uint64_t)u(params, "workload_id", 1));
  try {
    ServingUseToken got = shared.runtime.acquire_serving_use(tok, "coordinator");
    use_id = got.use_id;
    return true;
  } catch (const DomainError& e) {
    err = e.what();
    return false;
  }
}
void release_use_for(WorkerRouter& r, SessionShared& shared, ServingUseId use_id,
                     WorkOutcome outcome, const std::string& detail) {
  try {
    ServingUseToken rel;
    rel.use_id = use_id;
    rel.incarnation_id = r.incarnation();
    rel.incarnation_generation = EngineIncarnationGeneration(1);
    shared.runtime.release_serving_use(rel, outcome, detail);
  } catch (...) { /* best effort: the use may already be closed (stale/replace) */ }
}
}  // namespace

void WorkerRouter::process(WorkerCommand cmd, SessionShared& shared) {
  std::string err;
  if (!check_cycle(shared, cmd.epoch, cmd.authority, err)) {
    CommandReply rep; rep.ok = false; rep.detail = err; rep.fields["msg"] = err;
    try { cmd.promise.set_value(rep); } catch (...) {}
    return;
  }
  if (cmd.type == MessageType::EXECUTE) {
    if (shared.runtime.incarnation_is_fenced(inc_) || shared.runtime.drain_state(inc_).admission_fenced) {
      CommandReply rep; rep.ok = false; rep.detail = "incarnation fenced/drained"; rep.fields["msg"] = rep.detail;
      try { cmd.promise.set_value(rep); } catch (...) {}
      return;
    }
    ServingUseId use_id;
    std::string aerr;
    if (!acquire_use_for(*this, shared, cmd.params, use_id, aerr)) {
      CommandReply rep; rep.ok = false; rep.detail = aerr; rep.fields["msg"] = aerr;
      try { cmd.promise.set_value(rep); } catch (...) {}
      return;
    }
    ProtocolMessage fwd;
    fwd.type = MessageType::EXECUTE;
    fwd.epoch = cmd.epoch;
    fwd.authority = cmd.authority;
    fwd.payload = make_payload({
        {"cmd_id", std::to_string(cmd.command_id)},
        {"seed", get(cmd.params, "seed", "42")},
        {"batch", get(cmd.params, "batch", "1")},
        {"seq_len", get(cmd.params, "seq_len", "16")},
        {"hold_iters", get(cmd.params, "hold_iters", "0")},
        {"marker", get(cmd.params, "marker", "")},
        {"hold_barrier", get(cmd.params, "hold_barrier", "")},
    });
    if (!send_raw(fwd)) {
      release_use_for(*this, shared, use_id, WorkOutcome::FAILED, "worker disconnected during execute");
      fail("worker disconnected during execute");
      CommandReply rep; rep.ok = false; rep.detail = "worker disconnected during execute"; rep.fields["msg"] = rep.detail;
      try { cmd.promise.set_value(rep); } catch (...) {}
      return;
    }
    ProtocolMessage resp;
    if (!recv_raw(resp)) {
      release_use_for(*this, shared, use_id, WorkOutcome::FAILED, "worker read failed during execute");
      fail("worker read failed during execute");
      CommandReply rep; rep.ok = false; rep.detail = "worker read failed during execute"; rep.fields["msg"] = rep.detail;
      try { cmd.promise.set_value(rep); } catch (...) {}
      return;
    }
    bool comp = (resp.type != MessageType::ERROR);
    release_use_for(*this, shared, use_id, comp ? WorkOutcome::COMPLETED : WorkOutcome::FAILED,
                    comp ? "execute completed" : "worker reported failure");
    CommandReply rep;
    rep.ok = comp;
    rep.fields = parse_payload(resp.payload);
    rep.detail = get(rep.fields, "detail", "execute result");
    if (!rep.ok && rep.fields.find("msg") == rep.fields.end()) rep.fields["msg"] = rep.detail;
    try { cmd.promise.set_value(rep); } catch (...) {}
    return;
  }

  if (cmd.type == MessageType::DRAIN) {
    ProtocolMessage fwd;
    fwd.type = MessageType::DRAIN;
    fwd.epoch = cmd.epoch;
    fwd.authority = cmd.authority;
    fwd.payload = make_payload({{"cmd_id", std::to_string(cmd.command_id)}, {"marker", get(cmd.params, "marker", "")}});
    if (!send_raw(fwd)) {
      fail("worker disconnected during drain");
      CommandReply rep; rep.ok = false; rep.detail = "worker disconnected during drain"; rep.fields["msg"] = rep.detail;
      try { cmd.promise.set_value(rep); } catch (...) {}
      return;
    }
    ProtocolMessage resp;
    if (!recv_raw(resp)) {
      fail("worker read failed during drain");
      CommandReply rep; rep.ok = false; rep.detail = "worker read failed during drain"; rep.fields["msg"] = rep.detail;
      try { cmd.promise.set_value(rep); } catch (...) {}
      return;
    }
    std::string aerr;
    try { shared.runtime.acknowledge_backend_cleanup(inc_); }
    catch (const DomainError& e) { aerr = e.what(); }
    DrainState st = shared.runtime.drain_state(inc_);
    CommandReply rep;
    rep.ok = (resp.type != MessageType::ERROR) && aerr.empty();
    rep.fields = parse_payload(resp.payload);
    rep.fields["drain_phase"] = std::string(to_string(st.phase));
    rep.detail = aerr.empty() ? get(rep.fields, "detail", "drain complete") : aerr;
    if (!rep.ok && rep.fields.find("msg") == rep.fields.end()) rep.fields["msg"] = rep.detail;
    try { cmd.promise.set_value(rep); } catch (...) {}
    return;
  }

  CommandReply rep; rep.ok = false; rep.detail = "unsupported routed command"; rep.fields["msg"] = rep.detail;
  try { cmd.promise.set_value(rep); } catch (...) {}
}

namespace {
bool reply_to_controller(net::TcpSocket& sock, const CommandReply& rep,
                         std::uint64_t cmd_id, std::uint64_t epoch, std::uint64_t authority) {
  ProtocolMessage ack;
  ack.type = rep.ok ? MessageType::REGISTER_ACK : MessageType::ERROR;
  ack.epoch = (std::uint32_t)epoch;
  ack.authority = (std::uint32_t)authority;
  std::map<std::string, std::string> fields = rep.fields;
  fields["cmd_id"] = std::to_string(cmd_id);
  if (!rep.ok && fields.find("msg") == fields.end()) fields["msg"] = rep.detail;
  if (fields.find("detail") == fields.end()) fields["detail"] = rep.detail;
  ack.payload = make_payload(fields);
  return sock.send_frame(ack);
}

std::shared_ptr<WorkerRouter> find_router(SessionShared& shared, EngineIncarnationId target,
                                          std::uint64_t target_boot, std::string& why) {
  std::shared_ptr<WorkerRouter> router;
  {
    std::lock_guard<std::mutex> lk(shared.mtx);
    auto it = shared.worker_router.find(target);
    if (it != shared.worker_router.end()) router = it->second;
  }
  if (!router) { why = "no worker bound to target incarnation"; return nullptr; }
  if (target_boot != 0 && router->worker_boot().raw() != target_boot) {
    why = "worker boot binding mismatch";
    return nullptr;
  }
  if (!router->ready()) { why = "worker is not command-ready"; return nullptr; }
  return router;
}

void route_execute(SessionShared& shared, net::TcpSocket& sock,
                   const std::map<std::string, std::string>& f,
                   std::uint32_t epoch, std::uint32_t authority,
                   EngineIncarnationId target, std::uint64_t target_boot) {
  std::string why;
  auto router = find_router(shared, target, target_boot, why);
  if (!router) {
    CommandReply rep; rep.ok = false; rep.detail = why; rep.fields["msg"] = why;
    reply_to_controller(sock, rep, u(f, "cmd_id", 0), epoch, authority);
    return;
  }
  std::uint64_t cmd_id = u(f, "cmd_id", 0);
  auto fut = router->enqueue(MessageType::EXECUTE, cmd_id, epoch, authority, f);
  auto status = fut.wait_for(std::chrono::seconds(60));
  if (status != std::future_status::ready) {
    CommandReply rep; rep.ok = false; rep.detail = "execute timed out awaiting worker";
    reply_to_controller(sock, rep, cmd_id, epoch, authority);
    return;
  }
  CommandReply rep = fut.get();
  reply_to_controller(sock, rep, cmd_id, epoch, authority);
}

void route_drain(SessionShared& shared, net::TcpSocket& sock,
                 const std::map<std::string, std::string>& f,
                 std::uint32_t epoch, std::uint32_t authority,
                 EngineIncarnationId target, std::uint64_t target_boot) {
  std::string why;
  auto router = find_router(shared, target, target_boot, why);
  if (!router) {
    CommandReply rep; rep.ok = false; rep.detail = why; rep.fields["msg"] = why;
    reply_to_controller(sock, rep, u(f, "cmd_id", 0), epoch, authority);
    return;
  }
  std::uint64_t cmd_id = u(f, "cmd_id", 0);
  try {
    if (shared.runtime.incarnation_is_fenced(target)) throw_error(ErrorCode::FencedWorker, "fenced incarnation");
    shared.runtime.request_drain(target);
  } catch (const DomainError& e) {
    CommandReply rep; rep.ok = false; rep.detail = e.what(); rep.fields["msg"] = e.what();
    reply_to_controller(sock, rep, cmd_id, epoch, authority);
    return;
  }
  auto fut = router->enqueue(MessageType::DRAIN, cmd_id, epoch, authority, f);
  auto status = fut.wait_for(std::chrono::seconds(60));
  if (status != std::future_status::ready) {
    CommandReply rep; rep.ok = false; rep.detail = "drain timed out awaiting worker";
    reply_to_controller(sock, rep, cmd_id, epoch, authority);
    return;
  }
  CommandReply rep = fut.get();
  reply_to_controller(sock, rep, cmd_id, epoch, authority);
}
}  // namespace

void run_session(SessionShared& shared, net::TcpSocket sock) {
  std::uint64_t sid = 0;
  { std::lock_guard<std::mutex> lk(shared.mtx); sid = shared.next_session++; }
  bool running = true;
  EngineIncarnationId inc_id;
  while (running) {
    ProtocolMessage msg;
    if (!sock.recv_frame(msg)) break;
    auto f = parse_payload(msg.payload);
    if (msg.epoch != 0 && msg.epoch != shared.runtime.current_epoch().raw()) {
      ProtocolMessage ack; ack.type = MessageType::ERROR;
      ack.payload = make_payload({{"code",std::to_string((int)ErrorCode::StaleEpoch)},{"msg","stale epoch traffic"}});
      sock.send_frame(ack);
      continue;
    }
    switch (msg.type) {
      case MessageType::HELLO: {
        ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
        ack.epoch = msg.epoch; ack.authority = msg.authority;
        ack.payload = make_payload({{"ok","true"},{"epoch",std::to_string(shared.runtime.current_epoch().raw())},{"authority",std::to_string(shared.runtime.current_authority().raw())}});
        sock.send_frame(ack);
        break;
      }
      case MessageType::REGISTER: {
        WorkerId w((std::uint64_t)u(f,"worker_id",1));
        WorkerBootId b((std::uint64_t)u(f,"worker_boot",1));
        SourceId s((std::uint64_t)u(f,"source_id",1));
        SourceBootId sb((std::uint64_t)u(f,"source_boot",1));
        try {
          RegistrationPermit permit = shared.runtime.issue_registration_permit(w, b, s, sb);
          EngineIncarnation inc;
          inc.engine_id = EngineId(1);
          inc.engine_generation = EngineGeneration(1);
          inc.config_generation = EngineConfigGeneration(1);
          inc.worker_id = w; inc.worker_boot = b; inc.source_id = s; inc.source_boot = sb;
          inc.process.pid = (std::uint32_t)u(f,"pid",0); inc.process.label = get(f,"label","worker");
          inc.backend_id = BackendId(1); inc.backend_generation = BackendGeneration(1);
          EngineIncarnation registered = shared.runtime.register_incarnation(inc, permit);
          inc_id = registered.incarnation_id;
          auto router = std::make_shared<WorkerRouter>(sid, w, b, registered.incarnation_id, registered.engine_id);
          { std::lock_guard<std::mutex> lk(shared.mtx); shared.session_inc[sid] = inc_id; shared.session_owner[sid] = get(f,"label","worker"); shared.worker_router[inc_id] = router; shared.boot_to_inc[{w.raw(), b.raw()}] = inc_id; }
          ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
          ack.payload = make_payload({{"ok","true"},{"incarnation_id",std::to_string(registered.incarnation_id.raw())},{"incarnation_generation",std::to_string(registered.incarnation_generation.raw())},{"readiness_gen",std::to_string(registered.readiness_generation.raw())},{"epoch",std::to_string(shared.runtime.current_epoch().raw())},{"authority",std::to_string(shared.runtime.current_authority().raw())}});
          sock.send_frame(ack);
        } catch (const DomainError& e) {
          ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack);
        }
        break;
      }
      case MessageType::PUBLISH_COMPONENT: {
        try {
          EngineIncarnationId target = shared.session_inc[sid];
          if (u(f,"incarnation_id",0) != 0) target = EngineIncarnationId(u(f,"incarnation_id",0));
          if (shared.runtime.incarnation_is_fenced(target) || shared.runtime.drain_state(target).admission_fenced) throw_error(ErrorCode::FencedWorker, "fenced/drained incarnation");
          ComponentEvidence ev;
          ev.incarnation_id = target;
          ev.engine_id = EngineId(1); ev.engine_generation = EngineGeneration(1);
          ev.category = (ComponentCategory)(int)u(f,"category",0);
          ev.state = (ComponentState)(int)u(f,"state",0);
          ev.subject = get(f,"subject","");
          ev.namespace_ = get(f,"ns","er");
          ev.provenance = (Provenance)(int)u(f,"provenance",0);
          ev.detail = get(f,"detail","");
          ev.model_generation = ModelGeneration((std::uint64_t)u(f,"model_gen",1));
          ev.kv_capacity_generation = KvCapacityGeneration((std::uint64_t)u(f,"kv_gen",1));
          ev.kernel_generation = KernelGeneration((std::uint64_t)u(f,"kernel_gen",1));
          ev.graph_generation = GraphGeneration((std::uint64_t)u(f,"graph_gen",1));
          ev.compatibility = CompatibilityKey::with_namespace(get(f,"compat","ref/v1"));
          ev.size_bytes = u(f,"size_bytes",0);
          ev.elapsed_ns = u(f,"elapsed_ns",0);
          ev.coordinator_epoch = shared.runtime.current_epoch();
          ComponentEvidence pub = shared.runtime.publish_component(ev);
          ProtocolMessage ack; ack.type = MessageType::PREPARE_RESULT;
          ack.payload = make_payload({{"ok","true"},{"evidence_id",std::to_string(pub.evidence_id.raw())}});
          sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::PREPARE: {
        try {
          PreparationPlan plan = shared.runtime.create_preparation_plan(inc_id, ReadinessProfileId(1), DesiredResidency::WARM);
          PreparationAttempt a = shared.runtime.begin_preparation(plan);
          for (std::size_t i = 0; i < plan.steps.size() + 1; ++i) a = shared.runtime.step_preparation(a.attempt_id);
          PreparationAttempt done = shared.runtime.complete_preparation(a.attempt_id);
          ProtocolMessage ack; ack.type = MessageType::PREPARE_RESULT;
          ack.payload = make_payload({{"ok","true"},{"preparation",std::string(to_string(done.state))}});
          sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::QUERY_READINESS: {
        try {
          EngineIncarnationId target = inc_id;
          if (u(f,"incarnation_id",0) != 0) target = EngineIncarnationId(u(f,"incarnation_id",0));
          if (shared.runtime.incarnation_is_fenced(target) || shared.runtime.drain_state(target).admission_fenced) throw_error(ErrorCode::FencedWorker, "fenced/drained incarnation");
          ReadinessResult rr = shared.runtime.evaluate_readiness(target, ReadinessProfileId(1));
          ProtocolMessage ack; ack.type = MessageType::PREPARE_RESULT;
          std::string miss=[&]{std::string x; for (const auto& m : rr.missing){if(!x.empty())x+=",";x+=m;} for (const auto& i : rr.incompatible){if(!x.empty())x+=",";x+=i;} return x;}(); ack.payload = make_payload({{"outcome",std::string(to_string(rr.outcome))},{"missing",miss}});
          sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::ACTIVATE: {
        try {
          EngineIncarnationId target = inc_id;
          if (u(f,"incarnation_id",0) != 0) target = EngineIncarnationId(u(f,"incarnation_id",0));
          if (shared.runtime.incarnation_is_fenced(target) || shared.runtime.drain_state(target).admission_fenced) throw_error(ErrorCode::FencedWorker, "fenced/drained incarnation");
          ActivationRequest req;
          req.incarnation_id = target; req.incarnation_generation = EngineIncarnationGeneration(1);
          req.engine_id = EngineId(1); req.engine_generation = EngineGeneration(1);
          req.profile_id = ReadinessProfileId(1); req.profile_generation = ReadinessProfileGeneration(1);
          std::uint64_t rgen = u(f,"readiness_gen",0);
          req.readiness_generation = (rgen != 0) ? ReadinessGeneration(rgen) : shared.runtime.current_readiness_generation(target);
          req.standby_generation = StandbyGeneration(1);
          req.caller_authority = shared.runtime.current_authority();
          req.slot_id = EngineSlotId((std::uint64_t)u(f,"slot_id",1));
          req.caller = get(f,"caller","controller");
          ActivationRecord rec = shared.runtime.activate(req);
          ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
          ack.payload = make_payload({{"ok","true"},{"activation_id",std::to_string(rec.activation_id.raw())},{"state",std::string(to_string(rec.state))}});
          sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::ACQUIRE_USE: {
        try {
          EngineIncarnationId target = inc_id;
          if (u(f,"incarnation_id",0) != 0) target = EngineIncarnationId(u(f,"incarnation_id",0));
          if (shared.runtime.incarnation_is_fenced(target) || shared.runtime.drain_state(target).admission_fenced) throw_error(ErrorCode::FencedWorker, "fenced/drained incarnation");
          ServingUseToken tok;
          tok.incarnation_id = target; tok.incarnation_generation = EngineIncarnationGeneration(1);
          tok.engine_id = EngineId(1); tok.engine_generation = EngineGeneration(1);
          tok.profile_id = ReadinessProfileId(1);
          tok.readiness_generation = ReadinessGeneration((std::uint64_t)u(f,"readiness_gen",1));
          tok.activation_generation = ActivationGeneration((std::uint64_t)u(f,"activation_gen",1));
          tok.execution_id = ExecutionId((std::uint64_t)u(f,"execution_id",1));
          tok.workload_id = WorkloadId((std::uint64_t)u(f,"workload_id",1));
          ServingUseToken acquired = shared.runtime.acquire_serving_use(tok, get(f,"caller","controller"));
          { std::lock_guard<std::mutex> lk(shared.mtx); shared.session_use[sid] = acquired.use_id; }
          ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
          ack.payload = make_payload({{"ok","true"},{"use_id",std::to_string(acquired.use_id.raw())}});
          sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::RELEASE_USE: {
        try {
          EngineIncarnationId target = inc_id;
          if (u(f,"incarnation_id",0) != 0) target = EngineIncarnationId(u(f,"incarnation_id",0));
          if (shared.runtime.incarnation_is_fenced(target) || shared.runtime.drain_state(target).admission_fenced) throw_error(ErrorCode::FencedWorker, "fenced/drained incarnation");
          ServingUseToken tok;
          tok.use_id = ServingUseId((std::uint64_t)u(f,"use_id",0));
          tok.incarnation_id = target; tok.incarnation_generation = EngineIncarnationGeneration(1);
          shared.runtime.release_serving_use(tok, WorkOutcome::COMPLETED, get(f,"detail","released"));
          ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK; ack.payload = make_payload({{"ok","true"}}); sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::DRAIN: {
        EngineIncarnationId target = (u(f,"target_incarnation",0) != 0) ? EngineIncarnationId(u(f,"target_incarnation",0)) : inc_id;
        if (u(f,"target_incarnation",0) != 0) {
          route_drain(shared, sock, f, msg.epoch, msg.authority, target, u(f,"target_worker_boot",0));
          break;
        }
        try { shared.runtime.request_drain(inc_id); ProtocolMessage ack; ack.type = MessageType::DRAIN_RESULT; ack.payload = make_payload({{"ok","true"}}); sock.send_frame(ack); }
        catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::EXECUTE: {
        EngineIncarnationId target = (u(f,"target_incarnation",0) != 0) ? EngineIncarnationId(u(f,"target_incarnation",0)) : inc_id;
        if (u(f,"target_incarnation",0) != 0) {
          route_execute(shared, sock, f, msg.epoch, msg.authority, target, u(f,"target_worker_boot",0));
          break;
        }
        ProtocolMessage ack; ack.type = MessageType::ERROR;
        ack.payload = make_payload({{"code",std::to_string((int)ErrorCode::InvalidArgument)},{"msg","execute requires a target_incarnation"}});
        sock.send_frame(ack);
        break;
      }
      case MessageType::REPLACE: {
        try {
          std::string action = get(f,"replacement_action","");
          if (action == "begin") {
            ReplacementPlan plan;
            plan.engine_id = EngineId(1); plan.engine_generation = EngineGeneration(1);
            plan.old_incarnation = EngineIncarnationId(u(f,"old_incarnation",0));
            plan.old_incarnation_generation = EngineIncarnationGeneration(1);
            plan.candidate_incarnation = EngineIncarnationId(u(f,"candidate_incarnation",0));
            plan.candidate_incarnation_generation = EngineIncarnationGeneration(1);
            plan.epoch = shared.runtime.current_epoch();
            plan.authority = shared.runtime.current_authority();
            plan.make_before_break = (u(f,"make_before_break",1) != 0);
            ReplacementPlan ret = shared.runtime.begin_replacement(plan);
            ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
            ack.payload = make_payload({{"ok","true"},{"replacement_id",std::to_string(ret.replacement_id.raw())},{"phase",to_string_phase(ret.phase)}});
            sock.send_frame(ack);
          } else if (action == "commit") {
            ReplacementPlan ret = shared.runtime.commit_cutover(ReplacementId(u(f,"replacement_id",0)));
            ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
            ack.payload = make_payload({{"ok","true"},{"phase",to_string_phase(ret.phase)}});
            sock.send_frame(ack);
          } else if (action == "retire") {
            ReplacementPlan ret = shared.runtime.retire_old(ReplacementId(u(f,"replacement_id",0)));
            ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
            ack.payload = make_payload({{"ok","true"},{"phase",to_string_phase(ret.phase)}});
            sock.send_frame(ack);
          } else {
            throw_error(ErrorCode::InvalidArgument, "unknown replacement_action");
          }
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::FENCE_WORKER: {
        WorkerId w((std::uint64_t)u(f,"worker_id",1)); WorkerBootId b((std::uint64_t)u(f,"worker_boot",1));
        try { shared.runtime.fence_worker(w, b, get(f,"reason","fenced")); ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK; ack.payload = make_payload({{"ok","true"}}); sock.send_frame(ack); }
        catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::SAVE: {
        std::string path = get(f,"path","er-state.bin");
        try { std::string blob = shared.runtime.serialize(); FILE* fp = fopen(path.c_str(),"wb"); if (fp) { fwrite(blob.data(),1,blob.size(),fp); fclose(fp); } ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK; ack.payload = make_payload({{"ok","true"},{"bytes",std::to_string(blob.size())}}); sock.send_frame(ack); }
        catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::EXECUTION_RESULT: {
        try {
          EngineIncarnationId target = inc_id;
          if (u(f,"incarnation_id",0) != 0) target = EngineIncarnationId(u(f,"incarnation_id",0));
          if (shared.runtime.incarnation_is_fenced(target) || shared.runtime.drain_state(target).admission_fenced) throw_error(ErrorCode::FencedWorker, "fenced/drained incarnation");
          ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK; ack.payload = make_payload({{"ok","true"}}); sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::COMMAND_READY: {
        std::shared_ptr<WorkerRouter> router;
        { std::lock_guard<std::mutex> lk(shared.mtx);
          auto it = shared.session_inc.find(sid);
          if (it != shared.session_inc.end()) { auto rit = shared.worker_router.find(it->second); if (rit != shared.worker_router.end()) router = rit->second; } }
        if (!router) {
          ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)ErrorCode::InvalidState)},{"msg","no worker router for this session"}}); sock.send_frame(ack);
          break;
        }
        ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
        ack.payload = make_payload({{"ok","true"},{"command_ready","true"},{"incarnation_id",std::to_string(router->incarnation().raw())}});
        sock.send_frame(ack);
        std::printf("coordinator: session %llu worker command-ready (incarnation %llu)\n",
                    (unsigned long long)sid, (unsigned long long)router->incarnation().raw());
        fflush(stdout);
        if (!router->adopt(std::move(sock))) break;
        router->run(shared);
        running = false;
        break;
      }
      case MessageType::QUERY_DRAIN: {
        try {
          EngineIncarnationId target = (u(f,"incarnation_id",0) != 0) ? EngineIncarnationId(u(f,"incarnation_id",0)) : inc_id;
          DrainState st = shared.runtime.drain_state(target);
          ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
          ack.payload = make_payload({{"ok","true"},{"drain_phase",to_string(st.phase)},{"active_use",std::to_string(st.active_use)},{"admission_fenced",st.admission_fenced?"1":"0"},{"backend_cleanup_ack",st.backend_cleanup_ack?"1":"0"},{"total_active_use",std::to_string(shared.runtime.active_use_count())}});
          sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::SHUTDOWN: {
        ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK; ack.payload = make_payload({{"ok","true"}}); sock.send_frame(ack);
        running = false;
        break;
      }
      default: { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code","1"},{"msg","unhandled message"}}); sock.send_frame(ack); break; }
    }
  }
  { std::lock_guard<std::mutex> lk(shared.mtx);
    shared.session_inc.erase(sid); shared.session_owner.erase(sid); shared.session_use.erase(sid);
    auto it = shared.worker_router.find(inc_id);
    if (it != shared.worker_router.end() && it->second->session_id() == sid) {
      shared.boot_to_inc.erase({it->second->worker_id().raw(), it->second->worker_boot().raw()});
      shared.worker_router.erase(it);
    }
  }
}

}  // namespace engine_residency
