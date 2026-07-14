// op: fs.list_dir — 디렉터리 나열. read-only, path_guard.
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace wu {

class FsListDirOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "fs.list_dir"; d.summary = "디렉터리의 항목을 나열한다";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = { { "path", "path", true, nullptr, {}, "대상 디렉터리" } };
        d.returns = { {"entries","array<object>","[{name,type}]"}, {"count","int",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.at("path").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(path)) return OpResult::err("path_guard 거부: " + path);
        std::error_code ec;
        if (!fs::exists(path, ec)) return OpResult::na("경로 없음: " + path);
        json arr = json::array();
        for (const auto& e : fs::directory_iterator(path, ec)) {
            arr.push_back({ {"name", e.path().filename().string()},
                            {"type", e.is_directory(ec) ? "dir" : "file"} });
        }
        return OpResult::ok_({ {"entries", arr}, {"count", (int)arr.size()} });
    }
};
REGISTER_OP(FsListDirOp)

} // namespace wu
