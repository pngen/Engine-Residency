#include "engine_residency/net.hpp"
#include "engine_residency/protocol.hpp"
#include "engine_residency/components.hpp"
#include "engine_residency/engine.hpp"
#include "spawn.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <thread>

using namespace engine_residency;
using engine_residency::detail::make_payload;
using engine_residency::detail::parse_payload;
using engine_residency::detail::get;

static std::uint64_t u(const std::map<std::string,std::string>& f,const std::string& k,std::uint64_t d=0){
  auto it=f.find(k); if(it==f.end()) return d; try{ return std::stoull(it->second);}catch(...){return d;}
}
static bool send_msg(net::TcpSocket& s, MessageType t, std::uint64_t ep, std::uint64_t au, const std::map<std::string,std::string>& f){
  ProtocolMessage m; m.type=t; m.epoch=(std::uint32_t)ep; m.authority=(std::uint32_t)au; m.payload=make_payload(f); return s.send_frame(m);
}
static bool recv_msg(net::TcpSocket& s, ProtocolMessage& out){ return s.recv_frame(out); }

int main() {
  const std::uint16_t port = 27312;
  if (!net::init()) { return 1; }
  if (!er_spawn_process("er_coordinator.exe", std::to_string(port))) { std::printf("cannot spawn coordinator\n"); return 1; }
  net::TcpSocket sock; bool connected=false;
  for (int i=0;i<80;++i){ if (sock.connect("127.0.0.1",port)){ connected=true; break;} std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
  if(!connected){ er_kill_process(); std::printf("coordinator not reachable\n"); return 1; }
  std::uint64_t ep=0, au=0; ProtocolMessage mr;

  send_msg(sock, MessageType::HELLO, 0, 0, {{"label","integration"}}); recv_msg(sock, mr);
  send_msg(sock, MessageType::REGISTER, ep, au, {{"worker_id","1"},{"worker_boot","1"},{"source_id","1"},{"source_boot","1"},{"pid","100"},{"label","A"}});
  recv_msg(sock, mr);
  if (mr.type == MessageType::ERROR) { er_kill_process(); std::printf("FAIL: register A rejected\n"); return 1; }
  auto reg = parse_payload(mr.payload); std::uint64_t incA = u(reg,"incarnation_id",0);
  std::printf("integration: registered A incarnation=%llu\n",(unsigned long long)incA);

  auto pub=[&](int cat,int st,const std::string& subj,const std::string& detail,const std::map<std::string,std::string>& extra){
    std::map<std::string,std::string> f={{"category",std::to_string(cat)},{"state",std::to_string(st)},{"subject",subj},{"detail",detail},{"incarnation_id",std::to_string(incA)}};
    for(auto& kv:extra) f[kv.first]=kv.second;
    send_msg(sock, MessageType::PUBLISH_COMPONENT, 0, 0, f); recv_msg(sock, mr);
  };
  pub((int)ComponentCategory::BACKEND,(int)ComponentState::VERIFIED,"backend-reference","b",{});
  pub((int)ComponentCategory::DEVICE_CONTEXT,(int)ComponentState::BOUND,"device-context-0","d",{});
  pub((int)ComponentCategory::MODEL,(int)ComponentState::VERIFIED,"reference-model","m",{{"model_gen","1"},{"compat","ref/v1"}});
  pub((int)ComponentCategory::KV_CAPACITY,(int)ComponentState::BOUND,"kv-main","k",{{"kv_gen","1"}});
  pub((int)ComponentCategory::KERNEL,(int)ComponentState::VERIFIED,"er-reference-kernel","kr",{{"kernel_gen","1"},{"compat","ref/v1"}});
  pub((int)ComponentCategory::GRAPH,(int)ComponentState::VERIFIED,"er-reference-graph","g",{{"graph_gen","1"}});
  pub((int)ComponentCategory::WARMUP,(int)ComponentState::VERIFIED,"warmup","w",{});

  send_msg(sock, MessageType::QUERY_READINESS, 0, 0, {}); recv_msg(sock, mr);
  std::string outcome = get(parse_payload(mr.payload),"outcome","UNKNOWN");
  std::printf("integration: readiness=%s\n",outcome.c_str());
  if(outcome!="READY"){ er_kill_process(); std::printf("FAIL: expected READY\n"); return 1; }

  std::uint64_t rgen=1;
  send_msg(sock, MessageType::ACQUIRE_USE, 0, 0, {{"readiness_gen",std::to_string(rgen)},{"execution_id","1"},{"workload_id","1"},{"caller","integration"}});
  recv_msg(sock, mr); std::uint64_t use_id=u(parse_payload(mr.payload),"use_id",0);
  if(use_id==0){ er_kill_process(); std::printf("FAIL: acquire use failed\n"); return 1; }
  send_msg(sock, MessageType::RELEASE_USE, 0, 0, {{"use_id",std::to_string(use_id)}}); recv_msg(sock, mr);

  // Re-register A under same boot -> duplicate rejected.
  send_msg(sock, MessageType::REGISTER, 0, 0, {{"worker_id","1"},{"worker_boot","1"},{"source_id","1"},{"source_boot","1"},{"pid","100"},{"label","A"}});
  recv_msg(sock, mr);
  if (mr.type != MessageType::ERROR) { er_kill_process(); std::printf("FAIL: duplicate register accepted\n"); return 1; }
  std::printf("integration: duplicate register rejected\n");

  // Fence A then re-register under fenced boot -> rejected.
  send_msg(sock, MessageType::FENCE_WORKER, 0, 0, {{"worker_id","1"},{"worker_boot","1"},{"reason","worker died"}}); recv_msg(sock, mr);
  send_msg(sock, MessageType::REGISTER, 0, 0, {{"worker_id","1"},{"worker_boot","1"},{"source_id","1"},{"source_boot","1"},{"pid","100"},{"label","A"}});
  recv_msg(sock, mr);
  if (mr.type != MessageType::ERROR) { er_kill_process(); std::printf("FAIL: fenced re-register accepted\n"); return 1; }
  std::printf("integration: fenced re-register rejected\n");

  // Replacement A-prime (new boot) registers OK.
  send_msg(sock, MessageType::REGISTER, 0, 0, {{"worker_id","1"},{"worker_boot","2"},{"source_id","1"},{"source_boot","2"},{"pid","300"},{"label","A-prime"}});
  recv_msg(sock, mr);
  if (mr.type == MessageType::ERROR) { er_kill_process(); std::printf("FAIL: replacement register rejected\n"); return 1; }
  std::printf("integration: replacement registered\n");

  er_kill_process();
  net::shutdown();
  std::printf("integration tests pass\n");
  return 0;
}
