# M24-1 — MCDS Decoder Library API 표면 구현 (mcdsdl) (2026-10-18)

`mcds_decoder.dll`의 **API 표면 25개 export + 구현 가능한 계층 전부**를 우리 구현으로 재현 —
원본 교차 검증으로 **완전 파리티** (엔진 영역 제외 — 문서화된 한계). 신규 프로젝트 `C:\work\mcdsdl\`.

## 1. 구현 범위 (25/25 export + 계층)

| 계층 | 함수 | 상태 |
|---|---|---|
| 코어 | lib_init(4!)·lib_version(4.1.0!)·get_error | ✓ |
| 상태 | clear·get_error_counter·get_trace_marker·get_num_msg·get_msg·get_next_msg·get_previous_msg | ✓ |
| **.mcds 파일** | **mcds_file_load**(텍스트 헤더 파서 — recorder/CCL 양식!)·**mcds_file_save** | ✓ |
| **ELF 심볼** | **elf_file_load**(ELF32 LE 파서!)·unload_all·get_code/data_symbol·..._addr | ✓ |
| 설정 | set_mcdsconfig·set_mcdsconfig_with_file(.mcdsc 파서!) | ✓ |
| 콜백 | set_device_callback | ✓ |
| **엔진** | **decode**(트레이스 압축 해제!)·**get_instr/get_instr_type**(TriCore 디스어셈블!) | **정직한 스텁** (문서화!) |

- `.mcds` 파서: `McdsDevice`/`McdsDeviceUsed`/`McdsToolUsed`/`McdsFrequencyMHz`/`RecordingDate`/
  `RecordingTime` + `START_OF_MCDS_CONFIG` + 쌍 + `START_OF_TDR` + raw (recorder 양식!) **+**
  CCL 양식(`McdsFrequencyMHz=…\n\n` + raw!) 둘 다 지원 ✓
- ELF: ELF32 LE + SHT_SYMTAB/strtab + STT_FUNC/STT_OBJECT (TriCore EM=189!) ✓

## 2. 원본 교차 검증 (스모크 양쪽 실행 — 동일 바이너리!)

| 항목 | 우리 | 원본 | 결과 |
|---|---|---|---|
| lib_init(4)/version | 4.1.0 | 4.1.0 | **일치** ✓ |
| ELF 심볼 by-addr | 이름·주소·크기 정확 | 동일 | **일치** ✓✓ |
| get_msg(범위 밖) | 0xFFFFFFFF (-1) | 0xFFFFFFFF | **일치** ✓ (캘리브레이션 1!) |
| .mcds 로드(데이터 없음) | 0x00400000 + "ERROR no trace data in file" | 동일 | **일치** ✓ (2!) |
| by-name 심볼 크기 | size-1 (63/15!) | size-1 | **일치** ✓ (3!) |
| get_instr | 0x01000000 + 출력 불변 | 동일 | **일치** ✓ (4!) |
| file_save(데이터 없음) | 0x01000000 | 0x01000000 | **일치** ✓ (5!) |
| **get_instr_type(0x1D000000)** | 0 (UNKNOWN — 정직!) | **1 (NONE — 실제 해석!)** | **문서화된 차이** (엔진!) |

→ **캘리브레이션 5건 전부 해소 + 완전 파리티** (엔진 1건만 의도적 차이!)
→ 증거: `m24_smoke_ours2.log`/`m24_smoke_orig2.log` (1차: m24_smoke_ours/orig.log)

## 3. 엔진 영역 (의도적 미구현 — M23 타당성 분석과 정합)

- `mcdsdl_decode` = 174k줄 엔진 (트레이스 압축 해제 + 메시지 복원!) — 스텁 (상태 보존 + 명확한 오류 문자열!)
- `get_instr/get_instr_type` = TriCore 디스어셈블 — 스텁 (rc/출력 시맨틱은 원본과 일치하게 캘리브레이션!)
- → 실사용 파이프라인은 원본 decoder 유지: [우리 recorder ✓ → .mcds → **원본 decoder** → MTV/CCL]

## 4. 산출물/환경

- 프로젝트: `C:\work\mcdsdl\` — `mcdsdl.dll`(25 export!) + `mcdsdl_smoke.exe` + `tests/gen_test_elf.py`
  (합성 ELF32 생성기!) + CMake — 공개: tas-sw `mcdsdl/`
- 검증: 스모크(양쪽 DLL!) + 합성 ELF + M23의 실파일(`m23_rec_orig.mcds`!)
- 참고: 헤더 v4.1.0(25 export — CCL 계층 함수 포함!), 디컴파일 2,409 함수, M23_DECODER_FEASIBILITY.md
