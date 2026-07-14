# wu — AI Security Agent (ReAct + On-device LLM)

모노레포:

| 경로 | 역할 |
|------|------|
| `agent/` | C++17 온디바이스 보안 에이전트 (Reason → Act → Observe) |
| `server/` | 신규 API 서버 (task/result, 모델·카탈로그 배포, 학습 파이프라인) |
| `plan/` | 설계 플랜 백업 |

자세한 설계는 [`plan/ai-security-agent.plan.md`](plan/ai-security-agent.plan.md) 참고.

## Quick start (agent Phase 0)

```bash
cd agent
cmake -S . -B build
cmake --build build
./build/wu_agent --config config/reactor.json
```
