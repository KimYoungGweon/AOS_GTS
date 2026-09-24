/*
 * gts_ui_p4.c — PAGE 4 · AOS Manual Mode (사양서 5.4)
 *
 * 타일 6개 = jog 대상 후보. 선택된 타일의 값이 Jog 회전으로 변하고
 * step 은 외부 Jog S/W 로 순환한다 (gts_input.c).
 *
 * 사양서와의 차이 한 가지: 요구사항 문서에 있는 LF_Shape(4종)가
 * 목업에는 타일로 없어서, 안내 박스를 줄이고 그 자리에 LF_SHAPE
 * 순환 버튼을 넣었다. 타일 6개 배치는 사양서 그대로다.
 */
#include "gts_ui.h"
#include "gts_theme.h"
#include "gts_config.h"
#include "gts_protocol.h"

#include <stdio.h>

#define CY(y) ((y) - GL_CONTENT_Y)

#define TILE_W  253
#define TILE_H  140

typedef struct {
    lv_obj_t *card;
    lv_obj_t *name;
    lv_obj_t *range;
    lv_obj_t *value;
    lv_obj_t *unit;
    lv_obj_t *sub;
    lv_obj_t *badge;
    lv_obj_t *badge_lbl;
} tile_t;

static lv_obj_t *s_root;
static tile_t    s_tile[GTS_P_COUNT];
static lv_obj_t *s_lf_btn, *s_shape_btn, *s_hint;

/* ── 이벤트 ──────────────────────────────────────────────────────── */
static void on_tile(lv_event_t *e)
{
    gts_param_id_t id = (gts_param_id_t)(intptr_t)lv_event_get_user_data(e);

    gts_state_lock();
    bool locked = !g_gts.lf_on && (id == GTS_P_LF_FRQ || id == GTS_P_LF_VOLT);
    if (!locked) g_gts.param_sel = id;
    gts_state_unlock();

    gts_state_mark_dirty();
}

static void on_lf(lv_event_t *e)
{
    (void)e;
    bool next;

    gts_state_lock();
    next = !g_gts.lf_on;
    g_gts.lf_on = next;
    /* Off 로 가면서 LF 항목이 선택돼 있었다면 HV 로 되돌린다 */
    if (!next && (g_gts.param_sel == GTS_P_LF_FRQ ||
                  g_gts.param_sel == GTS_P_LF_VOLT)) {
        g_gts.param_sel = GTS_P_HV;
    }
    gts_state_unlock();

    gts_proto_aos_set_lf_mode(next);
    gts_state_mark_dirty();
}

static void on_shape(lv_event_t *e)
{
    (void)e;
    gts_lf_shape_t next;

    gts_state_lock();
    next = (gts_lf_shape_t)((g_gts.lf_shape + 1) % GTS_LF_SHAPE_COUNT);
    g_gts.lf_shape = next;
    gts_state_unlock();

    gts_proto_aos_set_lf_shape((uint8_t)next);
    gts_state_mark_dirty();
}

/* ── 생성 ────────────────────────────────────────────────────────── */
static void make_tile(lv_obj_t *parent, gts_param_id_t id, int x, int y)
{
    const gts_param_spec_t *sp = &GTS_PARAM_SPEC[id];
    tile_t *t = &s_tile[id];
    char buf[48];

    t->card = gts_card(parent, x, CY(y), TILE_W, TILE_H);
    lv_obj_set_clickable(t->card, true);
    lv_obj_add_event_cb(t->card, on_tile, LV_EVENT_CLICKED, (void *)(intptr_t)id);

    t->name = gts_label(t->card, 14, 10, 120, sp->name, GF_TILE_LABEL, GC_TEXT);

    snprintf(buf, sizeof(buf), "%.*f - %.*f %s",
             sp->decimals, sp->min, sp->decimals, sp->max, sp->unit);
    t->range = gts_label_right(t->card, 113, 14, 126, buf, GF_BODY, GC_N600);

    t->value = gts_label(t->card, 14, 46, 0, "0", GF_TILE_VALUE, GC_TEXT);
    t->unit  = gts_label(t->card, 150, 68, 0, sp->unit, GF_BODY, GC_N600);

#if GTS_USE_KR_FONT
    t->sub = gts_label(t->card, 14, 110, 130, sp->name_kr, GF_BODY, GC_N600);
#else
    t->sub = gts_label(t->card, 14, 112, 130, "", GF_SMALL, GC_N600);
#endif

    t->badge     = gts_fill(t->card, TILE_W - 14 - 110, TILE_H - 14 - 27,
                            110, 27, GC_N200);
    t->badge_lbl = gts_label(t->badge, 0, 3, 110, "STEP -", GF_LABEL, GC_N600);
    lv_obj_set_style_text_align(t->badge_lbl, LV_TEXT_ALIGN_CENTER, 0);
}

