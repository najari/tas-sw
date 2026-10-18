# MTV (원본 MCDS Trace Viewer) Frida 캡처 — ShieldBuddy에서의 녹화 시도 (2026-10-18)

**결론: 원본 Trace Viewer도 MCDS 레지스터 접근(map 1)에서 0x8D로 차단됨 — 녹화 불가 확정.**

- 캡처 대상: `mcds_trace_viewer.exe` (Frida 스폰, ws2_32 send/recv 후킹 — MTV는
  `mcds_recorder.dll`+`WS2_32.dll` 사용 = TAS 클라이언트!)
- 총 136 요청/응답 쌍 (PL2 재조립) — 흐름: [연결] → [MCDS 프로브] → [폴링 루프]
- **설정 기록(write) 프레임이 아예 없음** = 녹화가 프로브 단계에서 차단되어 진행되지 못함

## 핵심 프레임

### pair 4 — MCDS 레지스터 읽기 (map 1)
```
req: 1c000000 01b0ff00 c1ff 0200 00110100 00150ef0 00 2500 8000 b10100
rsp: 10000000 00b08108 00 25 00 8d 00 b1c1ff
=> RD32 @0xF00E0080 (map 1): err 0x8D (버스 에러!)  ← recorder/configurator와 동일한 블록
```

### pair 5 — SoC측 읽기 (map 0, 0xF003601C)
```
req: 18000000 01b0ff00 c2ff 0100 001503f0 00 251c 6000 b10100
rsp: 14000000 00b08108 01 25 01 80 0f020200 00 b1c2ff   => OK
```

### pair 6 — 타임스탬프 카운터 (map 15, RD64 @0x50000090)
```
req: 1c000000 01b0ff00 c3ff 0080 00110f00 00150050 00 2790 0000 b10100
rsp: 18000000 00b08108 02 27 02 80 a926ae16 00000000 00 b1c3ff   => OK (카운터 = 0x16AE26A9)
```

### pair 7~ = 폴링 루프 (카운터 + 상태 읽기만 반복 — 설정 쓰기 없음)
```
pair 8: RD64 @0x50000090 → 0x16AE2A70 (증가 ✓)
pair 9: RD64 @0x50000090 → 0x16AE2BF3 (증가 ✓)
pair 10: RD32 @0xF0001000 → 0x74E2E5C6
```

## 정합성
- M23-1의 원본 recorder 캡처(c1: map-1 0xF00E0080 → 0x8D!)와 **완전 동일**
- 우리 mcdsrl 구현의 실패(0x8D)와도 **완전 동일** → 소프트웨어 문제 아님, 어댑터(McdAccHw) 한계 확정
- 카운터(map 15 RD64)·EMEM(map 0)·SoC 읽기는 전부 정상 ✓
