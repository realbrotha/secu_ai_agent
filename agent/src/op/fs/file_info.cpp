// op: fs.file_info — 파일 메타데이터(존재/크기/권한). read-only, path_guard 경유.
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;

namespace wu {

static std::string perms_octal(fs::perms p) {
    int m = 0;
    using fp = fs::perms;
    auto has = [&](fp bit){ return (p & bit) != fp::none; };
    if (has(fp::owner_read))  m |= 0400; if (has(fp::owner_write)) m |= 0200; if (has(fp::owner_exec)) m |= 0100;
    if (has(fp::group_read))  m |= 0040; if (has(fp::group_write)) m |= 0020; if (has(fp::group_exec)) m |= 0010;
    if (has(fp::others_read)) m |= 0004; if (has(fp::others_write))m |= 0002; if (has(fp::others_exec))m |= 0001;
    char buf[8]; std::snprintf(buf, sizeof(buf), "0%03o", m);
    return buf;
}

class FsFileInfoOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "fs.file_info"; d.summary = "파일 존재/크기/권한(octal) 등 메타데이터";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = { { "path", "path", true, nullptr, {}, "대상 경로" } };
        d.returns = { {"exists","bool",""},{"isDir","bool",""},{"size","int",""},{"mode","string","octal 예:0644"} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.at("path").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(path))
            return OpResult::err("path_guard 거부: " + path);

        std::error_code ec;
        bool exists = fs::exists(path, ec);
        json out = { {"path", path}, {"exists", exists} };
        if (exists) {
            out["isDir"] = fs::is_directory(path, ec);
            out["size"]  = out["isDir"].get<bool>() ? 0 : (long long)fs::file_size(path, ec);
            out["mode"]  = perms_octal(fs::status(path, ec).permissions());
        }
        return OpResult::ok_(out);
    }
};
REGISTER_OP(FsFileInfoOp)

} // namespace wu
