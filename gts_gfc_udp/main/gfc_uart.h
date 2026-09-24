/*
 * gfc_uart.h — STM32 보드와의 UART2 게이트웨이
 *
 * 규격: DOC/GTS_GFC_UDP.md 5절 (STM32 코드는 변경하지 않는다)
 *
 *   +-------+-------+--------+----------------+----------+
 *   |  STX  |  CMD  |  SIZE  |   DATA[SIZE]   | CHECKSUM |
 *   | 0x02  | 1byte | 1byte  |    N bytes     |  1byte   |
 *   +-------+-------+--------+----------------+----------+
 *   CHECKSUM = 255 - ((CMD ~ DATA 합) mod 256)   ※ STX 제외
 *
 * UDP 쪽(2byte SIZE + CRC16)과 전혀 다른 규격이다. 변환은 전부 여기와
 * gfc_ctrl.c 가 맡는다 — 터널링이 아니라 게이트웨이다 (DOC D4).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UART 명령 (DOC 5절) */
#define GFC_UART_CMD_PUMP_SET       0x70    /* 요청 3 byte — Pump1/2/3 */
#define GFC_UART_CMD_PUMP_QUERY     0x71    /* 요청 0 / 응답 3 byte    */
#define GFC_UART_CMD_SENSOR_QUERY   0x72    /* 요청 0 / 응답 11 byte   */

#define GFC_UART_MAX_FRAME          272     /* STX+CMD+SIZE+255+CS 여유 */

/** STM32 에서 읽어 온 최신 상태 (스냅샷) */
typedef struct {
    uint8_t  pump1, pump2, pump3;
    uint16_t raw1, raw2;        /* CH1/CH2 TVOC 12bit ADC raw      */
    uint16_t co2_1, co2_2;      /* 예약 2바이트 2쌍 (U2 미확인)    */
    bool     link_ok;           /* 최근에 유효 프레임을 받았는가   */
    bool     auto_push;         /* STM32 가 스스로 밀어 올리는가   */
    bool     poll_mode;         /* 1초 주기 0x72 폴링 중인가       */
    uint32_t rx_frames;         /* 누적 수신 프레임 수             */
    uint32_t age_ms;            /* 마지막 유효 프레임 이후 경과    */
} gfc_uart_state_t;

/** UART2 초기화 + 수신/폴링 태스크 기동. 부팅 시 1회. */
bool gfc_uart_init(void);

/** Pump1/2/3 을 STM32 에 밀어 넣는다 (0x70). */
bool gfc_uart_pump_set(uint8_t p1, uint8_t p2, uint8_t p3);

/** 펌프 상태 조회 요청 (0x71). 응답은 비동기로 캐시에 반영된다. */
bool gfc_uart_pump_query(void);

/** 센서+펌프 조회 요청 (0x72). 응답은 비동기로 캐시에 반영된다. */
bool gfc_uart_sensor_query(void);

/** 최신 캐시 스냅샷을 복사해 간다 (락 내부 처리). */
void gfc_uart_get(gfc_uart_state_t *out);

/** raw(12bit) → 전압. DOC 2.2: raw * 5.0 / 4095 (U4 미확정). */
float gfc_uart_raw_to_volt(uint16_t raw);

#ifdef __cplusplus
}
#endif
