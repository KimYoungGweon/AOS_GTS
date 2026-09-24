# F/W RS232 통신 규격

> AOS Twin System — PC ↔ F/W (STM32) RS232 통신 프로토콜 명세
> 소스: `Source/MyCTL/mRS232.cs`, `Source/sMain_Comm.cs`, `Source/sMain_TwinSystem.cs`
> 작성일: 2026-07-04 (9차 세션 기준)

---

## 0. 개요

| 항목 | 값 |
|------|-----|
| 물리 계층 | RS232 (Serial) |
| Baud Rate | **38400 bps** |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| Read/Write Timeout | 500 ms |
| Byte 순서 | **Little Endian** (모든 정수·float) |
| 통신 방향 | 반이중 요청-응답 (PC master, F/W slave) |

**단일 채널 설계:** FAIMs 메인 장비 하나만 이 프로토콜을 사용. GasFlowController는 별도 솔루션.

---

## 1. 프레임 포맷

모든 패킷은 아래 프레임 구조를 따릅니다.

```
┌──────┬──────┬──────────────┬─────────────┬─────────┐
│ STX  │ CMD  │ SIZE (1 or 2)│ DATA[SIZE]  │ CHKSUM  │
│ 1 B  │ 1 B  │ 1 B or 2 B   │ SIZE B      │ 1 B     │
└──────┴──────┴──────────────┴─────────────┴─────────┘
```

### 1-1. 필드 상세

| 필드 | 크기 | 값 / 의미 |
|------|------|-----------|
| **STX** | 1 byte | `0x02` — 프레임 시작 마커 |
| **CMD** | 1 byte | 명령 코드 (Section 4 참조) |
| **SIZE** | 1 또는 2 byte | DATA 필드의 바이트 수. **`Is_2Byte` 설정에 따라 결정** |
| **DATA** | SIZE byte | 명령별 payload (없으면 0 byte) |
| **CHKSUM** | 1 byte | `(~sum) & 0xFF` — CMD부터 DATA 끝까지의 합의 1의 보수 |

### 1-2. Is_2Byte 모드

`cRS232.REC.Is_2Byte` 플래그로 SIZE 필드 크기 결정:

- **`Is_2Byte = false`**: SIZE 1 byte (0~255 payload). Legacy 방식.
- **`Is_2Byte = true`** ← **현재 프로젝트 설정** (`frmmain.cs` line 59):
  - SIZE 2 byte, Little Endian (`SIZE_LO`, `SIZE_HI`)
  - 최대 payload **65535 byte**
  - Twin batch 전송(0x86, 약 8.6 KB) 지원을 위해 필수

**⚠️ 중요:** F/W 측에서도 반드시 2-byte SIZE 모드로 파싱해야 함. 1 byte로 파싱하면 8576 byte 패킷 수신 시 프레임 오정렬.

### 1-3. Checksum 계산

**송신 시:**
```c
chksum = 0;
for (i = CMD; i <= DATA[SIZE-1]; i++) {
    chksum = (chksum + byte[i]) % 256;
}
CHKSUM = (255 - chksum) & 0xFF;   // = ~chksum & 0xFF
```

**수신 시 검증:**
```c
수신한 CHKSUM == (255 - 누적_chksum) ?
```

**주의:** STX(0x02)는 checksum 계산에서 **제외**됨.

### 1-4. 예시 프레임

**Command Only (payload 없음, 예: `CMD_SCAN_ABORT` 0x84):**

```
02 84 00 00 7B
│  │  │  │  │
│  │  │  │  └── CHKSUM = ~(0x84 + 0x00 + 0x00) & 0xFF = ~0x84 = 0x7B
│  │  │  └───── SIZE_HI = 0
│  │  └──────── SIZE_LO = 0
│  └─────────── CMD = 0x84 (CMD_TWIN_SCAN_ABORT)
└────────────── STX
```

