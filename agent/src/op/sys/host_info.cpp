// op: sys.host_info — OS/arch/hostname + 배포판(리눅스) + IP 주소 (cross-platform)
#include "op/op.h"
#include "op/registry.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

namespace wu {

static std::string strip_quotes(std::string s) {
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
        s = s.substr(1, s.size() - 2);
    return s;
}

// /etc/os-release 파싱 → {id, id_like, version_id}. 리눅스 외에는 비어있음.
static void read_os_release(std::string& id, std::string& idLike, std::string& ver) {
    std::ifstream f("/etc/os-release");
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = strip_quotes(line.substr(eq + 1));
        if (k == "ID")            id = v;
        else if (k == "ID_LIKE")  idLike = v;
        else if (k == "VERSION_ID") ver = v;
    }
}

static std::string distro_family(const std::string& id, const std::string& idLike) {
    std::string all = id + " " + idLike;
    std::transform(all.begin(), all.end(), all.begin(), ::tolower);
    auto has = [&](const char* k){ return all.find(k) != std::string::npos; };
    if (has("rhel") || has("fedora") || has("centos") || has("rocky") || has("alma")) return "rhel";
    if (has("debian") || has("ubuntu"))  return "debian";
    if (has("suse") || has("sles"))      return "suse";
    if (has("arch"))                     return "arch";
    return id.empty() ? "" : "other";
}

static json list_ips() {
    json arr = json::array();
#if defined(_WIN32)
    ULONG sz = 15000;
    std::vector<char> buf(sz);
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                             nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &sz) == NO_ERROR) {
        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                char ip[INET6_ADDRSTRLEN] = {0};
                if (getnameinfo(u->Address.lpSockaddr, (socklen_t)u->Address.iSockaddrLength,
                                ip, sizeof(ip), nullptr, 0, NI_NUMERICHOST) == 0 && ip[0])
                    arr.push_back(ip);
            }
        }
    }
#else
    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) == 0) {
        for (auto* p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr) continue;
            int fam = p->ifa_addr->sa_family;
            if (fam != AF_INET && fam != AF_INET6) continue;
            char ip[INET6_ADDRSTRLEN] = {0};
            socklen_t sl = (fam == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
            if (getnameinfo(p->ifa_addr, sl, ip, sizeof(ip), nullptr, 0, NI_NUMERICHOST) == 0 && ip[0]) {
                std::string s(ip);
                if (s != "127.0.0.1" && s.rfind("::1", 0) != 0 && s.rfind("fe80", 0) != 0)
                    arr.push_back(s);
            }
        }
        freeifaddrs(ifa);
    }
#endif
    return arr;
}

class SysHostInfoOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name    = "sys.host_info";
        d.summary = "호스트 OS/아키텍처/hostname/배포판/IP 정보를 반환한다";
        d.safety  = Safety::read_only_local;
        d.os      = {"linux", "windows", "macos"};
        d.impl    = "native";
        d.returns = { {"os","string",""}, {"arch","string",""}, {"hostname","string",""}, {"kernel","string",""},
                      {"distro","string",""}, {"distroFamily","string","rhel|debian|suse|arch|other"},
                      {"distroVersion","string",""}, {"ipAddresses","array<string>",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json&, OpContext& ctx) override {
        json out;
#if defined(_WIN32)
        char host[256] = {0};
        DWORD n = sizeof(host);
        GetComputerNameA(host, &n);
        out = { {"os","windows"}, {"arch","x86_64"}, {"hostname", host}, {"kernel","windows"} };
#else
        struct utsname u{};
        char host[256] = {0};
        gethostname(host, sizeof(host));
        if (uname(&u) == 0) {
            out = {
                {"os", ctx.host.os.empty() ? std::string(u.sysname) : ctx.host.os},
                {"arch", u.machine},
                {"hostname", host[0] ? host : u.nodename},
                {"kernel", std::string(u.sysname) + " " + u.release},
            };
        } else {
            out = { {"os", ctx.host.os}, {"arch",""}, {"hostname", host}, {"kernel",""} };
        }
#endif
        std::string id, idLike, ver;
        read_os_release(id, idLike, ver);
        out["distro"]        = id;
        out["distroFamily"]  = distro_family(id, idLike);
        out["distroVersion"] = ver;
        out["ipAddresses"]   = list_ips();
        return OpResult::ok_(out);
    }
};

REGISTER_OP(SysHostInfoOp)

} // namespace wu
