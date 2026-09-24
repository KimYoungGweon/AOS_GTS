# GTS Console 개발 문서

- **작성** 2026-09-21
- **대상** GTS_CONSOLE_ESP32P4 — AOS / GFC 장비를 제어하는 4.3″ 터치 콘솔 펌웨어
- **용도** 새 세션·새 작업자가 이 문서만 읽고 이어서 작업할 수 있도록 정리한 인수인계 문서

---

## 0. 새로 시작하는 사람에게

### 0-1. 먼저 읽을 것

| 순서 | 문서 | 내용 |
| --- | --- | --- |
| 1 | **이 문서** | 전체 그림·결정사항·함정 |
| 2 | `DOC/GTS_Console_Programming.md` | 요구사항 원본 + 진행 표시(✅/⏳/⬜) |
| 3 | `DOC/gts-console-ui-mockups/project/GTS_Console_LCD_Spec.md` | 화면 사양서 Rev 0.2 (좌표·색·폰트) |
| 4 | `DOC/GTS_UDP_Protocol.md` | UDP 통신 규격 Rev 0.1 — **콘솔↔서버의 기준** |
| 5 | `GTS_CONSOLE_ESP32P4/README.md` | 빌드 방법과 설정 함정 |

참고용 (콘솔 작업에 직접 필요하지는 않음):

- `DOC/FW_RS232_Protocol.md` — AOS 장치 내부 STM32 ↔ ESP32 RS232 규격.
  **UDP 프로토콜 설계의 출발점**이므로 값의 단위·타입을 맞출 때 본다.
- `DOC/ESP32_UDP_Bridge_규격.md` — AOS 장치 안의 ESP32 브리지(PC↔RS232). 별도 장치.
- `DOC/udp_server_source.md` — 서버(Ubuntu) 설치·소스·운영.

### 0-2. 첫 빌드

```bash
. $HOME/.espressif/v6.1/esp-idf/export.sh
cd .../AOS_GTS/GTS_CONSOLE_ESP32P4
idf.py build flash monitor
```

> **`idf.py set-target` 은 쓰지 말 것.** sdkconfig 를 지우고 재생성한다.
> 타깃은 이미 `sdkconfig.defaults` 에 있다.

### 0-3. 작업 규칙 (요구사항 7·8번)

- 완료된 사항은 `GTS_Console_Programming.md` 에 ✅ 로 표시한다.
- 추가사항은 번호를 붙여 이어서 적는다.
- **의문사항이 있으면 반드시 질문하고 시작한다.**

---

## 1. 시스템 구성

```
  ┌──────────────┐   UDP 5502    ┌──────────────┐   UDP 5500/5501   ┌──────────┐
  │ GTS Console  │◄─────────────►│  GTS Server  │◄─────────────────►│ AOS / GFC│
  │ ESP32-P4     │  218.147.     │ Ubuntu 24.04 │                   │  장치     │
  │ 4.3" 터치    │  152.41:5502  │ 192.168.0.6  │                   │          │
  └──────────────┘               └──────────────┘                   └──────────┘
        ▲                          Python asyncio
        │ Jog(encoder) + Jog S/W    systemd 서비스
        └─ 외부 입력
```

서버 포트: `5500 AOS` · `5501 GFC` · **`5502 CONSOLE`**

**현재 서버는 수신만 한다.** 받은 패킷을 로그로 남길 뿐 송신 코드가 없다.
그래서 콘솔은 `GTS_OFFLINE_MODE` 로 화면을 먼저 검증하는 중이다 (10절).

---

## 2. 폴더 구조

```
AOS_GTS/
├── DOC/
│   ├── GTS_Console_개발문서.md          ← 이 문서
│   ├── GTS_Console_Programming.md       요구사항 + 진행 표시
│   ├── GTS_UDP_Protocol.md              UDP 규격 Rev 0.1 (기준)
│   ├── FW_RS232_Protocol.md             AOS 내부 RS232 규격
│   ├── ESP32_UDP_Bridge_규격.md         AOS 내부 ESP32 브리지
│   ├── udp_server_source.md             서버 운영
│   └── gts-console-ui-mockups/project/
│       ├── GTS_Console_LCD_Spec.md      화면 사양서 Rev 0.2  ★
│       └── GTS Console LCD.dc.html      목업 원본
├── JC4880P443C_Demo/                    출발점이 된 데모 (동작 검증됨)
└── GTS_CONSOLE_ESP32P4/                 ← 본 프로젝트
    ├── main/                            소스 (아래 4절)
    ├── components/jc4880p443c/          ST7701 초기화 시퀀스 (데모에서)
    ├── managed_components/              LVGL 9.6 등 (데모에서 복사, 재다운로드 불필요)
    ├── test/host/                       PC gcc 로 도는 검증 (11절)
    ├── docs/demo_ref/                   데모 원본 소스 보관 (빌드 제외)
    ├── partitions.csv                   데모와 동일 — 바꾸지 말 것
    ├── sdkconfig.defaults               ★ 모든 필수 설정이 여기 명시됨
    └── README.md
```

