#include <cuda_runtime.h>

// Deterministic reference kernel matching er::cpu_reference_compute exactly,
// with no FMA contraction across statements. The seed is read from a device
// pointer so a CUDA graph can be parametric in the per-execution seed.
__global__ void er_reference_kernel(const float* in, float* out, int n, const unsigned int* seed) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) {
    unsigned int s = (seed != nullptr) ? *seed : 0u;
    float r = in[i];
    r = r * 0.0625f;                 // exact scale
    r = r + 0.5f;                    // exact add
    r = r + ((float)(i % 67)) * 0.01f;
    r = r + ((float)(s % 1000u)) * 0.001f;
    out[i] = r;
  }
}

__global__ void er_zero_kernel(float* p, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) p[i] = 0.0f;
}

extern "C" cudaError_t er_launch_reference_kernel(const float* in, float* out, int n, const unsigned int* seed, int blocks, int threads, cudaStream_t stream) {
  er_reference_kernel<<<blocks, threads, 0, stream>>>(in, out, n, seed);
  return cudaGetLastError();
}

extern "C" cudaError_t er_launch_zero_kernel(float* p, int n, int blocks, int threads, cudaStream_t stream) {
  er_zero_kernel<<<blocks, threads, 0, stream>>>(p, n);
  return cudaGetLastError();
}
