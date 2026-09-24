# ESP32 UDP ↔ RS232 브리지 프로토콜 규격

> ESP32가 PC와 UDP로 통신하고, F/W(STM32)와 RS232로 통신하는 브리지 장치용 프로토콜.
> Base 프로토콜: `FW_RS232_통신규격.md` 참조 (프레임/CMD 정의).
> 작성일: 2026-07-04 (9차 세션 기준)

---

## 0. 시스템 구조

```
┌─────────┐        WiFi/UDP         ┌─────────┐       RS232        ┌─────────┐
│   PC    │◄───────────────────────►│  ESP32  │◄──────────────────►│   F/W   │
│ AOS_CTL │        (bridge protocol) │ Bridge  │  38400/8-N-1       │ (STM32) │
└─────────┘                          └─────────┘                    └─────────┘
```

ESP32의 역할:
1. UDP 서버로 PC 명령 수신
2. RS232로 F/W에 프레임 전송 (base 프로토콜 그대로)
3. F/W → RS232 응답 수신
4. UDP로 PC에 응답 전달

---

## 1. 기본 설계 원칙

### 1-1. Payload passthrough

**base RS232 프로토콜의 프레임(STX~CHKSUM)을 UDP payload에 그대로 담아 전달.**

- 재프레이밍 없음
- CMD 값/의미 완전 동일
- Checksum 규칙 동일 (`~sum & 0xFF`)
- **Is_2Byte = true 필수** (기존 프로젝트 설정과 동일)

이 방식의 장점:
- PC 측 코드 최소 수정 (UART Write → UDP Send만 교체)
- F/W 측 코드 무수정
- 프로토콜 이중 관리 부담 없음

### 1-2. ESP32는 "투명" 브리지

ESP32는 payload 내용 해석 불필요. 단 다음만 처리:
- UDP → RS232: payload 통째로 UART로 forward
- RS232 → UDP: STX 감지 + CHKSUM 검증 후 한 프레임 단위로 UDP 송신

⚠️ **PC와 F/W 통신 규격 확장 시 ESP32 코드 수정 필요 없음.** 새 CMD 추가돼도 그대로 forward.

---

## 2. UDP 통신 규약

### 2-1. 네트워크 파라미터

| 항목 | 값 | 비고 |
|------|-----|------|
| Transport | UDP | TCP 대비 저지연, 재전송 없음 |
| ESP32 IP | (설정 필요) | DHCP or Static |
| ESP32 Port | **5000** (권장) | PC ↔ ESP32 |
| PC Port | (dynamic) | PC가 임의 할당 |
| Max UDP payload | **9000 byte** (권장) | Jumbo frame 지원 네트워크 |

### 2-2. UDP payload 형식

**UDP payload = 완전한 RS232 프레임 1개** (STX 포함).

```
UDP payload:
┌──────┬──────┬──────────────┬─────────────┬─────────┐
│ STX  │ CMD  │ SIZE (2B LE) │ DATA[SIZE]  │ CHKSUM  │
│ 0x02 │ 1 B  │ 2 B          │ SIZE B      │ 1 B     │
└──────┴──────┴──────────────┴─────────────┴─────────┘
```

**하나의 UDP 패킷에 하나의 프레임만 담는다** (혼란 방지).

### 2-3. 크기 한계

RS232 base 프로토콜은 최대 payload 65535 byte. UDP는 이론상 64 KB지만 실제로는:
- 표준 Ethernet MTU: 1500 byte → IP fragmentation 발생
- Jumbo frame: 9000 byte까지 안전
- Twin SCAN_DATA_ALL: 약 **8576 byte** (헤더 + 프레임 오버헤드 포함 시 ~8582)
- → **Jumbo frame 사용 권장**

WiFi 환경이라면 fragmentation을 피하기 위해:
- MTU 1500 이하로 chunk 나눠 UDP 여러 개로 전송하는 방식 (Section 5) 고려

---

## 3. 통신 흐름

