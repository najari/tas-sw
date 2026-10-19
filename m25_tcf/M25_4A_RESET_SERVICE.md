# M25-4a: ContextReset 서비스 (리셋/시작 플로우)

작성: 2026-10-19 · 상태: 완료 (라이브 검증 3항목 통과)

## 1. 구현

원본의 시작 플로우 파리티의 첫 조각: **TCF ContextReset 서비스**.

- `context-aurix.c`에 `aurix_reset()` 핸들러 추가
- 각 코어 컨텍스트 생성 시 `add_reset(ctx, "reset", ...)` 등록
- 타입 이름 **"reset"** (소문자) — VS Code 확장이 보내는 폴백 타입
  (`debugProvider.js`: candidates = `[requestedType, launchResetType, 'reset']`)
- 빌드: `SERVICE_ContextReset=1` (`cmake-tcf-lib.txt`)

## 2. 메커니즘 (M17 재사용)

- `tas_device_connect(0x0003)` = TAS DEVICE_CONNECT + RESET_AND_HALT
  (활성 세션에서 DC 재전송 = 리셋, M17에서 규명·검증)
- `params->suspend`:
  - **true** = 리셋 후 정지 유지 → 프레임워크에 정지 보고
  - **false** = 리셋 후 실행 재개(DBGSR run 쓰기 0x4, M17b: DE=1 유지) → started 이벤트
- **비차단 설계**: 리셋 직후 디바이스가 재부팅하므로 CSFR 접근이 수십 ms
  불가(M17의 과도기) → 핸들러는 DC 성공만 확인하고, 실제 상태 전환은
  기존 코어 모니터(`aurix_monitor`, 100ms 폴)가 추적·보고
  (run 해제는 재부팅 창을 통과하도록 소량 재시도, 과도 실패 허용)

## 3. 라이브 검증 (`tools/m25_4_reset_verify.js`)

```
running check:        0x8003a146 -> 0x8003a142  RUNNING ✓
reset(suspend=true):  err "" → PC = 0xA0000020 (리셋 벡터 정지) ✓
reset(suspend=false): err "" → 재부팅 후 RUNNING ✓ (0x8003a146 -> 0x8003a328)
```

- 0xA0000020 = TC375 리셋 벡터 — M17의 DC(0x0003) 결과와 동일 ✓
- reset+run 후 프로그램이 정상 재실행 = 자연 상태 복원 ✓
- `ContextReset getCapabilities` → `[{"Type":"reset",...}]` ✓

## 4. 엔트리 정지 BP (-e / -f) — 완료

원본의 "label to stop after reset" 플로우 구현:

- `tcf/main/main.c`: `-e<label>` → `aurix_entry_label` 저장, `-f` → `aurix_flasher_used=1`
  (기존엔 수용·무시였음)
- 백엔드(`context-aurix.c`):
  - 리셋 후 **재부팅 창(≈100ms)을 기다린 뒤**(비동기 후처리, `post_event_with_delay`)
  - `find_symbol_by_name`으로 `-e` 라벨(또는 `-f`면 `_START`) 주소 해석
  - `create_eventpoint_ext`로 **임시 HW BP** 심기 (스텝머신과 동일 패턴)
  - halt 해제(run) → 프로그램 부팅·실행
  - BP 히트 시 콜백: "entry function met" + **BP 자동 제거**(원본의
    "Removing stop at function breakpoint")

### 라이브 검증 (`tools/m25_4_entrybp_test.js`)

스텁 ELF(`main` = 0x8003A328, 라이브 프로그램 루프 주소)를 매핑 후:

```
MemoryMap set err: ""  (main 심볼 제공)
reset err: ""
PC after reset+entry-BP: 0x8003a328 -> 0x8003a328  (main에서 정지 ✓)

에이전트 로그:
  AURIX: core 0 device reset performed (DC 0x0003)
  AURIX: entry function found at address: 8003a328 (BP planted)
  AURIX: entry function met (stop-at-function breakpoint)
```

리셋(17.060) → BP 심기(17.166, 재부팅 창 통과) → 히트(17.197) — 원본의
시작 플로우(리셋 → 엔트리 정지)가 완전히 재현됨 ✓

## 5. 산출물

- `agent/system/aurix/tcf/context-aurix.c` — `aurix_reset` + 엔트리 BP + 등록
- `agent/tcf/main/main.c` — `-e`/`-f` 저장
- `cmake-tcf-lib.txt` — `SERVICE_ContextReset=1`
- `tools/m25_4_reset_test.js`, `tools/m25_4_reset_verify.js`,
  `tools/m25_4_entrybp_test.js`, `tools/m25_4_entry_test.elf`(스텁)

## 6. 잔여

- 자체 플래셔(M25-4b)는 원본에 없는 별도 기능 (보류)
- `aurix_entry_fn` TCF 파라미터 전달(launcher 연동) — 선택
