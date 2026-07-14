// op: sys.detect_was_web — 호스트의 WAS/WEB 제품·경로 자동 탐지 (native, read-only).
//   경로(설치 관례) + 프로세스 + 리스닝 포트 로 추정. (D3)
#include "op/op.h"
#include "op/registry.h"
#include "platform/proc.h"
#include "platform/net.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace wu {

namespace {
bool exists(const std::string& p) { std::error_code ec; return fs::exists(p, ec); }

// 프로세스 이름에 키워드 포함 여부
bool proc_has(const std::vector<platform::ProcEntry>& ps, const std::string& kw) {
    for (const auto& p : ps) {
        std::string n = p.name + " " + p.path;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (n.find(kw) != std::string::npos) return true;
    }
    return false;
}
bool port_listen(const std::vector<platform::PortEntry>& ports, int port) {
    for (const auto& e : ports) if (e.localPort == port && e.state == "LISTEN") return true;
    return false;
}
} // namespace

class SysDetectWasWebOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "sys.detect_was_web"; d.summary = "WAS/WEB 제품·홈경로 자동 탐지(경로+프로세스+포트)";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.returns = { {"was","array<object>","[{product,home}]"}, {"web","array<object>","[{product,root}]"} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json&, OpContext&) override {
        auto procs = platform::list_procs();
        auto ports = platform::list_ports("tcp", "listen");

        json was = json::array(), web = json::array();

        // ── WAS: Tomcat ──
        for (const char* home : { "/opt/tomcat", "/usr/local/tomcat", "/usr/share/tomcat",
                                  "/opt/apache-tomcat", "C:/tomcat", "C:/Program Files/Apache Software Foundation/Tomcat" }) {
            if (exists(std::string(home) + "/conf")) {
                was.push_back({ {"product","tomcat"}, {"home", home},
                                {"evidence","path"} });
            }
        }
        // 프로세스/포트 힌트 (경로 못 찾았을 때)
        if (was.empty() && (proc_has(procs, "catalina") || proc_has(procs, "tomcat")))
            was.push_back({ {"product","tomcat"}, {"home",""}, {"evidence","process"} });
        if (was.empty() && port_listen(ports, 8080))
            was.push_back({ {"product","tomcat?"}, {"home",""}, {"evidence","port:8080"} });

        // ── WEB: Apache / Nginx ──
        for (const char* root : { "/etc/httpd", "/etc/apache2", "/usr/local/apache2" }) {
            if (exists(std::string(root) + "/conf") || exists(std::string(root) + "/apache2.conf") ||
                exists(std::string(root) + "/httpd.conf"))
                web.push_back({ {"product","apache"}, {"root", root}, {"evidence","path"} });
        }
        for (const char* root : { "/etc/nginx", "/usr/local/nginx/conf" }) {
            if (exists(std::string(root) + "/nginx.conf") || exists(root))
                web.push_back({ {"product","nginx"}, {"root", root}, {"evidence","path"} });
        }
        if (web.empty() && proc_has(procs, "httpd"))  web.push_back({ {"product","apache"}, {"root",""}, {"evidence","process"} });
        if (web.empty() && proc_has(procs, "nginx"))  web.push_back({ {"product","nginx"},  {"root",""}, {"evidence","process"} });

        return OpResult::ok_({ {"was", was}, {"web", web} });
    }
};
REGISTER_OP(SysDetectWasWebOp)

} // namespace wu
