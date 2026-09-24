# AOS Bridge (ESP32) — 콘솔 Manual 파라미터 7종

- 작성일: 2026-09-21
- 대상: `gts_aos_bridge/` (신규). 하드웨어는 `gts_gfc_udp` 와 **같은 ESP32 모듈**
- 범위: 콘솔 P4 (AOS Manual) 의 **7가지만**
  `HV · FRQ · DUTY · CV · LF On/Off · LF_FRQ · LF_VOLT`
  (LF 파형 shape 는 UART 명령이 어차피 같이 실어 보내므로 덤으로 포함)
- 범위 밖: Twin scan / heatmap 측정 (`0x80/0x82/0x86`), 가스 측정(P3)

---

## 0. 구조LV_DISPLAY_ROTATION_270

```
 ESP32-P4 콘솔        Ubuntu 서버 192.168.0.6        ESP32 AOS 브리지      STM32 H753
┌───────────┐ UDP 5502 ┌────────────────────┐ UDP 5500 ┌─────────────┐ UART2  ┌────────┐
│  P4 Manual │─────────▶│  udp_server.py      │─────────▶│gts_aos_bridge│───────▶│ HV/Frq │
│            │◀─────────│  0x40~0x43 ↔ 0x40~  │◀─────────│              │◀───────│ Duty/CV│
└───────────┘ 0xC3      └────────────────────┘ 0x44     └─────────────┘ 38400  │ LF Mod │
                                                                                └────────┘
```

**하드웨어는 GFC 와 동일하다** — LED GPIO32(RED)/GPIO33(GREEN), UART2 GPIO17/16.
다른 것은 UART 속도와 SIZE 필드 폭뿐이다.

| | GFC (`gts_gfc_udp`) | AOS (`gts_aos_bridge`) |
|---|---|---|
| UDP 포트 / DTYPE | 5501 / 2 | **5500 / 1** |
| UDP 명령 대역 | 0x30~0x3A | **0x40~0x4F** |
| UART Baud | 115200 | **38400** |
| UART SIZE 필드 | 1 byte | **2 byte** (Is_2Byte=true) |
| UART 체크섬 | `255 - Σ` | 동일 (`~Σ & 0xFF`) |
| LED 핀 / 패턴 | GPIO32/33 | 동일 |

UDP 프레임 자체(STX/DTYPE/DID/CMD/SEQ/SIZE/DATA/CRC16)는 GFC 와 **완전히 같다.**
서버가 같은 파서를 쓴다.

---

## 1. STM32 쪽에서 확인한 사실 ★

`DOC/FAIMs_H753_V1_6_1_TWIN/Core/Src/MyWork/UART_PC.c` 와 `MyCTL.c` 를 읽고
설계를 고쳤다. 문서(`FW_RS232_Protocol.md`)만 봤을 때의 추측과 다른 점이 있다.

### 1-1. 개별 파라미터 명령이 실제로 구현돼 있다

`FW_RS232_Protocol.md` 5절은 이들을 "Legacy, Twin 에서는 사용 안 함" 으로
적어 두었지만, **H753 펌웨어의 `RxMessage_CTL_PC()` 에 살아 있다.**
그래서 Twin 의 `0x82 MEASURE_POINT` 에 7개 값을 우겨 넣을 필요가 없다.
(`0x82` 는 파라미터 설정이 아니라 "이 조건으로 1 point 측정하라" 라서
매번 측정이 돌아간다.)

### 1-2. 메인 루프가 적용한다 — 별도 "apply" 명령이 없다

`MyCTL.c` 의 `SET_Control()` 이 매 루프에서 `MData.SET.*` 와 `MData.OSET.*`
를 비교해 **바뀐 것만** 하드웨어에 반영한다.

