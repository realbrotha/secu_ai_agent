// op: sys.host_info — OS/arch/hostname 기본 정보 (cross-platform)
#include "op/op.h"
#include "op/registry.h"

#if defined(_WIN32)
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <sys/utsname.h>
#endif

namespace wu {

class SysHostInfoOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name    = "sys.host_info";
        d.summary = "호스트의 OS/아키텍처/hostname 등 기본 정보를 반환한다";
        d.safety  = Safety::read_only_local;
        d.os      = {"linux", "windows", "macos"};
        d.impl    = "native";
        d.returns = { {"os","string",""}, {"arch","string",""}, {"hostname","string",""}, {"kernel","string",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json&, OpContext& ctx) override {
        json out;
#if defined(_WIN32)
        char host[256] = {0};
        DWORD n = sizeof(host);
        GetComputerNameA(host, &n);
        out = { {"os","windows"}, {"arch","x86_64"}, {"hostname", host}, {"kernel","windows"} };
#else
        struct utsname u{};
        char host[256] = {0};
        gethostname(host, sizeof(host));
        if (uname(&u) == 0) {
            out = {
                {"os", ctx.host.os.empty() ? std::string(u.sysname) : ctx.host.os},
                {"arch", u.machine},
                {"hostname", host[0] ? host : u.nodename},
                {"kernel", std::string(u.sysname) + " " + u.release},
            };
        } else {
            out = { {"os", ctx.host.os}, {"arch",""}, {"hostname", host}, {"kernel",""} };
        }
#endif
        return OpResult::ok_(out);
    }
};

REGISTER_OP(SysHostInfoOp)

} // namespace wu
