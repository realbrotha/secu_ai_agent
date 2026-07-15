// op: docker.api_get — Docker Engine API 를 unix 소켓으로 직접 GET (CLI/subprocess 미사용).
//   허용 경로: /version /info /containers/json /containers/{id}/json /containers/{id}/top /networks ...
//   Windows npipe 는 v1 미지원(na). 소켓 부재/권한없음 → na(cannot_inspect).
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include "platform/netclient.h"
#include <string>

namespace wu {

namespace {
// GET 전용 read-only allowlist (POST/exec 차단). 접두 매칭.
bool allowed_path(const std::string& path) {
    static const char* prefixes[] = {
        "/version", "/info", "/containers/json", "/containers/", "/networks", "/images/json", "/volumes"
    };
    for (auto* pre : prefixes) if (path.rfind(pre, 0) == 0) {
        // /containers/{id}/... 중 exec/attach/logs 등 변형 방지: 위험 서브경로 차단
        if (path.find("/exec") != std::string::npos) return false;
        if (path.find("/attach") != std::string::npos) return false;
        return true;
    }
    return false;
}
} // namespace

class DockerApiGetOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "docker.api_get"; d.summary = "Docker Engine API 를 unix 소켓으로 GET (read-only)";
        d.safety = Safety::read_only_local; d.os = {"linux","macos"}; d.impl = "native";
        d.params = {
            { "path",   "string", true,  nullptr, {}, "API 경로(예: /containers/json?all=1)" },
            { "socket", "path",   false, "/var/run/docker.sock", {}, "docker 소켓 경로" },
            { "timeout_ms", "int", false, 3000L, {}, "타임아웃(ms)" },
        };
        d.returns = { {"status","int",""}, {"json","any","파싱된 응답 본문"}, {"count","int","배열 응답 시 길이"} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext& ctx) override {
        const std::string path = p.at("path").get<std::string>();
        if (!allowed_path(path)) return OpResult::err("허용되지 않은 docker API 경로: " + path);
        const std::string sock = p.value("socket", std::string("/var/run/docker.sock"));
        if (ctx.guard && !ctx.guard->allowed(sock)) return OpResult::err("path_guard 거부: " + sock);
        const long tmo = p.value("timeout_ms", 3000L);

        auto r = platform::http_request_unix(sock, "localhost", "GET", path, (int)tmo, 1 << 20);
        if (!r.ok) return OpResult::na("docker 소켓 접근 불가: " + r.error);   // 데몬 없음/권한없음 → na

        json parsed;
        try { parsed = json::parse(r.body); }
        catch (...) { parsed = r.body; }
        json out = { {"status", r.status}, {"json", parsed} };
        if (parsed.is_array()) out["count"] = (int)parsed.size();
        return OpResult::ok_(out);
    }
};
REGISTER_OP(DockerApiGetOp)

} // namespace wu
