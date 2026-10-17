# TAS 서버-클라이언트 평문 프로토콜 캡처 분석 (TAS Protocol Capture)

> 2026-10-17 · 대상: TasServer V2.0 (PID 19760, `C:\Windows\System32\tas_server.exe`) ↔ TasClient(PyTAS v1.2.4)
> 캡처 방식: **로깅 프록시**(TcpProxy) — 관리자 권한/스니퍼 설치 없이 앱 계층에서 양방향 원문 캡처
> 원본 캡처: `pkt_capture\c2s.bin`, `pkt_capture\s2c.bin` (평문 TCP, 24817)
> ※ 2026-10-17 추가: DAS64 네이티브 도구(`mcd_basic_client`·`mcds_trace_viewer`) 실캡처 해독은 **§8**에 수록 (도구별 UI 분석은 `MCD_TOOLS_ANALYSIS.md`)

---

## 0. 요약

- 클라이언트→서버 총 **11개 패킷(384B)**, 서버→클라이언트 총 **11개 패킷(768B)** 을 전량 해석했다.
- 와이어 구조는 3계층: **PL2(4B 길이 프리픽스) → PL1(제어 명령/PL0 프레임) → PL0(워드길이 자기서술 구조체 연속)**.
- 세션 흐름: `SERVER_CONNECT → GET_TARGETS → SESSION_START → DEVICE_CONNECT → PL0 RW 프레임 ×7`.
- 전부 **암호화·서명 없는 평문**이며, 보호라고는 세션명/패스워드(빈 값이면 무시)와 언락 challenge(이번 서버는 `0`) 뿐이다.
- 아래 §4에서 모든 패킷을 바이트 단위로 해설한다. 자체 구현/스니퍼/모의서버 작성 시 §2와 §6만 보면 된다.

---

## 1. 캡처 방법과 재현

스니퍼(pktmon/Npcap 등) 없이, **클라이언트가 접속할 로컬 프록시**를 하나 띄워 24817의 실제 서버로 중계하면서 양방향 스트림을 파일로 기록했다.
(TAS API의 `server_connect(ip, port)`가 임의 포트를 받으므로 가능 — PyTAS 바인딩도 동일.)

```
[CaptureClient(PyTAS)] --24900--> [proxy_capture.py] --24817--> [TasServer]
                                        | 기록: c2s.bin / s2c.bin / timeline.log
```

재현 순서 (`C:\work\tas_server_analysis\pkt_capture`, Python 3.13):

```powershell
# 1) 프록시 기동 (단발성: 첫 연결이 끝나면 종료)
python proxy_capture.py
# 2) 다른 창에서 캡처 클라이언트 실행 (고정 시나리오)
python capture_client.py
# 3) 해석 리포트 생성
python decode_capture.py        # -> capture_decoded.txt
```

- 캡처 스크립트: `proxy_capture.py`(프록시) / `capture_client.py`(시나리오) / `decode_capture.py`(해석기)
- 캡처 시나리오(고정): connect → get_targets → session_start → device_connect(RESET_AND_HALT)
  → read32 ×2 → write32 → read32 → read8 → read(240B) → (오류 케이스) read32 @0xDEADBEEF → 종료
- `timeline.log`: 요청/응답 청크 타임스탬프 — 이번 캡처에서는 **API 호출 1회 = c2s 1청크 + s2c 1청크**로 1:1 대응(§부록 B).
- 프록시는 소켓에 `TCP_NODELAY`를 설정해 패킷 경계가 뭉개지지 않게 했다.

---

## 2. 와이어 포맷 (핵심 요약)

### 2.1 계층

```
[PL2] u32 LE 전체길이(이 4바이트 포함)
  └ [PL1 프레임]  = PL0_START + PL0 항목들 + PL0_END  (연속 배치, 한 PL2에 전부)
        └ [PL0 항목]  각자 (wl+1)*4 바이트 자기서술
```

- 모든 값 **리틀엔디안**, 모든 길이 **4의 배수**.
- `wl` = **해당 구조체 전체 워드수 − 1** (즉 구조체 바이트 크기 = `(wl+1)*4`). `tas_pkt.h` 안의 "Request size: 10" 같은 주석은 낡아서 실제와 다름 — **코드/sizeof 기준**.
- PL2 최대 0x10010(64KB+16). 이번 접속에서 서버가 광고한 값: 최대 요청 **1060B**, 최대 응답 **1040B**.

### 2.2 PL1 제어 명령 (이번 캡처에서 사용된 것)

| cmd | 이름 | 방향 | 크기 | 필드 |
|---|---|---|---|---|
| 0x80 | SERVER_CONNECT | rq | 56B | wl, cmd, reserved(u16), client_name[32], user_name[16], pid(u32) |
| 0x80 | SERVER_CONNECT | rsp | 136B | wl, cmd, reserved, err, reserved1(u32), server_info(120B), challenge(u64) |
| 0x86 | GET_TARGETS | rq | 4B | wl, cmd, start_index, reserved |
| 0x86 | GET_TARGETS | rsp | 12B+88B×N | wl, cmd, reserved, err, num_target, start_index, num_now, reserved1, target_info[N] |
| 0xA1 | SESSION_START | rq | 112B | wl, cmd, con_id, client_type, param8[4], param64(u64), identifier[64], session_name[16], session_pw[16] |
| 0xA1 | SESSION_START | rsp | 132B | ping rsp: wl, cmd, con_id, err, ver_min, ver_max, num_instances, reserved, con_info(124B) |
| 0xA8 | DEVICE_CONNECT | rq | 8B | wl, cmd, con_id, reserved, option(u16), reserved1(u16) |
| 0xA8 | DEVICE_CONNECT | rsp | 12B | wl, cmd, con_id, err, feat_used(u16), reserved(u16), device_type(u32) |
| 0xB0 | PL0_START | rq | 8B | wl=1, cmd, con_id(=0xFF), protoc_ver(=0), pl1_cnt(u16), pl0_addr_map_mask(u16) |
| 0xB0 | PL0_START | rsp | 4B | wl=0, cmd, con_id(=0x81), err |
| 0xB1 | PL0_END | rq | 4B | wl=0, cmd, num_pl0_rw(u16) |
| 0xB1 | PL0_END | rsp | 4B | wl=0, cmd, pl1_cnt(u16 — rq의 값 echo) |

