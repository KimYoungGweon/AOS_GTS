# 콘솔 → 서버 → GFC ID=1 제어 경로

- 작성일: 2026-09-21
- 목표: **콘솔 P1 에서 GFC ID=1 을 고르고 P2 에서 조작하면 실제 GFC 장비가 움직인다**
- 관련 규격: `GTS_UDP_Protocol.md` (콘솔↔서버), `GTS_GFC_UDP.md` (서버↔GFC), 5절 (GFC↔STM32)

---

## 0. 한 장 요약

```
 ESP32-P4 콘솔            Ubuntu 서버 192.168.0.6           ESP32 GFC #1        STM32
┌───────────┐  UDP 5502  ┌──────────────────────┐  UDP 5501 ┌──────────┐ UART2 ┌───────┐
│  P1/P2 화면 │──────────▶│  udp_server.py        │──────────▶│gts_gfc_udp│──────▶│ Pump  │
│            │◀──────────│  ├ 콘솔 프레임 파서     │◀──────────│           │◀──────│ TVOC  │
└───────────┘  0x01~0xA3 │  ├ 명령 변환           │ 0x30~0x3A └──────────┘115200 └───────┘
                         │  └ GFC 프레임 빌더     │           0x70/0x71/0x72
                         └──────────────────────┘
```

프로토콜이 두 가지인 게 핵심이다. 콘솔 쪽은 `seq` 가 DATA 안(헤더 6byte),
GFC 쪽은 `seq` 가 헤더 안(헤더 8byte)이다. **변환은 전부 서버가 한다.**

### 명령 변환표

| 콘솔 (5502) | → | GFC (5501) | → | STM32 (UART2) |
|---|---|---|---|---|
| `0x20 GFC_SET_MODE` (u8 mode) | | MANUAL 이면 `0x34 SRC_SET(enable=0)` | | `0x70` |
| `0x21 GFC_SET_PUMP` (u8 on) | | `0x30 PUMP_SET(on,on,on)` | | `0x70 PUMP_VALVE_SET` |
| `0x22 GFC_SET_TIMES` (u16,u16 ds) | | 저장 · 동작 중이면 `0x34` 재전송 | | — |
| `0x23 GFC_AUTO_RUN` (u8 run) | | `0x34 SRC_SET(enable, init=start_ds/10, cycle=cycle_ds/10, period=600)` | | `0x70` (시퀀스가 구동) |
| `0x01 CONNECT` | | (서버가 GFC 레지스트리 조회) | | — |
| `0x03 PING` | | — | | — |

역방향: GFC `0x32 SENSOR_DATA` (1초) → 서버 → 콘솔 `0xA3 GFC_AUTO_STATE` (500 ms)

---

## 1. 이번 세션에서 바뀐 것

### 1-1. 콘솔 `GTS_CONSOLE_ESP32P4`

| 파일 | 변경 |
|---|---|
| `main/gts_config.h` | `GTS_OFFLINE_MODE` **1 → 0**. 서버 IP 는 `218.147.152.41:5502` 유지. 백업 `gts_config.h.bak` |

그 외 콘솔 코드는 손대지 않았다. `gts_protocol.c` 의 `handle_frame()` 이 이미
`CONNECT_ACK(0x81)` / `PONG(0x83)` / `GFC_AUTO_STATE(0xA3)` 를 처리한다.

**OFFLINE_MODE 를 끄면 달라지는 것**

- `CONNECT_ACK` 을 실제로 받아야 `ENTER CONTROL` 이 열린다 (서버·GFC 가 떠 있어야 함)
- PING 무응답 3회 연속 → `NO REPLY` + P1 자동 복귀가 살아난다
- 상단바의 `OFFLINE` 배지가 사라지고 RTT 가 실제 값으로 뜬다
- `gts_sim.c` 의 자체 카운트다운이 꺼진다 — P2 의 남은 시간은 전적으로 서버 push 를 따른다

### 1-2. GFC `gts_gfc_udp` — 사실상 새로 씀

기존 184줄짜리 텍스트 송신 스켈레톤(`"GFC01,STATUS=OK,COUNT=%d"`)을 걷어내고
규격대로 구현했다. 원본은 `main/gts_gfc_udp.c.bak` 에 남겨 뒀다.

