# mcds_decoder.dll 재구현 타당성 분석 (2026-10-18)

**결론: API 표면(주요 함수 25개)과 주변 레이어는 구현 가능 ✓ — 그러나 핵심인 "디코드 엔진"은
비현실적 ✗ (10만+ 줄 규모). recorder와 달리 decoder는 "로직 자체"가 라이브러리 안에 있어
얇은 대체(shim)가 성립하지 않음. → **원본 decoder 유지가 정답** (M16-era 결정과 일치).**

## 1. API 표면 (25 export, mcdsdl_*) — 전부 얇은 래퍼!

| 함수 | 본문 | 함수 | 본문 |
|---|---|---|---|
| get_msg_str | 185줄 | mcds_file_save/load | 43/41줄 |
| decode | 68줄 | elf_file_load | 35줄 |
| get_instr / get_instr_type | 62/50줄 | set_mcdsconfig(_with_file) | 25/44줄 |
| get_data/code_symbol(+addr) | 45~24줄 | lib_init/version/error/clear | 21~16줄 |
| get_msg/next/prev/num | 20~17줄 | get_error_counter/trace_marker | 20/17줄 |

→ **export 전체 합계 ≈ 800줄** — recorder(22 export)와 같은 급! (전부 "디스패처"!)

## 2. 주변 미들웨어 (export가 직접 호출하는 내부) — 소형~중형 ✓

| 내부 함수 | 역할 | 크기 |
|---|---|---|
| FUN_180058a10 | **.mcds 파일 파서** | 146줄 |
| FUN_18003b080 | 메시지 처리 (MCTRL_MT_* 분기) | 111줄 |
| FUN_1800599a0 | 트레이스 피드 진입 | 60줄 |
| FUN_18008a3f0 | 상태 리셋 | 66줄 |
| FUN_180059690 / FUN_18005b070 | NULL/0 처리 / 에러 카운터 | 23/17줄 |

→ **.mcds 포맷은 이미 규명 완료**(텍스트 헤더 + raw!) — 파일/심볼/설정/상태 레이어 = 구현 가능 ✓

## 3. ★ 핵심 블로커 — 디코드 엔진 (비현실적 ✗)

- 전체: **2,409 함수 / 174,670줄** (decompile 5.1MB!)
- **500줄 이상 거대 함수 23개**: FUN_1800cc220(**3,998줄**!), FUN_18007c480(3,240!), FUN_1800c48e0
  (2,409!), FUN_18006e040(1,995!), FUN_18006a0b0(1,742!), FUN_18009d960/a0d20(1,724×2!) …
- 100~499줄 409개 + 100줄 미만 1,977개
- **실체**: MCDS 트레이스 압축 해제(바이너리 트레이스 스트림 → 메시지 복원!) + 메시지 타입별
  해석(MCTRL_MT_* 수십 종!) + **TriCore 명령어 디스어셈블**(get_instr!) — 전부 사양 문서 없이
  디컴파일만으로 재현해야 하는 영역

## 4. recorder vs decoder — 근본 차이

| | recorder (M23, 완료 ✓) | decoder (이번 조사) |
|---|---|---|
| 성격 | **I/O 심(shim)** — 로직은 타깃(TAS/서버)에 있음 | **알고리즘 본체** — 로직이 DLL 안에 있음 |
| API | 22 export (얇음) | 25 export (얇음 ✓) |
| 내부 | TAS 클라이언트(규명 완료!) | **디코드 엔진 17만줄** |
| 구현 | ✓ 가능 (M23-1/2 완료!) | API는 가능, **엔진은 불가** ✗ |

## 5. 권고

1. **전체 재구현 = 비현실적** ✗ (M16-era의 "수작업 정리 규모가 비현실적" 판단이 정확했음!)
2. **파이프라인 = 원본 decoder 유지**: [우리 recorder ✓ → .mcds → **원본 decoder** → MTV/CCL] —
   .mcds 포맷이 표준이라 완전 호환 (M23-3에서 검증 예정!)
3. **부분 구현(가능한 범위)**: [API 표면 + .mcds load/save + ELF 심볼 + 설정/상태] ≈ 1,500줄
   — 단, **디코드가 빠지면 실효 가치 제한적** (커스텀 뷰어/도구용 조각으로만 유효)
4. **검증 전제**: .mcds 테스트 파일 = 실녹화 필요 (어댑터 해결 후 — M23-3와 동일 블로커!)

## 6. 참고 자료
- 헤더: `C:\Program Files\DAS64\include\mcds_libs\mcds_lib_decoder.h` (v4.1.0 — 25 export!)
- 디컴파일: `ghidra_work\mcds_decoder.dll_decomp.txt` (2,409 함수!) / CCL판(10,487 함수!)
- M16 분석: `MCDS_LIBS_GHIDRA_ANALYSIS.md` §3/§8
- CCL 사용 흐름: `mcds_lib.py` (mcdsdl_* 래퍼 — lib_init → file_load → get_msg 순회 → get_msg_str!)
