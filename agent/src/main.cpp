// wu-agent — Phase S: Standalone Interactive (서버·LLM 없이)
//   서브커맨드: ops | op <name> [json] | packs | run <packId> [--var k=v] | repl
// 설계: ../plan/final_plan.md (§1.6 실행 프로파일, §1.7 결과 표시, §6 op, §8.5 pack)
#include "util/log.h"
#include "op/op.h"
#include "op/context.h"
#include "op/registry.h"
#include "op/runner.h"
#include "core/interpreter.h"
#include "core/react.h"
#include "op/context.h"
#include "llm/llm_engine.h"

#include <filesystem>
#include <memory>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdint>

#if defined(WU_HAVE_REPLXX)
#include "replxx.hxx"
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;
using wu::json;

#ifndef WU_AGENT_VERSION
#define WU_AGENT_VERSION "0.0.0"
#endif

static const char* os_name() {
#if defined(WU_OS_WINDOWS)
    return "windows";
#elif defined(WU_OS_MACOS)
    return "macos";
#elif defined(WU_OS_LINUX)
    return "linux";
#else
    return "unknown";
#endif
}

static wu::HostInfo make_host() {
    wu::HostInfo h; h.os = os_name();
#if !defined(_WIN32)
    struct utsname u{};
    if (uname(&u) == 0) h.arch = u.machine;
    char buf[256] = {0};
    if (gethostname(buf, sizeof(buf)) == 0) h.hostname = buf;
#endif
    return h;
}

// ── 설정 ───────────────────────────────────────────────────
struct AppConfig {
    std::string configPath;
    std::string agentRoot;              // config 기준 루트 (상대경로 rebase 기준)
    std::string rulesDir = "config/rules";
    std::string spoolDir = "cache/spool";
    std::vector<std::string> allow, deny;
    bool        enableLlmReasoning = false;
    // LLM
    std::string llmMode = "off";        // off | local | remote
    std::string modelPath;
    int         nCtx = 4096;
    float       temperature = 0.2f;
};

static bool wu_stdin_is_tty() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

static fs::path executable_dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return fs::current_path();
    return fs::path(buf).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return fs::current_path();
    std::error_code ec;
    fs::path p = fs::weakly_canonical(buf, ec);
    return ec ? fs::path(buf).parent_path() : p.parent_path();
#else
    std::error_code ec;
    fs::path p = fs::weakly_canonical("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
    return fs::current_path();
#endif
}

static std::string rebase_rel(const fs::path& base, const std::string& p) {
    if (p.empty()) return p;
    fs::path path(p);
    if (path.is_absolute()) return path.lexically_normal().string();
    std::error_code ec;
    fs::path joined = fs::weakly_canonical(base / path, ec);
    return (ec ? (base / path).lexically_normal() : joined).string();
}

// config.json 이 config/ 아래면 agent 루트는 그 parent
static fs::path agent_root_from_config(const fs::path& config_file) {
    fs::path dir = config_file.parent_path();
    if (dir.filename() == "config") return dir.parent_path();
    return dir;
}

// cwd → exe 상위 방향으로 config 탐색
static std::string resolve_config_path(const std::string& requested) {
    fs::path req(requested);
    std::error_code ec;
    if (req.is_absolute()) {
        if (fs::exists(req, ec)) return fs::weakly_canonical(req, ec).string();
        return "";
    }
    fs::path cwd_try = fs::current_path() / req;
    if (fs::exists(cwd_try, ec)) return fs::weakly_canonical(cwd_try, ec).string();

    for (fs::path d = executable_dir(); !d.empty(); d = d.parent_path()) {
        fs::path cand = d / req;
        if (fs::exists(cand, ec)) return fs::weakly_canonical(cand, ec).string();
        fs::path fallback = d / "config" / "config.json";
        if (fs::exists(fallback, ec)) return fs::weakly_canonical(fallback, ec).string();
        // 구 이름 하위호환
        fs::path legacy = d / "config" / "reactor.json";
        if (fs::exists(legacy, ec)) return fs::weakly_canonical(legacy, ec).string();
        if (d == d.root_path()) break;
    }
    return "";
}

