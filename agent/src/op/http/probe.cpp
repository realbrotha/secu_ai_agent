// op: http.probe — 루프백 HTTP 헤더/상태 점검 (native socket). https 는 v1 미지원(na).
//   nginx 2.4/2.5(Server:/Allow: 헤더) 증적용. 기본 루프백만 허용(read_only_loopback).
#include "op/op.h"
#include "op/registry.h"
#include "platform/netclient.h"
#include <string>
#include <algorithm>

namespace wu {

namespace {
struct Url { std::string scheme, host; int port = 0; std::string path; bool ok = false; };

Url parse_url(const std::string& in) {
    Url u;
    std::string s = in;
    auto sp = s.find("://");
    if (sp == std::string::npos) { u.scheme = "http"; }
    else { u.scheme = s.substr(0, sp); s = s.substr(sp + 3); }
    auto slash = s.find('/');
    std::string hostport = slash == std::string::npos ? s : s.substr(0, slash);
    u.path = slash == std::string::npos ? "/" : s.substr(slash);
    auto colon = hostport.find(':');
    if (colon == std::string::npos) { u.host = hostport; u.port = (u.scheme == "https") ? 443 : 80; }
    else { u.host = hostport.substr(0, colon); u.port = std::atoi(hostport.substr(colon + 1).c_str()); }
    u.ok = !u.host.empty() && u.port > 0;
    return u;
}

bool is_loopback(const std::string& host) {
    return host == "127.0.0.1" || host == "::1" || host == "localhost" || host == "[::1]";
}
} // namespace

class HttpProbeOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "http.probe"; d.summary = "루프백 HTTP 요청으로 상태/응답헤더 점검 (https v1 미지원)";
        d.safety = Safety::read_only_loopback; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "url",     "string", true,  nullptr, {}, "예: http://127.0.0.1/  또는 http://127.0.0.1/webdav/" },
            { "method",  "enum",   false, "GET", { {}, {}, {"GET","HEAD","OPTIONS"}, "" }, "HTTP 메서드" },
            { "timeout_ms", "int", false, 3000L, {}, "타임아웃(ms)" },
            { "allow_nonloopback", "bool", false, false, {}, "루프백 외 대상 허용(기본 금지)" },
        };
        d.returns = { {"status","int",""}, {"server","string","Server 헤더"}, {"allow","string","Allow 헤더"},
                      {"headers","object",""}, {"body","string","앞부분(capped)"} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext&) override {
        Url u = parse_url(p.at("url").get<std::string>());
        if (!u.ok) return OpResult::err("잘못된 URL");
        if (u.scheme == "https") return OpResult::na("https 는 v1 미지원(na)");
        if (u.scheme != "http")  return OpResult::err("지원하지 않는 scheme: " + u.scheme);
        if (!is_loopback(u.host) && !p.value("allow_nonloopback", false))
            return OpResult::na("루프백 외 대상은 기본 차단: " + u.host);

        const std::string method = p.value("method", std::string("GET"));
        const long tmo = p.value("timeout_ms", 3000L);
        auto r = platform::http_request_tcp(u.host, u.port, method, u.path, (int)tmo, 65536);
        if (!r.ok) return OpResult::na("프로브 실패: " + r.error);   // 서비스 미동작 → 판단불가

        json headers = json::object();
        for (auto& kv : r.headers) headers[kv.first] = kv.second;
        std::string body = r.body.size() > 2048 ? r.body.substr(0, 2048) : r.body;
        return OpResult::ok_({
            {"status", r.status},
            {"server", r.headers.count("server") ? r.headers["server"] : ""},
            {"allow",  r.headers.count("allow")  ? r.headers["allow"]  : ""},
            {"headers", headers}, {"body", body},
        });
    }
};
REGISTER_OP(HttpProbeOp)

} // namespace wu