- `server_info`(120B) = server_name[64] + v_minor(u16) + v_major(u16) + supp_protoc_ver(u32) + supp_chl_target(u32) + supp_trc_type(u32) + reserved[4](u32×4) + date[16] + **start_time_us(u64, µs since 1970)**.
- `target_info` = identifier[64] + device_type(u32) + device_id(u32×4) + dev_con_phys(u8) + num_client(u8) + reserved(u16) = **실측 88B** (헤더 주석 "76 bytes"는 오기).
- `con_info`(124B) = max_pl2rq(u32), max_pl2rsp(u32), device_type(u32), device_id(u32×4), ipv4(u32), dev_con_feat(u16), dev_con_phys(u8), pl0_max_num_rw(u8), pl0_rw_mode_mask(u16), pl0_addr_map_mask(u16), msg_length_c2d(u16), msg_length_d2c(u16), msg_num_c2d(u8), msg_num_d2c(u8), reserved(u16), identifier[64], device_id_hash(u32), device_id_hash_str[8].

### 2.3 PL0 항목 (RW 트랜잭션, PL1 프레임 내부)

| cmd | 이름 | wl | 페이로드 |
|---|---|---|---|
| 0x10 | ACCESS_MODE | 0 | acc_mode(u16) — 다음 트랜잭션부터 적용 |
| 0x11 | ADDR_MAP | 0 | addr_map(u8) |
| 0x15 | BASE_ADDR32 | 0 | ba31to16(u16) — 상위 16비트 베이스 (하위 16비트는 각 트랜잭션 a15to0) |
| 0x16 | BASE_ADDR64 | 1 | ba31to16(u16), ba63to32(u32) |
| 0x20/22/24 | WR8/16/32 | 1 | a15to0(u16), data(u32) |
| 0x21/23/25/27 | RD8/16/32/64 | 0 | a15to0(u16) |
| 0x26 | WR64 | 2 | a15to0(u16), data(u64) |
| 0x2A | WRBLK | N | a15to0(u16) + N×u32 데이터 |
| 0x2B | FILL | 3 | a15to0(u16), wlwr(u8), value(u64) |
| 0x2C | RDBLK | 1 | a15to0(u16), wlrd(u8 — 워드수, 0=256) |
| 0x2D | RDBLK1KB | — | 1KB 전용 특수: **응답에서** wl=0이면 4+1024B (예외 규칙) |

응답(PL0): RD rsp = `wl, cmd, wlrd, err` + wlrd×u32 데이터 / WR rsp = `wl=0, cmd, wlwr, err`(데이터 없음) / 오류 시 wlrd=0.

**에러코드(관찰·정의)**: `0x08` PL1 NO_ERROR, `0x80` PL0 NO_ERROR, `0x8D` PL0_DATA(디바이스 접근 실패), `0x98` PL0_CONSEQUENTIAL(선행 실패로 인한 연쇄 오류). PL0 오류는 클라이언트에서 `TAS_ERR_RW_READ/WRITE (0x0600/0x0610)`로 매핑된다.

---

## 3. 세션 전체 시퀀스

```
C→S  SERVER_CONNECT      (60B)  client=PktCaptureDemo, user=admin, pid=13944
S→C  SERVER_CONNECT rsp  (140B) TasServer V2.0 (Apr 30 2026), start_time, challenge=0
C→S  GET_TARGETS         (8B)
S→C  GET_TARGETS rsp     (100B) target[0] = "DAS JTAG AURDUINO 46YPEADW"
C→S  SESSION_START       (116B) type=RW, session="PktSession", pw=""
S→C  SESSION_START rsp   (136B) con_info (max 1060/1040, TC27x, 'LBHJYL', JTAG)
C→S  DEVICE_CONNECT      (12B)  option=0x0003 (RESET_AND_HALT)
S→C  DEVICE_CONNECT rsp  (16B)  feat_used=0x0003
───── 이하 PL0 RW 프레임 (각 24~32B rq / 16~256B rsp) ─────
① read32  @0x70000000    (BASE 0x7000 + RD32 +0)
② read32  @0x70000004    (BASE 0x7000 + RD32 +4)
③ write32 @0x70000000 = 0x12345678
④ read32  @0x70000000    → 0x12345678 확인
⑤ read8   @0x70000003    (ACCESS_MODE(1) + RD8 +3 — §5.4 참고)  → 0x12
⑥ read    @0x70000000 240B (RDBLK wlrd=0x3C)
⑦ read32  @0xDEADBEEF    (비정렬 → RD8/RD16/RD8 분해, 접근 실패 → PL0_ERR_DATA + CONSEQUENTIAL)
종료: 서버 세션 해제 패킷 없음 — TCP FIN 으로 종료
```

---

## 4. 패킷별 상세 해설

아래는 `decode_capture.py`가 생성한 해석 결과(발췌)다. `wl`과 각 필드의 의미는 §2 기준.

### 4.1 접속 (SERVER_CONNECT)

**C→S #00 (60B)** — `3C 00 00 00 | 0D 80 00 00 | "PktCaptureDemo" | "admin" | 78 36 00 00`
- PL2 길이=0x3C(60) / PL1 `wl=0x0D(13), cmd=0x80, reserved=0`
- `client_name`(32B), `user_name`(16B), `client_pid=0x3678=13944`=캡처 클라이언트 PID.

**S→C #00 (140B)**
```
wl=33 cmd=0x80 err=0x08 reserved1=0
server_name='TasServer'  v=2.0
supp_protoc_ver=0x2  supp_chl_target=0x2  supp_trc_type=0x8000000c
date='Apr 30 2026'
start_time_us=1792215577030000  (= 2026-10-17 05:39:37.030 UTC = 14:39:37.030 KST — 서버 프로세스 기동 시각과 일치)
challenge=0x0000000000000000   (서버 언락 불필요)
```
- `start_time_us`는 실제로 **평범한 µs since 1970**이다(라이브 `PyTAS.get_server_info()` 값과 바이트 단위 일치 확인).

### 4.2 타깃 조회 (GET_TARGETS)