---

## 3. 하드웨어 · 빌드 환경 ★ 함정 모음

이 절의 항목은 전부 **한 번씩 실제로 당했던 것**이다. 값이 틀리면 나는 증상을 같이 적었다.

| 항목 | 값 |
| --- | --- |
| MCU | ESP32-P4, **실측 리비전 v1.3** |
| 보드 | JC4880P443C EVM Kit |
| LCD | ST7701, MIPI-DSI 2 lane, **패널 480×800 세로** |
| 논리 화면 | **800×480 가로** (LVGL 270도 회전) |
| 터치 | GT911, I2C (SDA 7 / SCL 8 / RST 22 / INT 21) |
| Jog | 외부 encoder A=30 B=31 SW=29 |
| Flash | 16 MB, **BOYA** |
| PSRAM | 32 MB, HEX 모드 200 MHz |
| CPU | **360 MHz** (기본 400 에서 낮춤) |
| ESP-IDF | **v6.1** |
| LVGL | 9.6 |

### 3-1. 반드시 유지할 sdkconfig

전부 `sdkconfig.defaults` 에 명시되어 있다. 데모는 이 값들이 `sdkconfig` 에만 있고
defaults 에는 빠져 있어서, 복사해 만든 새 프로젝트가 조용히 기본값으로 돌아갔다.

