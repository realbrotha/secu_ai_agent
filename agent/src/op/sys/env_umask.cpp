// op: sys.shell_var — 셸 초기화/설정 파일에서 변수 값 추출 + 판정.
//   TMOUT(1.13)/UMASK(2.18)/HISTSIZE·HISTTIMEFORMAT(4.6) 등. glob 파일 지원, "any 소스가 준수" 의미.
//   form=assign: `export K=V` / `K=V` / `K V`(login.defs) | form=umask: `umask NNNN`
//   rule(verdict): num_le|num_ge|octal_ge|present (+threshold). 지정 시 pass.rule:"op_verdict".
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
std::string strip_quotes(std::string s) {
    if (s.size() >= 2 && (s.front()=='"'||s.front()=='\'') && s.back()==s.front()) s = s.substr(1, s.size()-2);
    return s;
}
// 단순 glob(디렉터리 내 * 하나) 확장. 없으면 그대로.
std::vector<std::string> expand(const std::string& pat) {
    std::vector<std::string> out;
    if (pat.find('*') == std::string::npos) { out.push_back(pat); return out; }
    fs::path p(pat); fs::path dir = p.parent_path(); std::string name = p.filename().string();
    std::string pre = name.substr(0, name.find('*'));
    std::string suf = name.substr(name.find('*') + 1);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::string fn = e.path().filename().string();
        if (fn.size() >= pre.size()+suf.size() && fn.rfind(pre,0)==0 &&
            fn.compare(fn.size()-suf.size(), suf.size(), suf)==0)
            out.push_back(e.path().string());
    }
    return out;
}

long to_num(const std::string& s, bool octal) {
    return std::strtol(s.c_str(), nullptr, octal ? 8 : 10);
}

} // namespace

class SysShellVarOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "sys.shell_var"; d.summary = "셸/설정 파일에서 변수값 추출·판정(TMOUT/UMASK/HIST* 등)";
        d.safety = Safety::read_only_local; d.os = {"linux","macos"}; d.impl = "native";
        d.params = {
            { "files", "array", true, nullptr, {}, "대상 파일 경로 배열(glob * 지원)" },
            { "key",   "string", true, nullptr, {}, "변수명(예: TMOUT, UMASK, HISTSIZE) 또는 umask" },
            { "form",  "enum", false, "assign", { {}, {}, {"assign","umask"}, "" }, "추출 형식" },
            { "rule",  "enum", false, nullptr, { {}, {}, {"num_le","num_ge","octal_ge","present"}, "" }, "판정 규칙" },
            { "threshold", "string", false, nullptr, {}, "판정 임계값(num/octal)" },
        };
        d.returns = { {"found","bool",""}, {"values","array<object>","[{value,source}]"},
                      {"verdict","string","rule 모드"}, {"detail","string",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext& ctx) override {
        const std::string key = p.at("key").get<std::string>();
        const std::string form = p.value("form", std::string("assign"));
        std::string keyLow = key; std::transform(keyLow.begin(), keyLow.end(), keyLow.begin(), ::tolower);

        json values = json::array();
        for (const auto& fp : p.at("files")) {
            if (!fp.is_string()) continue;
            for (const auto& path : expand(fp.get<std::string>())) {
                if (ctx.guard && !ctx.guard->allowed(path)) continue;
                std::ifstream f(path); if (!f) continue;
                std::string line;
                while (std::getline(f, line)) {
                    std::string t = trim(line);
                    if (t.empty() || t[0] == '#') continue;
                    if (form == "umask") {
                        std::istringstream ss(t); std::string w; ss >> w;
                        if (w == "umask") { std::string v; ss >> v; if (!v.empty())
                            values.push_back({ {"value", v}, {"source", path} }); }
                        continue;
                    }
                    // assign: optional "export", KEY=VAL 또는 KEY VAL(login.defs)
                    std::string s = t;
                    if (s.rfind("export ", 0) == 0) s = trim(s.substr(7));
                    std::string lhs, rhs; bool matched = false;
                    auto eq = s.find('=');
                    if (eq != std::string::npos) { lhs = trim(s.substr(0, eq)); rhs = trim(strip_quotes(trim(s.substr(eq+1)))); matched = true; }
                    else { std::istringstream ss(s); ss >> lhs; std::string rest; std::getline(ss, rest); rhs = trim(rest); matched = !rhs.empty(); }
                    std::string lhsLow = lhs; std::transform(lhsLow.begin(), lhsLow.end(), lhsLow.begin(), ::tolower);
                    if (matched && lhsLow == keyLow && !rhs.empty())
                        values.push_back({ {"value", rhs}, {"source", path} });
                }
            }
        }

        const bool found = !values.empty();
        if (p.contains("rule") && p["rule"].is_string()) {
            const std::string rule = p["rule"].get<std::string>();
            json out = { {"found", found}, {"values", values} };
            if (rule == "present") {
                out["verdict"] = found ? "pass" : "fail";
                out["detail"]  = found ? (key + " 설정됨") : (key + " 미설정");
                return OpResult::ok_(out);
            }
            if (!found) { out["verdict"] = "fail"; out["detail"] = key + " 미설정"; return OpResult::ok_(out); }
            const bool octal = (rule == "octal_ge");
            long thr = to_num(p.value("threshold", std::string("0")), octal);
            bool anyOk = false; std::string best;
            for (auto& v : values) {
                long n = to_num(v["value"].get<std::string>(), octal);
                bool ok = (rule == "num_le") ? (n > 0 && n <= thr)
                        : (rule == "num_ge") ? (n >= thr)
                        : /* octal_ge */       (n >= thr);
                if (ok) { anyOk = true; best = v["value"].get<std::string>(); }
            }
            out["verdict"] = anyOk ? "pass" : "fail";
            out["detail"]  = anyOk ? (key + "=" + best + " (준수)")
                                   : (key + " 준수 값 없음(임계 " + p.value("threshold", std::string("")) + ")");
            return OpResult::ok_(out);
        }

        return OpResult::ok_({ {"found", found}, {"values", values} });
    }
};
REGISTER_OP(SysShellVarOp)

} // namespace wu
