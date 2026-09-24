/*
 * gts_theme.c
 */
#include "gts_theme.h"
#include <stdio.h>

static lv_style_t st_card, st_group, st_btn, st_btn_pr, st_bar_bg, st_bar_ind;
static bool s_inited;

void gts_theme_init(void)
{
    if (s_inited) return;
    s_inited = true;

    /* 카드 — 1px divider 테두리, 배경 없음, radius 0 */
    lv_style_init(&st_card);
    lv_style_set_radius(&st_card, 0);
    lv_style_set_bg_opa(&st_card, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_card, 1);
    lv_style_set_border_color(&st_card, GC_DIVIDER);
    lv_style_set_pad_all(&st_card, 0);
    lv_style_set_shadow_width(&st_card, 0);

    /* 투명 그룹 */
    lv_style_init(&st_group);
    lv_style_set_radius(&st_group, 0);
    lv_style_set_bg_opa(&st_group, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_group, 0);
    lv_style_set_pad_all(&st_group, 0);
    lv_style_set_shadow_width(&st_group, 0);

    /* 버튼 기본 — 비움 상태 */
    lv_style_init(&st_btn);
    lv_style_set_radius(&st_btn, 0);
    lv_style_set_bg_opa(&st_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&st_btn, GC_BG);
    lv_style_set_text_color(&st_btn, GC_TEXT);
    lv_style_set_border_width(&st_btn, 1);
    lv_style_set_border_color(&st_btn, GC_DIVIDER);
    lv_style_set_pad_all(&st_btn, 0);
    lv_style_set_shadow_width(&st_btn, 0);

    /* 눌림 피드백 — 사양서 6절: accent-700 채움 */
    lv_style_init(&st_btn_pr);
    lv_style_set_bg_color(&st_btn_pr, GC_ACCENT_700);
    lv_style_set_text_color(&st_btn_pr, GC_BG);
    lv_style_set_border_color(&st_btn_pr, GC_ACCENT_700);

    lv_style_init(&st_bar_bg);
    lv_style_set_radius(&st_bar_bg, 0);
    lv_style_set_bg_opa(&st_bar_bg, LV_OPA_COVER);
    lv_style_set_bg_color(&st_bar_bg, GC_N300);
    lv_style_set_border_width(&st_bar_bg, 0);

    lv_style_init(&st_bar_ind);
    lv_style_set_radius(&st_bar_ind, 0);
    lv_style_set_bg_opa(&st_bar_ind, LV_OPA_COVER);
    lv_style_set_bg_color(&st_bar_ind, GC_ACCENT);
}

static void no_scroll(lv_obj_t *o)
{
    lv_obj_set_scrollable(o, false);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t *gts_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, &st_card, 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    no_scroll(o);
    return o;
}

lv_obj_t *gts_group(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, &st_group, 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    no_scroll(o);
    return o;
}

lv_obj_t *gts_fill(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c)
{
    lv_obj_t *o = gts_group(parent, x, y, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    return o;
}

lv_obj_t *gts_label(lv_obj_t *parent, int x, int y, int w,
                    const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    }
    return l;
}

lv_obj_t *gts_label_right(lv_obj_t *parent, int x, int y, int w,
                          const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = gts_label(parent, x, y, w, text, font, color);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    return l;
}

lv_obj_t *gts_button(lv_obj_t *parent, int x, int y, int w, int h,
                     const char *text, const lv_font_t *font)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &st_btn, 0);
    lv_obj_add_style(b, &st_btn_pr, LV_STATE_PRESSED);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    no_scroll(b);

    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, text ? text : "");
    lv_obj_center(l);
    return b;
}

static lv_obj_t *btn_label(lv_obj_t *btn)
{
    return (lv_obj_get_child_count(btn) > 0) ? lv_obj_get_child(btn, 0) : NULL;
}

void gts_button_set_text(lv_obj_t *btn, const char *text)
{
    lv_obj_t *l = btn_label(btn);
    if (l) lv_label_set_text(l, text ? text : "");
}

void gts_button_set_colors(lv_obj_t *btn, lv_color_t bg, lv_color_t fg, lv_color_t border)
{
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_border_color(btn, border, 0);
    lv_obj_t *l = btn_label(btn);
    if (l) lv_obj_set_style_text_color(l, fg, 0);
}

void gts_button_set_fill(lv_obj_t *btn, bool filled)
{
    if (filled) gts_button_set_colors(btn, GC_ACCENT, GC_BG, GC_ACCENT);
    else        gts_button_set_colors(btn, GC_BG, GC_TEXT, GC_DIVIDER);
}

void gts_set_selected(lv_obj_t *obj, bool selected)
{
    if (selected) {
        lv_obj_set_style_border_width(obj, 2, 0);
        lv_obj_set_style_border_color(obj, GC_ACCENT, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(obj, GC_ACCENT_100, 0);
    } else {
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, GC_DIVIDER, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    }
}

void gts_set_enabled(lv_obj_t *obj, bool enabled, lv_opa_t dim_opa)
{
    lv_obj_set_style_opa(obj, enabled ? LV_OPA_COVER : dim_opa, 0);
    if (enabled) lv_obj_set_clickable(obj, true);
    else         lv_obj_set_clickable(obj, false);
}

lv_obj_t *gts_bar(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &st_bar_bg, 0);
    lv_obj_add_style(b, &st_bar_ind, LV_PART_INDICATOR);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_bar_set_range(b, 0, 1000);
    lv_bar_set_value(b, 0, LV_ANIM_OFF);
    return b;
}

void gts_corner_marks(lv_obj_t *card, int w, int h)
{
    /* 사양서: 11px 라인 2개로 만든 + 표시. 순수 장식. */
    const int L = 11;
    const int pos[4][2] = {
        { 6,         6         },
        { w - L - 6, 6         },
        { 6,         h - L - 6 },
        { w - L - 6, h - L - 6 },
    };
    for (int i = 0; i < 4; i++) {
        gts_fill(card, pos[i][0], pos[i][1] + L / 2, L, 1, GC_DIVIDER);
        gts_fill(card, pos[i][0] + L / 2, pos[i][1], 1, L, GC_DIVIDER);
    }
}

void gts_fmt_hhmmss(char *out, size_t cap, uint32_t sec)
{
    uint32_t h = sec / 3600u;
    uint32_t m = (sec % 3600u) / 60u;
    uint32_t s = sec % 60u;
    snprintf(out, cap, "%02u:%02u:%02u",
             (unsigned)h, (unsigned)m, (unsigned)s);
}
