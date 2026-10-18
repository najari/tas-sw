# M25-1 — 자체 TCF 에이전트 라이브 성공 (2026-10-18)

**결과: 우리 `tcf-agent.exe`(오픈 TCF + AURIX 백엔드)가 ShieldBuddy TC275ED에
TAS로 연결해 VS Code에서 3코어 디버깅이 동작했다.** 원본 `aurix-debugger.exe`
없이(업스트림 TCF 프레임워크 + 우리 백엔드) 달성 — "missing 10%" 재구현의
핵심 관문 통과.

## 0. 결정적 설계 규명 (원본 exe 재분석)

`aurix-debugger.exe`(9.2MB)의 DWARF 소스 목록·문자열·임포트 전량 분석 결과:

| 항목 | 원본 실제 | 우리 (수정 전 가정) |
|---|---|---|
| ContextMux | **미사용** (`context-dispatcher.c`·`context-mux.h` 없음!) | 사용 (드래프트) |
| 컨텍스트 백엔드 | `system/aurix/tcf/context-aurix.cpp`가 **context-win32.c를 대체** | 멕스 프로바이더로 추가 |
| 머신 | `machine/aurix/tcf/{cpudefs-mdep.cpp, disassembler-aurix-*}` (x86_64 없음!) | machine/x86_64 유지 |
| 빌드 | MinGW-w64 + **clang**(winpthreads!), `pthreads-win32.c` 없음 | MSVC + pthreads-win32 |
| 심볼 | `ENABLE_ELF=1`(ELF 심볼!), `ENABLE_PE=0`(windbgcache 없음), SymbolsProxy 없음 | 기본값 |
| 제외 파일 | context-win32.c, windbgcache.c, symbols_win32.c, test.c | — |
| 컨텍스트 이름 | `Core %d: CPU%d ...` (M9: `Core 0: CPU0 TriCore 1.6E` 등) | — |

→ **우리 빌드 재구성**: `TCF_MACHINE=aurix`, `system/Windows/tcf/context-win32.c`
제외, `ENABLE_ELF=1 ENABLE_PE=0 ENABLE_SymbolsProxy=0 ENABLE_RCBP_TEST=0`
(모두 `cmake-tcf-lib.txt`의 aurix 블록에서). MSVC이므로 `pthreads-win32.c`는 유지.

## 1. 구현 (신규/수정 파일)

```
agent/system/aurix/tcf/
  context-aurix.c     ← 네이티브 컨텍스트 백엔드 (M25-1 재작성! 720줄)
  aurix-tas.c/h       ← TAS 클라이언트 (mcd_tas 이식, `trace` 필드 충돌 수정)
agent/machine/aurix/tcf/
  cpudefs-mdep.h      ← REG_SET(40레지스터 캐시), regs_index, BREAK_INST(0x00A0=debug)
  cpudefs-mdep.c      ← TriCore 레지스터 40개(EABI DWARF 번호!) + CSFR 주소 + get_PC_definition
agent/tcf/main/main.c ← aurix-debugger 호환 옵션(-p/-e/-b/-r/-K/-A/-f/-G) 수용
cmake-tcf-lib.txt     ← aurix glob + context-win32.c 제외 + ENABLE_* 정의
```

### 핵심 계약 (프레임워크 ↔ 백엔드, 첫 빌드에서 4건 학습!)

1. **컨텍스트 생성 시점**: 서비스 초기화(`ini_diagnostics` 등)가 `context_extension()`을
   늦게 호출하므로, 컨텍스트는 **posted event로 지연 생성**해야 함
   (`init_contexts_sys_dep`은 `context_extension()` + `post_event`만).
2. **stop은 비동기 보고**: `context_stop()`은 TAS 정지 시퀀스만 수행하고
   `ctx->stopped`는 **이벤트로** 설정 + `send_context_stopped_event()`
   (runctrl이 직후 `assert(!ctx->stopped)`).
3. **resume/step은 동기 started 이벤트**: `send_context_started_event()`를 즉시
   호출(win32_resume과 동일), step은 started 후 비동기 stopped 이벤트.
4. **컨텍스트 생성 시 `stopped=1` 금지**: `event_context_created`에
   `assert(!ctx->stopped)`. → 생성(러닝) 후 `stopped=1; pending_intercept=1;
   send_context_stopped_event()`로 **정지+인터셉트** 보고(원본의 PAUSED 파리티!).

