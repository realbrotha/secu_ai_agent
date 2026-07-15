// op: config.xml_query — Tomcat XML(server.xml/web.xml/context.xml/tomcat-users.xml) 경량 질의.
//   자체 XML 파서(주석/선언 제거 → 요소 트리). tinyxml2 미사용.
//   observe: element 로 지정한 태그(임의 깊이) 열거 → attrs/text.
//   rule 모드(op_verdict):
//     servlet_init_param  (servletClass, initParam, mustEqual, defaultWhenAbsent)
//     error_codes         (codes[]: 모두 존재해야 pass)
//     attr_none_match     (attr, regex: 매칭 요소 있으면 fail)  ← 기본 계정명/디렉터리리스팅 등
//     attr_all_match      (attr, regex: 모든 요소가 매칭해야 pass)
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include "op/util_regex.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <regex>
#include <algorithm>

namespace wu {

namespace {

struct XmlNode {
    std::string tag;
    std::map<std::string,std::string> attrs;
    std::string text;
    std::vector<std::unique_ptr<XmlNode>> children;
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// 주석 / <?...?> / <!DOCTYPE ...> / CDATA 껍데기 제거(단순화)
std::string preprocess(std::string s) {
    // 주석 제거
    for (size_t p; (p = s.find("<!--")) != std::string::npos; ) {
        auto e = s.find("-->", p);
        if (e == std::string::npos) { s.erase(p); break; }
        s.erase(p, e - p + 3);
    }
    // <? ... ?>
    for (size_t p; (p = s.find("<?")) != std::string::npos; ) {
        auto e = s.find("?>", p);
        if (e == std::string::npos) { s.erase(p); break; }
        s.erase(p, e - p + 2);
    }
    // <!...>
    for (size_t p; (p = s.find("<!")) != std::string::npos; ) {
        auto e = s.find('>', p);
        if (e == std::string::npos) { s.erase(p); break; }
        s.erase(p, e - p + 1);
    }
    return s;
}

void parse_attrs(const std::string& in, std::map<std::string,std::string>& attrs) {
    static const std::regex re(R"RX(([A-Za-z_:][\w:.\-]*)\s*=\s*"([^"]*)"|([A-Za-z_:][\w:.\-]*)\s*=\s*'([^']*)')RX");
    for (auto it = std::sregex_iterator(in.begin(), in.end(), re); it != std::sregex_iterator(); ++it) {
        if ((*it)[1].matched) attrs[(*it)[1].str()] = (*it)[2].str();
        else                  attrs[(*it)[3].str()] = (*it)[4].str();
    }
}

// pos 를 진행시키며 children 을 파싱(부모 close 태그를 만나면 반환).
void parse_nodes(const std::string& s, size_t& pos, std::vector<std::unique_ptr<XmlNode>>& out) {
    while (pos < s.size()) {
        auto lt = s.find('<', pos);
        if (lt == std::string::npos) return;
        std::string text = trim(s.substr(pos, lt - pos));
        pos = lt;
        if (pos + 1 < s.size() && s[pos + 1] == '/') {   // 닫는 태그 → 상위로
            auto gt = s.find('>', pos); pos = (gt == std::string::npos) ? s.size() : gt + 1;
            return;
        }
        auto gt = s.find('>', pos);
        if (gt == std::string::npos) return;
        std::string inner = s.substr(pos + 1, gt - pos - 1);
        bool selfClose = !inner.empty() && inner.back() == '/';
        if (selfClose) inner.pop_back();
        std::istringstream ts(inner); std::string tag; ts >> tag;
        std::string rest = trim(inner.substr(std::min(inner.size(), inner.find(tag) + tag.size())));
        auto node = std::make_unique<XmlNode>();
        node->tag = tag;
        parse_attrs(rest, node->attrs);
        pos = gt + 1;
        if (!selfClose) {
            size_t childStart = pos;
            parse_nodes(s, pos, node->children);
            // 자식이 없으면 텍스트 노드로 간주
            if (node->children.empty()) {
                auto close = s.find("</", childStart);
                if (close != std::string::npos) node->text = trim(s.substr(childStart, close - childStart));
            }
        }
        out.push_back(std::move(node));
    }
}

void find_by_tag(const XmlNode* n, const std::string& tag, std::vector<const XmlNode*>& out) {
    for (auto& c : n->children) {
        if (c->tag == tag) out.push_back(c.get());
        find_by_tag(c.get(), tag, out);
    }
}

const XmlNode* child_by_tag(const XmlNode* n, const std::string& tag) {
    for (auto& c : n->children) if (c->tag == tag) return c.get();
    return nullptr;
}

} // namespace

class ConfigXmlQueryOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "config.xml_query"; d.summary = "Tomcat XML 요소/속성 질의 + 규칙 판정(경량 파서)";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "path",    "path",   true,  nullptr, {}, "XML 파일" },
            { "element", "string", false, nullptr, {}, "열거할 태그(임의 깊이)" },
            { "attr",    "string", false, nullptr, {}, "속성명(rule/observe)" },
            { "rule",    "enum",   false, nullptr,
              { {}, {}, {"servlet_init_param","error_codes","attr_none_match","attr_all_match"}, "" }, "판정 규칙" },
            { "regex",   "string", false, nullptr, {}, "attr_*_match 정규식" },
            { "servletClass", "string", false, nullptr, {}, "servlet_init_param 대상 클래스" },
            { "initParam",    "string", false, nullptr, {}, "servlet_init_param 파라미터명" },
            { "mustEqual",    "string", false, nullptr, {}, "기대 param 값" },
            { "defaultWhenAbsent", "string", false, nullptr, {}, "미설정 시 기본값" },
            { "codes",   "array",  false, nullptr, {}, "error_codes 필수 코드 목록" },
        };
        d.returns = { {"elements","array<object>",""}, {"count","int",""},
                      {"verdict","string",""}, {"detail","string",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.at("path").get<std::string>();
        if (ctx.guard && !ctx.guard->allowed(path)) return OpResult::err("path_guard 거부: " + path);
        std::ifstream f(path);
        if (!f) return OpResult::na("XML 없음: " + path);
        std::stringstream ss; ss << f.rdbuf();
        std::string body = preprocess(ss.str());

        XmlNode root; size_t pos = 0;
        parse_nodes(body, pos, root.children);

        const std::string rule = p.value("rule", std::string(""));

        // ── rule: servlet_init_param ──
        if (rule == "servlet_init_param") {
            std::vector<const XmlNode*> servlets; find_by_tag(&root, "servlet", servlets);
            const std::string cls  = p.value("servletClass", std::string(""));
            const std::string ip   = p.value("initParam", std::string(""));
            const std::string must = p.value("mustEqual", std::string(""));
            const std::string dflt = p.value("defaultWhenAbsent", std::string(""));
            std::string effective = dflt; bool foundParam = false;
            for (auto* sv : servlets) {
                auto* sc = child_by_tag(sv, "servlet-class");
                if (!sc || trim(sc->text) != cls) continue;
                for (auto& c : sv->children) {
                    if (c->tag != "init-param") continue;
                    auto* pn = child_by_tag(c.get(), "param-name");
                    auto* pv = child_by_tag(c.get(), "param-value");
                    if (pn && pv && trim(pn->text) == ip) { effective = trim(pv->text); foundParam = true; }
                }
            }
            bool ok = (effective == must);
            return OpResult::ok_({ {"verdict", ok ? "pass" : "fail"},
                {"detail", cls + " " + ip + "=" + effective + (foundParam ? "" : "(기본)")} });
        }

        // ── rule: error_codes ──
        if (rule == "error_codes") {
            std::vector<const XmlNode*> ec; find_by_tag(&root, "error-code", ec);
            std::vector<std::string> present;
            for (auto* e : ec) present.push_back(trim(e->text));
            json missing = json::array();
            for (auto& code : p.value("codes", json::array()))
                if (std::find(present.begin(), present.end(), code.get<std::string>()) == present.end())
                    missing.push_back(code);
            bool ok = missing.empty();
            return OpResult::ok_({ {"verdict", ok ? "pass" : "fail"},
                {"detail", ok ? "필수 error-code 모두 정의됨" : ("누락 error-code: " + missing.dump())} });
        }

        // ── observe / attr_*_match ──
        const std::string elem = p.value("element", std::string(""));
        std::vector<const XmlNode*> elems;
        if (!elem.empty()) find_by_tag(&root, elem, elems);

        if (rule == "attr_none_match" || rule == "attr_all_match") {
            const std::string attr = p.value("attr", std::string(""));
            std::regex re;
            try { re = compile_regex(p.value("regex", std::string(""))); }
            catch (...) { return OpResult::err("잘못된 정규식"); }
            json hits = json::array(); int total = 0;
            for (auto* e : elems) {
                auto it = e->attrs.find(attr);
                if (it == e->attrs.end()) continue;
                ++total;
                if (std::regex_search(it->second, re)) hits.push_back(it->second);
            }
            bool ok = (rule == "attr_none_match") ? hits.empty()
                                                  : (total > 0 && (int)hits.size() == total);
            return OpResult::ok_({ {"verdict", ok ? "pass" : "fail"},
                {"detail", (rule == "attr_none_match")
                    ? (ok ? "매칭 요소 없음" : ("매칭 " + attr + ": " + hits.dump()))
                    : (ok ? "모든 요소 매칭" : ("비매칭 존재(" + attr + ")"))} });
        }

        // observe
        json arr = json::array();
        for (auto* e : elems) {
            json a = json::object();
            for (auto& [k, v] : e->attrs) a[k] = v;
            arr.push_back({ {"attrs", a}, {"text", e->text} });
        }
        return OpResult::ok_({ {"elements", arr}, {"count", (int)arr.size()} });
    }
};
REGISTER_OP(ConfigXmlQueryOp)

} // namespace wu
