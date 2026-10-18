# MCD API over TAS — 프로토타입 (`mcd_tas`) 구현·검증 노트 (2026-10-17 ~ 18)

`mcdxdas.dll`(DAS 백엔드)과 **동일한 MCD API v1.1 "SPRINT Release" 계약(54개 함수)을
TasServer(24817) 백엔드로** 구동하는 프로토타입 DLL(`mcd_tas.dll`)의 구현 범위와 실서버 검증 결과.

- 구현 소스(C++)는 `mcd_tas/`(본 저장소 포함, M6부터 공개) — 검증 로그·증거는 마일스톤 폴더(`mNN_dropin/`).
- 프로토콜 근거: ① 본 저장소 캡처 해독 [TAS_PROTOCOL_CAPTURE.md](TAS_PROTOCOL_CAPTURE.md) §8
  ② 공식 `tas_client_api` 소스(`tas_pkt.h`, `tas_pkt_handler_rw.cpp`, `tas_client_server_con.cpp`) — 양쪽 상호 일치 확인.
- 원칙: **읽기 전용 기본** — Run/Stop(M16)·Reset(M17)·트리거(M18)는 사용 승인 하 실구현·검증 완료;
  `step`/`run_until`은 미구현(스텁, DAP 필요), CHL/trace API는 원본도 미구현(파리티 스텁).

## 0. 상태 요약

| 마일스톤 | 내용 | 상태 |
|---|---|---|
| M1 | 초기화·서버·시스템/디바이스/코어 열거 (11함수) | 완료·검증 |
| M2 | 코어 열기(세션 + 핫 애태치)·메모리 공간 열거·PL0 읽기/쓰기 (6함수) | 완료·검증 — 애태치 성공 경로 확보(§3-D; §3-C의 실패는 USB 허브 환경 문제였음) |
| M3 | 원본 `mcd_basic_client.exe` drop-in(`mcdxdas.dll` 자리) GUI 구동 | **GUI 검증 완료(§3-E)** — Select/애태치/Core Info/Read |
| M4 | State/IP 실값 복구 (`qry_reg_map` PC 레지스터 + `qry_state` 캘리브레이션) | **완료·검증(§3-F)** — `State: Debug / IP: 0xA0000020` (정상 시절 파리티) |
| (참고) | TC27x 디버그 레지스터 규명 — PC `0xF881FE08`·DBGSR `0xF881FD00` (해독·라이브 검증) | 완료(§3-G) — [TC27X_DEBUG_REGISTERS.md](TC27X_DEBUG_REGISTERS.md) |
| M5 | `qry_state` 실상태 — DBGSR 기반 도출 (세션 1회 판독·캐시) | **완료·검증(§3-H)** — `State: Debug` 파리티 · 4.6 Hz · 실패 0 |
| M6 | 읽기 실패 오류 표시 캘리브레이션 — (ret, err) 조합이 이미 정확함을 원본 프록시로 확인; error string을 원본 정본 문구로 정합 | **완료·검증(§3-I)** — `FAIL: Read failure for a transaction of the txlist` · 원본 직접 실행에서도 표시 동작 동일(모달 재현 불가) |
| M7 | reg_map/reg_groups 원본 교차 캘리브레이션 — 원본 "CPU Registers" 40개 전량 재현 + qry_state 필드 정합 | **완료·검증(§3-J)** — 호출 패턴 일치(`cap=0→40` · `start=0/1/2` · `space=0` PC 폴링) |
| M8 | mem_spaces 교차 + mem_blocks/스텁 정본 문구 정합 | **완료·검증(§3-K)** — `"Code and Data"`(id 0)·opts 0x200 · mem_blocks 미구현(0x100) · SMOKE OK |
| M9 | 열거(system/device/core) 교차 캘리브레이션 | **완료·검증(§3-L)** — 코어별 `1.6E/1.6P`·core_id(0x00B7C002/0x00C0C012)·devid 0 · Core Select 정상 시절 일치 |
| M10 | 리셋 클래스(rst_classes) 교차 캘리브레이션 | **완료·검증(§3-M)** — 벡터 `0x7` + 3개 클래스 문구 · 클라이언트 호출 함수 기준 교차 완료 |
| M11 | Run(HALT=00b) 실관찰 캘리브레이션 (승인 하 원본 GUI Run/Stop) | **완료·검증(§3-N)** — 실행 중 `state=1`/IP 진행 · 정지 복귀 · DBGSR RUNNING 경로 실증 |
| M12 | GUI Write 검증 (승인 하 타깃 메모리 쓰기) — LMU 스크래치 셀 쓰기/Read-back/원값 복원 + 원본 파리티 | **완료·검증(§3-O)** — `PL0_OK` · read-back 일치 · 최종 원값 복원 · 실패 모달 재규명(M6 정정) |
| M16 | run/stop 실구현 — DBGSR 쓰기(run=0x4/stop=0x6)·내부 상태 모델·1회성 STOPPED 이벤트 + trig_set 성공 no-op | **완료·검증(§3-P)** — smoke 라이브(DBGSR 독립 재판독) + 원본 클라이언트 GUI Run/Stop 전 구간 |
| M17 | reset 실구현 — DEVICE_CONNECT 재전송(0x0003 → 필요시 run 해제)·DBGSR 재판독 검증(M17b: DE 비트 수정) | **완료·검증(§3-Q)** — 리셋 벡터(0xA0000020)·GUI Reset/Reset-and-Halt/Stop 전 구간 |
| M18 | 트리거(브레이크포인트) 실구현 — OCDS TR0..TR7 직접 프로그래밍(PC/데이터·단일/범위·halt) | **완료·검증(§3-R)** — PC 일치 정지(EVTSRC=TR0EVT)·GUI IP Trigger 전 라이프사이클 |
| M19 | trace·잔여 함수 원본 교차 캘리브레이션 — 원본 DLL 직접 프로브(신규) | **완료·검증(§3-S)** — 12/12 라인 완전 일치(trace 무음 오류·core_modes 2종·max_payload 16384·trig_info) |
| M20 | SPR 로컬→전역 변환 — 로컬 DSPR/PSPR 주소를 코어별 버스 별칭으로 해석(M15 잔여 GAP) | **완료·검증(§3-T)** — 수정 전 PL0 오류 → 수정 후 원본 파리티(읽기+쓰기) · GUI 로컬 주소 쓰기/읽기/복원 · **M20b**: reg_map 코어별 PC(원본 교차 40/40) |

## 1. 구현 범위

54개 함수 중 **실동작 34개 + 참조 파리티 오류 8개 + 고정 성공 3개 + 스텁 9개**(미구현은 `MCD_ERR_FN_UNIMPLEMENTED` + 함수명 메시지, `mcd_qry_error_info_f`로 조회) — M19 기준. 스텁 9개 중 CHL 5종+`qry_input_handle`은 **원본도 스텁/미export**라 파리티.

