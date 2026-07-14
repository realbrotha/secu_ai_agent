#include "core/interpreter.h"
#include "op/registry.h"
#include "op/runner.h"
#include <regex>
#include <ctime>
#include <filesystem>

namespace wu {

// ── pack 파싱 ──────────────────────────────────────────────
PackSpec parse_pack(const json& j) {
    PackSpec p;
    p.packId  = j.value("packId", "");
    p.title   = j.value("title", "");
    p.version = j.value("version", "");
    p.target  = j.value("target", json::object());
    p.vars    = j.value("vars", json::object());
    for (const auto& r : j.value("requires", json::array()))
        if (r.is_string()) p.requiresPaths.push_back(r.get<std::string>());
    if (p.packId.empty()) throw std::runtime_error("pack: packId 누락");

    for (const auto& c : j.value("checks", json::array())) {
        CheckSpec cs;
        cs.checkId     = c.value("checkId", "");
        cs.title       = c.value("title", "");
        cs.category    = c.value("category", "");
        cs.severity    = c.value("severity", "info");
        cs.method      = c.value("method", "ops");
        cs.remediation = c.value("remediation", "");
        cs.reference   = c.value("reference", json::object());
        cs.passRule    = c.value("pass", json::object()).value("rule", "all_asserts_passed");
        for (const auto& s : c.value("steps", json::array())) {
            StepSpec st;
            st.op     = s.value("op", "");
            st.params = s.value("params", json::object());
            st.as     = s.value("as", "");
            cs.steps.push_back(std::move(st));
        }
        p.checks.push_back(std::move(cs));
    }
    return p;
}

// ── 변수 바인딩 helper ─────────────────────────────────────
namespace {

// bindings 에서 "was.home" 같은 dot-path 조회
json lookup(const json& bindings, const std::string& path) {
    const json* cur = &bindings;
    size_t start = 0;
    while (start <= path.size()) {
        size_t dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!cur->is_object() || !cur->contains(key)) return json(nullptr);
        cur = &(*cur)[key];
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return *cur;
}

// bindings 에 "was.home" = value 를 nested 로 설정
void set_nested(json& bindings, const std::string& path, const json& value) {
    json* cur = &bindings;
    size_t start = 0;
    while (true) {
        size_t dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (dot == std::string::npos) { (*cur)[key] = value; return; }
        if (!(*cur).contains(key) || !(*cur)[key].is_object()) (*cur)[key] = json::object();
        cur = &(*cur)[key];
        start = dot + 1;
    }
}

std::string json_to_str(const json& v) {
    return v.is_string() ? v.get<std::string>() : (v.is_null() ? std::string("") : v.dump());
}

// 문자열 내 $ref 해석. 전체가 정확히 하나의 $ref 면 원래 JSON 타입 보존, 아니면 문자열 보간.
json resolve_string(const std::string& s, const json& bindings) {
    static const std::regex whole(R"(^\$([A-Za-z0-9_.]+)$)");
    std::smatch m;
    if (std::regex_match(s, m, whole)) return lookup(bindings, m[1].str());

    static const std::regex tok(R"(\$([A-Za-z0-9_.]+))");
    std::string out;
    auto begin = std::sregex_iterator(s.begin(), s.end(), tok);
    auto end = std::sregex_iterator();
    size_t last = 0;
    for (auto it = begin; it != end; ++it) {
        out += s.substr(last, it->position() - last);
        out += json_to_str(lookup(bindings, (*it)[1].str()));
        last = it->position() + it->length();
    }
    out += s.substr(last);
    return out;
}

// params 트리 전체에서 $ref 해석
json resolve_refs(const json& params, const json& bindings) {
    if (params.is_object()) {
        json o = json::object();
        for (auto it = params.begin(); it != params.end(); ++it) o[it.key()] = resolve_refs(it.value(), bindings);
        return o;
    }
    if (params.is_array()) {
        json a = json::array();
        for (const auto& v : params) a.push_back(resolve_refs(v, bindings));
        return a;
    }
    if (params.is_string()) return resolve_string(params.get<std::string>(), bindings);
    return params;
}

std::string now_iso() {
    std::time_t t = std::time(nullptr);
    char buf[32]; std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

// params 안의 $ref 중 bindings 에서 null 로 풀리는(관찰값 없음) 첫 토큰 반환. 없으면 "".
std::string first_missing_ref(const json& v, const json& bindings) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        static const std::regex tok(R"(\$([A-Za-z0-9_.]+))");
        for (auto it = std::sregex_iterator(s.begin(), s.end(), tok); it != std::sregex_iterator(); ++it)
            if (lookup(bindings, (*it)[1].str()).is_null()) return (*it)[1].str();
        return "";
    }
    if (v.is_object())
        for (auto it = v.begin(); it != v.end(); ++it)
            if (auto m = first_missing_ref(it.value(), bindings); !m.empty()) return m;
    if (v.is_array())
        for (const auto& e : v)
            if (auto m = first_missing_ref(e, bindings); !m.empty()) return m;
    return "";
}

} // namespace

