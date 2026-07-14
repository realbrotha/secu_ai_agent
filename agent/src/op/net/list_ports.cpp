// op: net.list_ports — 로컬 소켓/리스너 열거 (netstat -ant 대응, 셸 미사용)
#include "op/op.h"
#include "op/registry.h"
#include "platform/net.h"

namespace wu {

class NetListPortsOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name    = "net.list_ports";
        d.summary = "로컬 호스트의 TCP/UDP 소켓과 소유 프로세스를 열거한다 (netstat -ant 대응, 셸 미사용)";
        d.safety  = Safety::read_only_local;
        d.os      = {"linux", "windows", "macos"};
        d.impl    = "native";
        d.params  = {
            { "proto", "enum", false, "all", { {}, {}, {"tcp","udp","all"}, "" }, "프로토콜 필터", 0 },
            { "state", "enum", false, "all", { {}, {}, {"listen","established","all"}, "" }, "상태 필터", 1 },
        };
        d.returns = {
            { "ports", "array<object>", "[{proto,localAddr,localPort,state,pid,process}]" },
        };
        d.examples = { json{{"params", {{"proto","tcp"},{"state","listen"}}}, {"note","리스닝 TCP만"}} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext&) override {
        const std::string proto = p.value("proto", "all");
        const std::string state = p.value("state", "all");
        auto ports = platform::list_ports(proto, state);

        json arr = json::array();
        for (const auto& e : ports) {
            arr.push_back({
                {"proto", e.proto}, {"localAddr", e.localAddr}, {"localPort", e.localPort},
                {"state", e.state}, {"pid", e.pid}, {"process", e.process},
            });
        }
        return OpResult::ok_({ {"ports", arr}, {"count", (int)arr.size()} });
    }
};

REGISTER_OP(NetListPortsOp)

} // namespace wu
