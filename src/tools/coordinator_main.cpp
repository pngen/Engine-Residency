#include "coordinator/session.hpp"
#include <cstdint>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
  std::uint16_t port = 27200;
  std::string state_path;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--state") state_path = argv[++i];
    else port = (std::uint16_t)std::atoi(a.c_str());
  }
  int rc = engine_residency::run_coordinator(port, state_path);
  std::printf("coordinator exit %d\n", rc);
  return rc;
}