static bool load_config(const std::string& path, AppConfig& c, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "config 없음: " + path; return false; }
    try {
        json j; f >> j;
        c.configPath = path;
        c.agentRoot  = agent_root_from_config(path).string();
        c.rulesDir = j.value("rulesDir", j.value("packsDir", c.rulesDir)); // packsDir 하위호환
        c.spoolDir = j.value("spoolDir", c.spoolDir);
        if (j.contains("policy")) {
            const auto& p = j["policy"];
            c.enableLlmReasoning = p.value("enableLlmReasoning", c.enableLlmReasoning);
            for (const auto& a : p.value("readAllowlist", json::array())) c.allow.push_back(a.get<std::string>());
            for (const auto& d : p.value("readDenylist",  json::array())) c.deny.push_back(d.get<std::string>());
        }
        if (j.contains("llm")) {
            const auto& l = j["llm"];
            c.llmMode     = l.value("mode", c.llmMode);
            c.modelPath   = l.value("modelPath", c.modelPath);
            c.nCtx        = l.value("nCtx", c.nCtx);
            c.temperature = l.value("temperature", c.temperature);
        }
        fs::path root(c.agentRoot);
        c.rulesDir  = rebase_rel(root, c.rulesDir);
        c.spoolDir  = rebase_rel(root, c.spoolDir);
        c.modelPath = rebase_rel(root, c.modelPath);
    } catch (const std::exception& e) {
        err = std::string("config JSON 파싱 실패: ") + e.what();
        return false;
    }
    return true;
}

// ── LLM 지연 초기화 + 헬퍼 (Phase 1/4) ─────────────────────
static std::unique_ptr<wu::ILlmEngine> g_llm;
static bool g_llm_tried = false;

static wu::ILlmEngine* ensure_llm(const AppConfig& cfg) {
    if (g_llm) return g_llm.get();
    if (g_llm_tried) return nullptr;
    g_llm_tried = true;
    if (cfg.llmMode == "off") { wu::log::warn("llm.mode=off (config 에서 local 로 변경 시 사용)"); return nullptr; }
    if (cfg.modelPath.empty()) { wu::log::warn("llm.modelPath 미설정"); return nullptr; }
    wu::log::info("모델 로드 중: " + cfg.modelPath + " ...");
    std::string err;
    g_llm = wu::make_local_llm(cfg.modelPath, cfg.nCtx, /*n_gpu_layers=*/0, err);
    if (!g_llm) { wu::log::error(err); return nullptr; }
    wu::log::info("모델 로드 완료: " + g_llm->model_name());
    return g_llm.get();
}

// explain/ask/agent/NL — policy.enableLlmReasoning 게이트
static wu::ILlmEngine* ensure_llm_reasoning(const AppConfig& cfg) {
    if (!cfg.enableLlmReasoning) {
        wu::log::warn("policy.enableLlmReasoning=false (config.json 에서 true 로 변경)");
        return nullptr;
    }
    return ensure_llm(cfg);
}

// status → 명확한 한국어 라벨 (LLM 오해 방지)
static std::string status_ko(const std::string& s) {
    if (s == "pass")  return "통과(문제없음)";
    if (s == "fail")  return "실패(취약)";
    if (s == "na")    return "점검불가(대상없음)";
    if (s == "error") return "오류";
    return s;
}

// a.json 을 프롬프트용 간결 텍스트로.
//   brief=true(요약용): 실패(취약) 항목만, detail 은 앞부분만 → 프롬프트 토큰 최소화(prefill 가속).
//   brief=false(ask/근거용): 전체 항목 + detail 유지.
static std::string compact_results(const json& result, bool brief = false) {
    std::string s = "packId: " + result.value("packId", "") + "\n";
    const auto& sm = result["summary"];
    s += "집계: 취약(실패) " + std::to_string(sm.value("fail",0)) +
         " / 통과 " + std::to_string(sm.value("pass",0)) +
         " / 점검불가 " + std::to_string(sm.value("na",0)) +
         " / 오류 " + std::to_string(sm.value("error",0)) + "\n";
    s += brief ? "실패(취약) 항목:\n" : "항목:\n";
    for (const auto& r : result["results"]) {
        const std::string st = r.value("status","");
        if (brief && st != "fail") continue;                       // 요약은 실패 항목만
        std::string detail = r.value("detail", std::string(""));
        if (brief && detail.size() > 120) detail = detail.substr(0, 120) + "…";  // detail 축약
        s += "- " + status_ko(st) + " | " + r.value("checkId","") + " (" + r.value("severity","") + ") "
             + r.value("title","") + (detail.empty()? "" : (" :: " + detail)) + "\n";
    }
    return s;
}

