# M25-4 정찰: 플래시 다운로드의 실체 규명

작성: 2026-10-19 · 상태: 정찰 완료 (구현 범위 재정의 필요)

## 1. 발견 — 원본 `aurix-debugger.exe`에 플래시 기록 코드는 없다

원본 바이너리(HighTec launcher 동봉, 9.2MB)의 전체 사용법과 문자열 분석 결과:

```
Usage: agent [OPTION]...
  -r<file>         XML file containing register definitions (mandatory)
  -e<label>        label to stop after reset
  -p"Board"        the name of the HW Board
  -b x             amount of breakpoints set by user 0=no user breakpoints
  -f               flasher was used
```

- **`-f` = "flasher was used"** — 플래시 "실행" 옵션이 아니라, **별도 flasher가 이미
  구웠다는 사실을 알려주는 시작 힌트**.
- erase/sector/프로그램 관련 문자열은 전부 C++/LLVM 라이브러리 심볼뿐 —
  **에이전트 내부에 플래시 알고리즘이 존재하지 않음**.
- 즉 HighTec 흐름의 플래싱 = launcher(별도 도구)가 수행 → 완료 후 에이전트 기동.

## 2. 원본의 실제 시작 플로우 (문자열로 복원)

```
No stop at function defined, flashing enabled, stopping at _START
aurix_entry_fn passed as parameter %s
entry function found at address: %8.8x
Device Reset as part of startup whitout stop at function
Device Reset as part of event_memory_map_changed %d
perform a hardware reset
After Device Reset + Halt %d TR0EVT_ADDR=%8.8x
Removing stop at function breakpoint: %s
entry function met: %8.8x
```

복원되는 동작:
1. `-e<label>`: 리셋 후 정지할 라벨(예: `main`) → **stop-at-function BP** 설정
2. `-f`: flasher 사용됨 → **정지 라벨이 없으면 `_START`에서 정지**
3. 메모리 맵(ELF) 설정 시 **디바이스 리셋(+halt)** 수행 → 엔트리 BP까지 실행 → 정지 보고
4. `aurix_entry_fn`: 진입 함수 파라미터(launcher가 전달)
5. TCF **ContextReset 서비스**로 노출("StopAtEntry" 등 reset 타입)

## 3. 우리 스택의 준비 상태

| 요소 | 상태 |
|---|---|
| TAS 리셋 프리미티브 | ✅ `tas_device_connect(option)` (0xA8 DC) — 0x0003=RESET_AND_HALT |
| 리셋 라이브 검증 | ✅ M17: DC 0x0003 → PC=0xA0000020(리셋 벡터), DBGSR=0x1013(DE=1+HALT) |
| 현재 attach | `tas_device_connect(..., 0x0000)` = **HOT_ATTACH(무리셋)** — 그래서 지금까지 상태 보존 |
| TCF ContextReset 서비스 | ✅ 프레임워크에 존재 (`add_reset(ctx, type, desc, fn)`) |
| ELF 심볼(라벨→주소) | ✅ M25-3에서 완성 |
| HW BP(트리거) | ✅ M25-2에서 완성 |

**결론: 리셋/엔트리 정지 플로우는 우리 스택의 기존 요소 조합만으로 구현 가능.**

## 4. 구현 범위 (제안)

**M25-4a — 리셋/시작 플로우 파리티 (원본 대응)**
- `-e`/`-f` 옵션 저장(main.c에서 현재 무시 중)
- ContextReset 서비스 등록: "Reset"(리셋+실행) / "ResetAndHalt"(리셋+정지)
- 시작 시: DC(0x0003) 리셋+halt → `-e` 라벨(또는 `-f`면 `_START`) 심볼 해석 →
  stop-at-function HW BP → run → 정지 보고
- 타깃 상태 변경(리셋) 수반 → **사용자 승인 필요**

**M25-4b — 자체 플래셔 (원본에 없는, 우리 스택 고유 기능)**
- ELF 로더블 섹션 → PFLASH erase/program 시퀀스 (DAS 쓰기 + 플래시 컨트롤러 레지스터)
- 원본 파리티가 아니라 별도 신규 기능 — 범위/리스크 큼 (실제 플래시 기록)

## 5. 다음 결정 필요

- (a) M25-4a 구현 (원본 파리티, 기존 요소 조합, 리셋 승인 필요)
- (b) M25-4b 자체 플래셔 (신규 기능, 플래시 알고리즘 역분석/구현 필요)
- (c) 둘 다 / 보류
