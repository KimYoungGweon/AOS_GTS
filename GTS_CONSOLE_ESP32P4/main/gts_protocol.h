/*
 * gts_protocol.h — GTS Console UDP 프로토콜
 *
 * 규격: DOC/GTS_UDP_Protocol.md
 *
 *   ┌─────┬──────┬──────┬─────┬────────┬────────┬───────────┬────────┐
 *   │ STX │ TYPE │  ID  │ CMD │ SIZE_L │ SIZE_H │ DATA[SIZE]│ CRC16  │
 *   │0x02 │ 1 B  │ 1 B  │ 1 B │  1 B   │  1 B   │  SIZE B   │ 2 B LE │
 *   └─────┴──────┴──────┴─────┴────────┴────────┴───────────┴────────┘
 *
 * DATA 선두 4 byte 는 항상 공통 헤더: src_type, src_id, seq(u16 LE).
 * CRC16 은 CRC-16/MODBUS, STX 를 제외한 TYPE~DATA 구간.
 *
 * 스레드 모델
 * ───────────
 *   gts_net_task  : 소켓 수신 전담. 수신 프레임을 해석해 gts_state 갱신.
 *   그 외 태스크  : gts_proto_send_*() 로 송신만 한다 (송신은 뮤텍스 보호).
 * LVGL 태스크는 이 모듈의 함수를 호출해도 되지만, 반대로 이 모듈이
 * LVGL 을 직접 건드리지는 않는다. 화면 갱신은 gts_state 를 폴링하는
 * lv_timer 가 담당한다.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTS_STX             0x02
#define GTS_HDR_LEN         4       /* src_type, src_id, seq(2) */
#define GTS_FRAME_OVERHEAD  8       /* STX+TYPE+ID+CMD+SIZE(2)+CRC(2) */
#define GTS_MAX_FRAME       512
#define GTS_MAX_PAYLOAD     (GTS_MAX_FRAME - GTS_FRAME_OVERHEAD - GTS_HDR_LEN)

/* ── Device type ─────────────────────────────────────────────────── */
typedef enum {
    GTS_DEV_NONE    = 0x00,
    GTS_DEV_AOS     = 0x01,
    GTS_DEV_GFC     = 0x02,
    GTS_DEV_CONSOLE = 0x03,
} gts_dev_type_t;

/* ── Command ─────────────────────────────────────────────────────── */
typedef enum {
    /* 시스템 */
    GTS_CMD_ACK             = 0x00,
    GTS_CMD_CONNECT         = 0x01,
    GTS_CMD_DISCONNECT      = 0x02,
    GTS_CMD_PING            = 0x03,
    GTS_CMD_STATUS_REQ      = 0x04,
    GTS_CMD_NAK             = 0x7F,
    GTS_CMD_CONNECT_ACK     = 0x81,
    GTS_CMD_DISCONNECT_ACK  = 0x82,
    GTS_CMD_PONG            = 0x83,
    GTS_CMD_STATUS_RESP     = 0x84,
    /* GFC */
    GTS_CMD_GFC_SET_MODE    = 0x20,
    GTS_CMD_GFC_SET_PUMP    = 0x21,
    GTS_CMD_GFC_SET_TIMES   = 0x22,
    GTS_CMD_GFC_AUTO_RUN    = 0x23,
    GTS_CMD_GFC_AUTO_STATE  = 0xA3,
    /* AOS 측정 */
    GTS_CMD_AOS_SET_TYPE    = 0x30,
    GTS_CMD_AOS_MEAS_RUN    = 0x31,
    GTS_CMD_AOS_MEAS_STATE  = 0xB1,
    /* AOS manual */
    GTS_CMD_AOS_SET_PARAM   = 0x40,
    GTS_CMD_AOS_SET_LF_MODE = 0x41,
    GTS_CMD_AOS_SET_LF_SHAPE= 0x42,
    GTS_CMD_AOS_GET_PARAMS  = 0x43,
    GTS_CMD_AOS_PARAMS      = 0xC3,
} gts_cmd_t;

/* NAK error codes */
enum {
    GTS_ERR_UNKNOWN_CMD = 1,
    GTS_ERR_RANGE       = 2,
    GTS_ERR_NO_DEVICE   = 3,
    GTS_ERR_BUSY        = 4,
    GTS_ERR_CRC         = 5,
};

/* ── 파싱된 수신 프레임 ──────────────────────────────────────────── */
typedef struct {
    uint8_t         type;           /* 대상 장치 종류 */
    uint8_t         id;             /* 대상 장치 ID   */
    uint8_t         cmd;
    uint8_t         src_type;
    uint8_t         src_id;
    uint16_t        seq;
    const uint8_t  *payload;        /* DATA + GTS_HDR_LEN 위치 */
    uint16_t        payload_len;    /* SIZE - GTS_HDR_LEN */
} gts_frame_t;

/* ── 순수 함수 (소켓과 무관, 단위 테스트 가능) ───────────────────── */

/** CRC-16/MODBUS. 다항식 0xA001, 초기값 0xFFFF. */
uint16_t gts_crc16(const uint8_t *data, size_t len);

/**
 * 프레임 조립.
 * @param out           출력 버퍼 (GTS_MAX_FRAME 이상)
 * @param out_cap       출력 버퍼 크기
 * @param payload       공통 헤더 뒤에 붙일 데이터 (없으면 NULL)
 * @param payload_len   payload 길이
 * @return  조립된 총 길이, 버퍼 부족/인자 오류면 -1
 */
int gts_frame_build(uint8_t *out, size_t out_cap,
                    uint8_t type, uint8_t id, uint8_t cmd,
                    uint8_t src_type, uint8_t src_id, uint16_t seq,
                    const void *payload, uint16_t payload_len);

/**
 * 프레임 해석 + CRC 검증.
 * @return true 면 out 이 유효. payload 포인터는 buf 내부를 가리킨다.
 */
bool gts_frame_parse(const uint8_t *buf, size_t len, gts_frame_t *out);

/* ── 소켓 계층 ───────────────────────────────────────────────────── */

/** 소켓 생성 + 수신 태스크 기동. Wi-Fi 연결 후 1회 호출. */
bool gts_proto_start(void);

/** 임의 명령 송신. 대상 type/id 는 현재 선택된 장치를 자동 사용한다. */
bool gts_proto_send(uint8_t cmd, const void *payload, uint16_t payload_len);

/** 대상을 명시해 송신 (연결 전 CONNECT 등에 사용). */
bool gts_proto_send_to(uint8_t type, uint8_t id,
                       uint8_t cmd, const void *payload, uint16_t payload_len);

/* ── 상위 레벨 헬퍼 ──────────────────────────────────────────────── */
bool gts_proto_connect(uint8_t type, uint8_t id);
bool gts_proto_disconnect(void);
bool gts_proto_ping(void);

bool gts_proto_gfc_set_mode(uint8_t mode);
bool gts_proto_gfc_set_pump(bool on);
bool gts_proto_gfc_set_times(uint16_t start_ds, uint16_t cycle_ds);
bool gts_proto_gfc_auto_run(bool run);

bool gts_proto_aos_set_type(uint8_t type);
bool gts_proto_aos_meas_run(bool run);

bool gts_proto_aos_set_param(uint8_t param_id, float value);
bool gts_proto_aos_set_lf_mode(bool on);
bool gts_proto_aos_set_lf_shape(uint8_t shape);
bool gts_proto_aos_get_params(void);

#ifdef __cplusplus
}
#endif
