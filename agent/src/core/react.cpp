#include "core/react.h"
#include "op/registry.h"
#include "op/runner.h"
#include "util/log.h"

namespace wu {

// react 에 노출할 op 목록(읽기전용 관찰 op만; assert.* 은 Tier2 내부이므로 제외)
static std::vector<IOp*> react_ops() {
    std::vector<IOp*> v;
    for (auto* op : OpRegistry::instance().all()) {
        const auto& d = op->descriptor();
        if (d.name.rfind("assert.", 0) == 0) continue;
        if (d.safety != Safety::read_only_local && d.safety != Safety::read_only_loopback) continue;
        v.push_back(op);
    }
    return v;
}

static std::string op_catalog(const std::vector<IOp*>& ops) {
    std::string s;
    for (auto* op : ops) {
        const auto& d = op->descriptor();
        s += "- " + d.name + "(";
        for (size_t i = 0; i < d.params.size(); ++i) s += (i ? "," : "") + d.params[i].name;
        s += "): " + d.summary + "\n";
    }
    return s;
}

// tool 값을 실제 op 이름 집합으로 제약하는 GBNF 동적 생성 (빈/무효 tool 방지)
static std::string build_grammar(const std::vector<IOp*>& ops) {
    std::string toolname;
    for (size_t i = 0; i < ops.size(); ++i)
        toolname += (i ? " | " : "") + std::string("\"\\\"") + ops[i]->descriptor().name + "\\\"\"";
    return std::string(
        "root ::= \"{\" ws \"\\\"thought\\\"\" ws \":\" ws string ws \",\" ws action ws \"}\"\n"
        "action ::= tool | final\n"
        "tool ::= \"\\\"tool\\\"\" ws \":\" ws toolname ws \",\" ws \"\\\"args\\\"\" ws \":\" ws object\n"
        "toolname ::= ") + toolname + "\n"
        "final ::= \"\\\"final\\\"\" ws \":\" ws string\n"
        "object ::= \"{\" ws ( member ( ws \",\" ws member )* )? ws \"}\"\n"
        "member ::= string ws \":\" ws value\n"
        "value ::= string | number | object | array | \"true\" | \"false\" | \"null\"\n"
        "array ::= \"[\" ws ( value ( ws \",\" ws value )* )? ws \"]\"\n"
        "string ::= \"\\\"\" ( [^\"\\\\] | \"\\\\\" [\"\\\\/bfnrt] )* \"\\\"\"\n"
        "number ::= \"-\"? [0-9]+ ( \".\" [0-9]+ )?\n"
        "ws ::= [ \\t\\n]*\n";
}

static std::string truncate(const std::string& s, size_t n) {
    return s.size() > n ? s.substr(0, n) + "...(생략)" : s;
}

std::string run_react(ILlmEngine& llm, OpContext ctx, const std::string& goal, int max_steps) {
    const std::vector<IOp*> ops = react_ops();
    const std::string grammar = build_grammar(ops);
    const std::string sys =
        "너는 보안 점검 에이전트다. 아래 도구(op)만 사용한다. 매 단계 반드시 JSON 하나만 출력한다.\n"
        "- 도구 사용: {\"thought\":\"간단한 이유\",\"tool\":\"op이름\",\"args\":{파라미터}}\n"
        "- 최종 답변: {\"thought\":\"이유\",\"final\":\"사용자에게 줄 한국어 답변\"}\n"
        "규칙: 목록에 있는 도구만 사용. args 값은 문자열/숫자로. 목적을 달성했으면 즉시 final 로 답하라.\n"
        "final 의 답변은 반드시 한국어로만 작성한다(중국어·일본어·한자 금지).\n"
        "예) 목표: 리스닝 포트 확인 → {\"thought\":\"포트를 열거\",\"tool\":\"net.list_ports\",\"args\":{\"proto\":\"tcp\",\"state\":\"listen\"}}\n"
        "그 다음 관찰을 받으면 → {\"thought\":\"결과 정리\",\"final\":\"리스닝 TCP 포트는 N개입니다\"}\n"
        "[사용 가능한 도구]\n" + op_catalog(ops);

    std::string scratch;  // 누적 관찰(생각-행동-관찰 궤적)
    for (int step = 0; step < max_steps; ++step) {
        std::vector<ChatMsg> msgs = {
            { "system", sys },
            { "user", "목표: " + goal + (scratch.empty() ? "" : ("\n[지금까지]\n" + scratch)) +
                      "\n\n다음 JSON 한 개만 출력:" },
        };
        std::string resp = llm.chat(msgs, 256, 0.0f, grammar);

        json j;
        try { j = json::parse(resp); }
        catch (...) { return "(형식 파싱 실패) " + resp; }

        if (j.contains("final")) return j["final"].get<std::string>();

        if (j.contains("tool")) {
            const std::string tool = j.value("tool", "");
            json args = j.value("args", json::object());
            wu::log::info("  [react] " + tool + " " + args.dump());
            IOp* op = OpRegistry::instance().find(tool);
            std::string obs;
            if (!op) obs = "오류: 미지원 도구 " + tool;
            else {
                OpResult r = run_op_guarded(op, args, ctx);
                obs = (r.status == OpStatus::error) ? ("오류: " + r.error) : truncate(r.data.dump(), 500);
            }
            scratch += "행동: " + tool + " " + args.dump() + "\n관찰: " + obs + "\n";
        } else {
            return "(도구/최종 없음)";
        }
    }
    return "(최대 단계 도달) 지금까지 관찰:\n" + scratch;
}

} // namespace wu
