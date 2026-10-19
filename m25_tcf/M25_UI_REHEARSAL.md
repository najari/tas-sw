# M25 UI 종합 리허설 (VS Code 실연)

작성: 2026-10-19 · 상태: 완료 — 8개 항목 전부 UI에서 실증

## 환경
- VS Code (pid 15364) — `C:\work\aurix_tcf_test` 워크스페이스, ShieldBuddy TC375 launch 구성
- 확장이 spawn한 에이전트: `tcf-agent.exe -s TCP:127.0.0.1:1534 -p "DAS JTAG AURDUINO 46YPEADW" -K C:\work\aurix_tcf_test -f`
  → **원본 런처 호환 플래그(-p/-K/-f) 그대로** 사용됨 (우리 `-f` 처리 라이브 확인)
- GUI 구동: cua-driver 0.32.0 (foreground delivery — Chromium은 background 입력 거부)

## 실증 항목 (모두 실측)

| # | 동작 | UI 경로 | 결과 |
|---|---|---|---|
| 1 | 세션 시작 | F5 | 에이전트 spawn ✓, UI "Core 0: CPU TRICORE 1.6E — **PAUSED ON ENTRY**" ✓ |
| 2 | Step Over | F10 | PC 0x8003a146 → 0x8003a148 (정확히 1명령) ✓ |
| 3 | Step In | F11 | PC 0x8003a148 → 0x8003a15c (분기 1명령 추종) ✓ |
| 4 | Step Out | Shift+F11 | PC 0x8003a15c → **0x8003a264** (호출자 — CSA 스택 크롤 기반) ✓ |
| 5 | Continue | F5 | PC 변화 (0x8003a142 → 0x8003a15e) = 실행 ✓ |
| 6 | Pause | F6 | PC 고정 ✓ |
| 7 | **Target Reset** | Ctrl+Shift+P → "AURIX TCF: Reset" | **PC = 0xA0000020 (리셋 벡터 정지)** ✓ — 우리 ContextReset 서비스가 UI에서 구동 |
| 8 | 리셋 후 실행 복원 | F5 | PC 변화 = 재부팅·실행 ✓ (자연 상태) |

- PC 검증은 동일 에이전트(127.0.0.1:1534)에 병행 접속한 TCF 클라이언트로 독립 확인
- 7번: 확장 `aurix-tcf.resetTarget` → `customRequest('aurix-tcf/reset', {suspend:true})`
  → `tcfClient.reset(ctx,'reset',true)` → 우리 `aurix_reset` (DC 0x0003) ✓

## 증거 (스크린샷)
`ui_rehearsal/`:
- `01_baseline.png` — 세션 시작 전
- `03_session_started.png` — 세션 라이브(코어 PAUSED ON ENTRY)
- `06_palette.png` — "AURIX TCF: Reset" 커맨드 팔레트
- `07_final_running.png` — 최종(프로그램 실행 중)

## 결론

원본 `aurix-debugger.exe`의 VS Code 디버깅 경험(시작·정지·스텝 3종·재개·일시정지·리셋)이
**우리 TCF 에이전트로 UI 레벨에서 완전히 재현**됨을 확인. 확장은 수정 없이 원본용 그대로 사용.