총 5 byte (Is_2Byte=true 기준). Is_2Byte=false면 4 byte.

---

## 2. 상태 기계 (수신 파서)

`cRS232.CMD_MessageFromDevice()` 함수가 수신 바이트를 상태 기계로 파싱.

```
[CMD_STX] ──────(0x02 수신)───────► [CMD_CMD]
    ▲                                    │
    │                                    ▼
    │                                (CMD 수신)
    │                                    │
    │                                    ▼
    │                              [CMD_SIZE1]
    │                                    │
    │                  Is_2Byte? ────────┤
    │                     │              │
    │                    Yes             No + size=0
    │                     ▼              │
    │                [CMD_SIZE2]         │
    │                     │              │
    │                (size 완성)          │
    │                     │              │
    │       size == 0? ───┤              │
    │            │        ▼              │
    │           Yes      No              │
    │            │        │              │
    │            │        ▼              │
    │            │  [CMD_DATA] ──(모두 수신)──┐
    │            │        │              │      │
    │            └────────┴──────────────┴──────┘
    │                     ▼
    │                 [CMD_CS]
    │                     │
    │              (chksum 검증)
    │                     │
    └─────── success ─────┘
```

**핵심 동작:**
- STX(0x02) 오면 언제나 CMD_CMD 상태로 전환 + chksum 리셋
- SIZE 필드 파싱 후 size=0이면 DATA 스킵하고 바로 CS 상태
- CS 검증 성공 시 handler dispatch, 실패 시 프레임 폐기
- **중간 상태에서 이상한 byte 와도 그냥 진행** — 다음 STX 만나야 리셋

**Timeout 처리:** 코드상 명시적 타임아웃 없음. Twin 측정 함수가 별도로 상위 레벨 폴링 timeout 관리 (100 ms 라인, 51×100×20 ms 완료).

---

## 3. 통신 흐름 (Twin System)

### 3-1. Heatmap 1장 측정 (batch 방식, 현재 표준)

```
PC                                     F/W
 │                                      │
 │──── SCAN_START (0x80) 44 byte ─────►│
 │                                      │  (측정 수행: 21×51 = 1071 point)
 │                                      │
 │◄─── SCAN_DATA_ALL (0x86) 8576 B ────│
 │                                      │
 │──── RECEIVED_OK (0x00) ────────────►│
 │                                      │
 │◄─── SCAN_DONE (0x85) 8 byte ────────│
 │                                      │
 │──── RECEIVED_OK (0x00) ────────────►│  (RECEIVED_OK 자동 발신 아님, 아래 참고)
 │                                      │
```

⚠️ **주의 (dispatcher 코드 기준):**
- `CMD_TWIN_SCAN_DATA` (0x81, legacy)와 `CMD_TWIN_SCAN_DATA_ALL` (0x86) 수신 시 → 자동 `RECEIVED_OK` 반송
- `CMD_TWIN_SCAN_DONE` (0x85) 수신 시 → **자동 반송 없음**
- `CMD_TWIN_POINT_DATA` (0x83) 수신 시 → 자동 `RECEIVED_OK` 반송

### 3-2. 단일 Point 측정 (Hill Climbing 시)

```
PC                                     F/W
 │                                      │
 │──── MEASURE_POINT (0x82) 26 B ─────►│
 │                                      │  (1점 측정)
 │                                      │
 │◄─── POINT_DATA (0x83) 16 byte ──────│
 │                                      │
 │──── RECEIVED_OK (0x00) ────────────►│
 │                                      │
```

### 3-3. Legacy 라인 단위 (0x81, 참고용)

8차 이전 방식. 9차 현재는 batch(0x86)로 대체됐지만 dispatcher는 여전히 보존.

