# AOS_H753_V1_Single 펌웨어 수정 사항 정리

> 작업일 2026-09-22 · 대상 `DOC/AOS_H753_V1_Single` (구 `DOC/FAIMs_H753_V1_6_1_TWIN`)
> 목적: **Twin(2셀 동시) → Single(단일셀)** 전환에 맞춰 heatmap 측정 경로를 정리하고,
> UDP 업로드가 가능한 형태로 페이로드를 줄인다.
> 상태: **빌드 통과. 실장비 검증 전.**

---

## 0. 한눈에

| | 구 | 신 |
|---|---|---|
| 명령 접두사 | `CMD_TWIN_*` | **`CMD_HMAP_*`** |
| 측정 채널 | Air+/Gas+ **2ch 동시** | **Is_P 1ch** (Air 는 별도 run) |
| HV / Frq | `u16` | **`float`** |
| `SCAN_START` | 40 byte | **44 byte** |
| heatmap 1장 (16×11) | 712 byte | **360 byte** |
| legacy 라인 전송 `0x81` | 있음 | **삭제** |
| 측정 버퍼 | `line_all_buf[11][15][2]` ⚠️ | `map_buf[21][51]` |

**같이 고친 버그 2건** — 3절. 둘 다 배열 밖 접근이었다.

---

## 1. 프로젝트 개명

`DOC/FAIMs_H753_V1_6_1_TWIN` → **`DOC/AOS_H753_V1_Single`**

프로젝트 안에 이름이 **네 가지** 섞여 있어 전부 통일했다.

| 위치 | 구 이름 | 신 이름 |
|---|---|---|
| 폴더 | `FAIMs_H753_V1_6_1_TWIN` | `AOS_H753_V1_Single` |
| `.project` `<name>` | `FAIMs_H753_V1_5_2` | `AOS_H753_V1_Single` |
| `.cproject` `refreshScope` | `FAIMs_H753_2nd` | `AOS_H753_V1_Single` |
| `AOS_H753_V1_Single.launch` | `FAIMs_H753_V1_5_1` | `AOS_H753_V1_Single` |
| `.ioc` `ProjectName` / `ProjectFileName` | `FAIMs_H753_V1_6_1_TWIN` | `AOS_H753_V1_Single` |
| `*.launch` 파일명 | `FAIMs_H753_V1_6_1_TWIN*` | `AOS_H753_V1_Single*` |

**남겨둔 것**

- `FAIMs_H753_V1_6_1 Debug.launch` — 존재하지 않는 프로젝트를 가리키는 잔재. 지워도 무방
- `Debug/` (96 MB) — 옛 경로가 박힌 생성물. **CubeIDE 에서 Project → Clean 후 재빌드 필요**

---

## 2. 명령 개명 및 프로토콜 변경

### 2-1. 명령 코드

값은 **0x80~0x86 그대로 유지**했다. 실제 `command.h` 를 확인한 결과 이 대역에 다른 명령이
없었기 때문이다 (`CMD_AD7739_RESET` / `CMD_BIAS_ONOFF` 는 **0x90 / 0x91**).
`FW_RS232_Protocol.md` 가 경고하던 "0x80 충돌" 은 **문서가 낡은 것**이었다.

| 값 | 구 | 신 | 방향 | 크기 |
|---|---|---|---|---|
| `0x80` | `CMD_TWIN_SCAN_START` | **`CMD_HMAP_START`** | →F/W | 40 → **44 byte** |
| `0x81` | `CMD_TWIN_SCAN_DATA` | **삭제** | — | — |
| `0x82` | `CMD_TWIN_MEASURE_POINT` | **`CMD_HMAP_POINT_REQ`** | →F/W | 22 → **26 byte** |
| `0x83` | `CMD_TWIN_POINT_DATA` | **`CMD_HMAP_POINT_DATA`** | F/W→ | 5 → **12 byte** |
| `0x84` | `CMD_TWIN_SCAN_ABORT` | **`CMD_HMAP_ABORT`** | →F/W | 0 byte |
| `0x85` | `CMD_TWIN_SCAN_DONE` | **`CMD_HMAP_DONE`** | F/W→ | 8 byte (동일) |
| `0x86` | `CMD_TWIN_SCAN_DATA_ALL` | **`CMD_HMAP_DATA`** | F/W→ | 8 + noCV·noLFV·**2** |