- **M1 (11)**: `mcd_initialize_f` `mcd_exit_f` `mcd_qry_servers_f` `mcd_open_server_f` `mcd_close_server_f`
  `mcd_set_server_config_f` `mcd_qry_server_config_f` `mcd_qry_systems_f` `mcd_qry_devices_f`
  `mcd_qry_cores_f` `mcd_qry_error_info_f`
- **M2 (6)**: `mcd_open_core_f` `mcd_close_core_f` `mcd_qry_mem_spaces_f` `mcd_execute_txlist_f`
  `mcd_qry_state_f` `mcd_qry_max_payload_size_f`
- **M4 (1)**: `mcd_qry_reg_map_f` (group 0/1 → 단일 `PC` 레지스터; `mcd_qry_reg_groups_f`도 'CPU' 그룹·1레지스터로 보강)
- **M16 (2+2)**: `mcd_run_f` `mcd_stop_f` 실구현(DBGSR 쓰기) · `mcd_activate_trig_set_f`
  `mcd_remove_trig_set_f` 성공 no-op (트리거 세트는 항상 비어 있음 — 원본 클라이언트 Run 시퀀스 대응, §3-P)
- **M17 (1)**: `mcd_rst_f` 실구현 — DC 0x0003(+필요시 run 해제) 재전송, DE=1 유지, DBGSR 재판독 검증(§3-Q)
- **M18 (4+4)**: `mcd_create_trig_f` `mcd_qry_trig_f` `mcd_remove_trig_f` `mcd_qry_trig_state_f` 실구현
  (OCDS TR0..TR7) + `qry_trig_info`·`qry_trig_set`·`qry_trig_set_state`·`remove_trig_set` 실데이터(§3-R)
- **M19 (교차 캘리브레이션)**: trace 4종 무음 오류 파리티 · `qry_core_modes` 2종 · `qry_max_payload_size` 16384 ·
  `qry_reg_compound`/`qry_current_time`/`qry_device_description` 0x100 · `qry_trig_info` 실측값(§3-S)

### 1.1 M2 동작 요약

- `mcd_open_core_f` — 코어당 **독립 TCP 연결 + 세션**(최대 4코어, 같은 타깃+코어는 재사용). 시퀀스는 §2.
- `mcd_execute_txlist_f` — 읽기/쓰기(R/W)만. `access_width` 0이면 정렬 기반 자동 분할(≤8B/아이템),
  읽기 블록은 RDBLK(8정렬·8배수·≤1024B), 프레임 한도(32건/1060B/1040B — §8.2 실측값)는 내부에서 자동 분할.
  첫 실패 트랜잭션 기준으로 `num_tx_ok`/`num_bytes_ok` 집계 후 중단 보고.
- `mcd_qry_mem_spaces_f` — 메모리 공간 1개: id **1** = AM0(TAS 주소 맵 0 = 타깃 물리 접근), physical, 8bit MAU, LE.
- `mcd_qry_reg_map_f` (M4) — group 0/1에 단일 `PC` 레지스터(AM0, `0xF881FE08`, 32bit) 제공.
  원본 `mcd_basic_client.exe`는 이 주소를 폴링해 `Instruction Pointer`를 표시(요청 용량 cap 준수).
- `mcd_qry_state_f` — **`MCD_CORE_STATE_DEBUG` 고정 반환**(M4 캘리브레이션 — UI 파리티. 실상태 도출은 후속).
- 에러 매핑: TAS PL1 `0x46→DEVICE_LOCKED`, `0x44/0x48→DEVICE_ACCESS`, `0x32→SERVER_LOCKED`,
  기타→`CONNECTION`(0x200) — 원 TAS 코드는 오류 메시지에 포함.

## 2. 애태치 시퀀스 (= 참조 클라이언트 conn 3341과 동일 순서)

| # | 메시지 | 내용 |
|---|---|---|
| 1 | `SERVER_CONNECT`(0x80) | 클라이언트 이름 등록 |
| 2 | `GET_TARGETS`(0x86) | 타깃(access HW) 열거 |
| 3 | `SESSION_START`(0xA1) | identifier + client_type=1 → 응답에 con_info 124B(§8.2) |
| 4 | `PING`(0xA2) | 동일 132B 스냅샷 갱신 |
| 5 | PL0 RDBLK ×3 (AM15) | `0xD000`(104B)·`0x0000`(96B)·`0x8000`(168B) — 참조 클라이언트 패리티(디스크립터 블록) |
| 6 | `SET_DEVICE_KEY`(0xAD) | `ulo=0x0001`(HOT_ATTACH), 키 없음(`01 ad 00 00 01 00 00 00`) |
| 7 | `DEVICE_CONNECT`(0xA8) | `option=0`(리셋·halt 요청 없음; `01 a8 00 00 00 00 00 00`) → rsp에 `feat_used`, `device_type` |

→ 이후 **AM0(타깃) 접근** 가능. 캡처 전수(PL0 프레임 31,562)에서 **AM0 프레임이 있는 연결은 전부 DC 선행** —
AM0 접근의 전제 조건. AM15(서버 내부)는 DC 없이 SS 직후부터 읽을 수 있다
(식별 시 `addr_map_mask=0x8001`=AM0|AM15, 미식별 시 `0x8000`=AM15만).

## 3. 검증 결과 (2026-10-17, 실서버 localhost:24817)

### A. 프로토콜 프로브 — 읽기 전용(DC 없음), 전 항목 ok → [mcd_tas_probe.log](../logs/mcd_tas_probe.log)

```
  [ok] SERVER_CONNECT: 'TasServer' ver 0.2 protos 0x00000002 date 'Apr 30 2026'
  [ok] GET_TARGETS: 1 target → 'DAS JTAG AURDUINO 46YPEADW' devtype 0x00000000 nclient 0
  [ok] SESSION_START: max pl2 rq 1060 rsp 1040, max_rw 32, map_mask 0x8000, hash NoUid
  [ok] PING
  [ok] AM15 RD32 @0x000C0100 (ACC_HW_FREQUENCY) = 400000   ← §8 캡처 값과 정확히 일치
  [ok] AM15 RDBLK @0xD000 (104B) / @0x8000 (168B, "DAS JTAG AURDUINO…" 디스크립터)
```

→ SS 응답 파싱과 PL0 인코더(컨텍스트 아이템/BASE32/RDBLK)가 실서버에서 캡처와 동일하게 동작함을 확인.

### B. DLL 스모크 — 읽기 전용(열거만) → [mcd_tas_smoke.log](../logs/mcd_tas_smoke.log)