```
PC                                     F/W
 │                                      │
 │──── SCAN_START (0x80) 44 byte ─────►│
 │                                      │
 │◄─── SCAN_DATA (0x81) 412 B [line 0]─│
 │──── RECEIVED_OK ───────────────────►│
 │◄─── SCAN_DATA (0x81) 412 B [line 1]─│
 │──── RECEIVED_OK ───────────────────►│
 │           ...                        │
 │◄─── SCAN_DATA (0x81) 412 B [line n]─│
 │──── RECEIVED_OK ───────────────────►│
 │◄─── SCAN_DONE (0x85) 8 byte ────────│
```

21회 ACK 왕복 → batch로 1회로 축소된 게 8차의 성과.

### 3-4. 중단 (abort)

측정 중 사용자가 중단 요청 시:

```
PC ──── SCAN_ABORT (0x84) 0 byte ────► F/W
```

F/W는 현재 측정 취소 후 SCAN_DONE 발신 여부는 구현에 따름.

---

## 4. Twin System 명령 (Phase 6)

### 4-1. CMD 요약표

| CMD | 이름 | 방향 | Payload | 설명 |
|-----|------|------|---------|------|
| **0x80** | `CMD_TWIN_SCAN_START` | PC→F/W | 44 byte | 측정 시작 요청 |
| **0x81** | `CMD_TWIN_SCAN_DATA` | F/W→PC | 412 byte | 라인 1개 데이터 (legacy) |
| **0x82** | `CMD_TWIN_MEASURE_POINT` | PC→F/W | 26 byte | 단일 point 측정 요청 |
| **0x83** | `CMD_TWIN_POINT_DATA` | F/W→PC | 16 byte | 단일 point 응답 |
| **0x84** | `CMD_TWIN_SCAN_ABORT` | PC→F/W | 0 byte | 측정 중단 |
| **0x85** | `CMD_TWIN_SCAN_DONE` | F/W→PC | 8 byte | 측정 완료 알림 |
| **0x86** | `CMD_TWIN_SCAN_DATA_ALL` | F/W→PC | 8 + noCV·noLFV·8 byte | Heatmap 1장 일괄 (**표준**) |
| **0x00** | `CMD_RECEIVED_OK` | 양방향 | 0 byte | ACK |

### 4-2. `CMD_TWIN_SCAN_START` (0x80) — 44 byte

측정 시작을 F/W에 요청. Section (sY, sX) 하나에 대한 heatmap 1장 측정 파라미터.

| Offset | Size | Type | 필드 | 설명 |
|--------|------|------|------|------|
| 0 | 4 | float | HV | Volt (V), 예: 125.0 |
| 4 | 4 | float | Frq | RF Frequency (kHz), 예: 200.0 |
| 8 | 4 | float | Duty | Duty (%), 예: 55.0 |
| 12 | 1 | uint8 | LF_Waveform | LF 파형 타입 (0/1/... — F/W 정의) |
| 13 | 1 | uint8 | Reserved | 0 padding |
| 14 | 2 | uint16 | LFF | LF Frequency (Hz), 예: 50 |
| 16 | 2 | uint16 | delay_ms | Point 측정 후 대기시간 (ms) |
| 18 | 4 | float | LFV_Start | LFV 시작값 (V) |
| 22 | 4 | float | LFV_Stop | LFV 종료값 (V) |
| 26 | 4 | float | LFV_Step | LFV 스텝 (V) |
| 30 | 4 | float | CV_Start | CV 시작값 (V) |
| 34 | 4 | float | CV_Stop | CV 종료값 (V) |
| 38 | 4 | float | CV_Step | CV 스텝 (V) |
| 42 | 1 | uint8 | SectionY | Section Y index (0 ~ noSY-1) |
| 43 | 1 | uint8 | SectionX | Section X index (0 ~ noSX-1) |

⚠️ **주의:** 이전 명세는 HV/Frq가 uint16 (총 40 byte)이었으나, 정확도를 위해 **float로 변경** (총 44 byte). F/W 파싱 시 offset 재계산 필수.

