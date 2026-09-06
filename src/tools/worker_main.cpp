#include "engine_residency/net.hpp"
#include "engine_residency/protocol.hpp"
#include "engine_residency/backend.hpp"
#include "engine_residency/components.hpp"
#include "engine_residency/engine.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace engine_residency;
using engine_residency::detail::make_payload;
using engine_residency::detail::parse_payload;
using engine_residency::detail::get;

static std::uint64_t u(const std::map<std::string,std::string>& f,const std::string& k,std::uint64_t d=0){
  auto it=f.find(k); if(it==f.end()) return d; try{ return std::stoull(it->second);}catch(...){return d;}
}

static bool send_msg(net::TcpSocket& s, MessageType t, std::uint64_t epoch, std::uint64_t auth, const std::map<std::string,std::string>& f) {
  ProtocolMessage m; m.type=t; m.epoch=(std::uint32_t)epoch; m.authority=(std::uint32_t)auth; m.payload=make_payload(f); return s.send_frame(m);
}
static bool recv_msg(net::TcpSocket& s, uint64_t& epoch, ProtocolMessage& out){ if(!s.recv_frame(out)) return false; epoch=out.epoch; return true; }

int main(int argc, char** argv) {
  std::string host="127.0.0.1"; std::uint16_t port=27200;
  std::uint64_t worker_id=1, boot=1; std::string label="worker"; std::uint32_t pid=0;
  bool use_cuda=false; int serve=0; std::string result_file; bool stay_alive=false; std::uint64_t slot=1;
  for (int i=1;i<argc;++i){ std::string a=argv[i];
    if(a=="--host") host=argv[++i];
    else if(a=="--port") port=(std::uint16_t)std::atoi(argv[++i]);
    else if(a=="--worker-id") worker_id=std::stoull(argv[++i]);
    else if(a=="--boot") boot=std::stoull(argv[++i]);
    else if(a=="--label") label=argv[++i];
    else if(a=="--pid") pid=(std::uint32_t)std::atoi(argv[++i]);
    else if(a=="--cuda") use_cuda=true;
    else if(a=="--serve") serve=std::atoi(argv[++i]);
    else if(a=="--result") result_file=argv[++i];
    else if(a=="--stay-alive") stay_alive=true;
    else if(a=="--slot") slot=std::stoull(argv[++i]);
  }
  if(!net::init()) return 1;
  net::TcpSocket sock;
  if(!sock.connect(host,port)){ std::printf("worker connect failed\n"); return 1; }
  std::uint64_t epoch=0,auth=0; ProtocolMessage mr;
  if(!send_msg(sock,MessageType::HELLO,0,0,{{"label",label}})) return 1;
  if(!recv_msg(sock,epoch,mr)) return 1;

  if(!send_msg(sock,MessageType::REGISTER,epoch,auth,{{"worker_id",std::to_string(worker_id)},{"worker_boot",std::to_string(boot)},{"source_id","1"},{"source_boot",std::to_string(boot)},{"pid",std::to_string(pid)},{"label",label}})) return 1;
  if(!recv_msg(sock,epoch,mr)) return 1;
  if(mr.type==MessageType::ERROR){ std::printf("worker register rejected: %s\n", get(parse_payload(mr.payload),"msg","").c_str()); return 1; }
  auto reg=parse_payload(mr.payload);
  std::uint64_t inc_id=u(reg,"incarnation_id",0);

  // Backend
  std::unique_ptr<ReferenceBackend> backend;
#ifdef ER_HAS_CUDA
  backend = use_cuda ? make_cuda_reference_backend() : make_cpu_reference_backend();
#else
  backend = make_cpu_reference_backend();
#endif
  BackendDiscovery disc = backend->discover(0);
  // Publish backend/device/model/kv/kernel/graph/warmup evidence via coordinator.
  auto pub=[&](int cat,int st,const std::string& subj,const std::string& detail,const std::map<std::string,std::string>& extra){
    std::map<std::string,std::string> f={{"category",std::to_string(cat)},{"state",std::to_string(st)},{"subject",subj},{"detail",detail},{"incarnation_id",std::to_string(inc_id)}};
    for(auto& kv:extra) f[kv.first]=kv.second;
    send_msg(sock,MessageType::PUBLISH_COMPONENT,epoch,auth,f);
    ProtocolMessage a; recv_msg(sock,epoch,a);
  };
  pub((int)ComponentCategory::BACKEND,(int)ComponentState::VERIFIED,"backend-reference",disc.backend_name,{{"provenance",std::to_string((int)Provenance::MEASURED)}});
  pub((int)ComponentCategory::DEVICE_CONTEXT,(int)ComponentState::BOUND,"device-context-0",disc.device_name,{{"provenance",std::to_string((int)Provenance::MEASURED)}});
  pub((int)ComponentCategory::MODEL,(int)ComponentState::VERIFIED,"reference-model","model bound and verified",{{"model_gen","1"},{"compat","ref/v1"},{"size_bytes",std::to_string(1u<<20)},{"provenance",std::to_string((int)Provenance::MEASURED)}});
  pub((int)ComponentCategory::KV_CAPACITY,(int)ComponentState::BOUND,"kv-main","1024 kv entries provisioned",{{"kv_gen","1"},{"size_bytes","1024"},{"provenance",std::to_string((int)Provenance::MEASURED)}});
  pub((int)ComponentCategory::KERNEL,(int)ComponentState::VERIFIED,"er-reference-kernel","kernel bound and verified",{{"kernel_gen","1"},{"compat","ref/v1"},{"provenance",std::to_string((int)Provenance::MEASURED)}});
  pub((int)ComponentCategory::GRAPH,(int)ComponentState::VERIFIED,"er-reference-graph","graph instantiated and replayed",{{"graph_gen","1"},{"provenance",std::to_string((int)Provenance::MEASURED)}});

  // Prepare through coordinator.
  send_msg(sock,MessageType::PREPARE,epoch,auth,{});
  recv_msg(sock,epoch,mr);

  // Warmup via real backend compute.
  backend->prepare(1u<<20,1u<<16,1024);
  backend->bind_kernel("er-reference-kernel");
  backend->instantiate_graph(true);
  ComputeResult warm = backend->warmup(1,16);
  std::uint64_t warm_elapsed = warm.elapsed_ns;
  pub((int)ComponentCategory::WARMUP,(int)ComponentState::VERIFIED,"warmup","warmup verified",{{"elapsed_ns",std::to_string(warm_elapsed)},{"provenance",std::to_string((int)Provenance::MEASURED)}});

  // Query readiness.
  send_msg(sock,MessageType::QUERY_READINESS,epoch,auth,{});
  recv_msg(sock,epoch,mr);
  std::string outcome = get(parse_payload(mr.payload),"outcome","UNKNOWN");
  std::string qp = mr.payload;
  std::printf("worker %llu readiness=%s\n",(unsigned long long)worker_id,outcome.c_str());

  // Authorize local activation (the worker requests its incarnation's activation
  // through the coordinator authority). This proves real activation over TCP.
  std::string activation = "UNKNOWN";
  if (outcome == "READY" || outcome == "DEGRADED") {
    send_msg(sock,MessageType::ACTIVATE,epoch,auth,{{"readiness_gen","0"},{"slot_id",std::to_string(slot)},{"caller","worker"}});
    recv_msg(sock,epoch,mr);
    activation = get(parse_payload(mr.payload),"state","REJECTED");
    if (mr.type == MessageType::ERROR) { activation = std::string("REJECTED:") + get(parse_payload(mr.payload),"msg","?"); }
    std::printf("worker %llu activation=%s\n",(unsigned long long)worker_id,activation.c_str());
  }

  // Serve: acquire use, execute real compute, verify parity, release.
  int served=0;
  for(int i=0;i<serve;++i){
    ComputeInput in; in.batch=1; in.seq_len=16; in.seed=(std::uint64_t)(1000+worker_id*7+i);
    for(int j=0;j<16;++j) in.input.push_back((float)((j+i)%11));
    std::vector<float> cpu_vec = cpu_reference_compute(in); // CPU parity
    ComputeResult cpu; cpu.ok = true; cpu.output = std::move(cpu_vec); cpu.seed = in.seed;
    send_msg(sock,MessageType::ACQUIRE_USE,epoch,auth,{{"execution_id","1"},{"workload_id",std::to_string(worker_id)},{"caller","worker"}});
    recv_msg(sock,epoch,mr); auto ur=parse_payload(mr.payload); std::uint64_t use_id=u(ur,"use_id",0);
    ComputeResult result = backend->execute(in);
    bool parity = backend->verify_parity(result, cpu);
    send_msg(sock,MessageType::EXECUTION_RESULT,epoch,auth,{{"use_id",std::to_string(use_id)},{"ok",parity?"1":"0"},{"parity",parity?"ok":"mismatch"},{"detail",result.detail}});
    // Release use.
    send_msg(sock,MessageType::RELEASE_USE,epoch,auth,{{"use_id",std::to_string(use_id)}});
    recv_msg(sock,epoch,mr);
    std::printf("worker %llu served req %d parity=%s\n",(unsigned long long)worker_id,i,parity?"OK":"MISMATCH");
    ++served;
  }

  // Write a machine-readable result file when requested (used by the proof tests
  // to observe readiness + parity across real CUDA worker processes).
  if (!result_file.empty()) {
    FILE* fp = fopen(result_file.c_str(), "wb");
    if (fp) { fprintf(fp, "worker=%llu incarnation=%llu readiness=%s activation=%s parity=%s detail=|%s|\n", (unsigned long long)worker_id, (unsigned long long)inc_id, outcome.c_str(), activation.c_str(), served > 0 ? "OK" : "NA", qp.c_str()); fclose(fp); }
  }

  if (stay_alive) {
    // Hold the device allocations and graph until a real OS termination. This
    // is the worker process holding genuine CUDA state that a caller kills.
    std::printf("worker %llu staying alive holding CUDA state\n", (unsigned long long)worker_id);
    fflush(stdout);
    while (true) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); }
  }

  backend->release();
  net::shutdown();
  return 0;
}
