#ifndef ENGINE_RESIDENCY_CUDA_KERNELS_H
#define ENGINE_RESIDENCY_CUDA_KERNELS_H
#include <cuda_runtime.h>
#ifdef __cplusplus
extern "C" {
#endif
cudaError_t er_launch_reference_kernel(const float* in, float* out, int n, const unsigned int* seed, int blocks, int threads, cudaStream_t stream);
cudaError_t er_launch_zero_kernel(float* p, int n, int blocks, int threads, cudaStream_t stream);
#ifdef __cplusplus
}
#endif
#endif