lv_obj_t *gts_p4_create(lv_obj_t *parent)
{
    s_root = gts_group(parent, 0, 0, GTS_SCR_W, GL_CONTENT_H);

    static const int col_x[3] = { 12, 273, 533 };
    static const int row_y[2] = { 56, 204 };

    for (int i = 0; i < GTS_P_COUNT; i++) {
        make_tile(s_root, (gts_param_id_t)i, col_x[i % 3], row_y[i / 3]);
    }

    s_lf_btn = gts_button(s_root, 12, CY(352), 230, 50, "LF_MODE  OFF", GF_CHIP);
    lv_obj_add_event_cb(s_lf_btn, on_lf, LV_EVENT_CLICKED, NULL);

    s_shape_btn = gts_button(s_root, 250, CY(352), 180, 50, "SHAPE  Square", GF_CHIP);
    lv_obj_add_event_cb(s_shape_btn, on_shape, LV_EVENT_CLICKED, NULL);

    lv_obj_t *box = gts_card(s_root, 438, CY(352), 348, 50);
    s_hint = gts_label(box, 12, 15, 324,
                       "LF_Mode Off disables LF_Frq / LF_Volt",
                       GF_SMALL, GC_N600);

    return s_root;
}

/* ── 갱신 ────────────────────────────────────────────────────────── */
void gts_p4_refresh(void)
{
    char buf[48];

    gts_state_lock();
    gts_param_t    p[GTS_P_COUNT];
    for (int i = 0; i < GTS_P_COUNT; i++) p[i] = g_gts.param[i];
    gts_param_id_t sel    = g_gts.param_sel;
    bool           lf_on  = g_gts.lf_on;
    gts_lf_shape_t shape  = g_gts.lf_shape;
    gts_state_unlock();

    for (int i = 0; i < GTS_P_COUNT; i++) {
        const gts_param_spec_t *sp = &GTS_PARAM_SPEC[i];
        tile_t *t = &s_tile[i];

        bool is_lf  = (i == GTS_P_LF_FRQ || i == GTS_P_LF_VOLT);
        bool locked = is_lf && !lf_on;
        bool on     = (i == (int)sel) && !locked;

        snprintf(buf, sizeof(buf), "%.*f", sp->decimals, p[i].value);
        lv_label_set_text(t->value, buf);

        /* 값 폭이 바뀌므로 단위 라벨을 값 오른쪽 +6 으로 다시 붙인다 */
        lv_obj_update_layout(t->value);
        lv_obj_set_pos(t->unit, 14 + lv_obj_get_width(t->value) + 6, 68);

        snprintf(buf, sizeof(buf), "STEP %.*f",
                 sp->decimals, sp->steps[p[i].step_idx]);
        lv_label_set_text(t->badge_lbl, buf);

        gts_set_selected(t->card, on);
        lv_obj_set_style_bg_color(t->badge, on ? GC_ACCENT : GC_N200, 0);
        lv_obj_set_style_text_color(t->badge_lbl, on ? GC_BG : GC_N600, 0);

        gts_set_enabled(t->card, !locked, LV_OPA_50);
    }

    snprintf(buf, sizeof(buf), "LF_MODE  %s", lf_on ? "ON" : "OFF");
    gts_button_set_text(s_lf_btn, buf);
    gts_button_set_fill(s_lf_btn, lf_on);

    snprintf(buf, sizeof(buf), "SHAPE  %s", GTS_LF_SHAPE_NAME[shape]);
    gts_button_set_text(s_shape_btn, buf);
    gts_set_enabled(s_shape_btn, lf_on, LV_OPA_50);

    lv_label_set_text(s_hint, lf_on
        ? "Jog changes the selected tile - Jog S/W cycles STEP"
        : "LF_Mode Off disables LF_Frq / LF_Volt");
}
