#ifndef ENGINE_RESIDENCY_NET_HPP
#define ENGINE_RESIDENCY_NET_HPP

#include <string>
#include "engine_residency/protocol.hpp"

namespace engine_residency {
namespace net {

// Initialize Winsock once. Returns true on success.
bool init();
void shutdown();

// A blocking TCP client socket bound to loopback by default.
class TcpSocket {
 public:
  TcpSocket() = default;
  ~TcpSocket();
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;
  TcpSocket(TcpSocket&& o) noexcept;
  TcpSocket& operator=(TcpSocket&& o) noexcept;

  bool connect(const std::string& host, std::uint16_t port);
  bool send(const char* data, std::size_t len);
  // Reads exactly len bytes (loops on partial reads). Returns false on EOF/error.
  bool recv_exact(char* data, std::size_t len);
  bool send_frame(const ProtocolMessage& m);
  // Reads one frame, handling partial buffered reads. Returns false on EOF/error.
  bool recv_frame(ProtocolMessage& m);
  void close();
  bool is_open() const;
  bool flush();

 private:
  friend class TcpListener;
  void* sock_{nullptr};  // SOCKET as void* to keep header WinSock-free.
  std::string rxbuf_;
};


// A loopback listener over TCP.
class TcpListener {
 public:
  TcpListener() = default;
  ~TcpListener();
  bool listen(std::uint16_t port);
  // Blocks for a new connection; returns the accepted socket (ownership) via out.
  bool accept(TcpSocket& out);
  void close();
  std::uint16_t port() const;
  bool is_open() const;
 private:
  void* sock_{nullptr};
  std::uint16_t port_{0};
};


}  // namespace net
}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_NET_HPP