상수도 함께 개명했다. `TWIN_LFV_POINTS_MAX` → `HMAP_LFV_POINTS_MAX` (51),
`TWIN_CV_LINES_MAX` → `HMAP_CV_LINES_MAX` (21).

### 2-2. `CMD_HMAP_START` (0x80) — 40 → 44 byte

**HV·Frq 가 `u16` → `float`.** 이게 이번 페이로드 변경의 핵심이다.

| off | size | type | field |
|---|---|---|---|
| 0 | 4 | **float** | HV |
| 4 | 4 | **float** | Frq |
| 8 | 4 | float | Duty |
| 12 | 1 | u8 | LF_Waveform |
| 13 | 1 | u8 | reserved |
| 14 | 2 | u16 | LFF |
| 16 | 2 | u16 | delay_ms |
| 18 | 4 | float | LFV_Start |
| 22 | 4 | float | LFV_Stop |
| 26 | 4 | float | LFV_Step |
| 30 | 4 | float | CV_Start |
| 34 | 4 | float | CV_Stop |
| 38 | 4 | float | CV_Step |
| 42 | 1 | u8 | SectionY |
| 43 | 1 | u8 | SectionX |

> **왜 float 이어야 하는가**
> `HV_List` 에 `95.55556`, `Frq_List` 에 `266.6667` 같은 값이 있다. `u16` 으로 받으면
> **95, 266 으로 잘린다.**
> 게다가 펌웨어 내부는 `MData.SET.RF_HV`, `.Frq`, `.Duty` 가 **원래부터 전부 `float`** 였다.
> 즉 **수신 파싱 단계에서만 정밀도를 버리고 다시 float 로 승격**하고 있었다.

### 2-3. `CMD_HMAP_POINT_REQ` (0x82) — 22 → 26 byte

`START` 와 필드 순서를 맞췄다 (구 코드는 duty 가 뒤쪽에 있어 순서가 달랐다).

| off | size | type | field |
|---|---|---|---|
| 0 | 4 | float | HV |
| 4 | 4 | float | Frq |
| 8 | 4 | float | Duty |
| 12 | 1 | u8 | LF_Waveform |
| 13 | 1 | u8 | reserved |
| 14 | 2 | u16 | LFF |
| 16 | 2 | u16 | delay_ms |
| 18 | 4 | float | LFV |
| 22 | 4 | float | CV |

### 2-4. `CMD_HMAP_POINT_DATA` (0x83) — 12 byte

| off | size | type | field |
|---|---|---|---|
| 0 | 1 | u8 | status (0=OK) |
| 1 | 1 | u8 | reserved |
| 2 | 2 | u16 | **Is_P** (ADC raw) — 구 2ch → 1ch |
| 4 | 4 | float | LFV (실제 적용값) |
| 8 | 4 | float | CV (실제 적용값) |

실제 적용된 LFV/CV 를 되돌려주므로 **메뉴얼 모드 실시간 표시**에 바로 쓸 수 있다.

### 2-5. `CMD_HMAP_DATA` (0x86) — heatmap 1장 ★

**헤더 첫 2 byte 를 규격대로 정정했다.**
구 코드는 `done_status`, `0` 을 넣었으나 규격은 `SectionY`, `SectionX` 다.
1,600장이 연속으로 올라올 때 **어느 heatmap 인지 식별하는 유일한 토큰**이라 서버에 꼭 필요하다.

**Header 8 byte**

| off | size | type | field |
|---|---|---|---|
| 0 | 1 | u8 | **SectionY** (구: done_status) |
| 1 | 1 | u8 | **SectionX** (구: 0) |
| 2 | 2 | u16 | noCV |
| 4 | 2 | u16 | noLFV |
| **6** | **1** | **u8** | **HeatmapY** (구 reserved) — Section 안의 LFF 인덱스 |
| **7** | **1** | **u8** | **HeatmapX** (구 reserved) — Section 안의 Duty 인덱스 |

