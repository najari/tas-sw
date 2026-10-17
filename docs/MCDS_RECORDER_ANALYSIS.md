# MCDS Recorder Library (`mcds_recorder.dll`) 정적 분석 (2026-10-18)

- 분석 대상: `C:\Program Files\DAS64\clients\mcds_recorder.dll` (사본: `C:\work\mcd_tas\mtv_dropin\mcds_recorder.dll`)
- 방법: dumpbin(MSVC 14.44) /imports·/disasm + Python PE 파서(헤더·섹션·익스포트·문자열) + 런타임 관찰(모듈·netstat)
- 관련: [MCD_TOOLS_ANALYSIS.md](MCD_TOOLS_ANALYSIS.md)(Trace Viewer §3) · [MCD_TAS_PROTOTYPE.md](MCD_TAS_PROTOTYPE.md)(§3-O M13 확인)

## 0. 한눈에 보기

| 항목 | 값 |
|---|---|
| 제품/버전 | **MCDS Recorder Library** (Infineon Technologies AG) **3.3.0.0** |
| 파일 | x64 PE DLL, 96,808 B, 빌드 2025-08-07, image base `0x180000000` |
| SHA256 | `07FEE00BA9723A96C9A0B93376E37C408F29552EF55FF3B78973E00D14621033` |
| 익스포트 | **22개 `mcdsrl_*`** — 디바이스·데이터·녹화·파일·쓰기·유틸 |
| 통신 | **자체 TAS TCP 클라이언트 내장**(WS2_32 **서수 임포트** = 전체 소켓 세트; `localhost`, `\start_tas_server.bat`) |
| 역할 | `mcds_trace_viewer.exe`가 동적 로드하는 **MCDS 녹화 엔진** — 타깃 접속·MCDS 설정·트레이스 FIFO 읽기·`.mcds` 파일 기록 |
| 관계 | MCD API(`mcdxdas.dll`)와 무관(별도 서브시스템) — M13에서 미로드 실증 |

## 1. 익스포트 (22개, 전부 `mcdsrl_*`)

| 분류 | 함수 (RVA) |
|---|---|
| 디바이스 | `mcdsrl_connect_device`(0x45F0) · `device_connected`(0x4610) · `disconnect_device`(0x4680) · `qry_devices`(0x48E0) · `reset_device`(0x4B20) |
| 데이터 | `get_data_from_fifo`(0x46E0) · `read`(0x4990) · `read_block`(0x49E0) · `read_list`(0x4A40) · `read_timestamped`(0x4AC0) |
| 녹화 | `get_recstate`(0x4750) · `start_recording`(0x4C30) · `stop_recording`(0x4C40) · `set_trace_buf`(0x4BD0) · `set_mcdsconfig`(0x4BB0) |
| 파일/쓰기 | `open_trace_file`(0x4860) · `write`(0x4D80) · `write_block`(0x4DD0) · `write_list`(0x4E30) |
| 유틸 | `lib_init`(0x4770) · `lib_version`(0x4850) · `get_error`(0x4740) |

- **스텁 패턴**: 각 익스포트는 **전역 컨텍스트(`0x180014000`, .data)를 첫 인자로** 넘겨 내부 구현으로 점프하는 썬(thon) 스텁:
  `mcdsrl_connect_device: mov r8,rdx; mov rdx,rcx; lea rcx,[ctx]; jmp 0x3A80`.
  `mcdsrl_lib_version: lea rax,[0x18000FED8]; ret` — 정적 버전 구조체 반환.
- **핸들 없는 단일 전역 상태** — 문자열 `"Call mcdsrl_lib_init() only once"`와 부합(라이브러리 인스턴스 1개).
- 뷰어는 이 DLL에서 **25개 심볼**을 참조(22 익스포트 + 타입명 `mcdsrl_cplty_st`·`mcdsrl_recstate_et`·`mcdsrl_trace_state_st`).

## 2. 임포트 — "소켓이 없는 DLL"의 진실

- 이름 기반 임포트 스캔에는 `ws2_32`가 `freeaddrinfo/getaddrinfo` 2개만 보임 — **그러나 dumpbin으로 보면 대부분의 소켓 함수가
  "서수(Ordinal) 임포트"** 로 들어 있다(구형 ws2_32 임포트 라이브러리로 빌드된 흔적):

| 서수 | 함수 | 서수 | 함수 |
|---|---|---|---|
| 3 | closesocket | 18 | select |
| 4 | connect | 19 | send |
| 7 | getsockopt | 21 | setsockopt |
| 9 | htons | 23 | socket |
| 10 | inet_addr | 111/112 | WSAGet/SetLastError |
| 16 | recv | 115/151 | WSAStartup / __WSAFDIsSet |

  → **일반 소켓 TCP 클라이언트 일체** (스트림 소켓 + select 기반 블로킹 I/O).
