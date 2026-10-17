# TAS 클라이언트 빌드 & 실기 E2E 검증 리포트 (tas_client_api v1.2.4)

- 일시: 2026-10-17 (KST)
- 목표: Infineon 공식 소스 기반 TAS 클라이언트 빌드 (기능 동일 / 자체 툴·자동화 연동)
- 결론: **빌드 성공. C++/Python(RW 계열) 모두 실기 TC275에서 E2E 검증 완료.**
- 소스: `Infineon/tas_client_api` v1.2.4 (commit `1adca89`, Apache-2.0) → `C:\work\tas_client_api`

---

## 1. 빌드 환경

| 항목 | 값 |
|---|---|
| OS | Windows x64 (10.0.26200) |
| 컴파일러 | MSVC 19.44.35229 (VS2022 Community 17.14, v143), Windows SDK 10.0.26100 |
| conan | 2.33.0 (pip 설치) |
| cmake | 4.4.4 (pip 설치) |
| Python | 3.13.15 (요구: ≥3.11) |
| conan 의존성 | pybind11/2.13.5 (conancenter) |
| PyTAS venv | `setuptools, build, wheel, numpy, pybind11-stubgen, twine, toml` |

## 2. 빌드 절차 (재현용)

```powershell
cd C:\work\tas_client_api

# 최초 1회: conan 프로필 생성 후 compiler.cppstd=17 로 수정
conan profile detect
#  → %USERPROFILE%\.conan2\profiles\default 에서 compiler.cppstd=17 (msvc 194, Release, x86_64)

# 의존성 설치 (+PyTAS용 pybind11).  C++만 필요하면  -o python=True  생략
conan install . -o python=True

# 구성 및 빌드
cmake --preset conan-default -A x64 -T v143
cmake --build --preset conan-release

# (선택) 통합용 설치 트리 생성 — find_package() 지원
cmake --install build --config Release --prefix C:\work\tas_client_api\_install
```

참고:
- conan이 `-o python=True`에 대해 "&:python=True 사용 권장" 경고를 내지만 동작에는 문제 없음.
- 인터넷 필요(conancenter + PyPI). 빌드 전체 소요 수 분.

## 3. 산출물

| 파일 | 크기 | SHA256 |
|---|---:|---|
| `build\src\tas_client\Release\tas_client.lib` | 492,132 B | `7DA350B0F05CFA439C2AE7524355F8BCEF7473C44C57A4BDF0F1052A06F697BF` |
| `build\src\tas_socket\Release\tas_socket.lib` | 100,512 B | `320BCFC325EE431A807B18672BBD24F4682763DF3765A284C3730580B7DCE4BB` |
| `build\apps\tas_rw_api_demo\Release\tas_rw_api_demo.exe` | 54,272 B | — |
| `build\apps\tas_chl_api_demo\Release\tas_chl_api_demo.exe` | 39,424 B | — |
| `build\python\dist\pytas-1.2.4-cp313-cp313-win_amd64.whl` | 156,287 B | `9900B433D14C7C38C4E4A6C2659CEC598CCC08B1385807F5385BDB26A839894A` |

