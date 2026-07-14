// op: config.read_value — 설정 파일에서 값 추출 (실용 파서). read-only, path_guard.
//   format: properties(key=value / key:value) | apache(Directive args) | xml_attr(attr="...") | regex(group)
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>

namespace wu {

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

class ConfigReadValueOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "config.read_value"; d.summary = "설정 파일에서 값 추출(properties/apache/xml_attr/regex)";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "path",    "path",   true,  nullptr, {}, "설정 파일" },
            { "format",  "enum",   true,  nullptr, { {}, {}, {"properties","apache","xml_attr","regex"}, "" }, "포맷" },
            { "key",     "string", false, nullptr, {}, "properties/apache 의 키/디렉티브" },
            { "attr",    "string", false, nullptr, {}, "xml_attr 의 속성명" },
            { "pattern", "string", false, nullptr, {}, "regex 포맷의 정규식(캡처그룹 1)" },
        };
        d.returns = { {"found","bool",""}, {"value","string","첫 값"}, {"values","array<string>","모든 값"} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.at("path").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(path)) return OpResult::err("path_guard 거부: " + path);
        std::ifstream f(path);
        if (!f) return OpResult::na("파일 없음: " + path);
        const std::string fmt = p.at("format").get<std::string>();

        std::vector<std::string> values;

        if (fmt == "xml_attr" || fmt == "regex") {
            std::stringstream ss; ss << f.rdbuf();
            const std::string body = ss.str();
            std::string rex = (fmt == "xml_attr")
                ? (p.value("attr", std::string("")) + "\\s*=\\s*\"([^\"]*)\"")
                : p.value("pattern", std::string(""));
            if (rex.empty()) return OpResult::err("attr/pattern 누락");
            try {
                std::regex re(rex);
                for (auto it = std::sregex_iterator(body.begin(), body.end(), re); it != std::sregex_iterator(); ++it)
                    values.push_back(it->size() > 1 ? (*it)[1].str() : (*it)[0].str());
            } catch (...) { return OpResult::err("잘못된 정규식"); }
        } else {  // properties / apache (라인 단위)
            const std::string key = lower(p.value("key", std::string("")));
            if (key.empty()) return OpResult::err("key 누락");
            std::string line;
            while (std::getline(f, line)) {
                std::string t = trim(line);
                if (t.empty() || t[0] == '#' || t[0] == ';') continue;
                if (fmt == "properties") {
                    size_t eq = t.find_first_of("=:");
                    if (eq == std::string::npos) continue;
                    if (lower(trim(t.substr(0, eq))) == key) values.push_back(trim(t.substr(eq + 1)));
                } else { // apache: "Directive args..."
                    std::istringstream ls(t); std::string dir; ls >> dir;
                    if (lower(dir) == key) { std::string rest; std::getline(ls, rest); values.push_back(trim(rest)); }
                }
            }
        }

        json arr = json::array(); for (auto& v : values) arr.push_back(v);
        return OpResult::ok_({ {"found", !values.empty()},
                               {"value", values.empty() ? "" : values.front()},
                               {"values", arr} });
    }
};
REGISTER_OP(ConfigReadValueOp)

} // namespace wu
