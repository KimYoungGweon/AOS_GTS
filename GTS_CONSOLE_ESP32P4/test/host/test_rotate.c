/*
 * test_rotate.c — 회전 매핑이 LVGL 과 일치하는지 검증
 *
 * gts_rotate_rgb888() 은 픽셀을 돌리고, LVGL 은 lv_display_rotate_area()
 * 로 영역 좌표를 돌린다. 둘이 같은 회전이어야 화면이 맞는다.
 * 여기서는 LVGL 9.6 의 lv_display_rotate_area() 를 그대로 옮겨 적은 참조
 * 구현으로 기대 위치를 계산하고, 실제 회전 결과와 픽셀 단위로 비교한다.
 *
 * 참조 원본: managed_components/lvgl__lvgl/src/display/lv_display.c:1366
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gts_rotate.h"

#define PANEL_W 480     /* disp->hor_res */
#define PANEL_H 800     /* disp->ver_res */

typedef struct { int x1, y1, x2, y2; } area_t;

/* lv_display_rotate_area() 를 그대로 옮긴 것 */
static void lv_rotate_area(area_t *a, gts_rot_t rot)
{
    int w = a->x2 - a->x1 + 1;
    int h = a->y2 - a->y1 + 1;
    switch (rot) {
    case GTS_ROT_90:
        a->y2 = PANEL_H - a->x1 - 1;
        a->x1 = a->y1;
        a->x2 = a->x1 + h - 1;
        a->y1 = a->y2 - w + 1;
        break;
    case GTS_ROT_180:
        a->y2 = PANEL_H - a->y1 - 1;
        a->y1 = a->y2 - h + 1;
        a->x2 = PANEL_W - a->x1 - 1;
        a->x1 = a->x2 - w + 1;
        break;
    case GTS_ROT_270:
        a->x1 = PANEL_W - a->y2 - 1;
        a->y2 = a->x2;
        a->x2 = a->x1 + h - 1;
        a->y1 = a->y2 - w + 1;
        break;
    default: break;
    }
}

/* 논리 좌표 한 점 -> 물리 좌표. 위 영역 변환과 같은 회전이어야 한다. */
static void lv_rotate_point(int lx, int ly, gts_rot_t rot, int *px, int *py)
{
    switch (rot) {
    case GTS_ROT_90:  *px = ly;                *py = PANEL_H - 1 - lx; break;
    case GTS_ROT_180: *px = PANEL_W - 1 - lx;  *py = PANEL_H - 1 - ly; break;
    case GTS_ROT_270: *px = PANEL_W - 1 - ly;  *py = lx;               break;
    default:          *px = lx;                *py = ly;               break;
    }
}

static const char *rot_name(gts_rot_t r)
{
    switch (r) {
    case GTS_ROT_90:  return "90";
    case GTS_ROT_180: return "180";
    case GTS_ROT_270: return "270";
    default:          return "0";
    }
}

static int fails;

/* 픽셀 하나를 (sx,sy) 로 유일하게 인코딩한다 */
static void encode(uint8_t *p, int sx, int sy)
{
    p[0] = (uint8_t)(sx & 0xFF);
    p[1] = (uint8_t)(sy & 0xFF);
    p[2] = (uint8_t)(((sx >> 8) & 0x0F) | (((sy >> 8) & 0x0F) << 4));
}

static int same(const uint8_t *p, int sx, int sy)
{
    uint8_t e[3]; encode(e, sx, sy);
    return p[0]==e[0] && p[1]==e[1] && p[2]==e[2];
}

/*
 * 논리 영역 하나를 회전시켜, 모든 픽셀이 LVGL 이 말하는 물리 위치에
 * 정확히 놓이는지 검사한다.
 */