**C→S #01 (8B)** — `00 86 00 00` (wl=0, start_index=0)

**S→C #01 (100B)**
```
num_target=1 start_index=0 num_now=1
target[0] identifier='DAS JTAG AURDUINO 46YPEADW'
          device_type=0x401da083 (TC27x)   device_id=0x47427060:0x11080040:0x64945fcb:0x20e65c06
          dev_con_phys=0x01 (JTAG)         num_client=0
```

### 4.3 세션 (SESSION_START)

**C→S #02 (116B)**
```
wl=27 cmd=0xA1 con_id=0 client_type=1 (RW)
param8=00000000  param64=0
identifier='DAS JTAG AURDUINO 46YPEADW'
session_name='PktSession'  session_pw=''
```

**S→C #02 (136B)**
```
wl=32 con_id=0 err=0x08  protoc_ver_min=1 protoc_ver_max=1
con_info: max_pl2rq=1060 max_pl2rsp=1040 device_type=0x401da083
          device_id=0x47427060:0x11080040:0x64945fcb:0x20e65c06
          ipv4=0 dev_con_feat=0x0013 dev_con_phys=0x01 pl0_max_num_rw=32
          pl0_rw_mode_mask=0x0000 pl0_addr_map_mask=0x8001
          msg_length_c2d=0 msg_length_d2c=0 msg_num_c2d=0 msg_num_d2c=0
          identifier='DAS JTAG AURDUINO 46YPEADW'
          device_id_hash=0x4013a2aa device_id_hash_str='LBHJYL'
```

### 4.4 디바이스 접속 (DEVICE_CONNECT)

**C→S #03 (12B)** — `01 A8 00 00 | 03 00 00 00` → option=0x0003 (`TAS_CLNT_DCO_RESET_AND_HALT`)

**S→C #03 (16B)** — `02 A8 81 08 | 03 00 00 00 | 83 A0 1D 40` → `feat_used=0x0003`, `device_type=0x401DA083`

### 4.5 PL0 RW 프레임 (7회)

각 프레임은 **한 PL2 패킷 안에** `[PL0_START … PL0_END]`가 연속으로 들어 있다. 요청 프레임 공통:
`01 B0 FF 00 | C1 FF 01 00` = `wl=1, cmd=0xB0, con_id=0xFF(자리표시자), protoc_ver=0, pl1_cnt=0xFFC1, addr_map_mask=0x0001`
→ `pl1_cnt`는 **0xFFC0에서 시작해 프레임마다 +1** 되는 16비트 카운터(서버가 `PL0_END` 응답에 echo, 클라이언트가 순서 검증).

**① read32 @0x70000000** — `C→S #04 (24B)` / `S→C #04 (20B)`
```
rq: PL0_START(C1) → BASE_ADDR32 ba=0x7000 → RD32 a15to0=0x0000 → PL0_END(num_pl0_rw=1)
rsp: PL0_START(con_id=0x81, err=0x08) → RD32 rsp wlrd=1 err=0x80 data=0x00000000 → PL0_END(pl1_cnt=0xFFC1)
```
**② read32 @0x70000004** — 동일 패턴, `RD32 a15to0=0x0004` / 응답 data=0x00000000

**③ write32 @0x70000000 = 0x12345678** — `C→S #06 (28B)` / `S→C #06 (16B)`
```
rq: … BASE(0x7000) → WR32 a15to0=0x0000 data=0x12345678 (78 56 34 12)
rsp: … WR32 rsp wlwr=1 err=0x80 (데이터 없음)
```
**④ read32 확인** — `data=0x12345678` 반환.

**⑤ read8 @0x70000003** — `C→S #08 (28B)` / `S→C #08 (20B)`
```
rq: … ACCESS_MODE acc_mode=0x0001 → BASE(0x7000) → RD8 a15to0=0x0003
rsp: RD8 rsp wlrd=1 err=0x80 data=0x12345612  (요청 바이트 0x12가 유효; 워드 전체는 디바이스 레인 값)
```
> 여기서만 `ACCESS_MODE(1)`가 추가된다 — §5.4의 초기화자 특성 참고.

**⑥ read 240B @0x70000000** — `C→S #09 (28B)` / `S→C #09 (256B)`
```
rq: … RDBLK a15to0=0x0000 wlrd=0x3C(60워드=240B)
rsp: RDBLK rsp wlrd=60 err=0x80 + 60워드 데이터(첫 워드 0x12345678 …)
```

**⑦ read32 @0xDEADBEEF (실패 케이스)** — `C→S #10 (32B)` / `S→C #10 (24B)`
```
rq: … BASE_ADDR32 ba31to16=0xDEAD → RD8 a15to0=0xBEEF → RD16 a15to0=0xBEF0 → RD8 a15to0=0xBEF2
    (비정렬 4바이트를 RD8+RD16+RD8로 분해)  PL0_END num_pl0_rw=3
rsp: RD8  rsp wlrd=0 err=0x8D (PL0_DATA — 접근 실패)
     RD16 rsp wlrd=0 err=0x98 (PL0_CONSEQUENTIAL)
     RD8  rsp wlrd=0 err=0x98
     PL0_END pl1_cnt=0xFFC7
→ 클라이언트 반환: TAS_ERR_RW_READ
```

---

## 5. 주요 관찰 / 특이사항 (자체 구현 시 필수 지식)

### 5.1 `con_id` 값 규칙
- PL1 제어 요청: `0x00`(session_start, device_connect 등), PL0_START 요청: **`0xFF` 고정**.
  소스 주석: `mPl0Start->con_id = 0xFF; // Overwritten and used on lower layer` (`tas_pkt_handler_rw.cpp:432`).
- 응답에서는 `0x81`이 온다(device_connect rsp, PL0_START rsp). 서버 측 연결 식별자로 보이며 **클라이언트는 검증하지 않는다** — 수신 시 무시해도 됨.

### 5.2 `pl1_cnt` 카운터
- `mPl1CntOutstandingLast = 0xFFC0` 초기화(`tas_pkt_handler_server_con.cpp:37`, "Enforce early overrun in test setups") → 첫 프레임이 `0xFFC1`.
- 매 PL0 프레임마다 +1, `PL0_END` 응답에 echo. 클라이언트는 FIFO 순서 검증에 사용(`tas_pkt_handler_rw.cpp:1106` 부근).