// ── 단일 점검 실행 ─────────────────────────────────────────
CheckResult Interpreter::run_check(const CheckSpec& check, json bindings) {
    CheckResult res;
    res.checkId = check.checkId; res.title = check.title; res.category = check.category;
    res.severity = check.severity; res.reference = check.reference; res.remediation = check.remediation;

    bool all_asserts_ok = true;
    bool has_assert = false;

    for (const auto& step : check.steps) {
        // 관찰값 없음($ref 이 null): 이전 관찰이 대상 부재 등으로 값을 못 냄 → 판단 불가(na)
        if (auto miss = first_missing_ref(step.params, bindings); !miss.empty()) {
            res.status = "na"; res.detail = "관찰값 없음: $" + miss;
            res.evidence.push_back({ {"op", step.op}, {"status", "na"}, {"detail", "unresolved $" + miss} });
            return res;
        }

        IOp* op = OpRegistry::instance().find(step.op);
        json rparams = resolve_refs(step.params, bindings);

        if (!op) {
            res.status = "na"; res.detail = "미지원 op: " + step.op;
            res.evidence.push_back({ {"op", step.op}, {"params", rparams}, {"status", "na"} });
            return res;
        }

        OpResult r = run_op_guarded(op, rparams, ctx_);
        res.evidence.push_back({ {"op", step.op}, {"params", rparams},
                                 {"status", to_string(r.status)}, {"data", r.data} });

        if (r.status == OpStatus::error) { res.status = "error"; res.detail = r.error; return res; }
        if (r.status == OpStatus::not_applicable) { res.status = "na"; res.detail = r.error; return res; }

        if (!step.as.empty()) bindings[step.as] = r.data;

        if (step.op.rfind("assert.", 0) == 0) {
            has_assert = true;
            if (!r.data.value("passed", false)) all_asserts_ok = false;
        }
    }

    if (check.passRule == "all_asserts_passed")
        res.status = (!has_assert || all_asserts_ok) ? "pass" : "fail";
    else
        res.status = all_asserts_ok ? "pass" : "fail";
    return res;
}

