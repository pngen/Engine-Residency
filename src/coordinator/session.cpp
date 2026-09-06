#include "coordinator/session.hpp"
#include "engine_residency/protocol.hpp"
#include "engine_residency/components.hpp"
#include "engine_residency/activation.hpp"
#include "engine_residency/serving_use.hpp"
#include "engine_residency/engine.hpp"
#include "engine_residency/state.hpp"

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

void run_session(SessionShared& shared, net::TcpSocket sock) {
  std::uint64_t sid = 0;
  { std::lock_guard<std::mutex> lk(shared.mtx); sid = shared.next_session++; }
  bool running = true;
  EngineIncarnationId inc_id;
  while (running) {
    ProtocolMessage msg;
    if (!sock.recv_frame(msg)) break;
    auto f = parse_payload(msg.payload);
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
          { std::lock_guard<std::mutex> lk(shared.mtx); shared.session_inc[sid] = inc_id; shared.session_owner[sid] = get(f,"label","worker"); }
          ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK;
          ack.payload = make_payload({{"ok","true"},{"incarnation_id",std::to_string(registered.incarnation_id.raw())},{"incarnation_generation",std::to_string(registered.incarnation_generation.raw())},{"epoch",std::to_string(shared.runtime.current_epoch().raw())},{"authority",std::to_string(shared.runtime.current_authority().raw())}});
          sock.send_frame(ack);
        } catch (const DomainError& e) {
          ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack);
        }
        break;
      }
      case MessageType::PUBLISH_COMPONENT: {
        try {
          ComponentEvidence ev;
          ev.incarnation_id = shared.session_inc[sid];
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
          ReadinessResult rr = shared.runtime.evaluate_readiness(inc_id, ReadinessProfileId(1));
          ProtocolMessage ack; ack.type = MessageType::PREPARE_RESULT;
          ack.payload = make_payload({{"outcome",std::string(to_string(rr.outcome))}});
          sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::ACTIVATE: {
        try {
          ActivationRequest req;
          req.incarnation_id = inc_id; req.incarnation_generation = EngineIncarnationGeneration(1);
          req.engine_id = EngineId(1); req.engine_generation = EngineGeneration(1);
          req.profile_id = ReadinessProfileId(1); req.profile_generation = ReadinessProfileGeneration(1);
          req.readiness_generation = ReadinessGeneration((std::uint64_t)u(f,"readiness_gen",1));
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
          ServingUseToken tok;
          tok.incarnation_id = inc_id; tok.incarnation_generation = EngineIncarnationGeneration(1);
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
          ServingUseToken tok;
          tok.use_id = ServingUseId((std::uint64_t)u(f,"use_id",0));
          tok.incarnation_id = inc_id; tok.incarnation_generation = EngineIncarnationGeneration(1);
          shared.runtime.release_serving_use(tok, WorkOutcome::COMPLETED, get(f,"detail","released"));
          ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK; ack.payload = make_payload({{"ok","true"}}); sock.send_frame(ack);
        } catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
        break;
      }
      case MessageType::DRAIN: {
        try { shared.runtime.request_drain(inc_id); ProtocolMessage ack; ack.type = MessageType::DRAIN_RESULT; ack.payload = make_payload({{"ok","true"}}); sock.send_frame(ack); }
        catch (const DomainError& e) { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code",std::to_string((int)e.code())},{"msg",e.what()}}); sock.send_frame(ack); }
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
      case MessageType::SHUTDOWN: {
        ProtocolMessage ack; ack.type = MessageType::REGISTER_ACK; ack.payload = make_payload({{"ok","true"}}); sock.send_frame(ack);
        running = false;
        break;
      }
      default: { ProtocolMessage ack; ack.type = MessageType::ERROR; ack.payload = make_payload({{"code","1"},{"msg","unhandled message"}}); sock.send_frame(ack); break; }
    }
  }
  { std::lock_guard<std::mutex> lk(shared.mtx); shared.session_inc.erase(sid); shared.session_owner.erase(sid); }
}

}  // namespace engine_residency
