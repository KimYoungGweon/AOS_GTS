/*
 * aos_config.h — AOS Bridge (ESP32) 빌드 설정
 *
 * 하드웨어는 gts_gfc_udp 와 같은 ESP32 모듈이다 — LED·UART2 핀 동일.
 * 다른 것은 UART 속도(38400)와 SIZE 필드 폭(2 byte)뿐이다.
 *
 * STM32 펌웨어 근거: DOC/FAIMs_H753_V1_6_1_TWIN/Core/Src/MyWork/UART_PC.c
 *                    (명령 코드는 Core/Src/Header/command.h)
 */
#pragma once

/* ── Wi-Fi ──────────────────────────────────────────────────────── */
/* 접속 정보는 wifi_secrets.h 로 분리했다 — git 에 올라가지 않는다.
 * 새 기계에서는 main/wifi_secrets.h.example 을 복사해 값을 채울 것. */
#include "wifi_secrets.h"

/* ── UDP ────────────────────────────────────────────────────────────
 * 서버 5500 = AOS 포트. GFC 와 마찬가지로 로컬 포트를 같은 번호에 bind 해
 * 서버가 (AOS IP, 5500) 으로 언제든 하향 명령을 보낼 수 있게 한다.      */
#define AOS_SERVER_IP           "192.168.0.6"
#define AOS_SERVER_PORT         5500
#define AOS_LOCAL_PORT          5500

/* ── 네트워크 식별 ★ ─────────────────────────────────────────────────
 * ESP-IDF 의 기본 hostname 은 모든 보드가 "espressif" 로 같다.
 * 공유기가 MAC 이 아니라 hostname 으로 DHCP 리스를 관리하면 보드 둘이
 * **같은 IP 를 받는다**. 그러면 상향(HELLO/센서)은 멀쩡히 올라오는데
 * 서버가 보내는 하향 명령만 엉뚱한 보드로 가거나 사라진다.
 * 겉보기에는 정상이라 제일 찾기 어려운 고장이다.                      */
#define AOS_HOSTNAME           "gts-aos-01"

/* 고정 IP. 필요할 때만 1 로 바꾼다.
 *
 * ⚠️ 아래 주소는 **보드가 실제로 붙는 AP 의 서브넷**이어야 한다.
 *    SSID "aos" 가 Google Nest WiFi 라면 그 아래는
 *    192.168.86.0/24 (게이트웨이 192.168.86.1) 이고, 서버가 있는
 *    192.168.0.0/24 와는 다른 망이다 — Nest 가 NAT 로 중계한다.
 *    서브넷을 잘못 넣으면 보드가 네트워크에 아예 못 붙는다.
 *
 *    지금 값은 192.168.86.x 기준이다. 보드를 서버와 같은 망(192.168.0.x)에
 *    올리게 되면 여기 네 줄을 그 망에 맞춰 고칠 것.
 *
 * 공유기 DHCP 풀 **밖** 주소를 고를 것 — 안에서 고르면 나중에 다른
 * 기기에 같은 주소가 배정될 수 있다.                                   */
#define AOS_USE_STATIC_IP      0
#define AOS_STATIC_IP          "192.168.86.32"
#define AOS_STATIC_MASK        "255.255.255.0"
#define AOS_STATIC_GW          "192.168.86.1"
#define AOS_STATIC_DNS         "8.8.8.8"

/* ── 장치 주소 (DTYPE, DID) ─────────────────────────────────────────
 * DTYPE 1 = AOS. DID 는 1~20 — 콘솔 P1 에서 고르는 ID 와 같아야 한다.   */
#define AOS_DTYPE               1
#define AOS_DID                 1

/* ── 상향 주기 ──────────────────────────────────────────────────── */
#define AOS_HELLO_PERIOD_MS     10000
#define AOS_STATUS_PERIOD_MS    1000

/* ── LED (GFC 와 동일 핀) ───────────────────────────────────────── */
#define AOS_PIN_LED_RED         32      /* 출력 상태   */
#define AOS_PIN_LED_GREEN       33      /* 서버 연결   */
#define AOS_LED_TICK_MS         100

/* ── STM32 UART2 ─────────────────────────────────────────────────────
 * ★ GFC 와 다른 점
 *   - Baud 38400 — STM32 main.c 의 huart2.Init.BaudRate = 38400
 *   - SIZE 필드가 2 byte LE. STM32 의 TxMSG_PC() 가 size%256, size/256 을
 *     항상 내보내고 수신 상태기계도 eCMD_SIZE1/eCMD_SIZE2 를 거친다.
 *     1 byte 로 파싱하면 프레임이 통째로 어긋난다.
 * 핀은 GFC 와 같다 (동일 모듈).                                        */
