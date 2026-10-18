# ShieldBuddy TC275 회로도 분석 — MCDS 접근 경로 확정 (2026-10-18)

**회로도 확보**: Hanyang University 배포본(AES 암호화 → 빈 사용자 암호로 복호화 성공!, 6페이지 —
`refs/shieldbuddy_schematic.pdf/.txt`, Rev B1 2017). + TC375 자매보드 회로도(공개본) 교차 확인.

## 1. 온보드 FTDI 배선 (핵심!)

- **U301 = FT2232D/TR** (온보드 USB 디버그 칩 — DAS의 "DAS JTAG AURDUINO 46YPEADW"!)
- FTDI 신호: `UTDI / UTDO / /UTRST / UTCK_DAP0 / UTMS_DAP1` →
  보드 신호: `TDI / TDO / /TRST / TCK_DAP0 / TMS_DAP1` (JTAG+DAP 공용 배선!)
- TC275 칩 핀 11~14 = `VSSP/TMS`, `NC/TCK/P0.0`, `SYSDISC/TDO/P0.2`, `NC/TDI/P0.1`

## 2. ★ P302 = "DAP2WIRE" 커넥터가 보드에 실장되어 있음!

```
P302 (DAP2WIRE) — 표준 Infineon 10핀 DAP 커넥터 (AP24003 규격!)
1: VREF        2: DAP1
3: GND         4: DAP0
5: GND         6: USER0_IO
7: KEY         8: USER1_IN
9: GND        10: RESET
```
- **외부 DAP 디버거(예: DAP miniWiggler V3!)를 여기에 직접 연결 가능** ✓
- 포럼의 동일 사례(uli, 2021): "We use the Lauterbach Trace32 debugger with DAP interface...
  I suppose connector P302 on the ShieldBuddy is a DAP connector but it is not mentioned in the
  user manual. Furthermore I can't figure out the interaction of the DAP interface with the
  on-board USB debug connector (FTDI chip)" ← **정확히 우리 문제!**

## 3. 결정적 해석 (ED + TDI 문제!)

- 매뉴얼 Table 1-3: **ED QFP 패키지의 TDI 핀 = VDDPSB(전원)로 대체 → "JTAG is not available
  for such EDs"** — 우리 보드의 칩 = TC275ED(EMEM 실재 확인!) → **JTAG TAP 사용 불가!**
- 회로도의 TDI 배선 = PD 기준(핀 14 = NC/TDI/P0.1) — **ED에서는 이 핀이 VDDPSB(3.3V 공급)**
  → **온보드 FTDI의 JTAG 경로 = 성립 불가** → **DAP(2-wire: DAP0/DAP1!)만 유효** ✓
- 즉: [온보드 FTDI = DAP는 가능(식별/메모리 정상 ✓)] — 그러나 [MCDS/EEC(map 1 = AP1!) 접근은
  ✗ 0x8D] — DAS64가 권장하는 "**AP56004 DAP miniWiggler V3**"(신형 FT2232H!)와 달리
  **온보드 FT2232D(2005년대 칩, "Bare On-Board Wiggler")는 DAS의 확장(AP1/EEC) 지원이 제한적**인
  것으로 판정 — 원본 recorder·configurator·MTV 전부 동일 실패와 정합.

## 4. 결론 — 실행 가능한 해결책!

**P302(DAP2WIRE) 커넥터에 외부 DAP 디버거를 연결하면 됩니다:**
1. **DAP miniWiggler V3** (DAS64 권장, AP24003 호환 10핀!) — P302에 직접 연결 ✓
2. 또는 Lauterbach/UAD 등 DAP 지원 디버거 (포럼 사례와 동일!)
3. 연결 후: map_probe로 MCDS 접근 개방 확인 → mcdsrl_record 실녹화 → M23-3 진행

**즉, 새 보드 없이 외부 DAP 디버거 1개로 검증 가능!** (온보드 FTDI는 기본 디버그용으로 유지)
