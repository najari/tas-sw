# TAS Server (tas_server.exe) 분석 리포트

- 분석 대상: `C:\Program Files\DAS64\servers\tas_server.exe` (+ `tas_server_console.exe`) — Infineon TAS/DAS 8.4.0
- 분석일: 2026-10-17 (KST)
- 환경: Windows x64, dumpbin 14.44 (VS2022), TAS/DAS 8.4.0 설치는 2026-10-17 14:03, 보드: KIT_AURIX_TC275 (DAS JTAG debugger 연결)
- 방법: 정적 분석(PE 헤더·임포트·디스어셈블·문자열) + 오픈소스 클라이언트 소스(`tas_client_api` v1.2.4) 대조 + 런타임 검증(비파괴)
- 원본 산출물: 같은 폴더의 *.txt / *.log 파일 (문서 말미 목록)

---

## 0. 요약

1. tas_server.exe는 **단일 정적 x64 exe**다. 외부 의존은 `FTD2XX.dll`, `WS2_32.dll`, CRT 계열뿐이며 **레지스트리·파일 I/O를 쓰지 않는다**(설정=환경변수/인자, 로그=stdout).
2. 서버 본체는 **순수 TCP(평문) 서버**다. 기본 포트 24817(`TAS_SERVER_PORT` 환경변수로 오버라이드), 디바이스 서버 기본 포트 24823. `bind`는 **INADDR_ANY(0.0.0.0)**.
3. JTAG/DAP/SPD/SWD 신호는 외부 DLL 없이 **FTDI D2XX(FT_SetBitMode = MPSSE/비트뱅)로 직접 생성**한다. (JDS miniWiggler 계열)
4. 클라이언트 측 API는 오픈소스 `tas_client_api`가 정본이며, 서버와 **동일한 패킷 계층 클래스**(CTasPktHandlerBase 등)를 공유한다 → 와이어 프로토콜은 클라이언트 소스가 곧 정의서.
5. **설치기가 System32에 서버 일습을 배포**하고, **클라이언트가 없으면 자동 기동**한다(뮤텍스 확인 → `start_tas_server.bat` ShellExecute). 2026-10-17 14:39 실제 자동 기동 사례를 실측으로 확인.
6. 런타임 검증 완료: 0.0.0.0:24817 LISTEN, TCP 접속/수락 성공, 중복 실행 차단(안내문+5초 후 종료) 실동작 확인.

---

## 1. 파일 신원

| 항목 | 값 |
|---|---|
| tas_server.exe | 345,128 B / SHA256 `109A5B145F7F6931789A38FBFFBD288BBECCA0BEEEF7613D55D5EDF5554845D3` |
| tas_server_console.exe | 346,152 B / SHA256 `3CDA4C74C25EE34FDCEDF9F0272E661FD054B5E3D45E6F4641080C4D44BFEC77` |
| 변형 차이 | 코드 동일, PE 서브시스템(GUI=CUI)과 안내 문구 문자열 일부 차이 |
| 전자서명 | Infineon Technologies AG — **Valid** (DigiCert SHA256 RSA4096 타임스탬프) |
| 버전 리소스 | FileDescription "Tool Access Socket (TAS) Server", ProductName "TAS Server", **ProductVersion 2.0.9**, © Infineon 2026 |
| 빌드 | MSVC 14.42 (VS2022 17.14), PE 타임스탬프 2026-05-01 01:03 UTC, 내장 날짜문자열 "Apr 30 2026" |
| EntryPoint | 0x1400344B4 (CRT), main 로직 ≈ 0x140001440 |

## 2. 의존성 (오디널 → 실명 매핑)

### FTD2XX.dll (17개, 전부 오디널 임포트)
FT_Open(1), FT_Close(2), FT_Read(3), FT_Write(4), FT_ResetDevice(6), FT_SetFlowControl(9),
FT_SetChars(15), FT_SetTimeouts(17), FT_GetQueueStatus(18), FT_OpenEx(27), FT_SetLatencyTimer(29),
**FT_SetBitMode(31)**, FT_SetUSBParameters(33), FT_CyclePort(69), FT_CreateDeviceInfoList(70),
FT_GetDeviceInfoDetail(72), FT_GetLibraryVersion(76)

