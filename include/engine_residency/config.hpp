#ifndef ENGINE_RESIDENCY_CONFIG_HPP
#define ENGINE_RESIDENCY_CONFIG_HPP

// Version of the Engine Residency runtime library.
#define ENGINE_RESIDENCY_VERSION_MAJOR 1
#define ENGINE_RESIDENCY_VERSION_MINOR 0
#define ENGINE_RESIDENCY_VERSION_PATCH 0
#define ENGINE_RESIDENCY_VERSION_STRING "1.0.0"

// Library namespace. Engine Residency is a vendor-neutral C++20 runtime.
namespace engine_residency {

inline constexpr const char* kLibraryVersion = ENGINE_RESIDENCY_VERSION_STRING;

// Default compatibility namespace used when a caller does not supply one.
// Adapters and callers should namespace their compatibility keys so distinct
// runtimes cannot cross-validate each other silently.
inline constexpr const char* kDefaultCompatibilityNamespace =
    "engine-residency/reference/v1";

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_CONFIG_HPP
