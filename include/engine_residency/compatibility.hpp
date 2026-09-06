#ifndef ENGINE_RESIDENCY_COMPATIBILITY_HPP
#define ENGINE_RESIDENCY_COMPATIBILITY_HPP

#include <cstdint>
#include <string>

#include "engine_residency/config.hpp"

namespace engine_residency {

// ---------------------------------------------------------------------------
// Explicit compatibility identity. Compatibility is compared EXACTLY. We never
// silently relax exact compatibility into approximate compatibility: a profile
// either holds a matching compatibility key or it does not.
// ---------------------------------------------------------------------------
struct CompatibilityKey {
  // Namespace scopes the key to a specific runtime/family. Two different
  // compatibility families must never cross-validate each other merely because
  // their raw key strings happen to match.
  std::string ns;
  // The exact compatibility value.
  std::string value;

  CompatibilityKey() = default;
  CompatibilityKey(std::string n, std::string v)
      : ns(std::move(n)), value(std::move(v)) {}

  [[nodiscard]] static CompatibilityKey with_namespace(const std::string& value) {
    return CompatibilityKey(kDefaultCompatibilityNamespace, value);
  }

  [[nodiscard]] std::string render() const { return ns + "/" + value; }
  [[nodiscard]] bool is_valid() const noexcept {
    return !ns.empty() && !value.empty();
  }

  friend bool operator==(const CompatibilityKey& a, const CompatibilityKey& b) noexcept {
    return a.ns == b.ns && a.value == b.value;
  }
  friend bool operator!=(const CompatibilityKey& a, const CompatibilityKey& b) noexcept {
    return !(a == b);
  }
};

}  // namespace engine_residency

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#endif
namespace std {
template <>
struct hash<engine_residency::CompatibilityKey> {
  size_t operator()(const engine_residency::CompatibilityKey& k) const noexcept {
    size_t h = std::hash<std::string>{}(k.ns);
    return h ^ (std::hash<std::string>{}(k.value) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
  }
};
}  // namespace std
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif  // ENGINE_RESIDENCY_COMPATIBILITY_HPP
