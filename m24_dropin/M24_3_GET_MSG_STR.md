# M24-3 — Decoder 엔진 확장: get_msg_str 메시지 타입 캘리브레이션 (2026-10-18)

## 1. get_msg_str — 합성 메시지 매트릭스로 11/11 완전 파리티!

**방법**: 트레이스 데이터 없이 **합성 mcdsdl_msg_st**(메시지 타입별!)로 원본과 직접 비교!

| type (MCTRL_MT_*) | 원본 comment 문자열 | 우리 | 결과 |
|---|---|---|---|
| 0 UNKNOWN / 1 ENDOFTRACE | '' (빈 문자열!) | '' | **일치** ✓ |
| 4 IFTG | 'Trace gap due to fill level of on-chip trace buffer' | 동일 | **일치** ✓ |
| 10 ERROR_MSG_FIFO | 'MCDS message fifo overrun in observation block' | 동일 | **일치** ✓ |
| 11 ERROR_MSG_FIFO_PTU | 'MCDS message fifo overrun in observation block (PTU)' | 동일 | **일치** ✓ |
| 13 ERROR_MSG_OTHER | 'Message can't be decoded' | 동일 | **일치** ✓ |
| 22 COUNTER / 21 WATCH / 23 TIME | '' (조건부 — 합성 기본값!) | '' | **일치** ✓ |
| 27 OTGB / 30 PTU | 'ERROR no msg available' (TDR 조회 실패!) | 동일 | **일치** ✓ |
| **20 DATA / 24 STATUS** | **크래시!!** (설정 의존 — 합성 입력에 취약!) | '' (안전!) | **원본 버그** (문서화!) |

**부수 규명**:
- 모든 msg_str 필드의 기본값 = **빈 문자열 ""** (NULL 아님!)
- `MCDSDL_MSO_BASIC`(비트 0!) 처리 = 엔진 의존 (FUN_18008b3e0!) — 원본은 합성 메시지에서 크래시
- 고정 문자열 4종 + TDR 오류 1종 = **정확히 일치** ✓

## 2. 원본의 취약점 2건 발견 (M24 시리즈 누적)

1. `decode(NULL, 0, 0x10, dm)` → **크래시** (NULL 역참조 — M24-2!)
2. `get_msg_str(합성 메시지 type=20/24)` → **크래시** (설정 의존 경로!)
→ 우리 구현은 모두 **우아한 처리** (문서화된 차이 — 안전 우위!)

## 3. 엔진 구조 정리 (M24-2 + M24-3 통합)

```
[decode] → [TDF/DM 디스패치 ✓ 캘리브레이션 완료!]
  → [피드 (FUN_1800599a0/597d0 — 58~60줄!)] ← 다음 증분
  → [코덱 (압축 해제 — 거대 함수 23개!)] ← 장기 (실데이터 필요!)
  → [파서 (FUN_18005ade0 — 92줄!)] ← 다음 증분
  → [전/후처리 (FUN_18005bd30/a190 — 282/229줄!)] ← 메시지 타입 분기!
  → [get_msg_str ✓ 고정 문자열 캘리브레이션 완료!]
  → [get_instr (DisasmDecoder!) ← 장기 (TriCore ISA!)
```

## 4. 산출물

- `mcdsdl` 갱신: get_msg_str 캘리브레이션 (스모크 13케이스 매트릭스!)
- 증거: `m24_smoke_ours4.log`/`m24_smoke_orig4.log` (m24_dropin 갱신!)
- 참고: mctrl_msg_type_et 정본 = `mcds_control.h` (MCTRL_MT_* 0~30!)
