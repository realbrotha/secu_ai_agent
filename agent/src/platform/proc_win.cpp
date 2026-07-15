// Windows 프로세스 열거 — Toolhelp32 (native).
#if defined(_WIN32)
#include "platform/proc.h"
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#pragma comment(lib, "advapi32.lib")

namespace wu::platform {

// 프로세스 토큰 소유자 SID → 계정명 (best-effort; 권한 부족 시 빈값).
static std::string proc_user(DWORD pid) {
    std::string result;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return result;
    HANDLE tok = nullptr;
    if (OpenProcessToken(h, TOKEN_QUERY, &tok)) {
        DWORD len = 0;
        GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
        if (len) {
            std::vector<char> buf(len);
            if (GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) {
                auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
                char name[256], domain[256];
                DWORD nlen = sizeof(name), dlen = sizeof(domain);
                SID_NAME_USE use;
                if (LookupAccountSidA(nullptr, tu->User.Sid, name, &nlen, domain, &dlen, &use))
                    result = name;
            }
        }
        CloseHandle(tok);
    }
    CloseHandle(h);
    return result;
}

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
            e.user = proc_user(pe.th32ProcessID);   // args 는 win 에서 best-effort 생략
            out.push_back(std::move(e));
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

} // namespace wu::platform
#endif
