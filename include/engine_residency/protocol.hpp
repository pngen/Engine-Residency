#ifndef ENGINE_RESIDENCY_PROTOCOL_HPP
#define ENGINE_RESIDENCY_PROTOCOL_HPP

#include <cstdint>
#include <map>
#include <string>

namespace engine_residency {

namespace detail {
// Simple key=value payload helpers used by the reference control plane.
std::string make_payload(const std::map<std::string, std::string>& fields);
std::map<std::string, std::string> parse_payload(const std::string& payload);
std::string get(const std::map<std::string, std::string>& fields, const std::string& key,
                const std::string& def = "");
}  // namespace detail

// Compact versioned framed protocol for the reference control plane.
// Frame (network byte order via explicit byte assembly):
//   [4] magic "ERF"
//   [1] protocol version
//   [1] message type
//   [4] authority epoch (u32)
//   [4] authority generation (u32)
//   [4] payload length (u32, bounded)
//   [4] checksum (FNV-1a of payload)
//   [N] payload
// Checksums detect corruption; they do NOT authenticate. Reference service
// binds to loopback by default.

inline constexpr std::uint32_t kProtoMagic = 0x46465245;  // "ERFF"
inline constexpr std::uint8_t kProtoVersion = 1;
inline constexpr std::uint32_t kProtoMaxPayload = 1u << 20;  // 1 MiB

enum class MessageType : std::uint8_t {
  HELLO = 1,
  REGISTER = 2,
  REGISTER_ACK = 3,
  PUBLISH_BACKEND = 4,
  PUBLISH_DEVICE = 5,
  PUBLISH_COMPONENT = 6,
  PREPARE = 7,
  PREPARE_RESULT = 8,
  WARMUP_RESULT = 9,
  QUERY_READINESS = 10,
  ASSIGN_STANDBY = 11,
  ACTIVATE = 12,
  ACQUIRE_USE = 13,
  RELEASE_USE = 14,
  EXECUTION_RESULT = 15,
  INVALIDATE = 16,
  DRAIN = 17,
  DRAIN_RESULT = 18,
  REPLACE = 19,
  FENCE_WORKER = 20,
  REVALIDATE = 21,
  SAVE = 22,
  SHUTDOWN = 23,
  ERROR = 24,
  EXECUTE = 25,
  COMMAND_READY = 26,
  QUERY_DRAIN = 27,
};

inline const char* to_string(MessageType t) noexcept {
  switch (t) {
    case MessageType::HELLO: return "HELLO";
    case MessageType::REGISTER: return "REGISTER";
    case MessageType::REGISTER_ACK: return "REGISTER_ACK";
    case MessageType::PUBLISH_BACKEND: return "PUBLISH_BACKEND";
    case MessageType::PUBLISH_DEVICE: return "PUBLISH_DEVICE";
    case MessageType::PUBLISH_COMPONENT: return "PUBLISH_COMPONENT";
    case MessageType::PREPARE: return "PREPARE";
    case MessageType::PREPARE_RESULT: return "PREPARE_RESULT";
    case MessageType::WARMUP_RESULT: return "WARMUP_RESULT";
    case MessageType::QUERY_READINESS: return "QUERY_READINESS";
    case MessageType::ASSIGN_STANDBY: return "ASSIGN_STANDBY";
    case MessageType::ACTIVATE: return "ACTIVATE";
    case MessageType::ACQUIRE_USE: return "ACQUIRE_USE";
    case MessageType::RELEASE_USE: return "RELEASE_USE";
    case MessageType::EXECUTION_RESULT: return "EXECUTION_RESULT";
    case MessageType::INVALIDATE: return "INVALIDATE";
    case MessageType::DRAIN: return "DRAIN";
    case MessageType::DRAIN_RESULT: return "DRAIN_RESULT";
    case MessageType::REPLACE: return "REPLACE";
    case MessageType::FENCE_WORKER: return "FENCE_WORKER";
    case MessageType::REVALIDATE: return "REVALIDATE";
    case MessageType::SAVE: return "SAVE";
    case MessageType::SHUTDOWN: return "SHUTDOWN";
    case MessageType::ERROR: return "ERROR";
    case MessageType::EXECUTE: return "EXECUTE";
    case MessageType::COMMAND_READY: return "COMMAND_READY";
    case MessageType::QUERY_DRAIN: return "QUERY_DRAIN";
  }
  return "UNKNOWN";
}

struct ProtocolMessage {
  MessageType type{MessageType::HELLO};
  std::uint32_t epoch{0};
  std::uint32_t authority{0};
  std::string payload;   // bounded, opaque to the frame layer.
};

void encode_frame(const ProtocolMessage& m, std::string& out);

bool decode_frame(const char* data, std::size_t len, ProtocolMessage& out,
                  std::size_t& used, std::string& error);

std::uint32_t proto_checksum(const std::string& bytes);

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_PROTOCOL_HPP