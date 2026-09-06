#ifndef ENGINE_RESIDENCY_SPAWN_HPP
#define ENGINE_RESIDENCY_SPAWN_HPP
#include <cstdint>
#include <string>
// Isolates the Windows OS-process API away from the engine headers so their
// enums are never polluted by windows.h macros.
struct ErProcess { void* handle{nullptr}; void* thread{nullptr}; std::uint32_t pid{0}; bool alive() const; };
bool er_spawn_process(const std::string& exe, const std::string& args);
void er_kill_process();
bool er_process_running();
bool er_spawn(const std::string& exe, const std::string& args, ErProcess& out);
void er_kill(ErProcess& p);
bool er_wait_alive(ErProcess& p);
void er_sleep(std::uint32_t ms);
#endif
