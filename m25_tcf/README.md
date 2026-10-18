# m25_tcf — 자체 AURIX TCF 에이전트 (M25)

`aurix-debugger.exe`(Infineon)의 "missing 10%"에 해당하는 AURIX 백엔드를
오픈 Eclipse TCF 에이전트 위에 구현한 소스 모음입니다.

**M25-1 완료 (2026-10-18)**: VS Code에서 ShieldBuddy TC275ED 3코어
디버깅(레지스터/메모리/스택프레임/run/pause) 라이브 성공.

## 구성

```
agent/system/aurix/tcf/
  context-aurix.c    네이티브 컨텍스트 백엔드 (원본의 context-aurix.cpp 대응)
  aurix-tas.c/h      TAS 와이어 클라이언트 (mcd_tas에서 이식)
agent/machine/aurix/tcf/
  cpudefs-mdep.c/h   TriCore 레지스터 정의 (EABI DWARF 번호, CSFR 주소, BREAK_INST)
patches/
  cmake-tcf-lib.patch   업스트림 cmake-tcf-lib.txt 수정 (aurix glob + context-win32 제외 + ENABLE_*)
  main-options.patch    업스트림 main.c 수정 (aurix-debugger 호환 옵션 수용)
M25_1_TCF_AGENT_LIVE.md    마일스톤 보고서 (설계 규명·구현·검증 전문)
M25_TCF_AGENT_BLUEPRINT.md 설계도 (갱신본)
```

## 적용 방법 (업스트림 TCF 에이전트 클론 기준)

1. `agent/system/aurix/tcf/`, `agent/machine/aurix/tcf/` 디렉터리를 복사
2. `git apply patches/cmake-tcf-lib.patch` (또는 수동 반영)
3. `git apply patches/main-options.patch`
4. 빌드: `cmake -B build -G "Visual Studio 17 2022" -A x64 -DTCF_MACHINE=aurix`
   → `cmake --build build --config Release`
5. 실행: `tcf-agent.exe -s TCP:127.0.0.1:1534 -p "<디바이스 이름>"`
   (tas_server가 24817에서 실행 중이어야 함)

## 검증 도구

- `tcf_test_client.js` — TCF 프로토콜 스모크 (컨텍스트·레지스터·메모리·suspend)
- `ext_client_test.js` — VS Code 확장 자체 클라이언트(out/tcfclient.js)로 조회
  (위 파일들은 `C:\work\tcf_agent\`에 있음 — 필요 시 이 디렉터리로 이동)
