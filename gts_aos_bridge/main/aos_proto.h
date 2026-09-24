/*
 * aos_proto.h — AOS Bridge ↔ 서버 UDP 바이너리 프로토콜
 *
 * 프레임은 GFC(DOC/GTS_GFC_UDP.md 6장)와 **완전히 동일**하다.
 * DTYPE 만 1(AOS) 이고 포트가 5500 이다. 서버는 같은 파서를 쓴다.
 *
 *   0     1       2      3     4   5     6   7    8 ...      N   N+1
 *  +-----+-------+------+-----+-----------+-----------+--------+---------+
 *  | STX | DTYPE | DID  | CMD |   SEQ     |   SIZE    |  DATA  |  CRC16  |
 *  | 0x02|  1B   |  1B  | 1B  |  2B (LE)  |  2B (LE)  |   N B  | 2B (LE) |
 *  +-----+-------+------+-----+-----------+-----------+--------+---------+
 *
 * CRC16-MODBUS, 범위 offset 1 ~ (7+SIZE), STX 제외, LE 전송.
 *
 * 명령 대역: DOC/GTS_GFC_UDP.md 12절 U5 가 "AOS 의 0x30~0x3F 대역 미정"
 * 으로 남겨 둔 자리다. 여기서는 **0x40~0x4F** 를 쓴다 — 콘솔↔서버의 AOS
 * Manual 명령(0x40~0x43, DOC/GTS_UDP_Protocol.md 4-5)과 번호를 맞춰
 * 서버의 변환을 1:1 로 단순하게 만들기 위해서다.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AOS_STX                 0x02
#define AOS_HDR_LEN             8
#define AOS_FRAME_OVERHEAD      10
#define AOS_MAX_FRAME           512
#define AOS_MAX_PAYLOAD         (AOS_MAX_FRAME - AOS_FRAME_OVERHEAD)

/* ── Device type (GFC 와 공용) ───────────────────────────────────── */
enum {
    AOS_DEV_AOS     = 1,
    AOS_DEV_GFC     = 2,
    AOS_DEV_CONTROL = 3,
    AOS_DEV_BCAST   = 255,
};

/* ── Command ─────────────────────────────────────────────────────── */
enum {
    /* 공통 대역 0x01~0x0F — GFC 와 같은 의미 */
    AOS_CMD_HELLO       = 0x01,
    AOS_CMD_ACK         = 0x02,
    AOS_CMD_CRES        = 0x03,
    AOS_CMD_EVENT       = 0x04,
    AOS_CMD_PING        = 0x05,
    AOS_CMD_DISCOVER    = 0x06,

    /* AOS Manual 대역 0x40~0x4F */
    AOS_CMD_PARAM_SET   = 0x40,     /* S→A  8  aos_param_set_t   */
    AOS_CMD_LF_MODE     = 0x41,     /* S→A  4  aos_u8_t          */
    AOS_CMD_LF_SHAPE    = 0x42,     /* S→A  4  aos_u8_t          */
    AOS_CMD_PARAMS_QUERY= 0x43,     /* S→A  0                    */
    AOS_CMD_PARAMS      = 0x44,     /* A→S 26  aos_params_t      */
    AOS_CMD_STATUS      = 0x45,     /* A→S 24  aos_status_t      */
    AOS_CMD_SYS         = 0x4F,     /* S→A  4  (1 = reboot)      */
};

/* ── 결과 코드 ───────────────────────────────────────────────────── */
enum {
    AOS_RES_OK          = 0,
    AOS_RES_UNKNOWN_CMD = 1,
    AOS_RES_BAD_SIZE    = 2,
    AOS_RES_RANGE       = 3,
    AOS_RES_UART_FAIL   = 4,
};

/* ── EVENT 코드 ──────────────────────────────────────────────────── */
enum {
    AOS_EV_BOOT       = 0x01,
    AOS_EV_PARAM_APPLY= 0x20,   /* 0x82 를 STM32 로 내보냄       */
    AOS_EV_POINT      = 0x21,   /* 0x83 POINT_DATA 수신          */
    AOS_EV_APPLY_FAIL = 0x22,   /* 재전송해도 STM32 값이 안 맞음 (a=0x2A/0x52) */
    AOS_EV_LINK_LOST  = 0x31,
};

