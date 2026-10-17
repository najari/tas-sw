# TC27x(D-Step) 디버그 레지스터 해독 — `0xF881FE08`(PC) / `0xF881FD00`(DBGSR) (2026-10-18)

드롭인 클라이언트(원본 `mcd_basic_client.exe` + `mcd_tas.dll`)가 폴링하는 `0xF881FE08`과,
정상 시절 캡처에서 최초 1회만 판독된 `0xF881FD00`(= `0x1013`)의 의미를 TC27x D-Step UM
(+TC1.6 코어 매뉴얼)으로 규명하고 드롭인 GUI Read(읽기 전용)로 라이브 검증한 기록.

## 1. 주소 구조 — CPUx CSFR 창

출처: TC27x D-Step UM V2.2(2014-12), **Table 3-3 "On Chip Bus Address Map of Segment 15"** (Memory Maps).

| 창 | 범위 | 비고 |
|---|---|---|
| CPU0 SFR | `0xF8800000` – `0xF880FFFF` | 64 KB |
| **CPU0 CSFR** | **`0xF8810000` – `0xF881FFFF`** | 64 KB — `0xF881xxxx`의 정체 |
| CPU1 SFR / CSFR | `0xF8820000` / `0xF8830000` | |
| CPU2 SFR / CSFR | `0xF8840000` / `0xF8850000` | |
| (예약) | `0xF8860000` – `0xF8FFFFFF` | |

Segment 15 = SFR/CSFR 툴 접근 창("This memory segment allows accesses to all SFRs, CSFRs").
→ `0xF881FE08` = **CPU0 CSFR + `0xFE08H`**, `0xF881FD00` = **CPU0 CSFR + `0xFD00H`**.
(CSFR = Core Special Function Register — 코어 내부 레지스터의 툴/디버그 접근용 창)

## 2. `0xFE08` = PC (Program Counter Register)

출처: UM **Table 5-19 "Core Special Function Registers"** + **§5.6.2 Program Counter (PC) Register**.

- 주변 CSFR: PCXI `FE00`, PSW `FE04`, **PC `FE08`**, SYSCON `FE14`, CPU_ID `FE18`, CORE_ID `FE1C`,
  BIV `FE20`, BTV `FE24`, ISP `FE28`, ICR `FE2C`, FCX `FE38`, LCX `FE3C`, PMA0 `8100` …
- 접근: 읽기 U/SV 32bit, 쓰기 SV+Privileged, Application 모드, 리셋 `XXXX XXXXH`.
- §5.6.2 요지: "PC는 현재 페치되어 파이프라인으로 전달되는 명령어의 주소를 담는다"; 소프트웨어 읽기는
  MFCR만 허용; **PC 쓰기는 CPU가 halted 상태에서만** 가능; "Segment 15에 매핑된 PC 주소로 Load/Store
  수행 금지(MEM trap)" — 즉 이 창은 툴 전용; **PC[0]은 read-only, 고정 0**.

## 3. `0xFD00` = DBGSR (Debug Status Register)

출처: UM **Table 5-24 "Core Debug and Performance Counter Registers"** +
TriCore TC1.6P & TC1.6E UM Vol.1 **§12.9 DBGSR** (비트필드 정본).

### 3.1 비트필드 (FD00H, 32bit, Debug 모드, 리셋: Boot Execute `0x00000000` / Boot Halt `0x00000002`)

| 필드 | 비트 | 의미 |
|---|---|---|
| DE | 0 | Debug Enable (CDC 활성화 여부) |
| HALT | [2:1] | CPU Halt 요청/상태. HALT[0]=실제 halt 비트, HALT[1]=쓰기 마스크(읽기는 항상 0). 읽기: `00b`=실행 중, `01b`=**정지(halted)**; 쓰기: `10b`=해제, `11b`=설정(DE=1일 때) |
| SIH | 3 | Suspend-In 신호 상태 (1=어서트, CPU가 Halt Mode) |
| SUSP | 4 | Core Suspend-Out 신호 현재 상태 |
| — | 5 | 예약 |
| PREVSUSP | 6 | Suspend-Out 이전 상태 |
| PEVT | 7 | Posted Event (브레이크포인트 인터럽트 지연 포스팅 표시) |
| EVTSRC | [12:8] | 최종 Debug Event 소스: `0`=EXEVT, `1`=CREVT, `2`=SWEVT, **`16+n`=TRnEVT**(트리거 n=0..7) |

- Halt 진입/해제(§12.4.4): "**Halt mode is entered when the DBGSR.HALT bit field is set to `01B`**";
  "only resumes execution once the … HALT bit is clear … **cleared by writing `10B`**".
- SIH(§12.4.11): Suspend-In 어서트 + 디버그 활성 → Halt 유지, 해제 시 복귀 (멀티코어 동시 정지용).

