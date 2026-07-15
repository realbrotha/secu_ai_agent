#include "op/runner.h"
#include "util/log.h"
#include <regex>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace wu {

namespace {

// 기본값 채움
void apply_defaults(const std::vector<ParamSpec>& specs, json& params) {
    for (const auto& s : specs) {
        if (!params.contains(s.name) && !s.default_value.is_null())
            params[s.name] = s.default_value;
    }
}

// enum 값 대소문자 정규화 (LLM 이 "LISTEN" 처럼 대문자를 줘도 canonical 소문자로 보정)
void normalize_enums(const std::vector<ParamSpec>& specs, json& params) {
    for (const auto& s : specs) {
        if (s.constraint.choices.empty() || !params.contains(s.name) || !params[s.name].is_string()) continue;
        std::string v = params[s.name].get<std::string>();
        std::string lv = v; std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
        for (const auto& c : s.constraint.choices) {
            std::string lc = c; std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
            if (lc == lv) { params[s.name] = c; break; }
        }
    }
}

// 타입/필수/enum/범위 검증. 실패 시 사유 문자열, 통과 시 빈 문자열.
std::string validate(const std::vector<ParamSpec>& specs, const json& params) {
    for (const auto& s : specs) {
        const bool present = params.contains(s.name);
        if (!present) {
            if (s.required) return "필수 파라미터 누락: " + s.name;
            continue;
        }
        const json& v = params.at(s.name);

        // 임의 타입(문자열/배열/객체 모두 허용) — op 가 자체 처리
        if (s.type == "any" || s.type == "json") continue;

        // enum
        if (!s.constraint.choices.empty()) {
            if (!v.is_string()) return s.name + " 은 enum(문자열) 이어야 함";
            const std::string sv = v.get<std::string>();
            bool ok = false;
            for (const auto& c : s.constraint.choices) if (c == sv) { ok = true; break; }
            if (!ok) return s.name + " 은 허용값 중 하나여야 함 (" + sv + ")";
        }

        // 기본 타입
        if (s.type == "int" || s.type == "uint" || s.type == "port") {
            if (!v.is_number_integer()) return s.name + " 은 정수여야 함";
            long n = v.get<long>();
            if (s.type == "uint" && n < 0) return s.name + " 은 0 이상이어야 함";
            if (s.type == "port" && (n < 1 || n > 65535)) return s.name + " 은 1..65535 여야 함";
            if (s.constraint.min && n < *s.constraint.min) return s.name + " 이 최소값 미만";
            if (s.constraint.max && n > *s.constraint.max) return s.name + " 이 최대값 초과";
        } else if (s.type == "bool") {
            if (!v.is_boolean()) return s.name + " 은 bool 이어야 함";
        } else if (s.type == "array") {
            if (!v.is_array()) return s.name + " 은 배열이어야 함";
        } else if (s.type == "object") {
            if (!v.is_object()) return s.name + " 은 객체여야 함";
        } else if (s.constraint.choices.empty()) {
            // string/path/glob/regex 등은 문자열
            if (!v.is_string()) return s.name + " 은 문자열이어야 함";
            if (!s.constraint.regex.empty()) {
                try {
                    if (!std::regex_match(v.get<std::string>(), std::regex(s.constraint.regex)))
                        return s.name + " 이 패턴 불일치";
                } catch (...) { /* 잘못된 스키마 regex 는 무시 */ }
            }
        }
    }
    return "";
}

} // namespace

OpResult run_op_guarded(IOp* op, json params, OpContext& ctx) {
    if (!op) return OpResult::err("알 수 없는 op");
    const OpDescriptor& d = op->descriptor();

    // OS 지원 여부
    if (!d.os.empty() && !ctx.host.os.empty()) {
        bool sup = false;
        for (const auto& o : d.os) if (o == ctx.host.os) { sup = true; break; }
        if (!sup) return OpResult::na("이 OS(" + ctx.host.os + ")에서 미지원: " + d.name);
    }

    // read-only 정책
    if (ctx.policy.readOnlyMode && d.safety == Safety::mutating)
        return OpResult::err("정책상 read-only op 만 허용: " + d.name);

    apply_defaults(d.params, params);
    normalize_enums(d.params, params);
    if (auto e = validate(d.params, params); !e.empty())
        return OpResult::err("파라미터 검증 실패(" + d.name + "): " + e);

    OpResult r;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        r = op->run(params, ctx);
    } catch (const std::exception& ex) {
        r = OpResult::err(std::string("op 실행 예외(") + d.name + "): " + ex.what());
    } catch (...) {
        r = OpResult::err("op 실행 알 수 없는 예외: " + d.name);
    }
    const long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();

    // 소프트 타임아웃 가드: native read-only op 는 캡(max_bytes/results/matches)으로 유계.
    // 초과 시 경고만(안전한 스레드 취소 불가). 하드 kill 은 subprocess/shell op 도입 시 프로세스 종료로.
    if (ctx.policy.perOpTimeoutSec > 0 && ms > (long)ctx.policy.perOpTimeoutSec * 1000)
        wu::log::warn("  [op] " + d.name + " 소요 " + std::to_string(ms) + "ms (perOpTimeout 초과)");

    // op 단위 상세는 기본 숨김. WU_OP_VERBOSE=1 일 때만.
    if (const char* v = std::getenv("WU_OP_VERBOSE"); v && v[0] && std::strcmp(v, "0") != 0) {
        wu::log::info("  [op] " + d.name + " (" + std::to_string(ms) + "ms) params=" + params.dump() +
                      " -> " + to_string(r.status));
    }
    return r;
}

} // namespace wu
