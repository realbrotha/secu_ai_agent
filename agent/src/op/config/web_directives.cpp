// op: config.web_directives — Apache/nginx 설정을 Include 그래프까지 평탄화 후 디렉티브 질의.
//   재귀 Include(+glob) 해석, 블록 컨텍스트 추적, Apache Options 토큰 평가.
//   observe(mode=directives): 매칭 디렉티브 목록.
//   rule 모드(op_verdict):
//     value_match      (directive, regex, default?)      : 유효값(마지막 등장)이 regex 매칭이면 pass
//     value_absent     (directive)                        : 디렉티브 미존재면 pass
//     any_line_match   (directive, regex)                 : 어느 등장이든 regex 매칭이면 fail(취약 징후)
//     options          (option, expect=enabled|disabled) : Apache Options 토큰 평가
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include "op/util_regex.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <algorithm>
#include <regex>

namespace fs = std::filesystem;

namespace wu {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

struct Directive {
    std::string name;                 // 소문자
    std::vector<std::string> args;    // 원문 토큰
    std::string context;              // 최상위 블록명(전역="")
    std::string source; int line = 0;
};

std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out; std::string cur; bool inq = false; char q = 0;
    for (char c : s) {
        if (inq) { if (c == q) inq = false; else cur += c; }
        else if (c == '"' || c == '\'') { inq = true; q = c; }
        else if (c == ' ' || c == '\t') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

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

std::string subst_vars(std::string v, const std::map<std::string,std::string>& vars) {
    for (auto& [k, val] : vars) {
        for (std::string form : { "${" + k + "}", "$" + k }) {
            size_t p;
            while ((p = v.find(form)) != std::string::npos) v.replace(p, form.size(), val);
        }
    }
    return v;
}

struct Parser {
    std::string product;
    const PathGuard* guard;
    std::map<std::string,std::string> vars;   // apache Define + ServerRoot
    std::string serverRoot;
    std::vector<Directive> dirs;

    std::string resolve_include(const std::string& raw, const std::string& baseDir) {
        std::string v = subst_vars(raw, vars);
        if (v.empty()) return v;
        if (v[0] == '/') return v;
        std::string base = (product == "apache" && !serverRoot.empty()) ? serverRoot : baseDir;
        return (fs::path(base) / v).string();
    }

    void parse_apache(const std::string& path, std::vector<std::string>& ctxStack, int depth) {
        if (depth > 10) return;
        if (guard && !guard->allowed(path)) return;
        std::ifstream f(path); if (!f) return;
        std::string line; int ln = 0;
        while (std::getline(f, line)) {
            ++ln;
            std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            if (t[0] == '<') {
                if (t[1] == '/') { if (!ctxStack.empty()) ctxStack.pop_back(); continue; }
                std::string inner = t.substr(1, t.find('>') == std::string::npos ? std::string::npos : t.find('>') - 1);
                std::istringstream is(inner); std::string blk; is >> blk;
                ctxStack.push_back(blk);
                continue;
            }
            auto toks = tokenize(t);
            if (toks.empty()) continue;
            std::string name = lower(toks[0]);
            std::vector<std::string> args(toks.begin() + 1, toks.end());
            for (auto& a : args) a = subst_vars(a, vars);

            if (name == "serverroot" && !args.empty()) serverRoot = args[0];
            if (name == "define" && args.size() >= 2) vars[args[0]] = args[1];
            if ((name == "include" || name == "includeoptional") && !args.empty()) {
                std::string resolved = resolve_include(args[0], fs::path(path).parent_path().string());
                for (auto& inc : glob_expand(resolved)) {
                    std::error_code ec;
                    if (fs::is_directory(inc, ec)) {
                        for (auto& e : fs::recursive_directory_iterator(inc, fs::directory_options::skip_permission_denied, ec))
                            if (e.is_regular_file(ec)) parse_apache(e.path().string(), ctxStack, depth + 1);
                    } else parse_apache(inc, ctxStack, depth + 1);
                }
                continue;
            }
            Directive d; d.name = name; d.args = args;
            d.context = ctxStack.empty() ? "" : ctxStack.back();
            d.source = path; d.line = ln;
            dirs.push_back(std::move(d));
        }
    }

    void parse_nginx(const std::string& path, std::vector<std::string>& ctxStack, int depth) {
        if (depth > 10) return;
        if (guard && !guard->allowed(path)) return;
        std::ifstream f(path); if (!f) return;
        std::stringstream buf; buf << f.rdbuf(); std::string s = buf.str();
        // 주석 제거(#... 줄 끝)
        std::string cleaned; bool inq = false; char q = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (inq) { cleaned += c; if (c == q) inq = false; continue; }
            if (c == '"' || c == '\'') { inq = true; q = c; cleaned += c; continue; }
            if (c == '#') { while (i < s.size() && s[i] != '\n') ++i; cleaned += '\n'; continue; }
            cleaned += c;
        }
        // 토큰 단위로 { } ; 처리
        std::string stmt; int ln = 1;
        for (size_t i = 0; i < cleaned.size(); ++i) {
            char c = cleaned[i];
            if (c == '\n') ++ln;
            if (c == '{') {
                auto toks = tokenize(trim(stmt)); stmt.clear();
                std::string blk = toks.empty() ? "" : lower(toks[0]);
                if (blk == "location" && toks.size() >= 2) blk = "location " + toks[1];
                ctxStack.push_back(blk);
            } else if (c == '}') {
                stmt.clear(); if (!ctxStack.empty()) ctxStack.pop_back();
            } else if (c == ';') {
                auto toks = tokenize(trim(stmt)); stmt.clear();
                if (toks.empty()) continue;
                std::string name = lower(toks[0]);
                std::vector<std::string> args(toks.begin() + 1, toks.end());
                if (name == "include" && !args.empty()) {
                    std::string resolved = args[0][0] == '/' ? args[0]
                        : (fs::path(path).parent_path() / args[0]).string();
                    for (auto& inc : glob_expand(resolved)) parse_nginx(inc, ctxStack, depth + 1);
                    continue;
                }
                Directive d; d.name = name; d.args = args;
                d.context = ctxStack.empty() ? "" : ctxStack.back();
                d.source = path; d.line = ln;
                dirs.push_back(std::move(d));
            } else stmt += c;
        }
    }
};

// Apache Options 한 라인이 특정 옵션을 최종적으로 켜는지.
int line_enables_option(const std::vector<std::string>& args, const std::string& opt) {
    // 반환: 1 enable, 0 disable, -1 무관
    int state = -1;
    for (auto& raw : args) {
        std::string tk = lower(raw);
        if (tk == "none") state = 0;
        else if (tk == "all") { if (opt != "multiviews") state = 1; }
        else if (tk[0] == '+') { if (tk.substr(1) == opt) state = 1; }
        else if (tk[0] == '-') { if (tk.substr(1) == opt) state = 0; }
        else { if (tk == opt) state = 1; }
    }
    return state;
}

} // namespace

class ConfigWebDirectivesOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "config.web_directives"; d.summary = "Apache/nginx 설정 Include 평탄화 후 디렉티브 질의/판정";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "product", "enum", true, nullptr, { {}, {}, {"apache","nginx"}, "" }, "제품" },
            { "root",    "path", true, nullptr, {}, "메인 설정 파일" },
            { "directive", "string", false, nullptr, {}, "질의 디렉티브명" },
            { "context",   "string", false, nullptr, {}, "블록 컨텍스트 필터(선택)" },
            { "mode",    "enum", false, "directives", { {}, {}, {"directives"}, "" }, "observe 모드" },
            { "rule",    "enum", false, nullptr,
              { {}, {}, {"value_match","value_absent","any_line_match","options"}, "" }, "판정 규칙" },
            { "regex",   "string", false, nullptr, {}, "value_match/any_line_match 정규식" },
            { "default", "string", false, nullptr, {}, "value_match 미설정 시 기본값" },
            { "option",  "string", false, nullptr, {}, "options 규칙의 옵션(indexes/followsymlinks/multiviews)" },
            { "expect",  "enum", false, "disabled", { {}, {}, {"enabled","disabled"}, "" }, "options 기대상태" },
        };
        d.returns = { {"directives","array<object>",""}, {"count","int",""},
                      {"verdict","string",""}, {"detail","string",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext& ctx) override {
        const std::string product = p.at("product").get<std::string>();
        const std::string root = p.at("root").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(root)) return OpResult::err("path_guard 거부: " + root);
        std::error_code ec;
        if (!fs::exists(root, ec)) return OpResult::na("설정 파일 없음: " + root);

        Parser ps; ps.product = product; ps.guard = ctx.guard;
        ps.serverRoot = fs::path(root).parent_path().parent_path().string();  // 기본 추정
        std::vector<std::string> stack;
        if (product == "apache") ps.parse_apache(root, stack, 0);
        else                     ps.parse_nginx(root, stack, 0);

        const std::string want = lower(p.value("directive", std::string("")));
        const std::string ctxFilter = p.value("context", std::string(""));
        const std::string rule = p.value("rule", std::string(""));

        auto matches = [&](const Directive& d) {
            if (!want.empty() && d.name != want) return false;
            if (!ctxFilter.empty() && d.context != ctxFilter) return false;
            return true;
        };

        // ── options 규칙 ──
        if (rule == "options") {
            const std::string opt = lower(p.value("option", std::string("")));
            int enabled = 0, disabled = 0;
            for (auto& d : ps.dirs) {
                if (d.name != "options") continue;
                int st = line_enables_option(d.args, opt);
                if (st == 1) ++enabled; else if (st == 0) ++disabled;
            }
            const std::string expect = p.value("expect", std::string("disabled"));
            bool effectivelyEnabled = enabled > 0;
            bool ok = (expect == "disabled") ? !effectivelyEnabled : effectivelyEnabled;
            return OpResult::ok_({ {"verdict", ok ? "pass" : "fail"},
                {"detail", "Options " + opt + (effectivelyEnabled ? " 활성" : " 비활성") +
                           " (enable=" + std::to_string(enabled) + ")"} });
        }

        // 매칭 디렉티브 수집
        json arr = json::array(); std::vector<const Directive*> hits;
        for (auto& d : ps.dirs) if (matches(d)) {
            json a = json::array(); for (auto& x : d.args) a.push_back(x);
            arr.push_back({ {"name", d.name}, {"args", a}, {"context", d.context},
                            {"source", d.source}, {"line", d.line} });
            hits.push_back(&d);
        }

        if (rule == "value_absent") {
            bool ok = hits.empty();
            return OpResult::ok_({ {"verdict", ok ? "pass" : "fail"},
                {"detail", ok ? (want + " 미설정") : (want + " 설정됨(" + std::to_string(hits.size()) + "건)")} });
        }
        if (rule == "value_match") {
            std::string val; bool found = !hits.empty();
            if (found) { for (auto& a : hits.back()->args) { if (!val.empty()) val += " "; val += a; } }
            else if (p.contains("default") && p["default"].is_string()) val = p["default"].get<std::string>();
            std::regex re;
            try { re = compile_regex(p.value("regex", std::string(""))); }
            catch (...) { return OpResult::err("잘못된 정규식"); }
            bool ok = std::regex_search(val, re);
            return OpResult::ok_({ {"verdict", ok ? "pass" : "fail"},
                {"detail", want + "=" + val + (found ? "" : "(기본)")} });
        }
        if (rule == "any_line_match") {
            std::regex re;
            try { re = compile_regex(p.value("regex", std::string(""))); }
            catch (...) { return OpResult::err("잘못된 정규식"); }
            for (auto* d : hits) {
                std::string joined; for (auto& a : d->args) { if (!joined.empty()) joined += " "; joined += a; }
                if (std::regex_search(joined, re))
                    return OpResult::ok_({ {"verdict","fail"}, {"detail", want + " 취약 패턴: " + joined} });
            }
            return OpResult::ok_({ {"verdict","pass"}, {"detail", want + " 취약 패턴 없음"} });
        }

        return OpResult::ok_({ {"directives", arr}, {"count", (int)arr.size()} });
    }
};
REGISTER_OP(ConfigWebDirectivesOp)

} // namespace wu
