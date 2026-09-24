/*
 * gts_rotate.h — 렌더 버퍼 회전
 *
 * LVGL 9.6 은 partial 렌더 모드에서 소프트웨어 회전을 해 주지 않는다.
 * lv_display_rotate_area() 로 영역 좌표만 바꿔 주고 픽셀은 그대로 넘기므로,
 * flush 콜백이 직접 돌려야 한다.
 *
 * 이 매핑은 lv_display_rotate_area() 와 반드시 같은 회전이어야 한다.
 * 한쪽만 바꾸면 각 부분 갱신이 올바른 사각형 안에 엉뚱한 방향으로 들어가
 * 화면이 깨지고 겹쳐 보인다. 그래서 LVGL 을 쓰지 않는 별도 파일로 떼어
 * 놓고 test/host/ 에서 LVGL 원본 공식과 대조한다.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 값은 lv_display_rotation_t 와 같지만, 이 파일은 LVGL 에 의존하지 않는다. */
typedef enum {
    GTS_ROT_0   = 0,
    GTS_ROT_90  = 1,
    GTS_ROT_180 = 2,
    GTS_ROT_270 = 3,
} gts_rot_t;

/**
 * RGB888 버퍼를 회전해 dst 에 쓴다.
 *
 * @param src        원본 (논리 좌표로 렌더된 area)
 * @param dst        결과. 폭은 90/270 이면 src_h, 0/180 이면 src_w.
 *                   행 패딩 없음 (stride = 목적지 폭 * 3).
 * @param src_w      원본 폭(픽셀)
 * @param src_h      원본 높이(픽셀)
 * @param src_stride 원본 행 간격(byte). LVGL 의 정렬 패딩을 반영한 값.
 * @param rot        회전 방향
 */
void gts_rotate_rgb888(const uint8_t *src, uint8_t *dst,
                       int32_t src_w, int32_t src_h, int32_t src_stride,
                       gts_rot_t rot);

#ifdef __cplusplus
}
#endif
