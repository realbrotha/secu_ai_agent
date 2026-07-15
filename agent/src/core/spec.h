#pragma once
// Check Pack / Check / Step 데이터 타입 (final_plan.md §8.1, §8.5)
#include "op/op.h"
#include <string>
#include <vector>

namespace wu {

struct StepSpec {
    std::string op;
    json        params = json::object();
    std::string as;                 // 결과를 이 이름으로 바인딩 (선택)
};

struct CheckSpec {
    std::string checkId, title, category, severity, method, remediation;
    json        reference = json::object();  // {kisa, cis}
    std::vector<StepSpec> steps;
    // passRule:
    //   "all_asserts_passed" (기본) — assert.* 가 모두 통과해야 pass (assert 없으면 pass)
    //   "op_verdict"         — 마지막 step op 이 반환한 data.{verdict:pass|fail|na, detail?, severity?}
    //                          를 체크 상태로 승격. 복잡한 판정 로직을 네이티브 op 안에 두는 하이브리드용.
    std::string passRule = "all_asserts_passed";
};

struct PackSpec {
    std::string packId, title, version;
    json        target = json::object();     // {kind, product}
    json        vars   = json::object();     // 초기 바인딩 (nested, 예 {"was":{"home":"..."}})
    std::vector<std::string> requiresPaths;  // 대상 존재 전제(예: "$was.home/conf"). 부재 시 전체 na
    std::vector<CheckSpec> checks;
};

// 단일 점검 결과
struct CheckResult {
    std::string checkId, title, category, severity;
    std::string status;             // pass | fail | na | error
    std::string detail;             // error/na 사유 또는 요약
    json        reference = json::object();
    std::string remediation;
    json        evidence = json::array();    // [{op, params, status, data}]
};

} // namespace wu
