# secu_ai_agent (wu)

온디바이스 AI 보안 점검 에이전트. 자연어 → Check Pack / ReAct(op) 실행 → 콘솔·`a.json` 결과.

저장소: [realbrotha/secu_ai_agent](https://github.com/realbrotha/secu_ai_agent)

## 실행 화면

![REPL에서 인사 후 Tomcat 점검 pack 실행](sample.png)

`./wu_agent repl` → 모델 선로드 → 자연어로 `kisa-tomcat` 실행 (대상 경로 없으면 NA).

## 구성

| 경로 | 설명 |
|------|------|
| [`agent/`](agent/README.md) | C++17 에이전트 (빌드·실행·CLI) |
| [`agent/third_party/`](agent/third_party/README.md) | 벤더링 라이브러리 (llama.cpp, replxx, json) |
| [`gguf/`](gguf/README.md) | GGUF 모델 (git 제외, 로컬 다운로드) |
| [`plan/`](plan/final_plan.md) | 설계·로드맵·검증 로그 |

## Quick start

```bash
# 1) 모델 (예: 7B Q4_K_M, 2분할)
cd gguf
for i in 00001 00002; do
  curl -L -C - -o "qwen2.5-7b-instruct-q4_k_m-${i}-of-00002.gguf" \
    "https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/main/qwen2.5-7b-instruct-q4_k_m-${i}-of-00002.gguf"
done
cd ../agent

# 2) 빌드
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 3) 실행 (cwd는 agent/ 권장 — config 상대경로 / -c 지원)
./build/wu_agent repl
./build/wu_agent run kisa-tomcat --var was.home=$PWD/testdata/tomcat-vuln
./build/wu_agent llm "안녕"
```

자세한 CLI·설정은 [`agent/README.md`](agent/README.md).

## 라이선스 요약

- 에이전트 코드: 저장소 정책에 따름
- llama.cpp / ggml: MIT · replxx: BSD-3-Clause · nlohmann/json: MIT
- Qwen2.5 GGUF: **0.5B/1.5B/7B/14B = Apache-2.0** (3B·72B 비상업 → 미사용)

상세 벤더 표: [`agent/third_party/README.md`](agent/third_party/README.md)
