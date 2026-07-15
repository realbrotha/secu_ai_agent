// Linux 프로세스 열거 — /proc 파싱 (native).
#if defined(__linux__)
#include "platform/proc.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <pwd.h>
#include <cerrno>
#include <vector>

namespace fs = std::filesystem;

namespace wu::platform {

static std::string read_first_line(const std::string& p) {
    std::ifstream f(p); std::string s; std::getline(f, s); return s;
}

static std::string uid_to_name(int uid) {
    if (uid < 0) return {};
    struct passwd pw{}; struct passwd* res = nullptr;
    std::vector<char> buf(1024);
    while (getpwuid_r((uid_t)uid, &pw, buf.data(), buf.size(), &res) == ERANGE && buf.size() < (1 << 20))
        buf.resize(buf.size() * 2);
    return res && res->pw_name ? std::string(res->pw_name) : std::string();
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
        // cmdline (NUL 구분) → path/args
        std::ifstream cf(d.path().string() + "/cmdline", std::ios::binary);
        std::string cmd((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
        for (auto& c : cmd) if (c == '\0') c = ' ';
        while (!cmd.empty() && cmd.back() == ' ') cmd.pop_back();
        e.path = cmd;
        e.args = cmd;
        // stat 에서 ppid (4번째 필드)
        std::string stat = read_first_line(d.path().string() + "/stat");
        auto rp = stat.rfind(')');
        if (rp != std::string::npos) {
            std::istringstream ss(stat.substr(rp + 1)); std::string st; int ppid = 0;
            ss >> st >> ppid; e.ppid = ppid;
        }
        // /proc/PID/status 의 "Uid:\treal\teff\t..." → real uid
        {
            std::ifstream sf(d.path().string() + "/status");
            std::string ln;
            while (std::getline(sf, ln)) {
                if (ln.rfind("Uid:", 0) == 0) {
                    std::istringstream us(ln.substr(4)); int ruid = -1; us >> ruid;
                    e.uid = ruid; break;
                }
            }
        }
        e.user = uid_to_name(e.uid);
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace wu::platform
#endif
