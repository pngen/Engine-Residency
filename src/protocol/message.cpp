#include "engine_residency/protocol.hpp"

#include <map>
#include <sstream>
#include <string>

namespace engine_residency {
namespace detail {

std::string make_payload(const std::map<std::string, std::string>& fields) {
  std::string out;
  for (const auto& kv : fields) { out += kv.first; out += "="; out += kv.second; out += "\n"; }
  return out;
}

std::map<std::string, std::string> parse_payload(const std::string& payload) {
  std::map<std::string, std::string> out;
  std::istringstream ss(payload);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty()) continue;
    std::size_t eq = line.find("=");
    if (eq == std::string::npos) continue;
    out[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return out;
}

std::string get(const std::map<std::string, std::string>& f, const std::string& k, const std::string& d) {
  auto it = f.find(k); return it == f.end() ? d : it->second;
}

}  // namespace detail
}  // namespace engine_residency