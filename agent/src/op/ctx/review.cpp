// op: ctx.review — 로컬 read-only 로 판정 불가한 항목을 명시적으로 na(확인필요) 처리.
//   패치 최신성/조직적 통제/자격증명 필요 점검 등. na 사유(detail)를 그대로 노출.
#include "op/op.h"
#include "op/registry.h"

namespace wu {

class CtxReviewOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "ctx.review"; d.summary = "로컬 판정 불가 항목을 na(확인필요)로 표기";
        d.safety = Safety::read_only_local; d.os = {"linux","windows","macos"}; d.impl = "native";
        d.params = { { "detail", "string", false, "로컬 read-only 로 판정 불가(수동 확인 필요)", {}, "na 사유" } };
        d.returns = {};
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }
    OpResult run(const json& p, OpContext&) override {
        return OpResult::na(p.value("detail", std::string("로컬 read-only 로 판정 불가(수동 확인 필요)")));
    }
};
REGISTER_OP(CtxReviewOp)

} // namespace wu
