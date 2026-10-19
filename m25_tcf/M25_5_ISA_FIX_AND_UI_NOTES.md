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

## 재현 도구

- `tools/m25_5_disasm_isa_test.js` — ISA 케이스 검증 (확장과 동일 요청)
- `tools/m25_5_frames_raw.js` — StackTrace 프레임 컨텍스트 JSON 덤프
- `tools/m25_5_sym_query.js` — `Symbols findByAddr` 조회
- `tools/m25_5_ctx_tree.js` — 컨텍스트 트리 조회 (Processes.getChildren)
