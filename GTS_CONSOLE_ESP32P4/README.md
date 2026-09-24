# GTS_CONSOLE_ESP32P4

AOS / GFC 장비를 제어하는 4.3″ 터치 콘솔 펌웨어.
ESP32-P4 + JC4880P443C(800 × 480, H mode) + LVGL 9 + UDP.

- 화면 사양: `DOC/gts-console-ui-mockups/project/GTS_Console_LCD_Spec.md` (Rev 0.2, 5화면)
- 통신 규격: `DOC/GTS_UDP_Protocol.md`
- 요구사항: `DOC/GTS_Console_Programming.md`
- 출발점: `JC4880P443C_Demo` (LCD·터치·Jog·UDP 동작 검증 완료된 코드)

---

## 개발 환경 — ESP-IDF v6.1

`JC4880P443C_Demo` 와 같은 환경이다. 데모 폴더를 복사해 만들었고,
데모가 menuconfig 로 맞춰 둔 값은 모두 `sdkconfig.defaults` 에 옮겨 적었다.

```bash
. $HOME/.espressif/v6.1/esp-idf/export.sh
idf.py build flash monitor
```

> **`idf.py set-target` 은 쓰지 말 것.** sdkconfig 를 지우고 다시 만드는데,
> 타깃은 이미 `sdkconfig.defaults` 의 `CONFIG_IDF_TARGET` 으로 정해져 있다.
> 굳이 돌렸더라도 잃을 건 없다 — 설정은 `sdkconfig.defaults` 가 복원하고
> Wi-Fi 접속 정보는 소스(`gts_config.h`)에 있다.

### 반드시 유지해야 하는 설정

데모에서 가져온 값이다. 셋 중 하나라도 빠지면 아래 증상이 난다.

