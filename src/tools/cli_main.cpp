#include "engine_residency/runtime.hpp"
#include "engine_residency/clock.hpp"
#include "engine_residency/engine.hpp"
#include "engine_residency/persistence.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
  std::printf("Engine Residency %s\n", ENGINE_RESIDENCY_VERSION_STRING);
  if (argc >= 3 && std::string(argv[1]) == "inspect") {
    std::ifstream in(argv[2], std::ios::binary);
    if (!in) { std::printf("cannot open %s\n", argv[2]); return 1; }
    std::string blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    engine_residency::PersistentState s;
    std::string err;
    if (!engine_residency::deserialize_persistent_state(blob, s, err)) {
      std::printf("reject: %s\n", err.c_str());
      return 1;
    }
    std::printf("epoch=%llu authority=%llu engines=%zu incarnations=%zu evidence=%zu\n",
                (unsigned long long)s.coordinator_epoch, (unsigned long long)s.authority_generation,
                s.engines.size(), s.incarnations.size(), s.evidence.size());
    for (const auto& e : s.engines) {
      std::printf("  engine=%llu name=%s role=%s\n",
                  (unsigned long long)e.engine_id.raw(), e.name.c_str(),
                  engine_residency::to_string(e.role));
    }
    for (const auto& i : s.incarnations) {
      std::printf("  incarn=%llu worker=%llu boot=%llu lifecycle=%s prep=%s recovery=%s\n",
                  (unsigned long long)i.incarnation_id.raw(), (unsigned long long)i.worker_id.raw(),
                  (unsigned long long)i.worker_boot.raw(),
                  engine_residency::to_string(i.state.lifecycle),
                  engine_residency::to_string(i.state.preparation),
                  engine_residency::to_string(i.state.recovery));
    }
    return 0;
  }
  std::printf("usage: er_cli inspect <state-file>\n");
  return 0;
}
