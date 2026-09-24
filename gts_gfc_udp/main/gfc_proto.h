/*
 * gfc_proto.h — GFC ESP32 ↔ 서버 UDP 바이너리 프로토콜
 *
 * 규격: DOC/GTS_GFC_UDP.md 6장
 *
 *   0     1       2      3     4   5     6   7    8 ...      N   N+1
 *  +-----+-------+------+-----+-----------+-----------+--------+---------+
 *  | STX | DTYPE | DID  | CMD |   SEQ     |   SIZE    |  DATA  |  CRC16  |
 *  | 0x02|  1B   |  1B  | 1B  |  2B (LE)  |  2B (LE)  |   N B  | 2B (LE) |
 *  +-----+-------+------+-----+-----------+-----------+--------+---------+
 *  전체 길이 = 10 + SIZE
 *
 * CRC16-MODBUS, 범위 = offset 1 ~ (7+SIZE), STX 제외, LE 전송.
 * 헤더 8byte 이므로 DATA 선두가 4의 배수 → payload 내 float 정렬 유지.
 *
 * ★ 콘솔↔서버 프로토콜(DOC/GTS_UDP_Protocol.md)과는 다른 규격이다.
 *   그쪽은 SEQ 가 DATA 안에 들어가고 헤더가 6byte 다. 변환은 서버가 한다.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GFC_STX                 0x02
#define GFC_HDR_LEN             8       /* STX DTYPE DID CMD SEQ(2) SIZE(2) */
#define GFC_FRAME_OVERHEAD      10      /* 헤더 8 + CRC 2 */
#define GFC_MAX_FRAME           512
#define GFC_MAX_PAYLOAD         (GFC_MAX_FRAME - GFC_FRAME_OVERHEAD)

/* ── Device type ─────────────────────────────────────────────────── */
enum {
    GFC_DEV_AOS     = 1,
    GFC_DEV_GFC     = 2,
    GFC_DEV_CONTROL = 3,
    GFC_DEV_BCAST   = 255,
};

/* ── Command (DOC 6.3) ───────────────────────────────────────────── */
enum {
    GFC_CMD_HELLO       = 0x01,
    GFC_CMD_ACK         = 0x02,
    GFC_CMD_CRES        = 0x03,
    GFC_CMD_EVENT       = 0x04,
    GFC_CMD_PING        = 0x05,
    GFC_CMD_DISCOVER    = 0x06,

    GFC_CMD_PUMP_SET    = 0x30,     /* UART 0x70 */
    GFC_CMD_PUMP_QUERY  = 0x31,     /* UART 0x71 */
    GFC_CMD_SENSOR_DATA = 0x32,     /* UART 0x72 */
    GFC_CMD_SENSOR_BULK = 0x33,
    GFC_CMD_SRC_SET     = 0x34,     /* ★ 주입 설정 */
    GFC_CMD_AUTO_SET    = 0x35,
    GFC_CMD_CFG_SET     = 0x36,
    GFC_CMD_NET_SET     = 0x37,
    GFC_CMD_TIME_SET    = 0x38,
    GFC_CMD_CFG_QUERY   = 0x39,
    GFC_CMD_SYS         = 0x3A,
};

/* ── 결과 코드 (ACK / CRES 의 result) ────────────────────────────── */
enum {
    GFC_RES_OK          = 0,
    GFC_RES_UNKNOWN_CMD = 1,
    GFC_RES_BAD_SIZE    = 2,
    GFC_RES_RANGE       = 3,
    GFC_RES_UART_FAIL   = 4,
    GFC_RES_BUSY        = 5,
};

/* ── EVENT 코드 ──────────────────────────────────────────────────── */
enum {
    GFC_EV_BOOT       = 0x01,
    GFC_EV_SRC_ON     = 0x10,
    GFC_EV_SRC_OFF    = 0x11,   /* 서버가 Pump2 구형파를 재구성하는 근거 */
    GFC_EV_LINK_LOST  = 0x31,
    GFC_EV_POLL_MODE  = 0x32,
};

/* ── flags 비트 (sensor_data_t.flags) ────────────────────────────── */
#define GFC_FLAG_SRC_ENABLE     (1u << 0)   /* 주입 시퀀스 동작 중     */
#define GFC_FLAG_SRC_ON         (1u << 1)   /* 지금 분사 중(Pump2 On)  */
#define GFC_FLAG_AUTO_CONC      (1u << 2)   /* 자동 농도조절 On        */
#define GFC_FLAG_UART_OK        (1u << 3)   /* STM32 링크 살아 있음    */
#define GFC_FLAG_SRC_INIT       (1u << 4)   /* 초기 주입 구간인가      */
#define GFC_FLAG_AUTO_PUSH      (1u << 7)   /* STM32 자동 송신 모드    */