> **Heatmap 좌표를 reserved 자리에 넣었다.** Section 좌표만으로는 heatmap 을 특정할 수
> 없다 — 한 Section 안에 Duty×LFF = 16장이 있기 때문이다. 1,600장이 연속으로 올라오는
> run 모드에서 서버가 `cond_idx` 를 복원하려면 네 좌표가 모두 필요하다.

**Data 영역** — `for iy = 0..noCV-1 { for ix = 0..noLFV-1 }`, point 당 **2 byte**

| off | size | type | field |
|---|---|---|---|
| +0 | 2 | u16 | **Is_P** (ADC raw) |

구 4 필드(`Is_Air_P`/`Is_Air_N`/`Is_Gas_P`/`Is_Gas_N`, 8 byte)에서 **1 필드로 축소**.
`Is_*_N` 두 채널은 **샘플 데이터 실측 결과 전부 0** 이었고 (64,000 / 320 point 전수 확인),
Air 는 이제 별도 run 으로 측정하므로 동시에 보낼 이유가 없다.

**크기**

| 격자 | 구 (2ch) | 신 (1ch) |
|---|---|---|
| 16×11 (실사용) | 712 byte | **360 byte** |
| 51×21 (규격 최대) | 4,292 byte | 2,150 byte |

> **UDP 관점** — 360 byte 는 **1 datagram 에 그대로 들어간다.**
> IP 단편화·재조립·부분손실 처리가 통째로 불필요해진다. 이번 축소의 실익은 여기다.
>
> **UART 관점 (38400 baud)** — 0.185 s → 0.094 s. 장당 0.09 s, 1,600장 기준 **약 147초(2.5분)**.
> 측정 자체가 장당 17.4 s 이므로 전체 소요시간에 미치는 영향은 **1% 미만**이다.
> (UART 전송이 병목이었던 적은 없다. 규격 기본 격자 51×21 을 썼다면 달랐겠지만 쓰지 않았다.)

---

## 3. 같이 고친 버그 2건 ★

### 3-1. 측정 버퍼 overrun — 실데이터 손상

**구 선언**

```c
u16 line_all_buf[11][15][2];   // CV=11 은 맞지만 LFV 가 15
```

**쓰기** (`Twin.c`) 와 **읽기** (`UART_PC.c` 0x86 생성부) 양쪽 모두 LFV 를 **0..15**,
즉 **16개**로 돌았다. 배열 2번째 차원은 15 이므로 `[i][15]` 는 **`[i+1][0]` 의 메모리**다.

클램프는 `TWIN_LFV_POINTS_MAX(51)` 로 하고 있어 **실제 배열을 전혀 보호하지 못했다.**

**증상** — 각 행의 마지막 값이 다음 행의 첫 값으로 나온다.

```
0.00319 ... 0.00425      ← 행1 끝
0.00425 ... 0.03398      ← 행2 시작이 행1 끝과 같음
0.03398 ... 0.18899
```

**보유 샘플에서 100% 재현**

| 파일 | `row[i][15] == row[i+1][0]` |
|---|---|
| `air_lavender_4x4_...dat` (full8) | **64,000 / 64,000 (100.00%)** |
| `Alcohol_air2_Sample_...dat` (fast) | **320 / 320 (100.00%)** |

**결과**

- **LFV 16번째 점(LFV=3.0V)은 한 번도 저장된 적이 없다.** 측정은 했으나 다음 행 자리에 쓰였다가 덮였다
- 모든 행의 **마지막 열이 가짜**다
- **마지막 행(cv=10)은 배열 밖**을 읽어 `cur_line_index`, `point_status` 등 인접 필드가 데이터로 나갔다

**수정**

```c
u16 map_buf[HMAP_CV_LINES_MAX][HMAP_LFV_POINTS_MAX];   // = [21][51]
...
if (lfv_count > HMAP_LFV_POINTS_MAX) lfv_count = HMAP_LFV_POINTS_MAX;
if (cv_count  > HMAP_CV_LINES_MAX)   cv_count  = HMAP_CV_LINES_MAX;
```

**클램프 상수와 배열 차원이 같은 이름을 쓰도록** 묶었다. 이게 원인이었으므로 재발하지 않는다.