// Level 1: 자연어 요청 → 라우팅 결정 (tool-calling / 구조화 JSON)
struct RouteDecision {
    std::string action = "none";       // "run" | "all" | "none"
    std::vector<std::string> packs;    // 화이트리스트 검증된 packId
    json vars = json::object();        // flat {"was.home":"/x"} (run_pack 이 set_nested)
};

// packId 집합·vars 를 GBNF 로 강제 (react.cpp build_grammar 패턴 복제)
static std::string build_route_grammar(const std::vector<std::string>& ids) {
    std::string pn;
    for (size_t i = 0; i < ids.size(); ++i)
        pn += (i ? " | " : "") + std::string("\"\\\"") + ids[i] + "\\\"\"";
    return std::string(
        "root ::= \"{\" ws \"\\\"action\\\"\" ws \":\" ws action ws \",\" ws "
        "\"\\\"packs\\\"\" ws \":\" ws packs ws \",\" ws "
        "\"\\\"vars\\\"\" ws \":\" ws vars ws \"}\"\n"
        "action ::= \"\\\"run\\\"\" | \"\\\"all\\\"\" | \"\\\"none\\\"\"\n"
        "packs ::= \"[\" ws ( packname ( ws \",\" ws packname )* )? ws \"]\"\n"
        "packname ::= ") + pn + "\n"
        "vars ::= \"{\" ws ( vpair ( ws \",\" ws vpair )* )? ws \"}\"\n"
        "vpair ::= string ws \":\" ws string\n"
        "string ::= \"\\\"\" ( [^\"\\\\] | \"\\\\\" [\"\\\\/bfnrt] )* \"\\\"\"\n"
        "ws ::= [ \\t\\n]*\n";
}

static RouteDecision llm_route(wu::ILlmEngine* llm, const AppConfig& cfg, const std::string& request) {
    RouteDecision d;
    std::vector<std::string> ids;
    if (fs::exists(cfg.rulesDir))
        for (const auto& e : fs::directory_iterator(cfg.rulesDir))
            if (e.path().extension() == ".json") ids.push_back(e.path().stem().string());
    std::sort(ids.begin(), ids.end());
    if (ids.empty()) return d;   // none (실행할 pack 없음)

    std::string list; for (auto& i : ids) list += "- " + i + "\n";
    const std::string grammar = build_route_grammar(ids);
    std::vector<wu::ChatMsg> msgs = {
        { "system",
          "너는 보안 점검 명령 라우터다. 사용자 요청을 아래 JSON 하나로만 출력한다(설명 금지).\n"
          "형식: {\"action\":\"run|all|none\",\"packs\":[\"packId\",...],\"vars\":{\"키\":\"값\"}}\n"
          "- 특정 대상 점검: action=run, packs 에 해당 packId(복수 가능).\n"
          "- 전체 점검(모두/전체): action=all, packs=[].\n"
          "- 점검 요청이 아니면(일반 대화): action=none, packs=[].\n"
          "- 경로/대상이 문장에 있으면 vars 에 넣어라. 웹서버 경로 키=web.root, WAS(톰캣) 경로 키=was.home. 없으면 vars={}.\n"
          "packId 는 반드시 아래 목록에서만 고른다.\n[packId 목록]\n" + list +
          "예) '톰캣이랑 아파치 검사' -> {\"action\":\"run\",\"packs\":[\"kisa-tomcat\",\"kisa-apache\"],\"vars\":{}}\n"
          "예) '톰캣 /opt/tomcat 점검' -> {\"action\":\"run\",\"packs\":[\"kisa-tomcat\"],\"vars\":{\"was.home\":\"/opt/tomcat\"}}\n"
          "예) '모든 검사' -> {\"action\":\"all\",\"packs\":[],\"vars\":{}}\n"
          "예) '안녕' -> {\"action\":\"none\",\"packs\":[],\"vars\":{}}" },
        { "user", request },
    };
    std::string resp = llm->chat(msgs, 128, 0.0f, grammar);

    json j;
    try { j = json::parse(resp); } catch (...) { return d; }   // 파싱 실패 → none

    std::string action = j.value("action", std::string("none"));
    if (action != "run" && action != "all" && action != "none") action = "none";
    d.action = action;

    if (j.contains("packs") && j["packs"].is_array())
        for (const auto& p : j["packs"]) {
            if (!p.is_string()) continue;
            const std::string ps = p.get<std::string>();
            if (std::find(ids.begin(), ids.end(), ps) != ids.end()) d.packs.push_back(ps);  // 화이트리스트
        }
    if (j.contains("vars") && j["vars"].is_object())
        for (auto it = j["vars"].begin(); it != j["vars"].end(); ++it)
            if (it.value().is_string()) d.vars[it.key()] = it.value();

    if (d.action == "run" && d.packs.empty()) d.action = "none";   // run 인데 대상 없음 → 폴백
    return d;
}

