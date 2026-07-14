# wu-agent

C++17 온디바이스 보안 에이전트. WAS/WEB 점검을 op·Check Pack으로 수행하고, 로컬 GGUF(LLM)로 자연어/ReAct를 지원한다.

설계 원본: [`../plan/final_plan.md`](../plan/final_plan.md)

## 빌드

요구: CMake ≥ 3.14, C++17 컴파일러. 첫 빌드는 `third_party/llama.cpp` 포함으로 수 분 소요.

```bash
cd agent
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
```

벤더링되지 않으면 LLM stub로 빌드된다. 목록: [`third_party/README.md`](third_party/README.md)

## 설정

[`config/reactor.json`](config/reactor.json)

| 키 | 의미 |
|----|------|
| `llm.mode` | `local` \| `off` \| `remote`(미구현) |
| `llm.modelPath` | GGUF 경로. 상대경로는 **agent 루트** 기준 (`../gguf/...`) |
| `policy.enableLlmReasoning` | explain/ask/agent/자연어 LLM 게이트 |
| `packsDir` | Check Pack JSON 디렉터리 |
| `policy.readAllowlist` | PathGuard 허용 루트 |

`-c` / `--config`로 설정 파일 지정. cwd가 `agent/`가 아니어도 실행파일 상위에서 `config/reactor.json`을 탐색한다.

## 실행

`agent/`에서 실행하는 것을 권장.

```bash
# 대화형 (시작 시 모델 선로드)
./build/wu_agent repl

# 결정적 pack
./build/wu_agent run kisa-tomcat --var was.home=$PWD/testdata/tomcat-vuln
./build/wu_agent run kisa-apache --var web.root=$PWD/testdata/apache-vuln

# op 직접
./build/wu_agent ops
./build/wu_agent op net.list_ports '{"proto":"tcp","state":"listen"}'

# LLM 단발 / ReAct
./build/wu_agent llm "안녕"
./build/wu_agent agent "리스닝 중인 TCP 포트 몇 개인지 확인해줘"

# 다른 cwd
./build/wu_agent -c /path/to/agent/config/reactor.json packs
```

결과: 콘솔 표 + `./a.json`  
llama 상세 로그: 기본 숨김. 필요 시 `WU_LLAMA_VERBOSE=1`.

### REPL 명령

| 명령 | 설명 |
|------|------|
| `list packs` / `ops` | pack·op 목록 |
| `run <packId> [--var k=v]` | pack 실행 |
| `show <checkId>` | 직전 결과 항목 |
| `explain` / `ask <질문>` | AI 요약·질의 |
| `agent <목표>` | 다단계 ReAct |
| 자연어 | pack 라우팅 또는 일반 대화 |
| `quit` | 종료 |

## 구조

```
src/main.cpp          CLI / REPL / config 해석
src/op/               op 프레임워크 + native op
src/core/             pack 인터프리터, ReAct
src/llm/              LocalLlamaEngine
src/platform/         OS별 net/proc
config/reactor.json   설정
config/packs/         kisa-tomcat, kisa-apache
third_party/          벤더링 (README.md)
../gguf/              모델 (git 미포함)
```

## 로드맵 (요약)

| Phase | 상태 | 내용 |
|-------|------|------|
| 0 | ✅ | 스캐폴드·CMake |
| 3 | ✅ | op 프레임워크·native op |
| S | ✅ | pack 인터프리터·REPL·a.json |
| 1 | ✅ | llama.cpp 벤더링·LocalLlamaEngine |
| 4 | ✅ | 라우팅·요약·ask·ReAct |
| 2/5 | ⬜ | remote LLM·서버 API |
| 6 | ⬜ | 패키징·서비스 |

## 모델

활성 기본: Qwen2.5-7B-Instruct Q4_K_M (2분할, 첫 shard만 `modelPath`에 지정).  
다운로드: [`../gguf/README.md`](../gguf/README.md)
