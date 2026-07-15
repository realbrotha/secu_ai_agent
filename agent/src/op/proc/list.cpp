// op: proc.list — 프로세스 열거 (셸 미사용). 이름/유저/인자 필터 + uid/user/args 반환.
#include "op/op.h"
#include "op/registry.h"
#include "platform/proc.h"
#include <algorithm>

namespace wu {

static std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

class ProcListOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "proc.list"; d.summary = "실행 중 프로세스 열거 (name/user/args 부분일치 필터)";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = {
            { "name_filter", "string", false, nullptr, {}, "이름(comm) 부분일치" },
            { "user_filter", "string", false, nullptr, {}, "실행 유저 정확일치" },
            { "args_filter", "string", false, nullptr, {}, "커맨드라인 인자 부분일치" },
        };
        d.returns = { {"procs","array<object>","[{pid,ppid,name,path,uid,user,args}]"}, {"count","int",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext&) override {
        const std::string nf = lower(p.value("name_filter", std::string("")));
        const std::string uf = p.value("user_filter", std::string(""));
        const std::string af = lower(p.value("args_filter", std::string("")));
        json arr = json::array();
        for (const auto& e : platform::list_procs()) {
            if (!nf.empty() && lower(e.name).find(nf) == std::string::npos) continue;
            if (!uf.empty() && e.user != uf) continue;
            if (!af.empty() && lower(e.args).find(af) == std::string::npos) continue;
            json o = { {"pid", e.pid}, {"ppid", e.ppid}, {"name", e.name}, {"path", e.path} };
            if (e.uid >= 0)       o["uid"]  = e.uid;
            if (!e.user.empty())  o["user"] = e.user;
            if (!e.args.empty())  o["args"] = e.args;
            arr.push_back(std::move(o));
        }
        return OpResult::ok_({ {"procs", arr}, {"count", (int)arr.size()} });
    }
};
REGISTER_OP(ProcListOp)

} // namespace wu
