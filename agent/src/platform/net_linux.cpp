// Linux 소켓 열거 — /proc/net/{tcp,tcp6,udp,udp6} 파싱 + inode→pid 매핑(/proc/*/fd). 셸 미사용.
#if defined(__linux__)
#include "platform/net.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <utility>

namespace fs = std::filesystem;

namespace wu::platform {

namespace {

// "0100007F:1F90" → port(hex, 뒤 2바이트)
int parse_hex_port(const std::string& hexpair) {
    auto pos = hexpair.find(':');
    if (pos == std::string::npos) return 0;
    return (int)std::strtol(hexpair.substr(pos + 1).c_str(), nullptr, 16);
}

// 리틀엔디안 hex v4 주소("0100007F" = 127.0.0.1) → 표시용 dotted
std::string parse_hex_addr_v4(const std::string& hexpair) {
    auto pos = hexpair.find(':');
    std::string h = pos == std::string::npos ? hexpair : hexpair.substr(0, pos);
    if (h.size() != 8) return "*";
    unsigned long v = std::strtoul(h.c_str(), nullptr, 16);
    unsigned b0 = v & 0xff, b1 = (v >> 8) & 0xff, b2 = (v >> 16) & 0xff, b3 = (v >> 24) & 0xff;
    char buf[16]; std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b0, b1, b2, b3);
    return buf;
}

std::string tcp_state(const std::string& sthex) {
    long s = std::strtol(sthex.c_str(), nullptr, 16);
    if (s == 0x0A) return "LISTEN";
    return "OTHER";
}

// inode → (pid, comm) 매핑: /proc/*/fd/* 심링크가 "socket:[inode]" 인 것 수집.
std::unordered_map<std::string, std::pair<int,std::string>> build_inode_map() {
    std::unordered_map<std::string, std::pair<int,std::string>> map;
    std::error_code ec;
    for (const auto& d : fs::directory_iterator("/proc", ec)) {
        const std::string pidname = d.path().filename().string();
        if (pidname.empty() || !std::all_of(pidname.begin(), pidname.end(), ::isdigit)) continue;
        int pid = std::atoi(pidname.c_str());
        std::string comm;
        { std::ifstream cf(d.path().string() + "/comm"); std::getline(cf, comm); }
        std::error_code ec2;
        for (const auto& fd : fs::directory_iterator(d.path() / "fd", ec2)) {
            std::error_code ec3;
            auto tgt = fs::read_symlink(fd.path(), ec3);
            if (ec3) continue;
            const std::string s = tgt.string();
            if (s.rfind("socket:[", 0) == 0 && s.back() == ']')
                map[s.substr(8, s.size() - 9)] = { pid, comm };
        }
    }
    return map;
}

void scan(const std::string& path, const std::string& proto, bool is_tcp,
          const std::string& state, bool v4,
          const std::unordered_map<std::string, std::pair<int,std::string>>& imap,
          std::vector<PortEntry>& out) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    std::getline(f, line);  // 헤더
    const bool only_listen = (state == "listen");
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        // 필드: sl local rem st tx:rx tr:when retrnsmt uid timeout inode ...
        std::string idx, local, rem, st, txrx, trwhen, retr, uid, timeout, inode;
        ss >> idx >> local >> rem >> st >> txrx >> trwhen >> retr >> uid >> timeout >> inode;
        if (local.empty()) continue;
        PortEntry e;
        e.proto = proto;
        e.localPort = parse_hex_port(local);
        if (e.localPort <= 0) continue;
        e.localAddr = v4 ? parse_hex_addr_v4(local) : "::";
        if (is_tcp) {
            e.state = tcp_state(st);
            if (only_listen && e.state != "LISTEN") continue;
        } else {
            if (only_listen) continue;
        }
        if (!inode.empty()) {
            auto it = imap.find(inode);
            if (it != imap.end()) { e.pid = it->second.first; e.process = it->second.second; }
        }
        out.push_back(std::move(e));
    }
}

} // namespace

std::vector<PortEntry> list_ports(const std::string& proto, const std::string& state) {
    std::vector<PortEntry> out;
    const bool want_tcp = (proto == "tcp" || proto == "all" || proto.empty());
    const bool want_udp = (proto == "udp" || proto == "all" || proto.empty());
    auto imap = build_inode_map();
    if (want_tcp) { scan("/proc/net/tcp",  "tcp", true,  state, true,  imap, out);
                    scan("/proc/net/tcp6", "tcp", true,  state, false, imap, out); }
    if (want_udp) { scan("/proc/net/udp",  "udp", false, state, true,  imap, out);
                    scan("/proc/net/udp6", "udp", false, state, false, imap, out); }
    return out;
}

} // namespace wu::platform
#endif // __linux__
