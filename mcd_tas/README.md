# mcd_tas — MCD API over TAS (프로토타입)

MCD(Multi-Core Debug) API v1.1 **"SPRINT Release"** 구현 DLL 프로토타입.
백엔드는 [TAS](https://github.com/najari/tas-sw) 서버(TCP 24817) — 즉, `mcdxdas.dll`(DAS 백엔드)의
자매품으로 "MCD API를 TAS로" 제공하는 실험 구현입니다.

- 상태: **M19 완료 (2026-10-18)** — M12까지 GUI 전 구간 검증·교차 캘리브레이션 + **M16 run/stop · M17 reset ·
  M18 트리거 · M19 trace·잔여 함수 원본 교차 캘리브레이션**(원본 DLL 직접 프로브, 12/12 라인 일치).
  (M2 애태치: 초기 DEVICE_CONNECT 실패는 USB 허브 경유 문제 — 본체 직결로 해결)
- 원칙: **읽기 전용 기본** — run/stop/reset/트리거는 M16~M18에서 구현(사용 승인 하 상태 변경), Step/CHL은 미구현(스텁)
- 소스 언어: C++ (SPRINT `mcd_api.h` 헤더가 `extern "C"` 비가드 선언이라 C에서는 컴파일 불가)

## 구성

| 경로 | 설명 |
|---|---|
| `include/mcd_api.h`, `include/mcd_types.h` | SPRINT MCD API v1.1 원본 헤더(수정 없음, SPRINT 라이선스 유지) |
| `src/tas_client.h/.cpp` | TAS 최소 클라이언트 — PL2 프레이밍(`[u32 size LE][payload]`), SERVER_CONNECT(0x80), GET_TARGETS(0x86), SESSION_START(0xA1), PING(0xA2), SET_DEVICE_KEY(0xAD), DEVICE_CONNECT(0xA8), PL0 프레임 실행기(START/컨텍스트/아이템/END) |
| `src/mcd_impl.cpp` | MCD API 54개 함수 — M1 11개 + M2 6개 + M4 1개(`qry_reg_map`) 구현, 나머지 정직한 스텁(`MCD_ERR_FN_UNIMPLEMENTED`) |
| `src/proxy_*.cpp/.h` | 원본 `mcdxdas.dll` 비교용 프록시(포워딩 + API 로깅, M6 — `tools/gen_proxy.py`로 생성/빌드) |
| `tests/smoke.cpp` | DLL 동적 로드 스모크(LoadLibrary/GetProcAddress — 실 MCD 툴과 동일한 로딩 방식). `--attach`로 코어 애태치 경로 시험 |
| `tests/tas_probe.cpp` | 프로토콜 레벨 read-only 프로브(SS/PING + AM15 내부 블록 읽기) — DEVICE_CONNECT 없음 |
| `tools/probe.py` | 실서버 바이트 덤프용 read-only 프로브 |
| `smoke_run.txt` | 실서버 스모크 결과 저장본 (M1/M2, 읽기 전용) |
| `smoke_attach_run.txt` | `--attach` 실행 결과 저장본 (DEVICE_CONNECT 시도, 어댑터 복구 전) |
| `smoke_attach_ok.txt` | USB 본체 직결 복구 후 `--attach` 성공 결과 (2026-10-18) |
| `probe_run.txt` | `tas_probe.exe` 결과 저장본 |
| `probe_idcode_retry2.txt` | 복구 후 IDCODE MATCH 프로브 결과 (2026-10-18) |
| `drop_in/` | M3 drop-in 테스트 폴더: 원본 클라이언트 사본 + `mcdxdas.dll`(우리 DLL). `client_stderr*.log` = GUI 세션 로그 |
| `gui_test/` | M3~M4 GUI 검증 스크린샷(Select/Core Info/Read/State·IP)·원본 `mcdxdas.dll` 디스어셈블 |

## 빌드 / 실행

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 스모크 (읽기 전용: SERVER_CONNECT + GET_TARGETS + 열거)
build\Release\mcd_tas_smoke.exe                # 인자: [dll경로] [config문자열]
build\Release\mcd_tas_smoke.exe --attach       # + 코어 애태치(DEVICE_CONNECT) 시험 → 타깃 halt 가능, 승인 필요

# 프로토콜 프로브 (읽기 전용: SVR/GT/SS/PING + AM15 내부 읽기, DC 없음)
build\Release\tas_probe.exe [host] [port]
```

- `config_string` 예: `McdHostName=localhost McdServerPort=24817` (프로토타입 확장 키).
  `NULL`/빈 값/`"localhost"`/`"host:port"` 형식 모두 허용, 기본값 `localhost:24817`.
- 디버그 로그: `set MCD_TAS_LOG=1` → stderr에 핸드셰이크/세션/애태치 로그.
- drop-in 실험 시: `mcd_tas.dll`을 사용 툴과 같은 폴더에 `mcdxdas.dll` 이름으로 배치(후속 마일스톤).

## M1 구현 함수 (11)

`mcd_initialize_f` `mcd_exit_f` `mcd_qry_servers_f` `mcd_open_server_f` `mcd_close_server_f`
`mcd_set_server_config_f` `mcd_qry_server_config_f` `mcd_qry_systems_f` `mcd_qry_devices_f`
`mcd_qry_cores_f` `mcd_qry_error_info_f`

## M2 구현 함수 (6)

`mcd_open_core_f` `mcd_close_core_f` `mcd_qry_mem_spaces_f` `mcd_execute_txlist_f`
`mcd_qry_state_f` `mcd_qry_max_payload_size_f`

나머지 8개는 `MCD_RET_ACT_HANDLE_ERROR` + `MCD_ERR_FN_UNIMPLEMENTED` + 함수명 메시지를 반환합니다
(`mcd_qry_error_info_f`로 조회 가능): `run_until`·`execute_command`,
CHL 5종(open/send/receive/reset/close), `qry_input_handle`
— 이 중 CHL 5종+`qry_input_handle`은 **원본도 스텁/미export**라 파리티.
(run/stop **M16** · reset **M17** · 트리거 **M18** · trace·잔여 캘리브레이션 **M19** · SPR 변환 **M20** ·
**step M22** — 아래 섹션)

## M22 구현 — `step` 실구현 + run 접근 HW 시퀀스 (2026-10-18)

- **Frida(WS2_32) 캡처로 원본 시퀀스 확보** — M21에서 "DAS 다이얼렉트 필요"로 결론지었던 항목을
  REA Frida로 원본 트래픽을 직접 캡처해 해결(프레임 대조: `m22_dropin/step_frames.txt`)
- **원본 step 시퀀스**: [디버그 초기화(트리거 유닛 8개 + DBGSR..+0xF 영소거 + `FD10←0x2A` — 한 프레임)] →
  [`0xF0000494←0x8000003E`(어댑터 싱글스텝 모드)] → [`DBGSR←4`] → [`0xF0000494←0x8000003F`] → [초기화 재실행]
  → 1명령 실행 후 재정지(DBGSR `0x100B`, SUSP=1)
- `mcd_step_f` 실구현: 정지 선확인·`step_type`(INSTR/CUSTOM_LO..HI) 검증·초기화(한 PL0 프레임, WRBLK 사용)·
  어댑터 브래킷·DBGSR←4 ×n_steps·정지 재확인 — 문구는 원본 정본("Stepping failed. Core is not in debug state"·
  "Core did not complete single step")
- **M22b — run**: 어댑터 시퀀스 추가([0494←3E→DBGSR=4→0494←3F→DBGSR=4→**`0x049C←2`(RUN 명령)**]) —
  SUSP(디버거 홀드) 상태의 코어도 재개 가능(기존 DBGSR 단독 쓰기로는 불가였던 케이스)
- 검증: smoke `--step`(우리/원본 **동일 동작** — 1명령 델타 +2~+22(분기 포함))·`--steps 4`·회귀 전량 OK·
  **GUI(원본 클라이언트 + 우리 DLL): Step ×3(IP 0x8003A142→A146→A15C)·Run(Running)·Stop(Debug)** ✓
  — DBGSR 영소거 창의 1회성 "Read failure" 표시는 M16/M6 캘리브레이션과 동일(자동 복구)
- 배포: `drop_in\mcdxdas.dll` = SHA256 `9C6F0D51B8B0ED23…` (M22)
- 환경 노트: 조사 중 어댑터 **Native JTAG Mode** 고착(디바이스 식별 실패) — 원본 DLL +
  config `McdAccHw.NativeJtagMode=false` open으로 복구(강제 서버 킬 직후 발생 — 정상 종료 권장)

## M20 구현 — SPR 로컬→전역 변환 (2026-10-18)

- `mcd_execute_txlist_f`에 **코어 로컬 스크래치패드 윈도 변환** 추가: 원본(DAS 경유)은 로컬 주소를
  대상 코어 기준으로 해석하지만 TAS는 전역 버스 뷰만 알기 때문에, 로컬 주소는 PL0 버스 오류였음(M12 실증)
  - local DSPR `0xD0000000..0xD001FFFF` → `0x70000000 - core*0x10000000` (코어별 버스 별칭)
  - local PSPR `0xC0000000..0xC0007FFF` → `0x70100000 - core*0x10000000`
  - 근거: TC27x UM Table 5-10(코어별 `PSPR_base/DSPR_base`), 로컬 주소는 항상 `C0000000/D0000000`.
    윈도는 파워오브투로 넉넉히(캐시 연속 매핑 포함) — 미구현 영역은 양쪽 모두 실패라 안전
  - 읽기·쓰기 공통 경로(스플릿 이전)에서 변환, `MCD_TAS_LOG`에 `local SPR ... -> bus ...` 라인 기록
- 검증(`smoke --spr`, 읽기 전용): 수정 전 로컬 SPR 읽기 **PL0 오류(rc 3, 0x2100)** → 수정 후
  `rc 0, ok 2` + **원본 mcdxdas.dll(1.14.12)과 동일 결과(파리티)** · `--attach --trace` 회귀 ok
- 검증(`smoke --sprw`, 승인 하 쓰기): 로컬 주소로 패턴(`0x5AA55AA5`) 쓰기 → 버스 별칭에서 리드백 일치
  → 원값(0) 복원 확인 — **원본과 동일 동작(파리티)**, 입력 상태 복원 불변식 유지
- GUI 검증(원본 `mcd_basic_client.exe` + 우리 DLL): 로컬 주소 `0xD0000000`으로 Write/Read —
  리드백 `0x5AA55AA5`(M15 북엔드 재현) → 원값 복원 `0x00000000` · DLL 로그에
  `local SPR 0xD0000000 -> bus 0x70000000 (core 0)` 실증
- **M20b — reg_map 코어별 PC**(원본 교차): 전 코어 `0xF881FE08`(CPU0) 반환 → 코어별 CSFR 윈도
  (`+0x20000*core`)로 수정 — 원본 실측: core1 `0xF883FE08`·core2 `0xF885FE08`,
  **전 40개 항목 +0x20000 시프트(40/40 일치)** · 라이브 PC 읽기 core0/1/2 상이값
  (`0x8003A142`/`0x8003A59A`/`0x80038E52`) · smoke `--regmap`/`--core N` 프로브 신규
- 배포: `drop_in\mcdxdas.dll` = SHA256 `AE2AF7F7141AAB1D…` (M20b)

## M19 구현 — trace·잔여 함수 원본 교차 캘리브레이션 (2026-10-18)

- **프로브 신규 확립**: smoke `--trace` 모드로 **원본 mcdxdas.dll(1.14.12)을 직접 로드**해 실측
  (원본 로드 조건: 빈 system_key·개행+따옴표 config; NULL 인자에서 원본은 크래시 → 실버퍼 사용)
- **③ execute_command**: 원본 exports에 **없음**(미export 3종: `execute_command`·`set_global`·`qry_input_handle`;
  원본은 대신 `mcd_das_*` 4개 export) — 우리는 54개 전부 export(API-정합, 차이로 기록)
- **④ trace 4종**: 원본은 전부 **무음 HANDLE_ERROR**(rc 3·code 0·출력 미변경) → 파리티 적용(`err_silent`)
- **부수 캘리브레이션**: `qry_core_modes`(**Supervisor·User 2종** 구현) · `qry_reg_compound`/`qry_current_time`/
  `qry_device_description`(→ rc 3+0x100) · `qry_max_payload_size`(**16384**) · `qry_trig_info`(**0xF/0x3/0x1/8/swbp=1**)
- 검증: 원본 vs 우리 프로브 **12/12 라인 완전 일치** · smoke `--runstop`·`--rst`·`--trig` 회귀 ok ·
  GUI Run/Stop sanity(Debug 복원) — 증거: `smoke_trace_orig.txt`/`smoke_trace_our.txt`
- 배포: `drop_in\mcdxdas.dll` = SHA256 `F266DACC3FED9DD8…` (M19)

## M16 구현 — run/stop (2026-10-18)

- `mcd_run_f` / `mcd_stop_f` 실구현: **DBGSR(0xF881FD00) 쓰기** 경로(PL0 WR32)
  - run = `0x00000004` (HALT[2:1]=10b: 해제 — CCL `startCpu`의 `mcdsrl_write(DBGSR, 0x0004)`와 동일)
  - stop = `0x00000006` (HALT[2:1]=11b: 설정, DE=1 필요) — TC1.6 UM Vol.1 §12.9
  - 상태는 내부 모델(원본 파리티 — DBGSR 재판독 없음): run→RUNNING, stop→DEBUG + **1회성 event=0x10(STOPPED)**
  - `global!=0`이면 애태치된 모든 코어 슬롯에 적용(그룹 시맨틱)
- 라이브 검증(`mcd_tas_smoke.exe --attach --runstop`): run→`DBGSR=0x00000001`(HALT=00b) ·
  stop→`DBGSR=0x00000003`(HALT=01b) **독립 재판독 확인**, 상태/이벤트 시퀀스 전 항목 ok,
  **입력 상태 복원(DEBUG)** · 실패 0 — 증거: `smoke_runstop_run.txt`
- **M16b — GUI 검증(원본 `mcd_basic_client.exe` + 우리 DLL)**:
  - Run 클릭 시퀀스 실측: `remove_trig_set` → `activate_trig_set` → `run`
    → 스텁이던 `remove/activate_trig_set`을 **성공 no-op**으로 구현(트리거 세트는 항상 비어 있음 —
    `create_trig` 불가 — 이므로 정합). Run/Stop 버튼 전 구간 동작 확인:
    Stop(Running→Debug, IP 고정) · Run(Debug→Running, IP 진행) · 최종 Debug 복원
  - 스크린샷: `gui_test\m16_main3.png`(Running) · `m16_stop1.png`(Debug) · `m16_run2.png`(Running) ·
    `m16_final.png`(Debug) — DLL 로그: `drop_in\client_stderr33.log`/`34.log`
  - 관찰: 상태 전환 직후 첫 폴링에서 **1회성 읽기 실패**(PL0 0x8D→0x98)가 표시될 수 있음 —
    M6에서 원본과 함께 캘리브레이션한 것과 동일한 표시·자동 복구
- 참고: EVTSRC 차이(원본 stop=TR0EVT `0x10xx` vs DBGSR 쓰기=EXEVT `0x000x`) — 상태 판정과 무관
- 미구현 유지: `mcd_rst_f`는 **M17에서 실구현**(아래 M17 섹션), `mcd_step_f`, `mcd_run_until_f`
- 배포: `drop_in\mcdxdas.dll` = SHA256 `9EA500C63676454B…` (M16b 기준, 전문은 파일 참조)

## M17 구현 — reset (2026-10-18)

- `mcd_rst_f(core, rst_class_vector, rst_and_halt)` 실구현: **활성 세션에서 TAS DEVICE_CONNECT 재전송**
  - 항상 option `0x0003`(RESET_AND_HALT) 사용 → `TRUE`면 그대로(정지) · `FALSE`면 DBGSR run 쓰기(0x4)로 해제(부팅·실행)
  - **(M17b) 이유**: `0x0001`(RESET)은 리셋 후 **DBGSR.DE=0**(디버그 비활성)을 남겨, 이후 stop(halt 쓰기 0x6)이
    하드웨어에서 무시됨(버스 ACK만) — `0x0003` 경로는 **DE=1 유지** → stop 정상.
    run/stop/rst 모두 **실제 DBGSR 재판독 검증**을 추가해 무음 상태 불일치를 차단
  - 공식 client API와 동일 계열(`CTasClientServerCon::mDeviceConnect(dco)`, `tas_clnt_dco_et`);
    서버 `feat_used`로 적용 확인(요청 비트 검사 — UNLOCK 비트 허용)
  - 리셋 클래스 선택은 TAS 백엔드 미지원 — 광고 벡터(0x7) 내 임의 값 → 동일 리셋(문서화)
  - 상태 모델: rst_and_halt→DEBUG(+1회성 STOPPED) · reset→RUNNING; 같은 디바이스의 다른 슬롯은 무효화(재판독)
- 라이브 검증(smoke `--attach --rst`): 리셋 전 PC=0x8003A330 → **리셋 후 PC=0xA0000020(리셋 벡터)·
  DBGSR=0x00001013(HALT=01b)** — 2012 MCD 데모 로그의 `rsthlt` 결과(0xA0000020)와 동일 ·
  **reset-and-run → DE=1 → stop → HALT=01b** (M17b 경로) · 실패 0 (증거: `smoke_rst_run.txt`)
- **GUI 검증**(원본 클라이언트): "Reset and Halt" → `mcd_rst_f(class 0x1, TRUE)` → DC 0x0003 → Debug(PC 0xA0000020) ·
  "Reset" → DC 0x0003+run → Running(DE=1) → **"Stop" → Debug·IP 고정(M17b 수정 확인)** · 최종 Debug
  (화면: `gui_test\m17*.png` · 로그: `drop_in\client_stderr35/36.log`)
- 배포: `drop_in\mcdxdas.dll` = SHA256 `29A1573B735F71A8…` (M17b)

## step·run_until 조사 결과 (2026-10-18, M22에서 step 해결)

- **`mcd_step_f`(싱글스텝) — 실구현 완료(M22)**:
  - 원본 시퀀스(Frida 캡처): 디버그 초기화(트리거 윈도 + DBGSR 영소거 + FD10) → 어댑터 `0494←3E` →
    `DBGSR←4` → `0494←3F` → 초기화 재실행 — 1명령 실행 후 재정지(SUSP 복귀).
  - M21의 "DAS 다이얼렉트 필요" 결론은 **어댑터 시퀀스 누락**이 원인이었음(acc_mode 불필요 — 일반 PL0 경로).
  - 도구: smoke `--step`/`--steps N` — 증거 `m22_dropin/`
- `mcd_run_until_f`: 대상 시간 소스(`qry_current_time`) 미확보 — 동일 계열 후속.

## M18 구현 — 트리거(브레이크포인트) (2026-10-18)

- `mcd_create_trig_f`/`qry_trig_f`/`remove_trig_f`/`qry_trig_state_f` 실구현: **OCDS 트리거 유닛 TR0..TR7**
  직접 프로그래밍(CPUx CSFR: `TRxEVT 0xF000+8n`·`TRxADR 0xF004+8n`·`TRIG_ACC 0xFD30`)
  - TRxEVT = `BOD|BBM|EVTA=Halt`(0x1A) + `TYP=PC`(0x1000)/`ALD`(read, 0x10000000)/`AST`(write, 0x08000000)
    + 범위 시 `RNG`(0x2000) — TC1.6 UM §12.9 필드 레이아웃 검증
    (참고: CCL 상수 0x1011은 EVTA=001 "관찰 전용" — 브레이크포인트용으로 정정)
  - 단일 주소=임의 유닛, **범위=짝수 유닛+상위 이웃(TR1ADR=끝주소)** — 총 8유닛
  - `qry_trig_state`: TRIG_ACC 판독(읽으면 클리어) → 모델 래치 → captured 보고
  - `remove_trig_set`: 하드웨어+모델 실제 제거 / `activate_trig_set`: no-op(생성 시 즉시 armed)
  - `qry_trig_info`: IP/READ/WRITE/RW·DBG_DEBUG/GLOBAL·8유닛 광고 / `qry_trig_set(_state)`: 실데이터
- 라이브 검증(smoke `--trig`): 브레이크포인트 @0x8003A160 → **PC=0x8003A160에서 정지·EVTSRC=16(TR0EVT)** ·
  captured=1 · 제거 후 세트 비움 · 실패 0 (증거: `smoke_trig_run.txt`)
- **GUI 검증**(원본 클라이언트): IP Trigger 체크+주소 입력 → Run →
  `remove_trig_set → create_trig(TR0, evt 0x101A) → activate → run` → **State Debug·IP 0x8003A160·"ACTIVE"** ·
  체크 해제 후 Run → `remove_trig_set(1 cleared)` → 정상 실행 · Stop → Debug
  (화면: `gui_test\m18_*.png` · 로그: `drop_in\client_stderr37.log`)
- 부수 개선: run 검증에서 **트리거 히트 구분**(브레이크포인트 레이스 — 정지가 트리거 때문이면 성공+DEBUG)
- 배포: `drop_in\mcdxdas.dll` = SHA256 `62C5E0EC2464FD11…` (M18)

### 세션 / 애태치 (`mcd_open_core_f`)

- 코어당 **독립 TCP 연결 + 세션**을 열어 최대 4개 코어 동시 지원(같은 타깃+코어 재호출은 기존 세션 재사용).
- 검증된 시퀀스 (참조 mcdxdas 클라이언트의 실측 캡처와 동일 순서):
  `SERVER_CONNECT → SESSION_START(0xA1) → PING(0xA2) → AM15 정보 블록 3종(0xD000/0x0000/0x8000, 참조 클라이언트 패리티) → SET_DEVICE_KEY(0xAD, ulo=0x0001 hot attach, 키 없음) → DEVICE_CONNECT(0xA8, option=0: 리셋·halt 요청 없음)`
- 애태치 후 `GET_TARGETS`를 재조회해 `device_type`/`device_id`(서버가 식별한 경우)를 갱신.
- 실패 시 TAS PL1 에러를 MCD 코드로 매핑(`0x46→DEVICE_LOCKED`, `0x44/0x48→DEVICE_ACCESS`, `0x32→SERVER_LOCKED`, 기타→`CONNECTION`)하고 원 코드를 메시지에 포함.

### 메모리 접근 (`mcd_execute_txlist_f`)

- **주소 공간**: 읽기/쓰기(R=1/W=2)만. `access_width` 0/1/2/4/8 — 0이면 정렬 기반 자동 분할
  (8바이트 정렬→RD64/WR64, 홀수 주소→1바이트 단위). 그 외 폭/비정합 크기/정렬은 `MCD_ERR_TXLIST_TX`.
- **PL0 인코딩 규칙** (참조 클라이언트 정본 기준):
  - 프레임 = `START(0xB0, pl1_cnt 0xFFC1부터 증가, 사용 맵 마스크)` + 컨텍스트 + 아이템 + `END(0xB1, n)`,
    컨텍스트 아이템은 **변경 시에만**: MAP(0x11) → BASE32(0x15)/BASE64(0x16). ACC(0x10)은 기본값 0이라 미사용.
  - 프레임당 ≤32 트랜잭션(SS의 `pl0_max_num_rw`), ≤1060B 요청, ≤1040B 응답(SS의 max_pl2rq/rsp).
  - 읽기: RD8/16/32/64(≤8B), RDBLK(8정렬·8배수·≤1024B, 1KB는 RDBLK1KB 0x2D 응답도 인식).
  - 쓰기: WR8/16/32(WL=1)/WR64(WL=2), WRBLK(WL=N, 4정렬·≤1024B).
- 첫 실패 트랜잭션에서 `num_tx_ok`/`num_bytes_ok`를 집계해 보고 후 중단. 이미 전송된 프레임은
  취소할 수 없어(서버가 전량 실행) 이후 트랜잭션 실행분이 버려질 수 있음 — 참조 API의 "즉시 중단"과 차이.

### 기타

- `mcd_qry_mem_spaces_f`: 단일 메모리 공간 id **1** = `"AM0"`(TAS 주소 맵 0, 타깃 물리 접근),
  `MEM_SPACE_IS_PHYSICAL`, 8bit MAU, little endian, 0..0xFFFFFFFF. (AM15는 서버 내부 맵이라 노출하지 않음)
- `mcd_qry_reg_map_f` (M4): group 0/1에 단일 `PC` 레지스터(AM0, `0xF881FE08`, 32bit) 제공 —
  클라이언트가 이 주소를 폴링해 `Instruction Pointer`를 표시(요청 용량 cap 준수).
- `mcd_qry_state_f`: **`MCD_CORE_STATE_DEBUG` 고정 반환**(M4 캘리브레이션 — UI State 표시 파리티.
  실상태 도출은 후속).
- `mcd_qry_max_payload_size_f`: 64KiB(초과 트랜잭션은 내부에서 자동 분할).

## 매핑 규약 (M1)

- **서버 열거**: `mcd_qry_servers_f`가 `host:24817`에 일회용 핸드셰이크(SERVER_CONNECT+GET_TARGETS)를
  수행해 살아 있는 서버 1개를 보고. `server` ← TAS 서버명("TasServer"), `acc_hw` ← 첫 타깃 identifier.
  실 HW 서버 관례에 따라 `system_instance`는 빈 문자열.
- **시스템**: `"Real HW"` 1개 (`mcd_core_con_info_st.system`).
- **디바이스**: GT 레코드 → `acc_hw`(identifier), `device_type`(idcode), `device_id`(devid[0], 잠정).
  이름은 잠정 테이블: `0x401DA083`(TC27x) → `"TC27x"`, 그 외/0 → `"Unknown device"`.
- **코어**: 잠정 테이블 — TC27x → 3코어, 이름 `"Core %d: CPU%d TriCore 1.6E"`,
  `core_type` = 44(ELF `EM_TRICORE`), `core_id` = `0x00B7C002`(잠정). **실 `mcdxdas.dll`과 재교정 필요**
  (값 출처: 실 스택 MCD Basic Client Core Info UI 관측).
- **오류**: 전역 `last_err` 1개(임계구역 보호). 코어별 분리는 M2+.

## 검증 기록 (2026-10-17)

실서버(0.0.0.0:24817, PID 19760) 대상 스모크 — 전 항목 ok, 실패 0:

```
  [ok]   mcd_initialize_f
       impl: api 1.1 'SPRINT Release', vendor 'mcd_tas prototype', build 0.1.1
  [ok]   mcd_qry_servers_f → server 'TasServer' acc_hw 'DAS JTAG AURDUINO 46YPEADW'
  [ok]   mcd_open_server_f
  [ok]   mcd_qry_systems_f → system 'Real HW' host localhost:24817
  [ok]   mcd_qry_devices_f → device[0] acc_hw 'DAS JTAG AURDUINO 46YPEADW'
  [ok]   mcd_qry_cores_f   → core[0] 'Core 0' core_type 44
  [ok]   stub mcd_run_f returns HANDLE_ERROR (0x0100)
  [ok]   mcd_close_server_f
```

관찰 사항:

1. **idcode/devid 변동**: 캡처 코퍼스(`tas_server_analysis\mcds\tas_capture`, 활성 세션 시기)의
   GET_TARGETS 응답 1741건은 전부 `devtype=0x401DA083`, `devid=47427060:11080040:64945FCB:20E65C06`였으나,
   현재 유휴 서버는 같은 필드를 **0**으로 보고(AARDUINO 디바이스 미식별 상태로 추정).
   프로토타입은 서버 값을 그대로 전달 — `device_type=0`이면 이름은 "Unknown device"가 됩니다.
2. GT 레코드 오프셋(88B: id64 + devtype + devid×4 + phys + nclient)은 코퍼스/실서버 원문과
   바이트 단위로 일치 확인.

### M2 검증 (2026-10-17)

**A. 프로토콜 프로브** (`tas_probe.exe`, read-only — DC 없음) — 전 항목 ok:

```
  [ok] SERVER_CONNECT: 'TasServer' ver 0.2 protos 0x00000002 date 'Apr 30 2026'
  [ok] GET_TARGETS: 1 target → 'DAS JTAG AURDUINO 46YPEADW' devtype 0x00000000 nclient 0
  [ok] SESSION_START: max pl2 rq 1060 rsp 1040, max_rw 32, map_mask 0x8000, hash NoUid
  [ok] PING
  [ok] AM15 RD32 @0x000C0100 (ACC_HW_FREQUENCY) = 400000   ← 코퍼스 값과 정확히 일치
  [ok] AM15 RDBLK @0xD000 (104B) / @0x8000 (168B, "DAS JTAG AURDUINO…" 디스크립터)
```

→ SS 응답 파싱 + PL0 프레임 빌더(컨텍스트/BASE32/RDBLK)가 실서버에서 코퍼스와 동일하게 동작함을 확인.

**B. 무플래그 스모크 (build 0.2.0)**: M1 전 항목 ok + attach 스킵 정상 — 회귀 없음.

**C. `--attach` 스모크** (사용자 승인 후 실행): 열거 전 단계 전 항목 ok,
`mcd_open_core_f`가 **DEVICE_CONNECT에서 `tas_err=0x20`(TAS_PL1_ERR_CMD_FAILED)로 실패**:

- SESSION_START/PING/SET_DEVICE_KEY/AM15 정보 읽기는 모두 성공, 요청 바이트는 코퍼스와 동일
  (`01 a8 00 00 00 00 00 00`) → 프로토콜 문제 아님.
- 원인은 **환경**: 서버가 식별된 디바이스 없음을 보고(AM15 0xD000 디스크립터 전부 0,
  `addr_map_mask=0x8000` = AM15만 — 코퍼스 식별 시점은 `0x8001` = AM0|AM15) +
  PnP에서 **USB-SERIAL CH340 (COM3) Status=Unknown** (AURDUINO 어댑터; DAS JDS 계열은 OK).
- DC 미성공으로 타깃 상태 변화는 없음. 어댑터 정상화(재연결/드라이버) 후 `--attach` 재실행 필요.
- 실패는 `MCD_ERR_CONNECTION`(메시지에 tas_err 원코드 + "server reports no identified device" 진단 포함)로 보고 확인.

### M3 검증 (2026-10-18) — `mcd_basic_client.exe` drop-in GUI

원본 `mcd_basic_client.exe`(DAS64 클라이언트 사본)가 우리 `mcd_tas.dll`을 `mcdxdas.dll` 이름으로 로드
(`drop_in/` 폴더, `MCD_TAS_LOG=1`). GUI 전 구간 실동작 확인:

- **애태치/코어 열기**: Core Select → Select 클릭 → 타이틀 `DAS JTAG AURDUINO 46YPEADW` 전환.
  로그: `open_core request → auto-open → session attached (0x401DA083) → core 0 opened ('TC27x')`.
- **Core Info 다이얼로그**: 전 항목이 우리 DLL 열거 값 — `gui_test/coreinfo_v1.png`
  (device_type `0x401DA083`, device `TC27x`, core `Core 0: CPU0 TriCore 1.6E`, core_type 44, core_id `0x00B7C002`).
- **GUI Read 성공 2건** (`gui_test/read_result.png`, `read_f0000480.png`):
  - `0x70000000` → Value `0x00000000` — rq `00 15 00 70`(BASE32)+`00 25 00 00`(RD32), 오류 없음.
  - `0xF0000480` → Value **`0x00080001`** — M2 스모크와 **동일 값**(정상 시절 시그니처 일치).
- 실무 함정(재현 시 필독): ① 주소 입력은 **`0x` 접두 필수** — 클라이언트 파서가 base-0(`%i`)라
  `070000000`처럼 0으로 시작하면 8진수 해석(=0xE00000). ② FLTK 창은 UIA 트리에 위젯이 없어 픽셀 클릭 필요;
  Ctrl 계열 조합키는 PostMessage로 전달되지 않음. ③ Core Info 등 모달이 열리면 입력 차단(Escape로 닫기).
- 잔여 차이(→M4에서 해소): State/IP 필드. 읽기 실패 시 원본은 모달 경고, 현재는 MCD Events 텍스트 —
  오류 반환 조합 캘리브레이션 후속.
- 상세 기록: `C:\work\tas_server_analysis\M3_GUI_DROPIN_NOTES.md`

### M4 검증 (2026-10-18) — State/IP 실값 복구 (정상 시절 파리티)

- 규명: 클라이언트는 `mcd_qry_reg_map_f`(**group 0**)로 폴링할 PC 레지스터를 얻는다 — M3까지는
  우리 DLL이 0개 반환 → 기본값 `RD16 @(space 0, 0x0)` 폴링 → 서버 0x8D → UI 갱신 정지
  (`qry_state` 반환값만으로는 표시가 갱신되지 않음 — 성공 폴링에 게이트).
- 수정: `qry_reg_map`이 `PC` @ **`0xF881FE08`**(AM0, 32bit — 정상 시절 폴링 주소) 반환,
  `qry_state` = `DEBUG`. 클라이언트가 즉시 `RD32 @0xF881FE08`을 폴링(**4.6 Hz** ≈ 정상 시절 4.4 Hz)
  → 응답 `0xA0000020`(오류 0).
- UI 확인(`gui_test/m4d_main_final.png`): **`State: Debug` / `IP: 0xA0000020`** — 정상 시절
  (`tas-sw/mcds/crop_13_after_read.png`)과 동일. MCD Events 비어 있음(실패 0).
- 회귀: GUI Read `0xF0000480` → `0x00080001` 유지(`gui_test/m4c_read_result.png`).
- 구현 결함 1건 수정: 클라이언트가 `cap=1`(버퍼 1개)로 요청하는데 2개를 기록(용량 초과) —
  단일 레지스터로 축소 + 용량 준수.
- **디버그 레지스터 규명(2026-10-18)**: `0xF881FE08` = **PC**(CPU0 CSFR+FE08H), `0xF881FD00` =
  **DBGSR**(Debug Status Register, HALT[2:1]=01b로 정지 판독) — TC27x UM 해독 + 드롭인 GUI Read
  라이브 검증. `qry_state` 실상태는 DBGSR 기반 구현 가능.
- 상세 기록: `C:\work\tas_server_analysis\M4_STATE_IP_RECOVERY.md`,
  `C:\work\tas_server_analysis\TC27X_DEBUG_REGISTERS.md`

### M5 검증 (2026-10-18) — `qry_state` 실상태 (DBGSR 기반, 읽기 전용)

- 구현: 첫 `qry_state` 호출 시 DBGSR(`0xF881FD00`)을 1회 RD32 → 해독·캐시
  (HALT[2:1]=01b→`DEBUG`, 00b→`RUNNING`; EVTSRC→`trig_id`/`stop_str`). 실패 시 DEBUG 폴백+재시도.
- 검증(`client_stderr13.log`, PID 30980): DBGSR 판독 1건 `0x0000101B → DEBUG (cached)`,
  UI `State: Debug / IP: 0xA0000020` 유지, 폴링 4.6 Hz, 실패/폴백 0, GUI Read 회귀 `0x00080001`.
- 트래픽 정합: DBGSR 세션당 1회 판독 — 정상 시절 캡처와 동일.
- 상세 기록: `C:\work\tas_server_analysis\M5_QRY_STATE_DBGSR.md`
  (화면/발췌: `gui_test/m5_state_*.png`, `gui_test/m5_state_log_excerpt.txt`).

### M6 검증 (2026-10-18) — 읽기 실패 오류 표시 캘리브레이션 (error string 정합)

- 원본 `mcdxdas.dll` 프록시(`mcdorig_dropin/`, `mcdxdas_orig.dll` + `mcd_proxy.dll`)로
  실패한 사용자 Read의 실제 반환값을 관찰: `(ret=3, code=0x2100, events=0)` — 우리 구현과
  **비트 단위 동일**. `num_tx_ok=0`, `num_bytes_ok=0`, data cleared.
- 차이는 `error_str` 문구뿐: 원본은 `"Read failure for a transaction of the txlist"` (Infineon
  MCD API `MCD_ERR_TXLIST_READ` 정본 문구)를 `qry_error_info`로 보고.
- 수정: `mcd_execute_txlist_f` 항목 실패 분기의 `err_set` 메시지를 원본 정본 문구로 교체
  (Read/Write). 진단 상세(주소/PL0 err)는 별도 `dbg()`로 보존.
- 검증(`client_stderr16.log`, MCD_TAS_LOG=1): `FAIL: Read failure for a transaction of the txlist` →
  `CALL qry_error_info ('Read failure for a transaction of the txlist')` — 원본과 문구 일치.
- **재검토**: 원본 DLL 직접(프록시 없이) 실행·주소 기본값 `0`/`0xE00000` 재현에서도
  모달·박스 표시가 없음(우리와 동일) — "원본 모달 vs 우리 무표시" 차이는 현재 환경에서 재현 불가.
  정상 시절 와이어(`RD32 @0x0000`→`00 25 00 8D`, cnt 0x014B)와 우리 응답도 동일.
  **[정정(M12)]**: 실패 모달은 양 스택 공통으로 표시·차단됨(캡처 미표시 + 상호작용 미시도로 오판) —
  M12 참조.
- 성공 Read 회귀(로그 실증, `client_stderr17.log`): 요청 `B32:F000|R32:0480`(정상 시절 동일 형태)
  → 응답 `01 00 08 00`(0x00080001) — GUI 표시 확인(`m6v_success_final.png`).
- 결론: **(ret, err) 조합 변경 불필요**(이미 정확); 캘리브레이션은 error string 정합으로 완료.
- 상세 기록: `C:\work\tas_server_analysis\M6_ERROR_DISPLAY_CALIBRATION.md`
  (화면/발췌: `gui_test/m5v_success.png`, `m5v_fail.png`, `m5orig_fail.png`, `m6_direct_read0.png`,
  `gui_test/m5_proxy_log_excerpt.txt`).

### M7 검증 (2026-10-18) — reg_map/reg_groups 원본 교차 캘리브레이션 (읽기 전용)

- 프록시 확장(reg_map 인자·엔트리 덤프 + 첫 count 질의에서 원샷 전체 덤프 + 그룹 프로브 +
  trig_set_state 덤프)으로 원본 레지스터 DB를 전량 관찰.
- 원본: 그룹 `id=0 "CPU Registers"` **40개** — `PCXI/PSW/PC/BIV/BTV/ISP/ICR/FCX` +
  `D0–D15`(0xF881FF00..) + `A0–A15`(0xF881FF80..); 공통 `space=0`, size=32, type=SIMPLE,
  `rmask=wmask=0x2`, **PC만 side_w=1**. 클라이언트는 count(→40) 후 `start=0/1/2`를 1개씩
  가져와 `PC`를 찾으면 중단 → PC만 폴링(space=0).
- 부수: `trig_set_state`는 원본도 전 필드 0(우리 스텁과 동일) · `qry_state`는 원본이
  `stop_str=''`/`info_str=''` — M5에서 채웠던 필드를 **공백으로 정정**(state만 실값).
- 수정: `g_reg_desc[40]` 테이블 + `mcd_tas_fill_reg_info()`(원본 값 그대로) ·
  `qry_reg_groups` id 0 "CPU Registers" n=40 · `qry_reg_map` group 0 서빙(count/fetch) ·
  `qry_state` 필드 공백화.
- 검증(`client_stderr18.log`, PID 25064): 호출 패턴이 원본과 일치
  (`qry_reg_map group=0 start=0 cap=0 → start=0/1/2 cap=1 → RD32 space=0 @0xF881FE08`),
  `State: Debug`/`IP: 0xA0000020`, 성공 Read `0xF0000480 → 0x00080001` 회귀.
- 상세 기록: `C:\work\tas_server_analysis\M7_REG_MAP_CROSS_CALIBRATION.md`
  (화면/발췌: `gui_test/m7_regs_main.png`, `m7_read_regression.png`).

### M8 검증 (2026-10-18) — mem_spaces 교차 + mem_blocks/스텁 정본 문구 정합 (읽기 전용)

- 원본 관찰(프록시 원샷 프로브): mem_spaces = `id=0 "Code and Data"`·`type=0`·mau=8·endian=LITTLE·
  `0..0xFFFFFFFF`·`opts=0x200`(ATOMIC_WITH_NEXT) / mem_blocks = **미구현**
  (`HANDLE_ERROR` + `0x100 "Function is not implemented"`).
- 수정: mem_spaces를 원본 값으로 정합(id 1→0, 이름, type, opts) · mem_blocks를 원본과 동일한
  미구현 오류로 · **스텁 문구 정본화**("Function is not implemented", 함수명은 `dbg`로 보존) ·
  txlist space 0 + legacy 1 허용.
- 검증(smoke `--attach`): `mem space id 0 'Code and Data' ... opts 0x200` ·
  `mem blocks rc 3: code 0x100 'Function is not implemented'` · **SMOKE OK (0 failures)**.
  GUI 회귀: `State: Debug`/`IP: 0xA0000020`, Read `0xF0000480 → 0x00080001`.
- 상세 기록: `C:\work\tas_server_analysis\M8_MEM_SPACES_STUB_PARITY.md`
  (화면/발췌: `gui_test/m8_main.png`, `m8_read_regression.png`, `smoke_run_m8.txt`).

### M9 검증 (2026-10-18) — 열거(system/device/core) 원본 교차 캘리브레이션 (읽기 전용)

- 프록시 원샷 덤프(qry_systems/devices/cores)로 원본 열거 전량 관찰: 시스템 `Real HW`만 채움,
  디바이스 `TC27x`·devtype 0x401DA083·**devid 0**·localhost:24817, 코어 3개 —
  CPU0 `1.6E`/`core_id 0x00B7C002`, CPU1/2 `1.6P`/`core_id 0x00C0C012`(core_type 44 공통).
- 수정: 코어별 이름·core_id 분리, `device_id`→0, 시스템 엔트리 host/port 공백.
- 검증: smoke 전 항목 원본 일치(SMOKE OK) · Core Select 행이 정상 시절 crop_09/10과 일치
  (`m9_select.png`) · `State: Debug`/`IP: 0xA0000020` · Read `0xF0000480→0x00080001`.
- 상세 기록: `C:\work\tas_server_analysis\M9_ENUMERATION_CROSS_CALIBRATION.md`
  (화면/발췌: `gui_test/m9_select.png`, `m9_main.png`, `m9_read_regression.png`, `smoke_run_m9.txt`).

### M10 검증 (2026-10-18) — 리셋 클래스(rst_classes) 원본 교차 캘리브레이션 (읽기 전용)

- 원본 관찰(프록시): `rst_classes` 벡터 **`0x7`** — class 0 "Power-On Reset (with PORST pin)",
  class 1 "System Reset", class 2 "Application Reset" (우리는 `0x1`+"System Reset"이었음 → 수정).
- 수정: 벡터 `0x7` + 3개 클래스 문구 정합 (`rst_class_info` 0..2). `mcd_rst_f`는 스텁 유지(실행 없음).
- 검증(smoke): `rst class vector 0x00000007` + `rst_info[0..2]` 문구 일치 · **SMOKE OK (0 failures)**.
  GUI 애태치 정상(`State: Debug`/`IP: 0xA0000020`, `m10_main.png`).
- 상세 기록: `C:\work\tas_server_analysis\M10_RST_CLASSES_CROSS_CALIBRATION.md`
  (화면/발췌: `gui_test/m10_main.png`, `smoke_run_m10.txt`).

### M11 검증 (2026-10-18) — Run(HALT=00b) 실관찰 캘리브레이션 (승인 하 원본 GUI Run/Stop)

- 원본(프록시) 관찰: `mcd_run_f -> 0` → `qry_state state=1 (RUNNING)` · PC `0xA0000020`→`0x8003A1xx..`
  진행 · GUI `State: Running`; `mcd_stop_f -> 0` → `state=3 (DEBUG)` 복귀, 정지 직후
  **`event=0x10`(STOPPED) 1회**·`trig_set_state active=1` 전환(동적 값).
- 원본은 DBGSR 재판독 0회(내부 모델) — 우리는 세션당 1회 판독(상태 소스), 값은 동일.
- 우리 DLL 실측: 실행 중 애태치 → `DBGSR=0x00001001 (HALT=00b) → RUNNING`(GUI `Running`/IP 진행);
  정지 복원 후 `DBGSR=0x00001003 (HALT=01b) → DEBUG`. **M5 미실측 항목 해소.**
- 캐시 모델: 외부 정지 시 세션 표시 유지(IP 고정) — 원본 내부 모델과 정합.
- 코드 변경 없음(M5 매핑 검증만). 상세 기록: `C:\work\tas_server_analysis\M11_RUN_STATE_CALIBRATION.md`
  (화면/발췌: `gui_test/m11_*.png`).

### M12 검증 (2026-10-18) — GUI Write 검증 (승인 하 타깃 메모리 쓰기)

- 스크래치 셀(LMU `0x90000000`, 기준값 0)에서 절차 검증: 기준 Read 0 → 패턴 Write →
  Read-back 일치 → **원값(0) 복원** → 최종 Read 0. **최종 상태 = 초기값, 타깃 정지 유지.**
- 우리 DLL: Write `0xA5C35A3C` → ack `00 24 01 80`(PL0_OK) → Read-back `3C 5A C3 A5` 일치.
  원본(프록시): Write `0x3C5AA55A` → `-> 0`, `num_tx_ok=1` → Read-back `[5A A5 5A 3C]` 일치.
  양쪽 `width=118`(클라이언트가 채우는 비정상 값) 동일 — 파리티.
- **부수(M6 정정)**: 실패 Read 직후 모달이 UI·폴링을 블록(양 스택 공통) — 캡처에 안 보임,
  **Escape 1회로 닫힘·폴링 재개**, Escape(모달 없을 때)는 앱 종료, 창 ID는 재조회 필요.
- 상세 기록: `C:\work\tas_server_analysis\M12_GUI_WRITE_VERIFICATION.md`
  (화면/발췌: `gui_test/m12_*.png`, `m12_dropin/modal_notes.txt`).

## 알려진 한계 (M2)

- ~~`mcd_open_core_f`의 성공 경로 미실측~~ → **해결(2026-10-18)**: 실패 원인은 환경(USB 허브 경유) —
  본체 직결 복구 후 애태치 성공 검증(`smoke_attach_ok.txt`, M3 GUI).
- ~~AM0 주소 맵 미포함~~ → **해결**: 복구 후 `addr_map_mask=0x8001`(AM0|AM15) 확인됨.
- `mcd_qry_state_f`는 **DBGSR 기반 실상태**(M5 완료 — `M5_QRY_STATE_DBGSR.md`): 정지(HALT=01b→`DEBUG`)
  실측 완료; Run(HALT=00b) 실관찰 캘리브레이션은 타깃 상태 전환 승인 필요.
- 메모리 공간 1개(AM0, id=1 잠정). 캐시/가상 공간·메모리 블록은 미노출.
- `mcd_execute_txlist_f` 중단 시 이미 전송된 프레임은 취소 불가(서버가 전량 실행) — 집계 결과만 중단 시점 기준.
- 트리거(M18)·리셋(M17)·run/stop(M16)·**step(M22)**은 실구현; 채널/trace API는 원본도 미구현(파리티 스텁),
  `run_until`은 미구현(시간 소스 부재).
- 스레드 안전성: 단일 전역 임계구역으로 직렬화(재진입 없음).
- 매핑 상수(TC27x 이름, core_id 등)는 잠정 — 실 `mcdxdas.dll`과 교차 캘리브레이션 TODO.

## 로드맵

- **M2 (완료, 2026-10-17)**: `mcd_open_core_f`/`close_core_f`(세션 + 애태치 시퀀스),
  `mcd_qry_mem_spaces_f`, `mcd_execute_txlist_f`(PL0 프레임), `mcd_qry_state_f`, `mcd_qry_max_payload_size_f`.
  잔여: 어댑터 정상화 후 `--attach` 성공 경로 실측(DC 성공 시 `addr_map_mask` 0x8001 전환 확인 포함).
- **M3 (완료, 2026-10-18)**: `mcd_basic_client.exe` drop-in GUI 검증(애태치·Core Info·Read 2건).
- **M4 (완료, 2026-10-18)**: State/IP 실값 복구 — `qry_reg_map`(PC @0xF881FE08) + `qry_state` DEBUG,
  클라이언트 폴링 파리티(4.6 Hz), GUI Read 회귀 0.
- **M5 (완료, 2026-10-18)**: TC27x 디버그 레지스터 규명(PC/DBGSR — `TC27X_DEBUG_REGISTERS.md`) +
  `qry_state` 실상태(DBGSR 세션 1회 판독·캐시, HALT→DEBUG/RUNNING — `M5_QRY_STATE_DBGSR.md`).
  잔여: Run 실관찰 캘리브레이션(승인 필요)·오류 표시 조합 캘리브레이션·reg groups/trig 확장·
  (승인 후) Write 및 타깃 제어
- **M6 (완료, 2026-10-18)**: 읽기 실패 오류 표시 캘리브레이션 (`M6_ERROR_DISPLAY_CALIBRATION.md`) —
  원본 프록시로 (ret, err) 조합이 이미 정확함을 확인, error string을 원본 정본 문구
  ("Read failure for a transaction of the txlist")로 정합. 잔여: Run 실관찰 캘리브레이션(승인 필요)·
  reg groups/trig 확장·(승인 후) Write 및 타깃 제어.
- **M7 (완료, 2026-10-18)**: reg_map/reg_groups 원본 교차 캘리브레이션
  (`M7_REG_MAP_CROSS_CALIBRATION.md`) — 원본 "CPU Registers" 40개(PCXI/PSW/PC/…/D0–D15/A0–A15)
  전량 재현, 호출 패턴·space=0 폴링·qry_state 필드 정합. 잔여: Run 실관찰(승인 필요)·
  mem_spaces/blocks 교차(선택)·(승인 후) Write 및 타깃 제어.
- **M8 (완료, 2026-10-18)**: mem_spaces 교차 + mem_blocks/스텁 정본 문구 정합
  (`M8_MEM_SPACES_STUB_PARITY.md`) — "Code and Data"(id 0)·opts 0x200, mem_blocks 미구현 오류,
  스텁 문구 "Function is not implemented". 잔여: Run 실관찰(승인 필요)·(승인 후) Write 및 타깃 제어.
- **M9 (완료, 2026-10-18)**: 열거(system/device/core) 교차 캘리브레이션
  (`M9_ENUMERATION_CROSS_CALIBRATION.md`) — 코어별 "1.6E/1.6P"·core_id(0x00B7C002/0x00C0C012)·
  device_id=0·시스템 엔트리 공백. 잔여: Run 실관찰(승인 필요)·(승인 후) Write 및 타깃 제어.
- **M10 (완료, 2026-10-18)**: 리셋 클래스 교차 캘리브레이션
  (`M10_RST_CLASSES_CROSS_CALIBRATION.md`) — 벡터 0x7 + 3개 클래스 문구 정합.
  클라이언트가 호출하는 함수 기준 교차 캘리브레이션 전 항목 완료. 잔여: Run 실관찰(승인 필요)·
  (승인 후) Write 및 타깃 제어.
- **M11 (완료, 2026-10-18)**: Run 실관찰 캘리브레이션 (`M11_RUN_STATE_CALIBRATION.md`) —
  승인 하 원본 GUI Run/Stop으로 실행 중 `State: Running`/IP 진행·정지 복귀 실증,
  DBGSR RUNNING 경로(M5 미실측) 해소.
- **M12 (완료, 2026-10-18)**: GUI Write 검증 (`M12_GUI_WRITE_VERIFICATION.md`) —
  LMU 스크래치 셀 쓰기·Read-back·원값 복원 + 원본 파리티, 실패 모달 재규명(M6 정정).
  **캘리브레이션·검증 항목 전부 완료** — 추가 작업은 새 요구사항에 한함.
- **M13 (확인, 2026-10-18)**: MCDS Trace Viewer 독립성 확인 (`M13_MCDS_TRACE_VIEWER_CHECK.md`) —
  Trace Viewer는 `mcdxdas.dll`을 로드하지 않음(자체 TAS 클라이언트 + `mcds_recorder.dll`;
  같은 폴더에 우리 DLL 배치·연결 후에도 미로드) → DLL 드롭인과 무관.
- **M14 (참고, 2026-10-18)**: `mcds_recorder.dll` 정적 분석 (`MCDS_RECORDER_ANALYSIS.md`) —
  22개 `mcdsrl_*` API · 내장 TAS TCP 클라이언트(WS2_32 **서수 임포트**) · MCDS 설정/녹화/`.mcds` 파일 기능.
- 참고 문서: `C:\work\tas_server_analysis\`(TAS 프로토콜 캡처·분석), `tas-sw` 저장소 문서

## 라이선스

- `include/` 하위 SPRINT 헤더: 원 SPRINT 라이선스(저작권 고지 유지, 수정 없이 배포).
- 프로젝트 자체 코드(`src/`, `tests/`, `tools/`): MIT.