### 5.3 서버 정보 필드
- `start_time_us`는 정상 µs 값. 라이브 검증: 프로세스 기동(14:39:37)과 일치.
- `challenge=0`: 이 서버는 언락(패스워드) 미설정 → `SET_DEVICE_KEY` 불필요.

### 5.4 `read8()`은 `ACCESS_MODE=1`을 보낸다 (v1.2.4 특성)
- `tas_rw_trans_st{addr, num_bytes, acc_mode, addr_map, type, data}` 초기화 시,
  `tas_client_rw_base.cpp:69`의 `read8()`만 3번째 필드에 `TAS_RW_TT_RD(=1)`를 넣는다:
  `{ addr, 1, TAS_RW_TT_RD, addr_map, TAS_RW_TT_RD, value }` → **acc_mode=1**.
- read16/32/64, write 계열은 `0`을 사용하며 캡처와 일치(⑤에서만 ACCESS_MODE 패킷 존재).
- 서버/타깃은 mode 1을 그대로 수용(응답 NO_ERROR). 자체 구현 시 acc_mode=0 사용 무방(대칭성 유지).

### 5.5 비정렬/블록 접근 규칙
- 비정렬 read32 → **RD8+RD16+RD8** 분해(클라이언트가 자동).
- 실패 시 첫 접근만 `PL0_DATA(0x8D)`, 이후는 `PL0_CONSEQUENTIAL(0x98)`, 데이터 미반환(wlrd=0).
- 240B 블록은 **RDBLK(0x2C) 1건**, 1KB 초과 블록은 여러 PL2 패킷/ RDBLK1KB 규칙 사용.

### 5.6 주소 처리
- 32비트 접근은 `BASE_ADDR32(상위 16비트)` + `a15to0(하위 16비트)` 조합. 연속 접근에서 BASE는 매 프레임 재전송(클라이언트는 프레임 최적화만 수행).
- 이 타깃의 지원 주소맵 마스크 `0x8001`(bit0=기본맵, bit15=TAS_AM15). 캡처에선 기본맵(bit0)만 사용.

### 5.7 세션 종료
- 별도의 종료 명령이 없다 — 소켓 close(TCP FIN). 서버는 클라이언트 연결 단위로 세션/점유를 해제한다.

### 5.8 그 외
- 세션 패스워드는 16바이트 평문 필드 — 실제 보호가 필요하면 네트워크 차단이 유일한 수단(와이어에 MAC/암호화 없음).
- 한 `execute()`(예: `execute_trans` 여러 건)는 **여러 PL2 패킷 묶음**으로 나갈 수 있고, 응답도 같은 수만큼 온다(동기 1:1).

---

## 6. 자체 툴 구현 가이드 (최소 동작 세트)

```
1) TCP connect (기본 24817; 서버는 0.0.0.0 리슨)
2) SERVER_CONNECT → server_info(+start_time_us) + challenge 확인
3) GET_TARGETS → identifier 목록
4) SESSION_START(identifier, client_type=1, session_name/pw) → con_info(최대 패킷크기/기능 플래그)
5) DEVICE_CONNECT(option: 0=hot-attach, 1=reset, 3=reset+halt)
6) PL0 프레임: [PL0_START(pl1_cnt++, addr_map_mask)] + [BASE/RD/WR/RDBLK/WRBLK/FILL 항목들] + [PL0_END(num_pl0_rw)]
   → 같은 수의 PL2 응답을 받아 cmd 순으로 매칭(PL0_END의 pl1_cnt로 프레임 정합 검증)
7) close (별도 종료 패킷 없음)
```

수신 파서 골격(의사코드):

```python
hdr = recv_exact(4); size = u32le(hdr); frame = recv_exact(size - 4)
off = 0
while off < len(frame):
    wl  = frame[off]; cmd = frame[off+1]
    n   = 4 + 1024 if (cmd == 0x2D and wl == 0) else (wl + 1) * 4   # RDBLK1KB 예외
    handle(frame[off:off+n]); off += n
```

- 요청은 항상 4바이트 정렬·4의 배수 길이. `wl`/길이 필드는 신뢰 가능(자기서술 구조).
- 프록시 방식(socket 릴레이)은 그대로 재활용 가능 — 자체 툴의 회귀 테스트에 유용(§1 명령 그대로).
- 타임아웃/재동기화: 요청-응답이 동기이므로 응답 미수신 시 소켓 재연결이 가장 단순.

---

## 7. 부록

### A. 파일/해시 (SHA256)

| 파일 | 설명 | SHA256 |
|---|---|---|
| `pkt_capture\c2s.bin` | 클라이언트→서버 원문 384B | `63541E3C94135411EE0B29F8CD84021B41EA05E7EC6148F0FCC15F05C1F85404` |
| `pkt_capture\s2c.bin` | 서버→클라이언트 원문 768B | `D916030D53A34DD594F8C3953DE0D382A1089590E5B2606DF146ABC08952855A` |
| `pkt_capture\timeline.log` | 청크 타임스탬프 | `60889625BDD67A0581544675C3CEAC6052AB3D65F59CA9BFDBCB36CA60C5B4F2` |
| `pkt_capture\capture_decoded.txt` | 해석 리포트 | `AD8A39997AE573E482A120D772D53E991A5BE8AD30BF54A0A824BBAA32F700EE` |

### B. 타임라인 (발췌)

각 API 호출이 c2s 1청크 + s2c 1청크에 1:1 대응됨(설계·검증의 근거):

```
…908.650783 c2s 60    →  .651069 s2c 140   (server_connect)
…908.651282 c2s 8     →  .837913 s2c 100   (get_targets)
…908.838438 c2s 116   →  .846653 s2c 136   (session_start)
…908.847160 c2s 12    →  .876344 s2c 16    (device_connect)
…908.876871 c2s 24    →  .878121 s2c 20    (read32 #1)
…908.878550 c2s 24    →  .879778 s2c 20    (read32 #2)
…908.879997 c2s 28    →  .882749 s2c 16    (write32)
…908.883134 c2s 24    →  .884396 s2c 20    (read32 #3)
…908.884741 c2s 28    →  .886004 s2c 20    (read8)
…908.886334 c2s 28    →  .899592 s2c 256   (read 240B)
…908.900053 c2s 32    →  .901901 s2c 24    (read32 @0xDEADBEEF, 오류)
```

