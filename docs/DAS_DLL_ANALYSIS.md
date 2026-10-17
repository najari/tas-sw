# DAS 클라이언트 DLL 구현 분석 — das_api.dll / mcdxdas.dll

- 작성: 2026-10-17
- 분석 대상: `C:\Windows\System32\das_api.dll`, `C:\Windows\System32\mcdxdas.dll` (Infineon DAS64 배포본)
- 관련 문서: [REPORT.md](REPORT.md)(tas_server.exe 분석), [TAS_PROTOCOL_CAPTURE.md](TAS_PROTOCOL_CAPTURE.md)(TAS 프로토콜 해석), [TAS_CLIENT_BUILD_AND_E2E.md](TAS_CLIENT_BUILD_AND_E2E.md)(공식 클라이언트 빌드·E2E)
- 방법: 정적 분석 — dumpbin(MSVC 14.44) 산출물 + Python PE 파서(VA↔파일 오프셋 매핑, 구조체 덤프) + DAS64 SDK 헤더(`das_api.h`, `mcd_api.h`, `mcd_api_das_if.h`) 대조 + 공개 소스 `tas_client_api`와 대조
- 원본 덤프(디스어셈블·문자열·임포트 등)는 저장소에 포함하지 않음 — 로컬 `C:\work\tas_server_analysis\das\` 보관

---

## 0. 한눈에 보기

| | das_api.dll | mcdxdas.dll |
|---|---|---|
| 제품 설명 | "DAS Client DLL" — DAS API 4.3 구현체 (내부 버전 2.3) | "MCD API over DAS" — 1.14.12.0 |
| 익스포트 | `das_api_load` 1개 (팩토리 함수) | `mcd_*_f` 55개 |
| 제공 API | DAS API (`das_api_t`: 함수 포인터 26개 + 메타데이터) | MCD API 1.1 + DAS 연동 함수 4개 |
| 내부 구현 | **TAS 클라이언트 코어 내장** (공개 `tas_client_api`와 동일 계열) → TCP로 `tas_server.exe`와 통신 | **내장 DAS API 구현("DAS API (static lib) ")** + 동일 계열 TAS 클라이언트를 포함한 완결형 MCD 클라이언트 |
| MCD 채널 함수 | 전부 미구현 스텁 (항상 `MCD_RET_ACT_HANDLE_ERROR` 반환) | MCD API 수준은 실장, 내장 DAS 테이블의 채널 슬롯은 스텁 |
| 서버 자동 기동 | 있음 (`start_tas_server.bat` + 전역 뮤텍스) | 있음 (동일 코드·동일 뮤텍스) |

**핵심 결론**: 두 DLL은 Infineon "DAS(Device Access Server)"가 **TAS(도구 접속 소켓, `tas_server.exe`) 위에 재구현된 차세대 스택**이라는 사실을 보여준다. `das_api.dll`은 그 DAS API 측 구현체이고, `mcdxdas.dll`은 **같은 DAS 구현을 "정적 라이브러리" 형태로 내장**한 MCD API 구현체다. 실제 통신은 결국 이전 문서에서 해석한 TAS 프로토콜(TCP 24817)로 수렴한다.

---

## 1. 분석 대상과 배포 구조

### 1.1 파일 신원

| 항목 | das_api.dll | mcdxdas.dll |
|---|---|---|
| 경로 | `C:\Windows\System32\das_api.dll` | `C:\Windows\System32\mcdxdas.dll` |
| 크기 / SHA-256 | 153,128 B / `28D537BBB75ADDA309F989ABBC6F7723E87AC58F11BC6E76CCE0657B2DA4CD9E` | 286,248 B / `5C01307016F6D3E0735CB8C373650A234AD409FD6578E7031A19D8E039D0D2FF` |
| 아키텍처 | x64 (PE32+) | x64 (PE32+) |
| PE 타임스탬프 | 2026-05-01 (링커 14.42) | 2026-05-01 (링커 14.42) |
| 파일 타임스탬프 | 2026-05-02 12:32:12 | 2026-05-02 12:32:06 |
| 버전 정보 | 구현 2.3 / 대상 API 4.3 (테이블 바이트 `02 03 04 03`) | 제품 버전 1.14.12.0, 설명 "MCD API over DAS" |
| 서명 | Authenticode 유효 — CN=Infineon Technologies AG | 동일 |
| 사본 | `C:\Program Files\DAS64\clients\das_api.dll` **해시 동일**, `DAS64\win32\das_api.dll`(32-bit, 138,280 B, SHA-256 `C15C3BC9…5F35A9`) | `DAS64\clients\mcdxdas.dll` **해시 동일**, `DAS64\win32\mcdxdas.dll`(32-bit, 240,168 B, SHA-256 `E15E67B5…05BEC0`) |

### 1.2 배포 트리와 System32 전개

`C:\Program Files\DAS64\` 하위: `clients\`, `servers\`, `win32\`, `include\`, `lib\`, `others\`, `icons\`, `resources\`

- `clients\`: `das_api.dll`, `mcdxdas.dll`, `mcd_basic_client.exe`, `mcds_configurator.dll`, `mcds_decoder.dll`, `mcds_recorder.dll`, `mcds_trace_viewer.exe`, `tas_basic_client_chl.exe`, `tas_basic_client_rw.exe`, `tas_perfmeter.exe` 등
- `servers\`: `tas_server.exe`(345,128 B), `tas_server_console.exe`(346,152 B), `check_redistributable.exe`(88,063 B), `start_tas_server.bat`(101 B)
- `win32\`: 32-bit `das_api.dll`, `mcdxdas.dll`
- `include\`, `lib\`: SDK 헤더/임포트 라이브러리 (`das_api.h`, `das_api.lib`, `mcd_api.h`, `mcd_api_das_if.h` 등)

설치 시 `C:\Windows\System32\`에도 전개되어 있음(모두 2026-05-02 12:32, 설치본과 해시 동일/동일 크기):

- `das_api.dll`, `mcdxdas.dll`
- `mcds_configurator.dll`(8,724,008 B), `mcds_decoder.dll`(10,636,840 B), `mcds_recorder.dll`(96,808 B)
- `tas_server.exe`(345,128 B), `check_redistributable.exe`, `start_tas_server.bat`

`start_tas_server.bat` 실물 내용(System32·`DAS64\servers\` 동일):

```bat
@echo off
check_redistributable.exe
if %ERRORLEVEL% EQU 0 (
    echo.
    tas_server.exe
)
exit
```

### 1.3 이름 충돌 주의 — DAS와 무관한 동명/유사명 파일 (분석 제외)

- `C:\Windows\System32\das.dll`(749,568 B), `dasHost.exe`(200,704 B) — **Microsoft** 구성요소(장치 연결 프레임워크 계열). Infineon DAS와 무관.
- `C:\Windows\System32\RTHDASIO64.dll`(299,920 B) — **Realtek Semiconductor** ASIO 오디오 드라이버(3.1.10.8, WHCP 서명). 이름만 유사.
- "das" 문자열로 검색 시 위 파일들이 먼저 걸릴 수 있으므로 주의.

---

## 2. 전체 아키텍처 (TAS 기반 DAS)

```
[디버거/툴 프로세스]
   │
   ├─ DAS API 호출 (das_api_t) ──────────────► das_api.dll
   │                                              │  · 내장 TAS 클라이언트 코어 (RTTI: CTasClientRw 등)
   │                                              │  · 자동 서버 기동 (start_tas_server.bat)
   └─ MCD API 호출 (mcd_*_f) ──► mcdxdas.dll ─────┤
                                      │  · 내장 DAS API "DAS API (static lib) " (das_api_t 내장 싱글턴)
                                      │  · 내장 TAS 클라이언트 코어 (동일 계열)
                                      ▼
                          TCP 24817 (TAS 프로토콜: PL2/PL1/PL0)
                                      │
                                      ▼
                              tas_server.exe  ──► USB(FTD2XX 등) ──► HW 타깃(TriCore/XC800/…)
