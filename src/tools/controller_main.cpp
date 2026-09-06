#include "engine_residency/net.hpp"
#include "engine_residency/protocol.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

using namespace engine_residency;
using engine_residency::detail::make_payload;
using engine_residency::detail::parse_payload;
using engine_residency::detail::get;

int main(int argc, char** argv) {
  std::string host = "127.0.0.1"; std::uint16_t port = 27200;
  if (argc > 1) port = (std::uint16_t)std::atoi(argv[1]);
  if (!net::init()) return 1;
  net::TcpSocket sock;
  if (!sock.connect(host, port)) { std::printf("controller connect failed\n"); return 1; }
  ProtocolMessage m; m.type = MessageType::HELLO; m.payload = make_payload({{"label","controller"}});
  if (!sock.send_frame(m)) return 1;
  ProtocolMessage mr; if (!sock.recv_frame(mr)) return 1;
  std::printf("controller connected epoch=%s authority=%s\n",
              get(parse_payload(mr.payload), "epoch", "").c_str(),
              get(parse_payload(mr.payload), "authority", "").c_str());
  net::shutdown();
  return 0;
}
