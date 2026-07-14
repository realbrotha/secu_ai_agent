#pragma once
// 프로세스 열거 백엔드 (OS 분기 격리).
#include <string>
#include <vector>

namespace wu::platform {

struct ProcEntry {
    int         pid = 0;
    int         ppid = 0;
    std::string name;   // 실행 파일명
    std::string path;   // 실행 경로(가능 시)
};

std::vector<ProcEntry> list_procs();

} // namespace wu::platform