**Loop 순서 (F/W가 측정 시):** 외부 iy=0..noCV-1, 내부 ix=0..noLFV-1
- `noCV = (CV_Stop - CV_Start) / CV_Step + 1` (기본 21)
- `noLFV = (LFV_Stop - LFV_Start) / LFV_Step + 1` (기본 51)

### 4-3. `CMD_TWIN_SCAN_DATA_ALL` (0x86) — Heatmap 1장 일괄 ★ 표준

**F/W → PC.** Heatmap 1장(LFV × CV) 전체를 한 번에 송신. Payload = **header 8 byte + data (noCV × noLFV × 8) byte**.

#### Header (8 byte)

| Offset | Size | Type | 필드 | 설명 |
|--------|------|------|------|------|
| 0 | 1 | uint8 | SectionY | 요청 시 받은 sectionY 그대로 |
| 1 | 1 | uint8 | SectionX | 요청 시 받은 sectionX 그대로 |
| 2 | 2 | uint16 | noCV | CV 축 point 수 (예: 21) |
| 4 | 2 | uint16 | noLFV | LFV 축 point 수 (예: 51) |
| 6 | 2 | uint16 | Reserved | 0 padding |

#### Data 영역

Loop 순서 (외→내): `for iy = 0..noCV-1 { for ix = 0..noLFV-1 { ... } }`

각 point당 **8 byte:**

| Offset | Size | Type | 필드 |
|--------|------|------|------|
| +0 | 2 | uint16 | Is_Air_P (ADC raw) |
| +2 | 2 | uint16 | Is_Air_N (현재 미사용, F/W는 값 채워야 하지만 PC는 무시하고 0으로 저장) |
| +4 | 2 | uint16 | Is_Gas_P (ADC raw) |
| +6 | 2 | uint16 | Is_Gas_N (현재 미사용, PC는 0으로 저장) |

#### 총 payload 크기

기본값(noCV=21, noLFV=51) 기준: `8 + 21 × 51 × 8 = 8 + 8568 = 8576 byte`.

⚠️ **Is_2Byte=true 필수.** 1 byte SIZE 모드로는 8576 byte 표현 불가.

#### ADC raw → 물리량 변환

PC에서 `mData.adcValue_to_Current(channel, rawValue)` 함수로 float 전류값 변환. F/W는 ADC raw uint16만 송신하면 됨.

- Channel 0: Is_Air_P
- Channel 1: Is_Gas_P
- (N 채널은 현재 미사용 → PC에서 0으로 대체 저장)

### 4-4. `CMD_TWIN_SCAN_DATA` (0x81) — Legacy 라인 방식

**F/W → PC.** 총 **412 byte payload.** Heatmap의 CV 한 라인(LFV 축 전체) 데이터.

| Offset | Size | Type | 필드 |
|--------|------|------|------|
| 0 | 1 | uint8 | LineIndex (CV index, 0 ~ noCV-1) |
| 1 | 1 | uint8 | Reserved / Section 정보 (구현 의존) |
| 2 | 2 | uint16 | PointCount (LFV point 수, ≤ noLFV) |
| 4~ | 408 | uint16 × 4 × PointCount | 각 point당 8 byte (AP/AN/GP/GN) |

**주의:** 9차 시점에서 이 방식은 사용 안 함. PC dispatcher는 여전히 보존 (호환성).

### 4-5. `CMD_TWIN_MEASURE_POINT` (0x82) — 26 byte

**PC → F/W.** 단일 point 측정 요청 (Hill Climbing).

| Offset | Size | Type | 필드 |
|--------|------|------|------|
| 0 | 4 | float | HV (V) |
| 4 | 4 | float | Frq (kHz) |
| 8 | 1 | uint8 | LF_Waveform |
| 9 | 1 | uint8 | Reserved |
| 10 | 2 | uint16 | LFF (Hz) |
| 12 | 2 | uint16 | delay_ms |
| 14 | 4 | float | Duty (%) |
| 18 | 4 | float | LFV (V) |
| 22 | 4 | float | CV (V) |