/* ── flags (aos_status_t.flags) ──────────────────────────────────── */
#define AOS_FLAG_LF_ON      (1u << 0)
#define AOS_FLAG_APPLIED    (1u << 1)   /* 한 번이라도 0x82 를 보냈다 */
#define AOS_FLAG_UART_OK    (1u << 3)

/* ── err 비트 ────────────────────────────────────────────────────── */
#define AOS_ERR_UART_LOST   (1u << 11)
#define AOS_ERR_NO_REPLY    (1u << 12)  /* 0x82 를 보냈는데 0x83 이 없다 */
#define AOS_ERR_NO_CURRENT  (1u << 13)  /* 2026-09-23: STM32 전류(0x03)가 AOS_CUR_STALE_MS 넘게 없음
                                         * → air_p..gas_n 은 유효값이 아니다 (0 이어도 측정값 아님) */

/* ── 파라미터 ID (콘솔과 동일, DOC/GTS_UDP_Protocol.md 4-5) ─────── */
typedef enum {
    AOS_P_HV = 0,
    AOS_P_FRQ,
    AOS_P_DUTY,
    AOS_P_CV,
    AOS_P_LF_FRQ,
    AOS_P_LF_VOLT,
    AOS_P_COUNT
} aos_param_id_t;

/* ── 페이로드 ────────────────────────────────────────────────────── */
#pragma pack(push, 1)

typedef struct {
    uint8_t  param_id;      /* aos_param_id_t */
    uint8_t  pad[3];        /* f32 를 4 byte 경계에 맞춘다 */
    float    value;
} aos_param_set_t;                          /* 8 byte */

typedef struct {
    uint8_t  v;
    uint8_t  pad[3];
} aos_u8_t;                                 /* 4 byte */

/*
 * 콘솔의 AOS_PARAMS(0xC3, 26 byte)와 **바이트 배치가 같다**.
 * 서버가 슬라이싱 없이 그대로 실어 보낼 수 있게 하려는 것이다.
 */
typedef struct {
    float    hv;            /* 0  V    */
    float    frq;           /* 4  kHz  */
    float    duty;          /* 8  %    */
    float    cv;            /* 12 V    */
    float    lf_frq;        /* 16 Hz   */
    float    lf_volt;       /* 20 V    */
    uint8_t  lf_on;         /* 24      */
    uint8_t  lf_shape;      /* 25      */
} aos_params_t;                             /* 26 byte */

typedef struct {
    uint32_t ts;                            /* 0                        */
    uint32_t uptime;                        /* 4  초                    */
    uint16_t air_p, air_n, gas_p, gas_n;    /* 8  마지막 0x83 POINT_DATA */
    uint32_t point_count;                   /* 16 누적 0x83 수신 수     */
    uint8_t  flags;                         /* 20                       */
    int8_t   rssi;                          /* 21                       */
    uint16_t err;                           /* 22                       */
} aos_status_t;                             /* 24 byte */

typedef struct {
    uint8_t  ack_cmd;
    uint8_t  result;
    uint16_t ack_seq;
} aos_ack_t;                                /* 4 byte */

typedef struct {
    uint32_t ts;
    uint16_t code;
    uint16_t rsv;
    uint32_t a;
    uint32_t b;
} aos_event_t;                              /* 16 byte */

typedef struct {
    char     model[16];                     /* "GTS-AOS"  */
    char     fw[16];                        /* "0.1.0"    */
    uint8_t  mac[6];
    uint8_t  dtype;
    uint8_t  did;
    uint32_t uptime;
    char     ip[16];
    uint8_t  rsv[4];
} aos_hello_t;                              /* 64 byte */

#pragma pack(pop)

/* ── 파싱 결과 ───────────────────────────────────────────────────── */
typedef struct {
    uint8_t         dtype;
    uint8_t         did;
    uint8_t         cmd;
    uint16_t        seq;
    const uint8_t  *payload;
    uint16_t        payload_len;
} aos_frame_t;

/* ── 순수 함수 ───────────────────────────────────────────────────── */

uint16_t aos_crc16(const uint8_t *data, size_t len);

int aos_frame_build(uint8_t *out, size_t out_cap,
                    uint8_t dtype, uint8_t did, uint8_t cmd, uint16_t seq,
                    const void *payload, uint16_t payload_len);

bool aos_frame_parse(const uint8_t *buf, size_t len, aos_frame_t *out);

#ifdef __cplusplus
}
#endif
