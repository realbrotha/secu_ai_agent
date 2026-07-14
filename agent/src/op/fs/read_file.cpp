// op: fs.read_file — 텍스트 파일 내용 읽기(상한). read-only, path_guard.
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <fstream>
#include <sstream>

namespace wu {

class FsReadFileOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "fs.read_file"; d.summary = "파일 내용을 읽는다(최대 max_bytes)";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "path",      "path", true,  nullptr, {}, "대상 파일" },
            { "max_bytes", "uint", false, 8192,    {}, "최대 바이트" },
        };
        d.returns = { {"content","string",""}, {"size","int",""}, {"truncated","bool",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.at("path").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(path)) return OpResult::err("path_guard 거부: " + path);
        const long cap = p.value("max_bytes", 8192L);
        std::ifstream f(path, std::ios::binary);
        if (!f) return OpResult::na("파일 없음: " + path);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        bool truncated = false;
        if ((long)content.size() > cap) { content.resize(cap); truncated = true; }
        return OpResult::ok_({ {"content", content}, {"size", (long)content.size()}, {"truncated", truncated} });
    }
};
REGISTER_OP(FsReadFileOp)

} // namespace wu
