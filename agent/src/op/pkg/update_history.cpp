// op: pkg.update_history — 최근 패키지 변경 이력(apt/dpkg 텍스트 로그). 패치검사(5.1) 증적용.
//   dnf/yum(history.sqlite)은 v1 미지원. 이력만 관찰하며 최신성 판정은 하지 않음(→ 체크는 na/needs_review).
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

namespace wu {

class PkgUpdateHistoryOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "pkg.update_history"; d.summary = "apt/dpkg 로그의 최근 패키지 변경 이력";
        d.safety = Safety::read_only_local; d.os = {"linux"}; d.impl = "native";
        d.params = { { "limit", "int", false, 50L, {}, "최대 이벤트 수" } };
        d.returns = { {"events","array<object>",""}, {"count","int",""}, {"lastDate","string",""}, {"source","string",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext&) override {
        const long limit = p.value("limit", 50L);
        std::error_code ec;
        json events = json::array(); std::string lastDate, source;

        // apt history.log: "Start-Date: ...", "Commandline: ...", "Upgrade: pkg (a, b), ..."
        const std::string aptLog = "/var/log/apt/history.log";
        if (fs::exists(aptLog, ec)) {
            source = aptLog;
            std::ifstream f(aptLog); std::string line, start, cmd;
            while (std::getline(f, line)) {
                if (line.rfind("Start-Date:", 0) == 0) start = line.substr(11);
                else if (line.rfind("Commandline:", 0) == 0) cmd = line.substr(12);
                else if (line.empty() && !start.empty()) {
                    events.push_back({ {"date", start}, {"command", cmd} });
                    if (!start.empty()) lastDate = start;
                    start.clear(); cmd.clear();
                    if ((long)events.size() >= limit) break;
                }
            }
            return OpResult::ok_({ {"events", events}, {"count", (int)events.size()},
                                   {"lastDate", lastDate}, {"source", source} });
        }

        // dpkg.log: "YYYY-MM-DD HH:MM:SS action pkg version"
        const std::string dpkgLog = "/var/log/dpkg.log";
        if (fs::exists(dpkgLog, ec)) {
            source = dpkgLog;
            std::ifstream f(dpkgLog); std::string line;
            std::vector<std::string> lines;
            while (std::getline(f, line)) if (line.find(" upgrade ") != std::string::npos ||
                                              line.find(" install ") != std::string::npos) lines.push_back(line);
            long start = (long)lines.size() > limit ? (long)lines.size() - limit : 0;
            for (long i = start; i < (long)lines.size(); ++i) {
                std::istringstream ss(lines[i]); std::string date, time, action, pkg, ver;
                ss >> date >> time >> action >> pkg >> ver;
                events.push_back({ {"date", date + " " + time}, {"action", action}, {"package", pkg}, {"version", ver} });
                lastDate = date + " " + time;
            }
            return OpResult::ok_({ {"events", events}, {"count", (int)events.size()},
                                   {"lastDate", lastDate}, {"source", source} });
        }

        return OpResult::na("패키지 이력 로그 없음(apt/dpkg)");
    }
};
REGISTER_OP(PkgUpdateHistoryOp)

} // namespace wu
