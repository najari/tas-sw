# M24-W2 — mcdsrl 오류 문자열·주소 변환 파리티 (2026-10-18)

## ★ 결정적 발견 — configurator 주소 ↔ 전송 주소의 변환!

스모크가 configurator의 실제 config를 출력한 결과:

| | configurator (mcdsc — 디바이스 뷰!) | 원본 recorder의 전송 (EEC wire!) |
|---|---|---|
| MCDS 레지스터 | **0xF90E6xxx** (0xF90E6000/6020/6034/6028!) | **0xF00E6xxx** (F00E6000!) |
| MCDS 상태/FIFO | **0xF90E8000** | **0xF00E8000** |
| EMEM | **0x9F000000** (init 0xFFFFFFFF!) | **0xAF000000** |

→ **원본 recorder = 클라이언트 측에서 주소 변환** (0xF9→0xF0 = -0x09000000, 0x9F→0xAF =
+0x10000000!) — M23-era가 "Ghidra 오독"으로 오판했던 것의 정체! (Ghidra 값 = configurator의
디바이스 뷰가 맞았음!)

## 수정 내역 (mcdsrl)

1. **주소 변환** — config 쌍 (디바이스 뷰 → EEC wire!) + SoC/MCDS 분리 전송
   (SoC = map 0 그대로 / MCDS = map 1!)
2. **map 1 (EEC)** — config 경로 전용 (`mcdsrl_write_list_emap`!) / 공개 rw = map 0 유지
   (M23-1 증거: DSPR 읽기 = map 0!)
3. **오류 문자열** — 원본 형식: `"ERROR: Write of addr %llX in addr_map %d failed"` ✓
4. **set_mcdsconfig 스캔** — 변환 주소로 비교 (FIFO/MCDS-ID 검출!)
5. **stop cleanup** — 동일 변환 + map 1!

## 검증 (녹화 스모크 — 양쪽 완전 동일!)

```
[우리]   config: num_reg 43 crsv 2 ... mcds_device 0x0A00ED83 vsi 43
         [FAIL] mcdsrl_start_recording
         error: ERROR: Write of addr F00E6000 in addr_map 1 failed
[원본]   config: num_reg 43 crsv 2 ... mcds_device 0x0A00ED83 vsi 43
         [FAIL] mcdsrl_start_recording
         error: ERROR: Write of addr F00E6000 in addr_map 1 failed
```
→ **주소·맵·문자열 완전 일치!** ✓✓ (어댑터 블로커의 실패까지 파리티!)

- 회귀: 코어 스모크 OK ✓ (read_timestamped 카운터 정상!)
- **주목**: 어댑터가 해결되면 이 변환 경로로 원본과 동일하게 실녹화될 것 (M23-3 대비 완료!)

## 산출물
- `mcdsrl` 갱신 (src!) — 증거: 녹화 스모크 로그 2종
