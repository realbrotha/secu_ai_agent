#pragma once
// 다단계 ReAct 에이전트 (final_plan.md §4, 계층2)
//   goal → (LLM이 op 선택 → 실행 → 관찰 반복) → 최종 답변
//   실행은 화이트리스트 op 만(안전 불변). 출력은 GBNF 로 JSON 강제.
#include "op/op.h"
#include "llm/llm_engine.h"
#include <string>

namespace wu {

std::string run_react(ILlmEngine& llm, OpContext ctx, const std::string& goal, int max_steps);

} // namespace wu
