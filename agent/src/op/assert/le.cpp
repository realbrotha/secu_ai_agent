// op: assert.le — value <= threshold 이면 통과. (권한 mode 등)
// 값은 숫자 또는 문자열("0640"=octal, "420"=decimal). "0" 접두 + 8진수 숫자면 octal 로 해석.
#include "op/op.h"
#include "op/registry.h"
#include <string>

namespace wu {

static bool is_octal_str(const std::string& s) {
    if (s.size() < 2 || s[0] != '0') return false;
    for (size_t i = 1; i < s.size(); ++i) if (s[i] < '0' || s[i] > '7') return false;
    return true;
}

static long coerce(const json& v) {
    if (v.is_number_integer()) return v.get<long>();
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        return std::strtol(s.c_str(), nullptr, is_octal_str(s) ? 8 : 10);
    }
    if (v.is_number_float()) return (long)v.get<double>();
    return 0;
}

class AssertLeOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "assert.le"; d.summary = "value <= threshold 이면 통과 (octal 문자열 지원)";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "value",     "any", true, nullptr, {}, "검사 값(숫자/octal문자열)" },
            { "threshold", "any", true, nullptr, {}, "상한(숫자/octal문자열)" },
        };
        d.returns = { {"passed","bool",""}, {"value","int",""}, {"threshold","int",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext&) override {
        long v = coerce(p.at("value"));
        long t = coerce(p.at("threshold"));
        return OpResult::ok_({ {"passed", v <= t}, {"value", v}, {"threshold", t} });
    }
};
REGISTER_OP(AssertLeOp)

} // namespace wu