```

- 여기서 "DAS 서버"는 전통적 `dasServer`가 아니라 **`tas_server.exe`(TAS 서버)** 이다. `das_api.dll`/`mcdxdas.dll`이 공통으로 그 서버에 접속한다.
- 구 버전 DAS(V3.x 계열)의 하드웨어 추상화·MCD 채널 기능은 이 빌드에서 스텁으로만 남아 있다(§3.7, §4.3).

---

## 3. das_api.dll 상세

### 3.1 인터페이스 계약 (SDK 헤더 `das_api.h`)

- 라이브러리의 유일한 진입점은 `das_api_load(unsigned version_major, unsigned *error_code)` — "표준 사용법: `extern das_api_t *das;` 전역 포인터를 두고 로드한다"고 헤더가 명시.
- `das_api_t` = 메타데이터(name/manufacturer/version/date/init_flag) + 함수 포인터 26개. `das_api.lib`은 `das_api_load`만 익스포트하는 임포트 라이브러리.
- 주요 상수: `DAS_LIST_MAX_TRANSACTIONS = 32`, `DAS_MAX_TRANSFER_SIZE = 1024`, 기본 타임아웃 10초 — 아래 역공학 결과(32개 상한, 0x41C 버퍼 상한 등)와 일치.

### 3.2 `das_api_load` 구현 (VA 0x180001ED0)

디스어셈블 전문(의사코드):

```c
das_api_t *das_api_load(unsigned version_major, unsigned *error_code) {
    if (*error_code != 0) return NULL;                 // 호출자가 0으로 초기화해 전달하는 규약
    if (version_major != 3 && version_major != 4) {    // V3.x/V4.x만 허용
        *error_code = 0x01000000;                      // DAS_ERROR_PARAMETER
        return NULL;
    }
    return &das_api;                                   // 정적 싱글턴 (0x180013000)
}
```

- 반환하는 `das_api_t`는 **런타임 생성이 아니라 .data에 정적으로 초기화된 싱글턴**이다. 여러 프로세스·스레드가 같은 테이블을 공유한다.

### 3.3 `das_api_t` 싱글턴 전체 덤프 (VA 0x180013000, 파일 오프셋 0x11600)

| 슬롯 | 오프셋 | 값 | 의미 |
|---|---|---|---|
| name | +0x00 | → `"DAS Client DLL"` @0x18000F3A0 | 라이브러리 이름 |
| manufacturer | +0x08 | → `"Infineon"` @0x18000F3B0 | |
| version | +0x10 | `02 03 04 03` | 구현 2.3 / 대상 DAS API 4.3 |
| date | +0x18 | → `"Apr 30 2026"` @0x18000F3C0 | 빌드 일자 |
| init_flag | +0x20 | 0 → 1 (`init` 시) @0x180013020 | 초기화 플래그 |
| cancel_lists | +0x28 | 0x180001000 | |
| mcd_chl_open_f | +0x30 | **0x180001D30 (스텁)** | MCD 채널 미구현 |
| close_port | +0x38 | 0x180001030 | |
| connect_to_device | +0x40 | 0x1800010F0 | |
| disconnect_device | +0x48 | 0x180001130 | |
| exec_config_list | +0x50 | 0x180001180 | |
| exit | +0x58 | 0x180001270 | |
| mcd_send_msg_f | +0x60 | **스텁** | |
| get_das_servers | +0x68 | 0x1800012A0 | 서버 목록 조회 |
| get_ids_servers | +0x70 | 0x180001470 | |
| mcd_receive_msg_f | +0x78 | **스텁** | |
| init | +0x80 | 0x180001480 | |
| init_device | +0x88 | 0x180001CD0 | |
| mcd_chl_reset_f | +0x90 | **스텁** | |
| map_port | +0x98 | 0x1800014A0 | |
| mcd_chl_close_f | +0xA0 | **스텁** | |
| open_port | +0xA8 | 0x180001500 | |
| mcd_qry_error_info_f | +0xB0 | **0x180001D40 (no-op)** | |
| mcd_qry_input_handle_f | +0xB8 | **스텁** | |
| send_list | +0xC0 | 0x180001620 | 리스트 실행 |
| tas_fexec | +0xC8 | **0x180001D50 (오류 설정 스텁)** | |
| sleep | +0xD0 | 0x1800017C0 | |
| spawn_port | +0xD8 | 0x180001930 | |
| start_server | +0xE0 | 0x180001B00 | |
| terminate_server | +0xE8 | 0x180001C30 | |
| wait_list | +0xF0 | 0x180001C70 | |
| 확장 (+0xF8) | +0xF8 | 0x18000F8E0 | 추가 함수 테이블(코드 포인터 4개: 0x180009320, 0x180009EF0, 0x180009DD0, 0x180009D40) |
| 확장 (+0x100) | +0x100 | 정수 `3` | 용도 미상 |
| 확장 (+0x108) | +0x108 | 0x18000F920 | 추가 함수 테이블(0x180009320, 0x180009F00, 0x180009E30, 0x180009CC0) |

> +0xF8/+0x100/+0x108은 SDK 헤더(das_api.h V4.x)에 선언되지 않은 **확장 슬롯**이다. mcdxdas.dll의 내장 테이블도 같은 위치·같은 형태(+0xF8=0x180039A78, +0x100=3, +0x108=0x180039AB8)를 가진다.

### 3.4 내장 TAS 클라이언트 코어 — 근거

`das_api.dll`은 DAS 기능을 새로 구현한 것이 아니라, **공개 소스 `tas_client_api`와 동일 계열의 TAS 클라이언트 코어를 통째로 내장**하고 있다:

- **C++ RTTI 클래스**: `CTasClientRw`, `CTasClientRwBase`, `CTasClientServerCon`, `CTasPktMailboxSocket`, `CTasPktHandlerRw`, `CTasPktHandlerBase`, `CTasPktHandlerServerCon`
- **TAS 오류 카탈로그 문자열**(그대로 내장):
  - `ERROR: Could not start TasServer on local machine`
  - `ERROR: Server connection protocol (PL1 packet count)`
  - `ERROR: Function failed at TasServer`, `ERROR: Wrong TAS API usage`, `ERROR: Target not connected`, `ERROR: Server is locked` 등
  - 리스트 검증 메시지: `%s addr=0x%llX, num_bytes=%u, acc_mode=0x%4.4hX, addr_map=%hhu`
- **포트/디바이스 명칭**: `UDAS`, `edas`, `emulator`
- **소켓 코드**: 0x18000C1C8~0x18000C519 구역에서 `WSAStartup` → `socket` → `getaddrinfo` → `connect` (WS2_32 임포트 사용)
- **기본 포트 24817 (0x60F1)**: `get_das_servers`가 호스트 문자열 파싱 시 기본 포트로 사용
- **전역 뮤텍스**: UTF-16 문자열 `Global\only_one_TasServer_on_host`(파일 오프셋 0xE1A0) — 공개 소스 `tas_utils_os.cpp`의 `OpenMutexW(..., L"Global\\only_one_TasServer_on_host")`와 일치

### 3.5 서버 자동 기동 시퀀스 (함수 0x18000A1F0 부근)

서버 접속 경로(0x1800057xx 부근)에서 다음 시퀀스를 실행한다:

1. 환경변수 `TAS_INSTALL_DIR` 확인 → 후보 경로 순서로 배치 확인
   - `%s\start_tas_server.bat`
   - `%s\bin\start_tas_server.bat`
   - `%s\servers\start_tas_server.bat`
2. 없으면 `GetSystemDirectoryA`로 `System32\start_tas_server.bat` fallback
3. 파일 존재 확인(`exists` 문자열)
4. `OpenMutexW`로 `Global\only_one_TasServer_on_host` 검사 — 이미 서버가 실행 중이면 기동 생략
5. `Wow64DisableWow64FsRedirection` → `ShellExecuteA(NULL, "open", <bat>, NULL, NULL, SW_HIDE)` → `Wow64RevertWow64FsRedirection`
   - 즉 **콘솔 창을 숨긴 채** `check_redistributable.exe` → `tas_server.exe`를 실행
6. 클라이언트 이름 준비: `GetUserNameA` → `"%s"`(16바이트) — 접속 시 사용자명이 클라이언트 식별자로 쓰인다
7. 실패 시: `ERROR: Could not start TasServer on local machine`

### 3.6 주요 함수 동작 (디스어셈블 기반)

| 함수 | VA | 관찰 내용 |
|---|---|---|
| `init` | 0x1480 | `*error_code==0`이면 내부 초기화(0x2470 호출) 후 `init_flag`(테이블+0x20)=1 설정 |
| `get_das_servers` | 0x12A0 | 내부 조회 객체 생성 후 가상 호출(타임아웃 2000 ms)로 서버 확인; 호스트 파싱 시 기본 포트 24817 적용; 성공 시 `das_server_info_t`를 **1대**로 합성(server_host_addr=호스트, port, name, manufacturer="Infineon"). 식별 문자열 `"DAS API DLL"` 사용 |
| `open_port` | 0x1500 | 1번째 인자가 0이 아니면 `DAS_ERROR_PARAMETER`. 내부 포트 객체 0x778 바이트 생성(operator new → ctor 0x1FD0), 실오픈 0x40F0, 성공 시 공개 `das_port_struct_t`(0x38 B)를 할당해 내부 포인터 6개를 채워 반환 |
| `map_port` | 0x14A0 | 포트 객체 magic `0x1B30CCF7` + 상태([port+0x170]) 검증; 옵션은 0/1만 허용; 위반 시 `DAS_ERROR_PORT_ACCESS`(0x100) |
| `send_list` | 0x1620 | magic·상태(magic 0x1B30CCF7, 상태 3/4) 검증; **트랜잭션 수 상한 0x20=32**(헤더 `DAS_LIST_MAX_TRANSACTIONS`와 일치); 트랜잭션 24바이트 단위(addr/num_bytes/acc_mode/addr_map); `addr_map ≤ 0x8C`; `acc_mode` 상위 니블 0x10으로 읽기 계열 구분; 전체 버퍼 상한 **0x41C(1052 B)**; 리스트 플래그 0x100 검사 후 TAS 계층으로 실행 위임 |

### 3.7 MCD 관련 슬롯 — 미구현 스텁 (바이트 수준)

| 스텁 | 주소 | 기계어 | 동작 |
|---|---|---|---|
| 공용 스텁 | 0x180001D30 | `B8 03 00 00 00 C3` | `mov eax,3; ret` → `MCD_RET_ACT_HANDLE_ERROR`(3) 반환 |
| `mcd_qry_error_info_f` | 0x180001D40 | `C2 00 00` | `ret 0` (아무 것도 안 함) |
| `tas_fexec` | 0x180001D50 | `mov rax,[rsp+40h]; mov dword [rax],80000h; ret` | `*error_code = 0x00080000` (`DAS_ERROR_COMMAND_FAILED`) 설정 |

적용 대상: `mcd_chl_open_f`, `mcd_send_msg_f`, `mcd_receive_msg_f`, `mcd_chl_reset_f`, `mcd_chl_close_f`, `mcd_qry_input_handle_f`(전부 0x1D30), `mcd_qry_error_info_f`(0x1D40), `tas_fexec`(0x1D50).
→ V4.3 인터페이스 **호환용 슬롯**이며, 이 빌드의 DAS API 계층에서 MCD 채널/원격 실행 기능은 제공하지 않는다(MCD 기능이 필요하면 mcdxdas.dll 쪽 사용).

### 3.8 오류 코드 체계 (`das_api.h` 발췌)

| 코드 | 상수 |
|---|---|
| 0x00000001/02/04/08 | `DAS_ERROR_DEVICE_RESET/LOCKED/ACCESS/DATA` |
| 0x00000080 | `DAS_ERROR_ADDR_RANGE_RESERVED` |
| 0x00000100 | `DAS_ERROR_PORT_ACCESS` |
| 0x00001000 | `DAS_ERROR_SERVER_LOCKED` |
| 0x00010000 | `DAS_ERROR_TIMEOUT` |
| 0x00020000 | `DAS_ERROR_LIST_RULE` |
| 0x00080000 | `DAS_ERROR_COMMAND_FAILED` |
| 0x01000000 | `DAS_ERROR_PARAMETER` (치명) |
| 0x02000000 | `DAS_ERROR_CONNECTION` (치명) |

---

## 4. mcdxdas.dll 상세

### 4.1 MCD API 개요

- MCD API(Multi-Core Debug)는 유럽 SPRINT 프로젝트 산출물로 **ARM, Infineon, NXP, Lauterbach, STMicroelectronics, TIMA**가 공동 정의한 툴↔디버거 인터페이스다(`mcd_api.h` 라이선스 헤더).
- 함수명 접미사 `_f`, 버전 1.1, 저자 상수 `MCD_API_VER_AUTHOR = "SPRINT Release"`.
- 이 DLL의 설명 문자열은 "MCD API over DAS" — 즉 **MCD API를 DAS 전송 계층 위에서 구현**한 클라이언트다.

### 4.2 익스포트 55개 (`mcd_*_f`)

전체 목록은 [부록 B](#9-부록-b-mcdxdasdll-익스포트-55개-전체-목록). 분류:

- 초기화/종료: `mcd_initialize_f`, `mcd_exit_f`
- 서버: `mcd_open_server_f`, `mcd_close_server_f`, `mcd_qry_servers_f`, `mcd_qry_server_config_f`, `mcd_set_server_config_f`
- 시스템/장치/코어: `mcd_qry_systems_f`, `mcd_qry_devices_f`, `mcd_qry_cores_f`, `mcd_open_core_f`, `mcd_close_core_f`, `mcd_qry_state_f`, `mcd_qry_core_modes_f`
- 메모리/레지스터: `mcd_qry_mem_spaces_f`, `mcd_qry_mem_blocks_f`, `mcd_qry_reg_groups_f`, `mcd_qry_reg_map_f`, `mcd_qry_reg_compound_f`
- 리스트 실행/트레이스/트리거/실행 제어/메시지: `mcd_execute_txlist_f`, `mcd_qry_traces_f`, `mcd_qry_trace_state_f`, `mcd_set_trace_state_f`, `mcd_read_trace_f`, `mcd_create_trig_f`, `mcd_qry_trig_f`, `mcd_activate_trig_set_f`, `mcd_run_f`, `mcd_stop_f`, `mcd_step_f`, `mcd_rst_f`, `mcd_chl_open_f`, `mcd_send_msg_f`, `mcd_receive_msg_f`, …
- DAS 연동(비표준 확장, `mcd_api_das_if.h`): `mcd_das_get_das_lib_f`, `mcd_das_get_das_port_f`, `mcd_das_initialize_f`, `mcd_das_open_core_f`

### 4.3 핵심 발견 — 내장 DAS API ("DAS API (static lib) ")

`mcdxdas.dll`은 das_api.dll을 로드하지 않는다:

- 정적 임포트에 `das_api.dll` 없음, **delay-import 디렉터리 없음, `LoadLibrary`/`GetProcAddress` 임포트 자체가 없음** → 동적 로딩 불가능/불수행.

대신 자체적으로 **DAS API 구현 전체를 내장**한다:

- **전역 포인터 `das` @0x180041000**: .data 정적 초기값이 `0x1800415D0` — 내부 `das_api_t` 싱글턴을 가리킴. (런타임에 이 전역을 갱신하는 명령은 발견되지 않음 — 정적 초기화가 유일한 설정 경로)

| 내장 `das_api_t` @0x1800415D0 | 값 |
|---|---|
| name | `"DAS API (static lib) "` @0x180039548 |
| manufacturer | `"Infineon"` @0x180036DD0 |
| version | `02 03 04 03` (구현 2.3 / API 4.3 — das_api.dll과 동일) |
| date | `"Apr 30 2026"` @0x180033D68 |
| 18개 실함수 | cancel_lists=0x25050, close_port=0x25080, connect_to_device=0x25140, disconnect_device=0x25180, exec_config_list=0x251D0, exit=0x252C0, get_das_servers=0x252F0, get_ids_servers=0x25560, init=0x25570, init_device=0x25E50, map_port=0x25590, open_port=0x255F0, send_list=0x25710, sleep=0x258B0, spawn_port=0x25A20, start_server=0x25BF0, terminate_server=0x25DB0, wait_list=0x25DF0 |
| `mcd_qry_error_info_f` | 0x22AC0 — 별도 실구현(스텁 아님) |
| mcd_chl_*/qry_input_handle | 0x25EB0 — 공용 스텁(`mov eax,3; ret`, das_api.dll 0x1D30과 동일 패턴) |
| `tas_fexec` | 0x25EC0 — `*error_code=0x80000` 스텁(동일 패턴) |
| 확장 슬롯 | +0xF8=0x180039A78, +0x100=3, +0x108=0x180039AB8 |

→ `"DAS API (static lib) "` 문자열이 이 설계의 이름표다. DAS64는 DAS API를 **① 별도 DLL(`das_api.dll` — "DAS Client DLL") 또는 ② 정적 라이브러리로 내장**하는 두 형태로 제공하며, mcdxdas.dll은 ② 형태로 **MCD API + DAS API + TAS 클라이언트가 한 몸에 들어 있는 완결형 클라이언트**다. (관련 문자열: `Automatic DLL Search`, `DAS API (static lib) `, `MCD API (legacy devices)`)

### 4.4 초기화 흐름 — `mcd_initialize_f` = `mcd_das_initialize_f` (RVA 0x2AF0 → jmp 0x2C90)

두 익스포트는 **동일 코드**(같은 RVA)다. 동작:

1. **`version_req` 검증**: `+4` 오프셋의 author 문자열이 `"SPRINT Release"`(`MCD_API_VER_AUTHOR`)와 일치해야 함 — 불일치 시 `Unknown API version author ("%s" required)`, 상태 3, 코드 0x10000000
2. **중복 초기화 방지**: 이미 초기화됨(플래그 0x180041DF8≠0)이면 코드 **0x101** 오류
3. **성공 경로**:
   - 오류 블록/작업 버퍼 클리어(0x180041E00 0x10C, 0x180041F10 0x800, 0x180042710 0x26000, 0x180068718 0x29000)
   - 초기화 플래그 `[0x180041DF8] = 1`
   - `mivi`(2번째 인자) 채움: `v_api = {1, 1, "SPRINT Release"}`(0x00010001), **`v_imp = 1.14.12`**(+0x24/+0x26/+0x28 = 0001/000E/000C — 파일 버전 1.14.12.0과 일치), vendor `"Infineon Technologies"`(+0x2A), date `"Apr 30 2026"`(+0x4A)
   - DAS 클라이언트 정보 구성(버전 4.3 바이트 0x304, `"MCD API Client"` 등) 후 **내장 `das`의 `init` 호출**([0x180041000]+0x80)
   - 내장 `das` 포인터가 NULL이거나 초기화 실패 시: `Could not load DAS API library V4.x` (상태 3, 코드 0x10000000)
4. 반환: 성공 `0`(`MCD_RET_ACT_NONE`) / 실패 `3`(`MCD_RET_ACT_HANDLE_ERROR`)

> `Could not load DAS API library V4.x`는 이름과 달리 **DLL 로딩 실패가 아니라 내장 DAS 스택 초기화 실패**를 뜻한다(LoadLibrary 미임포트가 근거). 문서상 `mcd_das_initialize_f`의 3번째 인자(외부 `das_api_t` 테이블)는 이 빌드에서 **전역 `das`를 바꾸는 데 사용되지 않는다** — 항상 내장 구현을 쓴다.

### 4.5 DAS 연동 함수 (`mcd_api_das_if.h` 대조)

| 함수 | RVA | 동작 |
|---|---|---|
| `mcd_das_get_das_lib_f` | 0x2930 | 문서 "Case 1"(MCD 접속 후 DAS 수준 접근용). 초기화 확인 후 **내장 `das` 포인터 반환**; 미초기화 시 코드 0x101 오류 |
| `mcd_das_get_das_port_f` | 0x29F0 | core 객체에서 해당 코어의 **DAS port** 추출 |
| `mcd_das_open_core_f` | 0x2B00 | 문서 "Case 2"(DAS port + core_sel → core 생성) |
| `mcd_das_initialize_f` | 0x2AF0 | 위 4.4 (mcd_initialize_f와 동일) |

주소공간 규약(헤더): `MCD_DAS_AMAP_SPACE_OFFSET = 0xDADADA00`, `MASK = 0xFFFFFF00` — MCD 주소공간 ID 상위 바이트로 DAS `addr_map`을 유도하는 표준 변환.

### 4.6 MCD→DAS 매핑 정황 (디스어셈블)

- **리스트 실행 헬퍼 @0x2FA0**: DAS 리스트/트랜잭션을 구성(관찰 상수 `0x428401`, `0xC00B0000` 등)한 뒤 **내장 `das`의 `exec_config_list`([0x41000]+0x50)를 직접 호출** — MCD 트랜잭션 리스트가 DAS 리스트로 변환되어 실행되는 경로.
- **`mcd_open_server_f` @0x4B80**: 구성 문자열 파싱 함수(0x3080 부근)에서 다음 키 사용 — `McdHostName`(기본값 `"localhost"`), `McdAccHw`, `McdServerName`, `McdServerKey`, `McdAccHw.Address`, `McdAccHw.Port`; 이어 내장 `get_das_servers`([0x41000]+0x68) 호출로 서버 탐색.
- **오류/상태 카탈로그(문자열)**: `Could not get DAS Server list`, `DAS Server not found`, `Could not open DAS Server`, `DAS Server connection`, `Device not (anymore) connected to DAS Server`, `DAS Error 0x%8.8X`, `Access to this address is restricted by reservation of another tool`, `Server is already running`
- **내장 TAS 클라이언트 증거**: 동일 RTTI 클래스 + TAS 오류 문자열 + `TAS_INSTALL_DIR` 참조 코드(0x2E36D, 문자열 0x180039B78) + 소켓 호출부(0x1800302F8 부근: WSAStartup/socket/getaddrinfo/connect) + 전역 뮤텍스 `Global\only_one_TasServer_on_host`(UTF-16, 파일 오프셋 0x38530)

### 4.7 런타임 상태 저장소

| 주소 | 용도 |
|---|---|
| 0x180041000 | 전역 `das` (내장 das_api_t 포인터) |
| 0x180041DF0 | 콜백 포인터(트레이스 등, 초기 0) |
| 0x180041DF8 | 초기화 플래그(0 → 1) |
| 0x180041E00 | 오류 블록: 상태 dword + 상세 코드 qword(+4) + 텍스트 0x100바이트(0x41E0C) + NUL(0x41F0B) |
| 0x180041F10 / 0x180042710 / 0x180068718 | 작업 버퍼(0x800 / 0x26000 / 0x29000) |

### 4.8 기타 문자열 — 레거시 지원·디바이스 명칭

`UDAS`, `edas`, `emulator`, `PCP1`, `PCP2`, `TriCore 1.2/1.3/1.3.1/2.0`, `C166SV2`, `XC800` — 전통 DAS의 포트/디바이스 패밀리 명칭이 설정·레거시 경로에 잔존. UIO 명령 문자열(`IO_*`)과 `MCD API (legacy devices)`도 포함.

### 4.9 소비자 예 — `mcd_basic_client.exe`

- 정적 임포트: 시스템 DLL(GDI32/ole32/COMCTL32/KERNEL32/USER32/SHELL32/ADVAPI32) + VC 런타임/CRT만 — `mcdxdas.dll`/`das_api.dll` 정적 임포트 **없음**
- KERNEL32에서 `LoadLibraryA`/`GetProcAddress`/`FreeLibrary` 사용, 바이너리 내 `mcdxdas`(ASCII, 파일 0x381F8)·`mcd_api`(0x48EA8) 문자열 존재
- → **런타임에 `mcdxdas.dll`을 찾아 로드하고 함수 테이블을 얻는 방식** (MCD API의 "구현 DLL 교체" 모델)

---

## 5. das_api.dll vs mcdxdas.dll 비교

| 항목 | das_api.dll | mcdxdas.dll |
|---|---|---|
| API 표면 | DAS API 4.3 (`das_api_load` 팩토리 → `das_api_t`) | MCD API 1.1 (55개 함수 직접 익스포트) + DAS 연동 4개 |
| 역할 | DAS 클라이언트 (DasServer 대체 = TAS 기반) | MCD 클라이언트 (내장 DAS 포함) |
| 내장 DAS 구현 | 본체 자체 | 내장 사본 — `"DAS API (static lib) "` @0x1800415D0 |
| 내장 TAS 클라이언트 | 있음 | 있음 (동일 계열 코드) |
| 서버 자동 기동 | 있음 | 있음 (동일 bat·뮤텍스) |
| MCD 채널 슬롯 | 전부 스텁 | MCD API 수준 실장, 내장 DAS 표의 채널 슬롯은 스텁 |
| 소비 방식 | `das_api.lib` 링크 또는 `das_api_load` 호출 | `LoadLibrary`로 로드(예: mcd_basic_client.exe) + 함수 테이블 연결 |

---

## 6. 운영·보안 관찰

- **System32 전개**: `das_api.dll`/`mcdxdas.dll`/`tas_server.exe`/`start_tas_server.bat`까지 System32에 배치되어, 아무 툴 프로세스나 PATH에서 바로 로드·기동 가능. (이전 REPORT.md §4.1 기록과 일치)
- **자동 기동**: 접속 실패 시 콘솔 숨김(`SW_HIDE`)으로 `check_redistributable.exe` → `tas_server.exe`를 실행하며, WOW64 파일시스템 리다이렉션을 일시 해제한다. 사용자에게 창이 보이지 않는다.
- **호스트당 서버 1개**: 전역 뮤텍스 `Global\only_one_TasServer_on_host`로 상호 배제(이전 중복 실행 가드 검증 로그와 일치).
- **클라이언트 식별 = Windows 사용자명**(`GetUserNameA`) — 서버 측 로그/감사에 남는다.
- **전송 보안**: TAS 프로토콜은 평문 TCP(기본 24817)이며 인증은 서버 lock/비밀번호 기능에 의존한다 — [REPORT.md](REPORT.md), [TAS_PROTOCOL_CAPTURE.md](TAS_PROTOCOL_CAPTURE.md) 참조.
- 두 DLL 모두 **Infineon Technologies AG Authenticode 서명 유효**.

---

## 7. 재현 방법

```powershell
# 1) PE 구조/임포트/익스포트
dumpbin /headers  C:\Windows\System32\das_api.dll > das_api_headers.txt
dumpbin /exports  C:\Windows\System32\das_api.dll > das_api_exports.txt
dumpbin /imports  C:\Windows\System32\das_api.dll > das_api_imports.txt
dumpbin /disasm   C:\Windows\System32\das_api.dll > das_api_disasm.txt   # mcdxdas.dll 동일

