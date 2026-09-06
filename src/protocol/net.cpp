#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "engine_residency/net.hpp"

#include <cstdint>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace engine_residency {
namespace net {

static bool g_winsock = false;
bool init() { if (g_winsock) return true; WSADATA d; if (WSAStartup(MAKEWORD(2,2), &d) != 0) return false; g_winsock = true; return true; }
void shutdown() { if (g_winsock) { WSACleanup(); g_winsock = false; } }

namespace { SOCKET as_socket(void* p) { return (SOCKET)(std::uintptr_t)p; } }

TcpSocket::~TcpSocket() { close(); }
TcpSocket::TcpSocket(TcpSocket&& o) noexcept { sock_ = o.sock_; rxbuf_ = std::move(o.rxbuf_); o.sock_ = nullptr; }
TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept { if (this != &o) { close(); sock_ = o.sock_; rxbuf_ = std::move(o.rxbuf_); o.sock_ = nullptr; } return *this; }
void TcpSocket::close() { if (sock_) { closesocket(as_socket(sock_)); sock_ = nullptr; } rxbuf_.clear(); }
bool TcpSocket::is_open() const { return sock_ != nullptr; }
bool TcpSocket::flush() { return true; }

bool TcpSocket::connect(const std::string& host, std::uint16_t port) {
  if (sock_) close();
  SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  ::inet_pton(AF_INET, (host == "localhost") ? "127.0.0.1" : host.c_str(), &addr.sin_addr);
  if (::connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { closesocket(s); return false; }
  sock_ = (void*)(std::uintptr_t)s;
  return true;
}

bool TcpSocket::send(const char* data, std::size_t len) {
  if (!sock_) return false;
  std::size_t off = 0;
  while (off < len) {
    int n = ::send(as_socket(sock_), data + off, (int)(len - off), 0);
    if (n == SOCKET_ERROR || n == 0) return false;
    off += (std::size_t)n;
  }
  return true;
}

bool TcpSocket::recv_exact(char* data, std::size_t len) {
  if (!sock_) return false;
  std::size_t off = 0;
  while (off < len) {
    int n = ::recv(as_socket(sock_), data + off, (int)(len - off), 0);
    if (n <= 0) return false;
    off += (std::size_t)n;
  }
  return true;
}

bool TcpSocket::send_frame(const ProtocolMessage& m) { std::string frame; encode_frame(m, frame); return send(frame.data(), frame.size()); }

bool TcpSocket::recv_frame(ProtocolMessage& m) {
  if (!sock_) return false;
  while (true) {
    std::size_t used = 0; std::string err;
    if (decode_frame(rxbuf_.data(), rxbuf_.size(), m, used, err)) { rxbuf_.erase(0, used); return true; }
    if (!err.empty()) return false;
    char buf[4096];
    int n = ::recv(as_socket(sock_), buf, sizeof(buf), 0);
    if (n <= 0) return false;
    rxbuf_.append(buf, (std::size_t)n);
  }
}

TcpListener::~TcpListener() { close(); }
void TcpListener::close() { if (sock_) { closesocket(as_socket(sock_)); sock_ = nullptr; } }
std::uint16_t TcpListener::port() const { return port_; }
bool TcpListener::is_open() const { return sock_ != nullptr; }
bool TcpListener::listen(std::uint16_t port) {
  if (sock_) close();
  SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) return false;
  BOOL opt = TRUE; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port); ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { closesocket(s); return false; }
  if (::listen(s, 16) == SOCKET_ERROR) { closesocket(s); return false; }
  sock_ = (void*)(std::uintptr_t)s; port_ = port;
  return true;
}
bool TcpListener::accept(TcpSocket& out) {
  if (!sock_) return false;
  sockaddr_in c{}; int cl = sizeof(c);
  SOCKET a = ::accept(as_socket(sock_), (sockaddr*)&c, &cl);
  if (a == INVALID_SOCKET) return false;
  out.close(); out.sock_ = (void*)(std::uintptr_t)a;
  return true;
}

}  // namespace net
}  // namespace engine_residency