// verdict/설명: 결과를 한국어로 요약
static std::string llm_summarize(wu::ILlmEngine* llm, const json& result) {
    std::vector<wu::ChatMsg> msgs = {
        { "system", "너는 보안 분석가다. 아래 점검 결과를 3~5문장으로 요약하라.\n"
                    "판정 규칙(엄수): '실패(취약)' 항목만 취약으로 간주한다. "
                    "'점검불가(대상없음)'·'오류'·'통과'는 취약이 아니며 취약으로 말하지 마라. "
                    "취약(실패) 항목이 0개면 '취약 항목은 확인되지 않았습니다'라고 답하고, 점검불가가 있으면 그 사실만 알려라. "
                    "데이터에 없는 내용은 절대 지어내지 마라. 반드시 한국어로만 작성(한자·일본어 금지)." },
        { "user", compact_results(result, /*brief=*/true) },
    };
    return llm->chat(msgs, 220, 0.2f);
}

// 자동 요약: 실패(취약) 항목이 있을 때만 LLM 호출 (환각 표면 최소화)
static void print_ai_summary(const AppConfig& cfg, const json& r) {
    if (r.is_null() || !r.contains("summary")) return;
    if (r["summary"].value("fail", 0) <= 0) return;   // 취약 없음 → 규칙기반 판정줄로 충분, LLM 생략
    if (auto* llm = ensure_llm_reasoning(cfg))
        std::cout << "\n\033[36mAI>\033[0m " << llm_summarize(llm, r) << "\n";
}

// 후속 질문(ask): 직전 결과를 근거로 답변
static std::string llm_answer(wu::ILlmEngine* llm, const json& lastResult, const std::string& q) {
    std::string ctx = lastResult.is_null() ? "(직전 점검 결과 없음)" : compact_results(lastResult);
    std::vector<wu::ChatMsg> msgs = {
        { "system", "너는 보안 분석가다. 아래 점검 결과를 근거로 사용자의 질문에 답한다. "
                    "근거가 없으면 모른다고 답하라. 반드시 한국어로만 답하라. 중국어·일본어·한자 표기 금지.\n[결과]\n" + ctx },
        { "user", q },
    };
    return llm->chat(msgs, 400, 0.3f);
}

// ── op 서브커맨드 (Phase 3) ────────────────────────────────
static int cmd_list_ops() {
    auto ops = wu::OpRegistry::instance().all();
    wu::log::info("등록된 op " + std::to_string(ops.size()) + "개:");
    for (auto* op : ops) {
        const auto& d = op->descriptor();
        std::cout << "  " << d.name << "  [" << wu::to_string(d.safety) << "/" << d.impl << "]  " << d.summary << "\n";
    }
    return 0;
}

static int cmd_run_op(const std::string& name, const std::string& params_json) {
    auto* op = wu::OpRegistry::instance().find(name);
    if (!op) { wu::log::error("알 수 없는 op: " + name + " (`wu_agent ops`)"); return 1; }
    json params = json::object();
    if (!params_json.empty()) {
        try { params = json::parse(params_json); }
        catch (const std::exception& e) { wu::log::error(std::string("params JSON 파싱 실패: ") + e.what()); return 1; }
    }
    wu::OpContext ctx; ctx.host = make_host();
    wu::OpResult r = wu::run_op_guarded(op, params, ctx);
    std::cout << "status: " << wu::to_string(r.status) << "\n";
    if (r.status == wu::OpStatus::error) { wu::log::error(r.error); return 1; }
    std::cout << r.data.dump(2) << "\n";
    return 0;
}

