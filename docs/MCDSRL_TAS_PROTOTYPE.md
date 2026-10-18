# MCDS Recorder Library over TAS (`mcdsrl_tas`) — 프로토타입 노트

Infineon **MCDS Recorder Library 3.3.0.0**(`mcd_recorder.dll`)의 **API 호환 재구현** — 자체 TAS TCP
클라이언트(mcd_tas에서 캘리브레이션 완료)를 백엔드로 사용. 공식 API 헤더(`mcds_lib_recorder.h`) 기준
22개 export. 구현 소스·빌드·검증 개요는 [mcdsrl/README.md](../mcdsrl/README.md) 참조.

- 배경: M14 정적 분석(22 `mcdsrl_*`·내장 TAS 클라이언트) + MCDS 녹화 엔진의 재구현 트랙.
  **MCDS 설정은 호출자(configurator/CCL)가 (주소,값) 쌍으로 생성** → recorder는 일괄 기록만 담당.

## M23-1 — 코어 구현·원본 교차 검증 (2026-10-18)

- 구현(15/22): `lib_init`·`lib_version`(3.3.0)·`get_error` / `qry_devices`·`connect_device`·
  `device_connected`·`disconnect_device`·`reset_device` / `read`·`write`·`read_list`·`write_list`·
  `read_block`·`write_block`·`read_timestamped` — 녹화 7함수는 M23-2 예정(FUNCTION_NOT_SUPPORTED 스텁)
- **원본 교차 규명** (Frida WS2_32 캡처 + 디컴파일):
  - `read_timestamped` = **3개의 개별 프레임**: `RD64 @0x50000090`(**맵 15 = 서버 내부 뷰 — MCDS
    프리런닝 64비트 타임스탬프 카운터!**) → `RD32 @addr`(맵 0) → `RD64 @0x50000090`. 한 프레임 혼합은
    서버가 **PL0 0x8D**로 거부 — 프레임 대조 [m23_dropin/ts_frames.txt](../m23_dropin/ts_frames.txt)
  - **ED(Emulation Device) 판별**: 동일 IDCODE라도 EMEM/MCDS 존재 시 "ED"(예: `TC27xED`) — 우리는
    **EMEM(0x50000000) 읽기 성공 = ED** 프로브로 구현
  - capability `0x8`(CONT_TRACE_UNRELIABLE — miniWiggler/Windows 클래스) 정합
- 검증: 스모크(양쪽 DLL 동일 실행) — device/cplty/타임스탬프 카운터 전부 일치 ·
  `SMOKE OK (0)` ×2 — 증거 [m23_dropin/](../m23_dropin/) (smoke 2종·ts 프레임 대조)

## M23-2 — 녹화 7함수 구현 + 하드웨어 블로커 규명 (2026-10-18)

- **구현 완료 (22/22!)**: `set_mcdsconfig`·`set_trace_buf`·`open_trace_file`·`start_recording`·
  `get_recstate`·`stop_recording`·`get_data_from_fifo` — 원본 디컴파일 대조 (HAR 마지막 쌍 분리·
  OCS/PD 검사·0x310 시퀀스·트레이스 리드백(고정 포트 1KB×N)·cleanup write_list!)
- **`.mcds` 텍스트 헤더** 규명 (원본 `trace_file_write_header` 대조): `McdsDevice`/`McdsDeviceUsed`/
  `McdsToolUsed`/`McdsFrequencyMHz`/`RecordingDate`/`RecordingTime` + `START_OF_MCDS_CONFIG` +
  (addr,value) 쌍 + `START_OF_TDR` + raw
- 검증 스모크-2: CCL 정본 흐름(`CclDevice.record()`) — [원본 configurator config 생성] + reset(HAR) +
  open/set/start/poll/stop. config 실측: num_reg 43·crsv 2·mcds_device 0x0A00ED83
- **블로커**: 이 구성(DAS JTAG AURDUINO)에서 **MCDS 레지스터 접근(map 1, 0xF00E6xxx)이 0x8D(버스
  에러)로 전면 차단 — 원본 recorder도 동일 실패** ("Write of addr F00E6000 in addr_map 1 failed").
  유일한 MCDS 접근 = map15 RD64 타임스탬프 카운터(0x50000090) ✓; EMEM(map0) 읽기 ✓. 접근 매트릭스:
  [m23b_dropin/](../m23b_dropin/) (원본/우리 실패 로그·map 프로브). 어댑터/서버 측 지원 문제로 판정
  (웨이크업·세션명·NativeJtagMode config·2023 das 변형 전부 기각!)

## 다음 — M23-3 (파일/디코더·CCL 드롭인)

- 어댑터 측 MCDS 접근 해결 여부에 종속. 해결 시: 우리 recorder로 녹화 → `.mcds` →
  원본 디코더(`mcdsdl_mcds_file_load`) 교차 검증 → MTV/CCL 드롭인.

## 보드/어댑터 정체 규명 (2026-10-18 후속)

- 사용자 보드 = **Hitex ShieldBuddy TC275** — "**AURDuino**"(Hitex IDE 이름!) = 보드의 온보드 디버그
  → DAS의 "DAS JTAG AURDUINO 46YPEADW" = ShieldBuddy 온보드 인터페이스. USB = `Infineon DAS JDS
  Debug` (VID_058B/PID_0043, FTDI — DAS ReleaseNotes의 "Bare On-Board Wiggler (BOBW)" 계열!)
- **EMEM 실재 테스트(결정적)**: map 0 @ 0x50000000 쓰기→되읽기 **성공(0x12345678!)** → **EMEM(1MB)
  실재 = 칩은 TC275ED(MCDS 탑재!)** 확정 ✓ (증거: m23b_dropin/m23_emem_test.log)
- 매뉴얼: MTV = ED 전용 ✓ + "USB cable to a TriBoard or a **DAP miniWiggler**" (권장 = AP56004 DAP
  miniWiggler V3!) · 보드 매뉴얼 = "accessed via JTAG or DAP" · map 0=제품칩/map 1=EEC (TC27x UM §1.4.4)
- → 타깃 하드웨어(MCDS!)는 존재 ✓ — 남은 관건 = **온보드 AURDuino(FTDI/BOBW)의 EEC 접근 지원 여부**
  (0x8D의 유력 원인). DAP miniWiggler V3 또는 TriBoard로 검증 시 우리 recorder 즉시 동작 예상.