### 3-1. 기본 흐름 (Heatmap 1장 측정 예)

```
PC ─────────────────► ESP32 ──────────────► F/W
        UDP                       RS232
   [STX 80 sz.. data..]      [STX 80 sz.. data..]
        (44 byte 프레임)         (동일)

F/W ────────────────► ESP32 ──────────────► PC
        RS232                     UDP
   [STX 86 sz.. 8576B data..]  [STX 86 sz.. 8576B data..]
        (8582 byte 프레임)       (동일)

PC → ESP32 → F/W:  [STX 00 00 00 FF]  (RECEIVED_OK)

F/W → ESP32 → PC:  [STX 85 08 00 data.. CS]  (SCAN_DONE)
```

### 3-2. ESP32 내부 처리

**UDP → RS232 방향:**
```
1. UDP 패킷 수신
2. payload[0] == 0x02 확인 (STX)
3. payload 전체를 UART로 write (검증 없이 그대로)
```

**RS232 → UDP 방향:**
```
1. UART byte stream 수신
2. STX(0x02) → CMD → SIZE(2B) → DATA[SIZE] → CHKSUM 상태 기계 파싱
3. 프레임 하나 완성 시 buffer에 저장
4. CHKSUM 검증
5. 검증 성공 시 프레임 통째로 UDP payload로 PC에 송신
6. 검증 실패 시 버림 (또는 로그)
```

⚠️ **ESP32는 CHKSUM 검증만 하고, 프레임 무결성이 확인된 것만 UDP로 forward.** UART 노이즈로 인한 부분 프레임이 UDP로 새어나가지 않도록 함.

---

## 4. 신뢰성 대응

UDP는 재전송/순서보장이 없으므로 다음 대책 필요.

### 4-1. Application-level ACK

Base 프로토콜의 `CMD_RECEIVED_OK(0x00)`가 이미 ACK 역할. UDP 전환 후에도 그대로 활용:

- PC → F/W: SCAN_START (0x80) 전송
- F/W → PC: SCAN_DATA_ALL (0x86) 응답
- PC → F/W: RECEIVED_OK (0x00) 반드시 회신 ← **이걸로 왕복 확인**
- F/W → PC: SCAN_DONE (0x85) 이어서

PC는 응답이 timeout 내 안 오면 재시도 로직 실행.

### 4-2. Timeout 설정 (권장값)

| 상황 | RS232 원본 | UDP 브리지 |
|------|-----------|-----------|
| Point 응답 대기 | 100 ms | **150 ms** (WiFi 지연 고려 +50) |
| Heatmap 완료 대기 | 102 s | **105 s** (여유) |
| ACK 대기 | 500 ms | **500 ms** (WiFi RTT 여유) |

### 4-3. 프레임 손실 대응

UDP 패킷 하나 = 프레임 하나 원칙이므로 손실 감지가 쉽다:
- PC가 응답 timeout → 명령 재전송
- Heatmap 데이터(8576 B) 손실 → SCAN_START부터 재시도

### 4-4. Sequence Number (선택)

측정 정확성이 극도로 중요하다면 프레임에 seq 필드 추가 고려. 다만 base 프로토콜 수정이라 신중히:

**옵션 A:** base 그대로 두고 seq는 상위(PC-F/W 로직) 필드로 관리.
**옵션 B:** ESP32-PC 간 wrap 프로토콜 도입 (base RS232 프레임 앞에 별도 헤더).

**권장:** **옵션 A** — base 프로토콜 무결성 유지가 더 중요.

---

## 5. Fragmentation 대응 (필요 시)

WiFi/네트워크 환경에 따라 9000 byte 단일 UDP가 문제되면:

### 5-1. Chunk 분할 방식

Base 프로토콜의 프레임을 여러 UDP 패킷으로 나누어 전송 후 재조립.

**추가 헤더 정의 (UDP payload 앞에 6 byte 추가):**