> 이 손상 때문에라도 기존 `.dat` 샘플은 신규 데이터와 섞으면 안 된다.
> DB 설계에서 `data_origin='legacy_import'` 로 격리하기로 한 결정과 맞는다.

### 3-2. 송신 버퍼 overrun — 잠복

`UART_PC.c` 의 송신 버퍼는 **`u8 SOut[4000 + 10]`** 이다.
구 코드로 **규격 기본 격자(51×21, 2ch)** 를 쓰면 `8 + 51·21·4 = 4,292 byte` 를 쓰게 되어
**4,010 byte 버퍼를 넘긴다.** 실제로는 16×11 만 써서 우연히 살아 있었다.

**수정** — 1채널 전환으로 최대치가 `8 + 21·51·2 = 2,150 byte` 가 되어 안전해졌고,
앞으로 상수를 올려도 빌드가 막히도록 **컴파일 타임 가드**를 넣었다.

```c
#define HMAP_DATA_PAYLOAD_MAX  (8 + HMAP_CV_LINES_MAX * HMAP_LFV_POINTS_MAX * 2)
typedef char hmap_payload_fits_in_SOut[(HMAP_DATA_PAYLOAD_MAX <= 4000) ? 1 : -1];
```

---

## 4. 측정 격자 설정을 F/W 로 이관 (신규)

구 PCSW 는 격자를 PC 가 들고 있었다 (`Source/MyCTL/mStruct_B.cs`).
`cTwin_Conf` 의 상수와 `__tw_scan(filter, dutyMode, lffMode)` 생성자가
Filter Type 별 HV Min/Max 를 균등분할해 `HV_List` 를 만들어 냈다.

이제 **`GTS_DB_Design.md` 6항의 값을 F/W 의 default 로 박고**, 필요하면
대시보드에서 바꿔 **EEPROM 에 저장**한다. 실제로 바뀔 값은 사실상 **HV 뿐**이다.

### default 값 (`Hmap_Cfg.c`)

| 축 | 개수 | 값 |
|---|---|---|
| Frq | 10 | 200 ~ 800 kHz **균등분할** (200, 266.67, …, 800) |
| **HV** | 10 | **45, 60, 75, 90, 105, 120, 135, 150, 165, 180** |
| Duty | 4 | 50, 55, 60, 65 |
| LFF | 4 | 50, 100, 150, 200 |
| LFV | 16 | 0 ~ 3.0 (0.2 step) |
| CV | 11 | −1 ~ +1 (0.2 step) |

> **1Hour 모드는 Full 격자를 3칸 간격으로 솎은 것**이다.
> HV·Frq 모두 index `0, 3, 6, 9` → HV `45/90/135/180`, Frq `200/400/600/800`.
> `GTS_DB_Design.md` 7항과 정확히 일치한다. 그래서 별도 리스트를 두지 않고
> `HMAP_HOUR1_STEP 3` 하나로 표현한다.
>
> 장수: Full `10×10×4×4 = 1,600` / 1Hour `4×4×4×4 = **256**` (약 74분).
> 설계문서 7항에 Duty·LFF 축이 빠져 있어 그동안 불명확했던 부분이다.

### 저장 — Flash 아님, 기존 SPI EEPROM

`EEPROM.c` 의 byte 단위 R/W 를 그대로 쓴다. 설정이 240 byte 라 부담이 없다.

| EEPROM 주소 | 용도 |
|---|---|
| 0x0010 / 0x0200 / 0x0600 / 0x0800 | FRQ_CAL / HV_DAC / HV_VS / HV_IS |
| 0x0A00 / 0x0C00 / 0x0D00 / 0x1000 | CV_DAC / FAN_VS / BIAS_VS / SHALLOW_2D |
| **0x4000** | **`__hmap_cfg` (신규, 240 byte)** |

`magic('HMA1') + version + size + chksum` 으로 검증한다.
비어 있거나 깨졌으면 **조용히 default 로 되돌린다** — 첫 부팅도 이 경로를 탄다.
`System_Init()` 의 `EEPROM_INIT()` 직후 `Hmap_Cfg_Init()` 에서 1회 로드한다.