# 2) 문자열 추출(ASCII/UTF-16) — Python
# 3) VA↔파일 오프셋 매핑 후 싱글턴 덤프(0x180013000 / 0x1800415D0)
#    — Python PE 파서(섹션 테이블 → RVA 변환 → 포인터/문자열 해석)
# 4) 서명/해시
Get-AuthenticodeSignature C:\Windows\System32\das_api.dll
Get-FileHash C:\Windows\System32\mcdxdas.dll -Algorithm SHA256
```

분석 산출물 위치: `C:\work\tas_server_analysis\das\` (덤프 원본 — 저장소 미포함)

---

## 8. 부록 A — 주요 주소·오프셋 요약

**das_api.dll**

| 요소 | 주소 | 비고 |
|---|---|---|
| `das_api_load` | 0x180001ED0 | 단일 익스포트 |
| `das_api_t` 싱글턴 | 0x180013000 | .data, 파일 오프셋 0x11600 |
| init_flag | 0x180013020 | 바이트 |
| `"DAS Client DLL"` | 0x18000F3A0 | |
| `"Infineon"` / `"Apr 30 2026"` | 0x18000F3B0 / 0x18000F3C0 | |
| `"DAS API DLL"` | 0x18000F3E8 | 서버 조회 식별자 |
| `get_das_servers` / `init` / `open_port` / `map_port` / `send_list` | 0x12A0 / 0x1480 / 0x1500 / 0x14A0 / 0x1620 | |
| 스텁 3종 | 0x1D30 / 0x1D40 / 0x1D50 | |
| 자동 기동 함수 | 0x18000A1F0 부근 | TAS_INSTALL_DIR → bat → ShellExecuteA |
| 뮤텍스 문자열(UTF-16) | 파일 0xE1A0 | `Global\only_one_TasServer_on_host` |
| 소켓 구역 | 0x18000C1C8~0x18000C519 | WSAStartup/socket/getaddrinfo/connect |

**mcdxdas.dll**

| 요소 | 주소 | 비고 |
|---|---|---|
| 전역 `das` | 0x180041000 | 초기값 0x1800415D0 |
| 내장 `das_api_t` | 0x1800415D0 | `"DAS API (static lib) "` |
| 초기화 플래그 | 0x180041DF8 | |
| 오류 블록 | 0x180041E00 | 상태/코드/텍스트(0x100) |
| `mcd_das_get_das_lib_f` | 0x2930 | |
| `mcd_das_get_das_port_f` | 0x29F0 | |
| `mcd_das_initialize_f` = `mcd_initialize_f` | 0x2AF0 → 0x2C90 | |
| `mcd_das_open_core_f` | 0x2B00 | |
| 리스트 실행 헬퍼 | 0x2FA0 | 내장 `exec_config_list` 호출 |
| `mcd_open_server_f` | 0x4B80 | 구성 키 `Mcd*` |
| 내장 DAS 실함수 대역 | 0x25050~0x25DF0 | 18개 |
| 내장 스텁 | 0x25EB0(채널) / 0x25EC0(tas_fexec) | das_api.dll과 동일 패턴 |
| 소켓 호출부 | 0x1800302F8 부근 | WSAStartup/getaddrinfo/connect |
| 뮤텍스 문자열(UTF-16) | 파일 0x38530 | `Global\only_one_TasServer_on_host` |

---

## 9. 부록 B — mcdxdas.dll 익스포트 55개 전체 목록

```
RVA      이름
0x3FE0  mcd_activate_trig_set_f
0x4060  mcd_chl_close_f
0x40F0  mcd_chl_open_f
0x4180  mcd_chl_reset_f
0x4210  mcd_close_core_f
0x4290  mcd_close_server_f
0x42E0  mcd_create_trig_f
0x2930  mcd_das_get_das_lib_f
0x29F0  mcd_das_get_das_port_f
0x2AF0  mcd_das_initialize_f
0x2B00  mcd_das_open_core_f
0x4380  mcd_execute_txlist_f
0x4560  mcd_exit_f
0x2AF0  mcd_initialize_f
0x46A0  mcd_open_core_f
0x4B80  mcd_open_server_f
0x4BF0  mcd_qry_active_overlays_f
0x4C90  mcd_qry_core_modes_f
0x4D30  mcd_qry_cores_f
0x5010  mcd_qry_ctrigs_f
0x50C0  mcd_qry_current_time_f
0x5150  mcd_qry_device_description_f
0x51F0  mcd_qry_devices_f
0x5270  mcd_qry_error_info_f
0x5370  mcd_qry_max_payload_size_f
0x5410  mcd_qry_mem_blocks_f
0x54D0  mcd_qry_mem_spaces_f
0x5580  mcd_qry_reg_compound_f
0x5640  mcd_qry_reg_groups_f
0x56F0  mcd_qry_reg_map_f
0x57B0  mcd_qry_rst_class_info_f
0x5860  mcd_qry_rst_classes_f
0x5900  mcd_qry_server_config_f
0x5A80  mcd_qry_servers_f
0x5B10  mcd_qry_state_f
0x5BA0  mcd_qry_systems_f
0x5CF0  mcd_qry_trace_state_f
0x5D60  mcd_qry_traces_f
0x5DD0  mcd_qry_trig_f
0x5E70  mcd_qry_trig_info_f
0x5F10  mcd_qry_trig_set_f
0x5FC0  mcd_qry_trig_set_state_f
0x6060  mcd_qry_trig_state_f
0x6100  mcd_read_trace_f
0x6170  mcd_receive_msg_f
0x6230  mcd_remove_trig_f
0x62C0  mcd_remove_trig_set_f
0x6350  mcd_rst_f
0x6400  mcd_run_f
0x64A0  mcd_run_until_f
0x6550  mcd_send_msg_f
0x6600  mcd_set_server_config_f
0x66D0  mcd_set_trace_state_f
0x6740  mcd_step_f
0x67F0  mcd_stop_f
```

---

### 부록 C — 증거 파일 (로컬, 저장소 미포함)

`C:\work\tas_server_analysis\das\`의 `das_api_headers.txt`, `das_api_exports.txt`, `das_api_imports.txt`, `das_api_disasm.txt`, `das_api_strings_ascii.txt`, `das_api_strings_utf16.txt`, `das_api_lib_exports.txt`, `mcdxdas_headers.txt`, `mcdxdas_exports.txt`, `mcdxdas_imports.txt`, `mcdxdas_disasm.txt`, `mcdxdas_strings_ascii.txt`, `mcdxdas_strings_utf16.txt`, `mcd_basic_client_imports.txt`, `das_dll_headers.txt`, `das_dll_exports.txt`, `dasHost_headers.txt` 등

> 검증 환경: Windows / Python 3.13 / MSVC 2022 + dumpbin 14.44 / DAS64 설치본 + 실기(TC27x) 세션에서 로드 상태 확인.