### C. 전체 헥스덤프 (원문 바이트, `capture_hexdump.txt`와 동일)

```
=== c2s.bin (384 bytes) ===
0000  3C 00 00 00 0D 80 00 00 50 6B 74 43 61 70 74 75  <.......PktCaptu
0010  72 65 44 65 6D 6F 00 00 00 00 00 00 00 00 00 00  reDemo..........
0020  00 00 00 00 00 00 00 00 61 64 6D 69 6E 00 00 00  ........admin...
0030  00 00 00 00 00 00 00 00 78 36 00 00 08 00 00 00  ........x6......
0040  00 86 00 00 74 00 00 00 1B A1 00 01 00 00 00 00  ....t...........
0050  00 00 00 00 00 00 00 00 44 41 53 20 4A 54 41 47  ........DAS JTAG
0060  20 41 55 52 44 55 49 4E 4F 20 34 36 59 50 45 41   AURDUINO 46YPEA
0070  44 57 00 00 00 00 00 00 00 00 00 00 00 00 00 00  DW..............
0080  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0090  00 00 00 00 00 00 00 00 50 6B 74 53 65 73 73 69  ........PktSessi
00A0  6F 6E 00 00 00 00 00 00 00 00 00 00 00 00 00 00  on..............
00B0  00 00 00 00 00 00 00 00 0C 00 00 00 01 A8 00 00  ................
00C0  03 00 00 00 18 00 00 00 01 B0 FF 00 C1 FF 01 00  ................
00D0  00 15 00 70 00 25 00 00 00 B1 01 00 18 00 00 00  ...p.%..........
00E0  01 B0 FF 00 C2 FF 01 00 00 15 00 70 00 25 04 00  ...........p.%..
00F0  00 B1 01 00 1C 00 00 00 01 B0 FF 00 C3 FF 01 00  ................
0100  00 15 00 70 01 24 00 00 78 56 34 12 00 B1 01 00  ...p.$..xV4.....
0110  18 00 00 00 01 B0 FF 00 C4 FF 01 00 00 15 00 70  ...............p
0120  00 25 00 00 00 B1 01 00 1C 00 00 00 01 B0 FF 00  .%..............
0130  C5 FF 01 00 00 10 01 00 00 15 00 70 00 21 03 00  ...........p.!..
0140  00 B1 01 00 1C 00 00 00 01 B0 FF 00 C6 FF 01 00  ................
0150  00 15 00 70 01 2C 00 00 3C 00 00 00 00 B1 01 00  ...p.,..<.......
0160  20 00 00 00 01 B0 FF 00 C7 FF 01 00 00 15 AD DE   ...............
0170  00 21 EF BE 00 23 F0 BE 00 21 F2 BE 00 B1 03 00  .!...#...!......

=== s2c.bin (768 bytes) ===
0000  8C 00 00 00 21 80 00 08 00 00 00 00 54 61 73 53  ....!.......TasS
0010  65 72 76 65 72 00 00 00 00 00 00 00 00 00 00 00  erver...........
0020  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0030  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0040  00 00 00 00 00 00 00 00 00 00 00 00 00 00 02 00  ................
0050  02 00 00 00 02 00 00 00 0C 00 00 80 00 00 00 00  ................
0060  00 00 00 00 00 00 00 00 00 00 00 00 41 70 72 20  ............Apr 
0070  33 30 20 32 30 32 36 00 00 00 00 00 70 6D D4 B4  30 2026.....pm..
0080  02 5E 06 00 00 00 00 00 00 00 00 00 64 00 00 00  .^..........d...
0090  17 86 00 08 01 00 01 00 44 41 53 20 4A 54 41 47  ........DAS JTAG
00A0  20 41 55 52 44 55 49 4E 4F 20 34 36 59 50 45 41   AURDUINO 46YPEA
00B0  44 57 00 00 00 00 00 00 00 00 00 00 00 00 00 00  DW..............
00C0  59 00 01 58 8C 90 00 10 44 41 53 20 4A 54 41 47  Y..X....DAS JTAG
00D0  20 41 55 52 44 55 49 4E 83 A0 1D 40 60 70 42 47   AURDUIN...@`pBG
00E0  40 00 08 11 CB 5F 94 64 06 5C E6 20 01 00 00 00  @...._.d.\. ....
00F0  88 00 00 00 20 A1 00 08 01 01 00 00 24 04 00 00  .... .......$...
0100  10 04 00 00 83 A0 1D 40 60 70 42 47 40 00 08 11  .......@`pBG@...
0110  CB 5F 94 64 06 5C E6 20 00 00 00 00 13 00 01 20  ._.d.\. ....... 
0120  00 00 01 80 00 00 00 00 00 00 00 00 44 41 53 20  ............DAS 
0130  4A 54 41 47 20 41 55 52 44 55 49 4E 4F 20 34 36  JTAG AURDUINO 46
0140  59 50 45 41 44 57 00 00 00 00 00 00 00 00 00 00  YPEADW..........
0150  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0160  00 00 00 00 00 00 00 00 00 00 00 00 AA A2 13 40  ...............@
0170  4C 42 48 4A 59 4C 00 00 10 00 00 00 02 A8 81 08  LBHJYL..........
0180  03 00 00 00 83 A0 1D 40 14 00 00 00 00 B0 81 08  .......@........
0190  01 25 01 80 00 00 00 00 00 B1 C1 FF 14 00 00 00  .%..............
01A0  00 B0 81 08 01 25 01 80 00 00 00 00 00 B1 C2 FF  .....%..........
01B0  10 00 00 00 00 B0 81 08 00 24 01 80 00 B1 C3 FF  .........$......
01C0  14 00 00 00 00 B0 81 08 01 25 01 80 78 56 34 12  .........%..xV4.
01D0  00 B1 C4 FF 14 00 00 00 00 B0 81 08 01 21 01 80  .............!..
01E0  12 56 34 12 00 B1 C5 FF 00 01 00 00 00 B0 81 08  .V4.............
01F0  3C 2C 3C 80 78 56 34 12 00 00 00 00 00 00 00 00  <,<.xV4.........
0200  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0210  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0220  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0230  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0240  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0250  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0260  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0270  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0280  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0290  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
02A0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
02B0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
02C0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
02D0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
02E0  00 00 00 00 00 B1 C6 FF 18 00 00 00 00 B0 81 08  ................
02F0  00 21 00 8D 00 23 00 98 00 21 00 98 00 B1 C7 FF  .!...#...!......
```

