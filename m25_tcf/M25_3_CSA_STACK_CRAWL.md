# M25-3 부록 2: CSA 스택 크롤 (TriCore 콜스택 복원)

작성: 2026-10-19 · 상태: 완료 (TCF StackTrace 라이브, ELF 없는 코드에서도 5프레임 복원)

## 1. 배경

TCF 프레임워크의 스택 트레이스(`services/stacktrace.c`)는 두 단계로 동작한다:

1. **DWARF CFI 경로** — ELF가 매핑되어 있으면 `.debug_frame`/`.eh_frame`으로 언와인드
   (M25-3 데모에서 `add`/`main` 콜스택으로 이미 실증됨).
2. **스택 크롤 폴백** — 프레임 정보가 없으면 `crawl_stack_frame(frame, down)` 훅 호출
   (`tcf/framework/context-dispatcher.c` → 머신별 `cpudefs-mdep.c`).

ELF 없는 라이브 타깃(실행 중 프로그램)에서는 1이 불가능하므로, 2에
TriCore 고유의 **CSA(Context Save Area) 체인** 워커를 구현했다.

## 2. TriCore CSA 메커니즘 (하드웨어)

- **CALL 시**: 하드웨어가 호출자의 상위 컨텍스트를 64바이트 CSA에 저장하고
  PCXI를 그 CSA로 갱신한다. 복귀 주소는 **A11**에 들어간다(TriCore 관례:
  A11 = return address register).
- 따라서 임의 시점의 PCXI 체인 = "아직 복귀하지 않은 호출들"의 컨텍스트 목록.

## 3. 실증으로 확정한 두 가지 (TC375 라이브)

### (1) PCXI/FCX → CSA 주소 형식

```
bits [15:0]  = CSA 주소 bits [21:6]
bits [19:16] = CSA 주소 bits [31:28]  (세그먼트)
addr = ((pcxi & 0xFFFF) << 6) | ((pcxi & 0xF0000) << 12)
```

실측: `PCXI=0x003D013B → 0xD0004EC0`, `FCX=0x000D013C → 0xD0004F00`

### (2) CSA 워드 레이아웃 (64바이트, 실측 덤프로 레지스터 대조)

| 워드 | 내용 | 워드 | 내용 |
|---|---|---|---|
| 0 | PCXI (이전 링크) | 8-11 | A12-A15 |
| 1 | PSW | 12-15 | D12-D15 |
| 2 | A10 (SP) | | |
| 3 | A11 (피호출자의 복귀 주소) | | |
| 4-7 | D8-D11 | | |

(라이브 레지스터 값과 1:1 대조로 확인: A10=0x70009000, A11=0x8003A264,
D8-D15, A12-A15, PSW 전부 일치)

## 4. 크롤 알고리즘

프레임 N에서 프레임 N-1(호출자)을 만든다:

1. **N-1의 PC = N의 A11** — A11은 CSA에 저장되지 않는다(호출자의 A11은
   "그 호출자의 복귀 주소"이므로, N의 A11이 곧 N-1의 PC).
2. **N-1의 나머지 레지스터 = CSA[PCXI(N)]** — A10/A11/D8-D15/A12-A15/PSW.
3. **N-1의 PCXI = CSA의 워드 0** (다음 링크).
4. 현재 프레임의 CFA(`frame->fp`) = 그 프레임의 A10.

종료 조건:
- PCXI == 0 (체인 끝)
- 다음 CSA 주소 == **라이브 FCX**(사용 중 체인의 끝 = 자유 리스트 시작)
- 세그먼트 != 0xD (DSPR 아님)
- A11(복귀 주소) == 0
- (프레임워크 자체 가드: 최대 프레임 수, 동일 CFA/레지스터 중복 시 중단)

## 5. 검증 (라이브 타깃, ELF 없음)

`StackTrace getContext`로 FP0..FP4 조회 결과:

| 프레임 | IP | RP | 검증 (IP 직전 명령) |
|---|---|---|---|
| FP0 | 0x8003a328 | 0x8003a264 | (현재 PC) |
| FP1 | 0x8003a264 | 0x80038bea | `call 0x8003a138` @0x8003a260 ✓ |
| FP2 | 0x80038bea | 0x8003ccac | `call 0x8003a254` @0x80038be6 ✓ |
| FP3 | 0x8003ccac | 0x8003cf7a | `calli %a15` @0x8003cca8 ✓ |
| FP4 | 0x8003cf7a | (최외곽) | `calli %a15` @0x8003cf76 ✓ |

- **모든 프레임의 IP가 정확히 CALL 명령 다음 주소**임을 디스어셈블러로 실증
- 각 프레임의 RP = 다음 프레임의 IP (내부 정합) ✓
- FP4에서 `Level=0`(스택 완결), FP5 = null(체인 종료) ✓
- VS Code 확장은 TCF StackTrace 서비스(getContext/getChildren)를 사용하므로
  이 결과가 그대로 UI 콜스택이 된다.

## 6. 구현/배선

- `agent/machine/aurix/tcf/stack-crawl-tricore.{c,h}` — 크롤러 (MIT)
- `agent/machine/aurix/tcf/cpudefs-mdep.c` — `crawl_stack_frame()` →
  `crawl_stack_frame_tricore()` (기존 ERR_UNSUPPORTED 스텁 대체)
- `tools/m25_3_stacktrace_test.js` — FP0..N 조회 도구
- `tools/m25_3_csa_probe.js` — 레지스터+CSA 덤프 도구(레이아웃 실증용)

## 7. 한계 / 향후

- **하위 컨텍스트(인터럽트/트랩) 프레임**: 인터럽트 시 저장되는 하위 컨텍스트는
  레이아웃이 다르다(PCXI, PC, A11, A2/A3, D0-D3 등). 현재는 상위 컨텍스트
  체인(호출 체인)만 처리 — 인터럽트 중 정지 시 첫 프레임 해석은 DWARF 또는
  추후 보강 필요.
- ELF 없는 경우 프레임은 주소로만 표시(심볼 없음) — ELF 매핑 시 DWARF 경로가
  우선하므로 함수명/라인이 함께 나온다.