⚠️ **주의:** 이전 명세는 HV/Frq가 uint16 (총 22 byte)이었으나, 정확도를 위해 **float로 변경** (총 26 byte).

### 4-6. `CMD_TWIN_POINT_DATA` (0x83) — 16 byte

**F/W → PC.** 단일 point 응답.

| Offset | Size | Type | 필드 |
|--------|------|------|------|
| 0 | 2 | uint16 | Is_Air_P (ADC raw) |
| 2 | 2 | uint16 | Is_Air_N (미사용) |
| 4 | 2 | uint16 | Is_Gas_P (ADC raw) |
| 6 | 2 | uint16 | Is_Gas_N (미사용) |
| 8 | 8 | (reserved) | 추가 상태 필드 여유 |

### 4-7. `CMD_TWIN_SCAN_ABORT` (0x84) — 0 byte

**PC → F/W.** Command only. F/W는 진행 중인 측정을 중단.

### 4-8. `CMD_TWIN_SCAN_DONE` (0x85) — 8 byte

**F/W → PC.** Heatmap 1장 측정 완료 신호. Batch(0x86) 송신 후에 이어서 발신.

payload 8 byte 구조는 구현 의존 (상태 코드/경과 시간 등 확장 여지).

### 4-9. `CMD_RECEIVED_OK` (0x00) — 0 byte

**양방향 ACK.** dispatcher가 특정 CMD 수신 시 자동 반송:
- `CMD_TWIN_SCAN_DATA` (0x81) 수신 → 자동 반송
- `CMD_TWIN_SCAN_DATA_ALL` (0x86) 수신 → 자동 반송
- `CMD_TWIN_POINT_DATA` (0x83) 수신 → 자동 반송
- `CMD_TWIN_SCAN_DONE` (0x85) 수신 → **자동 반송 없음**

---

## 5. Legacy 명령 (Twin 이외)

이 명령들은 이전 FAIMs 기능용. Twin 시스템에서는 사용 안 함. 향후 참고용.

### 5-1. RF Amp / 기본 제어

| CMD | 이름 | 방향 | 설명 |
|-----|------|------|------|
| 0x01 | CMD_SET_Volt | PC→F/W | RF 전압 설정 |
| 0x02 | CMD_SET_QUERY | PC→F/W | Set 값 조회 |
| 0x03 | CMD_STATUS_QUERY | PC→F/W | 상태 조회 |
| 0x04 | CMD_AUTO_STATUS_ONOFF | PC→F/W | 자동 상태 보고 ON/OFF |
| 0x10 | CMD_Query_SET | PC→F/W | RF Amp Set 조회 |
| 0x11 | CMD_Query_Set_and_Sense | PC→F/W | Set + Sense 조회 |

### 5-2. CV / Duty / Frequency 제어

| CMD | 이름 | 설명 |
|-----|------|------|
| 0x21 | CMD_CV_SET | CV 값 설정 |
| 0x22 | CMD_CV_STATUS | CV 상태 조회 |
| 0x23 | CMD_CV_TIME_SET | CV 시간 설정 |
| 0x24 | CMD_CURRENT_TYPE | 전류 타입 |
| 0x25 | CMD_ION_SELECTION_SET | Ion 선택 |
| 0x26 | CMD_DUTY_SET | Duty 설정 |
| 0x27 | CMD_FREQUENCY_SET | Frequency 설정 |
| 0x28 | CMD_BIAS_SELECTION | Bias 선택 |
| 0x2A | CMD_SET_CONTROL | 종합 제어 |
| 0x2B | CMD_SET_CONTROL_ALL | 전체 제어 |
| 0x2C | CMD_FAN_SET | Fan 제어 |

### 5-3. Single Scan