### 신규 명령 (0x87~0x8A)

| 값 | 이름 | 방향 | 내용 |
|---|---|---|---|
| `0x87` | `CMD_HMAP_CFG_QUERY` | →F/W | 0 byte. 현재 설정 요청 |
| `0x88` | `CMD_HMAP_CFG` | F/W→ | **240 byte** 설정 1벌 |
| `0x89` | `CMD_HMAP_CFG_SET` | →F/W | 240 byte. RAM 반영 (EEPROM 미기록) |
| `0x8A` | `CMD_HMAP_CFG_SAVE` | →F/W | 0 byte. RAM 설정을 EEPROM 에 기록 |

`SET`·`SAVE` 는 **응답으로 항상 `CMD_HMAP_CFG` 를 돌려준다.** 호스트는 회신값을 보고
반영 여부를 판단한다 — 검증에 걸려 거부돼도 현재 값이 오므로 **조용한 실패가 없다.**
**측정 진행 중에는 둘 다 거부**한다 (현재 값만 회신).

### `__hmap_cfg` 바이트 배치 (ARM LE, 총 240 byte)

서버·대시보드가 같은 배치를 써야 하므로 그대로 남긴다. **내부 padding 없음.**

| off | size | type | field |
|---|---|---|---|
| 0 | 4 | u32 | `magic` = `0x484D4131` ('HMA1') |
| 4 | 2 | u16 | `version` = 1 |
| 6 | 2 | u16 | `size` = 240 |
| 8 | 1 | u8 | `no_sx` (Frq, 기본 10) |
| 9 | 1 | u8 | `no_sy` (HV, 기본 10) |
| 10 | 1 | u8 | `no_hx` (Duty, 기본 4) |
| 11 | 1 | u8 | `no_hy` (LFF, 기본 4) |
| 12 | 1 | u8 | `no_lfv` (기본 16) |
| 13 | 1 | u8 | `no_cv` (기본 11) |
| 14 | 2 | u16 | `delay_ms` (기본 100) |
| 16 | 40 | float×10 | `frq_list` |
| **56** | **40** | **float×10** | **`hv_list`** ← 실질적으로 이것만 바뀜 |
| 96 | 16 | float×4 | `duty_list` |
| 112 | 16 | float×4 | `lff_list` |
| 128 | 64 | float×16 | `lfv_list` |
| 192 | 44 | float×11 | `cv_list` |
| 236 | 2 | u16 | `chksum` — offset 0~235 의 u8 합 |
| 238 | 2 | — | tail padding |

---

## 5. 측정 순회 엔진을 F/W 로 이관 (신규)

구 PCSW 는 **PC 가 1,600번 `0x80` 을 쐈다** (`TWIN_Coarse_Map_Find()`).
이제 F/W 가 스스로 돈다. **통신이 끊겨도 측정이 계속된다.**

### 반드시 상태기계여야 하는 이유 ★

`Hmap_Scan_Run()` 은 heatmap 한 장(약 17초) 동안 메인 루프를 잡는다.
8시간을 한 함수 안에서 돌면 **그동안 ABORT·STATUS 명령이 하나도 처리되지 않는다.**
그래서 `Hmap_Run_Step()` 은 **한 장만 측정하고 곧바로 복귀**한다.

```
메인 루프 ─ else if (g_hmap_run.state == HMAP_RUN_RUNNING) Hmap_Run_Step();
              │
              ├ abort 요청 → ABORTED, 0x85 송신
              ├ pause 요청 → PAUSED  (진행 상태 보존)
              ├ index >= total → DONE, 0x85 송신
              ├ Bias 타이머 1시간 경과 → Hmap_Bias_Reset()
              └ index 조건 적재 → Hmap_Scan_Run() → 0x86 송신 → index++
```

### 순회 순서 — 구 PC 코드와 동일

```
for sY { for sX { for hY { for hX { 1장 } } } }      // hX 가 가장 안쪽
cond_idx = ((sy*no_sx + sx)*no_hy + hy)*no_hx + hx
```

`cond_idx` 정의가 **DB 설계 문서와 같다.** 서버가 0x86 의 네 좌표로 그대로 복원한다.