```c
if (MData.SET.CV     != MData.OSET.CV)     CV_Control(...);
if (MData.SET.RF_HV  != MData.OSET.RF_HV)  HV_SET_Control(...);
if (MData.SET.Frq    != MData.OSET.Frq ||
    MData.SET.Duty   != MData.OSET.Duty)   Wave_Frq_and_Duty_Update(...);
switch (MData.MODULATED_MODE) { case 0: /* LF_MOD On/frq/type → LF_Modulator_Set()
                                            amp → LF_Modulator_Voltage_Set() */ }
```

`MData.MODULATED_MODE` 는 부팅 시 `0` 이므로 LF 경로가 살아 있다 (`MyCTL.c:189`).
따라서 **값만 써 넣으면 된다.**

### 1-3. LF On/Off 는 진짜로 있다

`CMD_LF_MOD_SET` (0x52) 이 `type, OnOff, amp(f32), frq(f32)` 를 받는다.
LF On/Off · LF_VOLT(amp) · LF_FRQ(frq) 를 이 한 명령이 전부 덮는다.

> 처음에는 "LF Off = LFV 0 V 로 보낸다" 로 흉내 내려 했는데, 그럴 필요가 없다.
> `OnOff` 를 0 으로 보내면 사용자가 맞춰 둔 amp/frq 가 STM32 안에 그대로
> 남아 있어서 다시 On 할 때 복원된다.

### 1-4. 쓰는 UART 명령은 3개뿐

| CMD | 이름 | 방향 | payload |
|---|---|---|---|
| `0x2A` | `CMD_SET_CONTROL` | →STM32 | 16 B — **f32 HV, f32 Frq, f32 Duty, f32 CV** |
| `0x52` | `CMD_LF_MOD_SET` | →STM32 | 10 B — u8 type, u8 OnOff, f32 amp, f32 frq |
| `0x02` | `CMD_SET_QUERY` | →STM32 0 B / ←STM32 **32 B** | 되읽기 |

`0x2A` 와 `0x52` 는 **응답이 없다.** 그래서 `0x02` 주기 조회가 UART 생존 신호를
겸한다 (1초).

### 1-5. LF 파형 — `type = 6 (eSquare)` 로 고정 ★

**결정(2026-09-21, 사용자 지정): STM32 로 내보내는 `LF_MOD.type` 은 6 (eSquare)
고정.** 콘솔이 `0x42 SET_LF_SHAPE` 를 보내도 STM32 파형은 바뀌지 않고,
브리지가 서버에 되올리는 `lf_shape` 도 콘솔 SQUARE(0) 로 보고한다 — 화면이
실제 장비 상태와 어긋나지 않게 하기 위해서다.

이렇게 고정해 둔 배경: **두 번호 체계가 애초에 서로 다르다.** 나중에 파형
선택을 열 때는 반드시 변환해야 한다.

| 콘솔 `gts_lf_shape_t` | | STM32 `eLF_Type` |
|---|---|---|
| 0 SQUARE | → | **6 eSquare** |
| 1 SINE | → | **5 eSine** |
| 2 TRIANGLE | → | **0 eTriangle** |
| 3 TRAPEZOID | → | **8 eTPZ1_9** |

STM32 전체 목록: `0 eTriangle · 1 eRamp · 2 eRamp1_9 · 3 eRamp2_8 · 4 eRamp3_7 ·
5 eSine · 6 eSquare · 7 eTPZ05_95 · 8 eTPZ1_9 · 9 eTPZ2_8 · 10 eTPZ3_7 · 11 eARB`

변환 없이 그대로 넘기면 **SQUARE 를 골랐는데 TRIANGLE 이 나온다.**
게다가 `LF_Modulator_Voltage_Set()` 의 `default:` 는 파형 배열을 전부
`minValue` 로 채우므로, 모르는 번호는 에러 없이 조용히 틀린다.

