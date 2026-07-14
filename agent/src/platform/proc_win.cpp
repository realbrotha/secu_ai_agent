// Windows 프로세스 열거 — Toolhelp32 (native).
#if defined(_WIN32)
#include "platform/proc.h"
#include <windows.h>
#include <tlhelp32.h>
#include <vector>

namespace wu::platform {

std::vector<ProcEntry> list_procs() {
    std::vector<ProcEntry> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            ProcEntry e;
            e.pid  = (int)pe.th32ProcessID;
            e.ppid = (int)pe.th32ParentProcessID;
            e.name = pe.szExeFile;
            out.push_back(std::move(e));
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

} // namespace wu::platform
#endif