### 1Hour = Full 격자를 3칸 간격으로 솎기

별도 리스트를 두지 않고 `sec_step` 하나로 처리한다.

| 모드 | `no_sx × no_sy` | `sec_step` | 실제 값 | 장수 |
|---|---|---|---|---|
| Full | 10 × 10 | 1 | HV 45~180 전부 | 1,600 |
| 1Hour | 4 × 4 | **3** | HV 45/90/135/180, Frq 200/400/600/800 | **256** |
| Sample | — | — | 서버가 내려준 N세트 | N |

### `Bias_Reset` — 1시간 주기 ★

구 PCSW 는 `sY` 행마다 호출했다 (원래 1시간 주기 의도였으나 주석 처리된 상태).
**요구대로 1시간 주기로 바로잡았다.**

```c
if ((u32)(HAL_GetTick() - r->t_bias) >= HMAP_BIAS_PERIOD_MS) { ... }
```

뺄셈으로 비교하므로 `HAL_GetTick()` 이 49.7일마다 wrap 해도 안전하다.
동작은 구 PCSW `sMain_Comm.cs:1093` 과 같다 — **Bias ON → 3초 → OFF.**
run 시작 시 1회 실행하고 거기서부터 타이머를 잡는다.

### 신규 명령 (0x8B~0x8F)

| 값 | 이름 | 방향 | 페이로드 |
|---|---|---|---|
| `0x8B` | `CMD_HMAP_RUN_START` | →F/W | `u8 mode` (0=Full 1=1Hour 2=Sample) |
| `0x8C` | `CMD_HMAP_RUN_CTRL` | →F/W | `u8 action` (0=abort 1=pause 2=resume) |
| `0x8D` | `CMD_HMAP_RUN_QUERY` | →F/W | 0 byte |
| `0x8E` | `CMD_HMAP_RUN_STATUS` | F/W→ | **20 byte** |
| `0x8F` | `CMD_HMAP_SAMPLE_SET` | →F/W | `u8 count, u8 rsv`, count×`{hv,frq,duty,lff}` float |

**네 명령 모두 응답으로 `CMD_HMAP_RUN_STATUS` 를 돌려준다.** 수락 여부를 status 로
판단하므로 조용한 실패가 없다 — 설정 명령(0x89/0x8A)과 같은 원칙이다.

**`RUN_START` 는 재개도 겸한다.** `PAUSED` 상태에서 같은 mode 로 다시 부르면
`index` 를 보존한 채 이어서 간다. 8시간짜리를 처음부터 다시 하지 않는다.

### `CMD_HMAP_RUN_STATUS` 배치 (20 byte)

| off | size | field |
|---|---|---|
| 0 | 1 | `state` (0=idle 1=running 2=paused 3=done 4=aborted) |
| 1 | 1 | `mode` |
| 2 | 2 | `total` |
| 4 | 2 | `done_count` |
| 6 | 2 | `index` — 다음에 측정할 `cond_idx` |
| 8~11 | 4 | `sy`, `sx`, `hy`, `hx` |
| 12 | 4 | `elapsed_ms` |
| 16 | 1 | `scan_running` |
| 17 | 1 | `sample_count` |
| 18 | 2 | reserved |

### Sample 모드 — F/W 에는 파일시스템이 없다

구 PCSW 는 `c:\FTLAB\FAIMs\FAIMs_Twin_Sample.dat` 에서 세트를 읽었다.
F/W 는 그럴 수 없으므로 **측정 직전에 서버가 `0x8F` 로 내려준다.**
RAM 에만 들고 있고 (`HMAP_SAMPLE_MAX 16`), 세트 관리는 서버·DB 가 맡는다.

---

## 6. 파일별 변경

