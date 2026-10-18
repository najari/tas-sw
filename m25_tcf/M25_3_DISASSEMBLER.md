# M25-3 부록: TriCore 디스어셈블러 (TCF Disassembly 서비스)

작성: 2026-10-19 · 상태: 완료 (VS Code Disassembly 뷰 라이브)

## 1. 목표

원본 `aurix-debugger.exe`의 **Disassembly 뷰** 파리티. TCF Disassembly 서비스는
`getCapabilities`(ISA 목록)와 `disassemble(ctx, addr, size, {ISA,...})`를 노출하며,
에이전트가 ISA별 디스어셈블러 함수를 등록해야 동작한다:

```c
typedef DisassemblyResult * Disassembler(uint8_t * code, ContextAddress addr,
                                         ContextAddress size, DisassemblerParams * param);
void add_disassembler(Context * ctx, const char * isa, Disassembler fn);
```

binutils(GPL)의 tricore-dis를 쓰지 않고 **자체 구현**(MIT)했다.

## 2. 명령 길이 규칙 (가장 먼저 규명한 것)

**첫 바이트(최하위 주소)의 LSB가 0이면 16비트, 1이면 32비트.**

- 16비트: opcode = `[7:0]` (nop/ret/debug/rfe는 `[15:8]` 특수)
- 32비트: opcode = `[7:0]`(op1), 보조 = `[15:8]`(op2)/`[19:16]`/`[23:20]`

실타깃 코드 384개 명령 전부 이 규칙과 일치함을 확인(참조 objdump의
`.hword` 폴백조차 이 규칙 기준 2바이트 스텝의 산물이었다).

## 3. 필드 레이아웃 도출 방법

HighTec 툴체인(`tricore-elf-as` / `tricore-elf-objdump`)에 **후보 명령을 조립**해
바이트를 얻고, 피연산자를 체계적으로 바꿔가며 비트 필드를 역산했다
(`tools/tricore_fields*.js`, `tools/tricore_leaabs.js`). 확정된 주요 레이아웃:

### 16비트

| op | 명령 | 필드 |
|---|---|---|
| 0x00 | nop/ret(0x9000)/debug(0xA000)/rfe(0x8000) | `[15:8]` |
| 0x02/0x82 | mov d,s / mov d,const4(부호) | d=`[11:8]`, s/const=`[15:12]` |
| 0xDA | mov d15, const8 | const=`[15:8]` (d15 내재) |
| 0xD4 | ld.a a, [a] | d=`[11:8]`, a=`[15:12]` |
| 0x16/0x96 | and/or d15, const8 | const=`[15:8]` (d15 내재) |
| 0x40/0x80/0x60 | mov.aa / mov.d / mov.a | `[11:8]`=`[15:12]` |
| 0x42/0xC2/0xA2 | add d,s / add d,c4(부호) / sub d,s | |
| 0x26/0xA6/0xC6/0x46 | and/or/xor/not | |
| 0xE2/0x06/0x86 | mul / sh / sha | |
| 0x20/0x30/0xB0 | sub.a sp,const8 / add.a a,a / add.a a,c4 | |
| 0x14/0x34/0x54/0x74/0x94/0xB4 | ld.bu/st.b/ld.w/st.w/ld.h/st.h `[a]` | |
| 0x04/0x24/0x44/0x64/0x84/0xA4 | 위의 `[a+]` (post-increment) | |
| 0x48/0x68 | ld.w d,[a15]off4 / st.w [a15]off4,d | off=`[15:12]`*4 |
| 0x4C/0x6C/0x78 | ld.w d15,[a]off4 / st.w [a]off4,d15 / st.w [sp]off4,d15 | |
| 0xDC | ji a | |
| 0x3C/0x5C | j/call disp8(부호) | target = addr + disp*2 |
| 0x6E | jz d15, disp8(부호) | target = addr + disp*2 |
| 0x76/0xF6 | jz/jnz d, disp4(무부호, 전방 전용) | |
| 0x5E/0x7E | jeq/jne d15, d, disp4 | |
| 0x1E/0x9E | jeq/jne d15, const4, disp4 | |
| 0x2E/0xAE | jz.t/jnz.t d15, const4, disp4 | |

### 32비트