`aos_config.h` 의 `AOS_LF_SHAPE_FIXED` 를 **0** 으로 바꾸면 위 표대로 변환하는
경로가 살아난다 (`aos_shape_to_stm32()` / `aos_shape_from_stm32()` 에 이미 들어
있다). 사다리꼴은 STM32 에 4종(5/5, 10/10, 20/20, 30/30)이 있고 콘솔에는
하나뿐이라 10/10 을 대표로 쓴다.

### 1-6. ⚠️ `0x02` 응답의 CV 는 두 번째다

송신과 순서가 다르다. 여기서 틀리면 조용히 CV 와 Frq 가 바뀐다.

```
0x2A 송신 (16 byte)          0x02 응답 (32 byte)
  0  f32 HV                    0  f32 RF_HV
  4  f32 Frq                   4  f32 CV        ← ★
  8  f32 Duty                  8  f32 Frq
 12  f32 CV                   12  f32 Duty
                              16  u8  RF_MOD_OnOff
                              17  f32 RF_MOD_frq      ← 비정렬
                              21  u8  LF_MOD.OnOff
                              22  u8  LF_MOD.type
                              23  f32 LF_MOD.amp      ← 비정렬
                              27  f32 LF_MOD.frq      ← 비정렬
                              31  u8  Current_Type
```

offset 17/23/27 의 float 은 4 byte 경계가 아니다. 포인터 캐스팅 금지, `memcpy` 로.

---

## 2. 파일 구성

| 파일 | 역할 |
|---|---|
| `main/aos_config.h` | Wi-Fi / 서버 주소 / DTYPE·DID / 핀 / 범위 / 타이밍 |
| `main/aos_proto.[ch]` | UDP 프레임 조립·해석 (GFC 와 동일 규격, 순수 함수) |
| `main/aos_uart.[ch]` | STM32 UART2 게이트웨이 (38400, SIZE 2 byte) |
| `main/aos_ctrl.[ch]` | 파라미터 섀도 + 합치기 + 주기 조회 + LED |
| `main/gts_aos_bridge.c` | Wi-Fi, 소켓, 명령 디스패치, 주기 상향 |

`sdkconfig` 는 `gts_gfc_udp` 것을 복사했다 (같은 ESP32 모듈). 새로 만들려면
`rm sdkconfig && idf.py set-target esp32`.

### 확인할 값 (`aos_config.h`)

```c
#define AOS_DID                 1        // 콘솔 P1 에서 고르는 ID 와 같아야 한다
#define AOS_SERVER_IP           "192.168.0.6"
#define AOS_UART_TX_PIN         17       // GFC 와 동일 배선 전제
#define AOS_UART_RX_PIN         16
#define AOS_STM32_ENABLE        1        // 0 이면 UART 출력 없이 경로만 검증
#define AOS_AUTO_STATUS         1        // STM32 자동 상태 송신(0x03) On — 2026-09-23 부터 기본 On (전류값 표시)
```

---

## 3. 명령 변환표

### 3-1. 콘솔 → 서버 → 브리지

번호를 일부러 맞춰 뒀다. 변환이랄 게 거의 없다.

| 콘솔 (5502) | → | 브리지 (5500) | → | STM32 (UART2) |
|---|---|---|---|---|
| `0x40 AOS_SET_PARAM` (u8 id, pad[3], f32) | | `0x40 PARAM_SET` — **8 byte 그대로** | | id 0~3 → `0x2A`<br>id 4~5 → `0x52` |
| `0x41 AOS_SET_LF_MODE` (u8 on) | | `0x41 LF_MODE` (u8 on, pad[3]) | | `0x52` |
| `0x42 AOS_SET_LF_SHAPE` (u8 shape) | | `0x42 LF_SHAPE` (u8, pad[3]) | | — (1-5 참조, 무시) |
| `0x43 AOS_GET_PARAMS` | | `0x43 PARAMS_QUERY` | | `0x02` |

`param_id` 는 콘솔과 같다: `0 HV · 1 FRQ · 2 DUTY · 3 CV · 4 LF_FRQ · 5 LF_VOLT`.

