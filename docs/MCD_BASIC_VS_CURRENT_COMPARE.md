# mcd_basic_client(정상 시절) vs 현재 판독값 대조 — 2026-10-18

> ## ⚡ 해결 (2026-10-18 13:06)
> 사용자가 **USB를 허브에서 제거하고 PC 본체 직결** (LocationPaths가 `...#USBROOT(0)#USB(3)#USB(2)` → `...#USBROOT(0)#USB(2)`로 변경, 13:06:22 재인식).
> **전 항목 정상 복구 확인:**
> - `tas_probe --idcode`: devtype `0x401DA083`, hash `LBHJYL (0x4013A2AA)`, device_id `47427060 11080040 64945FCB 20E65C06`,
>   @0xD000 `"TriCore-Family"`, 스캔 IDCODE = `0x401DA083` MATCH, 전 스캔 아이템 err=0x80 (← `probe_idcode_retry2.txt`)
> - `mcd_tas_smoke --attach`: **DEVICE_CONNECT 성공**, 'TC27x' 3코어(TriCore 1.6E) 인식,
>   **RD32 @0xF0000480 = 0x00080001** (정상 시절 시그니처와 일치) (← `smoke_attach_ok.txt`)
> - **근본 원인 확정: USB 허브 경유로 인한 타깃 reset 고착** (동일 증상의 포럼 보고와 일치). 소프트웨어·서버·드라이버 무결.


목적: 2026-10-17 정상 시절의 MCD API(legacy) 클라이언트(mcd_basic_client) 데이터와,
현재(2026-10-18) tas_probe/mcd_tas_smoke 판독값을 항목별로 대조하여 차이의 발생 위치를 특정한다.

## 1. 데이터 출처

| 구분 | 파일 |
|---|---|
| 정상 시절 e2e | `e2e_demo_rw.log` (2026-10-17 14:39, target info 전문) |
| 정상 시절 패킷 캡처 | `TAS_PROTOCOL_CAPTURE.md` §타깃/블록 표 (테이블 @0xD000/@0x8000/@0x0000) |
| 정상 시절 MCD 클라이언트 캡처 | `mcds/native_report/detail_CONN3341.txt` (2026-10-17 19:03, 'MCD API (legacy devices)') |
| 현재 재시도 (방금) | `C:\work\mcd_tas\probe_idcode_retry.txt` (tas_probe, 와이어 로그 포함) |
| 현재 재시도 (방금) | mcd_tas_smoke.exe --attach 결과(tas_err=0x20) |

## 2. 항목별 대조표

| # | 항목 | 정상 시절 (10-17) | 현재 (10-18) | 판정 |
|---|---|---|---|---|
| 1 | 서버 정체 | TasServer V2.0 (Apr 30 2026) | 동일 | ✅ 동일 |
| 2 | identifier | `DAS JTAG AURDUINO 46YPEADW` | 동일 | ✅ 동일 |
| 3 | max_pkt / max_rw | 1060/1040, 32 | 1060/1040, 32 | ✅ 동일 |
| 4 | rw_mode_mask / addr_map_mask | 0x0000 / 0x8001 | 0x0000 / 0x8000 | 🔸 (맵0을 이번 읽기에 안 씀, 이상 아님) |
| 5 | ACC 주파수 @0x000C0100 | 400000 (0x61A80) | 400000 (0x61A80) | ✅ 동일 |
| 6 | **@0x8000 블록 (168B)** | `"DAS JTAG AURDUINO"[64]+"Infineon"[32]+"46YPEADW"[32]+8B0+u32 0x80000001×2+24B0` | **바이트 단위 완전 동일** (원시: `...01 00 00 80 01 00 00 80...` @오프셋 136) | ✅ 동일 |
| 7 | **device_type** (GT/SS rsp) | `0x401DA083` (TC27x) | `0x00000000` | ❌ 상실 |
| 8 | **device_id (4×u32)** | `47427060 11080040 64945FCB 20E65C06` | `00000000 ×4` | ❌ 상실 |
| 9 | **device_id_hash** | `0x4013A2AA ('LBHJYL')` | `0x00000000 ('NoUid')` | ❌ 상실 |
| 10 | **@0xD000 블록 (104B)** | `83 A0 1D 40 00..` + `"TriCore-Family"` + u32 2 | `00 00 00 00 00..` + `"Unknown Device"` + 0 | ❌ 상실 |
| 11 | **DEVICE_TYPE_ACC_HW @0x00DD1000** | 유효 (식별됨) | `0x00000000` ("access HW has read no JTAG ID") | ❌ 상실 |
| 12 | **DEVICE_CONNECT** | `DCrsp(err=OK)` | `tas_err=0x20` (reject) | ❌ 상실 |
| 13 | (연결 후) RD32 @0xF0000480 | `0x00080001` (TriCore 스캔 성공 증거) | attach 불가로 도달 못 함 | ❌ 상실 |
| 14 | 네이티브 JTAG 스캔 아이템 (en/rst/cap/wr/rd) | (성공 시 err=0x80) | 전부 `err=0x8D` | ❌ 실패 |