---

## 8. (2026-10-17 추가) DAS64 네이티브 도구 실캡처 해독 — `mcd_basic_client` / `mcds_trace_viewer`

> 대상: `C:\Program Files\DAS64\clients\mcd_basic_client.exe`(PID 48160), `mcds_trace_viewer.exe`(PID 58980) ↔ TasServer V2.0(PID 19760)
> 캡처: **포트 하이재킹 프록시**(`mcds/tas_hijack_proxy.py`) — 24817을 선점해 실제 서버로 중계, 양방향 원문 기록
> 규모: 연결 **3,662개** · c2s **39,382 msg / 1,333,244 B** · s2c **39,382 msg / 2,120,928 B** · 18:59:24.971 ~ 19:15:02.820
> 산출물: `mcds/tas_capture/`(bin=정본 + timeline.log) · `mcds/native_report/`(전량 해독 리포트) · 도구별 UI 분석은 `MCD_TOOLS_ANALYSIS.md`
> ⚠️ `timeline.log`는 장수명 대용량 연결에서 라인이 중도 누락될 수 있다(§8.7) — **bin이 정본**.

### 8.1 §2·§5 대비 보강/정정 요약

| 항목 | 기존 문서 | 이번 실측(네이티브 캡처) |
|---|---|---|
| `pl1_cnt` | 0xFFC1 시작(소스 근거) | ✅ 연결별 카운터 실측: 3341 9,476프레임 0xFFC1→0x24C4, 4085 19,170프레임 0xFFC1→0x4AA2 — 전 델타 +1, u16 랩 실측(4085 #68 cnt=0x0000, 3341 #400 cnt=0x014B) |
| `con_id`(rsp 2번째 바이트) | 0x81(예시 1회) | **동적 핸들**: 0x84(MCD 3341)/0x85(MTV 4085·TS 3343)/0x95(426)/0x96(429·431) — 서버가 세션별 부여, 단명 세션은 재사용. `SESSION_START` rsp 자체는 0x00 |
| GET_TARGETS rsp | 헤더 12 B + 타깃 88 B | 헤더 8 B `{num_target u16=1, start u16=1}` + 타깃 88 B `{identifier[64], device_type u32, device_id[16], dev_con_phys u8, nclient u8, resv u16}` — `nclient`=SS 성립 동시 세션 수(0~2 관측) |
| RD64 | 미관측 | 요청 wl=0 4 B `[00][27][a15to0]` → 응답 wl=2 12 B(헤더 4 B + u64) |
| ADDR_MAP/mask | 별도 항목 존재만 기술 | **mask 비트 n ↔ ADDR_MAP n** 상관 실측: 0x8000↔맵15, 0x0002↔맵1, 0x0001↔맵0(맵0은 ADDR_MAP 항목 생략) |
| RDBLK 요청 | `{a15to0, wlrd}` | `[01][0x2C][a15to0 u16][wlrd u8][3 B 0]` — wl=1(8 B) 고정 |
| PL0_END(요청) n_rw | 항목 수로 추정 | 동일 구성 프레임에서도 **1/2/3 가변**(4085 m_b 프레임: 1·3이 5,830회씩 동수, m_c #8=2) — 항목 수가 아닌 클라이언트 내부 상태값으로 보임. **서버 응답은 cnt(u16) echo만** — 자체 구현 시 무시 가능 |
| SESSION_START | 오류 예 없음 | 오류 2례 실측: ① `session_name`≠'' → **err=0x38**(빈 이름 재시도 성공 — MTV Target Select 기본값 'admin'이 이 경로) ② `client_type=2`(정보 세션) → **err=0x20**(ctype=1이 정상) |
| DEVICE_CONNECT | opt=3(리셋+정지) 예시 | 네이티브 도구는 **opt=0x0000**(무변경). rsp = `{feat_used u16=0, resv u16, device_type u32}` — 캡처 전 구간 타깃 상태 변경 0건 |
| SET_DEVICE_KEY | challenge=0이면 불필요 | 네이티브 MCD·MTV도 전송함(0xAD, 데이터 u32=1 — challenge=0 서버라 검증 없음) |

### 8.2 con_info 구조체·부속 문자열 블록 (실측)

`SESSION_START` rsp의 con_info 124 B 실측값(도구 Info 창 표시와 동일):

| 필드 | 값 | 필드 | 값 |
|---|---|---|---|
| max_pl2rq | 1060 | max_pl2rsp | 1040 |
| device_type | 0x401DA083 | device_id | `0x47427060 0x11080040 0x64945FCB 0x20E65C06` |
| ipv4 | 0(미사용) | dev_con_feat | 0x0013 (도구 표기: reset/HAR, unlocking) |
| dev_con_phys | 1 (JTAG) | pl0_max_num_rw | 32 |
| rw_mode_mask | 0x0000 | addr_map_mask | **0x8001**(맵0\|맵15) |
| msg_length c2d/d2c | 0 / 0 | ident/hash | `DAS JTAG AURDUINO 46YPEADW` / 0x4013A2AA(`LBHJYL`) |

문자열 블록 3종(맵15 RDBLK — Core Info·코어 목록의 원천):

| 주소 | 크기 | 내용(원문) |
|---|---|---|
| @0xD000 | 104 B | `{u32 device_type=0x401DA083, u32 0, char[32] "TriCore-Family", u32 2, 60 B 0}` |
| @0x0000 | 96 B | `{"UDAS JDS"[32], "Infineon"[32], "01 01"+6 B 0, "Apr 30 2026"[24]}` — "UDAS JDS"는 Core Select의 `Server Name: UDAS`와 일치 |
| @0x8000 | 168 B | `{"DAS JTAG AURDUINO"[64], "Infineon"[32], "46YPEADW"[32], 8 B 0, u32 0x80000001 ×2, 24 B 0}` |

### 8.3 MCD Basic Client 해독

#### 8.3.1 열거(churn) 단계 — 18:59:25~19:03:07

- 연결 2,918개(초당 ~13개), 대부분 60~380 B 단명 질의. 아키타입: 60 B(SVR만) / 68 B(+GT) / 220 B(+SS+PL0(@0+@8000)+GT) / 248 B(SS+PL0×2) /
  **380 B 풀 프로브**(SVR+GT+SS+PL0(@0/@8000)+PING+PL0(@D000)+PL0(@C00B0000, 실패)+RD32@0x000C0100=0x61A80(400000)+RD32@0xC0020000=0x64(100)+PL0 재읽기 — conn 426).
- `0xC00B0000` 읽기 실패(PL0_DATA)는 정상적 능력 탐색. 0x000C0100/0xC0020000은 맵15 내부 값(의미 미상 — capability 플래그로 추정: 400000/100).

#### 8.3.2 선택 후 세션 (conn 3341, 19:03:11.19~)

시퀀스(#0~#8은 §3과 동일 구조 + 네이티브 특이점):

| # | 방향 | 요약 |
|---|---|---|
| 0 | c2s | SVR name=`MCD API (legacy devices)` user=admin pid=48160 (60 B) |
| 1 | s2c | GT rsp: 타깃 1개, nclient=0 |
| 2 | c2s/s2c | SS(ctype=1, sname='') → OK. rsp ci 124 B(§8.2) — 핸들 표시는 0 |
| 3 | c2s/s2c | PING(0xA2) → rsp에 **핸들 0x84** 표시(이후 PL0 rsp 공통) |
| 4 | c2s/s2c | RDBLK @0xD000(wlrd=26) → `83 a0 1d 40 00 00 00 00 "TriCore-Family"…` |
| 5,6 | c2s/s2c | RDBLK @0x0000(wlrd=24)+@0x8000(wlrd=42) **동일 프레임 2회**(284 B rsp: `"UDAS JDS"…`, `"DAS JTAG AURDUINO"…`) |
| 7 | c2s/s2c | SET_DEVICE_KEY(0xAD) `01ad000001000000` → ack |
| 8 | c2s/s2c | DEVICE_CONNECT `01a8000000000000`(opt=0) → rsp `02a884080000000083a01d40` |
| 9 | c2s/s2c | 문자열 블록 3번째 재읽기(같은 284 B) |
| 10~14 | c2s/s2c | 초기화 블록: RD32 0xF0000480=**0x00080001**, RD32 0xF0000490=0; WR32 0xF0000498←**0x2**, 0xF0000494←**0x8000003F**, 0xF0000420←**0x00000102**(원문 예: `01b0ff00c7ff0100001500f0012498040200000000b10100`) |

#### 8.3.3 상시 폴링 — 3프레임 사이클 (~225 ms, 13.3 msg/s)

| 순서 | c2s 원문 | 해석 | rsp 값 |
|---|---|---|---|
| m1 | `01b0ff00caff008000110f0000150c00002500820025008200b10200` | 맵15: B32 0x000C + RD32 @0x8200 ×2 | 0x400, 0x400 |
| m2 | `01b0ff00cbff008000110f0000150a0002260080008000000080000000b10100` | 맵15: B32 0x000A + WR64 @0x8000 ← 0x0000800000008000 | ack |
| m3 | `01b0ff00ccff0100001581f8002508fe002500fd00b10200`(최초 회차) | 맵0: B32 0xF881 + RD32 @0xFE08 (+최초1회 @0xFD00) | `0xA0000020`(PC), 0x00001013 |
| m3′ | `01b0ff00cdff0100001581f8002508fe00b10100` | 맵0: B32 0xF881 + RD32 @0xFE08 | `0xA0000020` — **UI Instruction Pointer와 일치** |

- 최초 회차만 4프레임(m1, m2, m3+@FD00, m3′), 이후 3프레임 반복(총 ~3,157회). 0x8200 동일주소 2회 읽기는 클라이언트 구현 특성.
- 0xF881 베이스(맵0, +0xFE08=PC)와 맵15의 0x000A/0x000C 레지스터는 **디버그/내부 영역으로 추정(명칭 미확정)**.
- 폴링 공백은 정확히 1곳: 8.3.4의 Read 실패 모달 동안(19:03:39.288~19:04:05.156, 약 26초)뿐.

#### 8.3.4 Read 실패 재현 (19:03:38~19:04:05)

- UI 값(Address=0, Space=0→맵0, Bytes=4)으로 `Read` 실행 → 다음 폴링 프레임에 항목 1개 추가:
  c2s `01b0ff004b0101000025000000b10100`(cnt=0x014B — 이때 pl1_cnt가 랩한 상태) → rsp `00b084080025008d00b14b01` — **err=0x8D(PL0_DATA)**.
- 모달 `Read failure for a transaction of the txlist` 표시 + 폴링 26초 정지(타임라인 공백으로 관측) 후 자동 재개.
- 해석: 실기(TC27x) 맵0 주소 0 접근 불가. 도구는 오류 표시 후 계속 동작.

### 8.4 MCDS Trace Viewer 해독

#### 8.4.1 Target Select 열거 (19:07:44.7~19:11:37.3)

- conn 3342: **GET_TARGETS ×1,110**(~4.8/s) — 목록 갱신.
- 소형 세션(~740개, 0.6초 주기): SVR(name=`Target Select`)+SS+DC 3메시지 — 타깃 상세 문의, 핸들 재사용(0x85 등).

#### 8.4.2 세션 수립 — `Session Name: admin` 거부 → 재시도 (conn 4085, 19:11:37.36~)

- c2s #2: SS(sname=`admin`) → s2c `20a10038…` **err=0x38, ci 전부 0** → c2s #3: SS(sname='') → OK → #4: DC(opt=0).
- 도구 UI(Target Select)의 Session Name 기본값이 `admin` — 서버가 명명 세션을 거부하자 **빈 이름으로 자동 재시도**해 성공.
- 이어서 능력 프로브: 맵1 B32 0xF00E + RD32 @0x8000 → `00b085080025008d…` PL0_DATA(오류 무시).

#### 8.4.3 트레이스 레지스터 폴링

| 계열 | c2s 원문 | 관측값 | 횟수 |
|---|---|---|---|
| m_a | `01b0ff00c2ff0100001503f000251c6000b10100` | `0x0002020F` 고정 | 583 |
| m_b | `01b0ff00c3ff008000110f00001500500027900000b10100` | 첫 `0x3CCC66A52` → 끝 `0x3D90410A3` (µs급, 64비트) | 11,660 (END 1/3 각 5,830) |
| m_c | `01b0ff00c4ff0100001500f00025100000b10200` | 첫 `0x9C0215B5` → 32비트 랩 1회 → 끝 `0x05AF23DD`(≈8.0×10⁶/s) | 5,830 |
| m_d | `01b0ff0000000100001503f00025346000b10100` / `…0100…0025406000b10100` | `0x2211` / `0x2` | 548 ×2 |

- 속도: 19:11:37~19:12:44는 10초 버킷 132~141(~14/s) → **19:12:46경(빈도 안내 닫힘)부터 1,100~1,141/10초(≥110/s)** 유지.
- m_b 증가율 ≈1.08~1.12×10⁶/s(장기 구간), m_c ≈8.0×10⁶/s — 도구의 "MCDS 50 MHz/19.8 ns, CPU 101 MHz" 표기와 별개 소스로 보임(기준 클록 미확정).
- 기록: Alt+R(19:13:37.892) → "No trace data" — **CPU가 Debug(정지, PC 0xA0000020)라 트레이스 이벤트 없음**(연결 문제 아님).

### 8.5 스터티(GET_CLIENTS)·기타 명령

- Info 버튼 흐름: conn 3437 — SS(**ctype=2**)+GET_CLIENTS+SS → SS rsp err=**0x20**(PL1_CMD_FAILED) →
  conn 3438 — SS(ctype=1)+**GET_CLIENTS**(요청 `[10][0x87][start u16=0][identifier 64 B]`) → 성공.
- GET_CLIENTS rsp: 8 B 헤더(n=2)+8 B 0 + **레코드 88 B ×2**:
  `{u64 ts, name[24], user[16], 8 B 0, u32 pid, u32 flags=1, u64 ts2, u64 bytes_sent, u64 bytes_recv}`

| name | pid | sent / recv (B) | 비고 |
|---|---|---|---|
| `MCD API (legacy devices)` | 48160 | 117,556 / 82,920 | 지속 폴링 중 — 도구 창 표시(83,048)와 소폭 차 |
| `con_info` | 58980 | 248 / **276** | 레코드값 = 실제 연결 바이트와 일치 |

- 도구 창 표기: `Session was started without a session name`, `15814.231 s after server start`, `Client connected 303.033 s after session start` — 실시각과 일치(서버 기동 14:39:37 → MCD 세션 19:03:11 = 15,814 s).
- SET_DEVICE_KEY(0xAD)는 네이티브 도구도 전송(데이터 u32=1).

### 8.6 UI 동작 ↔ 패킷 상관 타임라인 (2026-10-17)

| 시각 | 동작(사용자) | 서버 트래픽 |
|---|---|---|
| 18:59:24.971~ | MCD Core Select 열거 | 질의 churn(연결 2,918개) |
| 19:02:36.2 | Change 클릭(재열거) | churn 지속 |
| 19:03:07.849 | Core 0 Select 클릭 | 열거 종료 |
| 19:03:11.19 | — | conn 3341 세션 수립 + 초기화 |
| 19:03:38.034 | Read 클릭 | 다음 폴링에 RD32@0x0000 추가 |
| 19:03:39.288 | 오류 모달 표시 | err=0x8D, 폴링 26초 정지 |
| 19:04:06.6 | Core Info 클릭 | 추가 트래픽 없음(캐시) |
| 19:07:43.5 | Alt+D(Target Select) | conn 3342 GT 폴링 시작 |
| 19:08:12.9 | Info 클릭 | conn 3437(ctype=2 실패)+3438(GET_CLIENTS) |
| 19:11:36.369 | MTV Select 클릭 | conn 4085 세션(SS 'admin' 거부→재시도) |
| 19:12:46경 | 빈도 안내 닫기 | 폴링 ≥110 msg/s로 전환 |
| 19:13:37.892 | Alt+R(기록 시작) | 트레이스 폴링 지속, 이후 "No trace data" |
| 19:15:02.82 | (프록시 정지) | 캡처 종료 — MCD 연결 단절(앱 종료) |

### 8.7 ⚠️ 타임라인 로그 절단 함정 (필독)

- conn 3341의 timeline 라인은 **19:07:44.530(≈3,873줄)에서 중단**되지만, 동일 연결 bin에는 9,482메시지가 전량 기록되어 있고
  pl1_cnt 연속성(0x24C4까지)으로 **프록시 정지(19:15:02)까지 ~13.3 msg/s로 계속 폴링**했음이 증명된다.
- 즉 "MCD가 19:07:44에 폴링을 멈췄다"는 해석은 금물 — 프록시 로깅 측 누락(절단 아티팩트)이다.
- `conn_times.json` 등 로그 기반 파생 데이터도 이 구간에서 잘못될 수 있음 → **분석은 항상 bin 기준** (3342·4085의 로그는 정상).

### 8.8 재현·파일

- 스크립트(`mcds/`): `tas_hijack_proxy.py`(캡처), `recon_capture.py`(정찰), `dec_native.py`(전량 해독→`native_report/`),
  `probe_conns.py`·`dump2.py`·`dump3.py`·`seg.py`(표적 덤프), `ui.ps1`(스크린샷).
- 실행 순서: 하이재킹 프록시 기동 → DAS 도구를 localhost:24817로 조작 → 도구 종료/정지 후 해독.
- 캡처 종료 시점 상태: 프록시 정지 · tas_server(19760) 실행 중 · mcds_trace_viewer(58980) 실행 중(미접속) · mcd_basic_client 종료.
