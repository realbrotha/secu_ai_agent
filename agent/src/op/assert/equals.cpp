// op: assert.equals — value == expected 이면 통과 (JSON 값 비교)
#include "op/op.h"
#include "op/registry.h"

namespace wu {

class AssertEqualsOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "assert.equals"; d.summary = "value == expected 이면 통과";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "value",    "any", true, nullptr, {}, "검사 값" },
            { "expected", "any", true, nullptr, {}, "기대 값" },
        };
        d.returns = { {"passed","bool",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext&) override {
        // 숫자/문자열 혼용 관용: 둘 다 스칼라면 문자열로 비교도 시도
        const json& a = p.at("value");
        const json& b = p.at("expected");
        bool eq = (a == b);
        if (!eq && a.is_primitive() && b.is_primitive()) {
            auto s = [](const json& x){ return x.is_string() ? x.get<std::string>() : x.dump(); };
            eq = (s(a) == s(b));
        }
        return OpResult::ok_({ {"passed", eq} });
    }
};
REGISTER_OP(AssertEqualsOp)

} // namespace wu
