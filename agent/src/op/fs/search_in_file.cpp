// op: fs.search_in_file — 파일 내 문자열/정규식 검색(grep). read-only, path_guard 경유.
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <fstream>
#include <regex>
#include <string>

namespace wu {

class FsSearchInFileOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "fs.search_in_file"; d.summary = "파일 안에서 문자열/정규식을 검색해 매칭 라인을 반환";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "path",       "path",   true,  nullptr, {}, "대상 파일" },
            { "pattern",    "string", true,  nullptr, {}, "검색 패턴" },
            { "is_regex",   "bool",   false, false,   {}, "정규식 여부" },
            { "max_matches","uint",   false, 100,     {}, "최대 매칭 수" },
        };
        d.returns = { {"count","int",""}, {"matches","array<object>","[{line,text}]"}, {"fileExists","bool",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.at("path").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(path))
            return OpResult::err("path_guard 거부: " + path);

        const std::string pattern = p.at("pattern").get<std::string>();
        const bool is_regex = p.value("is_regex", false);
        const long max_matches = p.value("max_matches", 100L);

        std::ifstream f(path);
        if (!f) return OpResult::na("파일 없음: " + path);  // 검사 불가 → na (오탐 방지)

        std::regex re;
        if (is_regex) { try { re = std::regex(pattern); } catch (...) { return OpResult::err("잘못된 정규식: " + pattern); } }

        json matches = json::array();
        std::string line; long lineno = 0, count = 0;
        while (std::getline(f, line)) {
            ++lineno;
            bool hit = is_regex ? std::regex_search(line, re)
                                : (line.find(pattern) != std::string::npos);
            if (hit) {
                ++count;
                if ((long)matches.size() < max_matches)
                    matches.push_back({ {"line", lineno}, {"text", line} });
            }
        }
        return OpResult::ok_({ {"count", count}, {"matches", matches}, {"fileExists", true} });
    }
};
REGISTER_OP(FsSearchInFileOp)

} // namespace wu
