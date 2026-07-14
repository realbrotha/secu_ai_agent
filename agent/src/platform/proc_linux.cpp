// Linux 프로세스 열거 — /proc 파싱 (native).
#if defined(__linux__)
#include "platform/proc.h"
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace wu::platform {

static std::string read_first_line(const std::string& p) {
    std::ifstream f(p); std::string s; std::getline(f, s); return s;
}

std::vector<ProcEntry> list_procs() {
    std::vector<ProcEntry> out;
    std::error_code ec;
    for (const auto& d : fs::directory_iterator("/proc", ec)) {
        if (!d.is_directory(ec)) continue;
        const std::string name = d.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), ::isdigit)) continue;
        ProcEntry e; e.pid = std::atoi(name.c_str());
        e.name = read_first_line(d.path().string() + "/comm");
        // cmdline (NUL 구분) → path
        std::ifstream cf(d.path().string() + "/cmdline", std::ios::binary);
        std::string cmd((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
        for (auto& c : cmd) if (c == '\0') c = ' ';
        e.path = cmd;
        // stat 에서 ppid (4번째 필드)
        std::string stat = read_first_line(d.path().string() + "/stat");
        auto rp = stat.rfind(')');
        if (rp != std::string::npos) {
            std::istringstream ss(stat.substr(rp + 1)); std::string st; int ppid = 0;
            ss >> st >> ppid; e.ppid = ppid;
        }
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace wu::platform
#endif
