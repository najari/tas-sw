# M25 — 자체 TCF 에이전트(AURIX 백엔드) 설계도 (2026-10-18)

**목표**: `aurix-debugger.exe`의 "missing 10%"를 우리 TAS 스택으로 구현 — 완전 오픈 AURIX TCF 에이전트.

## 0. 검증된 기반 (오늘 실측!)

- **업스트림 빌드 성공**: `C:\work\tcf_agent` (gitlab.eclipse.org/eclipse/tcf/tcf.agent!)
  → `cmake -B build -G "Visual Studio 17 2022" -A x64` → **`tcf-agent.exe` (686KB!) ✓**
- 업스트림 = [machine/(a64/arm/i386/riscv/x86_64!) + system/(Windows/GNU/MinGW…!)] — **TriCore/aurix 없음!**
- Infineon `aurix-debugger.exe` = 8.9MB (백엔드 ~8.2MB!) — 그 정체 = `system/aurix/` + `machine/aurix/`

## 1. ★ ContextIf 매핑 (19함수 → 우리 M1~M22!)

| TCF ContextIf | 구현 (우리 TAS!) | 근거 |
|---|---|---|
| `context_has_state` | DBGSR 읽기 (HALT 비트!) | **M5!** |
| `context_stop` | 어댑터 stop 시퀀스 | **M16/M22!** |
| `context_continue` | 어댑터 run 시퀀스 (step-on→DBGSR→step-off→run!) | **M16/M22b!** |
| `context_resume` (mode/range) | run/step 분기 | M16/M21! |
| `context_can_resume` | (HALT 상태 확인!) | M5! |
| `context_single_step` | 어댑터 single-step | **M21/M22!** |
| `context_read_mem` / `write_mem` | TAS txlist (RD32/WR32/RDBLK/WRBLK!) | **M1/M2!** |
| `context_word_size` | 4 (TriCore!) | — |
| `context_read_reg` / `write_reg` | CSFR (PC=0xFE08! DBGSR=0xFD00!) + reg_map | **M4/M7!** |
| `context_get_group` | 코어 그룹 (멀티코어!) | M22! |
| `context_get_canonical_addr` | SPR 로컬→전역 (코어별 별칭!) | **M20!** |
| `context_get_memory_map` | mem_spaces ("Code and Data"!) | **M8!** |
| `context_get_supported_bp_access_types` | HW BP (IP/RW!) | **M18!** |
| `context_plant/unplant_breakpoint` | TR0-TR7 (TRxEVT/TRxADR/TRIG_ACC!) | **M18!** |
| CpuDefsIf: `get_reg_definitions` | 코어 레지스터 (PC/D0-15/A0-15/PSW!) | M4! |
| CpuDefsIf: `get_PC_definition` | PC @CSFR 0xFE08 | M4! |
| CpuDefsIf: `read/write_reg_bytes` | 프레임 기반 (스택!) | 신규 |
| CpuDefsIf: `get_break_instruction` | (SW BP — TriCore `debug` 명령!) | 신규 |
| CpuDefsIf: `crawl_stack_frame` | TriCore CSA (context save areas!) | **최난관!** |

## 2. 파일 구조 (Infineon과 동일 레이아웃!)

```
tcf_agent/agent/
  system/aurix/tcf/
    context-aurix.c      ← ContextIf 19함수 + init_contexts_sys_dep (the "10%"!)
    aurix-tas.c/h        ← 우리 TAS 클라이언트 (mcd_tas에서 이식!)
    cpudefs-aurix.c      ← TriCore 레지스터 정의 + sys_cpudefs_if
  machine/aurix/tcf/
    cpudefs-mdep.c       ← TriCore CPU defs (DWARF 번호!)
    disassembler-aurix-tricore.c ← (후순위 — binutils/OpenOCD 참조!)
```

## 3. 단계 계획

| 단계 | 내용 | 난이도 |
|---|---|---|
| **M25-1** | TAS 이식 + context-aurix.c 골격 (mem/reg/state/stop/continue/step!) + cpudefs (PC+GPR!) + init + 빌드 통합 | 중 (수 주!) |
| **M25-2** | 브레이크포인트 (M18!) + memory map + canonical (M20!) + group (M22!) | 중 |
| **M25-3** | 스택 크롤 (CSA!) + 디스어셈블러 + SVD/주변장치 | **최난관 (수개월!)** |
| (선택) | MCDS 사이드카 (우리 recorder/decoder!) | M23/M24 재사용! |

## 4. 빌드 통합 방안

- **호스트 OS = Windows** (기존 `system/Windows`!) — 우리 백엔드 = **타깃 컨텍스트**로 추가
- `ENABLE_ContextMux=1` (멀티 인터페이스 — 네이티브 + AURIX 동시!) 또는
- 별도 OPSYS로 빌드 (Infineon처럼!) — 1차는 CMake에 새 소스 추가로 시작!
- **검증**: 우리 tcf-agent.exe + VS Code(기존 확장의 "host/port" 지정!) + ShieldBuddy!

## 5. 즉시 다음 작업 (M25-1 착수!)

1. `aurix-tas.c/h` — mcd_tas의 tas_client 이식 (MCD 의존 제거!)
2. `context-aurix.c` — sys_ctx_if 골격 (read/write_mem/reg + has_state/stop/continue!)
3. `cpudefs-aurix.c` — PC + GPR 최소 정의
4. `init_contexts_sys_dep` — TAS 접속 + 3코어 컨텍스트 생성
5. CMake 통합 + 첫 빌드 + VS Code 연결 테스트!
