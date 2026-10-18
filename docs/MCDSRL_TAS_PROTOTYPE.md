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

## 다음 — M23-2 (녹화 7함수)

- `set_mcdsconfig`(저장 + start 시 일괄 `write_list` + **마지막 쌍 분리** = 트레이스 시작!) ·
  `start_recording`(OCS/PD/HAR 검사 + 초기화 시퀀스) · `stop_recording`(cleanup + 상태) · `get_recstate`
- **트레이스 리드백**: `read_block(base+0x100000, 1KB)` 루프 (디컴파일 규명 — base = 디바이스별 MCDS 베이스)
- `.mcds` 파일: `"McdsFrequencyMHz=%.1f\n\n"` + raw (CCL 정본) → **원본 디코더로 교차 검증**
