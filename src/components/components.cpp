#include "engine_residency/components.hpp"
#include <string>

namespace engine_residency {
std::string describe_component(const ComponentEvidence& e) {
  std::string s = e.subject + ":" + std::string(to_string(e.state));
  return s;
}
}  // namespace engine_residency
