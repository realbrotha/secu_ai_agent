// op: fs.file_info — 파일 메타데이터(존재/크기/권한/소유자/타입). read-only, path_guard 경유.
//   platform::stat_file(lstat 기반) 사용 → setuid/setgid/sticky, owner/group, symlink 대상까지.
//   하위호환: 기존 반환 exists/isDir/size/mode(9비트 3자리 octal) 유지 + 신규 필드 추가.
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include "platform/fsmeta.h"
#include <cstdio>

namespace wu {

static std::string octal3(unsigned mode) {   // 하위 9비트 (rwxrwxrwx), 기존 형식 "0644"
    char buf[8]; std::snprintf(buf, sizeof(buf), "0%03o", mode & 0777);
    return buf;
}
static std::string octal4(unsigned mode) {   // 특수비트 포함 12비트, "04755"
    char buf[8]; std::snprintf(buf, sizeof(buf), "0%04o", mode & 07777);
    return buf;
}

class FsFileInfoOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "fs.file_info"; d.summary = "파일 존재/크기/권한(octal)/소유자/타입 메타데이터";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "path", "path", true, nullptr, {}, "대상 경로" },
            { "follow_symlinks", "bool", false, false, {}, "심링크 대상까지 따라감(기본 false=lstat)" },
        };
        d.returns = {
            {"exists","bool",""},{"isDir","bool",""},{"size","int",""},
            {"mode","string","octal 9비트 예:0644"},{"modeFull","string","특수비트 포함 예:04755"},
            {"type","string","file|dir|symlink|socket|..."},
            {"uid","int",""},{"gid","int",""},{"owner","string",""},{"group","string",""},
            {"isSymlink","bool",""},{"isSocket","bool",""},{"linkTarget","string",""},
            {"worldWritable","bool",""},{"groupWritable","bool",""},
            {"setuid","bool",""},{"setgid","bool",""},{"sticky","bool",""},
            {"modeValid","bool","Windows 는 false"},
        };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.at("path").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(path))
            return OpResult::err("path_guard 거부: " + path);
        const bool follow = p.value("follow_symlinks", false);

        platform::FileMeta m = platform::stat_file(path, follow);
        json out = { {"path", path}, {"exists", m.exists} };
        if (!m.exists) return OpResult::ok_(out);

        out["isDir"]       = m.isDir;
        out["size"]        = m.size;
        out["type"]        = m.type;
        out["isSymlink"]   = m.isSymlink;
        out["isSocket"]    = m.isSocket;
        out["modeValid"]   = m.modeValid;
        if (!m.linkTarget.empty()) out["linkTarget"] = m.linkTarget;
        if (m.uid >= 0) out["uid"] = m.uid;
        if (m.gid >= 0) out["gid"] = m.gid;
        if (!m.owner.empty()) out["owner"] = m.owner;
        if (!m.group.empty()) out["group"] = m.group;

        if (m.modeValid) {
            out["mode"]          = octal3(m.mode);
            out["modeFull"]      = octal4(m.mode);
            out["worldWritable"] = (m.mode & 0002) != 0;
            out["groupWritable"] = (m.mode & 0020) != 0;
            out["setuid"]        = (m.mode & 04000) != 0;
            out["setgid"]        = (m.mode & 02000) != 0;
            out["sticky"]        = (m.mode & 01000) != 0;
        }
        return OpResult::ok_(out);
    }
};
REGISTER_OP(FsFileInfoOp)

} // namespace wu