| 파일 | 역할 |
|---|---|
| `main/gfc_config.h` | Wi-Fi / 서버 주소 / DTYPE·DID / 핀 / 타이밍 |
| `main/gfc_proto.[ch]` | UDP 바이너리 프레임 조립·해석 (순수 함수, 호스트 테스트 가능) |
| `main/gfc_uart.[ch]` | STM32 UART2 게이트웨이 + 자동송신 감지/폴링 폴백 (FR-2) |
| `main/gfc_ctrl.[ch]` | 주입 시퀀스 + LED 100 ms 패턴 타이머 |
| `main/gts_gfc_udp.c` | Wi-Fi, 소켓, 명령 디스패치, 주기 상향 |
| `main/CMakeLists.txt` | 소스 4개 등록 |

구현한 UDP 명령: `0x01 HELLO` `0x02 ACK` `0x04 EVENT` `0x05 PING` `0x06 DISCOVER`
`0x30 PUMP_SET` `0x31 PUMP_QUERY` `0x32 SENSOR_DATA` `0x34 SRC_SET` `0x39 CFG_QUERY` `0x3A SYS`

**확인해야 할 값 (gfc_config.h)**

```c
#define GFC_DID                 1        // 콘솔 P1 에서 고르는 ID 와 같아야 한다
#define GFC_SERVER_IP           "192.168.0.6"
#define GFC_UART_TX_PIN         17       // ★ U3 미확정 — 점퍼 실물 배선 확인
#define GFC_UART_RX_PIN         16       // ★
#define GFC_STM32_ENABLE        1        // 0 이면 UART 출력 없이 경로만 검증
```

### 1-3. 서버 `server/` (새 폴더 — SSH 로 옮길 것)

| 파일 | 역할 |
|---|---|
| `udp_server.py` | 5500 AOS(로그) · 5501 GFC(바이너리) · 5502 Console(바이너리) |
| `test_packets.py` | 프레임·변환 단위 테스트 (서버 고치면 먼저 돌릴 것) |
| `gfc_sim.py` | GFC ID=1 흉내 — ESP32 없이 서버 검증 |
| `console_sim.py` | 콘솔 흉내 — P4 보드 없이 서버 검증 |

5500(AOS)은 아직 규격이 없으므로 기존처럼 로그만 남긴다.

---

## 2. 서버 반영 절차 (Mac → 서버)

**순서가 중요하다. 백업을 먼저 뜨고 나서 scp 한다** — scp 가 기존 `udp_server.py` 를 덮어쓴다.

### 2-1. 먼저 서버에서 백업 + 서비스 정지

```bash
ssh gts@192.168.0.6
```

```bash
cd ~/gts_udp_server
cp udp_server.py udp_server_20260921.py     # 날짜 백업
ls -l                                        # udp_server.py, venv/, 백업 확인
sudo systemctl stop gts-udp                  # 포트 충돌 방지
exit
```

### 2-2. Mac 에서 scp

Mac 터미널을 새로 연다. iCloud 경로에 **공백이 있으므로 반드시 따옴표로 감싼다.**
매번 치기 번거로우니 변수에 넣어 둔다.

```bash
SRC=~/Library/Mobile\ Documents/com~apple~CloudDocs/Work/AOS_GTS/server

ls "$SRC"          # console_sim.py  gfc_sim.py  test_packets.py  udp_server.py
```

4개를 한 번에 올린다.

```bash
scp "$SRC"/*.py gts@192.168.0.6:/home/gts/gts_udp_server/
```

`gts` 비밀번호를 한 번 입력하면 끝이다. 정상이면 이렇게 나온다.

```text
console_sim.py                          100% 3681    1.2MB/s   00:00
gfc_sim.py                              100% 5146    1.8MB/s   00:00
test_packets.py                         100%   10KB   3.1MB/s   00:00
udp_server.py                           100%   29KB   5.4MB/s   00:00
```

> **`~` 를 쓰지 말 것** — 받는 쪽 경로에 `~` 를 쓰면 셸에 따라 `~` 라는 이름의
> 폴더가 새로 생긴다. `/home/gts/gts_udp_server/` 처럼 절대경로로 적는다.
>
> 서버 쪽 파일 하나만 다시 올릴 때:
> ```bash
> scp "$SRC/udp_server.py" gts@192.168.0.6:/home/gts/gts_udp_server/
> ```
>
> 반대로 서버에서 Mac 으로 가져올 때 (서버에서 직접 고친 걸 회수):
> ```bash
> scp gts@192.168.0.6:/home/gts/gts_udp_server/udp_server.py "$SRC/"
> ```

