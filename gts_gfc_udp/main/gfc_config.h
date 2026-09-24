/*
 * gfc_config.h — GFC (Gas Flow Controller) ESP32 빌드 설정
 *
 * 규격: DOC/GTS_GFC_UDP.md (6~7장), DOC/ESP32_PRD.md
 * 보드/배선이 바뀌면 이 파일만 고친다.
 */
#pragma once

/* ── Wi-Fi ──────────────────────────────────────────────────────── */
/* 접속 정보는 wifi_secrets.h 로 분리했다 — git 에 올라가지 않는다.
 * 새 기계에서는 main/wifi_secrets.h.example 을 복사해 값을 채울 것. */
#include "wifi_secrets.h"

/* ── UDP ─────────────────────────────────────────────────────────────
 * 서버와 GFC 는 같은 LAN(192.168.0.x) 이다. 로컬 포트를 서버 포트와
 * 같은 5501 로 고정해 두면 서버가 (GFC IP, 5501) 로 언제든 하향 명령을
 * 보낼 수 있다 — 상향 패킷을 기다렸다가 source port 를 기억할 필요가
 * 없어져 NAT 없는 환경에서 가장 단순하다.
 * (외부망을 거치게 되면 DOC 의 D12 "ACK 동봉" 방식으로 바꿀 것.)     */
#define GFC_SERVER_IP           "192.168.0.6"
#define GFC_SERVER_PORT         5501
#define GFC_LOCAL_PORT          5501

/* ── 네트워크 식별 ★ ─────────────────────────────────────────────────
 * ESP-IDF 의 기본 hostname 은 모든 보드가 "espressif" 로 같다.
 * 공유기가 MAC 이 아니라 hostname 으로 DHCP 리스를 관리하면 보드 둘이
 * **같은 IP 를 받는다**. 그러면 상향(HELLO/센서)은 멀쩡히 올라오는데
 * 서버가 보내는 하향 명령만 엉뚱한 보드로 가거나 사라진다.
 * 겉보기에는 정상이라 제일 찾기 어려운 고장이다.                      */
#define GFC_HOSTNAME           "gts-gfc-01"

/* 고정 IP. 필요할 때만 1 로 바꾼다.
 *
 * ⚠️ 아래 주소는 **보드가 실제로 붙는 AP 의 서브넷**이어야 한다.
 *    SSID "gfc" 가 Google Nest WiFi 라면 그 아래는
 *    192.168.86.0/24 (게이트웨이 192.168.86.1) 이고, 서버가 있는
 *    192.168.0.0/24 와는 다른 망이다 — Nest 가 NAT 로 중계한다.
 *    서브넷을 잘못 넣으면 보드가 네트워크에 아예 못 붙는다.
 *
 *    지금 값은 192.168.86.x 기준이다. 보드를 서버와 같은 망(192.168.0.x)에
 *    올리게 되면 여기 네 줄을 그 망에 맞춰 고칠 것.
 *
 * 공유기 DHCP 풀 **밖** 주소를 고를 것 — 안에서 고르면 나중에 다른
 * 기기에 같은 주소가 배정될 수 있다.                                   */
#define GFC_USE_STATIC_IP      0
#define GFC_STATIC_IP          "192.168.86.31"
#define GFC_STATIC_MASK        "255.255.255.0"
#define GFC_STATIC_GW          "192.168.86.1"
#define GFC_STATIC_DNS         "8.8.8.8"

/* ── 장치 주소 (DTYPE, DID) ─────────────────────────────────────────
 * DTYPE 2 = GFC. DID 는 1~20. 콘솔 P1 에서 고른 ID 와 같아야 한다.   */
#define GFC_DTYPE               2
#define GFC_DID                 1

/* ── 상향 주기 ──────────────────────────────────────────────────── */
#define GFC_HELLO_PERIOD_MS     10000   /* 서버가 재시작해도 다시 알림 */
#define GFC_SENSOR_PERIOD_MS    1000    /* SENSOR_DATA 주기 송신      */

/* ── LED (DOC 7.1) ──────────────────────────────────────────────── */
#define GFC_PIN_LED_RED         32      /* 제어 동작 상태 */
#define GFC_PIN_LED_GREEN       33      /* 서버 연결 상태 */
#define GFC_LED_TICK_MS         100     /* 16bit 패턴 1비트씩 시프트  */

/* ── STM32 UART2 ─────────────────────────────────────────────────────
 * DOC/GTS_GFC_UDP.md 5절: 115200 8N1, 프레임 타임아웃 약 50 ms.
 * 핀 번호는 미확정(U3) — 점퍼 실물 배선에 맞춰 고칠 것.
 *
 * GFC_STM32_ENABLE 0 이면 UART 로 아무것도 내보내지 않고 ESP32 내부
 * 상태만 움직인다 (보드 없이 프로토콜 경로만 검증할 때).             */
#define GFC_STM32_ENABLE        1
#define GFC_UART_PORT           2
#define GFC_UART_TX_PIN         17
#define GFC_UART_RX_PIN         16
#define GFC_UART_BAUD           115200

/* STM32 가 스스로 0x72 를 밀어 올리는지 판정하는 시간. 이 시간 안에
 * 요청 없이 프레임이 오면 AUTO_PUSH, 아니면 POLLING 으로 내려간다.  */
#define GFC_UART_AUTOPUSH_WAIT_MS   3000
#define GFC_UART_POLL_PERIOD_MS     1000
#define GFC_UART_AUTOPUSH_LOSS_MS   15000   /* 이 동안 무수신 → 폴링 */
#define GFC_UART_FAIL_MAX           3       /* 폴링 연속 무응답 허용  */

/* ── 주입 제어 기본값 (DOC 6.4 src_set_t) ───────────────────────── */
#define GFC_SRC_INIT_ON_SEC     30.0f
#define GFC_SRC_CYCLE_ON_SEC    1.0f
#define GFC_SRC_PERIOD_SEC      600.0f
#define GFC_CTRL_TICK_MS        100

/* ── ADC 환산 (DOC 2.2, U4 미확정) ──────────────────────────────── */
#define GFC_ADC_VREF            5.0f
#define GFC_ADC_FULL            4095.0f
