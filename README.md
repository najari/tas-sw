# tas-sw

TAS(Infineon) 관련 자체 분석·검증 자료 모음 (2026-10-17).

## 문서 (`docs/`)

| 문서 | 내용 |
|---|---|
| [REPORT.md](docs/REPORT.md) | TasServer(백그라운드 실행 파일) 정적·런타임 분석: 파일 신원, 시작 흐름, 배포·자동 기동 구조, 기능 인벤토리, 보안 참고 |
| [TAS_CLIENT_BUILD_AND_E2E.md](docs/TAS_CLIENT_BUILD_AND_E2E.md) | 공식 tas_client_api v1.2.4 소스 빌드(conan + MSVC) 재현 가이드, 산출물, 실기(TC27x) E2E 검증, PyTAS 설치 |
| [TAS_PROTOCOL_CAPTURE.md](docs/TAS_PROTOCOL_CAPTURE.md) | 클라이언트-서버 평문 프로토콜 캡처 전량 해석: PL2/PL1/PL0 계층 포맷, 패킷별 해설, 자체 구현 가이드 |
| [DAS_DLL_ANALYSIS.md](docs/DAS_DLL_ANALYSIS.md) | DAS 클라이언트 DLL(das_api.dll·mcdxdas.dll) 구현 분석: 로더·함수 테이블, 내장 TAS 클라이언트, 서버 자동 기동, MCD↔DAS 연동 구조 |

## 캡처 워크스페이스 (`pkt_capture/`)

로깅 프록시(24900 → 24817) 기반 캡처 도구와 원본 데이터.

- `proxy_capture.py` — 로깅 프록시 (c2s/s2c/timeline 기록)
- `capture_client.py` — 고정 시나리오 테스트 클라이언트 (PyTAS)
- `decode_capture.py` — 캡처 해석기 → `capture_decoded.txt`
- `c2s.bin` / `s2c.bin` / `timeline.log` — 원본 캡처 (384 B / 768 B / 576 B)
- `capture_decoded.txt` / `capture_hexdump.txt` — 해석 결과

재현:

```powershell
python pkt_capture/proxy_capture.py     # 1) 프록시 기동 (별도 창)
python pkt_capture/capture_client.py    # 2) 캡처 클라이언트 실행
python pkt_capture/decode_capture.py    # 3) 해석 리포트 생성
```

## 로그 (`logs/`)

실기 검증 증적:

- `e2e_demo_rw.log` — RW 데모 전문 (TC27x, 전 항목 성공)
- `e2e_pytas_tests.log` — PyTAS unittest 23/23 OK
- `e2e_demo_chl.log`, `e2e_demo_chl_retry.log` — CHL(채널) 타깃 제약 확인
- `server_run_out.log` — 서버 중복 실행 가드 검증

---
검증 환경: Windows / Python 3.13 / MSVC 2022 + conan / TC27x 보드 + DAS JTAG AURDUINO.
정적 분석 원본 덤프(디스어셈블·문자열 등)는 저장소에 포함하지 않았습니다.
