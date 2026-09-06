#ifndef ENGINE_RESIDENCY_BACKEND_HPP
#define ENGINE_RESIDENCY_BACKEND_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine_residency/clock.hpp"
#include "engine_residency/units.hpp"

namespace engine_residency {

// ---------------------------------------------------------------------------
// Reference backend contract.
//
// Engine Residency is backend-neutral. This is the narrow contract a backend
// adapter satisfies so the reference multiprocess/CUDA proof can run real
// bounded deterministic serving-like computation. It is intentionally NOT a
// production LLM server: it is a bounded, deterministic compute reference used
// to prove that "prepared" means a verified process-local executable binding.
//
// The core runtime does not depend on this interface. It lives behind the
// backend/adapters boundary.
// ---------------------------------------------------------------------------

struct ComputeInput {
  std::vector<float> input;    // batch*seq_len elements (row-major).
  std::uint32_t batch{1};
  std::uint32_t seq_len{1};
  std::string dtype{"fp32"};
  // Deterministic seed for reproducible reference output.
  std::uint64_t seed{0};
};

// A completed execution result. Output is the deterministic reference result.
struct ComputeResult {
  bool ok{false};
  std::vector<float> output;
  std::uint64_t seed{0};
  DurationNs elapsed_ns{0};
  std::string detail;   // machine-readable, bounded.
  Bytes bytes_moved{0}; // bytes moved (H2D + D2H), for accounting.
};

// Discovery summary from a backend/device.
struct BackendDiscovery {
  std::string backend_name;
  bool cuda{false};
  std::size_t device_ordinal{0};
  std::string device_name;
  std::string compute_capability;
  Bytes device_mem_total{0};
  Bytes device_mem_free{0};
  Bytes device_mem_base{0};  // baseline used before this engine's reservations.
  bool has_device_context{false};
  bool has_graph_support{false};
};

// The narrow backend preparation/execution/cleanup contract.
class ReferenceBackend {
 public:
  virtual ~ReferenceBackend() = default;

  virtual std::string backend_name() const = 0;
  virtual bool is_cuda() const = 0;

  // Discover context and device(s); fills discovery. Throws on no device.
  virtual BackendDiscovery discover(std::size_t device_ordinal) = 0;

  // Allocate real model-like weight buffers, workspace, and KV-like capacity.
  // Must BIND the buffers into the process-local context.
  virtual bool prepare(Bytes model_bytes, Bytes workspace_bytes,
                       std::uint64_t kv_entries) = 0;

  // Bind a real kernel for the given key. Returns false if incompatible.
  virtual bool bind_kernel(const std::string& kernel_key) = 0;

  // Instantiate a real executable graph if supported/required.
  virtual bool instantiate_graph(bool required) = 0;

  // Perform a real warmup and verify its output. Provenance is MEASURED.
  virtual ComputeResult warmup(std::uint32_t batch, std::uint32_t seq_len) = 0;

  // Execute a real (bounded, deterministic) serving-like computation.
  virtual ComputeResult execute(const ComputeInput& in) = 0;

  // Verify that a reference backend result matches the CPU reference within the
  // exact tolerance for the reference computation.
  virtual bool verify_parity(const ComputeResult& actual,
                             const ComputeResult& cpu_reference) = 0;

  // Release all allocations and sync. cudaFree/cudaFreeHost and graph
  // destruction happen here. Never reconstruct from metadata.
  virtual void release() = 0;
};

// CPU reference compute function used for parity and as the deterministic
// baseline. The same computation is implemented by the CPU backend and (in
// float32) by the CUDA kernel.
std::vector<float> cpu_reference_compute(const ComputeInput& in);

// Reference backend factories. The CPU one is always available; the CUDA one
// requires the er_cuda target (built only when a CUDA toolkit is present).
std::unique_ptr<ReferenceBackend> make_cpu_reference_backend();
std::unique_ptr<ReferenceBackend> make_cuda_reference_backend();

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_BACKEND_HPP