#define AOS_STM32_ENABLE        1
#define AOS_UART_PORT           2
#define AOS_UART_TX_PIN         17
#define AOS_UART_RX_PIN         16
#define AOS_UART_BAUD           38400

/* 수신 프레임 DATA 버퍼. 이 브리지가 쓰는 응답은 0x02(32 byte),
 * 0x03(21 byte), 0x22(28 byte) 정도다. Heatmap 일괄(0x86, 8576 byte)은
 * 범위 밖이므로 버퍼를 넘는 프레임은 바이트만 세어 흘려보내고 동기를 지킨다. */
#define AOS_UART_DATA_MAX       256

/* ── UART 진단 ───────────────────────────────────────────────────────
 * 1 이면 STM32 로 보내고 받는 프레임을 전부 hex 로 찍는다.
 * 배선이 맞는지 확인할 때 켠다. 동작이 확인되면 0 으로.               */
#define AOS_UART_LOG_HEX        1

/* UART 링크 상태를 이 주기로 한 줄 찍는다 (0 이면 안 찍음). */
#define AOS_UART_HEALTH_MS      5000

/* ── Manual 파라미터 적용 방식 ──────────────────────────────────────
 * STM32 의 SET_Control() (MyCTL.c) 이 메인 루프에서 MData.SET.* 와
 * MData.OSET.* 를 비교해 바뀐 것만 실제 하드웨어에 적용한다.
 * 따라서 브리지는 값만 써 넣으면 되고, 별도 "적용" 명령이 필요 없다.
 *
 * 7가지를 UART 명령 두 개로 덮는다.
 *
 *   0x2A CMD_SET_CONTROL (16 byte)  HV · Frq · Duty · CV
 *   0x52 CMD_LF_MOD_SET  (10 byte)  LF type · LF OnOff · LF_VOLT · LF_FRQ
 *
 * Twin 의 0x82 MEASURE_POINT 를 쓰지 않는 이유: 그건 파라미터 설정이
 * 아니라 "이 조건으로 1 point 측정하라" 는 명령이라 매번 측정이 돌아간다.
 *
 * COALESCE: 콘솔 jog 를 돌리면 SET_PARAM 이 연달아 온다. 이 시간 동안
 * 변경을 모았다가 한 번에 내보낸다.                                      */
#define AOS_COALESCE_MS         100

/* 읽기 — 0x02 CMD_SET_QUERY 를 주기적으로 던져 STM32 의 실제 설정값을
 * 되받는다. 0x2A 는 응답이 없으므로 이게 UART 생존 신호도 겸한다.       */
#define AOS_QUERY_PERIOD_MS     1000
#define AOS_UART_FAIL_MAX       3       /* 연속 무응답 허용 횟수 */

/* 적용 확인 (2026-09-23) — 0x2A/0x52 는 응답이 없어 UART 에서 한 바이트만 깨져도
 * STM32 가 조용히 무시한다. 보낸 뒤 VERIFY_DELAY 후 0x02 로 되읽어 섀도와 비교하고,
 * 다르면 같은 프레임을 다시 보낸다.                                              */
#define AOS_VERIFY_DELAY_MS     300     /* 보낸 뒤 이만큼 기다렸다 되읽기 */
#define AOS_APPLY_RETRY         3       /* 불일치 시 재전송 횟수 */

/* STM32 의 자동 상태 송신(0x03 CMD_STATUS_QUERY) 을 켤지.
 * 0x04 CMD_ATUTO_STATUS_ONOFF 로 제어한다. 이온 전류 읽기(대기 중 25ms)마다 올라온다.
 * 2026-09-23: 0 이면 전류가 한 번도 안 올라와 STATUS(0x45) 전류가 늘 0 이었다 → 1 로 켬.
 * STM32 는 0x03 요청에 응답하지 않으므로(송신 전용) 자동 송신 외엔 전류를 얻을 방법이 없다.  */
#define AOS_AUTO_STATUS         1
/* 전류가 이 시간 넘게 안 오면 STATUS err 에 AOS_ERR_NO_CURRENT 를 세운다  */
#define AOS_CUR_STALE_MS        3000
/* STM32 가 한가한데(측정 프레임 없음) 전류가 안 오면 = 자동 송신이 꺼졌다(STM32 리셋 등)
 * → 0x04 를 다시 보낸다. STM32 가 0x04 마다 짧게 삑 하므로 간격을 늘려 간다.   */
#define AOS_AUTO_STATUS_RETRY_MS     5000
#define AOS_AUTO_STATUS_RETRY_MAX_MS 60000

