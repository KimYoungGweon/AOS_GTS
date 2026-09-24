/*
 * gts_config.h — GTS Console 빌드 시 고정되는 설정값
 *
 * 하드웨어 핀, UDP 대상, 타이밍 상수를 한곳에 모았다.
 * 보드 리비전이 바뀌면 이 파일만 고치면 된다.
 */
#pragma once

/* ── 화면 ────────────────────────────────────────────────────────────
 * 패널 자체는 480(W) x 800(H) 세로 패널이다. 사양서는 800 x 480 가로
 * (H mode) 기준이므로 LVGL 디스플레이를 90도 회전시켜 논리 해상도를
 * 800 x 480 으로 만든다. UI 코드는 전부 논리 좌표만 쓴다.            */
#define GTS_PANEL_W             480
#define GTS_PANEL_H             800
#define GTS_SCR_W               800     /* 회전 후 논리 가로 */
#define GTS_SCR_H               480     /* 회전 후 논리 세로 */

/* 회전 방향. 90 과 270 은 서로 180도 반대이므로, 화면이 거꾸로 보이면
 * 둘을 바꾸면 된다. 보드를 케이스에 어느 방향으로 끼웠는지에 달렸다.
 * main.c 의 flush 콜백이 이 값을 그대로 따라간다.                    */
#define GTS_ROTATION_DEG        270

/* ── LCD / 터치 핀 (데모 코드와 동일) ───────────────────────────── */
#define GTS_PIN_LCD_RST         5
#define GTS_PIN_LCD_BL          23
#define GTS_MIPI_LDO_CHAN       3
#define GTS_MIPI_LDO_MV         2500

#define GTS_PIN_TOUCH_SDA       7
#define GTS_PIN_TOUCH_SCL       8
#define GTS_PIN_TOUCH_RST       22
#define GTS_PIN_TOUCH_INT       21
#define GTS_TOUCH_I2C_PORT      0
#define GTS_TOUCH_I2C_HZ        400000

/* ── Jog(encoder) 핀 (데모 코드와 동일) ─────────────────────────── */
#define GTS_PIN_ENC_SW          29
#define GTS_PIN_ENC_A           30
#define GTS_PIN_ENC_B           31

/* ── Wi-Fi 등록 지점 ─────────────────────────────────────────────────
 * 접속 장소가 2~3곳으로 고정이라 스캔 목록 대신 등록 목록을 쓴다.
 * 화면(P5)은 이 표를 그대로 보여주고 선택·접속만 한다. 비밀번호 입력
 * 화면은 없다.
 *
 * 순서를 바꾸면 NVS 에 저장된 "최근 접속 지점" 인덱스가 어긋나므로,
 * 지점을 추가할 때는 뒤에 붙이고 기존 순서는 건드리지 말 것.
 *
 * 주의 — 비밀번호가 소스에 들어간다. 외부에 넘길 때는 비울 것.
 */
/* 접속 정보는 wifi_secrets.h 로 분리했다 — git 에 올라가지 않는다.
 * 새 기계에서는 main/wifi_secrets.h.example 을 복사해 값을 채울 것.
 *   GTS_WIFI_NET_COUNT, GTS_WIFI_n_NAME / _SSID / _PASS 가 거기 있다. */
#include "wifi_secrets.h"

/* 한 지점에 대한 연결 재시도 횟수. 다 쓰면 DISCONNECTED 로 두고
 * P1 에 있을 때만 P5 로 자동 전환한다 (P2~P4 작업 중에는 뺏지 않음). */
#define GTS_WIFI_MAX_RETRY      5

/* 부팅 시 NVS 에 기록된 최근 접속 지점으로 먼저 붙는다. 없으면 0번. */
#define GTS_WIFI_NVS_NAMESPACE  "gtswifi"

/* SNTP — "최근 접속 시각"을 실제 시각으로 보여주려면 필요하다.
 * 0 이면 시각 없이 "최근 접속" 배지만 표시한다.                      */
#define GTS_WIFI_SNTP           1
#define GTS_WIFI_SNTP_SERVER    "pool.ntp.org"
#define GTS_WIFI_TZ             "KST-9"

/* 스캔 시 허용할 최소 보안 수준. esp_wifi_types.h 의 wifi_auth_mode_t.
 *   개방망 WIFI_AUTH_OPEN · WPA/WPA2 혼용 WIFI_AUTH_WPA_WPA2_PSK      */
