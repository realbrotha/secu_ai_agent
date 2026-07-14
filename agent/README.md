# wu-agent

AI 보안 점검 에이전트 (C++17). WAS/WEB 점검을 op 조합으로 수행하는 온디바이스 에이전트.
전체 설계: [../plan/final_plan.md](../plan/final_plan.md)

## 빌드
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
```

## 실행 (Phase 0)
```bash
# wu/agent 디렉터리에서 실행 (config/gguf 상대경로 기준)
./build/wu_agent
./build/wu_agent -c config/reactor.json
```
Phase 0 은 config/gguf 존재 체크 + 로그만 출력한다 (외부 lib 링크 없음).

## 로드맵 (final_plan.md §13)
- **Phase 0** ✅ 스캐폴드 + 빌드 (현재)
- Phase 3  op 프레임워크(IOp/registry/runner) + native op (net/proc/fs/config/sys/assert)
- **Phase S**  ★ standalone: op 인터프리터 + LocalPackSource + CLI REPL + ConsoleSink + a.json
             → `run kisa-tomcat` 가 서버·LLM 없이 동작
- Phase 1  llm_engine (local/remote)
- Phase 4  react_engine (자연어 명령 + verdict)
- Phase 2/5  api_client / 서버 연동(수집·배포)
- Phase 6  패키징·서비스

## 구조 (계획)
```
src/main.cpp            진입점 (현재: 스캐폴드)
src/util/log.h          로거
src/op/                 op 프레임워크 + native op (Phase 3)
src/core/               AgentCore (Phase S)
src/source/ src/sink/   Task Source / Result Sink
config/reactor.json     설정
config/packs/           로컬 Check Pack (kisa-tomcat.json 등)
third_party/            벤더링 (VENDOR.md)
../gguf/                GGUF 모델
```
