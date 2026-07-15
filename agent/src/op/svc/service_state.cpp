// op: svc.service_state — 서비스 활성/부팅활성 상태(systemd 심링크 + xinetd + sysv). 셸 미사용.
//   systemd is-enabled 는 *.wants/ 심링크로 신뢰 도출. is-active 는 프로세스 매칭 best-effort(절충).
//   서비스 관리 기반 부재(mac/win) → na. rule:"disabled" → 비활성/미기동이면 pass.
#include "op/op.h"
#include "op/registry.h"
#include "platform/proc.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace wu {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// systemd: 어느 .wants 디렉터리든 <unit> 심링크가 있으면 enabled.
bool systemd_enabled(const std::string& unit, std::string& evidence) {
    const char* roots[] = { "/etc/systemd/system", "/lib/systemd/system", "/usr/lib/systemd/system" };
    std::string want = unit;
    if (want.find('.') == std::string::npos) want += ".service";
    std::error_code ec;
    for (auto* r : roots) {
        if (!fs::exists(r, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(r, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const auto& pth = it->path();
            if (pth.filename().string() == want &&
                pth.parent_path().filename().string().find(".wants") != std::string::npos) {
                evidence = pth.string(); return true;
            }
        }
    }
    return false;
}

bool systemd_unit_exists(const std::string& unit, std::string& file) {
    const char* roots[] = { "/etc/systemd/system", "/lib/systemd/system", "/usr/lib/systemd/system" };
    std::string want = unit;
    if (want.find('.') == std::string::npos) want += ".service";
    std::error_code ec;
    for (auto* r : roots) { fs::path f = fs::path(r) / want; if (fs::exists(f, ec)) { file = f.string(); return true; } }
    return false;
}

// xinetd: /etc/xinetd.d/<name> 의 "disable = no" 면 enabled.
int xinetd_state(const std::string& name, std::string& file) {  // -1 없음, 0 disabled, 1 enabled
    fs::path f = fs::path("/etc/xinetd.d") / name;
    std::error_code ec;
    if (!fs::exists(f, ec)) return -1;
    file = f.string();
    std::ifstream in(f); std::string line; int state = 1;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        if (t.rfind("disable", 0) == 0) {
            if (t.find("yes") != std::string::npos) state = 0;
            else if (t.find("no") != std::string::npos) state = 1;
        }
    }
    return state;
}

bool proc_running(const std::string& name) {
    std::string n = name; std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    for (auto& e : platform::list_procs()) {
        std::string pn = e.name; std::transform(pn.begin(), pn.end(), pn.begin(), ::tolower);
        if (pn.find(n) != std::string::npos) return true;
    }
    return false;
}

} // namespace

class SvcServiceStateOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "svc.service_state"; d.summary = "서비스 enabled/active(systemd 심링크+xinetd+proc)";
        d.safety = Safety::read_only_local; d.os = {"linux"}; d.impl = "native";
        d.params = {
            { "name",      "string", true,  nullptr, {}, "서비스/유닛 이름(예: telnet, sshd)" },
            { "mechanism", "enum",   false, "auto", { {}, {}, {"auto","systemd","xinetd"}, "" }, "탐지 기반" },
            { "proc_name", "string", false, nullptr, {}, "active 판정용 프로세스명(기본=name)" },
            { "rule",      "enum",   false, nullptr, { {}, {}, {"disabled"}, "" }, "disabled: 미활성/미기동이면 pass" },
        };
        d.returns = { {"mechanism","string",""}, {"enabled","bool",""}, {"active","bool|null",""},
                      {"unitFile","string",""}, {"verdict","string",""}, {"detail","string",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext&) override {
        const std::string name = p.at("name").get<std::string>();
        const std::string mech = p.value("mechanism", std::string("auto"));
        const std::string procName = p.value("proc_name", name);

        std::error_code ec;
        const bool hasSystemd = fs::exists("/lib/systemd/system", ec) || fs::exists("/etc/systemd/system", ec);
        const bool hasXinetd  = fs::exists("/etc/xinetd.d", ec);
        if (!hasSystemd && !hasXinetd)
            return OpResult::na("서비스 관리 기반 없음(비-리눅스/미설치)");

        std::string usedMech, unitFile, evidence;
        bool enabled = false; bool active = proc_running(procName);

        int xs = -1;
        if ((mech == "auto" || mech == "xinetd") && hasXinetd) {
            xs = xinetd_state(name, unitFile);
            if (xs >= 0) { usedMech = "xinetd"; enabled = (xs == 1); }
        }
        if (usedMech.empty() && (mech == "auto" || mech == "systemd") && hasSystemd) {
            usedMech = "systemd";
            enabled = systemd_enabled(name, evidence);
            std::string uf; systemd_unit_exists(name, uf); if (unitFile.empty()) unitFile = uf;
        }
        if (usedMech.empty()) usedMech = hasSystemd ? "systemd" : "xinetd";

        json out = { {"mechanism", usedMech}, {"enabled", enabled}, {"active", active},
                     {"unitFile", unitFile}, {"evidence", evidence} };

        if (p.value("rule", std::string("")) == "disabled") {
            bool ok = !enabled && !active;
            out["verdict"] = ok ? "pass" : "fail";
            out["detail"]  = ok ? (name + " 비활성/미기동")
                                : (name + std::string(enabled ? " enabled" : "") + std::string(active ? " running" : ""));
        }
        return OpResult::ok_(out);
    }
};
REGISTER_OP(SvcServiceStateOp)

} // namespace wu