### 3-2. 역방향

| 브리지 | → | 콘솔 |
|---|---|---|
| `0x44 AOS_PARAMS` (26 byte) | | `0xC3 AOS_PARAMS` — **바이트 배치 동일, 그대로 전달** |
| `0x45 AOS_STATUS` (24 byte) | | (서버가 CONNECT_ACK 의 `dev_state` 로 사용) |

`aos_params_t` 를 콘솔 `0xC3` 과 같은 26 byte 로 맞춘 이유가 이것이다 —
서버가 슬라이싱·재조립 없이 그대로 실어 보낸다.

---

## 4. 브리지 내부 동작

### 4-1. 섀도 + 합치기

```
콘솔 jog ──▶ 0x40 ──▶ 섀도 갱신 + dirty 표시
                        │
                        │  AOS_COALESCE_MS(100 ms) 동안 변경이 이어지면 계속 미룬다
                        ▼
              HV/FRQ/DUTY/CV 중 하나라도 바뀜 → 0x2A CMD_SET_CONTROL (16 B)
              LF 관련 하나라도 바뀜          → 0x52 CMD_LF_MOD_SET  (10 B)
```

jog 를 한 바퀴 돌리면 `0x40` 이 연달아 온다. 매번 UART 프레임을 쏘면
38400 bps 가 금방 막히므로 100 ms 창으로 모은다. 콘솔 자체도 50 ms 디바운스를
걸어 보내므로 체감 지연은 거의 없다.

부팅만으로는 STM32 에 아무것도 내보내지 않는다 (`s_dirty = 0`). 콘솔이 값을
보내거나 `0x02` 읽기가 성공해야 섀도가 실제와 맞춰진다.

### 4-2. 범위 clamp (`DOC/GTS_UDP_Protocol.md` 4-5)

| id | 항목 | 범위 |
|---|---|---|
| 0 | HV | 0 ~ 200 V |
| 1 | FRQ | 200 ~ 800 kHz |
| 2 | DUTY | 20 ~ 80 % |
| 3 | CV | −5 ~ 5 V |
| 4 | LF_FRQ | 50 ~ 200 Hz |
| 5 | LF_VOLT | 0 ~ 5 V |

STM32 의 자체 한계(`MyCTL.c` 의 `MinV[]/MaxV[]`)와 비교하면:

| | 콘솔 사양서 | STM32 한계 | 브리지가 쓰는 값 |
|---|---|---|---|
| HV | 0~200 V | 30~560 V | 0~200 (0 = 끄기) |
| FRQ | 200~800 kHz | 200~800 kHz | 동일 ✅ |
| DUTY | ~~10~50 %~~ → **20~80 %** | 20~80 % | 동일 ✅ |
| CV | −5~5 V | −5~5 V | 동일 ✅ |
| LF_FRQ | 50~200 Hz | 30~500 Hz | 50~200 ✅ |
| LF_VOLT | 0~5 V | 0~5 V | 동일 ✅ |

**UART 경로에는 STM32 쪽 clamp 가 없다** (`CMD_SET_CONTROL` 은 `memcpy` 만 한다).
`MinV/MaxV` 는 전면 jog 노브에만 쓰인다. 즉 브리지가 유일한 방어선이다.

벗어나면 clamp 하고 로그를 남긴다 (NaN 도 방어). 서버에 되올리는 `0x44` 는
clamp 된 값이므로 콘솔 화면이 실제와 맞는다.

### 4-3. 서버에 올리는 값

`0x02` 응답을 한 번이라도 받았고 UART 링크가 살아 있으면 **STM32 가 실제로 들고
있는 값**을 싣는다. 아니면 섀도를 싣는다. 콘솔 화면이 실제와 갈라지는 걸
서버가 볼 수 있어야 하기 때문이다.

### 4-4. LED

