// op: assert.matches — value 가 정규식과 매칭되면 통과
#include "op/op.h"
#include "op/registry.h"
#include <regex>

namespace wu {

class AssertMatchesOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "assert.matches"; d.summary = "value 가 regex 와 매칭되면 통과";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = { { "value", "any", true, nullptr, {}, "대상(문자열화)" }, { "regex", "regex", true, nullptr, {}, "정규식" } };
        d.returns = { {"passed","bool",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext&) override {
        const json& v = p.at("value");
        const std::string s = v.is_string() ? v.get<std::string>() : v.dump();
        std::string pat = p.at("regex").get<std::string>();
        auto flags = std::regex::ECMAScript;
        if (pat.rfind("(?i)", 0) == 0) { flags |= std::regex::icase; pat = pat.substr(4); }  // std::regex 는 인라인 (?i) 미지원
        try {
            std::regex re(pat, flags);
            return OpResult::ok_({ {"passed", std::regex_search(s, re)} });
        } catch (...) { return OpResult::err("잘못된 정규식"); }
    }
};
REGISTER_OP(AssertMatchesOp)

} // namespace wu