// ── pack 실행 (Phase S) ────────────────────────────────────
static const char* status_tag(const std::string& s) {
    if (s == "pass")  return "\033[32m[PASS]\033[0m";
    if (s == "fail")  return "\033[31m[FAIL]\033[0m";
    if (s == "na")    return "\033[90m[ NA ]\033[0m";
    return "\033[33m[ERR ]\033[0m";
}

// ConsoleSink: §1.7 표 렌더링
static void render(const json& result) {
    std::cout << "\n";
    for (const auto& r : result["results"]) {
        std::cout << "  " << status_tag(r["status"].get<std::string>()) << " "
                  << r["checkId"].get<std::string>() << "  "
                  << r["title"].get<std::string>()
                  << "  (" << r["severity"].get<std::string>() << ")\n";
        const std::string st = r["status"].get<std::string>();
        if ((st == "fail" || st == "na" || st == "error")) {
            std::string d = r.value("detail", "");
            if (d.empty() && st == "fail") d = "조치: " + r.value("remediation", "");
            if (!d.empty()) std::cout << "         └ " << d << "\n";
        }
    }
    const auto& s = result["summary"];
    std::cout << "  ────────────────────────────────────────────\n";
    std::cout << "  요약: 총 " << s["total"].get<int>()
              << " | 통과 " << s["pass"].get<int>()
              << " | 실패 " << s["fail"].get<int>()
              << " | NA " << s["na"].get<int>()
              << " | 판정: " << s["verdict"].get<std::string>()
              << "  (" << s["headline"].get<std::string>() << ")\n";
}

// pack 실행 후 result json 반환(+a.json 기록). 실패 시 빈 객체.
static json run_pack(const AppConfig& cfg, const std::string& packId, const json& vars, bool write_file) {
    const std::string path = cfg.rulesDir + "/" + packId + ".json";
    std::ifstream f(path);
    if (!f) { wu::log::error("pack 없음: " + path); return json::object(); }
    json pj;
    try { f >> pj; } catch (const std::exception& e) { wu::log::error(std::string("pack JSON 파싱 실패: ") + e.what()); return json::object(); }

    wu::PathGuard guard(cfg.allow, cfg.deny);
    wu::OpContext ctx; ctx.host = make_host(); ctx.guard = &guard;

    try {
        wu::PackSpec pack = wu::parse_pack(pj);
        wu::Interpreter interp(ctx);
        wu::log::info("대상: " + pack.target.dump() + " | 점검 " + std::to_string(pack.checks.size()) + "건 실행");
        json result = interp.run_pack(pack, vars);

        if (write_file) {
            std::ofstream out("a.json");
            out << result.dump(2);
            wu::log::info("결과 저장: ./a.json");
        }
        return result;
    } catch (const std::exception& e) {
        wu::log::error(std::string("pack 실행 실패: ") + e.what());
        return json::object();
    }
}

// rulesDir 의 모든 packId (정렬)
static std::vector<std::string> all_pack_ids(const AppConfig& cfg) {
    std::vector<std::string> ids;
    if (fs::exists(cfg.rulesDir))
        for (const auto& e : fs::directory_iterator(cfg.rulesDir))
            if (e.path().extension() == ".json") ids.push_back(e.path().stem().string());
    std::sort(ids.begin(), ids.end());
    return ids;
}

// 주어진 pack 목록을 vars 로 실행 → 각각 표 렌더 + 통합 집계. last 로 쓸 마지막 결과 반환.
static json run_packs(const AppConfig& cfg, const std::vector<std::string>& ids, const json& vars) {
    if (ids.empty()) { wu::log::warn("실행할 pack 이 없습니다: " + cfg.rulesDir); return json::object(); }
    int T=0,P=0,F=0,N=0,E=0, vulnPacks=0;
    json last;
    for (const auto& id : ids) {
        json r = run_pack(cfg, id, vars, /*write_file=*/false);
        if (r.empty()) continue;
        std::cout << "\n\033[1m══ " << id << " ══\033[0m\n";
        render(r);
        const auto& s = r["summary"];
        T += s.value("total",0); P += s.value("pass",0); F += s.value("fail",0);
        N += s.value("na",0);    E += s.value("error",0);
        if (s.value("fail",0) > 0) ++vulnPacks;
        last = r;
    }
    std::cout << "\n\033[1m════════ 전체 통합 요약 ════════\033[0m\n"
              << "  점검 대상 " << ids.size() << "종 | 검사 항목 " << T << "개"
              << " | 통과 " << P << " | 실패 " << F << " | 검사불가 " << N << " | 오류 " << E
              << "  (취약 대상 " << vulnPacks << "종)\n";
    return last;
}