### 2-3. 서버에서 확인

```bash
ssh gts@192.168.0.6
cd ~/gts_udp_server
ls -l                                # 4개 파일 + venv/ + 백업
source venv/bin/activate             # 프롬프트에 (venv) 가 붙는다

python3 test_packets.py              # ★ 먼저 이것부터
```

마지막 줄이 `✅ 전부 통과` 여야 한다. 하나라도 FAIL 이면 서버를 띄우지 말고
그 항목부터 볼 것 (파일이 깨져서 올라갔거나 Python 버전 문제).

```bash
python udp_server.py                 # 수동 실행
```

```text
========================================
       GTS UDP SERVER  (rev.2 binary)
========================================
5500 : AOS Sensor   (raw log)
5501 : GFC          (binary 0x30~0x3A)
5502 : Console      (binary 0x01~0xC3)
========================================

[...] [SERVER] AOS UDP server started on port 5500
[...] [SERVER] GFC UDP server started on port 5501
[...] [SERVER] CONSOLE UDP server started on port 5502
```

여기서 3절 STEP 1 의 시뮬레이터 테스트를 한다. `Ctrl + C` 로 종료.

### 2-4. 서비스로 되돌리기

```bash
deactivate
sudo systemctl start gts-udp
systemctl status gts-udp             # active (running)
journalctl -u gts-udp -f             # 실시간 로그, Ctrl+C 로 빠져나옴
```

`gts-udp.service` 는 고칠 필요 없다 — `ExecStart` 가 이미
`venv/bin/python -u udp_server.py` 를 가리킨다.

### 2-5. 되돌리기 (문제가 생겼을 때)

```bash
cd ~/gts_udp_server
cp udp_server_20260921.py udp_server.py
sudo systemctl restart gts-udp
```

---

## 3. 단계별 테스트 순서

하드웨어를 한꺼번에 붙이지 말고 아래 순서로 좁혀 나간다.

### STEP 1 — 서버만 (장비 0대, 서버 PC 안에서)

```bash
# 터미널 A
cd ~/gts_udp_server && source venv/bin/activate && python udp_server.py

# 터미널 B — GFC 흉내 (서버와 같은 PC 라 로컬 포트를 6501 로 비켜 준다)
python3 gfc_sim.py 127.0.0.1 1 6501

# 터미널 C — 콘솔 흉내
python3 console_sim.py 127.0.0.1 1
> c            → RX CONNECT_ACK result=0 ... ENTER CONTROL 활성
> t 300 15     → RX ACK for 0x22
> run          → 터미널 B 에 "SRC enable=1 init=30.0s cycle=1.5s period=600.0s"
> stop
> on / off     → 터미널 B 에 "PUMP [1,1,1] / [0,0,0]"
```

여기까지 통과하면 **서버 로직은 끝**이다. 이후 실패는 전부 네트워크나 펌웨어 문제다.

### STEP 2 — GFC 펌웨어 (STM32 없이)

`gfc_config.h` 에서 `GFC_STM32_ENABLE 0` 으로 두고 플래시.