| 설정 | 값 | 빠졌을 때 증상 |
| --- | --- | --- |
| `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` | y | **부팅 거부** (아래 3-2) |
| `CONFIG_ESP32P4_REV_MIN_1` | v0.1 | 〃 |
| `CONFIG_LV_USE_CLIB_MALLOC` | y | 첫 렌더에서 Store access fault (3-3) |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360` | 360 MHz | 400 MHz 로 동작 |
| `CONFIG_PARTITION_TABLE_OFFSET` | 0x9000 | 부트로더 크기 초과로 **빌드 실패** |
| `CONFIG_SPIRAM_MODE_HEX` / `_SPEED_200M` | HEX / 200 MHz | PSRAM 오동작 |
| `CONFIG_SPI_FLASH_SUPPORT_GD_CHIP=n` / `_BOYA_CHIP=y` | BOYA | 플래시 미인식 |
| `CONFIG_ESPTOOLPY_FLASHMODE_QIO` / `_FLASHFREQ_80M` / `_16MB` | | |
| `CONFIG_LV_FONT_MONTSERRAT_*` 12종 | | 링크 에러 |

### 3-2. ★ 칩 리비전 — 가장 찾기 어려운 함정

보드의 P4 는 **v1.3** 인데 IDF 6.1 의 최소 리비전 기본값은 **v3.1** 이다.

`CONFIG_ESP32P4_SELECTS_REV_LESS_V3` 는 menuconfig 의
*Component config → Hardware Settings → Chip revision →
"Select ESP32-P4 revisions <3.0 (No >=3.x Support)"* 항목이고 **기본값이 n** 이다.

Kconfig 상 `ESP32P4_REV_MIN_0 / _1 / _100` 이 모두
`depends on ESP32P4_SELECTS_REV_LESS_V3` 이므로, 이 게이트가 n 이면
리비전 선택지가 v3.0 / v3.1 만 나온다. 이때 `CONFIG_ESP32P4_REV_MIN_1=y` 를
defaults 에 적어 두어도 **보이지 않는 심볼이라 조용히 무시**되고 v3.1 이 박힌다.

> **빌드는 정상 통과하고 부팅만 거부된다.** 에러 메시지가 빌드 로그에 안 남아서
> 원인을 찾기 어렵다.

이 한 줄이 `BOOTLOADER_CPU_CLK_FREQ_MHZ`(90), `REV_MAX_FULL`(199),
`PM_*` 절전 옵션까지 함께 결정한다.

부팅 로그로 확인:

```
I (....) efuse_init: Min chip rev:     v0.1
I (....) cpu_start: cpu freq: 360000000 Hz
```

### 3-3. ★ LVGL 메모리 — CLIB malloc 필수

LVGL 기본값은 내장 고정 풀(`LV_USE_BUILTIN_MALLOC`, `LV_MEM_SIZE` 64 KB)이다.
이 UI 는 화면 5종을 미리 다 만들어 두는 구조라 위젯이 300개쯤 된다.
64 KB 로는 **위젯 생성까지만 통과하고 첫 렌더에서 죽는다.**

```
Guru Meditation Error (Store access fault)
lv_draw_add_task ... lv_draw.c:100   new_task->area = *coords;   ← new_task 가 NULL
```

`lv_malloc()` 의 NULL 을 LVGL 이 검사하지 않고 바로 쓰는 자리라
"메모리 부족"이 아니라 널 포인터 쓰기로 나타난다.
UI 생성이 3~5초로 느려지는 것도 같은 원인(할당기가 꽉 차서 헤맨다).

CLIB malloc 이면 ESP-IDF 힙(내부 RAM + PSRAM 32 MB)을 쓴다.
LVGL 렌더 버퍼는 `main.c` 가 따로 `MALLOC_CAP_DMA` 로 잡으므로 영향 없다.
기동 로그의 `heap before/after UI` 로 여유를 확인할 수 있다.

### 3-4. 플래시 배치 (데모와 동일 — 바꾸지 말 것)

| 영역 | 주소 | 크기 |
| --- | --- | --- |
| bootloader | 0x02000 | 28 KB |
| partition table | **0x09000** | 4 KB |
| nvs | 0x0A000 | 24 KB |
| phy_init | 0x10000 | 4 KB |
| factory app | 0x20000 | 약 15.9 MB |

기본 0x8000 으로 두면 IDF 6.1 부트로더(0x6140)가 한도 0x6000 을 넘어
`Bootloader binary size ... is too large for partition table offset 0x8000` 로 빌드가 멈춘다.

### 3-5. iCloud 주의

작업 폴더가 iCloud Drive 에 있다.

- **`build/` 는 동기화하지 말 것.** 377 MB · 2800+ 파일이고 절대경로가 박혀 있다.
  자리를 옮기기 전에 `idf.py fullclean`.
  "Mac 저장 공간 최적화"가 build 파일을 클라우드로 내보내면 ninja 가 엉뚱한 에러를 낸다.
- **이름 뒤에 " 2" 가 붙은 것은 iCloud 충돌 사본이다.**
  `managed_components/` **안쪽에** `lvgl__lvgl 2/` 같은 게 생기면 IDF 가 중복
  컴포넌트로 인식할 수 있으니 반드시 지운다.
- 클라우드에만 있고 다운로드되지 않은 파일은 읽을 때 `Resource deadlock avoided` 가 난다.
  Finder 에서 한 번 열어 받아두면 된다.

---

## 4. 펌웨어 구조

`main/` (약 4,500 줄)

| 파일 | 줄 | 역할 |
| --- | --- | --- |
| `main.c` | 422 | MIPI DSI · ST7701 · GT911 · LVGL 초기화, 화면 회전, 태스크 기동 |
| `gts_config.h` | 133 | **핀·UDP·Wi-Fi·타이밍·모드 스위치 전부 여기** |
| `gts_rotate.[ch]` | 123 | 렌더 버퍼 픽셀 회전 (LVGL 비의존 → 호스트 테스트 대상) |
| `gts_protocol.[ch]` | 674 | UDP 프레임 조립/해석, CRC16, 수신 태스크, keep-alive |
| `gts_state.[ch]` | 536 | 전역 상태, jog 적용·clamp, 조그바 문자열 |
| `gts_input.[ch]` | 236 | Encoder 4상 디코딩, Jog S/W step 순환, 50 ms 송신 디바운스 |
| `gts_sim.[ch]` | 106 | 오프라인 테스트용 장비 시뮬레이션 |
| `wifi_manager.[ch]` | 375 | 등록 지점 목록, 논블로킹 접속, 스캔, NVS 최근 접속 |
| `gts_theme.[ch]` | 342 | 사양서 색·폰트 토큰과 위젯 헬퍼 |
| `gts_ui.[ch]` | 506 | 상단바·조그바·화면 전환·100 ms 갱신 타이머 |
| `gts_ui_p1.c` | 233 | P1 Device Select |
| `gts_ui_p2.c` | 207 | P2 GFC Mode |
| `gts_ui_p3.c` | 147 | P3 AOS 가스 Data 측정 |
| `gts_ui_p4.c` | 191 | P4 AOS Manual |
| `gts_ui_p5.c` | 244 | P5 WiFi 접속 List |

### 4-1. 스레드 모델

| 태스크 | 하는 일 |
| --- | --- |
| `lvgl` (core 1, prio 4) | LVGL 렌더 + 100 ms UI 갱신 타이머 + 터치 이벤트 |
| `gts_net` (prio 5) | UDP 수신 전담, 프레임 해석 → 상태 갱신 |
| `gts_ping` (prio 4) | 1초 keep-alive, 연속 3회 무응답 시 P1 복귀 |
| `gts_enc` (prio 6) | 1 ms Jog 폴링, 값 적용, 50 ms 디바운스 후 송신 |
| Wi-Fi 이벤트 | 접속·스캔 결과를 상태에 기록 |

**규칙 두 가지:**

1. 상태는 `g_gts` 하나로 모으고 **재귀 뮤텍스**로 보호한다.
   다른 태스크는 상태만 바꾸고 dirty 플래그를 세우며,
   **LVGL 호출은 LVGL 태스크의 갱신 타이머에서만** 일어난다.
2. 락 순서는 항상 **`tx_lock → state_lock`**. 역전 없음.

### 4-2. ★ 화면 회전

패널은 480×800 세로, 사양서는 800×480 가로다. 방향은 한 줄로 바꾼다.

```c
/* gts_config.h */
#define GTS_ROTATION_DEG   270   /* 화면이 거꾸로면 90 */
```

90 과 270 은 정확히 180도 반대다. 보드를 케이스에 어느 방향으로 끼웠는지에 따라
둘 중 하나가 맞는다. 터치 좌표는 LVGL 이 같은 설정으로 변환하므로 함께 따라온다.

> **LVGL 9.6 은 partial 렌더 모드에서 소프트웨어 회전을 해 주지 않는다.**
> `lv_display_rotate_area()` 로 **영역 좌표만** 바꿔 주고 픽셀은 그대로 넘긴다.
> 그래서 `gts_rotate.c` 가 픽셀을 직접 돌린다.
> **이 두 회전이 반드시 같아야 한다.** 한쪽만 바꾸면 각 부분 갱신이 올바른
> 사각형 안에 엉뚱한 방향으로 들어가 화면이 깨지고 겹쳐 보인다.

그래서 회전 함수를 LVGL 에 의존하지 않는 별도 파일로 떼어 놓고,
`test/host/test_rotate.c` 가 LVGL 의 `lv_display_rotate_area()` 공식을 그대로 옮긴
참조 구현과 **픽셀 단위로 대조**한다 (0/90/180/270 전부, 정렬 패딩·화면 구석 포함).

행 정렬(`LV_DRAW_BUF_STRIDE_ALIGN`)을 무시하면 화면이 어긋나므로
`lv_draw_buf_width_to_stride()` 로 실제 stride 를 받아 쓴다.

---

## 5. 화면 5종

공통 3단 구조. 상단바와 조그바는 **한 번만 만들고 내용만 교체**한다.

| 영역 | 좌표 |
| --- | --- |
| Top bar | 0, 0, 800, 44 |
| Content | 0, 44, 800, 372 (작업영역 12, 56, 776, 348) |
| Jog bar | 0, 416, 800, 64 |

### 5-1. 화면 전이

```
  [P5] WiFi List ──── 연결 성공 ────▶ [P1] Device Select
        ▲                                   │  ENTER CONTROL (연결 성공 시에만 활성)
        └── WiFi 미연결 시 자동 전환 ────────┤    ※ P1 에서만 자동 전환
             (상단바 Wi-Fi 표시를 누르면      ├─ dev_type = GFC ─▶ [P2] GFC Mode
              어느 화면에서든 P5 로)          └─ dev_type = AOS ─▶ [P4] AOS Manual ◀──▶ [P3] 가스측정
                                                                    (기본 진입)
  [P2] 내부 모드 전환 : 상단 MANUAL / AUTO 탭
  [P2·P3·P4] 조그바 "DEVICE / 장치 변경" ─▶ 연결 해제 후 [P1]
  모든 화면 ─ UDP 응답 3회 없음 ─▶ [P1] + NO REPLY
