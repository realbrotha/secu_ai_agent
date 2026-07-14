// op: proc.list — 프로세스 열거 (셸 미사용).
#include "op/op.h"
#include "op/registry.h"
#include "platform/proc.h"
#include <algorithm>

namespace wu {

class ProcListOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "proc.list"; d.summary = "실행 중 프로세스를 열거한다 (name_filter 부분일치)";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = { { "name_filter", "string", false, nullptr, {}, "이름 부분일치 필터" } };
        d.returns = { {"procs","array<object>","[{pid,ppid,name,path}]"}, {"count","int",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext&) override {
        std::string filt = p.value("name_filter", std::string(""));
        std::transform(filt.begin(), filt.end(), filt.begin(), ::tolower);
        json arr = json::array();
        for (const auto& e : platform::list_procs()) {
            if (!filt.empty()) {
                std::string n = e.name; std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n.find(filt) == std::string::npos) continue;
            }
            arr.push_back({ {"pid", e.pid}, {"ppid", e.ppid}, {"name", e.name}, {"path", e.path} });
        }
        return OpResult::ok_({ {"procs", arr}, {"count", (int)arr.size()} });
    }
};
REGISTER_OP(ProcListOp)

} // namespace wu