```
┌────────────┬──────────┬──────────┬────────────────────────────┐
│ FRAME_ID   │ CHUNK_NO │ TOTAL    │  Frame Payload (일부)      │
│ 2 B (LE)   │ 1 B      │ 1 B      │  ~1400 byte                │
│ 프레임 ID   │ 이 chunk │ 전체 개수 │  (RS232 프레임 일부)        │
└────────────┴──────────┴──────────┴────────────────────────────┘
```

- FRAME_ID: ESP32/PC가 프레임마다 증가시키는 카운터
- CHUNK_NO: 0-based
- TOTAL: 이 프레임의 전체 chunk 개수
- 재조립 완료 시 base 프레임 파싱

**단점:** 프로토콜 복잡도 상승, ESP32 buffer 관리 부담.

### 5-2. 권장 방식

- **기본:** Jumbo frame(MTU 9000) 지원 네트워크에서 단일 UDP로 전송
- **폴백:** WiFi 특성상 문제 발생 시에만 chunk 방식 도입

---

## 6. ESP32 구현 가이드

### 6-1. Hardware

- **ESP32-WROOM-32** 또는 **ESP32-S3** 권장
- UART pin: TX/RX 핀 F/W와 연결. 3.3V ↔ 5V 레벨 변환 필요 시 MAX3232 등 사용
- Baud: **38400 bps** (base 프로토콜 고정)

### 6-2. Software 구조 (Arduino IDE / ESP-IDF)

```cpp
// 개념 코드 (Arduino IDE 스타일)

#include <WiFi.h>
#include <WiFiUdp.h>

WiFiUDP udp;
IPAddress pcIP;         // PC IP (첫 UDP 수신 시 기록)
uint16_t  pcPort;
const uint16_t LOCAL_PORT = 5000;

// UART 수신 상태 기계 (STX → CMD → SIZE(2B) → DATA → CS)
enum RxState { RX_STX, RX_CMD, RX_SIZE_LO, RX_SIZE_HI, RX_DATA, RX_CS };
RxState rxState = RX_STX;
uint8_t  rxBuf[65540];
uint16_t rxSize = 0;
uint16_t rxCount = 0;
uint16_t rxChk = 0;
uint16_t rxFrameLen = 0;

void setup() {
    Serial.begin(115200);              // 디버그용
    Serial2.begin(38400, SERIAL_8N1);  // F/W와 통신 (UART2)
    WiFi.begin("SSID", "PASSWORD");
    while (WiFi.status() != WL_CONNECTED) delay(500);
    udp.begin(LOCAL_PORT);
}

void loop() {
    handleUDP_to_UART();
    handleUART_to_UDP();
}

// PC → F/W
void handleUDP_to_UART() {
    int size = udp.parsePacket();
    if (size <= 0) return;

    pcIP   = udp.remoteIP();
    pcPort = udp.remotePort();

    uint8_t buf[65540];
    int len = udp.read(buf, sizeof(buf));
    if (len <= 0 || buf[0] != 0x02) return;  // STX 확인

    // UART로 그대로 forward
    Serial2.write(buf, len);
}

// F/W → PC (상태 기계로 프레임 하나씩 파싱)
void handleUART_to_UDP() {
    while (Serial2.available()) {
        uint8_t d = Serial2.read();

        switch (rxState) {
            case RX_STX:
                if (d == 0x02) {
                    rxBuf[0] = 0x02;
                    rxFrameLen = 1;
                    rxChk = 0;
                    rxState = RX_CMD;
                }
                break;

            case RX_CMD:
                rxBuf[rxFrameLen++] = d;
                rxChk = (rxChk + d) & 0xFF;
                rxState = RX_SIZE_LO;
                break;

            case RX_SIZE_LO:
                rxBuf[rxFrameLen++] = d;
                rxChk = (rxChk + d) & 0xFF;
                rxSize = d;
                rxState = RX_SIZE_HI;
                break;

            case RX_SIZE_HI:
                rxBuf[rxFrameLen++] = d;
                rxChk = (rxChk + d) & 0xFF;
                rxSize |= (uint16_t)d << 8;
                rxCount = 0;
                if (rxSize == 0) rxState = RX_CS;
                else             rxState = RX_DATA;
                break;

            case RX_DATA:
                rxBuf[rxFrameLen++] = d;
                rxChk = (rxChk + d) & 0xFF;
                rxCount++;
                if (rxCount >= rxSize) rxState = RX_CS;
                break;

            case RX_CS:
                rxBuf[rxFrameLen++] = d;
                if (d == ((255 - rxChk) & 0xFF)) {
                    // 검증 성공 → UDP로 forward
                    if (pcIP) {
                        udp.beginPacket(pcIP, pcPort);
                        udp.write(rxBuf, rxFrameLen);
                        udp.endPacket();
                    }
                }
                // 실패해도 그냥 다음 STX 대기 (재동기)
                rxState = RX_STX;
                break;
        }
    }
}
```