| 파일 | 변경 |
|---|---|
| `Core/Src/Header/command.h` | `CMD_TWIN_*` → `CMD_HMAP_*`, `0x81` 삭제, `HMAP_*_MAX` 로 개명 |
| `Core/Src/Header/MyCTL.h` | `__twin`→`__hmap`, `MData.TWIN`→`MData.HMAP`, `FLAG.TWIN_*`→`FLAG.HMAP_*`, **버퍼 교체**, hv/frq **float**, `point_adc[4]`→스칼라, `line_buf[51][4]` 삭제, `#include "command.h"` 추가 |
| `Core/Src/Header/twin.h` | **삭제** → `Core/Src/Header/hmap.h` 신규 |
| `Core/Src/MyWork/Twin.c` | **삭제** → `Core/Src/MyWork/Hmap.c` 신규 (재작성) |
| `Core/Src/MyWork/UART_PC.c` | `#include`, 수신 디스패처, `0x83`/`0x85`/`0x86` 빌더 교체. `0x81` 경로 및 주석처리된 죽은 코드 삭제. `//Device TWIN` → `//Device AOS (Single)` |
| `Core/Src/MyWork/MyCTL.c` | `#include`, 메인 루프 `Twin_Scan_Run`/`Twin_Point_Run` → `Hmap_Scan_Run`/`Hmap_Point_Run`. `System_Init()` 에 `Hmap_Cfg_Init()` 추가 |
| `Core/Src/MyWork/Hmap_Cfg.c` | **신규** — 격자 default, EEPROM 저장·복원, 검증 |
| `Core/Src/MyWork/Hmap_Run.c` | **신규** — 측정 순회 상태기계, Bias 1시간 타이머, Sample 세트 |

### 함수 개명

| 구 | 신 |
|---|---|
| `Twin_OnReceive_ScanStart` | `Hmap_OnReceive_Start` |
| `Twin_OnReceive_MeasurePoint` | `Hmap_OnReceive_PointReq` |
| `Twin_OnReceive_ScanAbort` | `Hmap_OnReceive_Abort` |
| `Twin_Scan_Run` | `Hmap_Scan_Run` |
| `Twin_Point_Run` | `Hmap_Point_Run` |
| `Twin_CMD_Send_CTL` | `Hmap_CMD_Send_CTL` |
| `twin_measure_2ch` | `hmap_measure_1ch` (static) |
| `apply_common` / `set_lfv` / `rd_*_le` | 동명, **static 으로 전환** (외부 참조 없음 확인) |
| `Twin_Scan_Line_Run` | **삭제** (정의가 주석처리된 상태였음) |

---

## 7. 검증

| 항목 | 결과 |
|---|---|
| 빌드 | **통과** |
| `Core` 전체 `twin`/`Twin`/`TWIN` 잔존 | 설명 주석 외 **없음** |
| 제거·변경 심볼의 외부 참조 | **없음** (`apply_common`, `set_lfv`, `rd_*_le`, `lines_done`, `Twin_*` 전수 확인) |
| `MData.HMAP.*` 사용 필드 28개 | **전부 `__hmap` 에 존재** |
| 괄호 균형 (`{}`/`()`/`[]`) | 편집 파일 6개 **모두 0** |
| 송신 버퍼 한도 | 최대 2,150 / 4,000 byte — **여유 있음** |

**아직 안 한 것: 실장비 동작 검증.**

---

## 8. 확인이 필요한 항목

### 8-1. `HMAP_IS_P_CHANNEL` — 측정 채널 선택 ★

**구 Twin 하드웨어의 채널 배치** (확인됨)

| `MCP3202_Read_2ch_Avg()` | 셀 |
|---|---|
| `result[0]` | **Air** (reference cell) |
| `result[1]` | **Gas** (sample cell) |

```c
// hmap.h
#define HMAP_CH_AIR              0
#define HMAP_CH_GAS              1

#define HMAP_IS_P_CHANNEL        HMAP_CH_GAS
```

Twin 은 두 셀을 동시에 읽었지만 **Single 은 셀이 하나**다.
Air run 이든 Target run 이든 **같은 셀로 측정하고 흘리는 가스만 바꾼다.**
따라서 시료가 흐르는 **sample cell(구 Gas 채널)** 을 기본값으로 두었다.

> ⚠️ **실제 배선 확인 필요.** Single 장비가 구 Air 셀 쪽 배선을 살려 뒀다면
> 이 한 줄을 `HMAP_CH_AIR` 로 바꾸면 된다. 벤치에서 가스를 흘리며
> 값이 반응하는 쪽을 보면 바로 판별된다.

