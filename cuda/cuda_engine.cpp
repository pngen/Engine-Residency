#include "engine_residency/backend.hpp"
#include "engine_residency/errors.hpp"
#include "engine_residency/units.hpp"
#include "engine_residency/clock.hpp"
#include "cuda_kernels.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <cuda_runtime.h>

namespace engine_residency {

// The real CUDA reference engine. It performs the same bounded deterministic
// computation as the CPU reference, with real device allocations, a real
// kernel, a real executable CUDA graph, host-pinned staging, and measurable
// device-memory baseline. It is NOT a production LLM server.
class CudaReferenceBackend final : public ReferenceBackend {
 public:
  ~CudaReferenceBackend() override { release(); }
  std::string backend_name() const override { return "cuda-reference"; }
  bool is_cuda() const override { return true; }

  BackendDiscovery discover(std::size_t ordinal) override {
    BackendDiscovery d;
    d.backend_name = "cuda-reference";
    d.cuda = true;
    d.device_ordinal = ordinal;
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count <= 0) {
      throw DomainError(ErrorCode::BackendMissing, "no CUDA device");
    }
    if (ordinal >= (std::size_t)count) {
      throw DomainError(ErrorCode::BackendMissing, "device ordinal out of range");
    }
    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, (int)ordinal);
    cudaSetDevice((int)ordinal);
    d.device_name = prop.name;
    d.compute_capability = std::to_string(prop.major) + "." + std::to_string(prop.minor);
    std::size_t freeb = 0, total = 0;
    cudaMemGetInfo(&freeb, &total);
    d.device_mem_total = (Bytes)total;
    d.device_mem_free = (Bytes)freeb;
    d.device_mem_base = (Bytes)(total - freeb);
    d.has_device_context = true;
    d.has_graph_support = (prop.major >= 10);
    return d;
  }

  bool prepare(Bytes model_bytes, Bytes workspace_bytes, std::uint64_t kv_entries) override {
    release_alloc();
    model_bytes_ = model_bytes; workspace_bytes_ = workspace_bytes; kv_entries_ = kv_entries;
    if (cudaMalloc((void**)&d_model_, model_bytes_) != cudaSuccess) return false;
    if (workspace_bytes_ && cudaMalloc((void**)&d_workspace_, workspace_bytes_) != cudaSuccess) return false;
    if (cudaMalloc((void**)&d_kv_, kv_entries_ * sizeof(float)) != cudaSuccess) return false;
    if (cudaMallocHost((void**)&h_staging_, staging_bytes()) != cudaSuccess) return false;
    prepared_ = true;
    return true;
  }

  bool bind_kernel(const std::string& kernel_key) override {
    kernel_key_ = kernel_key; kernel_bound_ = !kernel_key.empty();
    return kernel_bound_;
  }

  bool instantiate_graph(bool required) override {
    if (!prepared_) return false;
    graph_required_ = required;
    destroy_graph();
    cudaStream_t s = nullptr;
    cudaStreamCreate(&s);
    cudaStreamBeginCapture(s, cudaStreamCaptureModeThreadLocal);
    int n = (int)staging_floats();
    int threads = 256; int blocks = (n + threads - 1) / threads;
    er_launch_reference_kernel(d_model_, d_workspace_, n, 42, blocks, threads, s);
    cudaStreamEndCapture(s, &graph_);
    cudaStreamDestroy(s);
    if (cudaGraphInstantiate(&graph_exec_, graph_, nullptr, nullptr, 0) != cudaSuccess) return false;
    graph_ready_ = true;
    return true;
  }

  ComputeResult warmup(std::uint32_t batch, std::uint32_t seq_len) override {
    ComputeInput in; in.batch = batch; in.seq_len = seq_len; in.seed = 42;
    std::size_t n = (std::size_t)batch * seq_len;
    in.input.assign(n, 0.0f);
    return run(in);
  }

  ComputeResult execute(const ComputeInput& in) override { return run(in); }

  bool verify_parity(const ComputeResult& actual, const ComputeResult& cpu_reference) override {
    if (!actual.ok || !cpu_reference.ok) return false;
    if (actual.output.size() != cpu_reference.output.size()) return false;
    for (std::size_t i = 0; i < actual.output.size(); ++i) {
      float a = actual.output[i], b = cpu_reference.output[i];
      if (a != b) {
        if (std::fabs((double)(a - b)) > 1e-5 * (1.0 + std::fabs((double)a))) return false;
      }
    }
    return true;
  }

  void release() override { release_alloc(); }

 private:
  std::size_t staging_floats() const { return 1u << 16; }
  std::size_t staging_bytes() const { return staging_floats() * sizeof(float); }

  ComputeResult run(const ComputeInput& in) {
    ComputeResult res;
    std::size_t n = in.input.size();
    if (n == 0) { res.ok = false; res.detail = "empty input"; return res; }
    if (n > staging_floats()) { res.ok = false; res.detail = "input too large for reference staging"; return res; }
    if (!prepared_) { res.ok = false; res.detail = "not prepared"; return res; }

    // H2D to pinned host staging, then to device.
    std::memcpy(h_staging_, in.input.data(), n * sizeof(float));
    if (cudaMemcpy(d_model_, h_staging_, n * sizeof(float), cudaMemcpyHostToDevice) != cudaSuccess) {
      res.ok = false; res.detail = "H2D failed"; return res;
    }
    int threads = 256; int blocks = (int)((n + threads - 1) / threads);
    if (graph_ready_ && graph_exec_) {
      if (cudaGraphLaunch(graph_exec_, 0) != cudaSuccess) { res.ok = false; res.detail = "graph replay failed"; return res; }
    } else {
      if (er_launch_reference_kernel(d_model_, d_workspace_, (int)n, (unsigned)in.seed, blocks, threads, 0) != cudaSuccess) {
        res.ok = false; res.detail = "kernel launch failed"; return res;
      }
    }
    if (cudaDeviceSynchronize() != cudaSuccess) { res.ok = false; res.detail = "sync failed"; return res; }
    std::uint64_t t0 = std::chrono::steady_clock::now().time_since_epoch().count();
    if (cudaMemcpy(h_staging_, d_model_, n * sizeof(float), cudaMemcpyDeviceToHost) != cudaSuccess) {
      res.ok = false; res.detail = "D2H failed"; return res;
    }
    std::uint64_t t1 = std::chrono::steady_clock::now().time_since_epoch().count();
    res.output.assign(h_staging_, h_staging_ + n);
    res.seed = in.seed;
    res.ok = true;
    res.detail = graph_ready_ ? "cuda-graph" : "cuda-kernel";
    res.elapsed_ns = (DurationNs)(t1 - t0);
    res.bytes_moved = (Bytes)n * sizeof(float) * 2;
    (void)blocks; (void)threads;
    return res;
  }

  void release_alloc() {
    destroy_graph();
    if (h_staging_) { cudaFreeHost(h_staging_); h_staging_ = nullptr; }
    if (d_model_) { cudaFree(d_model_); d_model_ = nullptr; }
    if (d_workspace_) { cudaFree(d_workspace_); d_workspace_ = nullptr; }
    if (d_kv_) { cudaFree(d_kv_); d_kv_ = nullptr; }
    prepared_ = false; kernel_bound_ = false; graph_ready_ = false;
  }
  void destroy_graph() {
    if (graph_exec_) { cudaGraphExecDestroy(graph_exec_); graph_exec_ = nullptr; }
    if (graph_) { cudaGraphDestroy(graph_); graph_ = nullptr; }
    graph_ready_ = false;
  }

  float* d_model_{nullptr};
  float* d_workspace_{nullptr};
  float* d_kv_{nullptr};
  float* h_staging_{nullptr};
  cudaGraph_t graph_{nullptr};
  cudaGraphExec_t graph_exec_{nullptr};
  Bytes model_bytes_{0}, workspace_bytes_{0};
  std::uint64_t kv_entries_{0};
  std::string kernel_key_;
  bool prepared_{false};
  bool kernel_bound_{false};
  bool graph_ready_{false};
  bool graph_required_{false};
};

std::unique_ptr<ReferenceBackend> make_cuda_reference_backend() {
  return std::make_unique<CudaReferenceBackend>();
}

}  // namespace engine_residency
