// op: pkg.query — 설치 패키지 조회. dpkg(/var/lib/dpkg/status, 텍스트) 네이티브.
//   rpm(rpmdb.sqlite/bdb)은 v1 미지원 → degraded=true, na. 비-리눅스 → na.
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace wu {

class PkgQueryOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "pkg.query"; d.summary = "설치 패키지 조회(dpkg 네이티브, rpm v1 미지원)";
        d.safety = Safety::read_only_local; d.os = {"linux"}; d.impl = "native";
        d.params = {
            { "name", "string", false, nullptr, {}, "패키지명 부분일치 필터(선택)" },
            { "status_path", "path", false, "/var/lib/dpkg/status", {}, "dpkg status 경로" },
        };
        d.returns = { {"manager","string","dpkg|rpm|unknown"}, {"packages","array<object>",""},
                      {"count","int",""}, {"degraded","bool",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext& ctx) override {
        const std::string statusPath = p.value("status_path", std::string("/var/lib/dpkg/status"));
        std::error_code ec;

        if (fs::exists(statusPath, ec)) {
            if (ctx.guard && !ctx.guard->allowed(statusPath)) return OpResult::err("path_guard 거부: " + statusPath);
            std::ifstream f(statusPath);
            if (!f) return OpResult::na("dpkg status 열기 실패");
            std::string filt = p.value("name", std::string(""));
            std::transform(filt.begin(), filt.end(), filt.begin(), ::tolower);

            json pkgs = json::array();
            std::string line, name, ver, st;
            auto flush = [&]() {
                if (name.empty()) return;
                bool installed = st.find("installed") != std::string::npos;
                std::string nl = name; std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
                if (installed && (filt.empty() || nl.find(filt) != std::string::npos))
                    pkgs.push_back({ {"name", name}, {"version", ver}, {"status", st} });
                name.clear(); ver.clear(); st.clear();
            };
            while (std::getline(f, line)) {
                if (line.empty()) { flush(); continue; }
                if (line.rfind("Package:", 0) == 0)      name = line.substr(8);
                else if (line.rfind("Version:", 0) == 0) ver  = line.substr(8);
                else if (line.rfind("Status:", 0) == 0)  st   = line.substr(7);
                auto t = [](std::string& s){ size_t a=s.find_first_not_of(" \t"); if(a!=std::string::npos) s=s.substr(a); };
                t(name); t(ver); t(st);
            }
            flush();
            return OpResult::ok_({ {"manager","dpkg"}, {"packages", pkgs},
                                   {"count", (int)pkgs.size()}, {"degraded", false} });
        }

        if (fs::exists("/var/lib/rpm", ec)) {
            return OpResult::na("rpm DB 파싱은 v1 미지원(degraded). dpkg 호스트에서만 네이티브 지원.");
        }
        return OpResult::na("패키지 관리자 없음(dpkg/rpm)");
    }
};
REGISTER_OP(PkgQueryOp)

} // namespace wu
