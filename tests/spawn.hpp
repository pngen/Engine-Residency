#ifndef ENGINE_RESIDENCY_SPAWN_HPP
#define ENGINE_RESIDENCY_SPAWN_HPP
#include <cstdint>
#include <string>
// Isolates the Windows OS-process API away from the engine headers so their
// enums are never polluted by windows.h macros.
bool er_spawn_process(const std::string& exe, const std::string& args);
void er_kill_process();
bool er_process_running();
#endif
