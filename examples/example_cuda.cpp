#include "engine_residency/backend.hpp"
#include <cstdio>
int main() {
  auto be = engine_residency::make_cuda_reference_backend();
  auto d = be->discover(0);
  std::printf("example cuda: %s cc=%s\n", d.device_name.c_str(), d.compute_capability.c_str());
  be->prepare(1u<<20, 1u<<16, 1024);
  be->bind_kernel("er-reference-kernel");
  be->instantiate_graph(true);
  auto warm = be->warmup(1, 16);
  std::printf("example cuda: warmup ok=%d detail=%s\n", warm.ok?1:0, warm.detail.c_str());
  be->release();
  return 0;
}