// 모든 pack 실행 (run_packs 위임)
static json run_all_packs(const AppConfig& cfg) {
    return run_packs(cfg, all_pack_ids(cfg), json::object());
}

static int cmd_list_rules(const AppConfig& cfg) {
    if (!fs::exists(cfg.rulesDir)) { wu::log::warn("rulesDir 없음: " + cfg.rulesDir); return 0; }
    wu::log::info("rules (" + cfg.rulesDir + "):");
    for (const auto& e : fs::directory_iterator(cfg.rulesDir))
        if (e.path().extension() == ".json")
            std::cout << "  " << e.path().stem().string() << "\n";
    return 0;
}

// "--var was.home=/x" 형태 파싱 → 평면 vars json (run_pack 이 set_nested 처리)
static json parse_vars(const std::vector<std::string>& args, size_t from) {
    json vars = json::object();
    for (size_t i = from; i < args.size(); ++i) {
        if (args[i] == "--var" && i + 1 < args.size()) {
            std::string kv = args[++i];
            auto eq = kv.find('=');
            if (eq != std::string::npos) vars[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
    }
    return vars;
}

// ── REPL (Phase S, Level 0 리졸버) ─────────────────────────
static bool read_repl_line(std::string& line
#if defined(WU_HAVE_REPLXX)
                           , replxx::Replxx* rx
#endif
                           ) {
#if defined(WU_HAVE_REPLXX)
    if (rx) {
        char const* r = rx->input("wu> ");
        if (r == nullptr) return false;
        line = r;
        if (!line.empty()) rx->history_add(line);
        return true;
    }
#endif
    std::cout << "wu> " << std::flush;
    return static_cast<bool>(std::getline(std::cin, line));
}

static int repl(const AppConfig& cfg) {
    wu::log::info("wu-agent REPL — `help` 로 도움말, `quit` 로 종료");

    // 대화형: 첫 입력 전에 모델 선로드 (대기시간 앞당김)
    if (cfg.llmMode != "off") {
        if (!ensure_llm(cfg))
            wu::log::warn("LLM 선로드 실패 — pack/op 명령은 계속 사용 가능");
    }

    json last;  // 마지막 실행 결과 (show 용)
    std::string line;
    std::string hist_path = rebase_rel(cfg.agentRoot, "cache/repl_history");

#if defined(WU_HAVE_REPLXX)
    std::unique_ptr<replxx::Replxx> rx;
    if (wu_stdin_is_tty()) {
        rx = std::make_unique<replxx::Replxx>();
        rx->set_max_history_size(1000);
        std::error_code ec;
        fs::create_directories(fs::path(hist_path).parent_path(), ec);
        rx->history_load(hist_path);
    }
#endif

    while (true) {
#if defined(WU_HAVE_REPLXX)
        if (!read_repl_line(line, rx.get())) break;
#else
        if (!read_repl_line(line)) break;
#endif
        std::istringstream ss(line);
        std::vector<std::string> tok; std::string t;
        while (ss >> t) tok.push_back(t);
        if (tok.empty()) continue;

        const std::string& cmd = tok[0];
        if (cmd == "quit" || cmd == "exit") break;
        else if (cmd == "help") {
            std::cout << "  list rules                       탐지 룰 목록\n"
                         "  run <ruleId> [--var k=v ...]     룰 실행 (예: run kisa-tomcat --var was.home=/opt/tomcat)\n"
                         "  run all                          모든 룰 실행 + 통합 요약\n"
                         "  ops                              op 목록\n"
                         "  show <checkId>                   직전 실행의 항목 상세\n"
                         "  explain                          직전 결과를 AI 가 요약 (llm 필요)\n"
                         "  ask <질문>                        직전 결과 근거로 AI 에게 질문 (llm 필요)\n"
                         "  <자연어>                          예: '톰캣 보안 점검해줘' → AI 가 룰 선택/응답\n"
                         "  quit                             종료\n";
        }
        else if (cmd == "ops") cmd_list_ops();
        else if (cmd == "list" && tok.size() >= 2 && (tok[1] == "rules" || tok[1] == "packs")) cmd_list_rules(cfg);
        else if (cmd == "run" && tok.size() >= 2 && (tok[1] == "all" || tok[1] == "*")) {
            json r = run_all_packs(cfg);
            if (!r.empty()) last = r;
        }
        else if (cmd == "run" && tok.size() >= 2) {
            json vars = parse_vars(tok, 2);
            json r = run_pack(cfg, tok[1], vars, /*write_file=*/true);
            if (!r.empty()) { render(r); last = r; print_ai_summary(cfg, r); }
        }
        else if (cmd == "show" && tok.size() >= 2) {
            if (last.is_null()) { wu::log::warn("먼저 pack 을 실행하세요"); continue; }
            bool found = false;
            for (const auto& r : last["results"]) if (r["checkId"] == tok[1]) {
                std::cout << r.dump(2) << "\n"; found = true; break;
            }
            if (!found) wu::log::warn("checkId 없음: " + tok[1]);
        }
        else if (cmd == "explain") {
            if (auto* llm = ensure_llm_reasoning(cfg)) std::cout << "AI> " << llm_summarize(llm, last) << "\n";
        }
        else if (cmd == "ask" && tok.size() >= 2) {
            std::string q = line.substr(line.find("ask") + 4);
            if (auto* llm = ensure_llm_reasoning(cfg)) std::cout << "AI> " << llm_answer(llm, last, q) << "\n";
        }
        else if (cmd == "agent" && tok.size() >= 2) {  // 다단계 ReAct
            std::string goal = line.substr(line.find("agent") + 6);
            if (auto* llm = ensure_llm_reasoning(cfg)) {
                wu::PathGuard guard(cfg.allow, cfg.deny);
                wu::OpContext ctx; ctx.host = make_host(); ctx.guard = &guard;
                std::cout << "AI> " << wu::run_react(*llm, ctx, goal, /*max_steps=*/4) << "\n";
            }
        }
        else {
            // Level 0-a: "모든/전체/전부 검사" → 모든 pack 실행
            {
                auto has = [&](const char* k){ return line.find(k) != std::string::npos; };
                bool wantAll = (has("모든") || has("전체") || has("전부") || has("all") || has("싹 다"));
                bool aboutCheck = (has("검사") || has("점검") || has("실행") || has("돌려") || has("scan") || has("run"));
                if (wantAll && aboutCheck) {
                    json r = run_all_packs(cfg);
                    if (!r.empty()) last = r;
                    continue;
                }
            }
            // Level 0: 문장에 pack 이름이 그대로 포함되면 즉시 실행 (LLM 불필요)
            std::string hit;
            if (fs::exists(cfg.rulesDir))
                for (const auto& e : fs::directory_iterator(cfg.rulesDir))
                    if (e.path().extension() == ".json" && line.find(e.path().stem().string()) != std::string::npos)
                        hit = e.path().stem().string();
            if (!hit.empty()) {
                wu::log::info("(해석) pack 실행: " + hit);
                json r = run_pack(cfg, hit, json::object(), true);
                if (!r.empty()) { render(r); last = r; print_ai_summary(cfg, r); }
                continue;
            }

            // Level 1: tool-calling 라우터 (grammar 강제 JSON → 파싱·검증)
            auto* llm = ensure_llm_reasoning(cfg);
            if (!llm) { wu::log::warn("알 수 없는 명령: " + line + "  (`help`)"); continue; }
            RouteDecision d = llm_route(llm, cfg, line);

            if (d.action == "all") {
                json r = run_packs(cfg, all_pack_ids(cfg), d.vars);
                if (!r.empty()) last = r;
            } else if (d.action == "run" && d.packs.size() == 1) {
                wu::log::info("(해석) pack 실행: " + d.packs[0] + (d.vars.empty() ? "" : (" vars=" + d.vars.dump())));
                json r = run_pack(cfg, d.packs[0], d.vars, true);
                if (!r.empty()) { render(r); last = r; print_ai_summary(cfg, r); }
            } else if (d.action == "run" && d.packs.size() >= 2) {
                std::string names; for (auto& p : d.packs) names += (names.empty()?"":", ") + p;
                wu::log::info("(해석) 복수 pack 실행: " + names + (d.vars.empty() ? "" : (" vars=" + d.vars.dump())));
                json r = run_packs(cfg, d.packs, d.vars);
                if (!r.empty()) last = r;
            } else {
                std::cout << "AI> " << llm_answer(llm, last, line) << "\n";  // none → 일반 대화 폴백
            }
        }
    }
#if defined(WU_HAVE_REPLXX)
    if (rx) rx->history_save(hist_path);
#endif
    return 0;
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    // -c / --config 선행 파싱
    std::string config_arg = "config/config.json";
    {
        std::vector<std::string> rest;
        for (size_t i = 0; i < args.size(); ++i) {
            if ((args[i] == "-c" || args[i] == "--config") && i + 1 < args.size()) {
                config_arg = args[++i];
            } else {
                rest.push_back(args[i]);
            }
        }
        args.swap(rest);
    }

    std::string resolved = resolve_config_path(config_arg);
    if (resolved.empty()) {
        wu::log::error("config 를 찾을 수 없음: " + config_arg +
                       " (cwd 또는 실행파일 상위에서 탐색, -c 로 지정 가능)");
        return 1;
    }
    AppConfig cfg;
    std::string cfg_err;
    if (!load_config(resolved, cfg, cfg_err)) {
        wu::log::error(cfg_err);
        return 1;
    }
    wu::log::info("config=" + cfg.configPath + " llm.mode=" + cfg.llmMode +
                  " modelPath=" + cfg.modelPath);

    if (!args.empty()) {
        const std::string& c = args[0];
        if (c == "ops") return cmd_list_ops();
        if (c == "op") {
            if (args.size() < 2) { wu::log::error("usage: wu_agent op <name> [json]"); return 1; }
            return cmd_run_op(args[1], args.size() >= 3 ? args[2] : "");
        }
        if (c == "rules" || c == "packs") return cmd_list_rules(cfg);
        if (c == "run") {
            if (args.size() < 2) { wu::log::error("usage: wu_agent run <packId|all> [--var k=v]"); return 1; }
            if (args[1] == "all" || args[1] == "*") { run_all_packs(cfg); return 0; }
            json r = run_pack(cfg, args[1], parse_vars(args, 2), true);
            if (r.empty()) return 1;
            render(r);
            return 0;
        }
        if (c == "repl") return repl(cfg);
        if (c == "agent") {  // 다단계 ReAct: wu_agent agent "<목표>"
            if (args.size() < 2) { wu::log::error("usage: wu_agent agent <goal>"); return 1; }
            auto* llm = ensure_llm_reasoning(cfg);
            if (!llm) return 1;
            std::string goal; for (size_t i = 1; i < args.size(); ++i) goal += (i>1?" ":"") + args[i];
            wu::PathGuard guard(cfg.allow, cfg.deny);
            wu::OpContext ctx; ctx.host = make_host(); ctx.guard = &guard;
            std::cout << wu::run_react(*llm, ctx, goal, 4) << "\n";
            return 0;
        }
        if (c == "llm") {  // Phase 1 스모크: 단일 턴 생성
            if (args.size() < 2) { wu::log::error("usage: wu_agent llm <text>"); return 1; }
            auto* llm = ensure_llm(cfg);
            if (!llm) return 1;
            std::string text; for (size_t i = 1; i < args.size(); ++i) text += (i>1?" ":"") + args[i];
            std::cout << llm->chat({ {"system","너는 간결한 한국어 비서다."}, {"user", text} }, 400, cfg.temperature) << "\n";
            return 0;
        }
        if (c == "-h" || c == "--help") {
            std::cout << "wu-agent " << WU_AGENT_VERSION << "\n"
                         "  [-c|--config <path>] ops | op <name> [json] | rules | run <ruleId> [--var k=v] | llm <text> | repl\n";
            return 0;
        }
    }

    wu::log::info(std::string("wu-agent ") + WU_AGENT_VERSION + " (os=" + os_name() + ") — Phase S");
    wu::log::info("`wu_agent repl` 로 대화형, `wu_agent run kisa-tomcat` 로 즉시 점검.");
    return 0;
}
