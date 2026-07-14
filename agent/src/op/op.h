#pragma once
// op 프레임워크 핵심 계약 (final_plan.md §6.2)
#include "json.hpp"
#include <string>
#include <vector>
#include <optional>

namespace wu {

using json = nlohmann::json;

// ── 실행 결과 ──────────────────────────────────────────────
enum class OpStatus { ok, error, not_applicable };

inline const char* to_string(OpStatus s) {
    switch (s) {
        case OpStatus::ok: return "ok";
        case OpStatus::error: return "error";
        default: return "na";
    }
}

struct OpResult {
    OpStatus    status = OpStatus::ok;
    json        data   = json::object();  // 관찰 op: 구조화 결과 / assertion op: {"passed":bool}
    std::string error;                    // status==error 시 사유

    static OpResult ok_(json d)          { return { OpStatus::ok, std::move(d), "" }; }
    static OpResult err(std::string e)   { return { OpStatus::error, json::object(), std::move(e) }; }
    static OpResult na(std::string w="") { return { OpStatus::not_applicable, json::object(), std::move(w) }; }
};

// ── 안전 등급 (이름이 곧 등급 신호, §5.3/§11) ────────────────
enum class Safety { read_only_local, read_only_loopback, active_network, mutating };

inline const char* to_string(Safety s) {
    switch (s) {
        case Safety::read_only_local:    return "read_only_local";
        case Safety::read_only_loopback: return "read_only_loopback";
        case Safety::active_network:     return "active_network";
        default:                         return "mutating";
    }
}

// ── 파라미터/출력 스키마 (§6.3/§6.4) ────────────────────────
struct Constraint {
    std::optional<long>      min, max;   // 수치 범위 (port: 1..65535)
    std::vector<std::string> choices;    // enum 허용값
    std::string              regex;      // 패턴
};

struct ParamSpec {
    std::string name;
    std::string type;                    // int|uint|bool|string|enum|port|path|glob|regex|ipaddr|cidr|duration|product
    bool        required = false;
    json        default_value = nullptr;
    Constraint  constraint;
    std::string desc;
    int         position = -1;           // >=0 이면 대화형 positional 순서
};

struct FieldSpec { std::string name, type, desc; };  // 출력 필드

struct OpDescriptor {
    std::string              name;       // "net.list_ports"
    std::string              summary;    // LLM 프롬프트용
    std::vector<ParamSpec>   params;
    std::vector<FieldSpec>   returns;
    Safety                   safety = Safety::read_only_local;
    std::vector<std::string> os;         // {"linux","windows","macos"}
    std::vector<json>        examples;
    std::string              impl = "native";  // "native"|"subprocess"|"shell" (감사용)
};

// ── op 컨텍스트 (러너가 주입) ───────────────────────────────
struct HostInfo { std::string os, arch, hostname; };

struct Policy {
    bool     readOnlyMode    = true;
    int      perOpTimeoutSec = 30;
    size_t   outputCapBytes  = 65536;
    std::vector<std::string> readAllowlist;
    std::vector<std::string> readDenylist;
};

class PathGuard;  // context.h

struct OpContext {
    Policy           policy;
    HostInfo         host;
    const PathGuard* guard = nullptr;    // fs op 는 이걸 경유해야 함
};

// ── 모든 op 의 공통 인터페이스 ──────────────────────────────
class IOp {
public:
    virtual ~IOp() = default;
    virtual const OpDescriptor& descriptor() const = 0;   // 구조 = 이 하나
    virtual OpResult            run(const json& params, OpContext& ctx) = 0;
};

} // namespace wu