전 항목 ok(회귀 없음). DLL은 실 MCD 툴과 같은 방식(`LoadLibrary`/`GetProcAddress`)으로 로드해 시험.

### C. 애태치 시도 (`--attach`, 승인 하 실행) → [mcd_tas_attach.log](../logs/mcd_tas_attach.log)

> **정정(2026-10-18): 아래 실패는 환경 문제(USB 허브 경유로 인한 TAP 미응답 고착)였음 — 본체 직결 후 성공으로 대체(§3-D).**

- `SESSION_START`/`PING`/`SET_DEVICE_KEY`/AM15 정보 읽기까지 모두 성공.
- `DEVICE_CONNECT`만 `tas_err=0x20`(TAS_PL1_ERR_CMD_FAILED) 응답 — **요청 바이트는 §8 실캡처와 동일**, 프로토콜 문제 아님.
- 원인은 **환경(어댑터 미식별)**: 서버가 식별된 디바이스 없음을 보고(AM15 `0xD000` 디스크립터 전부 0,
  `addr_map_mask=0x8000`) + PnP에 `USB-SERIAL CH340 (COM3) Status=Unknown`(AURDUINO 어댑터).
- 실패는 `MCD_ERR_CONNECTION`(0x200)으로 보고됨(메시지에 tas_err 원코드 + "no identified device" 진단 포함).
- DC가 성공하지 못했으므로 **타깃 상태 변화 없음**.

### D. 애태치 성공 재검증 (2026-10-18) — USB 허브 원인 해결

- 근본 원인 확정: **USB 허브 경유로 인한 타깃 reset 고착**(동일 증상 포럼 보고와 일치) — 허브를 제거하고
  PC 본체 직결로 전환하자 즉시 복구(LocationPaths `...#USBROOT(0)#USB(3)#USB(2)` → `...#USBROOT(0)#USB(2)`, 13:06 재인식).
- `tas_probe --idcode`: devtype `0x401DA083`, hash `LBHJYL (0x4013A2AA)`, `device_id 47427060 11080040 64945FCB 20E65C06`,
  @0xD000 `"TriCore-Family"`, 네이티브 JTAG 스캔 전 아이템 err=0x80, IDCODE MATCH
  → [mcd_tas_probe_idcode_ok.log](../logs/mcd_tas_probe_idcode_ok.log)
