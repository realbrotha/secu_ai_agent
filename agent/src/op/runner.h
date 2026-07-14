#pragma once
// op 러너 — 파라미터 검증·read-only 강제·실행·evidence (final_plan.md §6.8)
#include "op/op.h"

namespace wu {

// 기본값 채움 → 타입/필수/enum 검증 → safety 확인 → 실행(예외 포집).
// (하드 timeout 은 subprocess/shell op 에서 프로세스 kill 로 강제. native op 는 후속 강화 — TODO)
OpResult run_op_guarded(IOp* op, json params, OpContext& ctx);

} // namespace wu
