// op: assert.absent — 경로가 존재하지 않아야 통과 (예: 예제앱 잔존 점검)
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <filesystem>

namespace wu {

class AssertAbsentOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "assert.absent"; d.summary = "지정 경로가 존재하지 않으면 통과";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = { { "path", "path", true, nullptr, {}, "존재하지 않아야 하는 경로" } };
        d.returns = { {"passed","bool",""}, {"exists","bool",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.at("path").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(path))
            return OpResult::err("path_guard 거부: " + path);
        std::error_code ec;
        bool exists = std::filesystem::exists(path, ec);
        return OpResult::ok_({ {"passed", !exists}, {"exists", exists} });
    }
};
REGISTER_OP(AssertAbsentOp)

} // namespace wu
