# GTS Console UDP 통신 규격

- **Rev** 0.1 · 2026-09-20
- **대상** GTS Console (ESP32-P4) ↔ GTS Server ↔ AOS / GFC 장치
- **기준** `FW_RS232_Protocol.md` (PC ↔ STM32 RS232) 의 프레임 개념을 UDP로 이식
- **구현** `GTS_CONSOLE_ESP32P4/main/gts_protocol.[ch]`

---

## 0-0. 디자인 사양서 §9 와의 관계 (2026-09-21 결정)

`DOC/gts-console-ui-mockups/project/GTS_Console_LCD_Spec.md` Rev 0.2 의 **§9 에도
UDP 프로토콜 초안이 들어 있으나, 이 문서가 기준이다.** §9 는 참고용으로만 두고
구현·서버 모두 여기를 따른다.

§9 를 따르지 않는 이유:

| 항목 | 이 문서 (채택) | §9 초안 |
| --- | --- | --- |
| 값 표현 | IEEE-754 float | 정수 스케일 (0.01 V 등) |
| DATA 헤더 | `src_type, src_id, seq` 4 byte 고정 | 없음 |
| GFC cmd | 0x20 ~ 0x23 | 0x10 ~ 0x13 |
| AOS 측정 cmd | 0x30 / 0x31, 통지 0xB1 | 0x20 / 0x21, 통지 0xA1 |
| AOS param cmd | 0x40, 통지 0xC3 | 0x30, 통지 0xB0 |
| ACK 규칙 | 요청 cmd \| 0x80 으로 확정 | "확정 필요" 로 미결 |

1. **float 가 RS232 규격과 그대로 맞는다.** `FW_RS232_Protocol.md` 의
   `CMD_TWIN_SCAN_START`(0x80) / `CMD_TWIN_MEASURE_POINT`(0x82) 가 HV·Frq·Duty·CV·LFV 를
   float 로 받는다. 정수 스케일을 쓰면 서버가 매 패킷마다 변환해야 하고,
   스케일 표를 양쪽에서 따로 관리하게 된다.
2. **`seq` 가 UDP 에 필요하다.** UDP 는 응답이 유실되거나 순서가 바뀐다.
   `seq` 가 없으면 늦게 도착한 이전 응답을 최신 것으로 오인한다. §9 에는 이 필드가 없다.
3. **이미 구현·검증되어 있다.** `test/host/run.sh` 가 CRC16 표준 검사값과
   이 문서의 예시 프레임 바이트를 매 빌드마다 대조한다.

프레임의 **기본 골격(STX / device_type / device_id / cmd / size_L / size_H / data / CRC16)과
CRC 계산 구간은 §9 와 동일**하다. 다른 것은 cmd 번호와 payload 표현뿐이다.

---

## 0. 개요

| 항목 | 값 |
| --- | --- |
| 물리/전송 계층 | UDP/IPv4 (unicast) |
| 서버 | `218.147.152.41 : 5502` |
| 콘솔 로컬 포트 | 임의 (bind 0) — 응답은 동일 소켓으로 수신 |
| 바이트 순서 | **Little Endian** (모든 정수·float) |
| 실수 표현 | IEEE-754 binary32 (`float`, 4 byte) |
| 최대 프레임 | 512 byte (payload ≤ 500 byte) |
| 통신 방식 | 콘솔 master, 요청-응답 + 서버 주기 push |
| 무결성 | **CRC-16/MODBUS** (RS232 규격의 1-byte checksum을 대체) |

RS232 규격과의 차이는 세 가지다. ① 장치를 1:N으로 구분해야 하므로 `device_type` / `device_id`
필드가 CMD 앞에 추가됐다. ② UDP는 비트 오류뿐 아니라 패킷 유실·순서 바뀜이 가능하므로
1-byte checksum 대신 CRC16을 쓰고, 요청마다 `seq`를 둔다. ③ SIZE는 RS232의 `Is_2Byte=true`
모드와 동일하게 **항상 2 byte LE**로 고정한다.

---

## 1. 프레임 포맷

