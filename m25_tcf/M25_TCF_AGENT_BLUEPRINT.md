# M25 — 자체 TCF 에이전트(AURIX 백엔드) 설계도 (2026-10-18, **M25-1 완료 후 갱신**)

**목표**: `aurix-debugger.exe`의 "missing 10%"를 우리 TAS 스택으로 구현 — 완전 오픈 AURIX TCF 에이전트.

**현황: M25-1 완료!** (2026-10-18) — 상세: `M25_1_TCF_AGENT_LIVE.md` 참조.

## 0. ★ 설계 정정 (원본 exe 재분석으로 확정!)

- ~~ENABLE_ContextMux=1~~ → **원본은 ContextMux를 쓰지 않는다** (`context-dispatcher.c`·
  `context-mux.h`가 바이너리에 없음!). `system/aurix/tcf/context-aurix.*`가
  **context-win32.c를 "대체"**하는 네이티브 백엔드다.
- 머신도 `machine/aurix/tcf/*` (x86_64 머신 파일 없음!) → `TCF_MACHINE=aurix`.
- 원본 빌드: MinGW/clang(winpthreads) + `ENABLE_ELF=1`(ELF 심볼) + `ENABLE_PE=0`
  (windbgcache/심볼 PE 없음) + SymbolsProxy 없음 + context-win32/windbgcache/test.c 제외.
- 우리 빌드(MSVC): 같은 파일 구성 + `pthreads-win32.c` 유지(MSVC 필수).

## 1. ContextIf(19함수) → 우리 TAS (구현 완료분)

| TCF | 구현 | 상태 |
|---|---|---|
| context_has_state | 항상 1 (HALT/run 상태 보유) | ✓ |
| context_stop | DBGSR←6 + HALT 검증 (M16) → **비동기 stopped 이벤트** | ✓ |
| context_continue | 어댑터 run 시퀀스 M22b (`0494`→DBGSR→`0494`→DBGSR→`049C`) | ✓ |
| context_resume (RM_RESUME/STEP_INTO) | run/step 분기 | ✓ |
| context_can_resume | RM_RESUME·RM_STEP_INTO=1 | ✓ |
| context_single_step | M22 step(디버그 초기화 포함) | ✓ (UI 미검증) |
| context_read/write_mem | TAS PL0 (정렬 인식 청킹: 블록/RD32/RD16/RD8) | ✓ |
| context_word_size | 4 | ✓ |
| context_read/write_reg | CSFR 코어 스트라이드 +0x20000 (M20b) | ✓ |
| context_get_group | ctx(코어=독립 그룹) | ✓ |
| context_get_canonical_addr | 항등(4KB 블록) | ✓ (M25-2: SPR) |
| context_get_memory_map | 빈 맵 | ✓ (M25-2: "Code and Data") |
| context_get_supported_bp_access_types | INSTRUCTION\|SOFTWARE | ✓ |
| context_plant/unplant_breakpoint | ERR_UNSUPPORTED → SW BP 폴백 | ✓ (M25-2: TR0-7) |
| context_get_isa | "TriCore" + BREAK_INST(0x00A0) | ✓ |
| CpuDefs: regs_index 40개 | PC,D0-15,A0-15,PSW,PCXI,FCX,ISP,ICR,BIV,BTV (EABI DWARF!) | ✓ |
| CpuDefs: get_PC_definition | PC(오프셋 매칭) | ✓ |
| CpuDefs: read/write_reg_bytes | 프레임워크(cpudefs-ext.h) 제공 | ✓ |
| CpuDefs: get_break_instruction | BREAK_INST = `00 A0` (TriCore `debug`) | ✓ |
| CpuDefs: crawl_stack_frame | ERR_UNSUPPORTED 스텁 | M25-3 (CSA) |

## 2. 프레임워크 계약 4건 (첫 컴파일·라이브에서 확정 — 재사용!)

1. 컨텍스트 생성은 **posted event로 지연**(서비스 init이 그 뒤에 context_extension() 호출).
2. `context_stop`은 **비동기**: TAS 시퀀스 후 `post_event`로 `ctx->stopped=1`+stopped 이벤트.
3. `context_continue`/`single_step`은 **동기 started 이벤트**(win32_resume 패턴).
4. 생성 직후 `stopped=1` 금지(`event_context_created` assert) — 생성(러닝) 후
   `stopped=1; pending_intercept=1; send_context_stopped_event()` → **PAUSED 파리티**.

## 3. 파일 구조 (실제 — 원본과 동일 레이아웃)

```
tcf_agent/agent/
  system/aurix/tcf/{context-aurix.c, aurix-tas.c, aurix-tas.h}   ← 완료
  machine/aurix/tcf/{cpudefs-mdep.c, cpudefs-mdep.h}             ← 완료(최소)
  tcf/main/main.c                                                ← 호환 옵션(-p/-e/-b/-r/-K/-A/-f/-G)
  cmake-tcf-lib.txt                                              ← aurix glob + context-win32 제외 + ENABLE_*
```

## 4. 단계 계획 (갱신)

| 단계 | 내용 | 상태 |
|---|---|---|
| **M25-1** | TAS 이식 + 네이티브 백엔드 + 레지스터 40 + run/stop/step + 빌드 통합 + VS Code 라이브 | **✓ 완료** |
| **M25-2** | 코어 모니터(비동기 정지!) + HW BP(TR0-7) + 정지사유(Breakpoint 파리티) + step-over/트리거 재장전 | **✓ 완료** (상세: `M25_2_BREAKPOINTS.md`) |
| **M25-2b** | memory map("Code and Data") + canonical SPR + BP 범위(유닛 페어) + 데이터 워치포인트 UI | 예정 |
| **M25-3** | CSA 스택 크롤(PCXI/FCX/LCX) + TriCore 디스어셈블러 + SVD + 심볼/소스(ELF 경로) | 예정 |
| (선택) | MCDS 사이드카(우리 recorder/decoder) | M23/M24 재사용 |

## 5. 검증 방법 (재현)

1. `tcf-agent.exe -s TCP:127.0.0.1:1534 -p "<디바이스>"` (tas_server 24817 전제)
2. 자체 스모크: `node C:\work\tcf_agent\tcf_test_client.js` (컨텍스트·레지스터·메모리·suspend)
3. 확장 클라이언트: `node C:\work\tcf_agent\ext_client_test.js` (확장 out/tcfclient.js!)
4. VS Code: `.vscode/settings.json`의 `aurix-tcf.agentPath`를 우리 exe로 → F5
   (launch.json의 targetDevice 등은 그대로 — 우리 에이전트가 수용·무시)
