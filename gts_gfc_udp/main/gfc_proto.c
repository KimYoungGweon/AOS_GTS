/*
 * gfc_proto.c — UDP 바이너리 프레임 조립/해석
 *
 * 규격: DOC/GTS_GFC_UDP.md 6장
 * 소켓과 무관한 순수 함수만 둔다 (호스트에서 그대로 단위 테스트 가능).
 */
#include "gfc_proto.h"

#include <string.h>

uint16_t gfc_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001)
                             : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

int gfc_frame_build(uint8_t *out, size_t out_cap,
                    uint8_t dtype, uint8_t did, uint8_t cmd, uint16_t seq,
                    const void *payload, uint16_t payload_len)
{
    if (!out) return -1;
    if (payload_len > GFC_MAX_PAYLOAD) return -1;
    if ((size_t)GFC_FRAME_OVERHEAD + payload_len > out_cap) return -1;

    size_t i = 0;
    out[i++] = GFC_STX;
    out[i++] = dtype;
    out[i++] = did;
    out[i++] = cmd;
    out[i++] = (uint8_t)(seq & 0xFF);
    out[i++] = (uint8_t)(seq >> 8);
    out[i++] = (uint8_t)(payload_len & 0xFF);
    out[i++] = (uint8_t)(payload_len >> 8);

    if (payload_len && payload) {
        memcpy(&out[i], payload, payload_len);
        i += payload_len;
    }

    /* CRC 범위 = offset 1 ~ (7+SIZE), 즉 STX 를 뺀 전부 */
    uint16_t crc = gfc_crc16(&out[1], i - 1);
    out[i++] = (uint8_t)(crc & 0xFF);
    out[i++] = (uint8_t)(crc >> 8);

    return (int)i;
}

bool gfc_frame_parse(const uint8_t *buf, size_t len, gfc_frame_t *out)
{
    if (!buf || !out) return false;

    /* ① 최소 길이 */
    if (len < (size_t)GFC_FRAME_OVERHEAD) return false;
    /* ② STX */
    if (buf[0] != GFC_STX) return false;

    /* ③ SIZE 일치 — 조작된 SIZE 폐기 */
    uint16_t size = (uint16_t)(buf[6] | ((uint16_t)buf[7] << 8));
    if (size > GFC_MAX_PAYLOAD) return false;
    if ((size_t)size + GFC_FRAME_OVERHEAD != len) return false;

    /* ④ CRC */
    uint16_t crc_rx = (uint16_t)(buf[8 + size] | ((uint16_t)buf[9 + size] << 8));
    uint16_t crc_ca = gfc_crc16(&buf[1], (size_t)(7 + size));
    if (crc_rx != crc_ca) return false;

    /* ⑤ DID 유효성 — 0 미할당, 21~254 폐기, 255 브로드캐스트 */
    uint8_t did = buf[2];
    if (did == 0) return false;
    if (did > 20 && did != GFC_DEV_BCAST) return false;

    out->dtype       = buf[1];
    out->did         = did;
    out->cmd         = buf[3];
    out->seq         = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8));
    out->payload     = (size > 0) ? &buf[GFC_HDR_LEN] : NULL;
    out->payload_len = size;
    return true;
}