| | 패턴 |
|---|---|
| **GREEN(33)** | WiFi 미연결 OFF / 서버 무응답 SLOW / 정상 ON |
| **RED(32)** | STM32 무응답 BLINK2 (최우선) / LF On 이면 ON / 설정이 나갔으면 SLOW / 아직 아무것도 안 보냈으면 OFF |

---

## 5. 콘솔 쪽 변경 (같이 반영해야 함)

`gts_protocol.c` 의 로컬 우선 구간(`GTS_LOCAL_HOLD_MS`, GFC 펌프 때 넣은 것)을
**AOS 에도 적용**했다. `gfc_hold_*` → `local_hold_*` 로 이름만 일반화했다.

이유는 GFC 펌프와 같은 문제다. jog 를 돌리는 중에 서버가 옛 `0xC3` 을 내려보내면
방금 돌린 값이 되돌아간다. `0x40/0x41/0x42` 를 보낸 뒤 1200 ms 동안은
`AOS_PARAMS` 를 무시한다. P4 진입 시의 `AOS_GET_PARAMS` 응답은 hold 가 걸려
있지 않아 정상 반영된다.

서버도 같은 이유로 **값이 실제로 바뀐 `0x44` 만** 콘솔에 내린다 — 같은 값을
1초마다 되쏘지 않는다.

---

## 6. 테스트

### STEP 1 — 서버만 (장비 0대)

```bash
# 터미널 A
cd ~/gts_udp_server && source venv/bin/activate && python udp_server.py

# 터미널 B — AOS 흉내 (서버와 같은 PC 라 로컬 포트를 6500 으로 비켜 준다)
python3 aos_sim.py 127.0.0.1 1 6500

# 터미널 C — 콘솔 흉내
python3 console_sim.py 127.0.0.1 1
> a              타깃을 AOS 로
> c              → RX CONNECT_ACK result=0
> hv 123.5
> frq 350
> duty 25
> cv -1.5
> lff 120
> lfv 2.5
> lfon / lfoff
> shape 2
> hv 999         → 터미널 B 에 "clamp 999 -> 200"
> get
```

매 명령마다 터미널 C 에 `RX AOS_PARAMS ...` 가 갱신되면 경로가 뚫린 것이다.

### STEP 2 — 브리지 펌웨어 (STM32 없이)

`aos_config.h` 에서 `AOS_STM32_ENABLE 0` 으로 두고 플래시.

```bash
cd .../gts_aos_bridge && idf.py build flash monitor
```

- 모니터에 `UDP 192.168.0.6:5500 <- local 5500 (DTYPE=1 DID=1)`
- 서버 로그에 `[AOS] NEW DEVICE DID=1` + `HELLO DID=1 model=GTS-AOS`
- GREEN LED 상시 점등
- `console_sim.py` 로 `hv 120` → 모니터에 `PARAM_SET id=0 value=120.0000`,
  이어서 `SET_CONTROL hv=120.00 ...`

### STEP 3 — STM32 연결

`AOS_STM32_ENABLE 1` 로 되돌린다. **38400** 이라 GFC(115200)와 다르다 —
배선을 그대로 옮겨 꽂았다면 속도만 맞으면 된다.

- 1초마다 `0x02` 조회가 나가고 32 byte 응답이 와야 한다
- `checksum fail` 이 계속 뜨면 SIZE 를 1 byte 로 착각한 게 아닌지 (여기는 2 byte),
  또는 보레이트
- 값을 바꿨을 때 STM32 LCD 의 HV/CV/FRQ/DUTY 표시가 따라오는지
- CV 와 FRQ 가 서로 바뀌어 보이면 → 1-5 의 offset 문제

### STEP 4 — 콘솔 실물

P1 에서 **AOS** + ID 1 → `ENTER CONTROL` → P4 Manual 진입.
타일을 골라 jog 를 돌리면 값이 STM32 까지 내려간다.

### 회귀 테스트

