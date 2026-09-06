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

bool ErProcess::alive() const {
  if (!handle) return false;
  DWORD code = 0;
  return GetExitCodeProcess((HANDLE)handle, &code) && code == STILL_ACTIVE;
}

bool er_spawn(const std::string& exe, const std::string& args, ErProcess& out) {
  STARTUPINFOA si{}; si.cb = sizeof(si);
  std::string cmd = "\"" + exe + "\" " + args;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(NULL, (LPSTR)cmd.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
  out.handle = pi.hProcess; out.thread = pi.hThread; out.pid = (std::uint32_t)pi.dwProcessId;
  return true;
}
void er_kill(ErProcess& p) {
  if (!p.handle) return;
  TerminateProcess((HANDLE)p.handle, 0);
  WaitForSingleObject((HANDLE)p.handle, 3000);
  CloseHandle((HANDLE)p.thread); CloseHandle((HANDLE)p.handle);
  p.handle = nullptr; p.thread = nullptr;
}
bool er_wait_alive(ErProcess& p) { return p.alive(); }
void er_sleep(std::uint32_t ms) { Sleep(ms); }
