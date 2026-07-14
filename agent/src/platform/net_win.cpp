// Windows 소켓 열거 — iphlpapi GetExtendedTcpTable/UdpTable (native). 셸 미사용.
#if defined(_WIN32)
#include "platform/net.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <vector>
#include <string>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace wu::platform {

static std::string v4_str(DWORD addr) {
    struct in_addr a; a.S_un.S_addr = addr;
    char buf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf[0] ? std::string(buf) : std::string("*");
}

std::vector<PortEntry> list_ports(const std::string& proto, const std::string& state) {
    std::vector<PortEntry> out;
    const bool want_tcp = (proto == "tcp" || proto == "all" || proto.empty());
    const bool want_udp = (proto == "udp" || proto == "all" || proto.empty());
    const bool only_listen = (state == "listen");

    if (want_tcp) {
        DWORD sz = 0;
        GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        std::vector<char> buf(sz);
        if (GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET,
                                TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            auto* t = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                const auto& row = t->table[i];
                const bool listening = (row.dwState == MIB_TCP_STATE_LISTEN);
                if (only_listen && !listening) continue;
                PortEntry e;
                e.proto = "tcp";
                e.localPort = ntohs((u_short)row.dwLocalPort);
                e.state = listening ? "LISTEN" : "OTHER";
                e.pid = (int)row.dwOwningPid;
                e.localAddr = v4_str(row.dwLocalAddr);
                out.push_back(std::move(e));
            }
        }
    }
    if (want_udp && !only_listen) {
        DWORD sz = 0;
        GetExtendedUdpTable(nullptr, &sz, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
        std::vector<char> buf(sz);
        if (GetExtendedUdpTable(buf.data(), &sz, FALSE, AF_INET,
                                UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            auto* t = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                const auto& row = t->table[i];
                PortEntry e;
                e.proto = "udp";
                e.localPort = ntohs((u_short)row.dwLocalPort);
                e.pid = (int)row.dwOwningPid;
                e.localAddr = v4_str(row.dwLocalAddr);
                out.push_back(std::move(e));
            }
        }
    }
    return out;
}

} // namespace wu::platform
#endif // _WIN32