```
┌─────┬──────┬──────┬─────┬────────┬────────┬───────────┬────────┐
│ STX │ TYPE │  ID  │ CMD │ SIZE_L │ SIZE_H │ DATA[SIZE]│ CRC16  │
│ 1 B │ 1 B  │ 1 B  │ 1 B │  1 B   │  1 B   │  SIZE B   │  2 B   │
└─────┴──────┴──────┴─────┴────────┴────────┴───────────┴────────┘
  0x02                                                     LE
```

| 필드 | 크기 | 의미 |
| --- | --- | --- |
| **STX** | 1 | `0x02` 고정 |
| **TYPE** | 1 | 이 프레임이 다루는 **대상 장치**의 종류 (2절) |
| **ID** | 1 | 대상 장치 ID `1 ~ 20`, `0x00` = 장치 무관(시스템 명령) |
| **CMD** | 1 | 명령 코드 (4절). bit7 = 1 이면 **응답/통지** |
| **SIZE** | 2 | DATA 길이 (byte), Little Endian. DATA 없으면 0 |
| **DATA** | SIZE | 공통 헤더 4 byte + 명령별 payload (1-1 참조) |
| **CRC16** | 2 | `TYPE`부터 `DATA` 끝까지의 CRC-16/MODBUS, Little Endian |

**STX는 CRC 계산에서 제외**한다 (RS232 규격이 checksum에서 STX를 제외한 것과 동일).

### 1-1. DATA 공통 헤더 (모든 프레임 필수, 4 byte)

UDP는 연결이 없으므로 **보낸 쪽**을 프레임 안에서 식별해야 한다. 그래서 DATA의 앞
4 byte는 명령 종류와 무관하게 항상 아래 고정 헤더다. `SIZE`는 이 헤더를 **포함**한 길이다.

| Offset | Size | Type | 필드 | 설명 |
| --- | --- | --- | --- | --- |
| 0 | 1 | uint8 | `src_type` | 송신자 종류 — 콘솔은 `0x03` |
| 1 | 1 | uint8 | `src_id` | 송신자 ID — 콘솔의 `my_id` (1~20) |
| 2 | 2 | uint16 | `seq` | 요청 일련번호. 응답은 요청의 `seq`를 그대로 반사 |

따라서 payload 없는 명령도 `SIZE = 4`, 전체 프레임은 `1+1+1+1+2+4+2 = 12 byte`다.

### 1-2. 예시 프레임 — `CMD_CONNECT` (AOS #7 연결 요청, my_id = 1, seq = 5)

```
02  01 07 01 04 00  03 01 05 00  05 16
│   │  │  │  └─┬─┘  └─────┬────┘ └─┬─┘
│   │  │  │     │         │        └── CRC16 = 0x1605 → LE 전송 (05 16)
│   │  │  │     │         └─────────── DATA(4) : src_type=0x03(CONSOLE),
│   │  │  │     │                               src_id=0x01(my_id), seq=0x0005
│   │  │  │     └───────────────────── SIZE = 4 (LE)
│   │  │  └─────────────────────────── CMD  = 0x01 CONNECT
│   │  └────────────────────────────── ID   = 7  (대상 AOS ID)
│   └───────────────────────────────── TYPE = 0x01 (AOS)
└───────────────────────────────────── STX
```

전체 12 byte. CRC 계산 구간은 STX를 뺀 `01 07 01 04 00 03 01 05 00` 9 byte다.

### 1-3. CRC-16/MODBUS

```c
uint16_t gts_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}
```

- 다항식 `0xA001` (reflected 0x8005), 초기값 `0xFFFF`, 최종 XOR 없음
- 전송 시 하위 바이트 먼저 (`crc & 0xFF`, `crc >> 8`)
- 검증은 수신 프레임의 `TYPE ~ DATA` 구간을 다시 계산해 비교

---

## 2. Device Type

| 값 | 이름 | 비고 |
| --- | --- | --- |
| `0x00` | `GTS_DEV_NONE` | 시스템 명령 (대상 장치 없음), ID도 0 |
| `0x01` | `GTS_DEV_AOS` | AOS 1 ~ 20 |
| `0x02` | `GTS_DEV_GFC` | GFC 1 ~ 20 |
| `0x03` | `GTS_DEV_CONSOLE` | 콘솔 자신 — `src_type`으로만 사용 |

