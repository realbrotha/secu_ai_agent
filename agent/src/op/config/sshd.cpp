// op: config.sshd — sshd_config 의 유효값(Include·drop-in 병합, first-match-wins).
//   sshd 는 각 키워드의 "첫" 등장이 유효. Include(glob)/sshd_config.d 재귀 병합.
//   Match 블록 내부는 전역 판정에서 제외(전역 컨텍스트만 반영).
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace wu {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

std::vector<std::string> glob_expand(const std::string& pat) {
    std::vector<std::string> out;
    if (pat.find('*') == std::string::npos && pat.find('?') == std::string::npos) { out.push_back(pat); return out; }
    fs::path p(pat); fs::path dir = p.parent_path(); std::string name = p.filename().string();
    std::string pre = name.substr(0, name.find_first_of("*?"));
    std::string suf = name.substr(name.find_last_of("*?") + 1);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::string fn = e.path().filename().string();
        if (fn.size() >= pre.size() + suf.size() && fn.rfind(pre, 0) == 0 &&
            fn.compare(fn.size() - suf.size(), suf.size(), suf) == 0)
            out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// (키,값,source) 를 파일 순서대로 수집. Match 블록 진입 시 전역 수집 중단(inMatch).
void collect(const std::string& path, const PathGuard* guard,
             std::vector<std::array<std::string,3>>& out, int depth) {
    if (depth > 10) return;
    if (guard && !guard->allowed(path)) return;
    std::ifstream f(path); if (!f) return;
    std::string line; bool inMatch = false;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        std::istringstream ss(t); std::string kw; ss >> kw;
        std::string kwl = lower(kw);
        std::string rest; std::getline(ss, rest); rest = trim(rest);
        if (kwl == "match") { inMatch = (lower(rest) != "all"); continue; }
        if (kwl == "include") {
            for (auto& inc : glob_expand(rest.empty() ? "" : (rest[0]=='/' ? rest : (fs::path(path).parent_path()/rest).string())))
                collect(inc, guard, out, depth + 1);
            continue;
        }
        if (inMatch) continue;
        out.push_back({ kwl, rest, path });
    }
}

} // namespace

class ConfigSshdOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "config.sshd"; d.summary = "sshd_config 유효값(Include/drop-in 병합, first-match-wins)";
        d.safety = Safety::read_only_local; d.os = {"linux","macos"}; d.impl = "native";
        d.params = {
            { "path", "path",   false, "/etc/ssh/sshd_config", {}, "메인 sshd_config" },
            { "key",  "string", true,  nullptr, {}, "키워드(예: PermitRootLogin)" },
            { "default", "string", false, nullptr, {}, "미설정 시 유효값(문서 기본)" },
        };
        d.returns = { {"found","bool",""}, {"value","string","유효값(first-match 또는 default)"},
                      {"values","array<string>","모든 등장"}, {"source","string","유효값 파일"} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.value("path", std::string("/etc/ssh/sshd_config"));
        if (ctx.guard && !ctx.guard->allowed(path)) return OpResult::err("path_guard 거부: " + path);
        std::error_code ec;
        if (!fs::exists(path, ec)) return OpResult::na("sshd_config 없음: " + path);

        std::vector<std::array<std::string,3>> all;
        collect(path, ctx.guard, all, 0);

        const std::string key = lower(p.at("key").get<std::string>());
        json values = json::array(); std::string effective, source; bool found = false;
        for (auto& e : all) if (e[0] == key) {
            values.push_back(e[1]);
            if (!found) { effective = e[1]; source = e[2]; found = true; }  // first-match-wins
        }
        if (!found && p.contains("default") && p["default"].is_string()) {
            effective = p["default"].get<std::string>(); source = "(default)";
        }
        return OpResult::ok_({ {"found", found}, {"value", effective}, {"values", values}, {"source", source} });
    }
};
REGISTER_OP(ConfigSshdOp)

} // namespace wu
