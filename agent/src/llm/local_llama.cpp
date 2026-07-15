// LocalLlamaEngine — llama.cpp(gguf) 온디바이스 추론 (final_plan.md Phase 1)
// WU_HAVE_LLAMA 정의 시에만 실제 구현. 아니면 make_local_llm 은 nullptr 반환.
#include "llm/llm_engine.h"

#if defined(WU_HAVE_LLAMA)
#include "llama.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <algorithm>

namespace wu {

namespace {
std::once_flag g_backend_once;

static void llama_quiet_logger(enum ggml_log_level level, const char * text, void * /*user_data*/) {
    // 기본: ERROR 만. WU_LLAMA_VERBOSE=1 이면 전부.
    const char* v = std::getenv("WU_LLAMA_VERBOSE");
    const bool verbose = v && v[0] && std::strcmp(v, "0") != 0;
    if (!verbose && level < GGML_LOG_LEVEL_ERROR) return;
    fputs(text, stderr);
    fflush(stderr);
}

void ensure_backend() {
    std::call_once(g_backend_once, []{
        llama_log_set(llama_quiet_logger, nullptr);
        llama_backend_init();
    });
}
}

class LocalLlamaEngine : public ILlmEngine {
public:
    LocalLlamaEngine(llama_model* m, llama_context* c, std::string name)
        : model_(m), ctx_(c), vocab_(llama_model_get_vocab(m)), name_(std::move(name)) {}

    ~LocalLlamaEngine() override {
        if (ctx_) llama_free(ctx_);
        if (model_) llama_model_free(model_);
    }

    std::string model_name() const override { return name_; }

    std::string chat(const std::vector<ChatMsg>& msgs, int max_tokens, float temperature,
                     const std::string& grammar) override {
        const std::string prompt = apply_template(msgs);
        return generate(prompt, max_tokens, temperature, grammar);
    }

private:
    std::string apply_template(const std::vector<ChatMsg>& msgs) {
        std::vector<llama_chat_message> lm;
        lm.reserve(msgs.size());
        for (auto& m : msgs) lm.push_back({ m.role.c_str(), m.content.c_str() });

        const char* tmpl = llama_model_chat_template(model_, nullptr);
        std::vector<char> buf(1 << 15);
        int32_t n = llama_chat_apply_template(tmpl, lm.data(), lm.size(), true, buf.data(), (int)buf.size());
        if (n < 0) {  // 모델 템플릿 미인식 → chatml 기본
            n = llama_chat_apply_template(nullptr, lm.data(), lm.size(), true, buf.data(), (int)buf.size());
        }
        if (n < 0) return manual_chatml(msgs);
        if (n > (int)buf.size()) { buf.resize(n); llama_chat_apply_template(tmpl, lm.data(), lm.size(), true, buf.data(), (int)buf.size()); }
        return std::string(buf.data(), n);
    }

    static std::string manual_chatml(const std::vector<ChatMsg>& msgs) {
        std::string s;
        for (auto& m : msgs) s += "<|im_start|>" + m.role + "\n" + m.content + "<|im_end|>\n";
        s += "<|im_start|>assistant\n";
        return s;
    }

    std::string generate(const std::string& prompt, int max_tokens, float temp, const std::string& grammar = "") {
        // 토크나이즈
        int n = -llama_tokenize(vocab_, prompt.c_str(), (int)prompt.size(), nullptr, 0, true, true);
        std::vector<llama_token> tokens(n);
        if (llama_tokenize(vocab_, prompt.c_str(), (int)prompt.size(), tokens.data(), (int)tokens.size(), true, true) < 0)
            return "(토크나이즈 실패)";

        // 컨텍스트 초과 방지: 프롬프트가 n_ctx-여유 를 넘으면 앞부분(지시문 포함)만 유지.
        const int n_ctx = (int)llama_n_ctx(ctx_);
        const int max_prompt = n_ctx - max_tokens - 8;
        if (max_prompt > 0 && (int)tokens.size() > max_prompt)
            tokens.resize(max_prompt);

        // KV 캐시 초기화 (매 호출 새 대화로 처리 — 단순/안전)
        llama_memory_clear(llama_get_memory(ctx_), true);

        // 샘플러 (grammar 있으면 먼저 추가 → 출력 포맷 강제)
        llama_sampler* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
        if (!grammar.empty())
            llama_sampler_chain_add(smpl, llama_sampler_init_grammar(vocab_, grammar.c_str(), "root"));
        // 반복 억제 (소형 모델의 degeneration/무한반복 방지)
        llama_sampler_chain_add(smpl, llama_sampler_init_penalties(/*last_n=*/256, /*repeat=*/1.3f, 0.0f, 0.0f));
        if (temp <= 0.0f) {
            llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
        } else {
            llama_sampler_chain_add(smpl, llama_sampler_init_top_k(40));
            llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.95f, 1));
            llama_sampler_chain_add(smpl, llama_sampler_init_temp(temp));
            llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        }

        // 프리필: 프롬프트를 n_batch 단위로 분할 decode (n_tokens > n_batch abort 방지).
        const int n_batch = (int)llama_n_batch(ctx_);
        std::string out;
        bool decode_ok = true;
        for (int start = 0; start < (int)tokens.size(); start += n_batch) {
            const int cnt = std::min(n_batch, (int)tokens.size() - start);
            llama_batch pb = llama_batch_get_one(tokens.data() + start, cnt);
            if (llama_decode(ctx_, pb) != 0) { decode_ok = false; break; }
        }

        // 생성 루프: 한 토큰씩.
        for (int i = 0; decode_ok && i < max_tokens; ++i) {
            llama_token tok = llama_sampler_sample(smpl, ctx_, -1);
            if (llama_vocab_is_eog(vocab_, tok)) break;
            char piece[256];
            int pn = llama_token_to_piece(vocab_, tok, piece, sizeof(piece), 0, true);
            if (pn > 0) out.append(piece, pn);
            llama_batch nb = llama_batch_get_one(&tok, 1);
            if (llama_decode(ctx_, nb) != 0) break;
        }
        llama_sampler_free(smpl);
        return out;
    }

    llama_model*       model_ = nullptr;
    llama_context*     ctx_   = nullptr;
    const llama_vocab* vocab_ = nullptr;
    std::string        name_;
};

std::unique_ptr<ILlmEngine> make_local_llm(const std::string& model_path, int n_ctx, int n_gpu_layers, std::string& err) {
    ensure_backend();

    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = n_gpu_layers;
    llama_model* model = llama_model_load_from_file(model_path.c_str(), mp);
    if (!model) { err = "모델 로드 실패: " + model_path; return nullptr; }

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = (uint32_t)n_ctx;
    llama_context* ctx = llama_init_from_model(model, cp);
    if (!ctx) { err = "컨텍스트 생성 실패"; llama_model_free(model); return nullptr; }

    std::string name = model_path.substr(model_path.find_last_of("/\\") + 1);
    return std::make_unique<LocalLlamaEngine>(model, ctx, name);
}

} // namespace wu

#else  // WU_HAVE_LLAMA 미정의 (llama 미빌드)

namespace wu {
std::unique_ptr<ILlmEngine> make_local_llm(const std::string&, int, int, std::string& err) {
    err = "이 빌드는 llama.cpp 를 포함하지 않음 (third_party/llama.cpp 벤더링 후 재빌드)";
    return nullptr;
}
}

#endif