---

## 3. 통신 흐름

### 3-1. 연결 확인 (PAGE 1 · ENTER CONTROL 활성 조건)

```
Console                                  Server
   │── CONNECT (0x01) ──────────────────▶│
   │                                      │  (해당 장치 상태 조회)
   │◀───────────── CONNECT_ACK (0x81) ───│  link=OK, state=IDLE
   │                                      │
```

- 콘솔은 `GTS_REPLY_TIMEOUT_MS`(기본 500 ms) 안에 응답이 없으면 **최대 3회** 재전송한다.
- 3회 모두 실패 → `link_state = NO_REPLY`, 상단바 적색, `ENTER CONTROL` 비활성.

### 3-2. Keep-alive 와 링크 감시 (전 화면 공통)

```
Console ── PING (0x03) ──▶ Server        1초 주기
Console ◀── PONG (0x83) ── Server
```

- PONG 왕복시간을 RTT로 표시한다.
- **연속 3회 무응답** → 링크 끊김 → 진행 중 동작 정지 요청 후 PAGE 1 복귀 (사양서 7절).

### 3-3. 값 변경 (PAGE 2 / 4)

```
Console ── SET_* ──▶ Server
Console ◀── ACK ─── Server      (생략 가능 · 콘솔은 ACK를 기다리지 않음)
```

- Jog 연속 회전 시 **50 ms 디바운스** 후 마지막 값만 전송한다 (사양서 6절).
- 화면은 전송 결과를 기다리지 않고 즉시 갱신한다 (로컬 우선). 서버가 값을 보정해
  `AOS_PARAMS`(0xC3)를 내려주면 그때 화면을 덮어쓴다.

### 3-4. 서버 → 콘솔 주기 통지 (push)

| CMD | 주기 | 내용 |
| --- | --- | --- |
| `0xA3` `GFC_AUTO_STATE` | 500 ms | Auto 운전 상태, 다음 분사까지 남은 시간 |
| `0xB1` `AOS_MEAS_STATE` | 500 ms | 측정 경과/총 시간, 샘플 수, 종료 플래그 |

통지에는 ACK를 보내지 않는다. 통지가 끊기면 3-2의 PING 감시가 링크 상태를 판단한다.

---

## 4. 명령 코드

응답/통지는 요청 코드에 `0x80`을 OR 한 값을 쓴다.

### 4-1. 시스템 (`TYPE`/`ID`는 대상 장치, 연결 전에는 P1에서 선택한 값)

| CMD | 이름 | 방향 | DATA(헤더 뒤) | 설명 |
| --- | --- | --- | --- | --- |
| `0x00` | `CMD_ACK` | 양방향 | `u8 ack_cmd` | 일반 긍정 응답 |
| `0x01` | `CMD_CONNECT` | C→S | 없음 | 장치 연결·상태 조회 |
| `0x81` | `CMD_CONNECT_ACK` | S→C | 4-2 참조 | 연결 결과 |
| `0x02` | `CMD_DISCONNECT` | C→S | 없음 | 장치 해제 (진행 동작 정지 포함) |
| `0x82` | `CMD_DISCONNECT_ACK` | S→C | `u8 result` | 0 = OK |
| `0x03` | `CMD_PING` | C→S | 없음 | keep-alive |
| `0x83` | `CMD_PONG` | S→C | `u8 link_state` | 0 IDLE / 1 RUN |
| `0x04` | `CMD_STATUS_REQ` | C→S | 없음 | 장치 상태 재조회 |
| `0x84` | `CMD_STATUS_RESP` | S→C | 4-2 와 동일 | |
| `0x7F` | `CMD_NAK` | S→C | `u8 err_cmd, u8 err_code` | 거부 |

`err_code`: `1` 알 수 없는 CMD · `2` 범위 초과 · `3` 장치 없음 · `4` 사용 중 · `5` CRC 오류.

### 4-2. `CMD_CONNECT_ACK` (0x81) / `CMD_STATUS_RESP` (0x84) payload — 12 byte