#define GTS_WIFI_AUTH_THRESHOLD WIFI_AUTH_WPA2_PSK
#define GTS_WIFI_SAE_MODE       WPA3_SAE_PWE_BOTH
#define GTS_WIFI_SAE_H2E_ID     ""

/* ── UDP ────────────────────────────────────────────────────────── */
#define GTS_UDP_SERVER_IP       "218.147.152.41"
#define GTS_UDP_SERVER_PORT     5502
#define GTS_MY_ID_DEFAULT       1       /* 콘솔 자기 ID (상단바 MyID) */

/* ── 프로토콜 타이밍 (DOC/GTS_UDP_Protocol.md 5절) ──────────────── */
#define GTS_REPLY_TIMEOUT_MS    500
#define GTS_RETRY_MAX           3
#define GTS_PING_PERIOD_MS      1000
#define GTS_LINK_FAIL_COUNT     3
#define GTS_JOG_DEBOUNCE_MS     50

/* 로컬 조작 우선 구간 — 사양서 3-3 "화면은 전송 결과를 기다리지 않고 즉시
 * 갱신한다 (로컬 우선)".
 *
 * GFC 제어를 보낸 직후에는 서버가 아직 옛 상태를 push 하고 있다. 그걸 그대로
 * 받아 쓰면 방금 누른 버튼이 되돌아가고, 다음 누름이 같은 값을 다시 보내는
 * 꼴이 된다. 이 시간 동안은 서버의 pump/run 값을 무시하고 내 조작을 유지한다.
 * 서버 push 주기(500 ms) + 왕복 여유로 잡는다. remain/cycle 카운트는 그대로
 * 서버 값을 따른다 — 그건 콘솔이 알 수 없는 값이다.                    */
#define GTS_LOCAL_HOLD_MS       1200

/* ── 오프라인 테스트 모드 ────────────────────────────────────────────
 * 서버가 아직 응답을 보내지 않는 단계에서 화면부터 검증하기 위한 것.
 *
 * 1 이면:
 *   - CONNECT 응답을 기다리지 않고 바로 연결된 것으로 친다
 *     → P1 의 ENTER CONTROL 이 활성되어 P2/P3/P4 로 들어갈 수 있다
 *   - keep-alive 무응답으로 P1 복귀시키지 않는다
 *   - GFC 주기 카운트다운과 AOS 측정 경과시간을 콘솔이 자체 진행시킨다
 *     → P2 의 카운트다운·펌프 상태, P3 의 경과/남은 시간·진행바·자동 Stop
 *       이 서버 없이도 움직인다
 *   - 상단바에 OFFLINE 이 표시된다 (실수로 켜 둔 채 넘어가지 않도록)
 *
 * 송신은 평소대로 나가므로 서버 수신부 확인에도 그대로 쓸 수 있다.
 * 서버 송신이 붙으면 0 으로 바꿀 것. 그때부터 화면은 서버가 보내는
 * GFC_AUTO_STATE / AOS_MEAS_STATE 만 따른다.
 */
#define GTS_OFFLINE_MODE        0

/* 오프라인 모드에서 시간을 몇 배로 돌릴지. 8시간 측정을 실시간으로
 * 지켜볼 수는 없으니 기본 20배 (preTest 5분 → 약 15초).
 * 1 이면 실시간.                                                     */
#define GTS_OFFLINE_SPEEDUP     20

/* ── 태스크 ─────────────────────────────────────────────────────── */
#define GTS_LVGL_TASK_STACK     (12 * 1024)
#define GTS_LVGL_TASK_PRIO      4
#define GTS_LVGL_TASK_CORE      1
#define GTS_NET_TASK_STACK      (6 * 1024)
#define GTS_NET_TASK_PRIO       5
#define GTS_ENC_TASK_STACK      (4 * 1024)
#define GTS_ENC_TASK_PRIO       6

/* ── 한글 폰트 ──────────────────────────────────────────────────────
 * LVGL 내장 Montserrat 에는 한글 글리프가 없다. 한글 보조 라벨을
 * 쓰려면 Pretendard/Noto Sans KR 서브셋 폰트를 변환해 넣고 아래를
 * 1로 바꾼 뒤 gts_theme.c 의 폰트 포인터를 교체한다.
 * 0 이면 영문 라벨만 표시한다 (기본).                                */
#define GTS_USE_KR_FONT         0