설치 트리(`_install\`): `bin\`(데모 2종), `lib\`(정적 라이브러리 2종 + CMake 패키지), `include\tas_client\`, `include\tas_socket\`

## 4. 자체 툴에 통합하는 방법

### C++ (CMake) — 권장
```cmake
find_package(TasClientApi REQUIRED PATHS "C:/work/tas_client_api/_install")
target_link_libraries(my_tool PRIVATE TasClientApi::tas_client)
# TasClientApi::tas_client → TasClientApi::tas_socket 자동 링크
```
- 요구사항: **C++17, x64, MSVC v143 호환, 동적 CRT(/MD)**
- 공개 헤더: `#include <tas_client/tas_client_rw.h>` / `tas_client_chl.h` / `tas_client.h` / `tas_utils.h` / `tas_device_family.h`
- 사용법은 `apps\tas_rw_api_demo\tas_rw_api_demo_main.cpp`가 사실상 교과서:
  connect → `get_targets()` → `session_start(identifier)` → `device_connect(TAS_CLNT_DCO_RESET_AND_HALT)` → read/write → (소멸자가 세션 종료)

### Python (PyTAS)
```powershell
pip install C:\work\tas_client_api\build\python\dist\pytas-1.2.4-cp313-cp313-win_amd64.whl
```
```python
from PyTAS.PyTAS import TasClientRw, TasReturn
c = TasClientRw("MyTool")
assert c.server_connect() == TasReturn.TAS_ERR_NONE  # localhost:24817 (서버 없으면 라이브러리가 자동 기동)
targets, _ = c.get_targets()
c.session_start(targets[0].identifier)
value, ret = c.read32(0x70000000)
```
- 이 휠은 Python 3.13(cp313, win_amd64) 전용. 다른 버전의 파이썬이면 해당 버전으로 재빌드.

## 5. 실기 E2E 결과 (KIT_AURIX_TC275, 2026-10-17)

서버: `localhost:24817` — 자동 기동 인스턴스 (`TasServer V2.0 (Apr 30 2026)`, 시작 14:39:37)
타깃: `TC27x LBHJYL (DAS JTAG AURDUINO 46YPEADW)`, `dev_con_phys = JTAG`

| 검증 | 결과 | 로그 |
|---|---|---|
| `tas_rw_api_demo.exe` 실행 | ✅ 전 항목 성공 | `e2e_demo_rw.log` |
| 8bit R/W (0x70000000/1) | ✅ write→readback 일치 (00→AB, 00→CD) | 〃 |
| 32bit R/W 정렬/비정렬 | ✅ 0xABCDEF09 리드백, 비정렬도 정상 | 〃 |
| fill32 (128 B = 0x1234ABCD) | ✅ 32워드 전부 일치 | 〃 |
| 트랜잭션 리스트 (RD/WR/RD) | ✅ FFABCDEF 리드백 | 〃 |
| 대량 패킷 읽기 (64 KB) | ✅ 정상 | 〃 |
| PyTAS unittest (RW 15 + Server 8) | ✅ **23/23 OK** (5.3초) | `e2e_pytas_tests.log` |
| CHL 데모 (채널 통신) | ⚠️ 세션 시작 실패(원인 아래) — 클라이언트 결함 아님 | `e2e_demo_chl.log`, `e2e_demo_chl_retry.log` |

### CHL 데모가 실패한 이유 (문서화된 제약)
- `tas_client_chl.h`: "A session can only be started when a channel description is available in the TasServer.
  The channel description is read from a device by the first ClientChl.session_start() call."
- 즉 CHL 세션은 **디바이스(타깃)에서 채널 디스크립터를 읽어야** 성립한다. 현재 TC275 타깃에는
  채널 디스크립터(타깃 측 채널 지원)가 없어 `ERROR: Could not read channel description from device` 발생.
- 디바이스를 reset-and-run 상태로 만든 뒤 재시도해도 동일 → 타깃 측 지원 여부 문제로 확인.
- RW 계열(주 사용 목적: 메모리 접근)은 전부 정상 동작.

## 6. 참고/유용 정보

- 서버 자동 기동은 클라이언트 라이브러리에 내장: `OpenMutexW("Global\only_one_TasServer_on_host")` 확인 →
  없으면 `start_tas_server.bat` ShellExecute (상세: `REPORT.md` §4).
- 쓰기 검증은 모두 RAM 영역(0x70000000 = DSPR, 0x70100000 = DSPR core1) — 플래시 미사용, 안전.
- 서버 접속은 평문 TCP. 원격/다중 툴 사용 시 방화벽(24817) 정책만 유의.
- 추가 데모(참고): `tas_basic_client_rw/chl.exe`(설치본, GUI) — 동일 API의 GUI 샘플.

## 7. 후속 결과 — PyTAS 메인 Python 설치 & 프로토콜 캡처 (2026-10-17)

### 7.1 PyTAS를 메인 Python에 설치
- 산출 휠: `build\python\dist\pytas-1.2.4-cp313-cp313-win_amd64.whl` (cp313 전용, Python 3.11+ 필요)
- 설치: `py -3.13 -m pip install build\python\dist\pytas-1.2.4-cp313-cp313-win_amd64.whl`
  → `pytas 1.2.4` + 런타임 의존성 `numpy(2.x)` 설치됨
- 실기 스모크 테스트(메인 Python 3.13): `server_connect` → `get_targets` → `session_start` → `read32` 전부 정상

  ```python
  from PyTAS.PyTAS import TasClientRw
  c = TasClientRw("MyTool")
  c.server_connect("127.0.0.1", 24817)   # ip, port (위치 인자)
  ```

- 참고: `get_challenge`(서버 언락 challenge 반환)는 C++ API에만 있고 PyTAS 바인딩에는 미노출.

### 7.2 클라이언트-서버 평문 프로토콜 캡처
- 로깅 프록시(`pkt_capture\proxy_capture.py`, 24900 → 24817)로 전 세션을 캡처 후 100% 해석.
- 결과 문서: **`TAS_PROTOCOL_CAPTURE.md`** — 계층(PL2/PL1/PL0) 포맷, 패킷별 해설, 자체 구현 가이드.
- 원본 데이터/스크립트: `pkt_capture\` (`c2s.bin`/`s2c.bin`/`timeline.log` + `capture_client.py`/`decode_capture.py`).