### 6-3. 성능 고려사항

- **UART DMA 사용 권장** — 8576 byte 연속 수신을 놓치지 않으려면 hardware buffer 확대
- **WiFi 안정성**: 2.4 GHz WiFi가 산업 환경에서 노이즈 취약. 5 GHz 또는 유선 이더넷 shield 고려
- **UDP 송신 buffer**: ESP32 lwIP 기본 UDP send buffer가 작을 수 있음. Jumbo 지원 필요 시 buffer 확장

### 6-4. 에러 케이스

| 상황 | ESP32 동작 |
|------|-----------|
| UART checksum 실패 | 프레임 폐기, 다음 STX 대기 |
| UART 중간 STX 감지 | 이전 상태 리셋, 새 프레임 시작 |
| UDP 재조립 실패 | 그냥 폐기, 상위(PC)가 timeout으로 재시도 |
| WiFi 끊김 | 자동 재접속 시도, 큐 없음 (재접속 중 도착한 UART 데이터는 그냥 forward) |
| PC 미지정 상태 (아직 UDP 수신 전) | UART 수신 데이터 폐기 |

---

## 7. PC 측 변경 사항

### 7-1. 최소 변경 (권장)

기존 `cRS232.RS232Command()` 를 UDP send로 wrapping.

```csharp
// mRS232.cs 수정 (개념)

using System.Net;
using System.Net.Sockets;

public static class cRS232
{
    public static UdpClient udpClient = new UdpClient();
    public static IPEndPoint esp32EndPoint = new IPEndPoint(IPAddress.Parse("192.168.1.100"), 5000);

    public static void RS232Command(int ch, byte inCMD, int size, byte[] inBuf)
    {
        // 기존 프레임 조립 로직 그대로
        byte[] OutData = new byte[size + 5];
        int chksum = 0, add = 0;
        OutData[add++] = 0x02;
        OutData[add++] = (byte)inCMD;
        OutData[add++] = (byte)(size % 256);
        if (REC.Is_2Byte) OutData[add++] = (byte)(size / 256);
        for (int i = 0; i < size; i++) OutData[add++] = inBuf[i];
        for (int i = 1; i < add; i++) chksum = (chksum + OutData[i]) % 256;
        OutData[add++] = (byte)(255 - chksum);

        // ★ UART 대신 UDP로 전송
        udpClient.Send(OutData, add, esp32EndPoint);
    }

    // 수신도 UART SerialPort 대신 UdpClient 사용
    // BeginReceive → OnUdpReceived → CMD_MessageFromDevice 호출
}
```

### 7-2. UDP 수신 처리

