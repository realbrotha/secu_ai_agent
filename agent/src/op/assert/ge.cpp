// op: assert.ge — value >= threshold 이면 통과 (octal 문자열 지원)
#include "op/op.h"
#include "op/registry.h"
#include <string>

namespace wu {

static bool ge_is_octal(const std::string& s) {
    if (s.size() < 2 || s[0] != '0') return false;
    for (size_t i = 1; i < s.size(); ++i) if (s[i] < '0' || s[i] > '7') return false;
    return true;
}
static long ge_coerce(const json& v) {
    if (v.is_number_integer()) return v.get<long>();
    if (v.is_string()) { const std::string s = v.get<std::string>(); return std::strtol(s.c_str(), nullptr, ge_is_octal(s) ? 8 : 10); }
    if (v.is_number_float()) return (long)v.get<double>();
    return 0;
}

class AssertGeOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "assert.ge"; d.summary = "value >= threshold 이면 통과";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = { { "value", "any", true, nullptr, {}, "값" }, { "threshold", "any", true, nullptr, {}, "하한" } };
        d.returns = { {"passed","bool",""}, {"value","int",""}, {"threshold","int",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext&) override {
        long v = ge_coerce(p.at("value")), t = ge_coerce(p.at("threshold"));
        return OpResult::ok_({ {"passed", v >= t}, {"value", v}, {"threshold", t} });
    }
};
REGISTER_OP(AssertGeOp)

} // namespace wu
