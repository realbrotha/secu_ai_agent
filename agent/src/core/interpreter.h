#pragma once
// Tier2 인터프리터 — checks-as-data 실행 (final_plan.md §6.9)
#include "core/spec.h"
#include "op/op.h"

namespace wu {

// pack JSON → PackSpec 파싱 (실패 시 예외)
PackSpec parse_pack(const json& j);

class Interpreter {
public:
    explicit Interpreter(OpContext ctx) : ctx_(std::move(ctx)) {}

    // 단일 점검 실행. bindings 는 초기 변수(target 등). 복사본으로 동작.
    CheckResult run_check(const CheckSpec& check, json bindings);

    // pack 전체 실행 → a.json 형태(요약+results) 반환.
    json run_pack(const PackSpec& pack, json initial_vars);

private:
    OpContext ctx_;
};

} // namespace wu
