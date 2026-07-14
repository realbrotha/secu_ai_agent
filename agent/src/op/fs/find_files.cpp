// op: fs.find_files — 이름 글로브로 재귀 탐색. read-only, path_guard.
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

namespace wu {

static std::regex glob_to_regex(const std::string& glob) {
    std::string re;
    for (char c : glob) {
        switch (c) {
            case '*': re += ".*"; break;
            case '?': re += '.'; break;
            case '.': re += "\\."; break;
            default:  re += c;
        }
    }
    return std::regex("^" + re + "$", std::regex::icase);
}

class FsFindFilesOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "fs.find_files"; d.summary = "root 이하에서 이름 글로브(*, ?)로 파일을 재귀 탐색";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "root",        "path",   true,  nullptr, {}, "탐색 시작 디렉터리" },
            { "name_glob",   "glob",   true,  nullptr, {}, "이름 패턴(예: *.conf)" },
            { "max_results", "uint",   false, 200,     {}, "최대 결과 수" },
        };
        d.returns = { {"files","array<string>",""}, {"count","int",""}, {"truncated","bool",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext& ctx) override {
        const std::string root = p.at("root").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(root)) return OpResult::err("path_guard 거부: " + root);
        std::error_code ec;
        if (!fs::exists(root, ec)) return OpResult::na("경로 없음: " + root);
        const long cap = p.value("max_results", 200L);
        std::regex re = glob_to_regex(p.at("name_glob").get<std::string>());

        json files = json::array(); bool truncated = false;
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file(ec) && std::regex_match(it->path().filename().string(), re)) {
                if ((long)files.size() >= cap) { truncated = true; break; }
                files.push_back(it->path().string());
            }
        }
        return OpResult::ok_({ {"files", files}, {"count", (int)files.size()}, {"truncated", truncated} });
    }
};
REGISTER_OP(FsFindFilesOp)

} // namespace wu