| Offset | Size | Type | 필드 | 설명 |
| --- | --- | --- | --- | --- |
| 0 | 1 | uint8 | `result` | 0 = 연결 OK, 그 외 오류 |
| 1 | 1 | uint8 | `dev_state` | 0 IDLE / 1 RUN / 2 ERROR / 3 OFFLINE |
| 2 | 2 | uint16 | `fw_ver` | 장치 F/W 버전 (상위 8bit major) |
| 4 | 4 | uint32 | `uptime_s` | 장치 가동 시간 (초) |
| 8 | 4 | uint32 | `last_reply_ms` | 서버가 그 장치와 마지막 통신한 경과(ms) |

화면의 `Device State`, `Last Reply` 행이 이 값을 그대로 쓴다.

### 4-3. GFC (`TYPE = 0x02`)

| CMD | 이름 | 방향 | payload |
| --- | --- | --- | --- |
| `0x20` | `GFC_SET_MODE` | C→S | `u8 mode` — 0 MANUAL / 1 AUTO |
| `0x21` | `GFC_SET_PUMP` | C→S | `u8 on` — Pump1·Pump2 동시 |
| `0x22` | `GFC_SET_TIMES` | C→S | `u16 start_ds`, `u16 cycle_ds` |
| `0x23` | `GFC_AUTO_RUN` | C→S | `u8 run` — 1 START / 0 STOP |
| `0xA3` | `GFC_AUTO_STATE` | S→C | `u8 run`, `u8 pump`, `u16 remain_ds`, `u32 cycle_count` |

`*_ds` 는 **0.1초 단위 정수**(deciseconds). 범위 `1 ~ 600` = 0.1 ~ 60.0 sec.
정수로 보내 부동소수 반올림 오차를 없앤다.

`GFC_AUTO_RUN` 의 `run = 0` 은 사양서대로 **Pump1·Pump2 모두 Off**를 포함한다.

### 4-4. AOS 가스 측정 (`TYPE = 0x01`)

| CMD | 이름 | 방향 | payload |
| --- | --- | --- | --- |
| `0x30` | `AOS_SET_TYPE` | C→S | `u8 type` — 0 preTest / 1 1h / 2 2h / 3 8h |
| `0x31` | `AOS_MEAS_RUN` | C→S | `u8 run` — 1 START / 0 STOP |
| `0xB1` | `AOS_MEAS_STATE` | S→C | 아래 16 byte |

`AOS_MEAS_STATE` payload:

| Offset | Size | Type | 필드 |
| --- | --- | --- | --- |
| 0 | 1 | uint8 | `run` (1 = 측정 중) |
| 1 | 1 | uint8 | `type` |
| 2 | 1 | uint8 | `done` (1 = 정상 종료 → 콘솔 자동 Stop) |
| 3 | 1 | uint8 | reserved |
| 4 | 4 | uint32 | `elapsed_s` |
| 8 | 4 | uint32 | `total_s` |
| 12 | 4 | uint32 | `samples` |

측정 종료 시 서버가 `done = 1`로 한 번 보내고, 콘솔은 버튼을 START로 되돌리되
진행 바는 100%로 유지한다 (사양서 5.3).

### 4-5. AOS Manual (`TYPE = 0x01`)

| CMD | 이름 | 방향 | payload |
| --- | --- | --- | --- |
| `0x40` | `AOS_SET_PARAM` | C→S | `u8 param_id`, `u8 pad[3]`, `f32 value` |
| `0x41` | `AOS_SET_LF_MODE` | C→S | `u8 on` |
| `0x42` | `AOS_SET_LF_SHAPE` | C→S | `u8 shape` |
| `0x43` | `AOS_GET_PARAMS` | C→S | 없음 |
| `0xC3` | `AOS_PARAMS` | S→C | 아래 26 byte |

`param_id` (사양서 5.4 타일 순서와 동일):

| id | 항목 | 범위 | step 순환 | 표시 |
| --- | --- | --- | --- | --- |
| 0 | `HV` | 0 ~ 200 V | 0.01 / 0.1 / 1 | 소수 2자리 |
| 1 | `FRQ` | 200 ~ 800 kHz | 0.1 / 1 / 10 | 소수 1자리 |
| 2 | `DUTY` | 20 ~ 80 % | 0.01 / 0.1 | 소수 2자리 |
| 3 | `CV` | −5 ~ 5 V | 0.001 / 0.01 / 0.1 | 소수 3자리 |
| 4 | `LF_FRQ` | 50 ~ 200 Hz | 1 | 정수 |
| 5 | `LF_VOLT` | 0 ~ 5 V | 0.01 / 0.1 | 소수 2자리 |

