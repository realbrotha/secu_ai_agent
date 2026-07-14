# 벤더링 라이브러리 (소스/정적 커밋 — submodule 아님)

업스트림 삭제/force-push/드리프트에 휘둘리지 않도록 코드·라이브러리를 저장소에 물리적으로 넣는다.
업데이트는 "다시 받아 교체 후 커밋"으로만 (자동 드리프트 없음). 설계: ../../plan/final_plan.md §2.

| lib | 버전/태그 | 출처 URL | 획득일 | sha256 | 도입 phase |
|-----|-----------|----------|--------|--------|-----------|
| llama.cpp / ggml | commit 2969d6d (ggml 0.16.0) | https://github.com/ggml-org/llama.cpp | 2026-07-14 | (shallow clone) | Phase 1 ✅ |
| nlohmann/json    | v3.11.3        | https://github.com/nlohmann/json      | 2026-07-14 | - | Phase 3 ✅ |
| replxx           | release-0.0.4 (2b24846) | https://github.com/AmokHuginnsson/replxx | 2026-07-15 | - | REPL line edit ✅ |
| curl             | (미정)         | https://github.com/curl/curl          | - | - | Phase 2 |
| openssl          | (미정)         | https://github.com/openssl/openssl    | - | - | Phase 3 (tls.inspect/sha256) |

## 절차
1. `git clone --branch <tag> <url>`
2. `rm -rf .git`
3. `third_party/<lib>/` 로 복사
4. 이 표에 버전/URL/날짜/sha256 기록 후 커밋

## 라이선스 집계 (배포 전 필수)
- llama.cpp: MIT / ggml: MIT
- replxx: BSD-3-Clause
- curl: curl License / openssl: Apache-2.0
- nlohmann/json: MIT
- 모델(gguf): 별도 — Qwen2.5 0.5B/1.5B/7B/14B = Apache-2.0 (3B/72B 비상업 회피). ../../plan/final_plan.md §11.1
  - 현재 활성: Qwen2.5-7B-Instruct Q4_K_M (2분할, 첫 shard 지정→자동로드). config/reactor.json llm.modelPath. 1.5B 는 fallback 로 보관.
