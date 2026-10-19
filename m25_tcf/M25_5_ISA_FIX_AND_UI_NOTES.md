# M25-5: ISA 대소문자 수정 + UI 표시 해석 (2026-10-19)

## 1. 디스어셈블리 명령어 미표시 수정 (ISA 대소문자)

**증상**: VS Code 디스어셈블리 뷰가 주소+바이트만 표시하고 명령어는 `.half 0x....` 폴백으로 표시.

**원인**: 확장(`debugProvider.js`)이 디스어셈블 요청에 `ISA:'TRICORE'`(대문자)를 보내는데,
`agent/tcf/services/disassembly.c`의 `find_disassembler_info()`가 `strcmp`(대소문자 구분)로
등록명 `"TriCore"`(`machine/aurix/tcf/cpudefs-mdep.c:143`)와 매칭 → 불일치 →
디코더 미발견 → `.byte/.half/.word` 폴백.

**수정**: `strcmp` → `strcasecmp` 1줄
(`tcf/config.h` → `framework/mdep.h`가 MSVC에서 `_stricmp`로 매핑 ✓).

**검증** (`tools/m25_5_disasm_isa_test.js` — 확장과 동일 파라미터
`ISA/Simplified/PseudoInstructions/OpcodeValue`):

- 수정 후 `ISA='TRICORE'` 요청 → **18개 디코딩 / 5개 폴백** (mov/add/call/jne/st.w/ret …)
- objdump 대조 **22/23 일치** — 잔여는 objdump도 `.hword`로 포기하는 것들 ✓
- **VS Code 디스어셈블리 뷰 표시 확인** (`evidence/ui_rehearsal/29_frame_click_disasm.png`)

## 2. Call Stack 표시 해석 (`FP0.core0` / `Unknown Source` / ⓪)

| 항목 | 의미 | 출처 |
|---|---|---|
| `FPn.core0` | TCF 표준 스택 프레임 컨텍스트 ID (프레임번호.부모컨텍스트) | `tcf/framework/cpudefs.c:205` `tmp_printf("FP%d.%s", frame, ctx->id)` |
| `Unknown Source` | 소스/라인 매핑 없음 | 확장: `frame.mapToSource` 없음 → VS Code 표시 |
| `⓪` 배지 | 라인 번호 배지 | VS Code `callStackView.ts:742` (`span.line-number.monaco-count-badge`); 프레임 line=0(@vscode/debugadapter 기본값) |

- **프레임 자체는 정상**: CSA 크롤이 복원한 실제 호출 체인
  (라이브 확인: FP0 0x8003a332 → FP1 0x80038bea → FP2 0x8003ccac → FP3 0x8003cf7a → 끝)
- 에이전트에 심볼이 없음 (라이브 확인: `Symbols findByAddr` → "Symbol not found") →
  함수명 대신 컨텍스트 ID가 표시됨
- **함수명/소스 라인 표시 조건**: 타깃에 실제 올라간 빌드와 일치하는
  **디버그 정보 포함 ELF**를 launch.json `program`에 지정
  (그러면 소스 스텝 추적 + 디스어셈블리 범위 클리핑도 통과)

## 3. 소스 레벨 디버깅 완성 — RAM 데모 라이브 실증 (2026-10-19)

자연 프로그램(0x8003xxxx)은 대응 ELF가 없어 소스 표시가 불가하므로,
**일치하는 ELF가 있는 RAM 데모**(`m25_pspr.elf`, PSPR 0x70100000)로
소스 레벨 전 구간을 UI에서 재실증했다 (M25-3 시퀀스 재사용).

**시퀀스** (launch.json `program` = `m25_pspr.elf` → 세션 재시작 → 코어 정지):

1. `tools/m25_3_ram_load.js`: 상태 저장(40레지스터 + DSPR/PSPR 8KB) →
   .text(180B) PSPR 적재(MATCH) → PC=main(0x7010003A), SP=0x7000C000, ICR.IE=0
2. 에디터 line 5에 소스 BP(F9) → 확장이 DWARF로 0x7010000C 해석
   (탭에 `add(int, int)` 표시) ✓
3. F5 재개 → **PAUSED ON BREAKPOINT** (PC=0x7010000C) ✓

**UI 실증** (`evidence/ui_rehearsal/34~39`):

| 항목 | 결과 |
|---|---|
| Call Stack | `add — m25_elf_test.c:5` + `main — m25_elf_test.c:15` (함수명+소스라인) ✓ |
| 에디터 | 현재 라인 하이라이트 + 실행 화살표 ✓ |
| Locals | add에서 `a=0,b=0` → main 복귀 후 `i=0` (프레임별 갱신) ✓ |
| Disassembly | 디코딩 + 현재 명령 하이라이트(0x7010000C) ✓ |
| 스텝(F10×3) | 0x0C→0x10→0x14→0x16(ret)→**0x7010005C(main)** — 에디터가 line 5→15로 이동 ✓ |
| F11 | 스텝인 정상(1명령) ✓ |
| 호스트 프레임 | FP1=0x8003xxxx(원본 PCXI)는 "Unknown Source" — 정상(ELF 범위 밖) ✓ |

**복원** (`tools/m25_3_ram_restore.js`): 레지스터 40/40 + 두 영역 MATCH,
PC=0x8003A332(입력값) ✓ → 코어1/2 재개(`tools/m25_5_resume_cores.js`) →
**3코어 실행(자연 상태) 복귀** ✓.

**남은 조건**: 자연 프로그램(사용자 앱, main=0x8003A328)에서 같은 경험을 하려면
해당 빌드의 **디버그 정보 포함 ELF**를 `program`에 지정하면 된다 (메커니즘 동일).

## 재현 도구

- `tools/m25_5_disasm_isa_test.js` — ISA 케이스 검증 (확장과 동일 요청)
- `tools/m25_5_frames_raw.js` — StackTrace 프레임 컨텍스트 JSON 덤프
- `tools/m25_5_sym_query.js` — `Symbols findByAddr` 조회
- `tools/m25_5_ctx_tree.js` — 컨텍스트 트리 조회 (Processes.getChildren)
- `tools/m25_5_resume_cores.js` — 코어 1/2 재개 (자연 상태 복원)
