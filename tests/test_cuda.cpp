#include "testutil.hpp"
#include "engine_residency/backend.hpp"
#include <cuda_runtime.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace engine_residency;

int main() {
  std::unique_ptr<ReferenceBackend> be = make_cuda_reference_backend();
  BackendDiscovery disc;
  try { disc = be->discover(0); } catch (const DomainError& e) { std::printf("no CUDA device: %s\n", e.what()); return 1; }
  std::printf("cuda: device=%s cc=%s total=%llu free=%llu base_used=%llu\n",
              disc.device_name.c_str(), disc.compute_capability.c_str(),
              (unsigned long long)disc.device_mem_total, (unsigned long long)disc.device_mem_free,
              (unsigned long long)disc.device_mem_base);
  if (!disc.cuda) { er_test::CheckFail("cuda: not a CUDA backend", 1); }

  // Measure baseline free memory immediately before prepare.
  std::size_t free_before = 0, total_before = 0;
  cudaDeviceSynchronize();
  cudaMemGetInfo(&free_before, &total_before);

  if (!be->prepare(1u << 20, 1u << 16, 1024)) { er_test::CheckFail("cuda: prepare failed", 1); return er_test::return_code(); }
  if (!be->bind_kernel("er-reference-kernel")) { er_test::CheckFail("cuda: bind kernel failed", 1); }
  if (!be->instantiate_graph(true)) { er_test::CheckFail("cuda: graph instantiate failed", 1); }

  // Warmup with parity against CPU reference.
  ComputeResult warm = be->warmup(1, 16);
  ComputeInput wref_in; wref_in.batch=1; wref_in.seq_len=16; wref_in.seed=42; wref_in.input.assign(16,0.0f);
  std::vector<float> wcpu = cpu_reference_compute(wref_in);
  ComputeResult wref; wref.ok=true; wref.output=wcpu; wref.seed=42;
  bool warm_parity = be->verify_parity(warm, wref);
  std::printf("cuda: warmup ok=%d parity=%s detail=%s\n", warm.ok?1:0, warm_parity?"OK":"MISMATCH", warm.detail.c_str());
  if (!warm_parity) er_test::CheckFail("cuda: warmup parity", 1);

  // Execute a deterministic real workload and verify parity.
  ComputeInput in; in.batch=1; in.seq_len=64; in.seed=1234;
  for (int i=0;i<64;++i) in.input.push_back((float)((i*7)%13));
  std::vector<float> cpu = cpu_reference_compute(in);
  ComputeResult cref; cref.ok=true; cref.output=cpu; cref.seed=in.seed;
  ComputeResult out = be->execute(in);
  bool parity = be->verify_parity(out, cref);
  std::printf("cuda: execute ok=%d parity=%s detail=%s bytes=%llu\n", out.ok?1:0, parity?"OK":"MISMATCH", out.detail.c_str(), (unsigned long long)out.bytes_moved);
  if (!parity) er_test::CheckFail("cuda: execute parity", 1);

  // Release and measure baseline restoration.
  be->release();
  cudaDeviceSynchronize();
  std::size_t free_after = 0, total_after = 0;
  cudaMemGetInfo(&free_after, &total_after);
  std::size_t used_before = total_before - free_before;
  std::size_t used_after = total_after - free_after;
  std::printf("cuda: used_before=%llu used_after=%llu delta=%lld\n",
              (unsigned long long)used_before, (unsigned long long)used_after,
              (long long)((long long)used_after - (long long)used_before));
  // Allow a small tolerance for driver/context variance; report exact numbers.
  long long delta = (long long)used_after - (long long)used_before;
  if (delta > (long long)(8u * 1024 * 1024)) er_test::CheckFail("cuda: device memory not restored within tolerance", 1);

  std::printf("cuda tests failures=%d\n", er_test::g_failures);
  return er_test::return_code();
}
