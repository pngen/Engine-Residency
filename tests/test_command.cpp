#include "engine_residency/net.hpp"
#include "engine_residency/protocol.hpp"
#include "engine_residency/components.hpp"
#include "spawn.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

using namespace engine_residency;
using engine_residency::detail::make_payload;
using engine_residency::detail::parse_payload;
using engine_residency::detail::get;

static bool send_msg(net::TcpSocket& s, MessageType t, std::uint64_t ep, std::uint64_t au, const std::map<std::string,std::string>& f){
  ProtocolMessage m; m.type=t; m.epoch=(std::uint32_t)ep; m.authority=(std::uint32_t)au; m.payload=make_payload(f); return s.send_frame(m);
}
static bool recv_msg(net::TcpSocket& s, ProtocolMessage& out){ return s.recv_frame(out); }

static bool file_contains(const std::string& path, const std::string& needle, std::uint32_t timeout_ms) {
  for (std::uint32_t t = 0; t < timeout_ms; t += 50) {
    std::ifstream in(path);
    if (in) { std::stringstream ss; ss << in.rdbuf(); if (ss.str().find(needle) != std::string::npos) return true; }
    er_sleep(50);
  }
  return false;
}
static std::string file_read(const std::string& path) { std::ifstream in(path); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static void file_create(const std::string& path){ FILE* fp=fopen(path.c_str(),"wb"); if(fp){ fwrite("1",1,1,fp); fclose(fp);} }
static std::uint64_t field_uint(const std::string& s, const std::string& key) {
  std::size_t p = s.find(key + "=");
  if (p == std::string::npos) return 0;
  std::size_t q = s.find(" ", p);
  std::string v = s.substr(p + key.size() + 1, q == std::string::npos ? std::string::npos : q - (p + key.size() + 1));
  return std::strtoull(v.c_str(), nullptr, 10);
}

static int g_fail = 0;
#define REQUIRE(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", (msg)); ++g_fail; } } while(0)

static bool wait_reachable(const std::uint16_t port) {
  for (int i=0;i<120;++i){ net::TcpSocket s; if (s.connect("127.0.0.1",port)) return true; er_sleep(50); }
  return false;
}

static std::string cpargs(bool use_cuda){ return use_cuda ? "--cuda" : ""; }

// ---------------------------------------------------------------------------
// Real-process drain: a coordinator + one command-mode worker, with an explicit
// event barrier to hold an admitted use outstanding while the drain is requested.
// ---------------------------------------------------------------------------
static void run_drain_proof(bool use_cuda, int tag) {
  const std::uint16_t port = (std::uint16_t)(27430 + tag + 1);
  std::string bar = "drain_hold_barrier_" + std::to_string(tag) + ".flag";
  std::string hold_marker = "drain_hold_" + std::to_string(tag);
  std::string cleanup_marker = "drain_cleanup_" + std::to_string(tag) + ".txt";
  std::string ready = "drain_ready_" + std::to_string(tag) + ".txt";
  std::remove(bar.c_str()); std::remove(hold_marker.c_str()); std::remove((hold_marker + ".executing").c_str()); std::remove(cleanup_marker.c_str()); std::remove(ready.c_str());

  ErProcess cp, wa;
  if (!er_spawn(ER_COORDINATOR_LOCATION, std::to_string(port), cp)) { std::printf("drain: coordinator spawn failed\n"); ++g_fail; return; }
  if (!wait_reachable(port)) { er_kill(cp); std::printf("drain: coordinator unreachable\n"); ++g_fail; return; }
  std::string pp = std::string("--port ") + std::to_string(port) + " " + cpargs(use_cuda);
  if (!er_spawn(ER_WORKER_LOCATION, (pp + " --worker-id 1 --boot 1 --label A --pid 100 --command --ready-marker " + ready).c_str(), wa)) {
    er_kill(cp); std::printf("drain: worker spawn failed\n"); ++g_fail; return;
  }
  if (!file_contains(ready, "incarnation=", 40000)) { er_kill(wa); er_kill(cp); std::printf("drain: worker never command-ready\n"); ++g_fail; return; }
  std::uint64_t inc = field_uint(file_read(ready), "incarnation");
  std::printf("drain: worker incarnation=%llu\n",(unsigned long long)inc);
  REQUIRE(inc != 0, "drain: worker incarnation id reported");

  // Controller connection 1: EXECUTE (with an event barrier so the use stays
  // outstanding). Runs in a background thread because it blocks until release.
  std::atomic<bool> t1_done{false}; std::string t1_reply;
  std::thread t1([&]{
    net::TcpSocket s; if(!s.connect("127.0.0.1",port)){ t1_done=true; return; }
    ProtocolMessage m; send_msg(s,MessageType::HELLO,0,0,{{"label","ctl-exec"}}); recv_msg(s,m);
    send_msg(s,MessageType::EXECUTE,0,0,{{"target_incarnation",std::to_string(inc)},{"target_worker_boot","1"},{"cmd_id","1"},{"seed","42"},{"batch","1"},{"seq_len","16"},{"hold_barrier",bar},{"marker",hold_marker}});
    recv_msg(s,m);
    t1_reply = m.payload; t1_done=true;
  });

  // Wait until the worker has admitted a use and is holding (real CUDA compute
  // outstanding, buffers live).
  if (!file_contains(hold_marker + ".executing", "incarnation=", 40000)) {
    t1.join(); er_kill(wa); er_kill(cp); std::printf("drain: execute never started/held\n"); ++g_fail; return;
  }
  std::printf("drain: use admitted and outstanding\n");

  // Controller connection 2: DRAIN requested while the use is outstanding.
  std::atomic<bool> t2_done{false}; std::string t2_reply;
  std::thread t2([&]{
    net::TcpSocket s; if(!s.connect("127.0.0.1",port)){ t2_done=true; return; }
    ProtocolMessage m; send_msg(s,MessageType::HELLO,0,0,{{"label","ctl-drain"}}); recv_msg(s,m);
    send_msg(s,MessageType::DRAIN,0,0,{{"target_incarnation",std::to_string(inc)},{"target_worker_boot","1"},{"cmd_id","2"},{"marker",cleanup_marker}});
    recv_msg(s,m);
    t2_reply = m.payload; t2_done=true;
  });

  // Controller connection 3: verify new admission is fenced while the use is
  // outstanding, and then confirm the drain lifecycle state.
  net::TcpSocket ctl;
  bool connected=false;
  for (int i=0;i<40;++i){ if(ctl.connect("127.0.0.1",port)){ connected=true; break;} er_sleep(50); }
  if (connected) {
    ProtocolMessage m; send_msg(ctl,MessageType::HELLO,0,0,{{"label","ctl3"}}); recv_msg(ctl,m);
    // Poll until request_drain has been applied (phase leaves NONE).
    std::string phase="NONE"; bool fenced=false;
    for (int i=0;i<200 && phase=="NONE";++i){
      send_msg(ctl,MessageType::QUERY_DRAIN,0,0,{{"incarnation_id",std::to_string(inc)}}); recv_msg(ctl,m);
      auto qp=parse_payload(m.payload); phase=get(qp,"drain_phase","NONE"); fenced=(get(qp,"admission_fenced","0")=="1");
      er_sleep(20);
    }
    std::printf("drain: after request phase=%s fenced=%d\n",phase.c_str(),fenced?1:0);
    REQUIRE(phase=="ACTIVE_USE_REMAINING" || phase=="REQUESTED" || phase=="ADMISSION_FENCED", "drain: drain requested while use outstanding");
    REQUIRE(fenced, "drain: admission fenced under outstanding use");
    // New serving-use acquisition rejects.
    send_msg(ctl,MessageType::ACQUIRE_USE,0,0,{{"target_incarnation",std::to_string(inc)},{"readiness_gen","1"},{"execution_id","99"},{"workload_id","99"},{"caller","ctl"}});
    recv_msg(ctl,m);
    std::printf("drain: new acquire rejected=%d\n", m.type==MessageType::ERROR?1:0);
    REQUIRE(m.type==MessageType::ERROR, "drain: new serving-use acquisition rejects after drain request");
  } else {
    REQUIRE(false, "drain: ctl3 connect failed");
  }

  // Release the event barrier so the admitted work completes and the use closes.
  file_create(bar);

  t1.join();
  auto t1f = parse_payload(t1_reply);
  std::printf("drain: execute reply ok=%d parity=%s type=%d\n", t1_reply.empty()?0:(get(t1f,"ok","0")=="1"?1:0), get(t1f,"parity","?").c_str(), t1_reply.empty()?0:1);
  REQUIRE(!t1_reply.empty(), "drain: execute reply received");
  REQUIRE(get(t1f,"parity","?")=="ok", "drain: admitted work completes with CPU parity (buffers/graph valid while use outstanding)");

  t2.join();
  auto t2f = parse_payload(t2_reply);
  std::printf("drain: drain reply ok=%d phase=%s\n", get(t2f,"ok","0")=="1"?1:0, get(t2f,"drain_phase","?").c_str());
  REQUIRE(get(t2f,"drain_phase","?")=="DRAINED", "drain: DRAINED reached after use closure + backend cleanup ack");

  REQUIRE(file_contains(cleanup_marker, "cleanup done", 20000), "drain: worker performed actual backend cleanup");

  // Stale release/completion/ack reflect rejection into the (now drained) inc.
  { net::TcpSocket s; if(s.connect("127.0.0.1",port)){ ProtocolMessage m; send_msg(s,MessageType::HELLO,0,0,{{"label","ctl-stale"}}); recv_msg(s,m);
    send_msg(s,MessageType::RELEASE_USE,0,0,{{"incarnation_id",std::to_string(inc)},{"use_id","1"}}); recv_msg(s,m);
    REQUIRE(m.type==MessageType::ERROR, "drain: stale release rejected");
    send_msg(s,MessageType::EXECUTION_RESULT,0,0,{{"incarnation_id",std::to_string(inc)},{"use_id","1"},{"ok","1"}}); recv_msg(s,m);
    REQUIRE(m.type==MessageType::ERROR, "drain: stale completion rejected");
    send_msg(s,MessageType::EXECUTE,0,0,{{"target_incarnation",std::to_string(inc)},{"cmd_id","9"},{"seed","1"},{"batch","1"},{"seq_len","16"}}); recv_msg(s,m);
    REQUIRE(m.type==MessageType::ERROR, "drain: stale execute (post-drain) rejected");
  } }

  er_kill(wa); er_kill(cp);
  std::printf("drain proof complete (tag=%d)\n", tag);
}

// ---------------------------------------------------------------------------
// Real-process make-before-break replacement: distinct old + candidate CUDA
// worker processes, authorized cutover, old drained/retired, replacement serves.
// ---------------------------------------------------------------------------
static void run_replacement_proof(bool use_cuda, int tag) {
  const std::uint16_t port = (std::uint16_t)(27450 + tag + 1);
  std::string old_ready = "repl_old_ready_" + std::to_string(tag) + ".txt";
  std::string cand_ready = "repl_cand_ready_" + std::to_string(tag) + ".txt";
  std::string old_cleanup = "repl_old_cleanup_" + std::to_string(tag) + ".txt";
  std::remove(old_ready.c_str()); std::remove(cand_ready.c_str()); std::remove(old_cleanup.c_str());

  ErProcess cp, oldp, candp;
  if (!er_spawn(ER_COORDINATOR_LOCATION, std::to_string(port), cp)) { std::printf("repl: coordinator spawn failed\n"); ++g_fail; return; }
  if (!wait_reachable(port)) { er_kill(cp); std::printf("repl: coordinator unreachable\n"); ++g_fail; return; }
  std::string pp = std::string("--port ") + std::to_string(port) + " " + cpargs(use_cuda);
  if (!er_spawn(ER_WORKER_LOCATION, (pp + " --worker-id 1 --boot 1 --label old --pid 100 --command --ready-marker " + old_ready).c_str(), oldp)) { er_kill(cp); std::printf("repl: old spawn failed\n"); ++g_fail; return; }
  if (!file_contains(old_ready, "incarnation=", 40000)) { er_kill(oldp); er_kill(cp); std::printf("repl: old never ready\n"); ++g_fail; return; }
  // Candidate: separate process-local state, prepared+ready but NOT current.
  if (!er_spawn(ER_WORKER_LOCATION, (pp + " --worker-id 2 --boot 1 --label cand --pid 200 --no-activate --command --ready-marker " + cand_ready).c_str(), candp)) { er_kill(oldp); er_kill(cp); std::printf("repl: cand spawn failed\n"); ++g_fail; return; }
  if (!file_contains(cand_ready, "incarnation=", 40000)) { er_kill(candp); er_kill(oldp); er_kill(cp); std::printf("repl: cand never ready\n"); ++g_fail; return; }
  std::uint64_t old_inc = field_uint(file_read(old_ready), "incarnation");
  std::uint64_t cand_inc = field_uint(file_read(cand_ready), "incarnation");
  std::printf("repl: old=%llu cand=%llu\n",(unsigned long long)old_inc,(unsigned long long)cand_inc);
  REQUIRE(old_inc!=0 && cand_inc!=0 && old_inc!=cand_inc, "repl: distinct old and candidate incarnations");

  net::TcpSocket ctl;
  if (!ctl.connect("127.0.0.1",port)) { er_kill(candp); er_kill(oldp); er_kill(cp); std::printf("repl: controller connect failed\n"); ++g_fail; return; }
  ProtocolMessage m;
  send_msg(ctl,MessageType::HELLO,0,0,{{"label","repl-ctl"}}); recv_msg(ctl,m);

  // Authorized make-before-break begin.
  send_msg(ctl,MessageType::REPLACE,0,0,{{"replacement_action","begin"},{"old_incarnation",std::to_string(old_inc)},{"candidate_incarnation",std::to_string(cand_inc)},{"make_before_break","1"}});
  recv_msg(ctl,m);
  std::string rid_s = get(parse_payload(m.payload),"replacement_id","0");
  std::printf("repl: begin phase=%s rid=%s\n", get(parse_payload(m.payload),"phase","?").c_str(), rid_s.c_str());
  REQUIRE(m.type!=MessageType::ERROR && rid_s!="0", "repl: authorized begin_replacement accepted");
  // Verify candidate prepared/ready and old still current (make-before-break).
  send_msg(ctl,MessageType::QUERY_READINESS,0,0,{{"incarnation_id",std::to_string(cand_inc)}}); recv_msg(ctl,m);
  REQUIRE(get(parse_payload(m.payload),"outcome","?")=="READY", "repl: candidate readiness verified");

  // Authorized cutover.
  send_msg(ctl,MessageType::REPLACE,0,0,{{"replacement_action","commit"},{"replacement_id",rid_s}}); recv_msg(ctl,m);
  REQUIRE(m.type!=MessageType::ERROR, "repl: authorized commit_cutover accepted");

  // Replacement executes authorized CUDA work with parity.
  send_msg(ctl,MessageType::EXECUTE,0,0,{{"target_incarnation",std::to_string(cand_inc)},{"target_worker_boot","1"},{"cmd_id","1"},{"seed","77"},{"batch","1"},{"seq_len","32"}}); recv_msg(ctl,m);
  auto exf=parse_payload(m.payload);
  std::printf("repl: candidate execute parity=%s ok=%d\n", get(exf,"parity","?").c_str(), m.type!=MessageType::ERROR?1:0);
  REQUIRE(m.type!=MessageType::ERROR && get(exf,"parity","?")=="ok", "repl: replacement executes authorized CUDA work with CPU parity");

  // Old backend drained and cleaned (worker performs physical cleanup + ack).
  send_msg(ctl,MessageType::DRAIN,0,0,{{"target_incarnation",std::to_string(old_inc)},{"target_worker_boot","1"},{"cmd_id","2"},{"marker",old_cleanup}}); recv_msg(ctl,m);
  auto drf=parse_payload(m.payload);
  std::printf("repl: old drain phase=%s\n", get(drf,"drain_phase","?").c_str());
  REQUIRE(m.type!=MessageType::ERROR && get(drf,"drain_phase","?")=="DRAINED", "repl: old backend drained+cleaned, cleanup ack reaches coordinator");
  REQUIRE(file_contains(old_cleanup, "cleanup done", 20000), "repl: old worker performed actual backend cleanup");

  // Retire old.
  send_msg(ctl,MessageType::REPLACE,0,0,{{"replacement_action","retire"},{"replacement_id",rid_s}}); recv_msg(ctl,m);
  std::printf("repl: retire phase=%s\n", get(parse_payload(m.payload),"phase","?").c_str());
  REQUIRE(m.type!=MessageType::ERROR, "repl: old incarnation retires");

  // Old commands/results cannot affect current authority.
  send_msg(ctl,MessageType::EXECUTE,0,0,{{"target_incarnation",std::to_string(old_inc)},{"target_worker_boot","1"},{"cmd_id","5"},{"seed","1"},{"batch","1"},{"seq_len","16"}}); recv_msg(ctl,m);
  std::printf("repl: old execute rejected=%d\n", m.type==MessageType::ERROR?1:0);
  REQUIRE(m.type==MessageType::ERROR, "repl: old executes cannot affect current authority");
  send_msg(ctl,MessageType::PUBLISH_COMPONENT,0,0,{{"incarnation_id",std::to_string(old_inc)},{"category","2"},{"state","5"},{"subject","reference-model"},{"model_gen","1"},{"compat","ref/v1"}}); recv_msg(ctl,m);
  REQUIRE(m.type==MessageType::ERROR, "repl: old component publication rejected (fenced)");

  // Replacement still serves with parity after the old is retired.
  send_msg(ctl,MessageType::EXECUTE,0,0,{{"target_incarnation",std::to_string(cand_inc)},{"target_worker_boot","1"},{"cmd_id","6"},{"seed","101"},{"batch","1"},{"seq_len","32"}}); recv_msg(ctl,m);
  auto ex2=parse_payload(m.payload);
  std::printf("repl: post-retire candidate execute parity=%s\n", get(ex2,"parity","?").c_str());
  REQUIRE(m.type!=MessageType::ERROR && get(ex2,"parity","?")=="ok", "repl: replacement still serves with parity after old retirement");

  er_kill(candp); er_kill(oldp); er_kill(cp);
  std::printf("replacement proof complete (tag=%d)\n", tag);
}

// ---------------------------------------------------------------------------
// Hardening/ownership regression: stale epoch/boot, worker death before cleanup
// acknowledgment (no leak, no false DRAINED), and unsolicited acknowledgment.
// ---------------------------------------------------------------------------
static void run_hardening_proof(bool use_cuda, int tag) {
  const std::uint16_t port = (std::uint16_t)(27470 + tag + 1);
  std::string ready = "hard_ready_" + std::to_string(tag) + ".txt";
  std::string bar = "hard_barrier_" + std::to_string(tag) + ".flag";
  std::string hm = "hard_hold_" + std::to_string(tag);
  std::remove(ready.c_str()); std::remove(bar.c_str()); std::remove((hm + ".executing").c_str());

  ErProcess cp, wa;
  if (!er_spawn(ER_COORDINATOR_LOCATION, std::to_string(port), cp)) { std::printf("hard: coordinator spawn failed\n"); ++g_fail; return; }
  if (!wait_reachable(port)) { er_kill(cp); std::printf("hard: coordinator unreachable\n"); ++g_fail; return; }
  std::string pp = std::string("--port ") + std::to_string(port) + " " + cpargs(use_cuda);
  if (!er_spawn(ER_WORKER_LOCATION, (pp + " --worker-id 1 --boot 1 --label H --pid 100 --command --ready-marker " + ready).c_str(), wa)) { er_kill(cp); std::printf("hard: worker spawn failed\n"); ++g_fail; return; }
  if (!file_contains(ready, "incarnation=", 40000)) { er_kill(wa); er_kill(cp); std::printf("hard: worker never ready\n"); ++g_fail; return; }
  std::uint64_t inc = field_uint(file_read(ready), "incarnation");

  net::TcpSocket ctl;
  if (!ctl.connect("127.0.0.1",port)) { er_kill(wa); er_kill(cp); std::printf("hard: ctl connect failed\n"); ++g_fail; return; }
  ProtocolMessage m;
  send_msg(ctl,MessageType::HELLO,0,0,{{"label","hard-ctl"}}); recv_msg(ctl,m);

  // 1) stale-epoch command rejected without mutating state.
  send_msg(ctl,MessageType::EXECUTE,999,0,{{"target_incarnation",std::to_string(inc)},{"cmd_id","900"},{"seed","1"},{"batch","1"},{"seq_len","16"}}); recv_msg(ctl,m);
  REQUIRE(m.type==MessageType::ERROR, "hard: stale-epoch command rejected");

  // 2) worker-boot binding mismatch rejected.
  send_msg(ctl,MessageType::EXECUTE,0,0,{{"target_incarnation",std::to_string(inc)},{"target_worker_boot","999"},{"cmd_id","901"},{"seed","1"},{"batch","1"},{"seq_len","16"}}); recv_msg(ctl,m);
  REQUIRE(m.type==MessageType::ERROR, "hard: worker-boot binding mismatch rejected");

  // 3) worker death before cleanup acknowledgment.
  std::atomic<bool> t1_done{false}; std::string t1_reply; bool t1_ok=false;
  std::thread t1([&]{
    net::TcpSocket s; if(!s.connect("127.0.0.1",port)){ t1_done=true; return; }
    ProtocolMessage x; send_msg(s,MessageType::HELLO,0,0,{{"label","hard-exe"}}); recv_msg(s,x);
    send_msg(s,MessageType::EXECUTE,0,0,{{"target_incarnation",std::to_string(inc)},{"target_worker_boot","1"},{"cmd_id","1"},{"seed","42"},{"batch","1"},{"seq_len","16"},{"hold_barrier",bar},{"marker",hm}});
    recv_msg(s,x); t1_reply=x.payload; t1_ok=(x.type==MessageType::ERROR); t1_done=true;
  });
  if (!file_contains(hm + ".executing", "incarnation=", 40000)) { t1.join(); er_kill(wa); er_kill(cp); std::printf("hard: execute never started\n"); ++g_fail; return; }

  std::atomic<bool> t2_done{false}; std::string t2_reply;
  std::thread t2([&]{
    net::TcpSocket s; if(!s.connect("127.0.0.1",port)){ t2_done=true; return; }
    ProtocolMessage x; send_msg(s,MessageType::HELLO,0,0,{{"label","hard-drn"}}); recv_msg(s,x);
    send_msg(s,MessageType::DRAIN,0,0,{{"target_incarnation",std::to_string(inc)},{"target_worker_boot","1"},{"cmd_id","2"}});
    recv_msg(s,x); t2_reply=x.payload; t2_done=true;
  });

  // Kill the worker while the use is outstanding and the drain is pending.
  er_sleep(400); er_kill(wa);

  t1.join();
  t2.join();
  auto t1f = parse_payload(t1_reply);
  auto t2f = parse_payload(t2_reply);
  std::printf("hard: worker-death exe_type_err=%d drain_ok=%d\n", t1_ok?1:0, get(t2f,"ok","0")=="1"?1:0);
  REQUIRE(t1_ok, "hard: execute fails after worker death (use not silently completed)");
  REQUIRE(get(t2f,"ok","0")=="0", "hard: drain fails after worker death before cleanup ack");

  // The drain must NOT be DRAINED (no cleanup ack) and the admitted use must be
  // released (no pending-request/use leak).
  std::string phase="?", bool_ack="?";
  std::uint64_t total_active=999;
  for (int i=0;i<200;++i) {
    send_msg(ctl,MessageType::QUERY_DRAIN,0,0,{{"incarnation_id",std::to_string(inc)}}); recv_msg(ctl,m);
    auto qp=parse_payload(m.payload);
    phase=get(qp,"drain_phase","?"); bool_ack=get(qp,"backend_cleanup_ack","?");
    std::string ta=get(qp,"total_active_use","999");
    total_active = ta=="999"||ta.empty()?999:std::strtoull(ta.c_str(),nullptr,10);
    if (total_active==0) break;
    er_sleep(20);
  }
  std::printf("hard: after worker death phase=%s cleanup_ack=%s total_active=%llu\n", phase.c_str(), bool_ack.c_str(), (unsigned long long)total_active);
  REQUIRE(phase!="DRAINED" && phase!="RETIRED", "hard: DRAINED not reached without cleanup ack after worker death");
  REQUIRE(bool_ack!="1", "hard: backend cleanup not acknowledged after worker death");
  REQUIRE(total_active==0, "hard: no serving-use/request leak after worker death");

  // 4) unsolicited/stale drain acknowledgment rejected.
  send_msg(ctl,MessageType::DRAIN_RESULT,0,0,{{"cmd_id","8"},{"ok","1"}}); recv_msg(ctl,m);
  REQUIRE(m.type==MessageType::ERROR, "hard: stale/unsolicited drain acknowledgment rejected");

  er_kill(wa); er_kill(cp);
  std::printf("hardening proof complete (tag=%d)\n", tag);
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (!net::init()) return 1;
  bool use_cuda = false;
#ifdef ER_TEST_CUDA
  use_cuda = true;
#endif
  run_drain_proof(use_cuda, 0);
  run_replacement_proof(use_cuda, 1);
  run_hardening_proof(use_cuda, 2);
  net::shutdown();
  std::printf("command proof failures=%d\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}