`tshape` (호스트 gcc) 가 LF 파형 번호 처리를 고정한다 — 고정 모드에서는
콘솔이 무엇을 보내든 STM32 로 6 이 나가고 보고는 SQUARE 여야 한다.

`test_packets.py` 의 AOS 절이 고정하는 것:
구조체 크기(26/24), 콘솔↔브리지 변환 6종 + LF 3종, 범위 밖 param_id NAK,
`0x44 → 0xC3` 26 byte 무손실 전달, 같은 값이면 재전송 안 함,
STM32 UART 프레임(2 byte SIZE 체크섬, `0x2A`/`0x52` 오프셋, `0x02` 응답 32 byte).

---

## 7. 아직 안 한 것

- **Twin 측정** — scan/heatmap(`0x80/0x82/0x86`), 콘솔 P3 가스 측정(`0x30/0x31/0xB1`)
- `CMD_STATUS_QUERY`(0x03) 자동 송신으로 오는 이온 전류를 콘솔에 보여주기
  (`AOS_AUTO_STATUS 1` 로 켤 수는 있고, 서버까지는 올라간다)
- HF Modulation (`0x50`), Current_Type, FAN
- NVS 설정 저장, AP 프로비저닝, NTP
- **HV 하한** — STM32 jog 한계는 30~560 V. 브리지는 콘솔 사양서대로 0~200 V 를
  쓴다 (0 은 "끄기" 로 정상 취급)
- LF 파형 선택 — 지금은 `eSquare` 고정. 열려면 `AOS_LF_SHAPE_FIXED 0`
  (사다리꼴 4종 중 무엇을 콘솔 TRAPEZOID 에 대응시킬지도 그때 정할 것)


## 2026-09-23 전류값(STATUS 0x45) 수정

- 증상: 웹 장비 상세 AOS Current 가 늘 0. 원인: `AOS_AUTO_STATUS 0` → STM32 가 전류(0x03)를 한 번도 안 보냄 →
  브리지 STATUS 의 air_p/air_n/gas_p/gas_n 이 memset 0 그대로 전송. STM32 는 0x03 **요청에 응답하지 않는다**(송신 전용)
- 수정
  - `AOS_AUTO_STATUS 1` — 부팅 시 0x04 On. STM32 는 대기 중 25ms 마다 0x03 (측정 순회 중엔 안 보냄)
  - `aos_uart_state_t.cur_count`(0x03 수신 수), `busy_count`(0x00/0x02/0x03/0x22 이외 프레임 수)
  - STATUS err 비트 13 `AOS_ERR_NO_CURRENT` — 0x03 이 `AOS_CUR_STALE_MS`(3초) 넘게 없음 → 전류 필드는 유효값 아님
  - STM32 가 한가한데(측정 프레임 3초 없음) 전류가 없으면 0x04 재요청 (5초 → 최대 60초 간격, STM32 리셋 대비)
- 서버: err 비트 13 또는 4채널 모두 0 이면 전류 샘플을 버리고 "전류값 미수신 + 이유" 를 웹에 표시 (0 과 구별)
- 백업: `gts_aos_bridge/backup_20260923b/`
- ⚠ 추가 수정 (같은 날): 자동 상태를 켜자 브리지가 멈춤(LED 꺼짐, UDP 끊김).
  원인: `AOS_UART_LOG_HEX 1` 로 0x03(초당 40번)을 프레임마다 hex 2줄씩 찍어 콘솔 출력이 못 따라가고,
  수신 태스크(우선순위 6)가 CPU 를 독점해 LED·UDP 태스크가 굶음. → 0x03 은 hex 로그에서 제외,
  값은 1초 health 로그(`cur#N adc=a/b/c/d`)로 확인. STM32 는 0x04 설정을 전원이 꺼질 때까지 기억하므로
  옛 펌웨어로 되돌릴 때는 STM32 전원도 껐다 켤 것
