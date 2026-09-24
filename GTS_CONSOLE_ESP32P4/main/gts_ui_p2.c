/*
 * gts_ui_p2.c — PAGE 2 · GFC Mode / 펌프 제어 (사양서 5.2)
 *
 * Pump1 · Pump2 는 항상 동시 구동이므로 화면에도 펌프 하나로 보인다.
 * Manual 모드에서는 시간 타일 2개가 40% 로 비활성되고 jog 가 무시된다.
 */
#include "gts_ui.h"
#include "gts_theme.h"
#include "gts_config.h"
#include "gts_protocol.h"
#include "gts_input.h"

#include <stdio.h>

#define CY(y) ((y) - GL_CONTENT_Y)

#define CYCLE_PERIOD_S  600u        /* 10분 주기 */

static lv_obj_t *s_root;
static lv_obj_t *s_tab_manual, *s_tab_auto;
static lv_obj_t *s_tile[GTS_GFC_SEL_COUNT];
static lv_obj_t *s_tile_val[GTS_GFC_SEL_COUNT];
static lv_obj_t *s_side_label, *s_side_big, *s_side_bar, *s_side_sub;
static lv_obj_t *s_main_btn, *s_note;

/* ── 이벤트 ──────────────────────────────────────────────────────── */
static void on_mode(lv_event_t *e)
{
    gts_gfc_mode_t m = (gts_gfc_mode_t)(intptr_t)lv_event_get_user_data(e);

    gts_state_lock();
    g_gts.gfc_mode = m;
    gts_state_unlock();

    gts_proto_gfc_set_mode((uint8_t)m);
    gts_state_mark_dirty();
}

static void on_tile(lv_event_t *e)
{
    gts_gfc_sel_t s = (gts_gfc_sel_t)(intptr_t)lv_event_get_user_data(e);

    gts_state_lock();
    if (g_gts.gfc_mode == GTS_GFC_AUTO) g_gts.gfc_sel = s;
    gts_state_unlock();

    gts_state_mark_dirty();
}

static void on_main_btn(lv_event_t *e)
{
    (void)e;
    gts_gfc_mode_t mode;
    bool run, pump;

    gts_state_lock();
    mode = g_gts.gfc_mode;
    run  = g_gts.gfc_auto_run;
    pump = g_gts.gfc_pump_on;
    gts_state_unlock();

    if (mode == GTS_GFC_AUTO) {
        bool next = !run;
        gts_state_lock();
        g_gts.gfc_auto_run = next;
        if (!next) g_gts.gfc_pump_on = false;   /* Stop = 둘 다 Off */
        gts_state_unlock();

        if (next) {
            uint16_t st, cy;
            gts_state_lock();
            st = g_gts.gfc_start_ds;
            cy = g_gts.gfc_cycle_ds;
            gts_state_unlock();
            gts_proto_gfc_set_times(st, cy);    /* 최신 시간 먼저 반영 */
        }
        gts_proto_gfc_auto_run(next);
    } else {
        bool next = !pump;
        gts_state_lock();
        g_gts.gfc_pump_on = next;
        gts_state_unlock();
        gts_proto_gfc_set_pump(next);
    }

    gts_state_mark_dirty();
}

/* ── 생성 ────────────────────────────────────────────────────────── */
static lv_obj_t *make_tile(lv_obj_t *parent, int y, const char *label,
                           gts_gfc_sel_t sel, lv_obj_t **out_val)
{
    lv_obj_t *t = gts_card(parent, 12, CY(y), 528, 144);
    lv_obj_set_clickable(t, true);
    lv_obj_add_event_cb(t, on_tile, LV_EVENT_CLICKED, (void *)(intptr_t)sel);

    gts_label(t, 18, 26, 480, label, GF_LABEL, GC_N600);
    *out_val = gts_label(t, 18, 56, 300, "0.0", GF_BIG, GC_TEXT);
    gts_label(t, 18, 116, 300, "sec  -  0.1 to 60", GF_BODY, GC_N600);
    return t;
}

