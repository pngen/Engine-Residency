#include "coordinator/session.hpp"
#include <cstdint>
#include <cstdio>

int main(int argc, char** argv) {
  std::uint16_t port = 27200;
  if (argc > 1) port = (std::uint16_t)std::atoi(argv[1]);
  int rc = engine_residency::run_coordinator(port);
  std::printf("coordinator exit %d\n", rc);
  return rc;
}