## 3. 해석

- **정상/실패를 가르는 경계는 정확히 하나다: JTAG TAP이 IDCODE를 응답하느냐.**
  - JTAG 의존 항목(7~14)은 전부 0/실패, JTAG 무관 항목(1~6)은 전부 정상·동일.
- @0x8000 블록이 **바이트 단위로 동일**하다는 사실은 서버가 USB·레지스트리·저장 디스크립터를 여전히
  정상 보유하고 있음을 의미한다(서버 프로세스/USB 경로 자체는 건강).
- @0xD000이 "Unknown Device"로 바뀐 것도 서버측 합성 로직의 결과일 뿐, 원인은 동일(TAP 미응답).
- 즉 **클라이언트 소프트웨어 차이가 아님** — 현재 mcd_basic_client(원본 MCD API)로 접속해도
  같은 자리에서 동일 실패(DC 거부)가 재현될 것으로 예측되며, 우리 프로토타입의 프로토콜 시퀀스가
  정상 시절과 일치함이 재확인됨.
- 결론: 남은 원인 후보는 물리 계층뿐 — ① 허브 경유(본체 직결 필요), ② 타깃 외부 전원,
  ③ USB 케이블 품질, ④ 타깃 reset 고착. (FTDI 직접 비트뱅 스캔에서도 TAP 무응답이 독립 확인됨)

## 4. 방금 실행한 재시도 기록

- `tas_probe.exe --idcode` → 실패 (devtype 0, NoUid, mask 0x0000, scan 전부 0x8D) → `probe_idcode_retry.txt`
- `mcd_tas_smoke.exe --attach` → `mcd_open_core_f` 실패 (tas_err=0x20, "server reports no identified device")
- `mcd_basic_client.exe` (원본 MCD 클라이언트, GUI) 실행 시도 → 60초 내 출력 없음(창 대기), 크래시 이벤트 없음

## 5. M3 drop-in 재검증 (2026-10-18, USB 직결 복구 후) — §3 예측의 확증

원본 `mcd_basic_client.exe`(DAS64 클라이언트)에 우리 `mcd_tas.dll`을 `mcdxdas.dll` 자리(drop-in)로 배치 —
**GUI 전 구간 정상 동작**:

- Core Select → 애태치 → 코어 열기: 타이틀 `DAS JTAG AURDUINO 46YPEADW` 전환
  (로그: `session attached (0x401DA083) → core 0 opened ('TC27x')`).
- Core Info 다이얼로그: 전 항목 = 우리 DLL 열거 값 (device_type `0x401DA083`, device `TC27x`, core_type 44 등).
- **GUI Read 성공 2건**:
  - `0x70000000` → `0x00000000` (rq `00 15 00 70` + `00 25 00 00`, 오류 0)
  - `0xF0000480` → **`0x00080001`** (rq `00 15 00 F0` + `00 25 80 04`, 데이터 `01 00 08 00`)
    — §2-#13 "정상 시절 시그니처"(RD32 @0xF0000480 = 0x00080001)와 **정확히 일치**.

→ §3의 결론("클라이언트 소프트웨어 차이가 아니며 남은 원인은 물리 계층")이 확증됨 —
물리 복구 후 원본 클라이언트가 우리 TAS 백엔드 위에서 정상 동작.

잔여 차이(후속):
- State/IP 필드 — 정상 시절 원본 스택은 `Debug`/`0xA0000020`(← `tas-sw/mcds/crop_13_after_read.png`)
  이나 현재는 `Unknown`/`XXXXXXXX`. 원인: ① `mcd_qry_state_f` 미구현(UNKNOWN 고정)
  ② 클라이언트 자체 폴링 RD16 @0x0(mem space 0)을 서버가 0x8D(DATA)로 거부. 앱 동작엔 영향 없음.
- 읽기 실패 표시: 정상 시절 모달 경고 vs 현재 MCD Events 텍스트만 — 오류 반환 조합 캘리브레이션 후속.

상세·실무 노트: `M3_GUI_DROPIN_NOTES.md`(로컬 분석 폴더) / 스크린샷 `C:\work\mcd_tas\gui_test\`,
발췌 로그·캡처는 tas-sw 저장소 `m3_dropin/` 참조.
