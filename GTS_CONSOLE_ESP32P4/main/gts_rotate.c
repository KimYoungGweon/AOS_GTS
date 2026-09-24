/*
 * gts_rotate.c
 *
 * lv_display_rotate_area() 의 매핑 (hor_res/ver_res 는 패널 원본 480/800):
 *
 *   ROTATION_90    phys_x = ly,               phys_y = (ver_res-1) - lx
 *   ROTATION_180   phys_x = (hor_res-1) - lx, phys_y = (ver_res-1) - ly
 *   ROTATION_270   phys_x = (hor_res-1) - ly, phys_y = lx
 *
 * area 내부의 지역 좌표로 옮기면 (sx, sy) -> (dx, dy) 는 아래가 된다.
 * 패널 해상도가 식에서 사라지는 것이 핵심 — 원점끼리 빼면 상쇄된다.
 */
#include "gts_rotate.h"
#include <string.h>

void gts_rotate_rgb888(const uint8_t *src, uint8_t *dst,
                       int32_t src_w, int32_t src_h, int32_t src_stride,
                       gts_rot_t rot)
{
    switch (rot) {

    case GTS_ROT_90: {
        /* dx = sy, dy = (src_w-1) - sx, 목적지 폭 = src_h */
        const int32_t dst_w = src_h;
        for (int32_t sy = 0; sy < src_h; sy++) {
            const uint8_t *s = src + (size_t)sy * src_stride;
            for (int32_t sx = 0; sx < src_w; sx++) {
                uint8_t *d = dst + ((size_t)(src_w - 1 - sx) * dst_w + sy) * 3;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                s += 3;
            }
        }
        break;
    }

    case GTS_ROT_270: {
        /* dx = (src_h-1) - sy, dy = sx, 목적지 폭 = src_h */
        const int32_t dst_w = src_h;
        for (int32_t sy = 0; sy < src_h; sy++) {
            const uint8_t *s = src + (size_t)sy * src_stride;
            const int32_t dx = src_h - 1 - sy;
            for (int32_t sx = 0; sx < src_w; sx++) {
                uint8_t *d = dst + ((size_t)sx * dst_w + dx) * 3;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                s += 3;
            }
        }
        break;
    }

    case GTS_ROT_180: {
        /* dx = (src_w-1) - sx, dy = (src_h-1) - sy, 목적지 폭 = src_w */
        const int32_t dst_w = src_w;
        for (int32_t sy = 0; sy < src_h; sy++) {
            const uint8_t *s = src + (size_t)sy * src_stride;
            uint8_t *row = dst + (size_t)(src_h - 1 - sy) * dst_w * 3;
            for (int32_t sx = 0; sx < src_w; sx++) {
                uint8_t *d = row + (size_t)(src_w - 1 - sx) * 3;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                s += 3;
            }
        }
        break;
    }

    default: {
        /* 회전 없음 — stride 패딩만 걷어내고 복사 */
        for (int32_t sy = 0; sy < src_h; sy++) {
            memcpy(dst + (size_t)sy * src_w * 3,
                   src + (size_t)sy * src_stride,
                   (size_t)src_w * 3);
        }
        break;
    }
    }
}
