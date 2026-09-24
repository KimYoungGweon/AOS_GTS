/*
 * aos_uart.h — STM32(FAIMs H753) UART2 게이트웨이
 *
 * 근거: DOC/FAIMs_H753_V1_6_1_TWIN/Core/Src/MyWork/UART_PC.c
 *       DOC/FW_RS232_Protocol.md
 *
 *   +-------+-------+---------+---------+--------------+----------+
 *   |  STX  |  CMD  | SIZE_LO | SIZE_HI |  DATA[SIZE]  | CHECKSUM |
 *   | 0x02  | 1byte |  1byte  |  1byte  |    N bytes   |  1byte   |
 *   +-------+-------+---------+---------+--------------+----------+
 *
 *   CHECKSUM = (~(CMD + SIZE_LO + SIZE_HI + ΣDATA)) & 0xFF    ※ STX 제외
 *
 * ★ GFC 브리지와 다른 점 — SIZE 가 2 byte 다 (Is_2Byte = true).
 *   STM32 의 TxMSG_PC() 가 size%256, size/256 을 항상 내보내고,
 *   수신 상태기계도 eCMD_SIZE1 → eCMD_SIZE2 를 거친다.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── STM32 명령 코드 (Core/Src/Header/command.h) ─────────────────── */
#define AOS_UART_RECEIVED_OK        0x00    /* 양방향 ACK, 0 byte        */
#define AOS_UART_SET_QUERY          0x02    /* 요청 0 / 응답 32 byte     */
#define AOS_UART_STATUS_QUERY       0x03    /* 자동 상태, 응답 21 byte   */
#define AOS_UART_AUTO_STATUS_ONOFF  0x04    /* 요청 1 byte               */
#define AOS_UART_CV_STATUS          0x22    /* CV 설정 시 STM32 가 보냄  */
#define AOS_UART_SET_CONTROL        0x2A    /* 요청 16 byte, 응답 없음   */
#define AOS_UART_LF_MOD_SET         0x52    /* 요청 10 byte, 응답 없음   */

/** STM32 가 돌려준 실제 설정값 + 측정값 */
typedef struct {
    /* 0x02 CMD_SET_QUERY 응답에서 */
    float    hv;            /* MData.SET.RF_HV        */
    float    cv;            /* MData.SET.CV           */
    float    frq;           /* MData.SET.Frq   [kHz]  */
    float    duty;          /* MData.SET.Duty  [%]    */
    uint8_t  rf_mod_on;
    float    rf_mod_frq;
    uint8_t  lf_on;         /* MData.SET.LF_MOD.OnOff */
    uint8_t  lf_type;       /* MData.SET.LF_MOD.type  */
    float    lf_amp;        /* MData.SET.LF_MOD.amp   */
    float    lf_frq;        /* MData.SET.LF_MOD.frq   */
    uint8_t  current_type;

    /* 0x03 / 0x22 에서 (자동 상태를 켠 경우) */
    uint16_t adc_p, adc_n;      /* MData.adcAvg_P / _N     */
    uint16_t tw_adc_p, tw_adc_n;/* MData.TW_adcAvg_P / _N  */
    float    hv_sense;          /* MData.SENSE.HV_Vs       */

    bool     have_set;      /* 0x02 응답을 한 번이라도 받았는가 */
    uint32_t set_count;     /* 0x02 응답 받은 횟수 — "보낸 뒤의 응답인가" 판별용 (2026-09-23) */
    uint32_t cur_count;     /* 0x03 전류 상태 받은 횟수 — 전류값이 살아 있는지 판별 (2026-09-23) */
    uint32_t busy_count;    /* 0x00/0x02/0x03/0x22 이외 프레임 수 — STM32 가 측정 순회 중인지 판별 */
    bool     link_ok;
    uint32_t rx_frames;
    uint32_t age_ms;        /* 마지막 유효 프레임 이후 경과     */
} aos_uart_state_t;

/** UART2 초기화 + 수신 태스크 기동. 부팅 시 1회. */
bool aos_uart_init(void);

/** 0x2A CMD_SET_CONTROL — HV · Frq · Duty · CV 를 한 프레임으로. */
bool aos_uart_set_control(float hv, float frq, float duty, float cv);

/** 0x52 CMD_LF_MOD_SET — LF 파형·On/Off·진폭(V)·주파수(Hz). */
bool aos_uart_lf_mod_set(uint8_t type, uint8_t on, float amp, float frq);

/** 0x02 CMD_SET_QUERY — 설정값 읽기 요청. 응답은 비동기로 캐시에 반영. */
bool aos_uart_set_query(void);

/** 0x04 CMD_ATUTO_STATUS_ONOFF — STM32 의 자동 상태 송신 On/Off. */
bool aos_uart_auto_status(bool on);

/** 최신 캐시 스냅샷. */
void aos_uart_get(aos_uart_state_t *out);

/** 조회를 보냈는데 응답이 없었음을 알린다 (aos_ctrl 의 틱이 부른다). */
void aos_uart_note_timeout(void);

/** UART 링크 상태를 한 줄 로그로. */
void aos_uart_log_health(void);

#ifdef __cplusplus
}
#endif