- `FT_SetBitMode` = MPSSE/비트뱅 모드 → **JTAG 신호를 FTDI로 직접 구동**.
- `FT_CyclePort` = USB 포트 재시작(복구용). `FT_GetLibraryVersion` = ftd2xx 버전 검증(경고 문구 존재).
- ftd2xx.dll은 `C:\Windows\System32\ftd2xx.dll` (FTDI D2XX 드라이버, v2.12.28) 사용.

### WS2_32.dll (23개, 대부분 오디널)
WSAStartup(115/2.2), socket(23), bind(2), listen(13), accept(1), connect(4), select(18),
send(19), recv(16), getpeername(5), getsockopt(7), setsockopt(21), htonl(8), htons(9), ntohs(15),
ioctlsocket(10), WSAGetLastError(111), WSASetLastError(112), __WSAFDIsSet(151), inet_ntop, getaddrinfo, freeaddrinfo

- 순수 TCP. UDP 소켓 함수(sendto/recvfrom) 없음. TLS/암호화 라이브러리 없음 → **평문 프로토콜**.

### 기타
KERNEL32(Sleep, CreateMutexW, GetEnvironmentVariableA, SetPriorityClass, GetCurrentProcess/ThreadId 등),
MSVCP140(_Thrd/_Mtx/_Cnd → std::thread 동기화), VCRUNTIME140(+_1), ucrt api-set.
→ ADVAPI32(레지스트리)·USER32·파일 오픈(fopen/CreateFile) 임포트 없음.

## 3. 시작 흐름 (디스어셈블 근거)

```
main() ≈ 0x140001440:
  SetPriorityClass(GetCurrentProcess(), 0x80 /*HIGH_PRIORITY_CLASS*/)
  h = CreateMutexW(NULL, TRUE, L"Global\only_one_TasServer_on_host")
  if (h == NULL) return -1
  if (GetLastError() == ERROR_ALREADY_EXISTS(183)) { CloseHandle(h); return -1; }  // 중복 실행 차단
  server_start(host, 24817)          // 0x14002C990

server_start():
  port = 24817
  if (GetEnvironmentVariableA("TAS_SERVER_PORT", buf, 20) 성공 && 파싱값 > 0)
      port = 파싱값                     // 포트 오버라이드
  WSAStartup(0x202)
  s = socket(AF_INET, SOCK_STREAM, ...)
  sockaddr_in { family=AF_INET, addr=htonl(0) /*INADDR_ANY*/, port=htons(port) }
  bind(s, 16) → 실패 시 setne (성공 플래그)
  ... accept/select 루프 (로그: "Client accepted  %s %d")
```

- 별도 경로(0x14002C862): 에이전트 타입 5(디바이스 서버) 기본 포트 **24823**.
- 오픈소스 클라이언트 `src/tas_client/tas_pkt.h`의 `TasDefaultPorts`와 일치: SERVER=24817, PROXY=24818, DEVICE=24823.

## 4. 설치·배포·자동 기동 구조 (핵심 발견)

### 4.1 설치기가 System32에 배포하는 파일 (모두 2026-10-17 14:03:19, 빌드일 2026-05-02)
| 파일 | 크기 | 역할 |
|---|---|---|
| `C:\Windows\System32\tas_server.exe` | 345,128 | DAS64 복사본과 **바이트 동일**(해시 일치), 서명 Valid |
| `C:\Windows\System32\start_tas_server.bat` | 101 | `check_redistributable.exe` → `tas_server.exe` 실행 (DAS64판과 내용 동일) |
| `C:\Windows\System32\check_redistributable.exe` | 88,063 | VC++ 재배포 확인 |
| `C:\Windows\System32\das_api.dll` | 153,128 | (기존) DAS API DLL — 도구들이 사용하는 공식 DAS 인터페이스 |
| `C:\Windows\System32\mcdxdas.dll` | 286,248 | (기존) MCD↔DAS 어댑터 |

→ 원래 "설치본에 TAS dll이 없다"고 봤던 것과 달리, **DAS 계열 DLL(das_api.dll, mcdxdas.dll)은 System32에 배포**되어 있음. TAS 클라이언트 API 자체는 정적 라이브러리(소스 빌드)라 DLL이 없음.

### 4.2 클라이언트의 자동 기동 (오픈소스 소스 근거: `tas_utils_os.cpp` 44~102행)
```cpp
// 서버 실행 여부 확인
OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\only_one_TasServer_on_host")  // 있으면 실행 중

// 없으면 기동
GetSystemDirectoryA(appPath) → appPath + "\\start_tas_server.bat"
ShellExecuteA(0, "open", appPath, ...)   // bat 실행 (32bit에서는 WOW64 리다이렉션 해제)
```
→ **클라이언트는 `%SystemRoot%\System32\start_tas_server.bat`를 실행**하도록 설계되어 있고, 설치기가 그 경로에 파일을 배치한다.

