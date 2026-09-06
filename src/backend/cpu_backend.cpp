#include "engine_residency/backend.hpp"
#include "engine_residency/units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine_residency {

std::vector<float> cpu_reference_compute(const ComputeInput& in) {
  const std::size_t n = in.input.size();
  std::vector<float> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    float r = in.input[i];
    r = r * 0.0625f;                       // exact power-of-two scale.
    r = r + 0.5f;                          // exact add.
    r = r + static_cast<float>(static_cast<int>(i % 67)) * 0.01f;
    r = r + static_cast<float>(in.seed % 1000u) * 0.001f;
    out[i] = r;
  }
  return out;
}

// Deterministic CPU reference backend. It performs the same bounded,
// deterministic serving-like computation the CUDA engine performs, so parity is
// verifiable. It never masquerades as a production LLM server.
class CpuReferenceBackend final : public ReferenceBackend {
 public:
  std::string backend_name() const override { return "cpu-reference"; }
  bool is_cuda() const override { return false; }

  BackendDiscovery discover(std::size_t ordinal) override {
    BackendDiscovery d;
    d.backend_name = "cpu-reference";
    d.cuda = false;
    d.device_ordinal = ordinal;
    d.device_name = "x86-64 reference";
    d.compute_capability = "n/a";
    d.device_mem_total = 1ULL * 1024 * 1024 * 1024;
    d.device_mem_free = d.device_mem_total;
    d.device_mem_base = 0;
    d.has_device_context = true;
    d.has_graph_support = true;
    return d;
  }

  bool prepare(Bytes model_bytes, Bytes workspace_bytes, std::uint64_t kv_entries) override {
    model_.assign(static_cast<std::size_t>(model_bytes), 1.0f);
    workspace_.assign(static_cast<std::size_t>(workspace_bytes), 0.0f);
    kv_entries_ = kv_entries;
    kv_.assign(static_cast<std::size_t>(kv_entries), 0.0f);
    prepared_ = true;
    return true;
  }

  bool bind_kernel(const std::string& kernel_key) override {
    kernel_key_ = kernel_key;
    kernel_bound_ = !kernel_key.empty();
    return kernel_bound_;
  }

  bool instantiate_graph(bool required) override {
    graph_instantiated_ = true;
    graph_required_ = required;
    return true;
  }

  ComputeResult warmup(std::uint32_t batch, std::uint32_t seq_len) override {
    ComputeInput in;
    in.batch = batch; in.seq_len = seq_len;
    in.input.assign(static_cast<std::size_t>(batch) * seq_len, 0.0f);
    in.seed = 42;
    return run(in);
  }

  ComputeResult execute(const ComputeInput& in) override { return run(in); }

  bool verify_parity(const ComputeResult& actual, const ComputeResult& cpu_reference) override {
    if (!actual.ok || !cpu_reference.ok) return false;
    if (actual.output.size() != cpu_reference.output.size()) return false;
    for (std::size_t i = 0; i < actual.output.size(); ++i) {
      float a = actual.output[i];
      float b = cpu_reference.output[i];
      if (std::isnan(a) || std::isnan(b)) return false;
      if (a != b) {
        float diff = std::fabs(a - b);
        float tol = 1e-5f * std::max(1.0f, std::max(std::fabs(a), std::fabs(b)));
        if (diff > tol) return false;
      }
    }
    return true;
  }

  void release() override {
    model_.clear(); model_.shrink_to_fit();
    workspace_.clear(); workspace_.shrink_to_fit();
    kv_.clear(); kv_.shrink_to_fit();
    prepared_ = false;
    kernel_bound_ = false;
    graph_instantiated_ = false;
  }

 private:
  ComputeResult run(const ComputeInput& in) {
    ComputeResult res;
    res.ok = true;
    res.output = cpu_reference_compute(in);
    res.seed = in.seed;
    res.bytes_moved = static_cast<Bytes>(in.input.size()) * sizeof(float);
    res.detail = "cpu-reference-compute";
    return res;
  }

  std::vector<float> model_;
  std::vector<float> workspace_;
  std::vector<float> kv_;
  std::uint64_t kv_entries_{0};
  std::string kernel_key_;
  bool prepared_{false};
  bool kernel_bound_{false};
  bool graph_instantiated_{false};
  bool graph_required_{false};
};

std::unique_ptr<ReferenceBackend> make_cpu_reference_backend() {
  return std::make_unique<CpuReferenceBackend>();
}

}  // namespace engine_residency

