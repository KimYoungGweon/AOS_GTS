/*
 * gts_theme.h — 색·폰트 토큰과 위젯 생성 헬퍼
 *
 * 사양서 3절(색상 토큰)·4절(타이포그래피)을 그대로 옮겼다.
 * 모든 요소는 radius 0, 그림자 없음, 테두리 1px(선택 시 2px accent).
 *
 * 폰트는 LVGL 내장 Montserrat 로 대체했다. 사양서가 지정한 Barlow /
 * Barlow Condensed 를 쓰려면 lv_font_conv 로 변환해 넣고 아래 매크로만
 * 바꾸면 된다. 사양서 px 와 Montserrat 의 가용 크기가 1~2px 다른 곳은
 * 가장 가까운 짝수 크기로 맞췄다.
 */
#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 색상 (사양서 3절) ───────────────────────────────────────────── */
#define GC_BG           lv_color_hex(0xF2F2F3)
#define GC_TEXT         lv_color_hex(0x1D1F20)
#define GC_ACCENT       lv_color_hex(0x5980A6)
#define GC_ACCENT_100   lv_color_hex(0xEEF6FF)
#define GC_ACCENT_700   lv_color_hex(0x416180)
#define GC_ACCENT_900   lv_color_hex(0x1D2D3D)
#define GC_N100         lv_color_hex(0xF5F5F8)
#define GC_N200         lv_color_hex(0xE7E7EA)
#define GC_N300         lv_color_hex(0xD4D4D7)
#define GC_N600         lv_color_hex(0x7A7A7D)
#define GC_DIVIDER      lv_color_hex(0xD0D0D1)
#define GC_CHIP         lv_color_hex(0x3F4C5A)
#define GC_OK           lv_color_hex(0x9FE0B4)
#define GC_FAIL         lv_color_hex(0xE0A39F)
#define GC_SIM          lv_color_hex(0xE8C44A)   /* 오프라인 모드 표시 */

/* ── 폰트 (사양서 4절 → 가장 가까운 Montserrat) ─────────────────── */
/* 상단 브랜드 — 사양서 Rev 0.2 에서 25px 로 커졌다 (글자가 넘쳐서).
 * Montserrat 은 Barlow Condensed 보다 넓으므로 26 으로 두고 뒤따르는
 * 요소들의 x 를 밀었다 (gts_ui.c topbar_create 참고).                */
#define GF_BRAND        (&lv_font_montserrat_26)
#define GF_LABEL        (&lv_font_montserrat_14)  /* 13 섹션 라벨·배지 */
#define GF_SMALL        (&lv_font_montserrat_14)  /* 14 보조 설명      */
#define GF_BODY         (&lv_font_montserrat_16)  /* 15~16 본문·표 값  */
#define GF_CHIP         (&lv_font_montserrat_18)  /* 18~19 칩·STEP     */
#define GF_TILE_LABEL   (&lv_font_montserrat_20)  /* 21 타일 항목명    */
#define GF_JOG_NAME     (&lv_font_montserrat_22)  /* 22 조그 대상명    */
#define GF_BTN          (&lv_font_montserrat_24)  /* 24 버튼           */
#define GF_CARD_TITLE   (&lv_font_montserrat_28)  /* 28 카드 제목      */
#define GF_REMAIN       (&lv_font_montserrat_34)  /* 34 남은 시간      */
#define GF_JOG_VALUE    (&lv_font_montserrat_38)  /* 38 조그바 값      */
#define GF_TILE_VALUE   (&lv_font_montserrat_40)  /* 40 타일 값        */
#define GF_BIG          (&lv_font_montserrat_48)  /* 48~56 큰 수치     */

/* ── 공용 레이아웃 (사양서 2절) ─────────────────────────────────── */
#define GL_TOPBAR_H     44
#define GL_CONTENT_Y    44
#define GL_CONTENT_H    372
#define GL_JOGBAR_Y     416
#define GL_JOGBAR_H     64
#define GL_PAD          12

/** 테마 초기화. lv_init() 이후 화면 생성 전에 1회. */
void gts_theme_init(void);

/* ── 위젯 헬퍼 ───────────────────────────────────────────────────────
 * 모두 절대 좌표를 받는다. 사양서의 x/y/w/h 표를 그대로 옮기기
 * 위해서다. flex 레이아웃을 쓰면 표와 대조하기 어려워진다.          */

/** 배경 없는 1px 테두리 패널 (카드). */
lv_obj_t *gts_card(lv_obj_t *parent, int x, int y, int w, int h);

/** 테두리도 배경도 없는 투명 그룹 컨테이너. */
lv_obj_t *gts_group(lv_obj_t *parent, int x, int y, int w, int h);

/** 단색 채움 사각형 (구분선·배지 바탕 등). */
lv_obj_t *gts_fill(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c);

/** 라벨. w<=0 이면 내용에 맞춰 자동 폭. */
lv_obj_t *gts_label(lv_obj_t *parent, int x, int y, int w,
                    const char *text, const lv_font_t *font, lv_color_t color);

/** 우측 정렬 라벨 (x,y 는 오른쪽 끝 기준이 아니라 박스 좌상단). */
lv_obj_t *gts_label_right(lv_obj_t *parent, int x, int y, int w,
                          const char *text, const lv_font_t *font, lv_color_t color);

/** 버튼 + 라벨 1개. radius 0, 그림자 없음. */
lv_obj_t *gts_button(lv_obj_t *parent, int x, int y, int w, int h,
                     const char *text, const lv_font_t *font);

/** 버튼 색을 채움/비움 상태로 바꾼다. */
void gts_button_set_fill(lv_obj_t *btn, bool filled);

/** 버튼 색을 임의 배경/글자색으로 지정 (ON 상태 accent-900 등). */
void gts_button_set_colors(lv_obj_t *btn, lv_color_t bg, lv_color_t fg, lv_color_t border);

/** 버튼의 라벨 텍스트 교체. */
void gts_button_set_text(lv_obj_t *btn, const char *text);

/** 카드/타일의 선택 상태 표시 (2px accent + accent-100 배경). */
void gts_set_selected(lv_obj_t *obj, bool selected);

/** 불투명도로 비활성 표시 + 터치 차단. */
void gts_set_enabled(lv_obj_t *obj, bool enabled, lv_opa_t dim_opa);

/** 진행 바. */
lv_obj_t *gts_bar(lv_obj_t *parent, int x, int y, int w, int h);

/**
 * 코너 마크(+) 4개를 카드 안에 그린다. 장식 요소.
 * 카드 크기를 인자로 받는다 — 생성 직후에는 lv_obj_get_width() 가 아직
 * 레이아웃 전이라 0 을 돌려주기 때문이다 (그러면 마크가 음수 좌표로 간다).
 */
void gts_corner_marks(lv_obj_t *card, int w, int h);

/* 초 → "hh:mm:ss" */
void gts_fmt_hhmmss(char *out, size_t cap, uint32_t sec);

#ifdef __cplusplus
}
#endif