### 4.3 실제 기동 사례 (2026-10-17 14:39, 이 PC)
- 14:39:36 `cmd.exe /c ""C:\Program Files\DAS64\servers\start_tas_server.bat" "` (ShellExecute 흔적: 이중 따옴표+꼬리 공백)
- bat 안의 `tas_server.exe`가 **PATH 해석으로 System32 복사본** 실행 → PID 19760
- 14:39:37 tas_server 시작 (부모: cmd.exe 8392, cmd는 실행 대기 중)
- 결론: 사용자가 (추정) TAS 클라이언트 데모를 실행했고, 그 클라이언트가 서버를 자동 기동한 것. **사람이 서버를 직접 관리할 필요가 없는 구조**.

## 5. 기능 인벤토리 (RTTI 클래스명 + 문자열 근거)

- **세션/프로토콜**: CTasServerClientCon, CTasServerClientPktIf, CTasPktHandlerBase/Rw, CTasPktMailboxIf*,
  "Client accepted %s %d", "Client name \"%s\", user \"%s\", session \"%s\"",
  "Number of clients %u (ClientRw %u, ClientChl %u, ClientTrc %u)" → 클라이언트 3종(Rw/Chl/Trc) 관리
- **보안**: 세션 이름/PW, 챌린지 기반 unlock, 패킷 CRC32 검증("wrong CRC32 -- expected ... actual ..."),
  오류 문구: "ERROR: Session name or password", "ERROR: Target not connected", "ERROR: Wrong TAS API usage" 등
- **하드웨어**: CJdsDevAccFtdi(FTDI JDS), CJdsDevAccEmuZebu(에뮬레이터), CTasAccHwUdas(UDAS),
  "DAS JDS miniWiggler JTAG/DAP/SPD V%d %c", "Automatic device detection", "Native JTAG Mode",
  JTAG 스캔모드(SET_IR/CAPTURE/GENERATE_CLOCK_CYCLES), jtag_setIR/setDR/moreDR/swapIR/swapDR,
  DAP 텔레그램(AddDapTelegram/JDA_ERR_*), SWD DP/AP R/W, SPD, GPIO(AddSetGpios)
- **트레이스**: CTasServerTrcTc2tc3, Tc2tc3Mm, Tc4 — TC2xx/3xx/4xx 트레이스(MCDS 계열), TRCUDP,
  OLDA/FLST 스트림
- **에이전트**: CTasAgent, CTasAgentAccHw, CTasAgentTcpip, CTasAgentTrcudp
- **지원 디바이스 문자열**: TC21x~TC4Zx (TC2xx/TC3xx/TC4xx), DummyDevice, Simulator
- **중복 실행 안내(콘솔판 전용 문자열)**: "ERROR: Another TasServer instance is already running on this host",
  "Will terminate after 5 seconds"

## 6. 프로토콜 / 오픈소스 대응

- 프로토콜 세대: PL1 (`TAS_PKT_PROTOC_VER_1`, `tas_pkt.h`). "ERROR: Server connection protocol (PL1 packet count)" 문자열로 서버도 PL1 취급 확인.
- 서버/클라이언트가 같은 코드 계보(동일 클래스명) → **와이어 포맷은 `tas_client_api` 소스가 정의서**.
- 전송: TCP 평문, 기본 24817. 접근제어는 세션 PW/챌린지 + CRC32.

## 7. 런타임 검증 결과 (2026-10-17)

| 검증 | 방법 | 결과 |
|---|---|---|
| 리스닝 | `netstat`/Get-NetTCPConnection | `0.0.0.0:24817 LISTENING` (PID 19760, System32 복사본) ✓ |
| TCP 수락 | 로컬 24817 접속(무데이터, 비파괴) | ESTABLISHED 생성/해제 정상 ✓ |
| 중복 실행 차단 | 콘솔판 추가 기동 | "ERROR: Another TasServer instance is already running on this host / Will terminate after 5 seconds" 출력 후 종료 ✓ |
| 단일 인스턴스 뮤텍스 | `Global\only_one_TasServer_on_host` 프로브 | createdNew=False, WaitOne(0)=False → 존재·소유 중 ✓ |
| 자동 기동 경로 | PID 8392 cmd 커맨드라인 | `cmd /c ""...\DAS64\servers\start_tas_server.bat" "` ✓ |