/* ── LF 파형 ★ 고정 ─────────────────────────────────────────────────
 * STM32 로 내보내는 LF_MOD.type 은 **6 (eSquare) 로 고정**한다 (사용자 지정).
 * 콘솔이 0x42 SET_LF_SHAPE 를 보내도 STM32 파형은 바뀌지 않는다.
 * 되읽어 서버에 올리는 lf_shape 도 콘솔 SQUARE(0) 로 보고한다 —
 * 화면이 실제 장비 상태와 어긋나지 않게 하기 위해서다.
 *
 * 참고: 두 번호 체계는 애초에 서로 다르다. 나중에 파형 선택을 열 때는
 * 반드시 아래 표대로 변환해야 한다. 그대로 넘기면 SQUARE 를 골랐는데
 * TRIANGLE 이 나온다. LF_Modulator_Voltage_Set() 의 default: 는 파형을
 * 전부 minValue 로 채우므로, 모르는 번호는 에러 없이 조용히 틀린다.
 *
 *   콘솔 gts_lf_shape_t     STM32 eLF_Type (Header/MyCTL.h)
 *     0 SQUARE      ──▶       6 eSquare      0 eTriangle
 *     1 SINE        ──▶       5 eSine        1 eRamp
 *     2 TRIANGLE    ──▶       0 eTriangle    2 eRamp1_9
 *     3 TRAPEZOID   ──▶       8 eTPZ1_9      3 eRamp2_8
 *                                            4 eRamp3_7
 *                                            5 eSine
 *                                            6 eSquare   ← STM32 부팅 기본값
 *                                            7 eTPZ05_95
 *                                            8 eTPZ1_9
 *                                            9 eTPZ2_8
 *                                           10 eTPZ3_7
 *                                           11 eARB
 *
 * 파형 선택을 열려면 AOS_LF_SHAPE_FIXED 를 0 으로 바꾸면 된다
 * (aos_ctrl.c 의 변환 함수에 위 표가 이미 들어 있다).                  */
#define AOS_LF_SHAPE_FIXED      1
#define AOS_LF_TYPE_FIXED       6       /* eSquare */

#define AOS_LF_TYPE_SQUARE      6       /* eSquare   */
#define AOS_LF_TYPE_SINE        5       /* eSine     */
#define AOS_LF_TYPE_TRIANGLE    0       /* eTriangle */
#define AOS_LF_TYPE_TRAPEZOID   8       /* eTPZ1_9   */

/* ── 파라미터 범위 (DOC/GTS_UDP_Protocol.md 4-5) ────────────────── */
/* HV — STM32 의 jog 한계는 30~560 V 이나, 0 은 "끄기" 로 정상 취급한다.
 * 콘솔 사양서의 0~200 V 를 그대로 쓴다.                                */
#define AOS_HV_MIN        0.0f
#define AOS_HV_MAX      200.0f
#define AOS_FRQ_MIN     200.0f
#define AOS_FRQ_MAX     800.0f
/* DUTY 20~80 % — STM32 의 실제 한계 (MyCTL.c 의 MinV[2]/MaxV[2]).
 * 사양서 초안의 10~50 은 틀린 값이었다 (2026-09-21 확인).
 * UART 경로에는 STM32 쪽 clamp 가 없으므로 브리지가 유일한 방어선이다.  */
#define AOS_DUTY_MIN     20.0f
#define AOS_DUTY_MAX     80.0f
#define AOS_CV_MIN       -5.0f
#define AOS_CV_MAX        5.0f
#define AOS_LFF_MIN      50.0f
#define AOS_LFF_MAX     200.0f
#define AOS_LFV_MIN       0.0f
#define AOS_LFV_MAX       5.0f

/* 부팅 기본값 — 콘솔이 아직 아무것도 안 보냈을 때의 섀도 초기값.
 * 부팅 직후에는 STM32 로 아무것도 내보내지 않는다. 콘솔이 값을 보내거나
 * 0x02 읽기가 성공하면 그때부터 섀도가 실제와 맞춰진다.                 */
/* STM32 의 부팅 기본값과 맞춰 둔다 (MyCTL.c 의 초기화 블록).
 * 첫 0x02 읽기가 성공하면 어차피 실제 값으로 덮어써진다.               */
#define AOS_DEF_HV       50.0f      /* MData.SET.RF_HV        */
#define AOS_DEF_FRQ     500.0f      /* MData.SET.Frq   [kHz]  */
#define AOS_DEF_DUTY     50.0f      /* MData.SET.Duty  [%]    */
#define AOS_DEF_CV        0.0f      /* MData.SET.CV           */
#define AOS_DEF_LFF     200.0f      /* LF_MOD.frq      [Hz]   */
#define AOS_DEF_LFV       2.5f      /* LF_MOD.amp      [V]    */
#define AOS_DEF_LF_SHAPE  0         /* 콘솔 SQUARE (= STM32 eSquare 6) */