- `mcd_tas_smoke --attach`: **DEVICE_CONNECT 성공**, `addr_map_mask=0x8001`(AM0|AM15), 'TC27x' 3코어,
  **RD32 @0xF0000480 = 0x00080001**(§2-#13 정상 시그니처와 일치)
  → [mcd_tas_attach_ok.log](../logs/mcd_tas_attach_ok.log)

### E. M3 — `mcd_basic_client.exe` drop-in GUI 검증 (2026-10-18)

원본 MCD Basic Client(DAS64)에 `mcd_tas.dll`을 `mcdxdas.dll` 자리로 배치(drop-in) — **GUI 전 구간 실동작**:

- Select → 애태치 → 코어 열기: 타이틀 `DAS JTAG AURDUINO 46YPEADW`, 로그 `session attached(0x401DA083) → core 0 opened('TC27x')`
  ([m3_dropin/select.png](../m3_dropin/select.png), [main_after_attach.png](../m3_dropin/main_after_attach.png)).
- Core Info 다이얼로그: 전 항목 = DLL 열거 값 (core `Core 0: CPU0 TriCore 1.6E`, core_type 44, core_id `0x00B7C002`)
  → [m3_dropin/coreinfo.png](../m3_dropin/coreinfo.png)
- GUI Read: `0x70000000` → `0x00000000` ([read_70000000.png](../m3_dropin/read_70000000.png));
  `0xF0000480` → **`0x00080001`** — 정상 시절 시그니처와 일치 ([read_f0000480.png](../m3_dropin/read_f0000480.png)).
  발췌 로그: [m3_dropin/log_excerpt.txt](../m3_dropin/log_excerpt.txt)
- 잔여 차이였던 State/IP 필드(`Unknown`/`XXXXXXXX`)는 **§3-F(M4)에서 해소**.
- 실무 노트(주소 `0x` 접두 필수 = base-0 파서, 모달 다이얼로그, 픽셀 클릭 등):
  [MCD_BASIC_VS_CURRENT_COMPARE.md](MCD_BASIC_VS_CURRENT_COMPARE.md) §5

### F. M4 — State/IP 실값 복구 (2026-10-18) — 정상 시절 파리티

정상 시절 캡처(CONN3341) 폴링의 3번째 프레임 = **AM0 RD32 `0xF881FE08` → `0xA0000020`** (UI `Instruction Pointer`와 일치).
`0xF881FE08`은 원본 `mcdxdas.dll`이 제공하는 장치별 디버그 레지스터 주소. 계측(`MCD_TAS_LOG=1`)으로 규명한 클라이언트 동작:

- 코어 열기 직후 `mcd_qry_reg_map_f`(**group id 0**)로 등록된 PC 레지스터를 얻고, 매 ≈220 ms 사이클
  `mcd_qry_state_f` + 그 주소 RD32 폴링으로 UI(State/IP)를 갱신 — **UI 갱신은 성공 폴링에 게이트**
  (빈 레지스터 맵 → 기본값 `RD16 @(space 0, 0x0)` 폴링 → 0x8D 실패 → 갱신 정지가 M3 잔여 차이의 원인).
- 수정: `qry_reg_map`(group 0/1)이 `PC` @ `0xF881FE08`(AM0, 32bit) 반환 + `qry_state` = `DEBUG`.
  구현 결함 1건(요청 cap=1에 2개 기록 — 용량 초과)도 단일 레지스터로 수정.

검증 결과:

- 클라이언트가 즉시 `RD32 @0xF881FE08` 폴링(**4.6 Hz**, 정상 시절 4.4 Hz와 동일) — 응답 `0xA0000020`, **오류 0**
  (`rq: 01 B0 FF 00 C4 FF 01 00 00 15 81 F8 00 25 08 FE 00 B1 01 00` → `rsp: 00 B0 88 08 01 25 01 80 20 00 00 A0 00 B1 C4 FF`).
- UI: **`State: Debug` / `Instruction Pointer 0xA0000020`** ([m4_dropin/state_ip_recovered.png](../m4_dropin/state_ip_recovered.png))
  — 정상 시절 [mcds/crop_13_after_read.png](../mcds/crop_13_after_read.png)와 동일. MCD Events 비어 있음(실패 0).
- 회귀: GUI Read `0xF0000480` → `0x00080001` ([m4_dropin/read_regression.png](../m4_dropin/read_regression.png)).
- 발췌 로그: [m4_dropin/log_excerpt.txt](../m4_dropin/log_excerpt.txt)

후속: `mcd_qry_state_f` 실상태 도출 — 디버그 레지스터(DBGSR) 규명 완료(§3-G), Run/Halt 실관찰
캘리브레이션 승인 필요. 읽기 실패 시 모달 경고 유무 차이 캘리브레이션.

### G. M4 후속 — TC27x 디버그 레지스터 규명 (2026-10-18)

클라이언트가 폴링하는 `0xF881FE08`과 정상 시절 최초 1회 판독된 `0xF881FD00`의 정체를
TC27x D-Step UM(+TC1.6 코어 매뉴얼)으로 규명하고, 드롭인 GUI Read(읽기 전용)로 라이브 검증.

- 주소 구조: `0xF8810000–0xF881FFFF` = **CPU0 CSFR 창**(UM Table 3-3) → 오프셋 `FE08H` = **PC**,
  `FD00H` = **DBGSR**(Debug Status Register).
- 라이브 Read 3/3 일치: `0xF881FD00` = `0x0000101B`(HALT=01b 정지 · DE=1 · SIH=1 · EVTSRC=16=TR0EVT),
  `0xF881FE08` = `0xA0000020`(UI IP와 동일), `0xF881FE18` = `0x00B7C002`(CPU_ID TC1.6E 시그니처).
  정상 시절 캡처 `0x1013`도 동일 해독(SIH=0 차이).
- 함의: `mcd_qry_state_f` 실상태 구현 경로 확보 — `DBGSR.HALT[2:1]` → Run/Halt, `EVTSRC` → 정지 소스.
- 전체 해독·출처: **[TC27X_DEBUG_REGISTERS.md](TC27X_DEBUG_REGISTERS.md)** ·
  화면: [tc27x_dbg_regs/](../tc27x_dbg_regs/)

### H. M5 — `qry_state` 실상태 (DBGSR 기반) (2026-10-18)

`mcd_qry_state_f`를 DEBUG 고정에서 **타깃 실상태 도출**로 교체 — 첫 호출 시
DBGSR(`0xF881FD00`)을 1회 RD32(읽기 전용)로 읽어 해독·캐시(세션 단위).
트래픽 정합: **세션당 1회 판독**(정상 시절 캡처와 동일), 이후 사이클은 캐시 반환.

- 매핑: `HALT[2:1]`=01b→`MCD_CORE_STATE_DEBUG` / 00b→`RUNNING` / 그 외(쓰기 코드)→`UNKNOWN`;
  `EVTSRC`(16..23)→`trig_id`·`stop_str`("Trigger n (TRnEVT)"), 0/1/2→EXEVT/CREVT/SWEVT;
  `info_str`=DBGSR 값.
- 실패 시: DEBUG 폴백(로그 1회)+다음 호출 재시도 — 클라이언트 UI 갱신 경로 보존.
- 검증(`client_stderr13.log`, PID 30980): 판독 1건 `0x0000101B → DEBUG (cached)`, 누적 525회 호출 무실패,
  `State: Debug`/`IP: 0xA0000020` 파리티, 폴링 **4.6 Hz**, GUI Read `0xF0000480→0x00080001` 회귀 유지.
- 증거: [m5_dropin/](../m5_dropin/) (state_main · read_regression · log_excerpt.txt)
- 한계: 타깃 정지 상태(HALT=01b)라 RUNNING 경로 미실측 — 실관찰 캘리브레이션은 별도 승인 필요.

### I. M6 — 읽기 실패 오류 표시 캘리브레이션 (error string 정합) (2026-10-18)

원본 `mcdxdas.dll` 프록시 비교로 실패한 사용자 Read의 (ret, err) 조합이 우리 구현과
**비트 단위 동일**임을 확인: `MCD_RET_ACT_HANDLE_ERROR(3)` / `MCD_ERR_TXLIST_READ(0x2100)` /
`events=0` / `num_tx_ok=0` / `num_bytes_ok=0`. 따라서 (ret, err) 변경 **불필요** —
캘리브레이션은 `error_str` 문구 정합으로 완료.

- 원본 보고 문구(정본): `qry_error_info` → `"Read failure for a transaction of the txlist"`
  (Infineon MCD API `MCD_ERR_TXLIST_READ` 정본 문구).
- 수정: `mcd_execute_txlist_f` 항목 실패 분기의 `err_set` 메시지를 원본 정본 문구로 교체
  (Read/Write), 진단 상세(주소/PL0 err)는 별도 `dbg()`로 보존.
- 검증(`client_stderr16.log`, MCD_TAS_LOG=1): `FAIL: Read failure for a transaction of the txlist` →
  `CALL qry_error_info ('Read failure for a transaction of the txlist')` — 원본과 문구 일치.
- **재검토(추가 실험)**: 원본 DLL **직접(프록시 없이)**·주소 기본값 `0`/`0xE00000` 재현에서도
  모달·박스 표시가 나타나지 않음(우리와 동일) — "원본 모달 vs 우리 무표시" 차이는 현재
  환경에서 재현 불가. 정상 시절 와이어(`RD32 @0x0000`→`00 25 00 8D`, s2c cnt 0x014B)와
  우리 응답도 동일. 정상 시절 `crop_13` 모달의 트리거는 미상(참고 관찰).
  **[정정 — M12(§3-O)]**: 실패 모달은 양 스택 공통으로 표시·차단됨 — 캡처에 미표시 +
  실패 후 상호작용 미시도로 인한 오판이었다(모달은 Escape로 닫히고 폴링 재개).
- 성공 Read 회귀(로그 실증, `client_stderr17.log`): 요청 `B32:F000|R32:0480`(정상 시절과 동일
  와이어 형태) → 응답 `01 00 08 00`(0x00080001) — GUI 표시 확인.
- 증거: [m6_dropin/](../m6_dropin/) (proxy_log_excerpt.txt · wire_compare.txt · ours/orig screenshots)
- 상세: `M6_ERROR_DISPLAY_CALIBRATION.md` (tas_server_analysis, 로컬)

### J. M7 — reg_map/reg_groups 원본 교차 캘리브레이션 (2026-10-18)

프록시 확장(reg_map 인자·엔트리 덤프 + 첫 count 질의에서 **원샷 전체 덤프** + 그룹 프로브 +
trig_set_state 덤프)으로 원본 `mcdxdas.dll`의 레지스터 DB를 전량 관찰·재현.

- 원본: 그룹 `id=0 "CPU Registers"` **40개** — `PCXI/PSW/PC/BIV/BTV/ISP/ICR/FCX` +
  `D0–D15`(0xF881FF00..) + `A0–A15`(0xF881FF80..); 공통 `space=0`·size=32·type=SIMPLE·
  `rmask=wmask=0x2`, **`PC`만 side_w=1**.
- 클라이언트 패턴: `qry_reg_map(group=0, cap=0)` → 40; `start=0/1/2`를 1개씩(`cap=1`) 가져와
  `PC`를 찾으면 중단 → 이후 `RD32 space=0 @0xF881FE08`만 폴링.
- 부수 정합: `trig_set_state`는 원본도 전 필드 0(스텁과 동일) · `qry_state`는 원본이
  `stop_str=''`/`info_str=''`(M5에서 채웠던 필드를 공백으로 정정 — state만 실값).
- 수정: `g_reg_desc[40]` 테이블 + "CPU Registers" 그룹 + `qry_reg_map` count/fetch 서빙.
- 검증(`client_stderr18.log`): 호출 패턴이 원본과 일치, `State: Debug`/`IP: 0xA0000020`,
  성공 Read `0xF0000480 → 0x00080001` 회귀.
- 증거: [m7_dropin/](../m7_dropin/) (proxy_reg_map_dump.txt · our_client_log_excerpt.txt · 화면 2종)
- 상세: `M7_REG_MAP_CROSS_CALIBRATION.md` (tas_server_analysis, 로컬)

### K. M8 — mem_spaces 교차 + mem_blocks/스텁 정본 문구 정합 (2026-10-18)

- 원본 관찰(프록시 원샷 프로브): mem_spaces = **`id=0 "Code and Data"`** · `type=0` · mau=8 ·
  LITTLE · `0..0xFFFFFFFF` · **`opts=0x200`**(`MCD_TX_OPT_ATOMIC_WITH_NEXT`) ·
  mem_blocks = **미구현**(`HANDLE_ERROR` + `0x100 "Function is not implemented"`).
- 수정: mem_spaces 원본 값 정합(id 1→0·이름·type·opts) · mem_blocks → 원본과 동일한 미구현 오류 ·
  **스텁 문구 정본화**("Function is not implemented") · txlist space 0 + legacy 1 허용.
- 검증(smoke `--attach`, [m8_dropin/](../m8_dropin/) smoke_run.txt):
  `mem space id 0 'Code and Data' ... opts 0x200` ·
  `mem blocks rc 3: code 0x100 'Function is not implemented'` · **SMOKE OK (0 failures)** ·
  GUI 회귀 `State: Debug`/`0xF0000480→0x00080001`.
- 증거: [m8_dropin/](../m8_dropin/) (proxy_mem_dump.txt · smoke_run.txt · 화면 2종)
- 상세: `M8_MEM_SPACES_STUB_PARITY.md` (tas_server_analysis, 로컬)

### L. M9 — 열거(system/device/core) 교차 캘리브레이션 (2026-10-18)

- 원본 관찰(프록시 원샷 덤프): 시스템 `Real HW`만 채움 · 디바이스 `TC27x`·devtype 0x401DA083·
  **devid 0**·localhost:24817 · 코어 3개 — CPU0 **"TriCore 1.6E"**/`core_id 0x00B7C002`,
  CPU1/CPU2 **"TriCore 1.6P"**/`core_id 0x00C0C012` (core_type 44 공통).
- 수정: 코어별 이름·core_id 분리(DB 테이블 2-포맷/2-아이디) · `device_id`→0 ·
  시스템 엔트리 host/port 공백.
- 검증: smoke 전 항목 원본 일치(SMOKE OK) · Core Select 행이 정상 시절 crop과 일치 ·
  `State: Debug`/`0xF0000480→0x00080001`.
- 증거: [m9_dropin/](../m9_dropin/) (proxy_enum_dump.txt · smoke_run.txt · select/main/read_regression)
- 상세: `M9_ENUMERATION_CROSS_CALIBRATION.md` (tas_server_analysis, 로컬)

### M. M10 — 리셋 클래스(rst_classes) 교차 캘리브레이션 (2026-10-18)

- 원본 관찰(프록시): `rst_classes` 벡터 **`0x7`** — class 0 **"Power-On Reset (with PORST pin)"** /
  class 1 **"System Reset"** / class 2 **"Application Reset"** (우리는 `0x1`+"System Reset"이었음).
- 수정: 벡터 `0x7` + 3개 클래스 문구 정합. `mcd_rst_f`는 스텁 유지(타깃 상태 변경 없음).
- 검증(smoke): `rst class vector 0x00000007` + `rst_info[0..2]` 문구 일치 · **SMOKE OK**.
- **의의: 원본 클라이언트가 호출하는 함수 기준 교차 캘리브레이션 전 항목 완료**
  (systems/devices/cores/reg_map/state/mem_spaces/mem_blocks/rst_classes/trig_set_state).
- 증거: [m10_dropin/](../m10_dropin/) (proxy_rst_dump.txt · smoke_run.txt · m10_main.png)
- 상세: `M10_RST_CLASSES_CROSS_CALIBRATION.md` (tas_server_analysis, 로컬)

### N. M11 — Run(HALT=00b) 실관찰 캘리브레이션 (2026-10-18)

사용자 승인 하에 **원본 스택 GUI로만** Run/Stop 수행(우리 코드/스텁은 상태 변경 없음) —
목적: `qry_state` RUNNING 경로(M5 미실측) 실증.

- 원본(프록시): `mcd_run_f -> 0` → `state=1 (RUNNING)` · PC `0xA0000020→0x8003A1xx..` 진행 ·
  GUI `State: Running`; `mcd_stop_f -> 0` → `state=3 (DEBUG)` 복귀, 정지 직후 **`event=0x10`
  (STOPPED) 1회** · `trig_set_state active=1`(동적 값). DBGSR 재판독 0회(내부 모델).
- 우리 DLL: 실행 중 애태치 → `DBGSR=0x00001001 (HALT=00b) → RUNNING`(GUI `Running`/IP 진행);
  복원 후 `0x00001003 (HALT=01b) → DEBUG`. **M5 미실측 항목 해소 — 코드 변경 없음(검증만).**
- 증거: [m11_dropin/](../m11_dropin/) (proxy_run_stop.txt · our_state_log_excerpt.txt · 화면 7종)
- 상세: `M11_RUN_STATE_CALIBRATION.md` (tas_server_analysis, 로컬)

### O. M12 — GUI Write 검증 (승인 하 타깃 메모리 쓰기) (2026-10-18)

- 스크래치 셀(LMU `0x90000000`, 기준값 0) 절차 검증: 기준 Read 0 → 패턴 Write → Read-back 일치 →
  **원값(0) 복원** → 최종 Read 0. **최종 상태 = 초기값, 타깃 정지 유지.**
- 우리 DLL: `0xA5C35A3C` Write → ack `00 24 01 80`(`PL0_OK`) → Read-back `3C 5A C3 A5` 일치 ·
  원본(프록시): `0x3C5AA55A` Write → `-> 0`·`num_tx_ok=1` → Read-back `[5A A5 5A 3C]` 일치.
  양쪽 `width=118`(클라이언트가 쓰기에 채우는 비정상 값) 동일 — 파리티.
- **부수 발견(M6 정정)**: 실패 Read 직후 클라이언트는 **모달**로 UI·폴링을 블록(양 스택 공통) —
  이 환경에서 캡처에 렌더링되지 않아 종전에 "재현 불가"로 오판. **Escape 1회로 닫힘·폴링 재개**,
  모달 없이 Escape는 앱 종료, 이벤트 후 창 ID 재조회 필요(자동화 프로토콜).
- 증거: [m12_dropin/](../m12_dropin/) (our/orig write_test · 화면 5종 · modal_notes.txt)
- 상세: `M12_GUI_WRITE_VERIFICATION.md` (tas_server_analysis, 로컬)

### P. M16 — run/stop 실구현 + GUI Run/Stop 검증 (2026-10-18)

- **메커니즘**: DBGSR(`0xF881FD00`) 쓰기 — run=`0x00000004`(HALT[2:1]=10b, 해제) ·
  stop=`0x00000006`(HALT[2:1]=11b, 설정·DE=1) (TC1.6 UM Vol.1 §12.9;
  run 값은 CCL recorder `startCpu`의 `mcdsrl_write(DBGSR, 0x0004)`와 동일)
- **상태 모델**: 원본 파리티 — DBGSR 재판독 없이 내부 추적(run→RUNNING · stop→DEBUG +
  **1회성 `event=0x10`(STOPPED)**)
- **smoke 라이브 검증**(`--attach --runstop`): run→`DBGSR=0x00000001` · stop→`DBGSR=0x00000003`
  **독립 재판독 확인** · 상태/이벤트 시퀀스 전 항목 ok · **입력 상태 복원** · 실패 0
  → [m16_dropin/smoke_runstop.txt](../m16_dropin/smoke_runstop.txt)
- **GUI 검증**(원본 `mcd_basic_client.exe` + 우리 DLL): 클라이언트 Run 시퀀스 실측 =
  `remove_trig_set` → `activate_trig_set` → `run` — 스텁이던 trig_set 2종을 **성공 no-op**으로 구현 후
  전 구간 동작: Stop(Running→Debug·IP 고정) · Run(Debug→Running·IP 진행) · 최종 Debug 복원
  → [m16_dropin/](../m16_dropin/) (화면 4종 · 로그 발췌)
- 관찰: 상태 전환 직후 첫 폴링에 1회성 읽기 실패(PL0 0x8D→0x98) 표시 가능 —
  M6 원본 캘리브레이션과 동일 표시·자동 복구
- 참고: EVTSRC 차이(원본 stop=TR0EVT `0x10xx` vs DBGSR 쓰기=EXEVT `0x000x`) — 상태 판정과 무관

### Q. M17 — reset 실구현 + GUI 검증 (2026-10-18)

- **메커니즘**: 활성 세션에서 TAS DEVICE_CONNECT(0xA8) 재전송 — 공식 client API
  (`CTasClientServerCon::mDeviceConnect(dco)`, `tas_clnt_dco_et RESET/RESET_AND_HALT`)와 동일 계열
- **M17b(DE 비트)**: DC `0x0001`(RESET)은 리셋 후 `DBGSR.DE=0`을 남겨 이후 halt 쓰기가 하드웨어에서
  무시됨(무음 실패 발견 — GUI Stop 후 IP가 계속 진행). 수정: 항상 `0x0003`(RESET_AND_HALT) 후
  `rst_and_halt=FALSE`면 run 쓰기(0x4)로 해제(**DE=1 유지**); run/stop/rst에 **실제 DBGSR 재판독 검증** 추가
- **smoke 검증**(`--attach --rst`): 리셋 후 **PC=0xA0000020(리셋 벡터)·DBGSR=0x00001013** —
  2012 MCD 데모 로그의 `rsthlt` 결과와 동일 · reset-and-run → stop → `HALT=01b` ✓ · 실패 0
- **GUI 검증**: "Reset and Halt"→Debug(PC 0xA0000020) · "Reset"→Running(DE=1) ·
  **"Stop"→Debug·IP 고정**(M17b 수정 확인) → [m17_dropin/](../m17_dropin/) (화면 5종·로그 발췌·smoke 2종)
- **step 조사(2026-10-18)**: `mcd_step_f`는 raw TAS로 구현 불가 — 메커니즘은 CDC 하드웨어 싱글스텝
  (OCDS/DAP 경유; 원본은 vtable+0x160 내부 메서드). AM15(raw JTAG 프리미티브만)·DC 옵션·서버 문자열
  어디에도 노출 없음 → DAP/Cerberus 프로토콜의 AM15 raw JTAG 상 구현이 별도 서브프로젝트로 필요.

### R. M18 — 트리거(브레이크포인트) 실구현 (2026-10-18)

- **메커니즘**: OCDS 트리거 유닛 TR0..TR7 직접 프로그래밍(CPUx CSFR `TRxEVT 0xF000+8n`·`TRxADR 0xF004+8n`·
  `TRIG_ACC 0xFD30`) — TRxEVT = `BOD|BBM|EVTA=Halt`(0x1A) + `TYP=PC`(0x1000)/`ALD`(read)/`AST`(write)
  + 범위 시 `RNG`(0x2000) (TC1.6 UM §12.9 필드 검증; CCL 상수 0x1011은 EVTA=001 "관찰 전용"이라 정정)
- 범위 트리거 = 짝수 유닛 + 상위 이웃(TR1ADR=끝주소); `qry_trig_state`는 TRIG_ACC 판독(클리어) 후 모델 래치
- **smoke 검증**(`--attach --trig`): 브레이크포인트 @0x8003A160 → **PC=0x8003A160 정지·EVTSRC=16(TR0EVT)** ·
  captured=1 · 제거 후 세트 비움 · 실패 0
- **GUI 검증**: 클라이언트 Run 시퀀스 실측 = `remove_trig_set → create_trig×N → activate_trig_set → run`
  (매 Run 재다운로드) — Run → **State Debug·IP 0x8003A160·"ACTIVE"** · 체크 해제 후 Run → 제거(1 cleared) → 정상
  → [m18_dropin/](../m18_dropin/) (화면 3종·로그 발췌·smoke)
- 부수: run 검증에서 트리거 히트 구분(브레이크포인트 레이스 오탐 방지)

### S. M19 — trace·잔여 함수 원본 교차 캘리브레이션 (2026-10-18)

- **프로브 신규 확립**: smoke `--trace` 모드로 **원본 `mcdxdas.dll`(1.14.12)을 직접 로드**해 실측 —
  원본 로드 조건(실측): 빈 `system_key`(NULL이면 원본 크래시)·개행+따옴표 config·실버퍼
  (NULL 버퍼/코어 핸들에서 원본은 미방어 크래시 — 우리 DLL은 전부 오류 반환, 차이로 기록)
- **③ execute_command**: 원본 exports(55)에 **없음** — 미export 3종(`execute_command`·`set_global`·
  `qry_input_handle`; 원본은 대신 `mcd_das_*` 4개 export). 우리는 54개 전부 export(API-정합)
- **④ trace 4종**: 원본은 전부 **무음 HANDLE_ERROR**(rc 3·오류정보 없음·출력 미변경) → `err_silent` 파리티
- **부수 캘리브레이션**: `qry_core_modes` = **`1 'Supervisor'`·`2 'User'`**(실데이터 구현) ·
  `qry_reg_compound`/`qry_current_time`/`qry_device_description` = rc 3+0x100 ·
  `qry_max_payload_size` = **16384** · `qry_trig_info` = **0xF/0x3/0x1/num 8/swbp 1**
- **검증**: 원본 vs 우리 프로브 **12/12 라인 완전 일치** · smoke `--runstop`·`--rst`·`--trig` 회귀 ok ·
  GUI Run/Stop sanity — → [m19_dropin/](../m19_dropin/) (프로브 2종·GUI 화면·로그 발췌)

### T. M20 — SPR 로컬→전역 변환 (M15 잔여 GAP) (2026-10-18)

- **GAP**: 우리 DLL은 로컬 스크래치패드 주소(DSPR `0xD0000000`/PSPR `0xC0000000`)를 그대로 PL0에 실어
  **버스 오류(PL0 0x8D)** — 원본(DAS)은 대상 코어 기준으로 해석(M12 실증 → M15 매핑 확정, 구현만 잔여)
- **정본**: TC27x UM Table 5-10 — 코어별 `PSPR_base/DSPR_base`(0: `0x70100000/0x70000000` …
  2: `0x50100000/0x50000000`) · 로컬은 항상 `C0000000/D0000000`; 윈도는 파워오브투(128K/32K)로
  캐시 연속 매핑까지 커버(과소 변환만 위험)
- **구현**: `mcd_execute_txlist_f` 스플릿 루프에서 코어별 변환(`spr_translate`) — 읽기/쓰기 공통,
  `MCD_TAS_LOG`에 `local SPR ... -> bus ...` 라인
- **검증**: smoke `--spr`(읽기): 수정 전 rc 3(0x2100) → 수정 후 rc 0·ok 2, **원본 1.14.12와 동일**;
  `--sprw`(쓰기): 로컬 쓰기 → 버스 리드백 일치 → 원값 복원(원본 파리티) · 회귀 `--trace` ok
- **GUI**(원본 클라이언트 + 우리 DLL): `0xD0000000` Write/Read → 리드백 `0x5AA55AA5`(M15 북엔드) →
  복원 `0x00000000` — → [m20_dropin/](../m20_dropin/) (로그 6종·화면 3종·DLL 로그 발췌)
- **M20b — reg_map 코어별 PC**: `qry_reg_map`이 전 코어 `0xF881FE08`(CPU0)을 반환하던 것을
  코어별 CSFR 윈도(`+0x20000*core`)로 수정 — 원본 교차: core1 `0xF883FE08`·core2 `0xF885FE08`,
  **전 40개 +0x20000(40/40 일치)** · 라이브 PC 읽기 코어별 상이값 · smoke `--regmap`/`--core N` 신규
  — → [m20b_dropin/](../m20b_dropin/) (원본/우리 덤프 각 3종)

## 4. 구현 메모 (정본 근거)

- **프레임 빌더** (`tas_pkt_handler_rw.cpp` 규칙, §8 실측과 일치):
  `[PL2] START(0xB0: con_id=0xFF 자리표시자, protoc_ver=0, pl1_cnt — 0xFFC0 시작 프레임마다 +1, 사용 맵 마스크)`
  + 컨텍스트 아이템(**변경 시에만**: MAP 0x11 → BASE32 0x15/BASE64 0x16) + RW 아이템 + `END(0xB1: n)`
- **RW 아이템**: RD8/16/32/64(0x21/23/25/27), RDBLK(0x2C, 워드수 0=256; 1KB 응답은 0x2D + 1024B 예외 규칙),
  WR8/16/32(0x20/22/24), WR64(0x26), WRBLK(0x2A)
- **응답**: RD `[wl cmd wlrd err][데이터 wl×4]`, WR `[00 cmd wlwr err]` — PL0 `0x80`=OK, `0x8D`=DATA(버스 에러)
- **pl1_cnt**: 연결별 16비트 카운터, `PL0_END` 응답이 echo — 프레임 정합 검증에 사용
- **열거 매핑(잠정)**: TC27x(`0x401DA083`) → 3코어, `"Core %d: CPU%d TriCore 1.6E"`, core_type=44, core_id `0x00B7C002`
  — 실 `mcdxdas.dll`과 교차 캘리브레이션 TODO

## 5. 한계와 로드맵

- ~~**DC 성공 경로 미실측** — 어댑터 정상화 후 `--attach` 재실행 (성공 시 `device_type=0x401DA083`,
  `addr_map_mask` `0x8000→0x8001` 전환 기대).~~ → **해결(§3-D)**: USB 허브→본체 직결로 복구 —
  애태치 성공, `device_type=0x401DA083`·`addr_map_mask=0x8001` 확인.
- `mcd_qry_state_f`=DBGSR 기반 실상태(§3-H 완료) — Run 실관찰 캘리브레이션만 후속,
  메모리 공간 1개(AM0), 캐시/가상 공간·메모리 블록 미노출.
- 트랜잭션 리스트 중단 시 이미 전송된 프레임은 취소 불가(서버가 전량 실행) — 집계만 중단 시점 기준.
- 트리거/채널/트레이스/레지스터/리셋 등은 스텁. 단일 전역 임계구역 직렬화(재진입 없음).
- **M5(완료, 2026-10-18)**: `qry_state` 실상태(§3-H) — DBGSR 세션 1회 판독·캐시, HALT→DEBUG/RUNNING.
- **M6(완료, 2026-10-18)**: 읽기 실패 오류 표시 캘리브레이션(§3-I) — 원본 프록시로 (ret, err) 조합
  이미 정확함을 확인, error string을 원본 정본 문구("Read failure for a transaction of the txlist")로 정합.
- **M7(완료, 2026-10-18)**: reg_map/reg_groups 교차 캘리브레이션(§3-J) — 원본 "CPU Registers"
  40개 전량 재현 + qry_state 필드 정합(원본은 state만 실값).
- **M8(완료, 2026-10-18)**: mem_spaces 교차 + mem_blocks/스텁 정본 문구 정합(§3-K) —
  "Code and Data"(id 0)·opts 0x200 · mem_blocks 미구현(0x100).
- **M9(완료, 2026-10-18)**: 열거 교차 캘리브레이션(§3-L) — 코어별 "1.6E/1.6P"·core_id ·
  device_id=0 · 시스템 엔트리 공백.
- **M10(완료, 2026-10-18)**: 리셋 클래스 교차(§3-M) — 벡터 0x7 + 3개 문구. **클라이언트 호출
  함수 기준 교차 캘리브레이션 전 항목 완료.**
- **M11(완료, 2026-10-18)**: Run 실관찰(§3-N) — 실행 중 `State: Running`/IP 진행·정지 복귀,
  DBGSR RUNNING 경로 실증.
- **M12(완료, 2026-10-18)**: GUI Write 검증(§3-O) — LMU 스크래치 셀 쓰기/Read-back/원값 복원 +
  원본 파리티 · 실패 모달 재규명(M6 정정). **캘리브레이션·검증 항목 전부 완료.**
- **M13(확인, 2026-10-18)**: MCDS Trace Viewer는 `mcdxdas.dll`을 로드하지 않음(자체 TAS 클라이언트 +
  `mcds_recorder.dll`) — DLL 드롭인과 무관. 상세: [MCD_TOOLS_ANALYSIS.md](MCD_TOOLS_ANALYSIS.md) §3.8
  · [m13_dropin/](../m13_dropin/).
- **M16(완료, 2026-10-18)**: run/stop 실구현(§3-P) — DBGSR 쓰기·1회성 STOPPED 이벤트·trig_set no-op,
  smoke + GUI 전 구간 검증. **미구현 유지**: `rst`(→M17에서 구현)·`step`·`run_until`.
- **M17(완료, 2026-10-18)**: reset 실구현(§3-Q) — DC 재전송·DBGSR 검증·DE 비트 수정(M17b).
  **미구현 유지**: `step`·`run_until`·트리거 생성·CHL/trace.
- **M18(완료, 2026-10-18)**: 트리거 실구현(§3-R) — OCDS TR0..TR7·PC/데이터·범위·halt, GUI 전 라이프사이클.
  **미구현 유지**: `step`·`run_until`(DAP 필요)·`execute_command`·`set_trace_state`·`read_trace`
  (CHL 5종+`qry_input_handle`은 원본도 스텁 — 파리티).
- **M19(완료, 2026-10-18)**: trace·잔여 함수 원본 교차 캘리브레이션(§3-S) — 원본 DLL 직접 프로브 12/12 일치.
  잔여 차이(기록): 서버명 표기(`TasServer` vs `UDAS`)·미export 3종(우리는 API-정합 export)·NULL 인자 방어(우리만).
- **M20(완료, 2026-10-18)**: SPR 로컬→전역 변환(§3-T) — M15 잔여 GAP 해소(로컬 DSPR/PSPR → 코어별
  버스 별칭), 원본 파리티(읽기+쓰기) · GUI 로컬 주소 쓰기/읽기/복원. **aurixtracing 데몬 통합의 전제 조건.**
  **M20b**: reg_map 코어별 PC(원본 교차 40/40, 라이브 PC 읽기) — 코어 문맥 계열 파리티 마감.
- **다음**: 새 요구사항에 한함.

## 부록. 산출물

- 검증 로그: [mcd_tas_probe.log](../logs/mcd_tas_probe.log) · [mcd_tas_smoke.log](../logs/mcd_tas_smoke.log) · [mcd_tas_attach.log](../logs/mcd_tas_attach.log)
  · 성공 재검증(2026-10-18): [mcd_tas_probe_idcode_ok.log](../logs/mcd_tas_probe_idcode_ok.log) · [mcd_tas_attach_ok.log](../logs/mcd_tas_attach_ok.log)
- M3 GUI 캡처·발췌: [m3_dropin/](../m3_dropin/) (select · main_after_attach · coreinfo · read_70000000 · read_f0000480 · log_excerpt.txt)
- M4 State/IP 복구 캡처·발췌: [m4_dropin/](../m4_dropin/) (state_ip_recovered · read_regression · log_excerpt.txt)
- M4+ 디버그 레지스터 검증 캡처·발췌: [tc27x_dbg_regs/](../tc27x_dbg_regs/) (dbgsr_read · pc_read · cpuid_read · log_excerpt.txt)
- M5 실상태 검증 캡처·발췌: [m5_dropin/](../m5_dropin/) (state_main · read_regression · log_excerpt.txt)
- M6 오류 표시 캘리브레이션 캡처·발췌: [m6_dropin/](../m6_dropin/) (proxy_log_excerpt.txt · wire_compare.txt · ours_fail/ours_success · orig_proxy_no_modal/orig_direct_no_modal)
- M7 reg_map 교차 캘리브레이션 캡처·발췌: [m7_dropin/](../m7_dropin/) (proxy_reg_map_dump.txt · our_client_log_excerpt.txt · regs_main/read_regression)
- M8 mem_spaces/스텁 정합 캡처·발췌: [m8_dropin/](../m8_dropin/) (proxy_mem_dump.txt · smoke_run.txt · main/read_regression)
- M9 열거 교차 캘리브레이션 캡처·발췌: [m9_dropin/](../m9_dropin/) (proxy_enum_dump.txt · smoke_run.txt · select/main/read_regression)
- M10 리셋 클래스 교차 캘리브레이션 캡처·발췌: [m10_dropin/](../m10_dropin/) (proxy_rst_dump.txt · smoke_run.txt · m10_main.png)
- M11 Run 실관찰 캘리브레이션 캡처·발췌: [m11_dropin/](../m11_dropin/) (proxy_run_stop.txt · our_state_log_excerpt.txt · 화면 7종)
- M12 GUI Write 검증 캡처·발췌: [m12_dropin/](../m12_dropin/) (our/orig write_test · 화면 5종 · modal_notes.txt)
- M13 Trace Viewer 독립성 확인: [m13_dropin/](../m13_dropin/) (module_check.txt · 화면 3종)
- 정상 시절 vs 현재 대조(§5 = M3 재검증): [MCD_BASIC_VS_CURRENT_COMPARE.md](MCD_BASIC_VS_CURRENT_COMPARE.md)
- 캡처 근거(참조 시퀀스): [mcds/](../mcds/) conn 3341(애태치+AM0), conn 4085(MCDS 폴링) — 해설 §8
- 도구 분석(대응 UI/동작): [MCD_TOOLS_ANALYSIS.md](MCD_TOOLS_ANALYSIS.md)
