#include "engine_residency/net.hpp"
#include "engine_residency/protocol.hpp"
#include "engine_residency/components.hpp"
#include "engine_residency/serving_use.hpp"
#include "spawn.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace engine_residency;
using engine_residency::detail::make_payload;
using engine_residency::detail::parse_payload;
using engine_residency::detail::get;

static bool send_msg(net::TcpSocket& s, MessageType t, std::uint64_t ep, std::uint64_t au, const std::map<std::string,std::string>& f){
  ProtocolMessage m; m.type=t; m.epoch=(std::uint32_t)ep; m.authority=(std::uint32_t)au; m.payload=make_payload(f); return s.send_frame(m);
}
static bool recv_msg(net::TcpSocket& s, ProtocolMessage& out){ return s.recv_frame(out); }

static bool file_contains(const std::string& path, const std::string& needle, std::uint32_t timeout_ms) {
  for (std::uint32_t t = 0; t < timeout_ms; t += 100) {
    std::ifstream in(path);
    if (in) { std::stringstream ss; ss << in.rdbuf(); std::string s = ss.str(); if (s.find(needle) != std::string::npos) return true; }
    er_sleep(100);
  }
  return false;
}
static std::string file_read(const std::string& path) { std::ifstream in(path); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static std::uint64_t field_uint(const std::string& s, const std::string& key) {
  std::size_t p = s.find(key + "=");
  if (p == std::string::npos) return 0;
  std::size_t q = s.find(" ", p);
  std::string v = s.substr(p + key.size() + 1, q == std::string::npos ? std::string::npos : q - (p + key.size() + 1));
  return std::strtoull(v.c_str(), nullptr, 10);
}

static int g_fail = 0;
#define REQUIRE(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", (msg)); ++g_fail; } } while(0)

// Sends a message and requires the coordinator to reject it (ERROR), proving stale
// traffic cannot mutate state.
static void expect_reject(net::TcpSocket& cli, MessageType t, const std::map<std::string,std::string>& f, const char* msg) {
  ProtocolMessage mr;
  send_msg(cli, t, 0, 0, f);
  recv_msg(cli, mr);
  std::printf("  stale %s rejected=%d\n", engine_residency::to_string(t), mr.type == MessageType::ERROR ? 1 : 0);
  REQUIRE(mr.type == MessageType::ERROR, msg);
}

// --- Two-process coordinator restart proof --------------------------------
static bool wait_reachable(const std::uint16_t port) {
  for (int i=0;i<80;++i){ net::TcpSocket s; if (s.connect("127.0.0.1",port)) return true; er_sleep(100); }
  return false;
}
static void run_restart_proof() {
  const std::uint16_t rport = 27420;
  ErProcess cp1, cp2, wa, wa2;
  if (!er_spawn(ER_COORDINATOR_LOCATION, std::to_string(rport), cp1)) { std::printf("restart: coordinator1 spawn failed\n"); ++g_fail; return; }
  if (!wait_reachable(rport)) { er_kill(cp1); std::printf("restart: coordinator1 not reachable\n"); ++g_fail; return; }
  std::string pp = std::string("--port ") + std::to_string(rport);
  er_spawn(ER_WORKER_LOCATION, ("--cuda " + pp + " --worker-id 1 --boot 1 --label A --pid 100 --serve 1 --result cr_restart_a.txt --stay-alive").c_str(), wa);
  bool a_ok = file_contains("cr_restart_a.txt", "activation=ACTIVE", 30000) && file_contains("cr_restart_a.txt", "parity=OK", 30000);
  REQUIRE(a_ok, "restart: worker A activates and serves before coordinator restart");
  // Persist durable state.
  { net::TcpSocket cli; cli.connect("127.0.0.1", rport); ProtocolMessage m;
    send_msg(cli, MessageType::HELLO, 0, 0, {{"label","ctl"}}); recv_msg(cli, m);
    send_msg(cli, MessageType::SAVE, 0, 0, {{"path","restart.bin"}}); recv_msg(cli, m);
    REQUIRE(m.type != MessageType::ERROR, "restart: durable state saved"); }
  // Terminate the real coordinator process.
  er_kill(cp1); er_sleep(1500);
  // Fresh coordinator process with durable state -> loads + advances epoch.
  if (!er_spawn(ER_COORDINATOR_LOCATION, std::to_string(rport) + " --state restart.bin", cp2)) { std::printf("restart: coordinator2 spawn failed\n"); ++g_fail; return; }
  if (!wait_reachable(rport)) { er_kill(cp2); std::printf("restart: coordinator2 not reachable\n"); ++g_fail; return; }
  // Old-epoch traffic is rejected without mutating state.
  { net::TcpSocket cli; cli.connect("127.0.0.1", rport); ProtocolMessage m;
    send_msg(cli, MessageType::HELLO, 0, 0, {{"label","ctl"}}); recv_msg(cli, m);
    std::string cur_epoch = get(parse_payload(m.payload), "epoch", "0");
    std::uint64_t cur = std::strtoull(cur_epoch.c_str(), nullptr, 10);
    std::printf("restart: recovered coordinator epoch=%llu\n", (unsigned long long)cur);
    REQUIRE(cur >= 2, "restart: durable coordinator epoch advanced (is >=2)");
    // stale epoch (1) traffic must be rejected.
    send_msg(cli, MessageType::QUERY_READINESS, 1, 0, {{"incarnation_id","1"}}); recv_msg(cli, m);
    std::printf("restart: old-epoch traffic rejected=%d\n", m.type == MessageType::ERROR ? 1 : 0);
    REQUIRE(m.type == MessageType::ERROR, "restart: old-epoch traffic rejected"); }
  // Fresh worker revalidation under the new epoch + authorized CUDA execution with parity.
  er_spawn(ER_WORKER_LOCATION, ("--cuda " + pp + " --worker-id 3 --boot 9 --label A2 --pid 400 --serve 1 --slot 7 --result cr_restart_a2.txt --stay-alive").c_str(), wa2);
  bool a2_ok = file_contains("cr_restart_a2.txt", "activation=ACTIVE", 30000) && file_contains("cr_restart_a2.txt", "parity=OK", 30000);
  REQUIRE(a2_ok, "restart: fresh revalidation + authorized CUDA execution with parity");
  er_kill(wa2); er_kill(wa); er_kill(cp2);
}
int main() {
  const std::uint16_t port = 27410;
  if (!net::init()) return 1;
  ErProcess cp, pa, pb, pa2;
  if (!er_spawn(ER_COORDINATOR_LOCATION, std::to_string(port), cp)) { std::printf("coordinator spawn failed\n"); return 1; }
  net::TcpSocket probe; bool up=false;
  for (int i=0;i<80;++i){ if (probe.connect("127.0.0.1",port)){ up=true; break;} er_sleep(100); }
  if (!up) { er_kill(cp); std::printf("coordinator not reachable\n"); return 1; }
  std::string pport = std::string("--port ") + std::to_string(port);

  // Real CUDA worker A: register, publish CUDA evidence, prepare, warmup, activate, serve.
  if (!er_spawn(ER_WORKER_LOCATION, ("--cuda " + pport + " --worker-id 1 --boot 1 --label A --pid 100 --serve 1 --result cr_worker_a.txt --stay-alive").c_str(), pa)) { std::printf("worker A spawn failed\n"); er_kill(cp); return 1; }
  bool a_ok = file_contains("cr_worker_a.txt", "activation=ACTIVE", 30000) &&
              file_contains("cr_worker_a.txt", "parity=OK", 30000);
  std::printf("worker A (cuda) activated+served=%d\n", a_ok?1:0);
  REQUIRE(a_ok, "worker A activates and executes authorized CUDA work with CPU parity");
  std::uint64_t incA = field_uint(file_read("cr_worker_a.txt"), "incarnation");
  std::printf("worker A incarnation=%llu\n", (unsigned long long)incA);
  REQUIRE(incA != 0, "worker A incarnation id reported");

  // Kill worker A (real OS process termination) while the coordinator stays alive.
  er_kill(pa);
  er_sleep(1200);

  // Controller client fences A and drives stale-traffic rejection for its fenced boot.
  net::TcpSocket cli;
  if (!cli.connect("127.0.0.1", port)) { std::printf("controller connect failed\n"); er_kill(cp); return 1; }
  ProtocolMessage mr;
  send_msg(cli, MessageType::HELLO, 0, 0, {{"label","controller"}}); recv_msg(cli, mr);
  send_msg(cli, MessageType::FENCE_WORKER, 0, 0, {{"worker_id","1"},{"worker_boot","1"},{"reason","worker died"}}); recv_msg(cli, mr);

  // Stale A registration rejected.
  send_msg(cli, MessageType::REGISTER, 0, 0, {{"worker_id","1"},{"worker_boot","1"},{"source_id","1"},{"source_boot","1"},{"pid","100"},{"label","A"}}); recv_msg(cli, mr);
  REQUIRE(mr.type == MessageType::ERROR, "stale fenced worker A re-registration rejected");

  // Stale A component/readiness/activation/release/execution-result traffic is rejected
  // without mutating live state (each targeting A's fenced incarnation).
  std::string incs = std::to_string(incA);
  expect_reject(cli, MessageType::PUBLISH_COMPONENT, {{"incarnation_id",incs},{"category","2"},{"state","5"},{"subject","reference-model"},{"model_gen","1"},{"compat","ref/v1"}}, "stale A component publication rejected");
  expect_reject(cli, MessageType::QUERY_READINESS, {{"incarnation_id",incs}}, "stale A readiness query rejected");
  expect_reject(cli, MessageType::ACTIVATE, {{"incarnation_id",incs},{"readiness_gen","0"},{"slot_id","1"}}, "stale A activation rejected");
  expect_reject(cli, MessageType::RELEASE_USE, {{"incarnation_id",incs},{"use_id","1"}}, "stale A serving-use release rejected");
  expect_reject(cli, MessageType::EXECUTION_RESULT, {{"incarnation_id",incs},{"use_id","1"},{"ok","1"}}, "stale A execution-result rejected");

  // Fresh CUDA standby/replacement B: activates under current authority and serves with parity.
  if (!er_spawn(ER_WORKER_LOCATION, ("--cuda " + pport + " --worker-id 2 --boot 1 --label B --pid 200 --serve 1 --result cr_worker_b.txt --stay-alive").c_str(), pb)) { std::printf("worker B spawn failed\n"); er_kill(cp); return 1; }
  bool b_ok = file_contains("cr_worker_b.txt", "activation=ACTIVE", 30000) &&
              file_contains("cr_worker_b.txt", "parity=OK", 30000);
  std::printf("worker B (cuda) activated+served=%d\n", b_ok?1:0);
  REQUIRE(b_ok, "worker B activates under fresh authority and executes authorized CUDA work with parity");

  // Fresh CUDA replacement A-prime: new boot, fresh process-local device state + graph.
  if (!er_spawn(ER_WORKER_LOCATION, ("--cuda " + pport + " --worker-id 1 --boot 2 --label A-prime --pid 300 --serve 1 --slot 2 --result cr_worker_a2.txt --stay-alive").c_str(), pa2)) { std::printf("worker A-prime spawn failed\n"); er_kill(cp); return 1; }
  bool a2_ok = file_contains("cr_worker_a2.txt", "activation=ACTIVE", 30000) &&
               file_contains("cr_worker_a2.txt", "parity=OK", 30000);
  std::printf("worker A-prime (cuda) activated+served=%d\n", a2_ok?1:0);
  REQUIRE(a2_ok, "fresh CUDA replacement A-prime prepares fresh process-local state, activates, and serves with parity");

  // Cleanup survivors.
  er_kill(pa2); er_kill(pb); er_kill(cp);
  run_restart_proof();
  net::shutdown();
  std::printf("cuda worker-death test failures=%d\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}
