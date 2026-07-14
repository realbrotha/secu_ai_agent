#pragma once
// LLM 엔진 추상화 (final_plan.md §1.3). 위치(local/remote)를 나머지 코드에서 숨긴다.
#include <string>
#include <vector>
#include <memory>

namespace wu {

struct ChatMsg { std::string role; std::string content; };  // role: system|user|assistant

struct ILlmEngine {
    virtual ~ILlmEngine() = default;
    // 대화 메시지 → assistant 응답 텍스트. grammar(GBNF) 비면 자유생성, 있으면 출력강제.
    virtual std::string chat(const std::vector<ChatMsg>& msgs, int max_tokens, float temperature,
                             const std::string& grammar = "") = 0;
    virtual std::string model_name() const = 0;
};

// 온디바이스(llama.cpp) 엔진 생성. 실패/미빌드 시 nullptr + err.
std::unique_ptr<ILlmEngine> make_local_llm(const std::string& model_path,
                                           int n_ctx, int n_gpu_layers, std::string& err);

} // namespace wu
