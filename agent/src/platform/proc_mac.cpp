// macOS 프로세스 열거 — libproc (native).
#if defined(__APPLE__)
#include "platform/proc.h"
#include <libproc.h>
#include <sys/proc_info.h>
#include <vector>

namespace wu::platform {

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
        if (proc_pidinfo(pid, PROC_PIDT_SHORTBSDINFO, 0, &bsd, sizeof(bsd)) == sizeof(bsd))
            e.ppid = (int)bsd.pbsi_ppid;
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace wu::platform
#endif
