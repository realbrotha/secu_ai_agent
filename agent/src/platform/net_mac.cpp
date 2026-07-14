// macOS 소켓 열거 — libproc (native, best-effort). 셸 미사용.
// 같은 사용자 소유 프로세스의 소켓은 root 없이 조회 가능(타 사용자는 스킵).
#if defined(__APPLE__)
#include "platform/net.h"

#include <libproc.h>
#include <sys/proc_info.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <vector>

namespace wu::platform {

static std::string fmt_v4(uint32_t addr_be) {
    struct in_addr a; a.s_addr = addr_be;
    char buf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf[0] ? std::string(buf) : std::string("*");
}

std::vector<PortEntry> list_ports(const std::string& proto, const std::string& state) {
    std::vector<PortEntry> out;
    const bool want_tcp = (proto == "tcp" || proto == "all" || proto.empty());
    const bool want_udp = (proto == "udp" || proto == "all" || proto.empty());
    const bool only_listen = (state == "listen");

    int cap = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (cap <= 0) return out;
    std::vector<pid_t> pids(cap / sizeof(pid_t) + 16, 0);
    int got = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                            (int)(pids.size() * sizeof(pid_t)));
    int npids = got / (int)sizeof(pid_t);

    for (int i = 0; i < npids; ++i) {
        pid_t pid = pids[i];
        if (pid <= 0) continue;

        int fsz = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
        if (fsz <= 0) continue;
        std::vector<proc_fdinfo> fds(fsz / sizeof(proc_fdinfo) + 1);
        fsz = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(),
                           (int)(fds.size() * sizeof(proc_fdinfo)));
        int nfds = fsz / (int)sizeof(proc_fdinfo);

        char pname[2 * MAXCOMLEN + 1] = {0};
        proc_name(pid, pname, sizeof(pname));

        for (int j = 0; j < nfds; ++j) {
            if (fds[j].proc_fdtype != PROX_FDTYPE_SOCKET) continue;

            struct socket_fdinfo si;
            std::memset(&si, 0, sizeof(si));
            int r = proc_pidfdinfo(pid, fds[j].proc_fd, PROC_PIDFDSOCKETINFO,
                                   &si, PROC_PIDFDSOCKETINFO_SIZE);
            if (r < (int)sizeof(si)) continue;

            const struct socket_info& so = si.psi;
            const int fam = so.soi_family;
            if (fam != AF_INET && fam != AF_INET6) continue;

            if (so.soi_kind == SOCKINFO_TCP) {
                if (!want_tcp) continue;
                const struct tcp_sockinfo& tcp = so.soi_proto.pri_tcp;
                const struct in_sockinfo& in = tcp.tcpsi_ini;
                const bool listening = (tcp.tcpsi_state == TSI_S_LISTEN);
                if (only_listen && !listening) continue;

                PortEntry e;
                e.proto = "tcp";
                e.localPort = ntohs((uint16_t)in.insi_lport);
                e.state = listening ? "LISTEN" : "OTHER";
                e.pid = (int)pid;
                e.process = pname;
                e.localAddr = (fam == AF_INET)
                    ? fmt_v4(in.insi_laddr.ina_46.i46a_addr4.s_addr) : "::";
                if (e.localPort > 0) out.push_back(std::move(e));
            } else if (so.soi_kind == SOCKINFO_IN) {
                if (!want_udp || only_listen) continue;  // udp 는 listen 개념 없음
                const struct in_sockinfo& in = so.soi_proto.pri_in;
                PortEntry e;
                e.proto = "udp";
                e.localPort = ntohs((uint16_t)in.insi_lport);
                e.pid = (int)pid;
                e.process = pname;
                e.localAddr = (fam == AF_INET)
                    ? fmt_v4(in.insi_laddr.ina_46.i46a_addr4.s_addr) : "::";
                if (e.localPort > 0) out.push_back(std::move(e));
            }
        }
    }
    return out;
}

} // namespace wu::platform
#endif // __APPLE__