// ── pack 전체 실행 → a.json 형태 ───────────────────────────
json Interpreter::run_pack(const PackSpec& pack, json initial_vars) {
    // 초기 바인딩 = pack.vars + initial_vars (initial 우선)
    json bindings = pack.vars.is_object() ? pack.vars : json::object();
    if (initial_vars.is_object())
        for (auto it = initial_vars.begin(); it != initial_vars.end(); ++it)
            set_nested(bindings, it.key(), it.value());

    const std::string started = now_iso();

    // 자동 탐지(D3): --var 로 명시하지 않았고 기본 경로가 없으면 sys.detect_was_web 로 보정.
    if (pack.target.contains("product")) {
        const std::string product = pack.target.value("product", "");
        // target.kind 로 home 변수 키 결정
        std::string homeKey, homeField;
        for (const auto& k : pack.target.value("kind", json::array())) {
            if (k == "was") { homeKey = "was.home"; homeField = "home"; }
            else if (k == "web") { homeKey = "web.root"; homeField = "root"; }
        }
        const bool user_set = initial_vars.is_object() && initial_vars.contains(homeKey);
        json cur = lookup(bindings, homeKey);
        const bool cur_exists = cur.is_string() && std::filesystem::exists(cur.get<std::string>());
        if (!homeKey.empty() && !user_set && !cur_exists) {
            if (IOp* det = OpRegistry::instance().find("sys.detect_was_web")) {
                OpResult dr = run_op_guarded(det, json::object(), ctx_);
                if (dr.status == OpStatus::ok) {
                    const char* arrKey = (homeField == "home") ? "was" : "web";
                    for (const auto& e : dr.data.value(arrKey, json::array())) {
                        if (e.value("product", "") == product && !e.value(homeField, std::string("")).empty()) {
                            set_nested(bindings, homeKey, e[homeField]);
                            break;
                        }
                    }
                }
            }
        }
    }

    // 대상 존재 전제(requires) 확인 — 부재 시 전체 na (오탐 clean 방지, §8.3 "대상 부재=na")
    std::string target_missing;
    for (const auto& req : pack.requiresPaths) {
        json rp = resolve_refs(json(req), bindings);
        std::string path = rp.is_string() ? rp.get<std::string>() : "";
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec)) { target_missing = path.empty() ? req : path; break; }
    }

    json results = json::array();
    int pass = 0, fail = 0, na = 0, error = 0;
    json by_sev = { {"critical",0},{"high",0},{"medium",0},{"low",0},{"info",0} };

    for (const auto& check : pack.checks) {
        CheckResult r;
        if (!target_missing.empty()) {
            r.checkId = check.checkId; r.title = check.title; r.category = check.category;
            r.severity = check.severity; r.reference = check.reference; r.remediation = check.remediation;
            r.status = "na"; r.detail = "대상 부재: " + target_missing;
        } else {
            r = run_check(check, bindings);
        }
        if (r.status == "pass") ++pass;
        else if (r.status == "fail") { ++fail; if (by_sev.contains(r.severity)) by_sev[r.severity] = by_sev[r.severity].get<int>() + 1; }
        else if (r.status == "na") ++na;
        else ++error;

        results.push_back({
            {"checkId", r.checkId}, {"title", r.title}, {"category", r.category},
            {"severity", r.severity}, {"status", r.status}, {"detail", r.detail},
            {"reference", r.reference}, {"remediation", r.remediation}, {"evidence", r.evidence},
        });
    }

    const int total = (int)pack.checks.size();
    // Level0 규칙(LLM 없이). Phase4 에서 LLM 종합.
    std::string verdict, headline;
    if (!target_missing.empty()) {
        verdict = "unknown"; headline = "대상 부재로 점검 불가: " + target_missing;
    } else if (fail > 0) {
        verdict = "vulnerable";
        headline = std::to_string(fail) + "건 취약 (critical " + std::to_string(by_sev["critical"].get<int>()) +
                   ", high " + std::to_string(by_sev["high"].get<int>()) + ")";
    } else if (pass > 0) {
        verdict = "clean";
        headline = na > 0 ? ("취약 없음(단 " + std::to_string(na) + "건 판단불가)") : "취약 항목 없음";
    } else {
        verdict = "unknown"; headline = "판단 가능한 항목 없음(na " + std::to_string(na) + ")";
    }

    return {
        {"schemaVersion", "1.0"},
        {"packId", pack.packId},
        {"target", pack.target},
        {"vars", bindings},
        {"startedAt", started},
        {"finishedAt", now_iso()},
        {"summary", { {"total", total}, {"pass", pass}, {"fail", fail}, {"na", na}, {"error", error},
                      {"bySeverity", by_sev}, {"verdict", verdict}, {"headline", headline} }},
        {"results", results},
    };
}

} // namespace wu
