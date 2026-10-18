# mcdsrl_tas — MCDS Recorder Library (`mcds_recorder.dll`) over TAS (프로토타입)

`mcd_recorder.dll`(Infineon **MCDS Recorder Library 3.3.0.0**)의 **API 호환 재구현** — 자체 TAS TCP
클라이언트(mcd_tas에서 캘리브레이션 완료)를 백엔드로 사용. 공식 API 헤더(`mcds_lib_recorder.h`) 기준
**22개 export**.

- 소스: `src/mcdsrl_impl.cpp` + `src/tas_client.cpp`(mcd_tas 공유) + `src/mcdsrl_tas.def`(22 export)
- 헤더: `include/` (SPRINT 라이선스 원본 유지 — mcd_api.h·mcd_types.h·mcds_lib.h·mcds_lib_recorder.h)
- 테스트: `tests/mcdsrl_smoke.cpp` — DLL(우리/원본) 로드 후 코어 API 실행·비교
- 빌드: `cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release`
- 실행: `mcdsrl_smoke.exe [dll]` (기본 mcdsrl_tas.dll)

## M23-1 구현 — 코어 (2026-10-18)

| 그룹 | 함수 (15) | 비고 |
|---|---|---|
| 코어 | `lib_init`·`lib_version`(3.3.0!)·`get_error` | 1회 제한·버전 검사·서버 자동기동(뮤텍스+배치!) |
| 디바이스 | `qry_devices`·`connect_device`·`device_connected`·`disconnect_device`·`reset_device` | TAS 열거/세션/DC + **ED 프로브**(EMEM 0x50000000 읽기!) |
| RW | `read`·`write`·`read_list`·`write_list`·`read_block`·`write_block`·`read_timestamped` | PL0 아이템 + mcdsl_return_et 매핑 |

## M23-2 구현 — 녹화 (2026-10-18)

녹화 7함수 구현 완료 (22/22!): `set_mcdsconfig`(압축 복사+파라미터 도출)·`set_trace_buf`·
`open_trace_file`·`start_recording`(HAR 마지막 쌍 분리·OCS/PD 검사·0x310 시퀀스·**`.mcds`
텍스트 헤더**!)·`get_recstate`·`stop_recording`(**트레이스 리드백** — 고정 포트 1KB×N +
cleanup write_list!)·`get_data_from_fifo`(연속 전용 → 미지원).

- `.mcds` 포맷: `McdsDevice`/`McdsDeviceUsed`/`McdsToolUsed`/`McdsFrequencyMHz`/`RecordingDate`/
  `RecordingTime` + `START_OF_MCDS_CONFIG` + (addr,value) 쌍 + `START_OF_TDR` + raw
- 검증 도구: `tests/mcdsrl_record.cpp`(CCL 정본 흐름!) · `tests/map_probe.cpp`(접근 매트릭스)

### ⚠ MCDS 접근 하드웨어 블로커 (실측 규명 — 원본도 동일 실패!)

이 구성(DAS JTAG AURDUINO)에서는 **MCDS 레지스터 접근(map 1, 0xF00E6xxx/8xxx)이 불가능**
(서버 응답 0x8D 버스 에러) — **원본 recorder도 동일 지점에서 실패**("Write of addr F00E6000
in addr_map 1 failed"):

| 경로 | 결과 |
|---|---|
| map 1 MCDS 레지스터 (RD32/RD64/WR) | 전부 실패 ✗ |
| map 15 MCDS 레지스터 (RD32/RD64/WR) | 전부 실패 ✗ |
| **map 15 0x50000090 (RD64)** | **성공 ✓ — MCDS 타임스탬프 카운터 (유일한 MCDS 접근)** |
| map 0 EMEM (0x50000000/0x50100000/0xAF000000) | 읽기 성공 ✓ (트레이스 RAM 뷰) |

기각된 가설: 웨이크업 순서·세션명·DeviceQry 예비 세션·NativeJtagMode=false config·2023
`mcds_recorder_das.dll`(TC27xED 미지원). → 어댑터/서버 측 MCDS 지원 문제로 판정.
MCDS 레지스터가 열리면 우리 recorder는 원본과 동일 프로토콜로 즉시 동작(프레임 대조 완료).

## 원본 교차 검증 (스모크 양쪽 실행)

```
[우리]   device 'TC27xED'  cplty 0x8  ts 0x4C489120/0x4C4894E2  SMOKE OK (0)
[원본]   device 'TC27xED'  cplty 0x8  ts 0x4C4A0C14/0x4C4A0F78  SMOKE OK (0)
```

교차로 규명·정합한 항목:

- **`read_timestamped` = 3개의 개별 프레임** (Frida 캡처!): `RD64 @0x50000090`(맵 15 = 서버 내부 —
  **MCDS 프리런닝 64비트 타임스탬프 카운터**!) → `RD32 @addr`(맵 0) → `RD64 @0x50000090`.
  한 프레임에 혼합(맵 전환)하면 서버가 **PL0 0x8D**로 거부 ✗ — 프레임 대조: `m23_dropin/ts_frames.txt`
- **ED(Emulation Device) 판별**: 동일 IDCODE라도 **EMEM/MCDS 존재 시 "ED"** (원본 디컴파일
  `device_id_to_name_long` + 별도 플래그) — 우리는 **EMEM(0x50000000) 읽기 성공 = ED** 프로브
- **capability 0x8** (CONT_TRACE_UNRELIABLE — miniWiggler/Windows 클래스) 정합
- 세션명 관례(참고): `"MCDS recorder"`(연결)/`"DeviceQry"`(조회) + 세션명 = 사용자명

증거: `m23_dropin/` (smoke_ours.txt·smoke_orig.txt·ts_frames.txt)

## 환경 노트

- 서버 재기동 후 **Native JTAG Mode** 고착 시: 원본 DLL + config `McdAccHw.NativeJtagMode=false`로
  open_server → 복구 (mcd_tas M22와 동일 절차)
- 강제 종료된 클라이언트의 잔여 세션 누적 시 `SESSION_START` = `tas_err 0x38` — 서버 재기동으로 해소
