# mcdsdl — MCDS Decoder Library (`mcds_decoder.dll`) API 프로토타입

`mcds_decoder.dll`(**MCDS Decoder Library 4.1.0**)의 **API 표면(25 export) + 구현 가능한 계층**을
재현한 프로토타입 — 원본과 교차 검증으로 **완전 파리티** (엔진 영역 제외 — 아래 참조).

- 소스: `src/mcdsdl_impl.cpp` + `src/mcdsdl.def`(25 export)
- 헤더: `include/` (SPRINT 라이선스 원본 유지)
- 테스트: `tests/mcdsdl_smoke.cpp` (양쪽 DLL 로드 비교!) + `tests/gen_test_elf.py`(합성 ELF32 생성!)
- 빌드: `cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release`
- 실행: `mcdsdl_smoke.exe [dll]` (기본 mcdsdl.dll)

## 구현 범위 (25/25 export)

| 계층 | 함수 | 상태 |
|---|---|---|
| 코어 | `lib_init(4)`·`lib_version`(4.1.0!)·`get_error` | ✓ |
| 상태 | `clear`·`get_error_counter`·`get_trace_marker`·`get_num_msg`·`get_msg`·`get_next_msg`·`get_previous_msg` | ✓ |
| **.mcds 파일** | `mcds_file_load`(텍스트 헤더 파서 — recorder/CCL 양식 둘 다!)·`mcds_file_save` | ✓ |
| **ELF 심볼** | `elf_file_load`(ELF32 LE!)·`unload_all`·`get_code/data_symbol`·`..._addr` | ✓ |
| 설정/콜백 | `set_mcdsconfig`·`set_mcdsconfig_with_file`·`set_device_callback` | ✓ |
| **엔진** | `decode`(트레이스 압축 해제 — 174k줄!)·`get_instr/get_instr_type`(TriCore 디스어셈블!) | **정직한 스텁** (의도적!) |

## 원본 교차 검증 (스모크 — 동일 바이너리 양쪽 실행)

```
[공통 일치]  lib_init(4)/version 4.1.0 · ELF 심볼(by-addr/by-name!) · .mcds 로드/저장 오류 시맨틱
[캘리브레이션 5건 — 전부 해소!]  get_msg(-1!) · "ERROR no trace data in file" · by-name size-1 ·
                                get_instr(0x01000000+출력 불변!) · file_save(0x01000000!)
[문서화된 차이 1건]  get_instr_type(0x1D000000): 우리 0(UNKNOWN) vs 원본 1(NONE — 실제 해석!)
```

증거: `m24_dropin/` (smoke_ours.txt·smoke_orig.txt)

## 왜 엔진은 구현하지 않는가 (M23 타당성 분석!)

- decoder = **알고리즘 본체** (recorder와 달리 "얇은 심"이 성립 안 함!): 2,409 함수 / **174,670줄**
  (3,998줄·3,240줄·2,409줄 거대 함수 23개!) — MCDS 트레이스 압축 해제 + 메시지 타입별 해석 +
  TriCore 디스어셈블
- 실사용 파이프라인: [우리 recorder ✓ → `.mcds` → **원본 decoder** → MTV/CCL] — `.mcds`가 표준
  포맷이라 완전 호환!

## 환경 노트

- 검증에 사용한 실파일: M23 세션의 recorder 산출물(`m23_rec_orig.mcds` — 헤더만, 데이터 없음!)
- 스텁 함수들은 원본과 동일한 반환코드/출력 시맨틱으로 캘리브레이션됨 (오류 문자열 포함!)