| 설정 | 값 | 빠졌을 때 |
| --- | --- | --- |
| `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` | y | **부팅 거부** (아래 설명) |
| `CONFIG_ESP32P4_REV_MIN_1` | v0.1 | 〃 |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360` | 360 MHz | 기본 400 MHz 로 동작 |
| `CONFIG_PARTITION_TABLE_OFFSET` | 0x9000 | 부트로더 크기 초과로 빌드 실패 |
| `CONFIG_SPIRAM_MODE_HEX` / `_SPEED_200M` | HEX / 200 MHz | PSRAM |
| `CONFIG_SPI_FLASH_SUPPORT_GD_CHIP=n` / `_BOYA_CHIP=y` | BOYA | 플래시 미인식 |
| `CONFIG_ESPTOOLPY_FLASHMODE_QIO` / `_FLASHFREQ_80M` / `_16MB` | | |
| `CONFIG_LV_FONT_MONTSERRAT_*` | 11종 | 링크 에러 |

#### 칩 리비전 — 가장 조심할 곳

이 보드의 ESP32-P4 는 **v1.3** 이다 (데모 부팅 로그 `chip revision: v1.3`).

`ESP32P4_SELECTS_REV_LESS_V3` 는 menuconfig 의
*Component config → Hardware Settings → Chip revision →
"Select ESP32-P4 revisions <3.0 (No >=3.x Support)"* 항목이고 **기본값이 n** 이다.
Kconfig 에서 `ESP32P4_REV_MIN_0 / _1 / _100` 이 모두
`depends on ESP32P4_SELECTS_REV_LESS_V3` 이므로, 이 옵션이 n 이면
최소 리비전 선택지가 **Rev v3.0 / v3.1 만** 나오고 기본 v3.1 이 잡힌다.

이때 `CONFIG_ESP32P4_REV_MIN_1=y` 를 defaults 에 적어 두어도 보이지 않는
심볼이라 **조용히 무시되고**, 이미지 최소 리비전이 v3.1 로 박힌다.
**빌드는 정상 통과하지만 v1.3 칩에서 부팅이 거부된다.**

이 한 줄이 `BOOTLOADER_CPU_CLK_FREQ_MHZ`(90), `REV_MAX_FULL`(199),
`PM_*` 절전 옵션까지 함께 결정한다.

부팅 로그에서 아래 두 줄로 확인한다.

```
I (....) efuse_init: Min chip rev:     v0.1
I (....) cpu_start: cpu freq: 360000000 Hz
```

### 플래시 배치

데모와 동일하다. 바꾸지 말 것.

| 영역 | 주소 | 크기 |
| --- | --- | --- |
| bootloader | 0x02000 | 28 KB |
| partition table | 0x09000 | 4 KB |
| nvs | 0x0A000 | 24 KB |
| phy_init | 0x10000 | 4 KB |
| factory app | 0x20000 | 약 15.9 MB |

파티션 테이블이 기본 0x8000 이 아니라 **0x9000** 인 것에 주의.
0x8000 으로 두면 IDF 6.1 부트로더가 한도 0x6000 을 넘어
`Bootloader binary size ... is too large for partition table offset 0x8000`
으로 빌드가 멈춘다.

### LVGL 메모리 — 반드시 CLIB malloc

`CONFIG_LV_USE_CLIB_MALLOC=y` 가 필요하다. 기본값은 내장 고정 풀
(`LV_USE_BUILTIN_MALLOC`, `LV_MEM_SIZE` 64 KB)인데, 이 UI 는 화면 4종을
미리 다 만들어 두는 구조라 위젯이 250개쯤 된다. 64 KB 로는 위젯 생성까지만
통과하고 **첫 렌더에서 죽는다.**

```
Guru Meditation Error: Core 1 panic'ed (Store access fault)
0x4003e81c in lv_draw_add_task (...) at lv_draw.c:100
100     new_task->area = *coords;      ← new_task 가 NULL (MTVAL=0x8)
```

`lv_malloc()` 이 NULL 을 돌려준 것을 LVGL 이 검사하지 않고 바로 쓰는 자리라,
증상이 "메모리 부족"이 아니라 널 포인터 쓰기로 나타난다. UI 생성이
비정상적으로 느려지는 것(로그상 3~5초)도 같은 원인이다.

CLIB malloc 으로 돌리면 ESP-IDF 힙(내부 RAM + PSRAM 32 MB)을 쓴다.
LVGL 렌더 버퍼는 `main.c` 에서 따로 `MALLOC_CAP_DMA` 로 잡으므로 영향 없다.

기동 로그에 힙 상황을 찍어 두었으니 여유를 확인할 수 있다.

```
I (....) GTS: heap before UI: internal ......, psram ......
I (....) GTS: heap after  UI: internal ......, psram ......
```

### 화면 회전

패널은 480×800 세로다. 사양서는 800×480 가로이므로 LVGL 을 회전시켜 쓴다.
방향은 `gts_config.h` 한 줄로 바꾼다.

```c
#define GTS_ROTATION_DEG   270   /* 화면이 거꾸로면 90 으로 */
```

90 과 270 은 서로 180도 반대다. 보드를 케이스에 어느 방향으로 끼웠는지에
따라 둘 중 하나가 맞는다. 터치 좌표는 LVGL 이 같은 설정으로 변환하므로
이 값만 바꾸면 화면과 터치가 함께 따라온다.

**주의** — LVGL 9.6 은 partial 렌더 모드에서 소프트웨어 회전을 해 주지
않는다. `lv_display_rotate_area()` 로 영역 좌표만 바꿔 주고 픽셀은 그대로
넘긴다. 그래서 `gts_rotate.c` 가 픽셀을 직접 돌린다. **이 두 회전이
반드시 같아야 한다.** 한쪽만 바꾸면 각 부분 갱신이 올바른 사각형 안에
엉뚱한 방향으로 들어가 화면이 깨지고 겹쳐 보인다.

그래서 회전 함수는 LVGL 에 의존하지 않는 별도 파일로 떼어 놓고,
`test/host/test_rotate.c` 가 LVGL 의 `lv_display_rotate_area()` 공식을
그대로 옮긴 참조 구현과 픽셀 단위로 대조한다 (0/90/180/270 전부,
정렬 패딩과 화면 구석 영역 포함).

### Wi-Fi 등록 지점 — 소스에서 고친다

접속 장소가 2~3곳으로 고정이라 스캔·비밀번호 입력 대신 등록 목록을 쓴다.
`gts_config.h`:

```c
#define GTS_WIFI_NET_COUNT  3

