# M25-2 — 브레이크포인트(HW/SW) + 비동기 정지 감지 (2026-10-18)

**결과: 우리 TCF 에이전트가 브레이크포인트를 완전 지원한다** — VS Code UI에서
"Paused on breakpoint" / "Paused on step" 실측! 원본과 동일하게 OCDS 트리거
유닛(TR0-7) 기반이며, 플래시 타깃에서도 브레이크포인트가 동작한다.

## 0. 핵심 발견 — 프레임워크의 BP 심기 순서

`breakpoints.c`의 `plant_instruction()`은 **항상 `context_plant_breakpoint()`
(하드웨어)를 먼저 시도**하고, ERR_UNSUPPORTED일 때만 SW(`debug` 명령 쓰기)로
폴백한다. 즉 **백엔드가 HW BP를 지원하면 "Software" 타입 BP도 HW로 심긴다**
(원본도 동일 — 플래시 코드에서는 이것이 유일한 방법!).

## 1. 구현 (context-aurix.c 확장)

### (a) 코어 상태 모니터 (원본의 monitorCoreState/모니터 스레드 대응)
- `post_event_with_delay` 100ms 주기 폴 (디스패치 스레드) — 각 코어 DBGSR 판독,
  running→halted 전이 감지 → `aurix_report_stop(ctx, core, async=1)`.
- 비동기 정지는 `ctx->pending_intercept = 1`을 설정해야 프레임워크가
  "contextSuspended" 이벤트를 클라이언트로 보낸다 (win32 백엔드와 동일 패턴).

### (b) 정지 사유 판독 (파리티!)
- `TRIG_ACC`(0xFD30, 읽으면 클리어) 판독 → 히트한 트리거 유닛의 BP들을
  `ctx->stopped_by_cb`(NULL 종료 배열)로 정확 매핑 (x86 DR6 패턴).
- 트리거 히트가 없으면 `is_breakpoint_address(ctx, pc/pc-2)`로 SW BP 판정.
- → 클라이언트 이벤트: `contextSuspended ... "Breakpoint" | {"BPs":["swbp1"]}` ✓

### (c) 하드웨어 브레이크포인트 (TR0-7, M18 정본)
- `context_plant_breakpoint`: 유닛 할당 → `TRxADR`←주소, `TRxEVT`←
  IP=`0x0000101A` / write=`0x0800001A` / read=`0x1000001A` / rw=`0x1800001A`
  (access_types의 VIRTUAL|SOFTWARE 비트는 마스킹).
- `context_unplant_breakpoint`: TRxEVT←0 + 유닛 해제.
- `context_get_supported_bp_access_types`: INSTRUCTION|DATA_READ|DATA_WRITE|SOFTWARE.
- `context_get_breakpoint_capabilities`: `{"BreakpointType":"Hardware"}` (원본 파리티).

### (d) BP 위에서 재개 (step-over) + 트리거 재장전
- **문제**: PC 트리거는 PC==주소일 때 발화 → BP 위치에서 그냥 run하면 즉시 재정지.
- **해결 1**: `context_continue`가 PC==IP BP를 감지하면 **먼저 single step**으로
  넘긴 뒤 run (PC가 이동해 재발화 없음).
- **해결 2**: 참조 step 시퀀스는 트리거 윈도를 비활성화(디버그 초기화)하므로,
  step 후 **`aurix_rearm_triggers()`로 장전된 BP를 재기록** — 원본 클라이언트가
  매 Run 트리거를 재다운로드하던 것과 달리, 우리는 BP가 지속된다(문서화된 개선).
- **해결 3**: run 직후 트리거가 잡은 경우(rc=1) started 이벤트로 뷰를 정합시킨 뒤
  비동기 정지 보고(스테일 `stopped` 플래그 회피).

## 2. 검증 (3단계)

### (a) TCF 스모크 (`m25_2_evidence/m25_2_bp_test.js` + `aurix_m252.log`)
```
SW BP (0x8003a32c): contextSuspended "Breakpoint" {"BPs":["swbp1"]}, PC=0x8003a32c (delta=0) ✓
HW BP (0x8003a330): contextSuspended "Breakpoint" {"BPs":["hwb1"]},  PC=0x8003a330 (delta=0) ✓
Breakpoints/status: HitCount 1 ✓
로그: "hardware breakpoint 0x... planted in TR0" / "stopped by hardware breakpoint (TRIG_ACC=0x1)" ✓
```
- 정지 PC가 BP 주소와 **정확히 일치** (TriCore PC 트리거 의미론 = M18).

### (b) VS Code UI (`m25_2_evidence/vscode_bp3.json`, `vscode_step1.json`)
- `Thread Core 0: CPU0 TriCore 1.6E **Paused on breakpoint**` ✓
- F11 Step Into → `**Paused on step**`, getState: `pc=0x8003A336 (BP+2=1명령), reason="Step"` ✓
- `[dap] StackTraceResponse threadId=1 ctx=core0 frames=1` ✓

### (c) 상태 복원
- BP 제거(getIDs=[]) 후 3코어 정지(Debug) — 입력 상태 복원 ✓

## 3. 잔여 (M25-2b)

| 항목 | 비고 |
|---|---|
| memory map ("Code and Data", M8) | 확장은 클라이언트 측 맵을 쓰므로 현재 빈 맵으로 충분 |
| canonical SPR 로컬→전역 (M20) | SPR-local 주소 기반 BP/스택용 |
| BP 범위(length>1) — 유닛 페어(TR0ADR/TR1ADR) | 미구현(ERR_UNSUPPORTED) |
| 데이터 워치포인트 UI 검증 | 백엔드는 지원(READ/WRITE/RW EVT) — 클라이언트 검증 필요 |

## 4. 산출물

- 에이전트: `C:\work\tcf_agent\agent\build\{Debug,Release}\tcf-agent.exe`
- 증거: `C:\work\tas_server_analysis\m25_2_evidence\`
  (스모크 로그·스크립트 2종·VS Code 상태 2종)
- 소스 사본: `C:\work\tas-sw\m25_tcf\agent\system\aurix\tcf\context-aurix.c` (갱신)