MCP3202 는 DOUT1/DOUT2 두 라인을 한 번에 읽는 구조라 **기존 2ch API 를 그대로 두고
채널 선택만 뽑아냈다.** 하드웨어 동작은 바뀌지 않았다.

### 8-2. LF_Waveform 적용

`Hmap.c` 의 `apply_common()` 에 구 코드부터 남아 있던 TODO 가 그대로다.

```c
// ⚠️ LF_Waveform 적용 (본인 환경에 맞게 추가)
```

현재 `eSquare(6)` 고정으로 동작 중이므로 당장 문제는 없다 (인수인계 E 시리즈 참조).

### 8-3. 벤치 점검표

- [ ] `0x80` 44 byte 수신 → `lfv_count=16`, `cv_count=11` 로 계산되는지
- [ ] HV 에 `95.55556` 을 보내면 **95.55556** 으로 적용되는지 (구: 95)
- [ ] `0x86` 페이로드가 **360 byte** 로 나오는지
- [ ] `0x86` 헤더 첫 2 byte 가 요청 시 보낸 **SectionY/SectionX 와 같은지**
- [ ] **LFV 16번째 점(3.0V)이 실제 값으로 들어오는지** ← 3-1 버그 수정 확인
- [ ] **각 행의 마지막 값이 다음 행 첫 값과 다른지** ← 3-1 버그 수정 확인
- [ ] `0x84` ABORT 가 스윕 중간에 걸리는지
- [ ] `0x82`/`0x83` 단일 point 왕복
- [ ] **가스를 흘렸을 때 값이 반응하는지** ← `HMAP_IS_P_CHANNEL` 채널 선택 확인 (8-1)
- [ ] `0x87` 로 설정을 읽으면 **240 byte** 가 오고 HV 가 `45~180` 인지 (첫 부팅 = default 경로)
- [ ] `0x89` 로 HV 를 바꾸고 `0x87` 로 다시 읽어 반영되는지
- [ ] `0x8A` 저장 후 **전원을 껐다 켜도** 바뀐 HV 가 유지되는지
- [ ] 측정 중 `0x89`/`0x8A` 가 거부되는지 (현재 값만 회신)
- [ ] `0x8B` mode=1(1Hour) 로 시작 → `0x8D` status 의 `total` 이 **256** 인지
- [ ] `0x8B` mode=0(Full) → `total` 이 **1600** 인지
- [ ] 측정 중 `0x8D` STATUS 가 **응답하는지** ← 상태기계가 제대로 복귀하는지 확인
- [ ] `0x8C` action=1(pause) → 현재 장 끝나고 멈추고, `0x8B` 로 **이어서** 재개되는지
- [ ] `0x8C` action=0(abort) → 즉시 중단되고 `0x85` 가 오는지
- [ ] `0x86` 헤더의 `sy/sx/hy/hx` 가 순회 순서대로 증가하는지 (hx 가 가장 빨리)
- [ ] 1시간 경과 시 Bias 펄스가 나가는지 (타이머를 짧게 줄여 시험)

---

## 9. 후속 작업

| 순서 | 대상 | 내용 |
|---|---|---|
| 1 | `DOC/FW_RS232_Protocol.md` | 이 문서 2절 내용 반영. **낡은 기술 3건 정정** — 0x80 충돌 오기, SCAN_START 40/44 byte, POINT_DATA 크기 |
| 2 | `gts_aos_bridge/` | UART `0x86` 수신 → UDP 중계. 브리지 명령 대역 `0x40~0x4F` 에 heatmap 코드 추가 |
| 3 | `server/udp_server.py` | heatmap 수신·파싱 → DB. **`test_packets.py` 회귀 먼저** |
| 4 | DB | `DOC/GTS_DB_Design/schema.sql` 적용, `heatmap.payload_fmt=0`(u16 raw) 경로 연결 |

관련 문서

- `DOC/GTS_DB_Design/GTS_DB_Design_Proposal_v1.md` — DB 설계 (v1.3)
- `DOC/GTS_인수인계_20260922.md` 6-4 — 전체 작업 순서
