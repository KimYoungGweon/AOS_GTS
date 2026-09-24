/*
 * gts_ui_p3.c — PAGE 3 · AOS 가스 Data 측정 (사양서 5.3)
 *
 * 측정 종료는 서버가 AOS_MEAS_STATE(done=1) 로 알려준다. 콘솔은
 * 버튼만 START 로 되돌리고 진행 바는 100% 로 남긴다.
 */
#include "gts_ui.h"
#include "gts_theme.h"
#include "gts_config.h"
#include "gts_protocol.h"

#include <stdio.h>

#define CY(y) ((y) - GL_CONTENT_Y)

static lv_obj_t *s_root;
static lv_obj_t *s_type_card[GTS_AOS_TYPE_COUNT];
static lv_obj_t *s_type_name[GTS_AOS_TYPE_COUNT];
static lv_obj_t *s_elapsed, *s_remain, *s_bar, *s_cap_left;
static lv_obj_t *s_btn;

/* ── 이벤트 ──────────────────────────────────────────────────────── */
static void on_type(lv_event_t *e)
{
    gts_aos_type_t t = (gts_aos_type_t)(intptr_t)lv_event_get_user_data(e);
    bool changed = false;

    gts_state_lock();
    if (!g_gts.aos_running) {
        g_gts.aos_type    = t;
        g_gts.aos_total_s = gts_aos_type_seconds(t);
        changed = true;
    }
    gts_state_unlock();

    if (changed) gts_proto_aos_set_type((uint8_t)t);
    gts_state_mark_dirty();
}

static void on_run(lv_event_t *e)
{
    (void)e;
    bool next;

    gts_state_lock();
    next = !g_gts.aos_running;
    g_gts.aos_running = next;
    if (next) {
        g_gts.aos_done      = false;
        g_gts.aos_elapsed_s = 0;
        g_gts.aos_samples   = 0;
    }
    gts_state_unlock();

    gts_proto_aos_meas_run(next);
    gts_state_mark_dirty();
}

/* ── 생성 ────────────────────────────────────────────────────────── */
lv_obj_t *gts_p3_create(lv_obj_t *parent)
{
    s_root = gts_group(parent, 0, 0, GTS_SCR_W, GL_CONTENT_H);

    gts_label(s_root, 12, CY(56), 774, "Data Type  ( 4 types )", GF_LABEL, GC_N600);

    for (int i = 0; i < GTS_AOS_TYPE_COUNT; i++) {
        int x = 12 + i * 195;
        s_type_card[i] = gts_card(s_root, x, CY(85), 187, 100);
        lv_obj_set_clickable(s_type_card[i], true);
        lv_obj_add_event_cb(s_type_card[i], on_type, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        s_type_name[i] = gts_label(s_type_card[i], 14, 12, 160,
                                   gts_aos_type_name((gts_aos_type_t)i),
                                   GF_CARD_TITLE, GC_TEXT);
        gts_label(s_type_card[i], 14, 50, 160,
                  gts_aos_type_duration((gts_aos_type_t)i), GF_BODY, GC_N600);
        gts_label(s_type_card[i], 14, 72, 160,
                  gts_aos_type_note((gts_aos_type_t)i), GF_SMALL, GC_N600);
    }

    /* ── 측정 상태 카드 ──────────────────────────────────────── */
    lv_obj_t *card = gts_card(s_root, 12, CY(195), 568, 207);
    gts_corner_marks(card, 568, 207);

    gts_label(card, 15, 13, 300, "Elapsed", GF_LABEL, GC_N600);
    s_elapsed = gts_label(card, 15, 33, 340, "00:00:00", GF_BIG, GC_TEXT);

    gts_label_right(card, 364, 22, 189, "Remaining", GF_LABEL, GC_N600);
    s_remain = gts_label_right(card, 364, 42, 189, "00:00:00",
                               GF_REMAIN, GC_ACCENT_700);

    s_bar = gts_bar(card, 15, 156, 538, 10);

    s_cap_left = gts_label(card, 15, 172, 300, "samples : 0", GF_SMALL, GC_N600);
    gts_label_right(card, 315, 172, 238,
                    "auto-stop on completion", GF_SMALL, GC_N600);

    /* ── START / STOP ────────────────────────────────────────── */
    s_btn = gts_button(s_root, 590, CY(195), 196, 207, "START", GF_BIG);
    lv_obj_add_event_cb(s_btn, on_run, LV_EVENT_CLICKED, NULL);

    return s_root;
}

/* ── 갱신 ────────────────────────────────────────────────────────── */
void gts_p3_refresh(void)
{
    char buf[48];

    gts_state_lock();
    gts_aos_type_t type    = g_gts.aos_type;
    bool           running = g_gts.aos_running;
    bool           done    = g_gts.aos_done;
    uint32_t       elapsed = g_gts.aos_elapsed_s;
    uint32_t       total   = g_gts.aos_total_s;
    uint32_t       samples = g_gts.aos_samples;
    gts_state_unlock();

    if (total == 0) total = gts_aos_type_seconds(type);
    if (elapsed > total) elapsed = total;

    for (int i = 0; i < GTS_AOS_TYPE_COUNT; i++) {
        bool on = (i == (int)type);
        gts_set_selected(s_type_card[i], on);
        lv_obj_set_style_text_color(s_type_name[i],
                                    on ? GC_ACCENT_900 : GC_TEXT, 0);
        gts_set_enabled(s_type_card[i], !running, LV_OPA_50);
    }

    gts_fmt_hhmmss(buf, sizeof(buf), elapsed);
    lv_label_set_text(s_elapsed, buf);

    gts_fmt_hhmmss(buf, sizeof(buf), total - elapsed);
    lv_label_set_text(s_remain, buf);

    int32_t pct = (total > 0) ? (int32_t)((uint64_t)elapsed * 1000u / total) : 0;
    if (done) pct = 1000;                       /* 종료 시 100% 유지 */
    lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);

    snprintf(buf, sizeof(buf), "samples : %u", (unsigned)samples);
    lv_label_set_text(s_cap_left, buf);

    gts_button_set_text(s_btn, running ? "STOP" : "START");
    if (running) gts_button_set_colors(s_btn, GC_ACCENT_900, GC_BG, GC_ACCENT_900);
    else         gts_button_set_fill(s_btn, true);
}