### 정렬 교훈 (타깃 하드웨어)

- RD32는 **4바이트 정렬 필수** (0x...6A에서 4바이트 읽기 실패 실측!).
- `tas_rw` 청킹: 8정렬=블록(≤1024), 4정렬=RD32, 2정렬=RD16, 그 외=RD8.

### 상태 시퀀스 (M16/M20b/M22 정본 재사용)

- attach: SERVER_CONNECT → SESSION_START → SET_DEVICE_KEY(hot) → DEVICE_CONNECT(0)
  → "OpenCore+Halt"(3코어 DBGSR←6+검증)
- run: `0494←0x8000003E → DBGSR←4 → 0494←0x8000003F → DBGSR←4 → 049C←2` + 검증
- stop: DBGSR←6 + HALT[2:1]==01b 검증
- step: debug init(트리거 윈도+DBGSR 영소거+FD10) → `0494←3E` → DBGSR←4 → `0494←3F`
  → debug init → 정지 검증
- 레지스터: CSFR(코어 스트라이드 +0x20000) — PC=0xFE08, PCXI=0xFE00, GPR D0=0xFF00, A0=0xFF80…

## 2. 검증 (3단계)

### (a) TCF 프로토콜 스모크 (자체 클라이언트, `m25_1_evidence/tcf_smoke*.txt`)

- Locator Hello(22 서비스) · RunControl getChildren → `[core0, core1, core2]`
- 컨텍스트: `Core 0: CPU0 TriCore 1.6E` / `1.6P`×2 (M9 파리티!), WordSize 4, rd-stop/wr-stop
- 레지스터 40개: `PC(0x8003A142 — M20b 실측과 일치!)`, D0=0xB00004FC 등 실값
- 메모리: PC에서 16바이트 = 실제 TriCore 명령 바이트, **비정렬(PC+2) 6바이트 OK**
- suspend: `contextSuspended` 이벤트(PC+reason="Suspended") → intercepted=true,
  PC 고정(정지 확인), **3코어 전부 정지 복원**

### (b) 확장 자체 TCF 클라이언트 (VS Code 확장의 out/tcfclient.js!)

- `getThreads()` → 3스레드 suspended=true + PC ✓ (PAUSED 파리티)
- StackTrace getChildren/getChildrenRange/getContext → `FP0.core0`, IP=0x8003A32C ✓

### (c) VS Code GUI 라이브 (cua-driver로 직접 조작!)

- launch.json: `aurix-tcf.agentPath` = 우리 exe → 확장이 **우리 에이전트를 spawn**
  (PID 18480, TAS→ShieldBuddy attach ✓)
- Call Stack: `Core 0: CPU0 TriCore 1.6E Paused`, 3코어 표시 ✓
- **F5(Continue) → 3코어 Running ✓** / **F6(Pause) → 3코어 Paused ✓**
- 최종: 3코어 DBGSR 정지(Debug) — **입력 상태 복원** ✓
- 증거: `m25_1_evidence/vscode_state{4,5,6}.json` (UIA 트리 전문)

## 3. 잔여 (다음 마일스톤)

| 항목 | 단계 |
|---|---|
| HW 브레이크포인트(OCDS TR0-7, M18) — 현재 SW BP(`debug` 명령)만 | M25-2 |
| memory map("Code and Data", M8) · canonical(SPR, M20) · group | M25-2 |
| 정지 사유 판독(DBGSR EVTSRC → "Breakpoint" 파리티) | M25-2 |
| CSA 스택 크롤(PCXI 체인) · TriCore 디스어셈블러 · SVD | M25-3 |
| 심볼/소스 표시(ELF 로딩 경로 — 확장이 ELF 경로 전달 필요) | M25-3 |

## 4. 산출물

- 에이전트: `C:\work\tcf_agent\agent\build\{Debug,Release}\tcf-agent.exe`
  (Release 764,928 B / Debug 1,743,872 B)
- 증거: `C:\work\tas_server_analysis\m25_1_evidence\` (스모크 2종·VS Code 상태 3종·로그)
- 도구: `C:\work\tcf_agent\{tcf_test_client.js, ext_client_test.js, tcf_probe_*.js}`
- 소스 사본: `C:\work\tas-sw\m25_tcf\` (예정)