#define GTS_WIFI_0_NAME  "Site 1"
#define GTS_WIFI_0_SSID  "YOUR_SSID"
#define GTS_WIFI_0_PASS  "YOUR_PASSWORD"
/* 1번 GwangGyo (103B) · 2번 Vdskim Home … */
```

> **순서를 바꾸지 말 것.** NVS 에 저장된 "최근 접속 지점"이 인덱스이므로,
> 지점을 추가할 때는 뒤에 붙이고 기존 순서는 그대로 둔다.

**최근 접속 지점**은 NVS(`gtswifi` 네임스페이스)에 인덱스와 시각만 남긴다.
SSID/비밀번호는 소스에 있다. 부팅하면 그 지점부터 접속을 시도하고,
P5 목록에 "최근 접속 HH:MM" 으로 표시한다. 시각은 SNTP(`GTS_WIFI_SNTP`)로
맞추며, 아직 안 맞았으면 시각 없이 "최근 접속" 만 나온다.

**주의** — 비밀번호가 소스에 들어간다. 외부에 넘길 때는 비울 것.

Wi-Fi 초기화는 **논블로킹**이다. 연결을 기다리지 않고 돌아오며 결과는
이벤트 핸들러가 `g_gts.wifi_*` 에 쓴다. (예전처럼 블록하면 P5 에서 지점을
고르는 동안 UI 가 멎는다.)

### P5 WiFi 화면과 자동 전환

- **P1 에서만** Wi-Fi 끊김 시 P5 로 자동 전환, P5 에서 연결되면 P1 로 복귀.
  P2~P4 작업 중에는 화면을 뺏지 않고 상단바에 `NO WIFI` 만 표시한다 —
  측정이나 펌프 운전 도중 화면이 바뀌면 곤란하기 때문.
- 어느 화면에서든 **상단바의 Wi-Fi 표시를 누르면 P5 로** 간다. 사양서에는
  P5 로 가는 버튼이 없어서 이 자리를 썼다.
- `GTS_OFFLINE_MODE 1` 이면 자동 전환을 끈다. 벤치에 Wi-Fi 가 없을 때
  P5 에 붙잡혀 P1~P4 를 못 보는 일을 막기 위해서다.

### Wi-Fi 코프로세서

ESP32-P4 에는 Wi-Fi 실리콘이 없어 ESP32-C6 슬레이브를 쓴다.
슬레이브 F/W 빌드 절차는 `firmware-build-esp32-c6-coprocessor.md` 참고 (데모와 동일).

### 오프라인 테스트 모드 — 서버 없이 화면 검증

서버 송신이 아직 없어도 P2/P3/P4 를 다 돌아볼 수 있게 하는 임시 발판이다.
`gts_config.h`:

```c
#define GTS_OFFLINE_MODE     1    /* 서버 송신이 붙으면 0 */
#define GTS_OFFLINE_SPEEDUP  20   /* 시간 배속. 1 이면 실시간 */
```

**1 일 때 달라지는 것**

| | 동작 |
| --- | --- |
| CONNECT | 응답을 기다리지 않고 바로 연결 성공 처리 → `ENTER CONTROL` 활성 |
| keep-alive | 무응답이어도 P1 으로 되돌리지 않음 |
| P2 GFC Auto | 10분 주기 카운트다운·펌프 ON/OFF·사이클 수를 콘솔이 자체 진행 |
| P3 AOS 측정 | 경과/남은 시간·진행바·샘플 수 진행, 종료 시 자동 Stop 까지 재현 |
| 상단바 | `OFFLINE` + 노란 표시등 (켜 둔 채 넘어가지 않도록) |
| P1 Link 행 | `OFFLINE MODE - no server reply` |

**송신은 평소대로 나간다.** CONNECT / PING / SET_* 가 그대로 전송되므로
서버 수신부를 함께 확인할 수 있다.

시뮬레이션 코드는 `gts_sim.c` 하나에 몰아 놓았고, `GTS_OFFLINE_MODE` 가
0 이면 통째로 빈 함수가 된다. 서버가 붙으면 이 값만 0 으로 바꾸면 되고
지울 코드는 없다.

`GTS_OFFLINE_SPEEDUP` 은 8시간 측정을 실시간으로 지켜볼 수 없어서 둔 것이다.
기본 20배면 preTest(5분)가 약 15초, 1 hour 가 약 3분에 끝난다.

### 장치 선택 시 자동 연결 테스트

사양서 5.1 의 "선택을 하면 장비와 테스트 컨넥션을 하여 장비상태를 읽어옴"
을 구현했다. P1 에서 장치 종류나 번호를 바꾸면 바로 `CONNECT` 를 보낸다.
Jog 로 번호를 돌릴 때는 `gts_input.c` 의 50 ms 디바운스가 눌러 두므로,
손을 뗀 뒤 최종 번호로 한 번만 나간다. `RE-TEST` 버튼은 같은 일을 수동으로 한다.

### 하드웨어 테스트 없이 로직만 검증

```bash
./test/host/run.sh
```

FreeRTOS·ESP-IDF 를 최소 stub 으로 대체해 `gts_protocol.c` 와
`gts_state.c` 를 PC gcc 로 컴파일하고, CRC16 표준 검사값·규격 문서의
예시 프레임 바이트·clamp/순환/step 동작을 검사한다.

---

## 소스 구성

| 파일 | 역할 |
| --- | --- |
| `main.c` | MIPI DSI · ST7701 · GT911 · LVGL 초기화, 화면 90° 회전, 태스크 기동 |
| `gts_config.h` | 핀·UDP 주소·타이밍 상수 (보드가 바뀌면 여기만) |
| `gts_protocol.[ch]` | UDP 프레임 조립/해석, CRC16, 송수신 태스크, keep-alive |
| `gts_state.[ch]` | 사양서 7절 전역 상태, jog 적용, clamp, 조그바 문자열 |
| `gts_input.[ch]` | 외부 Encoder(Jog) 4상 디코딩, Jog S/W, 50 ms 송신 디바운스 |
| `gts_theme.[ch]` | 사양서 3·4절 색·폰트 토큰과 위젯 헬퍼 |
| `gts_ui.[ch]` | 상단바 · 조그바 · 화면 전환 · 100 ms 갱신 타이머 |
| `gts_ui_p1.c` | P1 Device Select |
| `gts_ui_p2.c` | P2 GFC Mode |
| `gts_ui_p3.c` | P3 AOS 가스 Data 측정 |
| `gts_ui_p4.c` | P4 AOS Manual |
| `gts_ui_p5.c` | P5 WiFi 접속 List |
| `wifi_manager.[ch]` | 등록 지점 목록, 논블로킹 접속, 스캔, NVS 최근 접속 |
| `docs/demo_ref/` | 데모 원본 파일 보관 (빌드에 포함되지 않음) |

---

## 설계 메모

### 화면 회전 구현

`gts_rotate.c` 가 픽셀 회전을 맡는다. LVGL 에 의존하지 않는 순수 함수라
`test/host/test_rotate.c` 로 검증할 수 있다. 자세한 것은 위 "화면 회전" 참고.

행 정렬(`LV_DRAW_BUF_STRIDE_ALIGN`)을 무시하면 화면이 어긋나므로
`lv_draw_buf_width_to_stride()` 로 실제 stride 를 받아 쓴다.

터치는 물리 좌표 그대로 LVGL 에 넘긴다. LVGL 이 `lv_display_rotate_point()`
로 논리 좌표로 바꿔 주므로, 회전 설정을 바꾸면 터치도 함께 따라온다.

### 스레드 모델

| 태스크 | 하는 일 |
| --- | --- |
| `lvgl` (core 1) | LVGL 렌더 + 100 ms UI 갱신 타이머 + 터치 이벤트 |
| `gts_net` | UDP 수신 전담, 프레임 해석 → 상태 갱신 |
| `gts_ping` | 1초 keep-alive, 연속 3회 무응답 시 P1 복귀 |
| `gts_enc` | 1 ms Jog 폴링, 값 적용, 50 ms 디바운스 후 송신 |

상태는 `g_gts` 하나로 모으고 재귀 뮤텍스로 보호한다. 다른 태스크는
상태만 바꾸고 dirty 플래그를 세우며, 실제 LVGL 호출은 LVGL 태스크의
갱신 타이머에서만 일어난다. 락 순서는 항상 `tx_lock → state_lock` 이라
역전이 없다.

### 폰트

사양서는 Barlow / Barlow Condensed 를 지정하지만 LVGL 내장 Montserrat
(14 · 16 · 18 · 20 · 22 · 24 · 28 · 34 · 38 · 40 · 48) 로 대체했다.
`gts_theme.h` 의 `GF_*` 매크로만 바꾸면 변환한 Barlow 로 교체된다.

**한글 라벨은 기본 꺼져 있다.** Montserrat 에 한글 글리프가 없어 그대로
쓰면 □ 로 나온다. 한글 보조 라벨이 필요하면 Pretendard / Noto Sans KR 를
사용 문자만 서브셋해 13·14·15·16 px 로 변환한 뒤
`gts_config.h` 의 `GTS_USE_KR_FONT` 를 1 로 바꾸고 `gts_theme.h` 의
폰트 포인터를 교체한다.

---

## 사양서와 다른 점

1. **LF_Shape** — 요구사항 문서에는 Square / Sine / Triangle / Trapezoidal
   4종 선택이 있으나 목업 P4 에는 타일이 없다. 안내 박스를 줄이고
   (`438, 352, 348, 50`) 그 왼쪽에 `SHAPE` 순환 버튼(`250, 352, 180, 50`)을
   넣었다. 제어 타일 6개 배치는 사양서 그대로다.
2. **P3 타입 카드 x 좌표** — 사양서는 12 / 207 / 402 / 599 인데 폭 187 ·
   간격 8 로 계산하면 네 번째가 597 이다. 간격을 균일하게 두려고 597 을 썼다.
3. **폰트 크기** — 사양서의 13/15/19/21 px 는 Montserrat 가용 크기 중
   가장 가까운 14/16/20/20 px 로 맞췄다.
4. **상단바 x 좌표** — Brand 를 25→26px 로 키우면서, Montserrat 이
   Barlow Condensed 보다 넓어 MyID 칩을 138 → 200, 화면명을 237 → 298 로 밀었다.
   사양서의 148px Brand 폭에는 Montserrat 로 "GTS CONSOLE" 이 들어가지 않는다.
5. **P5 진입 버튼** — 사양서에 P5 로 가는 버튼이 없어 상단바의 Wi-Fi 표시를
   누르면 가도록 했다.
6. **UDP 규격** — 사양서 §9 의 초안 대신 `DOC/GTS_UDP_Protocol.md` Rev 0.1 을 쓴다.
   이유는 그 문서 0-0 절 참고.

---

## 서버 쪽에서 먼저 확인할 것

콘솔은 규격 문서의 다음 프레임을 기다린다. 아직 서버가 보내지 않으면
화면은 IDLE / NO REPLY 에서 더 진행되지 않는다.

- `CMD_CONNECT_ACK` (0x81) — 이게 와야 `ENTER CONTROL` 이 활성된다
- `CMD_PONG` (0x83) — 1초 keep-alive 응답
- `GFC_AUTO_STATE` (0xA3) — P2 카운트다운·진행바
- `AOS_MEAS_STATE` (0xB1) — P3 경과/남은 시간·샘플 수·자동 Stop
- `AOS_PARAMS` (0xC3) — P4 진입 시 장비의 현재값 동기화
