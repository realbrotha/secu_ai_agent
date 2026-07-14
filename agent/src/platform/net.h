#pragma once
// 네트워크 소켓 열거 백엔드 (OS 분기 격리). op 는 이 인터페이스만 본다.
#include <string>
#include <vector>

namespace wu::platform {

struct PortEntry {
    std::string proto;       // "tcp" | "udp"
    std::string localAddr;   // 표시용 (v4 dotted, v6 는 "::", 미상 "*")
    int         localPort = 0;
    std::string state;       // "LISTEN" | "OTHER" | "" (udp)
    int         pid = 0;     // 0 = 미상
    std::string process;     // 프로세스명 (가능 시)
};

// proto: "tcp"|"udp"|"all",  state: "listen"|"established"|"all"
std::vector<PortEntry> list_ports(const std::string& proto, const std::string& state);

} // namespace wu::platform