```csharp
private static void StartUdpReceive()
{
    udpClient.BeginReceive(OnUdpReceived, null);
}

private static void OnUdpReceived(IAsyncResult ar)
{
    IPEndPoint ep = new IPEndPoint(IPAddress.Any, 0);
    byte[] data = udpClient.EndReceive(ar, ref ep);

    // 기존 파서에 byte stream 밀어넣기
    for (int i = 0; i < data.Length; i++)
    {
        REC.BUF[REC.Head++] = data[i];
        REC.Head %= MaxBufferSize;
    }
    CMD_MessageFromDevice(0);  // 상태 기계로 파싱

    // 다음 수신 대기
    udpClient.BeginReceive(OnUdpReceived, null);
}
```

⚠️ **주의:** UDP 특성상 한 번의 Receive에 완전한 프레임 하나가 도착. ESP32가 프레임 단위로 송신하므로 부분 프레임 처리 걱정 없음.

**최적화 옵션:** ESP32가 완전한 프레임만 UDP로 보내므로 PC 측 상태 기계 없이 바로 CMD dispatch도 가능.

---

## 8. 설정 항목

ESP32 브리지 개발 시 노출해야 할 설정:

| 항목 | 기본값 | 저장 위치 |
|------|--------|-----------|
| WiFi SSID | (사용자 입력) | NVS (ESP32 flash) |
| WiFi Password | (사용자 입력) | NVS |
| ESP32 IP (Static/DHCP) | DHCP | NVS |
| UDP Port | 5000 | 코드 상수 |
| UART Baud | 38400 | 코드 상수 |
| PC IP (whitelist, 선택) | 없음 | NVS |

**초기 설정 방법 (권장):**
- ESP32를 AP 모드로 부팅 (첫 부팅 or 버튼 눌러 리셋)
- 스마트폰/PC로 접속 → 웹 브라우저로 설정 페이지 열기
- WiFi 정보 입력 후 STA 모드로 재부팅

---

## 9. 테스트 시나리오

### 9-1. Loopback 테스트 (ESP32 단독)

ESP32 TX/RX를 직접 연결해 자기 자신에게 echo:
1. PC → UDP 명령 → ESP32 → UART TX → UART RX → UDP 응답 → PC
2. 왕복 데이터 일치 확인

### 9-2. F/W 연결 통합 테스트

1. PC → `SCAN_START` (0x80) UDP 송신
2. ESP32가 UART로 F/W에 forward 확인 (오실로스코프/UART 캡처)
3. F/W → `SCAN_DATA_ALL` (0x86, 8576 B) 응답
4. ESP32가 UDP로 PC에 forward 확인
5. PC 파싱 결과가 F/W 데이터와 일치 확인

### 9-3. 스트레스 테스트

- 100회 연속 Heatmap 측정 → 무손실 확인
- WiFi 신호 약한 환경에서 timeout/재전송 동작 확인
- ESP32 재부팅 시 재접속 확인

---

## 10. Base RS232 프로토콜과의 차이점 요약

| 항목 | RS232 원본 | UDP 브리지 |
|------|-----------|-----------|
| 물리 계층 | Serial, 38400 bps | WiFi UDP |
| 프레임 구조 | STX/CMD/SIZE/DATA/CHKSUM | **동일** |
| Is_2Byte 모드 | true | **true (필수)** |
| CMD 값/의미 | Section 4 (base 문서) | **동일** |
| Payload 최대 | 65535 B | 9000 B (Jumbo) or chunk |
| 신뢰성 | UART hardware 수준 | UDP + app-level ACK |
| Timeout | 100 ms/102 s | +50 ms 여유 |
| 통신 방향 | 반이중, PC master | **동일** |
| 물리 거리 | 케이블 길이 (~15 m) | WiFi 범위 (~수십 m) |

**요약:** base 프로토콜을 그대로 UDP payload로 실어 나르는 투명 브리지. ESP32는 프레임 무결성만 확인하고 forward.

---

## 11. 관련 문서

- `FW_RS232_통신규격.md` — Base 프로토콜 (프레임 포맷, CMD 정의 등). 이 문서와 함께 참조 필수.
- ESP32 공식 문서: WiFi, UDP, UART DMA
- Arduino ESP32 core: `WiFiUdp.h`, `HardwareSerial.h`
