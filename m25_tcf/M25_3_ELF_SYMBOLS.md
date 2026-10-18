# M25-3 — ELF 심볼/소스 (source-level 디버깅 기반) (2026-10-19)

**결과: 소스 레벨 파이프라인이 완전 동작** — VS Code에서 `m25_elf_test.c:5`에
브레이크포인트를 걸면 에이전트가 ELF DWARF로 주소(0x8000000C)를 해석해
**3코어 전부에 HW 트리거로 심는다** (UI에 해석 결과 표시 확인).

## 0. 메커니즘 규명 (확장 ↔ 에이전트)

- 확장은 연결 직후 각 코어에 대해
  `MemoryMap set [ctx, [{FileName: "<ELF 경로>", ID: ctx}]]`를 보낸다
  (`debugProvider.initMemoryMap`).
- 에이전트의 `symbols_elf.c`는 **메모리 맵 리전의 FileName으로 ELF를 연다**
  (`elf_open_memory_region_file` + `add_memory_map_event_listener`) —
  즉 ELF 경로는 클라이언트가 메모리 맵으로 전달한다.
- 우리 백엔드는 이미 `ENABLE_ELF=1`(M25-1 빌드 구성)이라 별도 구현 없이 동작 ✓
  (단, 레지스터 DWARF 번호가 EABI와 일치해야 함 — M25-1에서 이미 일치시킴!).

## 1. 검증 (프로토콜 + UI)

### (a) 테스트 ELF 빌드 (HighTec 툴체인)
- `C:\work\aurix_tcf_test\m25_elf_test.c` (add/mul_add/main/g_counter) +
  `m25_test.ld`(PFLASH 0x80000000) / `m25_ram.ld`(DSPR 0x7000A000).
- 심볼: `add@0x80000000(24B)`, `mul_add@0x80000018`, `main@0x8000003A`,
  `g_counter@0x70000000`.

### (b) 프로토콜 (`m25_3_elf_probe.js`)
```
MemoryMap set core0 -> m25_test.elf  => MemoryMap/changed ✓
Symbols findByAddr core0 0x80000010  => "add" (Size 24, Address 0x80000000) ✓
LineNumbers mapToSource              => {SLine:5, File:"m25_elf_test.c",
                                         Dir:"C:\\work\\aurix_tcf_test", IsStmt:true} ✓
```

### (c) VS Code UI (소스 브레이크포인트)
- launch.json `program` = m25_test.elf → 확장이 자동 매핑 ✓
- 편집기에서 `m25_elf_test.c:5`에 BP(F9) → 에이전트가 DWARF 라인 테이블로 해석:
```
Breakpoints getProperties ["aurix-1"] => {"File":"m25_elf_test.c","Line":5} ✓
Breakpoints getStatus                 => Instances: core0/1/2,
                                         Address 2147483660 (0x8000000C),
                                         BreakpointType "Hardware" ✓
UI: "Added breakpoint, line 5, file ..." +
    "Breakpoint status (not hit) aurix-1 at [{...Address:2147483660...Hardware}]" ✓
```
- **의의**: 소스 라인 → DWARF 라인 테이블 → 주소 → OCDS TR 트리거(3코어) 전 구간 검증.

### (d) 부수 확인
- `Symbols findByAddr`는 ELF 범위 밖 주소(실행 중 PC 0x8003A336)에 대해
  "Symbol not found" — 정상(해당 ELF가 그 코드를 덮지 않음).
- **주의**: 기존 `test.elf`(480B, MicroBlaze stub)는 더미였음 — 실ELF 필요.

## 2. 남은 항목 (M25-3b/4)

| 항목 | 내용 | 비고 |
|---|---|---|
| CSA 스택 크롤 | PCXI/FCX/LCX 체인 언와인드 | DWARF(.debug_frame) 있으면 프레임워크가 이미 언와인드(SFT_CMD_RD_REG_PCXI_TRICORE!) — 크롤은 무-DWARF 폴백 |
| TriCore 디스어셈블러 | TCF Disassembly 서비스용 | 원본은 LLVM 사용(우리는 미구현 — binutils는 GPL이라 이식 불가) |
| SVD/주변장치 | **클라이언트 측**(확장+peripheral-viewer) | 에이전트 작업 불필요 ✓ |
| **플래시 다운로드**(M25-4) | 원본 `-f`의 실체 — 실ELF를 타깃에 기록 | RAM 데모(아래)로 대체 검증 가능 |

## 3. RAM 라이브 데모 (준비 완료 — 승인 대기)

실제 소스 BP가 **히트**하는 완전한 데모 (되돌림 가능):
1. `m25_3_ram_load.js`: 코어0의 **40레지스터 + DSPR 0x7000A000~0xC000(8KB) 저장**
   (`m25_3_ram_state.json`) → `m25_ram.elf`의 .text(146B)를 DSPR에 기록 →
   PC=main(0x7000A03A), A10(SP)=0x7000C000, ICR.IE=0 설정.
2. 메모리 맵을 `m25_ram.elf`로 교체 → BP 재해석(0x7000A00C) → Run →
   **소스 라인 정지 + 변수(g_counter) + 콜스택** UI 확인.
3. `m25_3_ram_restore.js`: 레지스터 40개 + DSPR 8KB 복원 → 정지 상태 복귀
   (DSPR은 전부 0 확인됨 — 복원도 0 기록).

## 4. 산출물

- 도구: `C:\work\tcf_agent\{m25_3_elf_probe.js, m25_3_ram_load.js, m25_3_ram_restore.js}`
- 테스트 ELF: `C:\work\aurix_tcf_test\{m25_elf_test.c, m25_test.elf, m25_test.ld, m25_ram.elf, m25_ram.ld, m25_ram_text.bin}`
- 증거: `C:\work\tas_server_analysis\m25_3_evidence\` (예정)
