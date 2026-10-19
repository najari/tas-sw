# M25-3 부록 3: Step In / Step Out / Continue (RunControl 모드)

작성: 2026-10-19 · 상태: 완료 (5개 모드 전부 TCF 실측 통과)

## 1. 배경

VS Code 디버그 툴바/메뉴의 동작은 DAP → 확장 → TCF RunControl `resume(mode)`로
전달된다. 확장(out/tcf/runcontrol.js)이 쓰는 모드 값:

| DAP 동작 | TCF 모드 | 값 |
|---|---|---|
| Continue (F5) | normalExecution | 0 |
| Next (F10, 명령 단위) | nextInstruction | 1 |
| Next (F10, 기본) | nextLine | 3 |
| Step In (F11) | stepIn (라인) | 4 |
| Step Out (Shift+F11) | stepOut | 5 |

에이전트 쪽은 `context_resume(ctx, mode, ...)` + `context_can_resume()`로 받는다.

## 2. 핵심 구조: 프레임워크의 스텝 머신

`services/runctrl.c`의 스텝 머신이 **모드 대부분을 자체 처리**한다:

- 스택 트레이스(`get_top_frame`/`get_prev_frame`/`get_frame_info`)로 스텝 목적지 계산
- `RM_STEP_OUT` → **호출자 프레임의 PC**를 읽어 임시 브레이크포인트 생성
  (`create_step_machine_breakpoint` → 우리 `context_plant_breakpoint` = HW 트리거)
  → `RM_RESUME`으로 백엔드에 전달
- `RM_STEP_OVER`/`_LINE`/`RM_STEP_INTO_LINE` → 라인 정보(DWARF) 또는 명령 범위로
  스텝 범위 설정 → 실제 이동은 백엔드의 **`RM_STEP_INTO`(하드웨어 단일 스텝)** 반복
- ELF가 없으면(라인 정보 없음) 명령 단위 스텝으로 자동 강등

따라서 백엔드가 제공할 것은:
1. `RM_RESUME` / `RM_STEP_INTO` (단일 스텝) — **구현 완료**
2. 스택 트레이스 — **CSA 크롤(M25-3 부록 2)로 ELF 없이도 동작**
3. 브레이크포인트(HW 트리거) — M25-2 완료

`context_can_resume()`이 `RM_STEP_INTO`를 1로 반환하면 프레임워크가
`RM_STEP_OVER`/`RM_STEP_OVER_RANGE`/`RM_STEP_OUT`/`RM_STEP_INTO_LINE`/
`RM_STEP_OVER_LINE`을 자동 광고하고, 클라이언트의 요청 모드 검증
(`get_resume_modes`)도 통과한다.

## 3. 실측 (라이브 타깃, `tools/m25_3_stepmodes_test.js`)

```
PC start = 0x8003a32c
mode 2 (stepInto):  0x8003a32c -> 0x8003a32e   ✓ (한 명령)
mode 3 (nextLine):  0x8003a32e -> 0x8003a330   ✓
mode 4 (stepInLine):0x8003a330 -> 0x8003a332   ✓
mode 1 (stepOver):  0x8003a332 -> 0x8003a334   ✓
mode 5 (stepOut):   0x8003a334 -> 0x8003a264   ✓ (호출자 PC — CSA 추적)
mode 0 (continue):  실행 확인 후 suspend       ✓
이벤트: contextResumed / contextSuspended "Step" / "Breakpoint" / "Suspended"
```

- **stepOut**: 스텝 머신이 스택 트레이스에서 호출자 PC(0x8003a264)를 얻어
  임시 HW BP를 심고 재개 → 정확히 그 주소에서 정지 ✓
  (CSA 크롤 이전에는 스택 트레이스가 1프레임뿐이라 "Cannot step out" 실패)
- ELF 매핑 시에는 라인 단위(F10/F11)로, 미매핑 시에는 명령 단위로 동작

## 4. 산출물

- 백엔드 변경 없음(기존 `RM_RESUME`/`RM_STEP_INTO` + CSA 크롤 + HW BP 조합으로 충족)
- `tools/m25_3_stepmodes_test.js` — 5개 모드 일괄 실측 도구
- 부수 수정: 디스어셈블러에 `calli`(0x2D) 추가 (스텝 대상 코드 가독성)
