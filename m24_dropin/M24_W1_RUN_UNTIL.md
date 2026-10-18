# M24-W1 — `run_until` 실구현 + 원본 대조 (2026-10-18)

## 1. 구현 (mcd_tas — TAS 백엔드!)

```
mcd_run_until_f(core, global, absolute_time, run_until_time):
  [검증] → [시간 기준]:
    absolute_time=FALSE → 타깃 STM (코어별!): STM0=0xF0000010, STM1=+0x100, STM2=+0x200
                          (TC27x UM 메모리맵: STM0=0xF0000000~FF! TIM0 @ +0x10!)
    absolute_time=TRUE  → 벽시계 (GetTickCount64!)
  → [run (M22b 어댑터 시퀀스 재사용!)] → [폴 (5ms, 60s 안전예산!)] → [stop] 
  - 과거 목표 → 즉시 정지 (int32 델타 검사!)
  - 32비트 wrap 안전 델타 (uint32!)
```

## 2. STM 실측 (TC27x UM + 라이브!)

- **STM0 = 0xF0000000-0xF00000FF** (UM 메모리맵!) — TIM0 = **0xF0000010** ✓
- 라이브: **~51 MHz** (200ms당 ~10.2M 틱!) — 프리런닝 32비트 (약 84초마다 wrap!)
- 코어별: STM1 = 0xF0000110, STM2 = 0xF0000210 ✓

## 3. 검증 결과 (스모크 --rununtil!)

| 항목 | 우리 | 원본 (mcdxdas!) |
|---|---|---|
| abs 1s | **rc 0x0, 1015 ms** ✓✓ | **rc 0x3, 0 ms** ✗ (무음 HANDLE_ERROR!) |
| STM +25M | **rc 0x0, 266 ms** ✓ | **rc 0x3, 0 ms** ✗ |
| 정지 후 상태 | DEBUG ✓ (복원!) | (변화 없음 — 실행조차 안 함!) |
| SMOKE | **OK (0!)** | FAILED (expected — 원본 미구현!) |

**결정적 발견**: **원본의 run_until = 무음 HANDLE_ERROR(rc 3, code 0!)** — DAS 백엔드 미구현!
(M-era가 "실갭 1"로 분류했던 항목 — **우리가 원본을 넘어섬** = 실동작 구현!)

## 4. 판단 (파리티 vs 실용)

- MCD 스펙: run_until = 표준 함수 (호출자가 실동작 기대!)
- 원본 = DAS 백엔드의 한계 (rc 3!) — 우리 TAS 백엔드 = 능력 있음 (STM!) → **실동작이 정답!**
- `qry_current_time` = 원본 "Function is not implemented" (M19 캘리브레이션 — **파리티 유지** ✓!)
- 문서화된 차이: run_until = 개선 (원본 대비!), 나머지 전부 파리티!

## 5. 산출물

- `mcd_tas` 갱신: run_until 실구현 + 스모크 `--rununtil` (양모드 + 오류 대조!)
- 증거: 스모크 로그 (ours/orig — W1 폴더!)
- MCD API 54함수 중 **마지막 실갭 해소!** (실동작 36개!)