- 기타 주목 임포트:
  - KERNEL32: `OpenMutexW`(서버 중복 가드 `Global\only_one_TasServer_on_host`), `ShellExecuteA`(SHELL32, `\start_tas_server.bat` 기동),
    `GetSystemDirectoryA`/`Wow64*FsRedirection`(서버 경로/WOW64 처리), `Sleep`, `TerminateProcess`.
  - ADVAPI32: `GetUserNameA` → **세션 사용자명**(Trace Viewer의 Session Name 기본값 `admin`의 출처).
  - CRT: `fopen/fwrite/fclose`(트레이스 파일), `_beginthreadex`(수신 스레드 추정), `qsort`.

## 3. 문자열로 본 기능 (발췌)

- **TAS 클라이언트 내장**: `MCDS recorder`(SERVER_CONNECT 세션명), `localhost`, `\start_tas_server.bat`,
  `Global\only_one_TasServer_on_host`, 오류군 — `Server connection protocol (PL1 packet count)`,
  `Session not yet started/of…`, `Could not exclusively subscribe to channel %d …`,
  `msg_length too high for TAS_CHL_TGT_TRC`, `TasServer does not support continuous trace`,
  `Could not read channel description from device`, `Could not start TasServer on local machine` 등.
  (tas-sw `TAS_PROTOCOL_CAPTURE`/`TAS_CLIENT_BUILD_AND_E2E`에서 본 공식 TAS 클라이언트 계열의 **CHL(채널)** 기능 포함.)
- **디바이스 계층**: `TC2XX_MINIMCDS` / `TC3XX_MINIMCDS`, `Device has no on-chip trace HW`.
- **MCDS 설정 검증(레지스터 지식)**: OCS 초기상태(CRSV_HAR: Halt After Reset 필요),
  FIFOBOT/FIFOTOP 일관성, `MCTRL_RECBUF_RAM`(FIFOTOP vs `TMECTRL.SIZE`), `MCXACT29`(MCDS0/MCDS1 일치),
  EMEM 구성, `START_OF_MCDS_CONFIG`, `Configured trace size is 0KB`, `Trace to RAM is out of range`.
- **녹화 흐름**: `Set mcdsconfig first`, `Recording already started`,
  `Call mcdsrl_stop_recording() before calling mcdsrl_start_recording again`, `No trace data available/recorded`.
- **파일**: `Could not open trace file`, `Open trace file exists` (.mcds 파일 기록/잠금).
- 기타: `Device not halted after reset (unplug and plug the USB cable of the miniWiggler)`(DAS 미니위글러 계열에서도 사용).

## 4. 아키텍처와 런타임 증거

```
mcds_trace_viewer.exe (UI, 자체 단명 TCP 연결로 열거 폴링)
   └─ LoadLibrary("mcds_recorder.dll") → mcdsrl_* 22개
        ├─ 자체 TAS TCP 클라이언트: SERVER_CONNECT("MCDS recorder") … DEVICE_CONNECT
        ├─ MCDS 설정(set_mcdsconfig) / FIFO·레지스터 읽기(read*/get_data_from_fifo) ← 0x50000090 상수 포함
        └─ .mcds 파일 기록(open_trace_file/write*)   [표시용 디코딩: mcds_decoder.dll]
```

- 런타임 관찰(M13/M14): 같은 폴더의 `mcdxdas.dll`(프로토타입)은 **미로드**; 연결 시 **뷰어 PID가
  `127.0.0.1:24817`에 ESTABLISHED 소켓을 소유**(= 내부 recorder의 소켓), 열거용 단명 연결은 TIME_WAIT로 다수 관측.
- 정상 시절 캡처(`MCD_TOOLS_ANALYSIS` §3.3–3.4)의 `MCDS recorder` 세션·트레이스 폴링(m_b: AM15 `0x50000090` RD64 증가 카운터)은
  **이 DLL이 수행한 것**으로 귀속 정리됨.

## 5. 미확정 / 후속

- `mcdsrl_*` 개별 시그니처·구조체 레이아웃 — 헤더 비공개(뷰어에서 확인된 타입명 3종뿐).
- `.mcds` 파일 포맷 레코드 구조 — `mcds_decoder.dll`(10.6MB) 쪽 분석 과제.
- MCDS 설정 write 시퀀스(OCS/FIFOBOT/… )의 TAS 프레임 수준 해석 — 선택(정상 시절 캡처에 일부 존재).

## 부록. 증거

- [m14_dropin/](../m14_dropin/): `recorder_identity.txt` · `recorder_exports.txt` · `recorder_imports.txt` · `recorder_strings_key.txt`
- 로컬 원본 덤프(`C:\work\mcd_tas\mtv_dropin\`): `recorder_imports.txt`(dumpbin 전문) · `mcds_recorder_disasm.txt`(/disasm nobytes)
