#pragma once
// 프로세스 열거 백엔드 (OS 분기 격리).
#include <string>
#include <vector>

namespace wu::platform {

struct ProcEntry {
    int         pid = 0;
    int         ppid = 0;
    std::string name;   // 실행 파일명(comm)
    std::string path;   // 실행 경로 또는 전체 커맨드라인(가능 시)
    int         uid = -1;   // 실행 uid (-1=미상)
    std::string user;       // uid → 이름(가능 시)
    std::string args;       // 전체 커맨드라인 인자(가능 시; win 은 best-effort/빈값)
};

std::vector<ProcEntry> list_procs();

} // namespace wu::platform
