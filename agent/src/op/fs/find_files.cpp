// op: fs.find_files — 재귀 탐색(이름 글로브 + 타입/권한/깊이/무소유자 필터). read-only, path_guard.
//   find 대체: type(f|d|l), perm(octal mask, -4000/-2000), maxdepth, nouser, nogroup, 히트별 메타.
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include "platform/fsmeta.h"
#include <filesystem>
#include <regex>
#include <cstdio>

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

static std::string octal4(unsigned mode) {
    char buf[8]; std::snprintf(buf, sizeof(buf), "0%04o", mode & 07777); return buf;
}

class FsFindFilesOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "fs.find_files"; d.summary = "root 이하 재귀 탐색(이름/타입/권한/깊이/무소유자 필터)";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "root",        "path",   true,  nullptr, {}, "탐색 시작 디렉터리" },
            { "name_glob",   "glob",   false, "*",     {}, "이름 패턴(예: *.conf)" },
            { "type",        "enum",   false, nullptr, { {}, {}, {"f","d","l"}, "" }, "f=파일 d=디렉터리 l=심링크" },
            { "perm",        "string", false, nullptr, {}, "octal mask(예: 4000 setuid, 2000 setgid, 0002 world-write)" },
            { "maxdepth",    "int",    false, -1L,     {}, "최대 재귀 깊이(-1=무제한)" },
            { "nouser",      "bool",   false, false,   {}, "소유 uid 가 passwd 에 없는 항목만" },
            { "nogroup",     "bool",   false, false,   {}, "소유 gid 가 group 에 없는 항목만" },
            { "max_results", "uint",   false, 500L,    {}, "최대 결과 수" },
        };
        d.returns = { {"files","array<string>","경로 목록"}, {"matches","array<object>","경로+메타"},
                      {"count","int",""}, {"truncated","bool",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext& ctx) override {
        const std::string root = p.at("root").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(root)) return OpResult::err("path_guard 거부: " + root);
        std::error_code ec;
        if (!fs::exists(root, ec)) return OpResult::na("경로 없음: " + root);

        const long cap      = p.value("max_results", 500L);
        const long maxdepth = p.value("maxdepth", -1L);
        const bool nouser   = p.value("nouser", false);
        const bool nogroup  = p.value("nogroup", false);
        const std::string typ = p.value("type", std::string(""));
        std::regex re = glob_to_regex(p.value("name_glob", std::string("*")));
        unsigned permMask = 0; bool hasPerm = false;
        if (p.contains("perm") && p["perm"].is_string()) {
            permMask = (unsigned)std::strtol(p["perm"].get<std::string>().c_str(), nullptr, 8);
            hasPerm = true;
        }

        json files = json::array(), matches = json::array();
        bool truncated = false;
        auto opts = fs::directory_options::skip_permission_denied;
        for (auto it = fs::recursive_directory_iterator(root, opts, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (maxdepth >= 0 && it.depth() > maxdepth) { it.disable_recursion_pending(); continue; }
            const fs::path& path = it->path();

            if (!std::regex_match(path.filename().string(), re)) continue;

            platform::FileMeta m = platform::stat_file(path.string(), false);
            if (!m.exists) continue;
            if (typ == "f" && !m.isRegular) continue;
            if (typ == "d" && !m.isDir) continue;
            if (typ == "l" && !m.isSymlink) continue;
            if (hasPerm && !(m.modeValid && (m.mode & permMask) == permMask)) continue;
            if (nouser  && !(m.uid >= 0 && m.owner.empty())) continue;
            if (nogroup && !(m.gid >= 0 && m.group.empty())) continue;

            if ((long)files.size() >= cap) { truncated = true; break; }
            files.push_back(path.string());
            json meta = { {"path", path.string()}, {"type", m.type} };
            if (m.modeValid)      meta["mode"]  = octal4(m.mode);
            if (!m.owner.empty()) meta["owner"] = m.owner;
            if (!m.group.empty()) meta["group"] = m.group;
            if (m.uid >= 0)       meta["uid"]   = m.uid;
            if (m.gid >= 0)       meta["gid"]   = m.gid;
            if (!m.linkTarget.empty()) meta["linkTarget"] = m.linkTarget;
            matches.push_back(std::move(meta));
        }
        return OpResult::ok_({ {"files", files}, {"matches", matches},
                               {"count", (int)files.size()}, {"truncated", truncated} });
    }
};
REGISTER_OP(FsFindFilesOp)

} // namespace wu