| CMD | 이름 | 설명 |
|-----|------|------|
| 0x30 | CMD_SCAN_SET | Scan 설정 |
| 0x31 | CMD_SCAN_START | Scan 시작 |
| 0x32 | CMD_SCAN_RESULT | Scan 결과 |
| 0x33 | CMD_SCAN_STOP | Scan 중단 |

### 5-4. HF/LF Modulation

| CMD | 이름 | 설명 |
|-----|------|------|
| 0x50 | CMD_HF_MOD_SET | HF Modulation 설정 |
| 0x51 | CMD_HF_MOD_QUERY | HF Modulation 조회 |
| 0x52 | CMD_LF_MOD_SET | LF Modulation 설정 |
| 0x53 | CMD_LF_MOD_QUERY | LF Modulation 조회 |

### 5-5. RF Modulation

| CMD | 이름 | 설명 |
|-----|------|------|
| 0x60 | CMD_RF_MOD_CONTROL_SET | RF Mod 제어 (⚠️ Twin의 CMD_SCAN_START_OK와 중복!) |
| 0x61 | CMD_RF_MOD_ONOFF | RF Mod ON/OFF |
| 0x62 | CMD_RF_MOD_SCAN_SET | RF Mod Scan 설정 |
| 0x63 | CMD_RF_MOD_SCAN_ONOFF | RF Mod Scan ON/OFF |

### 5-6. Full Mode Scan

| CMD | 이름 | 설명 |
|-----|------|------|
| 0x70 | CMD_SCAN_FULL_MODE_START | Full mode scan 시작 |
| 0x71 | CMD_SCAN_FULL_MODE_SET | Full mode scan 설정 |
| 0x72 | CMD_SCAN_FULL_MODE_SET_RETURN | Full mode scan 설정 응답 |
| 0x73 | CMD_SCAN_FULL_MODE_RESULT | Full mode scan 결과 |

### 5-7. ADC

| CMD | 이름 | 설명 |
|-----|------|------|
| 0x80 | CMD_AD7739_RESET | ADC 리셋 (⚠️ Twin의 CMD_TWIN_SCAN_START와 **중복!**) |
| 0x81 | CMD_BIAS_ONOFF | Bias ON/OFF (⚠️ Twin의 CMD_TWIN_SCAN_DATA와 **중복!**) |

---

## 6. ⚠️ CMD 코드 충돌 주의

`mRS232.cs` 코드에 **동일한 CMD 값이 다른 이름으로 두 번 정의된 경우** 존재:

| CMD 값 | 이름 (Legacy) | 이름 (Twin/RF) |
|--------|--------------|----------------|
| 0x60 | CMD_RF_MOD_CONTROL_SET | CMD_SCAN_START_OK |
| 0x61 | CMD_RF_MOD_ONOFF | CMD_SCAN_START_OK_RETURN |
| 0x80 | CMD_AD7739_RESET | **CMD_TWIN_SCAN_START** |
| 0x81 | CMD_BIAS_ONOFF | **CMD_TWIN_SCAN_DATA** |

### 영향

**같은 F/W 세션 내에서 두 기능(Legacy Scan + Twin) 을 동시에 활성화하면 충돌 위험.**

현재 프로젝트는:
- **Twin System만 사용** → 0x80/0x81/0x82~0x86은 Twin 의미로만 해석.
- Legacy Single Scan을 병행할 계획이면 CMD 값 재배치 필요.

**권장 F/W 구현:** Twin 세션 진입 시 legacy CMD 핸들러 비활성화, 또는 상위 상태 플래그로 분기.

---

## 7. Timeout / 재시도 정책

`sMain_TwinSystem.cs`에 정의된 상위 레벨 timeout:

