#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "spawn.hpp"
#include <string>

static PROCESS_INFORMATION g_pi{};

bool er_spawn_process(const std::string& exe, const std::string& args) {
  STARTUPINFOA si{}; si.cb = sizeof(si);
  std::string cmd = "\"" + exe + "\" " + args;
  return CreateProcessA(NULL, (LPSTR)cmd.data(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &g_pi) != 0;
}
void er_kill_process() {
  if (g_pi.hProcess) { TerminateProcess(g_pi.hProcess, 0); WaitForSingleObject(g_pi.hProcess, 3000); CloseHandle(g_pi.hThread); CloseHandle(g_pi.hProcess); g_pi.hProcess = NULL; }
}
bool er_process_running() { return g_pi.hProcess != NULL; }
