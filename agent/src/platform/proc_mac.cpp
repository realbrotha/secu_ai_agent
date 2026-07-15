// macOS 프로세스 열거 — libproc (native).
#if defined(__APPLE__)
#include "platform/proc.h"
#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <pwd.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

namespace wu::platform {

static std::string uid_to_name(int uid) {
    if (uid < 0) return {};
    struct passwd pw{}; struct passwd* res = nullptr;
    std::vector<char> buf(1024);
    while (getpwuid_r((uid_t)uid, &pw, buf.data(), buf.size(), &res) == ERANGE && buf.size() < (1 << 20))
        buf.resize(buf.size() * 2);
    return res && res->pw_name ? std::string(res->pw_name) : std::string();
}

// KERN_PROCARGS2 로 전체 커맨드라인 인자 조회(best-effort).
static std::string proc_args(pid_t pid) {
    int mib[3] = { CTL_KERN, KERN_PROCARGS2, (int)pid };
    size_t sz = 0;
    if (sysctl(mib, 3, nullptr, &sz, nullptr, 0) != 0 || sz == 0) return {};
    std::vector<char> buf(sz);
    if (sysctl(mib, 3, buf.data(), &sz, nullptr, 0) != 0) return {};
    if (sz < sizeof(int)) return {};
    int argc = 0; std::memcpy(&argc, buf.data(), sizeof(int));
    size_t pos = sizeof(int);
    // exec_path (NUL 종료) 스킵
    while (pos < sz && buf[pos] != '\0') ++pos;
    while (pos < sz && buf[pos] == '\0') ++pos;
    std::string out;
    int seen = 0;
    while (pos < sz && seen < argc) {
        std::string a;
        while (pos < sz && buf[pos] != '\0') a.push_back(buf[pos++]);
        while (pos < sz && buf[pos] == '\0') ++pos;
        if (!a.empty()) { if (!out.empty()) out += ' '; out += a; }
        ++seen;
    }
    return out;
}

std::vector<ProcEntry> list_procs() {
    std::vector<ProcEntry> out;
    int cap = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (cap <= 0) return out;
    std::vector<pid_t> pids(cap / sizeof(pid_t) + 16, 0);
    int got = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), (int)(pids.size() * sizeof(pid_t)));
    int n = got / (int)sizeof(pid_t);
    for (int i = 0; i < n; ++i) {
        pid_t pid = pids[i];
        if (pid <= 0) continue;
        ProcEntry e; e.pid = (int)pid;
        char name[2 * MAXCOMLEN + 1] = {0};
        if (proc_name(pid, name, sizeof(name)) > 0) e.name = name;
        char path[PROC_PIDPATHINFO_MAXSIZE] = {0};
        if (proc_pidpath(pid, path, sizeof(path)) > 0) e.path = path;
        struct proc_bsdshortinfo bsd;
        if (proc_pidinfo(pid, PROC_PIDT_SHORTBSDINFO, 0, &bsd, sizeof(bsd)) == sizeof(bsd)) {
            e.ppid = (int)bsd.pbsi_ppid;
            e.uid  = (int)bsd.pbsi_uid;
        }
        e.user = uid_to_name(e.uid);
        e.args = proc_args(pid);
        if (e.args.empty()) e.args = e.path;
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace wu::platform
#endif
