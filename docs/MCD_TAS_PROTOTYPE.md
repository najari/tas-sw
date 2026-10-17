# MCD API over TAS — 프로토타입 (`mcd_tas`) 구현·검증 노트 (2026-10-17)

`mcdxdas.dll`(DAS 백엔드)과 **동일한 MCD API v1.1 "SPRINT Release" 계약(54개 함수)을
TasServer(24817) 백엔드로** 구동하는 프로토타입 DLL(`mcd_tas.dll`)의 구현 범위와 실서버 검증 결과.

- 구현 소스(C++)는 별도 로컬 작업 트리에서 관리 — 본 저장소에는 **결과 문서·검증 로그만** 포함(저장소 원칙).
- 프로토콜 근거: ① 본 저장소 캡처 해독 [TAS_PROTOCOL_CAPTURE.md](TAS_PROTOCOL_CAPTURE.md) §8
  ② 공식 `tas_client_api` 소스(`tas_pkt.h`, `tas_pkt_handler_rw.cpp`, `tas_client_server_con.cpp`) — 양쪽 상호 일치 확인.
- 원칙: **읽기 전용** — 타깃 실행 제어(Run/Stop/Step/Reset)는 의도적으로 미구현(스텁).

## 0. 상태 요약

| 마일스톤 | 내용 | 상태 |
|---|---|---|
| M1 | 초기화·서버·시스템/디바이스/코어 열거 (11함수) | 완료·검증 |
| M2 | 코어 열기(세션 + 핫 애태치)·메모리 공간 열거·PL0 읽기/쓰기 (6함수) | 완료·검증 — 애태치 성공 경로 확보(§3-D; §3-C의 실패는 USB 허브 환경 문제였음) |
| M3 | 원본 `mcd_basic_client.exe` drop-in(`mcdxdas.dll` 자리) GUI 구동 | **GUI 검증 완료(§3-E)** — Select/애태치/Core Info/Read. 잔여: qry_state 실상태·매핑 캘리브레이션 |

## 1. 구현 범위

54개 함수 중 **17개 구현 + 37개 정직한 스텁**(`MCD_ERR_FN_UNIMPLEMENTED` + 함수명 메시지, `mcd_qry_error_info_f`로 조회).

- **M1 (11)**: `mcd_initialize_f` `mcd_exit_f` `mcd_qry_servers_f` `mcd_open_server_f` `mcd_close_server_f`
  `mcd_set_server_config_f` `mcd_qry_server_config_f` `mcd_qry_systems_f` `mcd_qry_devices_f`
  `mcd_qry_cores_f` `mcd_qry_error_info_f`
- **M2 (6)**: `mcd_open_core_f` `mcd_close_core_f` `mcd_qry_mem_spaces_f` `mcd_execute_txlist_f`
  `mcd_qry_state_f` `mcd_qry_max_payload_size_f`

### 1.1 M2 동작 요약

- `mcd_open_core_f` — 코어당 **독립 TCP 연결 + 세션**(최대 4코어, 같은 타깃+코어는 재사용). 시퀀스는 §2.
- `mcd_execute_txlist_f` — 읽기/쓰기(R/W)만. `access_width` 0이면 정렬 기반 자동 분할(≤8B/아이템),
  읽기 블록은 RDBLK(8정렬·8배수·≤1024B), 프레임 한도(32건/1060B/1040B — §8.2 실측값)는 내부에서 자동 분할.
  첫 실패 트랜잭션 기준으로 `num_tx_ok`/`num_bytes_ok` 집계 후 중단 보고.
- `mcd_qry_mem_spaces_f` — 메모리 공간 1개: id **1** = AM0(TAS 주소 맵 0 = 타깃 물리 접근), physical, 8bit MAU, LE.
- `mcd_qry_state_f` — 아직 `MCD_CORE_STATE_UNKNOWN` 고정(정직 — 실상태는 후속).
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
- 잔여 차이: State/IP 필드 `Unknown`/`XXXXXXXX` (`mcd_qry_state_f` 미구현 + 클라이언트 자체 RD16@0x0 폴링 0x8D) — 후속.
- 실무 노트(주소 `0x` 접두 필수 = base-0 파서, 모달 다이얼로그, 픽셀 클릭 등):
  [MCD_BASIC_VS_CURRENT_COMPARE.md](MCD_BASIC_VS_CURRENT_COMPARE.md) §5

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
- `mcd_qry_state_f`=UNKNOWN(State/IP 필드 `Unknown`/`XXXXXXXX` — 원본 시절 `Debug`/`0xA0000020`),
  메모리 공간 1개(AM0), 캐시/가상 공간·메모리 블록 미노출.
- 트랜잭션 리스트 중단 시 이미 전송된 프레임은 취소 불가(서버가 전량 실행) — 집계만 중단 시점 기준.
- 트리거/채널/트레이스/레지스터/리셋 등은 스텁. 단일 전역 임계구역 직렬화(재진입 없음).
- **다음(M3+)**: `mcd_qry_state_f` 실상태(PC/stop info), 매핑 캘리브레이션, (승인 후) Write 검증,
  reg groups/trig, 오류 반환 조합 캘리브레이션(실패 표시 방식).

## 부록. 산출물

- 검증 로그: [mcd_tas_probe.log](../logs/mcd_tas_probe.log) · [mcd_tas_smoke.log](../logs/mcd_tas_smoke.log) · [mcd_tas_attach.log](../logs/mcd_tas_attach.log)
  · 성공 재검증(2026-10-18): [mcd_tas_probe_idcode_ok.log](../logs/mcd_tas_probe_idcode_ok.log) · [mcd_tas_attach_ok.log](../logs/mcd_tas_attach_ok.log)
- M3 GUI 캡처·발췌: [m3_dropin/](../m3_dropin/) (select · main_after_attach · coreinfo · read_70000000 · read_f0000480 · log_excerpt.txt)
- 정상 시절 vs 현재 대조(§5 = M3 재검증): [MCD_BASIC_VS_CURRENT_COMPARE.md](MCD_BASIC_VS_CURRENT_COMPARE.md)
- 캡처 근거(참조 시퀀스): [mcds/](../mcds/) conn 3341(애태치+AM0), conn 4085(MCDS 폴링) — 해설 §8
- 도구 분석(대응 UI/동작): [MCD_TOOLS_ANALYSIS.md](MCD_TOOLS_ANALYSIS.md)
