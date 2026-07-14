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