| 상수 | 값 | 용도 |
|------|-----|------|
| `_twinReal_LineTimeoutMs` | 100 ms | 라인 단위 응답 대기 (0x81 legacy) |
| `_twinReal_DoneTimeoutMs` | 51 × 100 × 20 = **102,000 ms** (~102초) | Heatmap 완료 대기 |
| `_twinReal_PollMs` | 10 ms | `Application.DoEvents` 폴링 주기 |

**폴링 패턴:** UI thread에서 flag 폴링 + `Application.DoEvents()`. Serial 수신 이벤트가 UI thread에서 fire되므로 다른 방식(AutoResetEvent 등)은 deadlock 위험.

**재시도:** 프로토콜 자체는 재시도 없음. Timeout 시 상위에서 예외 발생 → 사용자에게 알림 → 사용자 재시도.

---

## 8. F/W 구현 시 체크리스트

Twin System F/W 개발 시 반드시 확인:

- [ ] Baud 38400, 8-N-1
- [ ] **Is_2Byte = true 모드** (SIZE 2 byte LE)
- [ ] STX = 0x02, Checksum = `~sum & 0xFF` (STX 제외)
- [ ] Little Endian 정수/float
- [ ] `CMD_TWIN_SCAN_START` (0x80) 수신 → 파라미터 파싱 → 측정 실행
- [ ] 측정 완료 → `CMD_TWIN_SCAN_DATA_ALL` (0x86) **1회** 송신 (약 8.6 KB)
- [ ] 8.6 KB UART 송신 buffer 확보 (chunk 전송 지원)
- [ ] ACK(`RECEIVED_OK` 0x00) 수신 확인
- [ ] `CMD_TWIN_SCAN_DONE` (0x85, 8 byte) 송신
- [ ] `CMD_TWIN_SCAN_ABORT` (0x84) 수신 시 측정 즉시 중단
- [ ] `CMD_TWIN_MEASURE_POINT` (0x82) 수신 → 1 point 측정 → `CMD_TWIN_POINT_DATA` (0x83) 송신
- [ ] Legacy CMD 값 (0x80=AD7739_RESET 등)이 Twin 세션에서 실행되지 않도록 방어

---

## 9. 관련 코드 참조

| 항목 | 위치 |
|------|------|
| CMD 상수 정의 | `mRS232.cs` line 30~123 |
| 프레임 파서 (수신) | `cRS232.CMD_MessageFromDevice` — mRS232.cs line 174 |
| 프레임 송신 (with data) | `cRS232.RS232Command(ch, cmd, size, buf)` — mRS232.cs line 232 |
| 프레임 송신 (command only) | `cRS232.RS232Command(ch, cmd)` — mRS232.cs line 257 |
| Serial 초기화 | `cRS232.Set_RS232` — mRS232.cs line 280 |
| Dispatcher | `sMain_Comm.cs` — CMD별 case 분기 |
| Twin 수신 핸들러 | `RS232_RECEIVE_TWIN_*` — sMain_TwinSystem.cs line 814~860 |
| Twin 송신 함수 | `TwinReal_SendScanStart/Point/Abort` — sMain_TwinSystem.cs line 866~919 |
| Twin batch 파서 | `TwinReal_ParseDataAllIntoHeatmap` — sMain_TwinSystem.cs line 1225 |

---

## 10. 변경 이력 (Twin 관련)

| 시점 | 변경 |
|------|------|
| Phase 6 | Twin 통신 프로토콜 신규 (0x80~0x85, 라인 단위 0x81) |
| Phase 8 (8차) | `CMD_TWIN_SCAN_DATA_ALL` (0x86) batch 방식 추가. 21회 ACK → 1회로 축소 |
| Phase 8 (8차) | `sMain_Comm.cs` dispatcher buffer 4096 → 동적 할당 (batch 8576 byte 대응) |
| Phase 9 (9차) | Mock mode 완전 제거. 이제 Real F/W만. `g_twin_use_mock = false` 고정 |
| Phase 9 (9차) | Heatmap 수신 완료 시 zdScope에 Lime 수직선 (시각적 구분) |
