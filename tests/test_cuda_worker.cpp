#include "engine_residency/net.hpp"
#include "engine_residency/protocol.hpp"
#include "engine_residency/components.hpp"
#include "spawn.hpp"

#include <cstdint>
#include <cstdio>
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

static int g_fail = 0;
#define REQUIRE(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", (msg)); ++g_fail; } } while(0)

int main() {
  const std::uint16_t port = 27400;
  if (!net::init()) return 1;
  ErProcess cp, pa, pb, pa2;
  if (!er_spawn(ER_COORDINATOR_LOCATION, std::to_string(port), cp)) { std::printf("coordinator spawn failed\n"); return 1; }
  net::TcpSocket probe; bool up=false;
  for (int i=0;i<80;++i){ if (probe.connect("127.0.0.1",port)){ up=true; break;} er_sleep(100); }
  if (!up) { er_kill(cp); std::printf("coordinator not reachable\n"); return 1; }

  // Real CUDA worker A.
  std::string pport = std::string("--port ") + std::to_string(port);
  if (!er_spawn(ER_WORKER_LOCATION, ("--cuda " + pport + " --worker-id 1 --boot 1 --label A --pid 100 --serve 1 --result cr_worker_a.txt --stay-alive").c_str(), pa)) { std::printf("worker A spawn failed\n"); er_kill(cp); return 1; }
  bool a_ready = file_contains("cr_worker_a.txt", "readiness=READY", 30000);
  std::printf("worker A (cuda) readiness observed=%d\n", a_ready?1:0);
  REQUIRE(a_ready, "CUDA worker A becomes READY with parity");

  // Warm CUDA standby B.
  er_spawn(ER_WORKER_LOCATION, ("--cuda " + pport + " --worker-id 2 --boot 1 --label B --pid 200 --serve 0 --result cr_worker_b.txt --stay-alive").c_str(), pb);
  bool b_ready = file_contains("cr_worker_b.txt", "readiness=READY", 30000);
  std::printf("worker B (cuda) readiness observed=%d\n", b_ready?1:0);
  REQUIRE(b_ready, "CUDA standby worker B becomes READY");

  // Kill worker A as a real OS process while the coordinator stays alive.
  er_kill(pa);
  er_sleep(1000);

  // Controller client fences A and verifies its stale boot is rejected.
  net::TcpSocket cli;
  if (!cli.connect("127.0.0.1", port)) { std::printf("controller connect failed\n"); er_kill(cp); return 1; }
  ProtocolMessage mr;
  send_msg(cli, MessageType::HELLO, 0, 0, {{"label","controller"}}); recv_msg(cli, mr);
  send_msg(cli, MessageType::FENCE_WORKER, 0, 0, {{"worker_id","1"},{"worker_boot","1"},{"reason","worker died"}}); recv_msg(cli, mr);
  send_msg(cli, MessageType::REGISTER, 0, 0, {{"worker_id","1"},{"worker_boot","1"},{"source_id","1"},{"source_boot","1"},{"pid","100"},{"label","A"}}); recv_msg(cli, mr);
  bool stale_rejected = (mr.type == MessageType::ERROR);
  std::printf("stale fenced worker A re-register rejected=%d\n", stale_rejected?1:0);
  REQUIRE(stale_rejected, "fenced stale boot re-registration rejected");

  // Fresh CUDA replacement A-prime (new boot, fresh process-local device state + graph).
  if (!er_spawn(ER_WORKER_LOCATION, ("--cuda " + pport + " --worker-id 1 --boot 2 --label A-prime --pid 300 --serve 1 --result cr_worker_a2.txt --stay-alive").c_str(), pa2)) { std::printf("worker A-prime spawn failed\n"); er_kill(cp); return 1; }
  bool a2_ready = file_contains("cr_worker_a2.txt", "readiness=READY", 30000);
  std::printf("worker A-prime (cuda) readiness observed=%d\n", a2_ready?1:0);
  REQUIRE(a2_ready, "fresh CUDA replacement A-prime becomes READY with parity");
  std::string a2 = file_read("cr_worker_a2.txt");
  REQUIRE(a2.find("parity=OK") != std::string::npos, "replacement performs real CUDA work with parity");

  // Cleanup survivors.
  er_kill(pa2); er_kill(pb); er_kill(cp);
  net::shutdown();
  std::printf("cuda worker-death test failures=%d\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}