static void check_area(int lx1, int ly1, int lw, int lh,
                       gts_rot_t rot, int stride_pad)
{
    int lx2 = lx1 + lw - 1, ly2 = ly1 + lh - 1;

    int src_stride = lw * 3 + stride_pad;
    uint8_t *src = calloc(1, (size_t)src_stride * lh);
    /* 목적지 폭은 90/270 이면 lh, 0/180 이면 lw */
    int dst_w = (rot == GTS_ROT_90 || rot == GTS_ROT_270) ? lh : lw;
    int dst_h = (rot == GTS_ROT_90 || rot == GTS_ROT_270) ? lw : lh;
    uint8_t *dst = calloc(1, (size_t)dst_w * dst_h * 3);
    if (!src || !dst) { printf("FAIL: alloc\n"); fails++; return; }

    for (int sy = 0; sy < lh; sy++)
        for (int sx = 0; sx < lw; sx++)
            encode(src + (size_t)sy * src_stride + (size_t)sx * 3, sx, sy);

    gts_rotate_rgb888(src, dst, lw, lh, src_stride, rot);

    /* LVGL 이 계산한 물리 영역 */
    area_t phys = { lx1, ly1, lx2, ly2 };
    lv_rotate_area(&phys, rot);

    int pw = phys.x2 - phys.x1 + 1, ph = phys.y2 - phys.y1 + 1;
    if (pw != dst_w || ph != dst_h) {
        printf("FAIL rot=%s: 물리 영역 %dx%d, 회전 결과 %dx%d\n",
               rot_name(rot), pw, ph, dst_w, dst_h);
        fails++; goto done;
    }
    if (phys.x1 < 0 || phys.y1 < 0 || phys.x2 >= PANEL_W || phys.y2 >= PANEL_H) {
        printf("FAIL rot=%s: 물리 영역이 패널 밖 (%d,%d)-(%d,%d)\n",
               rot_name(rot), phys.x1, phys.y1, phys.x2, phys.y2);
        fails++; goto done;
    }

    for (int sy = 0; sy < lh; sy++) {
        for (int sx = 0; sx < lw; sx++) {
            int px, py;
            lv_rotate_point(lx1 + sx, ly1 + sy, rot, &px, &py);
            int dx = px - phys.x1, dy = py - phys.y1;
            if (dx < 0 || dy < 0 || dx >= dst_w || dy >= dst_h) {
                printf("FAIL rot=%s: (%d,%d) -> 영역 밖 (%d,%d)\n",
                       rot_name(rot), sx, sy, dx, dy);
                fails++; goto done;
            }
            const uint8_t *p = dst + ((size_t)dy * dst_w + dx) * 3;
            if (!same(p, sx, sy)) {
                printf("FAIL rot=%s pad=%d: src(%d,%d) 가 dst(%d,%d) 에 없음\n",
                       rot_name(rot), stride_pad, sx, sy, dx, dy);
                fails++; goto done;
            }
        }
    }
done:
    free(src); free(dst);
}

int main(void)
{
    const gts_rot_t rots[4] = { GTS_ROT_0, GTS_ROT_90, GTS_ROT_180, GTS_ROT_270 };

    /* 논리 화면은 회전 후 800x480 (90/270) 또는 480x800 (0/180) */
    for (int i = 0; i < 4; i++) {
        gts_rot_t r = rots[i];
        int LW = (r == GTS_ROT_90 || r == GTS_ROT_270) ? 800 : 480;
        int LH = (r == GTS_ROT_90 || r == GTS_ROT_270) ? 480 : 800;

        /* 전체 화면 한 장 */
        check_area(0, 0, LW, LH, r, 0);
        /* 실제 partial 갱신 모양 — 전폭 30줄 */
        check_area(0, 0,   LW, 30, r, 0);
        check_area(0, LH-30, LW, 30, r, 0);
        /* 작은 조각, 화면 구석구석 */
        check_area(0, 0, 1, 1, r, 0);
        check_area(LW-1, LH-1, 1, 1, r, 0);
        check_area(12, 44, 253, 140, r, 0);      /* P4 타일 크기 */
        check_area(LW-100, LH-64, 100, 64, r, 0);
        /* stride 에 정렬 패딩이 붙은 경우 */
        check_area(7, 13, 61, 50, r, 3);
        check_area(7, 13, 61, 50, r, 12);
    }

    printf(fails ? "\n회전 검사 %d건 FAILED\n" : "\n회전 검사 전부 통과\n", fails);
    return fails ? 1 : 0;
}