| op1 | 명령 | 필드 |
|---|---|---|
| 0x19/0x59 | ld.w/st.w `[a]disp` | **산개 disp**: bits3-0←`[19:16]`, bits5-4←`[23:20].b0-1`, bits9-6←`[31:28]`, bits11-10←`[23:20].b2-3`, bits15-12←`[27:24]` |
| 0x09/0x89 | ld.b/bu/h/hu·ld.w / st.b/h·st.w `[a(+)]disp` | type=`[23:22]`, word=`[27:24].b0`, post-inc=`[27:24].b3==0`, disp=부호6비트×16+`[19:16]` |
| 0x0B | add/sub/min/max/abs | `[23:20]`+`[27:24]` 서브op |
| 0x0F | and/or/xor/sh/sha (3op) | `[23:20]` 서브op |
| 0x73 | mul | `[19:16]`==0xA |
| 0x1B/0x9B | addi/addih d,s2,const9 | const9={[23:20]b0,[19:16],[15:12]} (addi=부호, addih=무부호) |
| 0x91 | movh.a a, imm16 | imm={[27:16],[15:12]} |
| 0xD9/0xC5 | lea a,[a]disp / lea a,addr | 산개 disp (0x19와 동일) |
| 0x49 | lea a,[a]disp (단축) | 산개 disp에서 `[27:24]` 부분 제외(12비트) |
| 0x1D/0x6D/0x5D/0x9D | j/call/jl/ja disp16 | target = addr + disp*2 (ja는 절대) |
| 0x5F/0xDF | jeq/jne s1,s2 / d,const4 | `[31]`=negate, `[11:8]`=s1 |
| 0x3F/0x7F | jlt/jlt.u / jge/jge.u | `[31]`=unsigned 플래그 |
| 0x3B | mov d, const16 | {[27:24],[23:20],[19:16],[15:12]} |
| 0x37 | extr d,s,pos,width | pos=`[15:12]`, width=`[19:16]` |
| 0x01 | addsc.a a,a,d,shift | `[27:24]`==6, shift=`[19:16]` |
| 0x03/0x23 | madd/msub d,s1,s2,s3 | `[19:16]`==0xA, 순서=[31:28],[27:24],[11:8],[15:12] |
| 0x8F | sh/sha d,s,const5 | `[27:24]`=0/2, const5={[23:20]b0,[19:16]} |
| 0x4D/0xCD | mfcr/mtcr | CSR={[23:16],[15:12]} |
| 0xA5 | st.w 0xf000nnnn, d | nnnn=`[31:16]` |
| 0xAD/0x0D/0xFD | syscall/svlcx·rslcx·rfm/loop | |

### 검증 방법론의 주의점

- 어셈블러가 라벨 대상 분기를 **재배치(relocation)로 0을 채우므로**, 분기
  변위는 반드시 **숫자 리터럴**로 테스트해야 했다.
- 16비트 조건분기(disp4)는 **무부호·전방 전용**임을 실증: 후방 분기를 쓰면
  툴체인이 32비트(0xDF) 형식을 선택한다.
- `add %d, 8~15`(0xC2) 등은 부호 4비트(-8..7)로 인코딩되어 툴체인이
  32비트 형식을 택한다 → 표시도 부호 있음으로.

## 4. 구현

- `agent/machine/aurix/tcf/disassembler-aurix-tricore.c` (~430줄, MIT)
- 미지원 opcode → **직접 `.half`/`.word` 출력**(프레임워크 폴백 대신):
  정렬이 항상 유지된다. (참조 objdump는 미지원 opcode에서 2바이트 `.hword`로
  폴백해 스스로 정렬이 깨진다 — 대조 시 이 아티팩트를 감안해야 함.)

### 등록 배선

```c
/* cpudefs-mdep.h */  #define ENABLE_add_cpudefs_disassembler 1
/* cpudefs-mdep.c */  void add_cpudefs_disassembler(Context *cpu_ctx) {
                          add_disassembler(cpu_ctx, "TriCore", disassemble_tricore); }
/* context-aurix.c */  context_get_group(): 최초 1회 ini_cpu_disassembler(ctx);
```

ISA 이름 `"TriCore"`는 `context_get_isa()`가 반환하는 값과 일치해야 한다.

## 5. 검증 결과 (objdump 참조 대조)

도구: `tools/m25_3_disasm_compare.js` — TCF 실서비스에 `Disassembly disassemble`을
던지고, 같은 바이트를 `objdump -D -b binary -m tricore`로 디코딩해 주소 정렬
비교(수치 표기·심볼 주석·`.hword`↔`.half` 정규화).

실타깃(ShieldBuddy TC375, 실행 중 프로그램) PFLASH 코드 9개 구간 × 120바이트:

| 구간 | 일치 | 비고 |
|---|---|---|
| 0x8003a140 | 50/50 | 완전 일치 |
| 0x8003a198 | 43/43 | 완전 일치 |
| 0x8003a240 | 44/45 | 참조 폴백 아티팩트 |
| 0x8003a300 | 41/42 | 미지원 0x69 1개 |
| 0x8003a380 | 37/43 | 미지원 계열 일부 |
| 0x8003a400 | 30/36 | 참조 폴백 아티팩트 다수 |
| 0x8003a480 | 35/40 | |
| 0x8003a500 | 34/41 | |
| 0x8003a580 | 43/44 | |
| **합계** | **357/384 (93%)** | |

- 남은 차이의 대부분은 **참조 objdump 자체의 폴백/정렬 아티팩트**(미지원
  opcode에서 `.hword` 2바이트)로, 실제 디코딩 오류가 아니다.
- 알려진 미지원: 0x69(LD.A/ST.A disp 계열 추정), 0xFF(ADDSC.AT 추정),
  0x39 등 — 전부 `.word`로 정렬 유지 표시.

## 6. 산출물

- `agent/machine/aurix/tcf/disassembler-aurix-tricore.{c,h}`
- `agent/machine/aurix/tcf/cpudefs-mdep.{c,h}` (등록 훅)
- `agent/system/aurix/tcf/context-aurix.c` (ini_cpu_disassembler 호출)
- `tools/m25_3_disasm_compare.js` (참조 대조 도구)
- `tools/tricore_fields*.js`, `tools/tricore_leaabs.js` (필드 도출 도구)