한계: 실제 JTAG/트레이스 데이터 경로(E2E)는 정식 클라이언트 세션(챌린지/세션 시작, 타깃 연결)이 필요. 이번 검증은 서버 기동/소켓 계층까지. (클라이언트 빌드 후 E2E 가능)

## 8. 보안 / 운영 참고

- **평문 TCP + 0.0.0.0 바인드** → 같은 LAN의 임의 호스트가 접속 시도 가능. 보호는 세션 PW/챌린지·CRC32뿐.
  신뢰할 수 없는 네트워크에 노출된 PC에서는 주의(방화벽으로 24817 제한 권장).
- HIGH 우선순위 클래스로 실행, `Global\` 뮤텍스로 단일 인스턴스 보장(중복 시 5초 후 종료).
- 서버는 보통 **클라이언트가 자동 기동**(위 4.2). 수동 중지: `taskkill /IM tas_server.exe` (관리자 불필요).
- 로그를 보고 싶으면 DAS64의 `tas_server_console.exe`를 직접 실행(중복 인스턴스가 없을 때만).

## 9. 자체 툴/자동화 관점 시사점

1. **TAS 클라이언트 구현은 오픈소스 빌드가 정답** — 설치본에 클라이언트 DLL이 없는 이유(정적 라이브러리 배포 형태)가 확정됨.
2. 서버는 재구현 불필요. 자체 툴은 ① 서버 자동 기동(기존 bat 재사용) 또는 ② `tasutil_start_local_tas_server()`와 동일한 기동 로직 채택, 또는 ③ 상시 가동 서버 재사용 중 택일.
3. 와이어 프로토콜이 평문이라 자체 툴 개발·검증 시 스니핑/모의서버 테스트가 쉬움.
4. 포트/뮤텍스/환경변수(TAS_SERVER_PORT)/세션 개념은 그대로 자동화 설계에 반영 가능.

## 10. 산출물 목록 (이 폴더)

- `headers.txt`, `headers_console.txt` — PE 헤더
- `imports.txt`, `exports.txt` — 임포트/익스포트 원본
- `exports_ws2_32.txt`, `exports_ftd2xx.txt` — 오디널→이름 매핑 근거
- `strings_ascii.txt`(3,717), `strings_utf16.txt`(16) — GUI판 문자열
- `strings_console_ascii.txt`(3,731) — 콘솔판 문자열(중복 실행 안내문 포함)
- `disasm.txt` — 전체 디스어셈블(63,522줄)
- `server_run_out.log`, `server_run_err.log` — 중복 실행 실측 로그

## 11. 한계 및 다음 단계

- 미해결(필요시): 내부 함수 단위 심층 로직(디컴파일러 필요 — Ghidra/IDA), ftd2xx 버전 검사 기대값 상수, 특정 JTAG 시퀀스 타이밍.
- 다음 단계 후보: ① `tas_client_api` 빌드 → 실기 E2E(타깃 연결/메모리 RW) 검증, ② 클라이언트-서버 패킷 캡처(평문) 문서화, ③ 자체 툴에 자동 기동/세션 관리 통합.

## 12. 후속 결과 (2026-10-17) — 클라이언트 빌드 & 실기 E2E 완료

- `tas_client_api` v1.2.4 빌드 성공: C++ 정적 라이브러리(`tas_client.lib`, `tas_socket.lib`) + PyTAS 휠(pytas-1.2.4-cp313)
- 실기 TC275 E2E: RW 데모 전 항목 성공 + PyTAS unittest **23/23 OK**
- CHL(채널)은 타깃 측 채널 디스크립터 미제공으로 세션 불가(문서화된 제약, 클라이언트 결함 아님)
- 빌드 명령/산출물/통합 방법 상세: 같은 폴더의 **`TAS_CLIENT_BUILD_AND_E2E.md`** 참
- PyTAS 휠을 **메인 Python 3.13에 설치** 후 실기 스모크 테스트 통과(`server_connect`/`get_targets`/`session_start`/`read32`)
- 클라이언트-서버 **평문 패킷 캡처 및 전 패킷 해석** 완료 → 프로토콜 문서: **`TAS_PROTOCOL_CAPTURE.md`** (원본/스크립트: `pkt_capture\`)