lv_obj_t *gts_p2_create(lv_obj_t *parent)
{
    s_root = gts_group(parent, 0, 0, GTS_SCR_W, GL_CONTENT_H);

    gts_label(s_root, 12, CY(67), 300, "Control Mode", GF_LABEL, GC_N600);

    s_tab_manual = gts_button(s_root, 545, CY(57), 120, 38, "MANUAL", GF_CHIP);
    s_tab_auto   = gts_button(s_root, 665, CY(57), 120, 38, "AUTO",   GF_CHIP);
    lv_obj_add_event_cb(s_tab_manual, on_mode, LV_EVENT_CLICKED,
                        (void *)(intptr_t)GTS_GFC_MANUAL);
    lv_obj_add_event_cb(s_tab_auto,   on_mode, LV_EVENT_CLICKED,
                        (void *)(intptr_t)GTS_GFC_AUTO);

    s_tile[GTS_GFC_SEL_START] = make_tile(s_root, 106,
        "Start - Pump On Time", GTS_GFC_SEL_START, &s_tile_val[GTS_GFC_SEL_START]);
    s_tile[GTS_GFC_SEL_CYCLE] = make_tile(s_root, 258,
        "10min Cycle - Pump On Time", GTS_GFC_SEL_CYCLE, &s_tile_val[GTS_GFC_SEL_CYCLE]);

    /* ── 사이드 카드 ─────────────────────────────────────────── */
    lv_obj_t *side = gts_card(s_root, 550, CY(106), 236, 296);
    gts_corner_marks(side, 236, 296);

    s_side_label = gts_label(side, 13, 20, 210, "Cycle", GF_LABEL, GC_N600);
    s_side_big   = gts_label(side, 13, 48, 210, "--:--", GF_BIG, GC_ACCENT_700);
    s_side_bar   = gts_bar(side, 13, 120, 210, 6);
    s_side_sub   = gts_label(side, 13, 136, 210, "", GF_SMALL, GC_N600);

    s_main_btn = gts_button(side, 563 - 550, 295 - 106, 210, 66,
                            "AUTO START", GF_BTN);
    lv_obj_add_event_cb(s_main_btn, on_main_btn, LV_EVENT_CLICKED, NULL);

    s_note = gts_label(side, 563 - 550, 367 - 106, 210,
                       "Stop turns both pumps off", GF_LABEL, GC_N600);

    return s_root;
}

/* ── 갱신 ────────────────────────────────────────────────────────── */
void gts_p2_refresh(void)
{
    char buf[48];

    gts_state_lock();
    gts_gfc_mode_t mode  = g_gts.gfc_mode;
    gts_gfc_sel_t  sel   = g_gts.gfc_sel;
    uint16_t       st_ds = g_gts.gfc_start_ds;
    uint16_t       cy_ds = g_gts.gfc_cycle_ds;
    bool           run   = g_gts.gfc_auto_run;
    bool           pump  = g_gts.gfc_pump_on;
    uint16_t       rem   = g_gts.gfc_remain_ds;
    uint32_t       ccnt  = g_gts.gfc_cycle_count;
    gts_state_unlock();

    bool is_auto = (mode == GTS_GFC_AUTO);

    gts_button_set_fill(s_tab_manual, !is_auto);
    gts_button_set_fill(s_tab_auto,    is_auto);

    /* 시간 타일 */
    snprintf(buf, sizeof(buf), "%u.%u", st_ds / 10u, st_ds % 10u);
    lv_label_set_text(s_tile_val[GTS_GFC_SEL_START], buf);
    snprintf(buf, sizeof(buf), "%u.%u", cy_ds / 10u, cy_ds % 10u);
    lv_label_set_text(s_tile_val[GTS_GFC_SEL_CYCLE], buf);

    for (int i = 0; i < GTS_GFC_SEL_COUNT; i++) {
        gts_set_selected(s_tile[i], is_auto && (i == (int)sel));
        gts_set_enabled(s_tile[i], is_auto, LV_OPA_40);
    }

    if (is_auto) {
        lv_label_set_text(s_side_label, "Cycle  -  every 10 min");

        uint32_t rs = rem / 10u;
        snprintf(buf, sizeof(buf), "%02u:%02u",
                 (unsigned)(rs / 60u), (unsigned)(rs % 60u));
        lv_label_set_text(s_side_big, buf);
        lv_obj_set_style_text_color(s_side_big, GC_ACCENT_700, 0);

        lv_obj_set_hidden(s_side_bar, false);
        uint32_t done = (rs > CYCLE_PERIOD_S) ? 0 : (CYCLE_PERIOD_S - rs);
        lv_bar_set_value(s_side_bar, (int32_t)(done * 1000u / CYCLE_PERIOD_S),
                         LV_ANIM_OFF);

        snprintf(buf, sizeof(buf), "cycles : %u   pump %s",
                 (unsigned)ccnt, pump ? "ON" : "OFF");
        lv_label_set_text(s_side_sub, buf);

        gts_button_set_text(s_main_btn, run ? "AUTO STOP" : "AUTO START");
        if (run) gts_button_set_colors(s_main_btn, GC_ACCENT_900, GC_BG, GC_ACCENT_900);
        else     gts_button_set_fill(s_main_btn, true);
    } else {
        lv_label_set_text(s_side_label, "Pump State");
        lv_label_set_text(s_side_big, pump ? "ON" : "OFF");
        lv_obj_set_style_text_color(s_side_big, pump ? GC_ACCENT : GC_N600, 0);

        lv_obj_set_hidden(s_side_bar, true);
        lv_label_set_text(s_side_sub, "Pump1 + Pump2");

        gts_button_set_text(s_main_btn, pump ? "PUMP OFF" : "PUMP ON");
        if (pump) gts_button_set_colors(s_main_btn, GC_ACCENT_900, GC_BG, GC_ACCENT_900);
        else      gts_button_set_fill(s_main_btn, true);
    }

    lv_label_set_text(s_note, "Stop turns both pumps off");
}
