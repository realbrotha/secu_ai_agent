// op: config.pam — PAM 스택 파서(/etc/pam.d/*) + module/arg 판정.
//   여러 후보 파일(RHEL system-auth / Debian common-*)을 받아 존재하는 것들을 병합.
//   observe: rules[{type,control,module,args}] | rule 모드: module_arg(present|num_ge|num_le|eq).
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace wu {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

struct PamRule { std::string type, control, module; std::vector<std::string> args; std::string source; };

void parse_file(const std::string& path, const PathGuard* guard, std::vector<PamRule>& out, int depth) {
    if (depth > 8) return;
    if (guard && !guard->allowed(path)) return;
    std::ifstream f(path); if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        std::istringstream ss(t);
        std::vector<std::string> tok; std::string w;
        while (ss >> w) tok.push_back(w);
        if (tok.empty()) continue;
        // @include <service>
        if (tok[0] == "@include" && tok.size() >= 2) {
            fs::path inc = fs::path(path).parent_path() / tok[1];
            parse_file(inc.string(), guard, out, depth + 1);
            continue;
        }
        if (tok.size() < 2) continue;
        PamRule r; r.type = tok[0]; r.source = path;
        size_t i = 1;
        // control 이 [ ... ] 형태면 묶기
        if (tok[i][0] == '[') {
            std::string ctl = tok[i];
            while (ctl.find(']') == std::string::npos && ++i < tok.size()) ctl += " " + tok[i];
            r.control = ctl; ++i;
        } else { r.control = tok[i]; ++i; }
        if (i < tok.size()) r.module = tok[i++];
        // include/substack 유형
        if ((r.module == "include" || r.module == "substack") && i < tok.size()) {
            fs::path inc = fs::path(path).parent_path() / tok[i];
            parse_file(inc.string(), guard, out, depth + 1);
        }
        for (; i < tok.size(); ++i) r.args.push_back(tok[i]);
        out.push_back(std::move(r));
    }
}

// args 에서 "name=value" 또는 "name" 조회
bool arg_value(const PamRule& r, const std::string& name, std::string& val, bool& hasVal) {
    for (auto& a : r.args) {
        auto eq = a.find('=');
        if (eq == std::string::npos) { if (a == name) { hasVal = false; val = ""; return true; } }
        else if (a.substr(0, eq) == name) { hasVal = true; val = a.substr(eq + 1); return true; }
    }
    return false;
}

} // namespace

class ConfigPamOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "config.pam"; d.summary = "PAM 스택 파서 + 모듈/인자 판정";
        d.safety = Safety::read_only_local; d.os = {"linux","macos"}; d.impl = "native";
        d.params = {
            { "files",  "array",  true,  nullptr, {}, "후보 PAM 파일 경로 배열(존재하는 것 병합)" },
            { "module", "string", false, nullptr, {}, "모듈 필터(예: pam_pwquality.so)" },
            { "type",   "string", false, nullptr, {}, "type 필터(auth|account|password|session)" },
            { "arg",    "string", false, nullptr, {}, "인자명(예: minlen, deny)" },
            { "rule",   "enum",   false, nullptr, { {}, {}, {"present","num_ge","num_le","eq","absent"}, "" }, "판정 규칙" },
            { "threshold", "string", false, nullptr, {}, "num_ge/num_le/eq 값" },
        };
        d.returns = { {"rules","array<object>",""}, {"found","bool",""}, {"verdict","string",""}, {"detail","string",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext& ctx) override {
        std::vector<PamRule> rules; int existing = 0;
        for (const auto& fp : p.at("files")) {
            if (!fp.is_string()) continue;
            std::error_code ec;
            if (fs::exists(fp.get<std::string>(), ec)) existing++;
            parse_file(fp.get<std::string>(), ctx.guard, rules, 0);
        }
        if (existing == 0) return OpResult::na("PAM 파일 없음");

        const std::string modFilter = p.value("module", std::string(""));
        const std::string typeFilter = p.value("type", std::string(""));

        json arr = json::array();
        std::vector<const PamRule*> matched;
        for (auto& r : rules) {
            if (!modFilter.empty() && r.module.find(modFilter) == std::string::npos) continue;
            if (!typeFilter.empty() && r.type != typeFilter) continue;
            json a = json::array(); for (auto& x : r.args) a.push_back(x);
            arr.push_back({ {"type", r.type}, {"control", r.control}, {"module", r.module},
                            {"args", a}, {"source", r.source} });
            matched.push_back(&r);
        }

        if (p.contains("rule") && p["rule"].is_string()) {
            const std::string rule = p["rule"].get<std::string>();
            json out = { {"rules", arr}, {"found", !matched.empty()} };
            if (rule == "present") {
                out["verdict"] = matched.empty() ? "fail" : "pass";
                out["detail"]  = matched.empty() ? (modFilter + " 미설정") : (modFilter + " 설정됨");
                return OpResult::ok_(out);
            }
            if (rule == "absent") {
                out["verdict"] = matched.empty() ? "pass" : "fail";
                out["detail"]  = matched.empty() ? (modFilter + " 없음(양호)") : (modFilter + " 존재");
                return OpResult::ok_(out);
            }
            // arg 기반
            const std::string argName = p.value("arg", std::string(""));
            if (argName.empty()) { out["verdict"] = "na"; out["detail"] = "arg 누락"; return OpResult::ok_(out); }
            if (matched.empty()) { out["verdict"] = "fail"; out["detail"] = modFilter + " 모듈 없음"; return OpResult::ok_(out); }
            long thr = std::strtol(p.value("threshold", std::string("0")).c_str(), nullptr, 10);
            bool anyOk = false; std::string seen;
            for (auto* r : matched) {
                std::string v; bool hasVal = false;
                if (!arg_value(*r, argName, v, hasVal)) continue;
                seen = v;
                if (rule == "num_ge" && hasVal && std::strtol(v.c_str(),nullptr,10) >= thr) anyOk = true;
                if (rule == "num_le" && hasVal && std::strtol(v.c_str(),nullptr,10) <= thr) anyOk = true;
                if (rule == "eq" && hasVal && v == p.value("threshold", std::string(""))) anyOk = true;
            }
            out["verdict"] = anyOk ? "pass" : "fail";
            out["detail"]  = anyOk ? (argName + "=" + seen + " (준수)")
                                   : (argName + " 준수 값 없음");
            return OpResult::ok_(out);
        }

        return OpResult::ok_({ {"rules", arr}, {"found", !matched.empty()} });
    }
};
REGISTER_OP(ConfigPamOp)

} // namespace wu