### 3.2 판독값 해독

| 시점 | 값 | 해독 |
|---|---|---|
| 정상 시절(2026-10-17) 캡처 | `0x00001013` | DE=1, **HALT=01b(정지)**, SIH=0, SUSP=1, PREVSUSP=0, PEVT=0, **EVTSRC=16 → TR0EVT** |
| 라이브 Read(2026-10-18) | `0x0000101B` | 동일 조합 + **SIH=1**(Suspend-In 어서트로 Halt 유지) |

→ "디버그 활성 + CPU0 정지 + 최종 정지 유발 소스 = 트리거 0(TR0EVT)".
`HALT=01b` = 정지, 해제는 `10b` 기록(§12.4.4). Boot-Halt 리셋값(`0x0002`)도 HALT=01b와 정합.

### 3.3 같은 창의 주변 디버그 CSFR (Table 5-24 / TC1.6 §12.8 Table 12-1)

| 오프셋 | 이름 | 의미 |
|---|---|---|
| FC00 / FC04 / FC08 / FC0C / FC10 / FC14 | CCTRL / CCNT / ICNT / M1CNT / M2CNT / M3CNT | 성능 카운터(OCDS 제어) |
| **FD00** | **DBGSR** | Debug Status (본 문서 §3) |
| FD08 | EXEVT | External Event Register |
| FD0C | CREVT | Core Register Access Event Register |
| FD10 | SWEVT | Software Debug Event Register |
| FD30 | TRIG_ACC | Debug Trigger Accumulation Register |
| FD40 | DMS | Debug Monitor Start Address |
| FD44 | DCX | Debug Context Save Area Pointer |
| FD48 | DBGTCR | Debug Trap Control Register |
| F000–F03C | TR0..TR7 EVT/ADR | 트리거 이벤트/주소 레지스터 (EVTSRC `16+n`) |
| FFB0–FFBC | SBSRC3..SBSRC0 | 소프트웨어 브레이크포인트 SR 제어 |

### 3.4 라이브 검증 (드롭인 GUI Read, 읽기 전용)

| 주소 | 값 | 일치 대상 | 화면 |
|---|---|---|---|
| `0xF881FD00` | `0x0000101B` | DBGSR — HALT=01b/DE=1/TR0EVT (해독 §3.2) | [dbgsr_read.png](../tc27x_dbg_regs/dbgsr_read.png) |
| `0xF881FE08` | `0xA0000020` | PC = UI `Instruction Pointer` 표시와 동일 | [pc_read.png](../tc27x_dbg_regs/pc_read.png) |
| `0xF881FE18` | `0x00B7C002` | CPU_ID(TC1.6E) — UM Table 5-19 리셋값·`core_id`와 일치 | [cpuid_read.png](../tc27x_dbg_regs/cpuid_read.png) |

발췌 노트: [tc27x_dbg_regs/log_excerpt.txt](../tc27x_dbg_regs/log_excerpt.txt)

## 4. 프로토타입(`mcd_tas`)에 대한 함의

1. **`mcd_qry_state_f` 실상태 구현 경로 확보** — `DBGSR.HALT[2:1]`로 Run/Halt, `SIH`·`SUSP`·`EVTSRC`로
   상태·정지 소스 파생 가능(현재는 `DEBUG` 고정 캘리브레이션).
2. 원본 `mcdxdas`가 최초 1회 DBGSR(`0xF881FD00`)을 읽은 이유가 명확해짐 — 상태 확인.
3. `mcd_qry_reg_map_f`가 제공하는 PC 주소(`0xF881FE08`)는 문서상 PC CSFR과 정확히 일치 — M4 파리티의 근거.
4. 코어 정보의 `core_id 0x00B7C002`는 CPU_ID CSFR(`FE18H`) 판독값과 동일.

## 5. 출처

- TC27x D-Step User Manual V2.2 — Infineon 공개 PDF:
  <https://www.infineon.com/assets/row/public/documents/10/44/infineon-tc27x-d-step-usermanual-en.pdf>
  (근거 위치: Table 3-3 / §5.6.2 / Table 5-19 / Table 5-24)
- TriCore® TC1.6P & TC1.6E 32-bit Unified Processor Core — User Manual (Volume 1) — Infineon 공개 PDF:
  <https://www.infineon.com/assets/row/public/documents/10/44/infineon-tc2xx-architecture-vol1-usermanual-en.pdf>
  (근거 위치: §12.4.4 Halt / §12.4.11 Suspend-In Halt / §12.9 DBGSR / §12.8 Table 12-1)
- 원문 PDF·추출 덤프는 로컬 분석 폴더(`C:\work\tas_server_analysis\refs\`)에 보관.
