# M25-4b: 자체 플래셔 (PFLASH erase/program) — 1차 구현·검증 (2026-10-19)

## 1. 참조 구현 정찰

- 확장은 `aurixflasher.exe -gui off -elf <elf> [-hex ...] -start off -ver on -id <device>`로
  굽는다 (`extension.js:624` `runAurixFlasher`) — 별도 도구가 플래시를 담당.
- 이 머신의 AURIX Flasher Software Tool 3.1.0
  (`...\com.ifx.ads2.flash_2.0.24\AurixFlasherSoftwareTool\3.1.0.0\`):
  - `flax_TC37x.hex` = 플래시 알고리즘 (HEX: DSPR 0x70003800 스테이징 +
    PSPR 0x70100000 코드, 엔트리 0x70100008, start-address 레코드)
  - `DeviceConfigs/TC37x_A_step.json`: PFLASH 6MB(0xA0000000), 물리 1MB×6,
    **논리(소거) 섹터 16KB**, UCB 0xAF400000
- 알고리즘의 레지스터 시퀀스는 **iLLD IfxFlash**(TC3xx, Boost Software License 1.0 —
  파생 구현 허용)로 확정:
  - 커맨드 인터페이스: `0xAF000000 | {0x5554, 0xAA50, 0xAA58, 0xAAA8, 0x55F0}`
  - 소거: [AA50]=sectorAddr, [AA58]=1, [AAA8]=0x80, [AAA8]=0x50
  - 페이지(32B): enterPageMode 0x50 → [55F0]에 64비트×4 로드 →
    [AA50]=pageAddr, [AA58]=0, [AAA8]=0xA0, [AAA8]=0xAA
  - 상태: `DMU_HF_STATUS`(0xF8040010) P0BUSY=bit2/P1BUSY=bit3,
    에러 `DMU_HF_ERRSR`(0xF8040034)
  - ENDINIT: SCU `WDTS_CON0`(0xF00362A8) [ENDINIT0/LCK1/PW15:2] 클리어·복원
  - 워치독 비활성화: CON1.DR(bit3) — CPU0(0xF003624C/50), Safety(0xF00362A8/AC)

## 2. 자체 구현 (원본에 없는 신규)

- `flasher/m25_flash_loader.c` (TriCore C, HighTec 툴체인, **858 B**):
  - PSPR(0x70100000)에서 실행, DSPR 메일박스(0x7000F000) 폴링
  - 명령: erase(섹터 N개)/program(32B 페이지 N개)/exit
  - 시작 시 CPU0 + Safety 워치독 비활성화(표준 플래셔 관행), ENDINIT 클리어/복원
- `tools/m25_4b_flash.js` (호스트 드라이버, TCF/TAS 경유):
  - `save`(레지스터 40 + DSPR/PSPR 스크래치 저장 → 불변식),
    `load`(로더 업로드 → PC/SP 설정 → 실행 → magic 확인),
    `erase`/`program`/`read`/`verify`/`restore`, `stop`(ENDINIT 복원 + 상태 복원 + 재개),
    `scan`(섹터 상태), `bp`/`bpclear`

## 3. 라이브 검증 (마지막 논리 섹터 0xA05FC000 — 원래 내용 전부 0x00)

1. backup(16KB) → 2. erase(1섹터) → 3. program(로더 앞 256B 페이로드)
→ 4. **verify: 정확 일치 ✓** → 5. restore(0x00 16KB) → 6. **verify: 일치 ✓**
— **2회 반복 성공**

- 페이로드에 1-비트 포함 → erase 없이는 기록 불가 → **erase 실증** ✓
- 복원 후 대상 섹터 = 원래 값(0x00) ✓, 앱/다른 섹터 무변화 ✓ (앱 정상 실행 확인)
- 로더 기동: "loader ready (magic OK)" ✓

## 4. 알려진 제약 / 다음 단계

- 에이전트의 `-f` 엔트리 BP(M25-4a 파리티 — 앱 `_START`=0x8003A176에서 정지)가
  앱 재부팅 시 코어를 정지시켜 플래셔 운용과 간섭 → **리셋(트리거 클리어) 후 재개**로 해제 ✓
  (운영 플로우는 원본과 동일하게 "플래시 → 리셋 → 실행")
- 다음: ELF 로더블 섹션 자동 추출 + 부분 섹터 read-modify-write + 원본 `-f` 플로우 통합

## 5. 산출물

- `flasher/m25_flash_loader.{c,ld,bin}` + `flasher/m25_flash_loader_build.ps1`
- `tools/m25_4b_flash.js`
- 도구 체인: `C:\Infineon\AURIX-Configuration-Studio-1.0.24\eclipse\tricore-gcc11\bin`