```bash
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

- 모니터에 `UDP 192.168.0.6:5501 <- local 5501 (DTYPE=2 DID=1)`
- 서버 로그에 `[GFC] NEW DEVICE DID=1` + `HELLO DID=1 model=GTS-GFC`
- **GREEN LED(33) 상시 점등** = 서버 응답 정상
- 터미널 C 의 `console_sim.py` 로 `on` → 모니터에 `RX cmd=0x30`, `PUMP_SET 1 1 1`

### STEP 3 — STM32 연결

`GFC_STM32_ENABLE 1` 로 되돌리고 UART2 배선 확인 후 플래시.

- 부팅 3초 안에 `STM32 auto-push detected` 가 뜨면 장비가 스스로 `0x72` 를 올리는 것
- 안 뜨면 `no auto-push in 3000 ms -> polling` 으로 1초 폴링으로 내려간다 (정상)
- `checksum fail` 이 계속 뜨면 → **배선/레벨/보레이트** 문제
  - ESP32 는 5V tolerant 가 아니다. STM32 TX 레벨을 먼저 확인할 것 (`GTS_GFC_UDP.md` 5절)
  - TX/RX 가 서로 바뀌지 않았는지 (`GFC_UART_TX_PIN` / `GFC_UART_RX_PIN`)
- 서버 로그에서 `SENSOR` 의 `p=100` 같은 펌프 상태가 실제와 맞는지

### STEP 4 — 콘솔 실물

`GTS_OFFLINE_MODE 0` 으로 빌드 후 플래시.

1. P1 에서 **GFC** + **ID 1** 선택 → Connection Test 카드가 채워지고 `ENTER CONTROL` 활성
2. `ENTER CONTROL` → P2 진입 (콘솔이 자동으로 `SET_MODE(AUTO)` 를 보낸다)
3. **MANUAL** 탭 → Pump 버튼 → GFC 의 RED LED FAST + 펌프 실제 구동
4. **AUTO** 탭 → 두 시간 타일을 jog 로 맞추고 START
   - init 구간 동안 RED LED 상시 점등, 이후 SLOW
   - 우측 패널의 "다음 분사까지" 카운트다운이 서버 push 로 내려온다
5. STOP → Pump 전부 Off

---

## 4. 증상별 점검표

| 증상 | 확인할 것 |
|---|---|
| P1 에서 `ENTER CONTROL` 이 안 열림 | 서버 로그에 콘솔 `CONNECT` 가 찍히는가 → 찍히면 GFC 가 오프라인 (`result=3`). GFC 전원·Wi-Fi 확인 |
| 서버에 콘솔 패킷 자체가 안 옴 | `218.147.152.41:5502` 로 보내는데 포트포워딩(UDP 5502→192.168.0.6)이 살아 있는가. 콘솔이 LAN 안에 있으면 공유기 **헤어핀 NAT** 지원 여부 |
| 콘솔이 1초마다 P1 으로 튕김 | PONG 이 안 돌아온다. 서버가 응답을 보내는 주소(콘솔의 NAT 외부 주소)로 되돌아가는지 |
| `[GFC] 규격 외 ... HEX:` 로그 | 구 펌웨어(`GFC01,STATUS=OK`)가 아직 올라가 있다 |
| GFC RED LED 가 BLINK2 (짧게 2번) | STM32 UART 링크 끊김 — STEP 3 배선 항목 |
| 서버는 `TX->` 를 찍는데 GFC 가 못 받음 | GFC 가 로컬 5501 bind 에 실패했는지 모니터 확인. 공유기가 같은 LAN 인지 |
| 펌프가 안 멈춤 | `AUTO_RUN(0)` 은 `SRC_SET(enable=0)` + `PUMP_SET(0,0,0)` 두 발이다. 둘 다 서버 로그에 찍히는지 |
| **첫 조작만 먹고 두 번째부터 무반응** | 2026-09-21 수정됨 (E8/E9). 콘솔 버튼 라벨이 1초 안에 혼자 되돌아가면 서버가 `GFC_AUTO_STATE.pump` 에 엉뚱한 값을 싣고 있는 것 |

디버깅 시 `udp_server.py` 의 `LOG_HEX = True` 로 두면 모든 패킷이 HEX 로 찍힌다.

---

## 5. 설계 결정 (이번 세션)

| # | 결정 | 이유 |
|---|---|---|
| E1 | 서버→GFC 하향은 **직접 송신** (ACK 동봉 D12 아님) | 같은 LAN 이라 NAT 가 없다. GFC 가 로컬 5501 에 bind 하므로 주소가 결정적이고, 명령 지연이 ACK 주기에 묶이지 않는다. 외부망을 거치게 되면 D12 로 바꿀 것 |
| E2 | GFC 주소는 **수신 패킷의 source 주소**를 그대로 쓴다 | 펌웨어는 5501 에 bind 하므로 결과가 같고, 시뮬레이터처럼 임의 포트를 쓰는 상대도 받는다 |
| E3 | `cycle_count` 는 서버가 **`SRC_ON` 상승 에지**로 센다 | `sensor_data_t` 52 byte 를 규격대로 유지. 초기 주입(`FLAG_SRC_INIT`)은 주기 분사가 아니므로 제외 |
| E4 | `flags` bit4 = `SRC_INIT` 신설 | E3 을 위해. bit 5·6 은 비워 둠 |
| E5 | 수동 `PUMP_SET` 이 주입 시퀀스를 **정지시킨다** | 시퀀스가 매 100 ms 펌프를 덮어쓰므로, 멈추지 않으면 수동 조작이 곧바로 무효가 된다 |
| E6 | `period_sec` 는 서버가 **600 고정** | 콘솔 P2 에 주기 입력 UI 가 없다 (`CYCLE_PERIOD_S 600`). 바꾸려면 콘솔 UI 부터 |
| E7 | 주기 분사는 init 종료 후 **1주기 뒤부터** (`n >= 1`) | init 이 끝나자마자 또 분사되는 것을 막는다 |
| E8 | `GFC_AUTO_STATE.pump` 는 장비가 보고한 **실제 Pump2** 다 | `src_on`(주입 시퀀스 분사 중)을 쓰면 MANUAL 모드에서 항상 0 이라 콘솔 버튼이 500 ms 마다 되돌아간다 |
| E9 | 콘솔에 **로컬 조작 우선 구간**(`GTS_LOCAL_HOLD_MS` 1200 ms) | 명령 직후에는 서버가 아직 옛 상태를 push 한다. 그 구간의 `run`/`pump` 만 무시한다 (`remain`/`cycle` 은 콘솔이 알 수 없는 값이라 그대로 따름) |
| E10 | 서버의 명시적 `PUMP_SET` 은 값이 같아도 **UART 로 다시 내보낸다** | 변화 감지로 막아 두면 STM32 가 앞 프레임을 놓쳤을 때 복구할 길이 없다. 100 ms 시퀀스 틱만 변화 감지를 쓴다 |
| E11 | `gfc_uart_pump_set()` 이 캐시를 **낙관적으로 선반영** | 안 그러면 상향 SENSOR_DATA 가 STM32 폴링(1초)을 기다려야 갱신되고, 명령 직후 보내는 SENSOR_DATA 가 낡은 값을 퍼뜨린다. 틀렸으면 다음 응답이 덮어쓴다 |
| E12 | UART 폴링 태스크를 **100 ms 틱 + 조회 카운터** 로 | 펌프 명령 직후(`s_expedite`) 1초를 기다리지 않고 확인할 수 있다. `0x70` 뒤에 `0x71` 을 바로 붙이지는 않는다 — 한 틱(100 ms) 띄워 STM32 파서 부담을 줄인다 |
| E13 | 서버는 `run`/`pump` 가 **바뀌면 즉시 push** | 500 ms 주기를 기다리면 사용자가 버튼을 누른 뒤 상태가 화면에 돌아오기까지 그만큼 더 걸린다 |

---

## 5-1. 2026-09-21 수정 — "두 번째부터 펌프가 안 먹음"

### 증상

P2 MANUAL 에서 Pump On 은 되는데, 그 다음 조작부터 아무 반응이 없다.

### 원인

서버 `push_states()` 가 `GFC_AUTO_STATE` 의 `pump` 필드를 `dev.src_on`
(주입 시퀀스가 분사 중인지)으로 채우고 있었다. MANUAL 모드에서는 시퀀스가
꺼져 있으므로 이 값이 **항상 0** 이다.

```
1. PUMP ON 누름 → 콘솔 gfc_pump_on = true, SET_PUMP(1) 송신 → 펌프 켜짐  ✅
2. 500 ms 뒤 서버 push 가 gfc_pump_on = false 로 덮어씀 → 버튼이 "PUMP ON" 으로 복귀
3. 다시 누름 → next = !false = true → 또 SET_PUMP(1)
4. GFC 의 apply_pumps() 가 값이 같다고 early-return → UART 프레임 없음 → 무반응
```

ACK 와는 무관하다. 콘솔은 원래 ACK 를 기다리지 않는다 —
`gts_proto_gfc_set_pump()` 는 보내고 바로 리턴하고, `GTS_CMD_ACK` 수신 시
하는 일은 무응답 카운터 리셋뿐이다.

### 수정 (3곳)

| 파일 | 변경 |
|---|---|
| `server/udp_server.py` | `GfcDevice` 가 `pump1/2/3` 을 보관. `push_states()` 의 `pump` 를 `dev.src_on` → **`dev.pump2`** (E8) |
| `gts_gfc_udp/main/gfc_ctrl.c` | `apply_pumps_ex(..., force)` 신설. 서버발 `PUMP_SET` 과 시퀀스 정지는 `force=true` 로 반드시 송신 (E10) |
| `GTS_CONSOLE_ESP32P4/main/gts_protocol.c`, `gts_config.h` | `GTS_LOCAL_HOLD_MS` 1200 ms 로컬 우선 구간. GFC 제어 송신 시 타이머를 찍고, 그 안에 들어온 `GFC_AUTO_STATE` 의 `run`/`pump` 는 무시 (E9) |

### 회귀 테스트

`test_packets.py` 의 **"pump 필드는 실제 펌프 상태다 (MANUAL 회귀)"** 절이
이 조건을 고정한다. `src_on=0` 인데 장비가 `pump2=1` 을 보고하는 상황에서
push 의 `pump` 가 1 이어야 통과한다.

루프백 E2E 로 `on → off → on → off` 4회가 전부 GFC 까지 도달하는 것을 확인했다.

---

## 5-2. 2026-09-21 수정 — 반응 지연

동작은 하는데 느리다는 보고. **명령 경로가 아니라 상태 경로**가 문제였다.

```
명령 경로 (버튼 → 펌프)          : 수십 ms        문제 없음
상태 경로 (펌프 → 화면 복귀)      : 최대 ~2.5 초   ← 여기
  ① GFC 의 STM32 캐시는 1초 폴링으로만 갱신         1000 ms
  ② GFC → 서버 SENSOR_DATA 주기                    1000 ms
  ③ 서버 → 콘솔 push 주기                           500 ms
  ④ 콘솔 UI 갱신 (lv_timer 100 ms)                  100 ms   ← 문제 아님
