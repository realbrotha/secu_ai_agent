// op: assert.not_contains — value 에 needle 이 없어야 통과 (Tier2 조합용, 순수)
#include "op/op.h"
#include "op/registry.h"

namespace wu {

// value 가 문자열이면 부분문자열, 배열이면 원소 포함 여부로 판정
static bool contains(const json& value, const std::string& needle) {
    if (value.is_string())
        return value.get<std::string>().find(needle) != std::string::npos;
    if (value.is_array()) {
        for (const auto& v : value) {
            if (v.is_string() && v.get<std::string>() == needle) return true;
            if (!v.is_string() && v.dump() == needle) return true;
        }
    }
    return false;
}

class AssertNotContainsOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name    = "assert.not_contains";
        d.summary = "value(문자열/배열)에 needle 이 없어야 통과";
        d.safety  = Safety::read_only_local;
        d.os      = {"linux", "windows", "macos"};
        d.impl    = "native";
        d.params  = {
            { "value",  "any",    true, nullptr, {}, "검사 대상(문자열 또는 배열)" },
            { "needle", "string", true, nullptr, {}, "없어야 하는 값" },
        };
        d.returns = { {"passed", "bool", "needle 부재 시 true"} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext&) override {
        const json& value = p.at("value");   // 배열도 허용하므로 타입검증은 자체 처리
        const std::string needle = p.at("needle").get<std::string>();
        const bool passed = !contains(value, needle);
        return OpResult::ok_({ {"passed", passed} });
    }
};

REGISTER_OP(AssertNotContainsOp)

} // namespace wu
