#include "engine_residency/protocol.hpp"

#include <cstddef>
#include <string>

namespace engine_residency {

std::uint32_t proto_checksum(const std::string& bytes) {
  std::uint32_t h = 2166136261u;
  for (unsigned char c : bytes) { h ^= c; h *= 16777619u; }
  return h;
}

namespace {
void put_u32(std::string& out, std::uint32_t v) {
  out.push_back((char)((v >> 0) & 0xFF)); out.push_back((char)((v >> 8) & 0xFF));
  out.push_back((char)((v >> 16) & 0xFF)); out.push_back((char)((v >> 24) & 0xFF));
}
std::uint32_t get_u32(const char* p) {
  std::uint32_t v = 0;
  v |= (std::uint32_t)(unsigned char)p[0];
  v |= (std::uint32_t)(unsigned char)p[1] << 8;
  v |= (std::uint32_t)(unsigned char)p[2] << 16;
  v |= (std::uint32_t)(unsigned char)p[3] << 24;
  return v;
}
}  // namespace

void encode_frame(const ProtocolMessage& m, std::string& out) {
  out.push_back((char)0x45); out.push_back((char)0x52); out.push_back((char)0x46); out.push_back((char)0x46);  // "ERFF"
  out.push_back((char)kProtoVersion);
  out.push_back((char)m.type);
  put_u32(out, m.epoch);
  put_u32(out, m.authority);
  put_u32(out, (std::uint32_t)m.payload.size());
  put_u32(out, proto_checksum(m.payload));
  out += m.payload;
}

bool decode_frame(const char* data, std::size_t len, ProtocolMessage& out,
                  std::size_t& used, std::string& error) {
  used = 0;
  constexpr std::size_t kHeader = 4 + 1 + 1 + 4 + 4 + 4 + 4;  // 22
  if (len < kHeader) { return false; }  // need more data.
  if (data[0] != 0x45 || data[1] != 0x52 || data[2] != 0x46 || data[3] != 0x46) {
    error = "bad magic"; return false;
  }
  if ((std::uint8_t)data[4] != kProtoVersion) { error = "unsupported version"; return false; }
  std::uint8_t mt = (std::uint8_t)data[5];
  if (mt < 1 || mt > 25) { error = "invalid message type"; return false; }
  std::uint32_t epoch = get_u32(data + 6);
  std::uint32_t authority = get_u32(data + 10);
  std::uint32_t plen = get_u32(data + 14);
  std::uint32_t chk = get_u32(data + 18);
  if (plen > kProtoMaxPayload) { error = "oversized payload"; return false; }
  if (kHeader + plen > len) { return false; }  // need more data.
  std::string payload(data + kHeader, plen);
  if (proto_checksum(payload) != chk) { error = "checksum mismatch"; return false; }
  out.type = (MessageType)mt;
  out.epoch = epoch;
  out.authority = authority;
  out.payload = std::move(payload);
  used = kHeader + plen;
  return true;
}

}  // namespace engine_residency