```

게다가 명령 직후 보내던 `send_sensor(0)` 이 **STM32 캐시의 옛 펌프 값**을
실어 보내고 있었다 — 즉시 보고가 오히려 낡은 상태를 퍼뜨렸다.

### 수정

| 파일 | 변경 |
|---|---|
| `gfc_uart.c` | `gfc_uart_pump_set()` 이 캐시를 명령값으로 선반영 (E11). 폴링 태스크를 100 ms 틱 + `s_expedite` 로 바꿔 명령 직후 곧 확인 (E12). 자동송신 모드에서도 `fast` 패스면 `0x71` 한 번 조회 |
| `udp_server.py` | `apply_sensor()` 가 `(src_enable, pump2)` 변화 여부를 반환. 바뀌면 `HOLDER["console"].push_states()` 로 즉시 push (E13) |

### 결과

루프백 측정 (버튼 송신 → 콘솔이 바뀐 `pump` 를 받기까지) **6회 모두 1~5 ms**.

> 다만 `gfc_sim.py` 는 STM32 UART 지연을 흉내내지 않으므로 이 수치는
> **서버 즉시 push(E13)** 를 검증한 것이다. E11/E12 가 잡는 STM32 폴링
> 지연은 실물에서만 확인된다. 실장비에서는 Wi-Fi + UART 왕복이 더해져
> 수십 ms 대를 기대한다.

### 회귀 테스트

`test_packets.py` 의 **"상태 변화 시 즉시 push"** 절 — 변화가 없으면 push 하지
않고, `pump2` 나 `src_enable` 이 바뀌면 정확히 한 번 push 한다.

---

## 6. 아직 안 한 것

- **AOS(5500)** — 콘솔의 AOS 명령(`0x30/0x31/0x40~0x43`)은 서버가 NAK 로 답한다
- **자동 농도조절** (`0x35 AUTO_SET`) — `GTS_GFC_UDP.md` 9장 A단계(개루프)까지만 구현
- **오프라인 버퍼** (`0x33 SENSOR_BULK`), NVS 설정 저장, AP 프로비저닝, NTP
- **DB/로그 파일** — 서버는 아직 stdout(journalctl) 만
- `co2` 예약 2바이트의 의미 (U2), UART2 실제 핀 (U3), ADC 기준전압 (U4)
