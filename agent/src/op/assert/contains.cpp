// op: assert.contains — value(문자열/배열)에 needle 이 있어야 통과
#include "op/op.h"
#include "op/registry.h"

namespace wu {

static bool has(const json& value, const std::string& needle) {
    if (value.is_string()) return value.get<std::string>().find(needle) != std::string::npos;
    if (value.is_array())
        for (const auto& v : value) {
            if (v.is_string() && v.get<std::string>() == needle) return true;
            if (!v.is_string() && v.dump() == needle) return true;
        }
    return false;
}

class AssertContainsOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "assert.contains"; d.summary = "value 에 needle 이 있어야 통과";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = { { "value", "any", true, nullptr, {}, "대상" }, { "needle", "string", true, nullptr, {}, "포함되어야 하는 값" } };
        d.returns = { {"passed","bool",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext&) override {
        return OpResult::ok_({ {"passed", has(p.at("value"), p.at("needle").get<std::string>())} });
    }
};
REGISTER_OP(AssertContainsOp)

} // namespace wu
