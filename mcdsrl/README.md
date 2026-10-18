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

녹화 7함수(`set_mcdsconfig`·`set_trace_buf`·`open_trace_file`·`start/stop_recording`·`get_recstate`·
`get_data_from_fifo`)는 **M23-2 예정** (현재 FUNCTION_NOT_SUPPORTED 스텁).

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
