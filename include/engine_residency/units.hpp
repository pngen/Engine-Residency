#ifndef ENGINE_RESIDENCY_UNITS_HPP
#define ENGINE_RESIDENCY_UNITS_HPP

#include <cstdint>

namespace engine_residency {

// Byte counts and byte-based capacities. All quantities are non-negative.
using Bytes = std::uint64_t;
inline constexpr Bytes kKiB = 1024ULL;
inline constexpr Bytes kMiB = 1024ULL * kKiB;
inline constexpr Bytes kGiB = 1024ULL * kMiB;

// A monotone counter (e.g. for sequence numbers).
using Counter = std::uint64_t;

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_UNITS_HPP
