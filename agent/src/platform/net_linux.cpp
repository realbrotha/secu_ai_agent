// Linux 소켓 열거 — /proc/net/{tcp,tcp6,udp,udp6} 파싱 (native). 셸 미사용.
// Phase 3: 포트/상태 파싱. pid 매핑(inode→/proc/*/fd)은 후속 강화(TODO).
#if defined(__linux__)
#include "platform/net.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace wu::platform {

namespace {

// /proc/net/tcp 의 hex "0100007F:1F90" (addr:port, addr 는 리틀엔디안 hex)
int parse_hex_port(const std::string& hexpair) {
    auto pos = hexpair.find(':');
    if (pos == std::string::npos) return 0;
    return (int)std::strtol(hexpair.substr(pos + 1).c_str(), nullptr, 16);
}

// TCP state hex: 0A = LISTEN, 01 = ESTABLISHED
std::string tcp_state(const std::string& sthex) {
    long s = std::strtol(sthex.c_str(), nullptr, 16);
    if (s == 0x0A) return "LISTEN";
    if (s == 0x01) return "OTHER";  // ESTABLISHED 등
    return "OTHER";
}

void scan(const std::string& path, const std::string& proto, bool is_tcp,
          const std::string& state, std::vector<PortEntry>& out) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    std::getline(f, line);  // 헤더 스킵
    const bool only_listen = (state == "listen");
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string idx, local, rem, st;
        ss >> idx >> local >> rem >> st;
        if (local.empty()) continue;
        PortEntry e;
        e.proto = proto;
        e.localPort = parse_hex_port(local);
        if (e.localPort <= 0) continue;
        if (is_tcp) {
            e.state = tcp_state(st);
            if (only_listen && e.state != "LISTEN") continue;
        } else {
            if (only_listen) continue;  // udp
        }
        out.push_back(std::move(e));
    }
}

} // namespace

std::vector<PortEntry> list_ports(const std::string& proto, const std::string& state) {
    std::vector<PortEntry> out;
    const bool want_tcp = (proto == "tcp" || proto == "all" || proto.empty());
    const bool want_udp = (proto == "udp" || proto == "all" || proto.empty());
    if (want_tcp) { scan("/proc/net/tcp",  "tcp", true, state, out);
                    scan("/proc/net/tcp6", "tcp", true, state, out); }
    if (want_udp) { scan("/proc/net/udp",  "udp", false, state, out);
                    scan("/proc/net/udp6", "udp", false, state, out); }
    return out;
}

} // namespace wu::platform
#endif // __linux__