`pad[3]`은 뒤따르는 `f32`를 4 byte 경계에 맞추기 위한 것이다 — ESP32-P4는 비정렬
접근이 가능하지만 STM32 측 파싱을 단순하게 하려고 넣었다.

`shape`: `0` Square / `1` Sine / `2` Triangle / `3` Trapezoidal.

`AOS_PARAMS` (0xC3) payload:

| Offset | Size | Type | 필드 |
| --- | --- | --- | --- |
| 0 | 4 | float | `hv` (V) |
| 4 | 4 | float | `frq` (kHz) |
| 8 | 4 | float | `duty` (%) |
| 12 | 4 | float | `cv` (V) |
| 16 | 4 | float | `lf_frq` (Hz) |
| 20 | 4 | float | `lf_volt` (V) |
| 24 | 1 | uint8 | `lf_on` |
| 25 | 1 | uint8 | `lf_shape` |

### 4-6. RS232 규격과의 대응

콘솔의 Manual 파라미터는 서버가 RS232 `CMD_TWIN_MEASURE_POINT`(0x82) / `CMD_TWIN_SCAN_START`(0x80)
의 필드로 그대로 옮겨 실을 수 있도록 같은 단위·타입(float, V/kHz/%/Hz)을 쓴다.

| UDP `param_id` | RS232 0x80 offset | RS232 0x82 offset |
| --- | --- | --- |
| 0 `HV` | 0 (float) | 0 (float) |
| 1 `FRQ` | 4 (float) | 4 (float) |
| 2 `DUTY` | 8 (float) | 14 (float) |
| 3 `CV` | 30/34/38 (start/stop/step) | 22 (float) |
| 4 `LF_FRQ` | 14 (uint16 `LFF`) | 10 (uint16 `LFF`) |
| 5 `LF_VOLT` | 18/22/26 (start/stop/step) | 18 (float `LFV`) |
| `lf_shape` | 12 (uint8 `LF_Waveform`) | 8 (uint8) |

`LF_FRQ`만 RS232에서 `uint16`이므로 서버가 내림 변환한다. 콘솔은 step이 1 Hz라
정수값만 만들어내므로 손실이 없다.

---

## 5. 타이밍 · 재시도

| 상수 | 값 | 용도 |
| --- | --- | --- |
| `GTS_REPLY_TIMEOUT_MS` | 500 | 요청 응답 대기 |
| `GTS_RETRY_MAX` | 3 | CONNECT / DISCONNECT 재전송 횟수 |
| `GTS_PING_PERIOD_MS` | 1000 | keep-alive 주기 |
| `GTS_LINK_FAIL_COUNT` | 3 | 연속 무응답 허용 횟수 → P1 복귀 |
| `GTS_JOG_DEBOUNCE_MS` | 50 | 값 변경 송신 디바운스 |

`SET_*` 계열은 재전송하지 않는다. 유실되면 다음 회전/조작이 최신값을 덮어쓰고,
주기 통지가 실제 상태를 되돌려주기 때문이다.

---

## 6. 구현 체크리스트

- [ ] STX `0x02`, SIZE 2 byte LE 고정
- [ ] CRC16/MODBUS — STX 제외, `TYPE ~ DATA` 구간, LE 전송
- [ ] DATA 선두 4 byte 공통 헤더 (`src_type`, `src_id`, `seq`) — payload 없는 명령도 SIZE=4
- [ ] 수신 프레임 길이 검증: `SIZE + 8 == 수신 바이트 수`
- [ ] `seq` 불일치 응답 폐기 (늦게 도착한 이전 응답)
- [ ] `run=0` 수신/송신 시 GFC Pump 양쪽 모두 Off
- [ ] 연속 3회 무응답 → 진행 동작 정지 + P1 복귀 + `NO REPLY` 표시
- [ ] Jog 연속 회전 50 ms 디바운스