```

### 5-2. 화면별 요약

| 화면 | 내용 | 조그 대상 |
| --- | --- | --- |
| **P1** Device Select | AOS/GFC 선택, ID 5×4 그리드, Connection Test 카드 6행, RE-TEST / ENTER CONTROL | DEVICE ID (1↔20 순환) |
| **P2** GFC Mode | MANUAL/AUTO 탭, 시간 타일 2종(0.1–60 sec), 사이드 카드(카운트다운/펌프 상태), AUTO START·STOP / PUMP ON·OFF | 선택된 시간 타일 (Manual 에선 비활성) |
| **P3** AOS 가스측정 | 타입 카드 4종(preTest/1h/2h/8h), Elapsed·Remaining·진행바·샘플 수, START/STOP | DATA TYPE (4종 순환) |
| **P4** AOS Manual | 제어 타일 6종 3×2, LF_MODE 토글, SHAPE 순환 | 선택 타일 값, **STEP 배지 표시** |
| **P5** WiFi List | 등록 지점 3개 행, Selected 카드, CONNECT/DISCONNECT, SCAN | NETWORK (지점 순환) |

### 5-3. P4 제어 6종

| 위치 | 항목 | 범위 | step (Jog S/W 순환) | 표시 |
| --- | --- | --- | --- | --- |
| 1-1 | HV | 0 – 200 V | 0.01 / 0.1 / 1 | 소수 2자리 |
| 1-2 | Frq | 200 – 800 kHz | 0.1 / 1 / 10 | 소수 1자리 |
| 1-3 | Duty | 10 – 50 % | 0.01 / 0.1 | 소수 2자리 |
| 2-1 | CV | −5 – 5 V | 0.001 / 0.01 / 0.1 | 소수 3자리 |
| 2-2 | LF_Frq | 50 – 200 Hz | 1 | 정수 |
| 2-3 | LF_Volt | 0 – 5 V | 0.01 / 0.1 | 소수 2자리 |

`LF_Mode` Off 면 LF_Frq·LF_Volt 타일이 50% 로 비활성되고 jog 도 무시된다.

### 5-4. 사양서와 다르게 간 곳 (의도적)

| # | 내용 | 이유 |
| --- | --- | --- |
| 1 | `LF_Shape` 순환 버튼 추가 (250,352,180,50), 안내 박스를 438 부터로 축소 | 요구사항엔 있는데 목업 P4 에 타일이 없음 |
| 2 | P3 타입 카드 4번째 x 599 → 597 | 폭 187·간격 8 로 균일 배치 |
| 3 | 폰트 13/15/19/21px → Montserrat 14/16/20/20px | 가용 크기 중 가장 가까운 값 |
| 4 | 상단바 MyID 칩 x 138 → 200, 화면명 237 → 298 | Brand 를 26px 로 키웠는데 Montserrat 이 Barlow Condensed 보다 넓어 148px 에 안 들어감 |
| 5 | 상단바 Wi-Fi 표시를 누르면 P5 로 | 사양서에 P5 진입 버튼이 없음 |
| 6 | 사양서 §9 대신 `GTS_UDP_Protocol.md` 사용 | 8-1 참고 |

### 5-5. 폰트

사양서는 Barlow / Barlow Condensed 지만 **LVGL 내장 Montserrat** 으로 대체했다
(14·16·18·20·22·24·26·28·34·38·40·48). `gts_theme.h` 의 `GF_*` 매크로만 바꾸면
변환한 Barlow 로 교체된다.

> **한글 라벨은 기본 꺼져 있다.** Montserrat 에 한글 글리프가 없어 그대로 쓰면 □ 가 나온다.
> Pretendard / Noto Sans KR 를 **사용 문자만 서브셋**해 13·14·15·16px 로 변환한 뒤
> `gts_config.h` 의 `GTS_USE_KR_FONT` 를 1 로 바꾸고 `gts_theme.h` 의 폰트 포인터를 교체한다.

---

## 6. Jog 입력

데모에서 검증된 4상 디코딩을 그대로 쓴다 (1 ms 폴링).

- **Jog 회전** = 현재 화면의 선택 항목을 step 만큼 증감. 범위 clamp, 선택형은 순환.
- **Jog S/W** = 선택 항목의 step 순환 (P4 에서만 의미 있음).
- **송신 디바운스 50 ms** — 연속 회전 중에는 보내지 않고, 손을 뗀 뒤 최종값 1회만 보낸다.
  화면은 디바운스와 무관하게 즉시 갱신된다.
- P1 에서는 지점 대신 **CONNECT** 가 나간다 (사양서 5.1 "선택하면 테스트 커넥션").

부동소수 누적 오차를 막으려고 값은 매번 표시 자리수로 반올림한다
(0.01 을 100번 더해도 정확히 1.00).

---

## 7. 전역 상태

`gts_state.h` 의 `gts_state_t g_gts` 하나. 사양서 7절 표를 그대로 옮긴 것이다.

공통: `page` `my_id` `dev_type` `dev_id` `link` `fail_count` `rtt_ms`
P1: `dev_state` `dev_fw_ver` `dev_uptime_s` `last_reply_ms` `connect_ok`
P2: `gfc_mode` `gfc_sel` `gfc_start_ds` `gfc_cycle_ds` `gfc_auto_run` `gfc_pump_on` `gfc_remain_ds` `gfc_cycle_count`
P3: `aos_type` `aos_running` `aos_done` `aos_elapsed_s` `aos_total_s` `aos_samples`
P4: `param[6]` `param_sel` `lf_on` `lf_shape`
P5: `wifi_state` `wifi_sel` `wifi_conn_idx` `wifi_last_idx` `wifi_last_time`
`wifi_found[]` `wifi_rssi_list[]` `wifi_scanning` `wifi_scan_found` `wifi_scan_time` `wifi_ip` `wifi_rssi`

GFC 시간은 **0.1초 단위 정수**(deciseconds, 1~600)로 들고 있다. 반올림 오차 회피.

---

## 8. UDP 프로토콜

**기준 문서: `DOC/GTS_UDP_Protocol.md` Rev 0.1**

```
┌─────┬──────┬──────┬─────┬────────┬────────┬───────────┬────────┐
│ STX │ TYPE │  ID  │ CMD │ SIZE_L │ SIZE_H │ DATA[SIZE]│ CRC16  │
│0x02 │ 1 B  │ 1 B  │ 1 B │  1 B   │  1 B   │  SIZE B   │ 2 B LE │
└─────┴──────┴──────┴─────┴────────┴────────┴───────────┴────────┘
```

- CRC-16/MODBUS, **STX 제외** `TYPE ~ DATA` 구간, LE 전송
- **DATA 선두 4 byte 는 항상 공통 헤더** `src_type, src_id, seq(u16 LE)`
  → payload 없는 명령도 SIZE=4, 전체 12 byte
- device type: `0x01` AOS · `0x02` GFC · `0x03` CONSOLE
- 응답은 요청 cmd 에 `0x80` 을 OR
- 실수 값은 **IEEE-754 float**

주요 CMD: `CONNECT 0x01` / `DISCONNECT 0x02` / `PING 0x03` /
GFC `0x20~0x23` / AOS 측정 `0x30~0x31` / AOS manual `0x40~0x43`
서버 push: `GFC_AUTO_STATE 0xA3` · `AOS_MEAS_STATE 0xB1` · `AOS_PARAMS 0xC3`

### 8-1. ★ 디자인 사양서 §9 와의 관계 (2026-09-21 결정)

화면 사양서 Rev 0.2 의 **§9 에도 UDP 초안이 있으나 `GTS_UDP_Protocol.md` 가 기준**이다.
§9 는 대체되었다. 프레임 골격과 CRC 구간은 같고, **cmd 번호와 payload 표현만 다르다.**

| 항목 | 채택 (Rev 0.1) | §9 초안 |
| --- | --- | --- |
| 값 표현 | IEEE-754 float | 정수 스케일 (0.01 V 등) |
| DATA 헤더 | `src_type, src_id, seq` 4 byte | 없음 |
| GFC cmd | 0x20 ~ 0x23 | 0x10 ~ 0x13 |
| AOS 측정 | 0x30/0x31, 통지 0xB1 | 0x20/0x21, 통지 0xA1 |
| AOS param | 0x40, 통지 0xC3 | 0x30, 통지 0xB0 |
| ACK 규칙 | cmd \| 0x80 확정 | "확정 필요" 미결 |

이유:

1. **float 가 RS232 규격과 그대로 맞는다.** `FW_RS232_Protocol.md` 의
   `CMD_TWIN_SCAN_START`(0x80) / `CMD_TWIN_MEASURE_POINT`(0x82) 가
   HV·Frq·Duty·CV·LFV 를 float 로 받는다. 정수 스케일이면 서버가 매 패킷마다
   변환해야 하고 스케일 표를 양쪽에서 따로 관리하게 된다.
2. **`seq` 가 UDP 에 필요하다.** 응답이 유실되거나 순서가 바뀔 때,
   `seq` 가 없으면 늦게 도착한 이전 응답을 최신 것으로 오인한다. §9 에는 없다.
3. **이미 구현·검증되어 있다.** 호스트 테스트가 매번 대조한다.

### 8-2. ★ 서버가 구현해야 할 송신 (현재 없음)

| CMD | 없으면 |
| --- | --- |
| `CONNECT_ACK` 0x81 | **ENTER CONTROL 이 안 열림** (오프라인 모드로 우회 중) |
| `PONG` 0x83 | 링크 끊김 판정 → P1 복귀 |
| `GFC_AUTO_STATE` 0xA3 | P2 카운트다운·펌프 상태 정지 |
| `AOS_MEAS_STATE` 0xB1 | P3 경과시간·진행바·자동 Stop 정지 |
| `AOS_PARAMS` 0xC3 | P4 진입 시 장비 현재값 동기화 안 됨 |

서버 소스와 수정 절차는 `DOC/udp_server_source.md` 11·17절 참고.

### 8-3. 타이밍

| 상수 | 값 |
| --- | --- |
| `GTS_REPLY_TIMEOUT_MS` | 500 |
| `GTS_RETRY_MAX` | 3 (CONNECT/DISCONNECT 만) |
| `GTS_PING_PERIOD_MS` | 1000 |
| `GTS_LINK_FAIL_COUNT` | 3 → P1 복귀 |
| `GTS_JOG_DEBOUNCE_MS` | 50 |

`SET_*` 계열은 재전송하지 않는다. 유실되면 다음 조작이 덮어쓰고 주기 통지가 실제 상태를 되돌린다.

---

## 9. Wi-Fi

접속 장소가 2~3곳 고정이라 스캔·비밀번호 입력 대신 **등록 목록**을 쓴다.
`gts_config.h`:

```c
#define GTS_WIFI_NET_COUNT  3
#define GTS_WIFI_0_NAME  "GwangGyo (1006)"   /* OFFICE_SSID */
#define GTS_WIFI_1_NAME  "GwangGyo (103B)"   /* OFFICE_SSID_2   */
#define GTS_WIFI_2_NAME  "Home"       /* YOUR_SSID_3 */
```

> **순서를 바꾸지 말 것.** NVS 에 저장되는 "최근 접속 지점"이 **인덱스**다.
> 지점을 추가할 때는 뒤에 붙이고 기존 순서는 그대로 둔다.
>
> **비밀번호가 소스에 들어간다.** 외부에 넘길 때는 비울 것.

- **NVS 저장 범위(결정)**: 마지막 성공 지점 인덱스 + 접속 시각만.
  SSID/PW 는 소스. 부팅하면 그 지점부터 시도하고 P5 에 "최근 접속 HH:MM" 으로 표시.
  시각은 SNTP(`GTS_WIFI_SNTP`)로 맞추며, 아직이면 시각 없이 "최근 접속" 만 나온다.
- **논블로킹.** 연결을 기다리지 않고 돌아오며 결과는 이벤트 핸들러가 상태에 쓴다.
  (블록하면 P5 에서 지점을 고르는 동안 UI 가 멎는다.)
- **자동 전환은 P1 에서만(결정).** P2~P4 작업 중에는 화면을 뺏지 않고
  상단바에 `NO WIFI` 만 표시한다 — 측정·펌프 운전 중 화면이 바뀌면 곤란하기 때문.
  상단바 Wi-Fi 표시를 누르면 어느 화면에서든 P5 로 간다.

---

## 10. 오프라인 테스트 모드

서버 송신이 없어도 P2~P5 를 다 돌아볼 수 있게 한 **임시 발판**이다.

```c
/* gts_config.h */
#define GTS_OFFLINE_MODE     1    /* 서버 송신이 붙으면 0 */
#define GTS_OFFLINE_SPEEDUP  20   /* 시간 배속. 1 이면 실시간 */
```

| | 1 일 때 |
| --- | --- |
| CONNECT | 응답 없이 연결 성공 처리 → ENTER CONTROL 활성 |
| keep-alive | 무응답이어도 P1 으로 안 되돌림 |
| P2 GFC Auto | 10분 주기 카운트다운·펌프 ON/OFF·사이클 수 자체 진행 |
| P3 AOS 측정 | 경과/남은 시간·진행바·샘플 수, 종료 시 자동 Stop 까지 재현 |
| P1↔P5 자동 전환 | **끔** (벤치에 Wi-Fi 가 없을 때 P5 에 갇히는 것 방지) |
| 상단바 | `OFFLINE` + 노란 표시등 |

**송신은 평소대로 나간다** — CONNECT / PING / SET_* 가 전송되므로 서버 수신부를
함께 확인할 수 있다.

시뮬레이션은 `gts_sim.c` 하나에 격리했고 `GTS_OFFLINE_MODE 0` 이면 통째로 빈 함수가 된다.
**배포 전 반드시 0 으로.** 상단바의 OFFLINE 표시가 그것을 상기시킨다.

배속 20배면 preTest(5분) 약 15초, 1 hour 약 3분.

---

## 11. 검증

### 11-1. 하드웨어 없이 도는 호스트 테스트

```bash
./test/host/run.sh
```

FreeRTOS·ESP-IDF 를 `test/host/stub/` 의 최소 헤더로 대체해 PC gcc 로 컴파일한다.

| 대상 | 검사 내용 |
| --- | --- |
| `gts_protocol.c` | CRC-16/MODBUS 표준 검사값(0x4B37), **규격 문서의 예시 프레임 바이트 일치**, 왕복, 손상·길이 이상 프레임 거부, payload 경계 |
| `gts_state.c` | clamp·순환·step, 부동소수 누적 오차, LF 잠금, 조그바 표시 문자열 |
| `gts_rotate.c` | **LVGL `lv_display_rotate_area()` 공식과 픽셀 단위 대조** (0/90/180/270, stride 패딩, 화면 구석) |

### 11-2. 실기 확인 순서

1. 부팅 로그 `Min chip rev: v0.1` / `cpu freq: 360000000 Hz`
2. `LVGL display 800x480 (panel 480x800, rotated 270 deg)`
3. `heap after UI: internal ...` 로 여유 확인
4. 화면 방향과 터치 좌표 일치
5. Jog 회전·S/W

---

## 12. 진행 상황 (2026-09-21)

| 단계 | 상태 |
| --- | --- |
| 빌드 · 부팅 · LCD · 터치 · 회전(270도) | ✅ |
| P1 Device Select · Jog | ✅ |
| P2 / P3 / P4 | ⏳ 구현 완료, 오프라인 모드로 검증 예정 |
| P5 WiFi | ⏳ 신규 구현, 미확인 |
| UDP 서버 연동 | ⬜ 서버는 **수신만**, 송신 없음 |

호스트 테스트 전부 통과. 전 소스 LVGL 9.6 헤더로 `-Wall -Wextra` 경고 0.

### 남은 일

1. 오프라인 모드로 P2~P5 레이아웃·동작 검증
2. 서버 UDP 송신 구현 → `GTS_OFFLINE_MODE 0`
3. 한글 폰트 서브셋 변환 후 `GTS_USE_KR_FONT 1`
4. 실기 시인성 확인 후 폰트 크기 조정
5. 화면 깜빡임·잔상이 보이면 `main.c` 의 `.num_fbs = 2` 를 1 로
   (더블 버퍼 + partial 렌더 조합)

### 알려진 사소한 문제

- 기동 로그 `W: gpio: conflict found for GPIO[21]` — 터치 INT 핀. 데모에도 동일, 영향 없음.

---

## 13. 설정 스위치 한눈에 (`gts_config.h`)

| 매크로 | 현재 | 의미 |
| --- | --- | --- |
| `GTS_ROTATION_DEG` | 270 | 화면 방향. 거꾸로면 90 |
| `GTS_OFFLINE_MODE` | **1** | 서버 없이 화면 검증. **배포 전 0** |
| `GTS_OFFLINE_SPEEDUP` | 20 | 오프라인 시간 배속 |
| `GTS_USE_KR_FONT` | 0 | 한글 라벨. 서브셋 폰트 넣은 뒤 1 |
| `GTS_MY_ID_DEFAULT` | 1 | 콘솔 자기 ID |
| `GTS_UDP_SERVER_IP` / `_PORT` | 218.147.152.41 / 5502 | |
| `GTS_WIFI_*` | 3지점 | 순서 고정 |
| `GTS_WIFI_SNTP` | 1 | 접속 시각 표시용 |

---

## 부록 A. 하루 동안 겪은 문제와 해결 (재발 방지)

| # | 증상 | 원인 | 해결 |
| --- | --- | --- | --- |
| 1 | `Bootloader binary size 0x6140 is too large for partition table offset 0x8000` | 데모의 `PARTITION_TABLE_OFFSET=0x9000` 이 `sdkconfig.defaults` 에 없어 기본 0x8000 으로 되돌아감 | defaults 에 명시 |
| 2 | 빌드는 되는데 **계속 리부팅** | 같은 이유로 `ESP32P4_SELECTS_REV_LESS_V3` 가 빠져 이미지 최소 리비전이 v3.1 로 박힘 (보드는 v1.3) | 게이트 옵션을 defaults 에 명시. `REV_MIN_1` 만 적으면 **조용히 무시**됨 |
| 3 | 첫 렌더에서 `Store access fault` (`lv_draw_add_task`) | LVGL 내장 고정 풀 64 KB 고갈. `lv_malloc` NULL 을 검사 없이 사용 | `LV_USE_CLIB_MALLOC=y` |
| 4 | 화면이 뒤집혀 `ROTATION_270` 으로 바꿨더니 **버튼이 깨지고 겹침** | 픽셀 회전 함수가 90도로 하드코딩. LVGL 의 영역 좌표 회전과 어긋남 | `gts_rotate.c` 로 분리해 4방향 구현 + 호스트 테스트로 LVGL 공식과 대조 |
| 5 | ENTER CONTROL 이 계속 비활성 | 서버가 `CONNECT_ACK` 을 안 보냄 (수신만 구현) | `GTS_OFFLINE_MODE` 추가 |

**공통 교훈:** 데모의 menuconfig 설정이 `sdkconfig` 에만 있고 `sdkconfig.defaults` 에는
없었던 것이 1·2번의 원인이었다. 이 프로젝트는 **모든 필수 설정을 defaults 에 이유와 함께
적어 두었으므로**, `sdkconfig` 를 지우거나 다른 PC 로 옮겨도 재현된다.

## 부록 B. 색상 토큰 (사양서 3절)

| 역할 | HEX | 사용처 |
| --- | --- | --- |
| bg | `#F2F2F3` | 화면 바탕 |
| text | `#1D1F20` | 기본 글자 |
| accent | `#5980A6` | 선택 채움, 주 버튼, 게이지 |
| accent-100 | `#EEF6FF` | 선택 타일 배경 |
| accent-700 | `#416180` | 강조 수치, 눌림 피드백 |
| accent-900 | `#1D2D3D` | 상단바 바탕, ON 상태 버튼 |
| neutral-100 | `#F5F5F8` | 조그바 바탕 |
| neutral-200 | `#E7E7EA` | 비활성 배지 |
| neutral-300 | `#D4D4D7` | 게이지 트랙 |
| neutral-600 | `#7A7A7D` | 보조 텍스트 |
| divider | `#D0D0D1` | 1px 테두리·구분선 |
| MyID chip | `#3F4C5A` | 상단바 칩 |
| state OK | `#9FE0B4` | LINK 표시등 |
| state FAIL | `#E0A39F` | NO REPLY 표시등 |
| sim | `#E8C44A` | OFFLINE 표시등 (구현 추가) |

테두리는 모두 1px, 선택 상태는 2px accent, **radius 0**, 그림자 없음.
비활성 요소는 불투명도 40~50%.