/* ── err 비트 ────────────────────────────────────────────────────── */
#define GFC_ERR_POLL_MODE       (1u << 10)
#define GFC_ERR_UART_LOST       (1u << 11)

/* ── 페이로드 구조체 (DOC 6.4) ───────────────────────────────────── */
#pragma pack(push, 1)

typedef struct {
    uint32_t ts;                            /* 0  epoch 또는 uptime   */
    float    volt1, volt2;                  /* 4, 8    [V]            */
    float    ctrl, slope, sv;               /* 12,16,20               */
    float    src_remain, src_elapsed;       /* 24, 28  [s]            */
    uint32_t uptime;                        /* 32                     */
    uint16_t raw1, raw2, co2_1, co2_2;      /* 36,38,40,42            */
    uint8_t  pump1, pump2, pump3, flags;    /* 44,45,46,47            */
    uint16_t err;                           /* 48                     */
    int8_t   rssi;                          /* 50                     */
    uint8_t  rsv;                           /* 51                     */
} gfc_sensor_data_t;                        /* 52 byte */

typedef struct {
    uint8_t  pump1, pump2, pump3, rsv;
} gfc_pump_set_t;                           /* 4 byte */

typedef struct {
    uint8_t  enable;                        /* 0  1=시작, 0=정지      */
    uint8_t  rsv[3];
    float    init_on_sec;                   /* 4  초기 주입시간 [s]   */
    float    cycle_on_sec;                  /* 8  주기 주입시간 [s]   */
    float    period_sec;                    /* 12 주기 [s]            */
} gfc_src_set_t;                            /* 16 byte */

typedef struct {
    uint8_t  ack_cmd;                       /* 0  응답 대상 CMD       */
    uint8_t  result;                        /* 1  GFC_RES_*           */
    uint16_t ack_seq;                       /* 2  요청의 SEQ 반사     */
} gfc_ack_t;                                /* 4 byte */

typedef struct {
    uint8_t  cmd;
    uint8_t  result;
    uint16_t seq;
    uint32_t detail;
} gfc_cres_t;                               /* 8 byte */

typedef struct {
    uint32_t ts;
    uint16_t code;                          /* GFC_EV_*               */
    uint16_t rsv;
    uint32_t a;
    uint32_t b;
} gfc_event_t;                              /* 16 byte */

typedef struct {
    char     model[16];                     /* "GTS-GFC"              */
    char     fw[16];                        /* "0.1.0"                */
    uint8_t  mac[6];
    uint8_t  dtype;
    uint8_t  did;
    uint32_t uptime;
    char     ip[16];
    uint8_t  rsv[4];
} gfc_hello_t;                              /* 64 byte — 16+16+6+1+1+4+16+4 */

#pragma pack(pop)

/* ── 파싱 결과 ───────────────────────────────────────────────────── */
typedef struct {
    uint8_t         dtype;
    uint8_t         did;
    uint8_t         cmd;
    uint16_t        seq;
    const uint8_t  *payload;                /* 프레임 내부를 가리킴   */
    uint16_t        payload_len;
} gfc_frame_t;

/* ── 순수 함수 (소켓과 무관, 호스트에서 단위 테스트 가능) ────────── */

/** CRC-16/MODBUS. 다항식 0xA001, 초기값 0xFFFF, 최종 XOR 없음. */
uint16_t gfc_crc16(const uint8_t *data, size_t len);

/**
 * 프레임 조립.
 * @return 조립된 총 길이(= 10 + payload_len), 버퍼 부족/인자 오류면 -1
 */
int gfc_frame_build(uint8_t *out, size_t out_cap,
                    uint8_t dtype, uint8_t did, uint8_t cmd, uint16_t seq,
                    const void *payload, uint16_t payload_len);

/**
 * 프레임 해석 + CRC 검증 (DOC 10장 "검증 5단계").
 *   ① 길이 ≥ 10  ② STX  ③ SIZE 일치  ④ CRC  ⑤ DID 유효(1~20 또는 255)
 * @return true 면 out 이 유효.
 */
bool gfc_frame_parse(const uint8_t *buf, size_t len, gfc_frame_t *out);

#ifdef __cplusplus
}
#endif
