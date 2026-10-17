# mcds — DAS64 네이티브 도구 캡처 워크스페이스 (2026-10-17)

`mcd_basic_client.exe` / `mcds_trace_viewer.exe` ↔ TasServer(24817) 세션을 **포트 하이재킹 프록시**로 전량 캡처한 원본과 해독 산출물.

## 구성

- `tas_hijack_proxy.py` — 캡처 프록시(24817 선점 → 실제 서버 중계, c2s/s2c/timeline 기록)
- `recon_capture.py` — 캡처 정찰(연결별 시간/요약 → `conn_times.json`, `conn_info.json`)
- `dec_native.py` — 전량 해독기(요약·연결목록·상세 리포트 → `native_report/`)
- `probe_conns.py`, `dump2.py`, `dump3.py`, `seg.py`, `cnt.py` — 표적 덤프·검증 스크립트
- `ui.ps1` — UI 스크린샷 자동화
- `tas_capture/` — **원본 캡처(정본)**: `c2s_<conn>.bin`, `s2c_<conn>.bin`(PL2 프레임 그대로), `timeline.log`, `conn_times.json`, `conn_info.json`
- `native_report/` — 해독 리포트(`summary.txt`, `conn_list.txt`, `detail_CONN*.txt`)
- `crop_*.png`, `menu_*.png` — UI 증거 스크린샷
- `*_out.txt` — 표적 덤프 실행 로그

## 주의

- `timeline.log`는 장수명 대용량 연결(예: conn 3341)에서 라인이 중도 누락될 수 있음 — **bin이 정본** (`docs/TAS_PROTOCOL_CAPTURE.md` §8.7).
- 재현: 프록시 기동 후 도구를 `localhost:24817`로 조작 → 도구 종료 후 `dec_native.py` 실행.
- 해설 문서: `docs/TAS_PROTOCOL_CAPTURE.md` §8(프로토콜), `docs/MCD_TOOLS_ANALYSIS.md`(도구 UI/동작).
