/*
 * gts_ui.c — 공용 골격 (상단바 · 콘텐츠 · 조그바 · 화면 전환)
 */
#include "gts_ui.h"
#include "gts_theme.h"
#include "gts_config.h"
#include "gts_protocol.h"
#include "gts_input.h"
#include "gts_sim.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "GTS-UI";

static lv_obj_t *s_root;
static lv_obj_t *s_content;
static lv_obj_t *s_page[GTS_PAGE_COUNT];

/* 상단바 요소 */
static lv_obj_t *s_tb_myid, *s_tb_title, *s_tb_dev, *s_tb_dot,
                *s_tb_link, *s_tb_wifi_btn;

/* 조그바 요소 */
static lv_obj_t *s_jb_name, *s_jb_value, *s_jb_range;
static lv_obj_t *s_jb_step_box, *s_jb_step_val;
static lv_obj_t *s_jb_device_btn, *s_jb_goto_btn;
static lv_obj_t *s_jb_goto_l1, *s_jb_goto_l2;

/* ════════════════════════════════════════════════════════════════════
 * 상단바 (사양서 2.1)
 * ════════════════════════════════════════════════════════════════════ */
static void on_wifi_chip(lv_event_t *e)
{
    (void)e;
    /* 사양서에는 P5 로 가는 버튼이 없다. Wi-Fi 표시를 눌러 들어가게 했다 —
     * 현장에서 지점을 바꾸고 싶을 때와, 오프라인 벤치에서 P5 를 열어 볼 때
     * 모두 필요하다. (자동 전환은 P1 에서만 일어난다.)                */
    gts_ui_goto(GTS_PAGE_WIFI);
}

static void topbar_create(lv_obj_t *parent)
{
    lv_obj_t *bar = gts_fill(parent, 0, 0, GTS_SCR_W, GL_TOPBAR_H, GC_ACCENT_900);

    /* 사양서 Rev 0.2: Brand 14,10,148,25 · Cond 700 / 25.
     * Montserrat 은 Condensed 보다 넓어 148 에 안 들어간다. 폭을 풀고
     * 뒤따르는 MyID 칩·화면명 x 를 밀었다.                            */
    gts_label(bar, 14, 6, 0, "GTS CONSOLE", GF_BRAND, GC_BG);

    lv_obj_t *chip = gts_fill(bar, 200, 9, 86, 26, GC_CHIP);
    s_tb_myid = gts_label(chip, 0, 0, 86, "MyID : 01", GF_CHIP, GC_BG);
    lv_obj_set_style_text_align(s_tb_myid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_tb_myid, LV_ALIGN_CENTER, 0, 0);

    s_tb_title = gts_label(bar, 298, 13, 180, "DEVICE SELECT", GF_SMALL, GC_BG);
    lv_obj_set_style_text_opa(s_tb_title, LV_OPA_80, 0);

    lv_obj_t *devbox = gts_group(bar, 482, 8, 105, 29);
    lv_obj_set_style_border_width(devbox, 1, 0);
    lv_obj_set_style_border_color(devbox, GC_BG, 0);
    lv_obj_set_style_border_opa(devbox, LV_OPA_50, 0);
    s_tb_dev = gts_label(devbox, 0, 0, 105, "AOS - ID 01", GF_CHIP, GC_BG);
    lv_obj_set_style_text_align(s_tb_dev, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_tb_dev, LV_ALIGN_CENTER, 0, 0);

    /* 표시등 + 라벨을 묶어 누르면 P5 로 간다 */
    s_tb_wifi_btn = gts_group(bar, 595, 4, 66, 36);
    lv_obj_set_clickable(s_tb_wifi_btn, true);
    lv_obj_add_event_cb(s_tb_wifi_btn, on_wifi_chip, LV_EVENT_CLICKED, NULL);

    s_tb_dot = gts_fill(s_tb_wifi_btn, 6, 14, 8, 8, GC_N600);
    lv_obj_set_style_radius(s_tb_dot, 4, 0);
    s_tb_link = gts_label(s_tb_wifi_btn, 20, 7, 46, "IDLE", GF_BODY, GC_BG);

    lv_obj_t *srv = gts_label(bar, 661, 12, 123,
                              GTS_UDP_SERVER_IP ":5502", GF_SMALL, GC_BG);
    lv_obj_set_style_text_opa(srv, LV_OPA_60, 0);
    lv_obj_set_style_text_align(srv, LV_TEXT_ALIGN_RIGHT, 0);
}

static const char *page_title(gts_page_t p)
{
    switch (p) {
        case GTS_PAGE_DEVICE:     return "DEVICE SELECT";
        case GTS_PAGE_GFC:        return "GFC MODE";
        case GTS_PAGE_AOS_MEAS:   return "AOS - GAS DATA";
        case GTS_PAGE_AOS_MANUAL: return "AOS - MANUAL";
        case GTS_PAGE_WIFI:       return "WIFI NETWORKS";
        default:                  return "";
    }
}

static void topbar_refresh(void)
{
    char buf[48];

    gts_state_lock();
    uint8_t    my    = g_gts.my_id;
    uint8_t    did   = g_gts.dev_id;
    gts_dev_type_t dt = g_gts.dev_type;
    gts_link_t link  = g_gts.link;
    uint32_t   rtt   = g_gts.rtt_ms;
    gts_page_t page  = g_gts.page;
    gts_wifi_conn_t wst = g_gts.wifi_state;
    char       ip[16];
    memcpy(ip, g_gts.wifi_ip, sizeof(ip));
    gts_state_unlock();

    snprintf(buf, sizeof(buf), "MyID : %02u", my);
    lv_label_set_text(s_tb_myid, buf);

    lv_label_set_text(s_tb_title, page_title(page));

    /* 사양서 5.5: P5 에서는 Device chip 자리에 WiFi 상태 + IP 를 쓴다 */
    if (page == GTS_PAGE_WIFI) {
        lv_label_set_text(s_tb_dev,
                          (wst == GTS_WIFI_CONNECTED) ? "WIFI OK" : "NO WIFI");
    } else {
        snprintf(buf, sizeof(buf), "%s - ID %02u",
                 (dt == GTS_DEV_GFC) ? "GFC" : "AOS", did);
        lv_label_set_text(s_tb_dev, buf);
    }

    /* 표시등·라벨 — Wi-Fi 가 끊겼으면 그쪽이 우선이다. UDP 링크보다
     * 먼저 해결해야 하는 문제이기 때문.                              */
    lv_color_t lc;
    if (wst != GTS_WIFI_CONNECTED) {
        lc = (wst == GTS_WIFI_CONNECTING) ? GC_N600 : GC_FAIL;
        snprintf(buf, sizeof(buf),
                 (wst == GTS_WIFI_CONNECTING) ? "WIFI.." : "NO WIFI");
    } else if (page == GTS_PAGE_WIFI) {
        lc = GC_OK;
        snprintf(buf, sizeof(buf), "%s", ip[0] ? ip : "IP 미할당");
    } else {
#if GTS_OFFLINE_MODE
        lc = GC_SIM;
        (void)rtt; (void)link;
        snprintf(buf, sizeof(buf), "OFFLINE");
#else
        switch (link) {
            case GTS_LINK_ONLINE:   lc = GC_OK;   break;
            case GTS_LINK_TESTING:  lc = GC_N600; break;
            case GTS_LINK_NO_REPLY: lc = GC_FAIL; break;
            default:                lc = GC_N600; break;
        }
        if (link == GTS_LINK_ONLINE && rtt)
            snprintf(buf, sizeof(buf), "LINK %ums", (unsigned)rtt);
        else if (link == GTS_LINK_NO_REPLY)
            snprintf(buf, sizeof(buf), "NO REPLY");
        else if (link == GTS_LINK_TESTING)
            snprintf(buf, sizeof(buf), "TEST..");
        else
            snprintf(buf, sizeof(buf), "IDLE");
#endif
    }
    lv_obj_set_style_bg_color(s_tb_dot, lc, 0);
    lv_label_set_text(s_tb_link, buf);

    /* IP 는 길어서 칩 폭을 넘는다 — P5 에서만 넓힌다 */
    lv_obj_set_width(s_tb_wifi_btn, (page == GTS_PAGE_WIFI) ? 120 : 66);
    lv_obj_set_width(s_tb_link,     (page == GTS_PAGE_WIFI) ? 100 : 46);
}

/*
 * gts_ui.c — 공용 골격
 */

/* ════════════════════════════════════════════════════════════════════
 * 조그바 (사양서 2.2)
 * ════════════════════════════════════════════════════════════════════ */
static void on_device_btn(lv_event_t *e)
{
    (void)e;
    gts_ui_leave_device();
}

static void on_goto_btn(lv_event_t *e)
{
    (void)e;
    gts_page_t p;
    bool running;

    gts_state_lock();
    p       = g_gts.page;
    running = g_gts.aos_running;
    gts_state_unlock();

    if (p == GTS_PAGE_AOS_MEAS) {
        if (running) return;             /* 측정 중 전환 금지 */
        gts_ui_goto(GTS_PAGE_AOS_MANUAL);
    } else if (p == GTS_PAGE_AOS_MANUAL) {
        gts_ui_goto(GTS_PAGE_AOS_MEAS);
    } else if (p == GTS_PAGE_WIFI) {
        wifi_manager_scan();
    }
}

/* 사양서 Rev 0.2 조그바 치수
 *   여백 좌우 10 · 요소 간격 8
 *   "Jog target" 10,424,116,19 · 대상명 10,443,116,26
 *   구분선 156,426,1,42 · 값 171,422 · 단위·범위 값 우측 +7, 442
 *   전환 버튼 140 x 53 (20% 확대) · STEP 배지 674,421,112,53
 * y 는 조그바(416) 기준 상대값으로 옮겨 쓴다.                        */
#define JB_BTN_W    140
#define JB_BTN_H    53
#define JB_BTN_Y    (421 - GL_JOGBAR_Y)
#define JB_GAP      8
#define JB_RIGHT    786
#define JB_STEP_X   674

static void jogbar_create(lv_obj_t *parent)
{
    lv_obj_t *bar = gts_fill(parent, 0, GL_JOGBAR_Y, GTS_SCR_W, GL_JOGBAR_H, GC_N100);
    gts_fill(bar, 0, 0, GTS_SCR_W, 1, GC_DIVIDER);      /* 상단 1px 구분선 */

    gts_label(bar, 10, 8, 116, "Jog target", GF_LABEL, GC_N600);
    s_jb_name = gts_label(bar, 10, 27, 140, "-", GF_JOG_NAME, GC_TEXT);

    gts_fill(bar, 156, 10, 1, 42, GC_DIVIDER);

    s_jb_value = gts_label(bar, 171, 6, 0, "-", GF_JOG_VALUE, GC_TEXT);
    s_jb_range = gts_label(bar, 171, 30, 0, "", GF_BODY, GC_N600);

    /* STEP 배지 — P4 전용, 최우측 */
    s_jb_step_box = gts_fill(bar, JB_STEP_X, JB_BTN_Y, 112, JB_BTN_H, GC_ACCENT);
    gts_label(s_jb_step_box, 8, 8, 96, "STEP", GF_LABEL, GC_BG);
    s_jb_step_val = gts_label(s_jb_step_box, 8, 26, 96, "-", GF_CHIP, GC_BG);

    /* 전환 버튼 2개 — 140 x 53 */
    s_jb_device_btn = gts_button(bar, 0, JB_BTN_Y, JB_BTN_W, JB_BTN_H, "", GF_LABEL);
    lv_obj_t *dl = lv_obj_get_child(s_jb_device_btn, 0);
    lv_label_set_text(dl, "DEVICE");
    lv_obj_align(dl, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *dl2 = gts_label(s_jb_device_btn, 0, 28, JB_BTN_W,
                              "change", GF_BODY, GC_N600);
    lv_obj_set_style_text_align(dl2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(s_jb_device_btn, on_device_btn, LV_EVENT_CLICKED, NULL);

    s_jb_goto_btn = gts_button(bar, 0, JB_BTN_Y, JB_BTN_W, JB_BTN_H, "", GF_LABEL);
    s_jb_goto_l1 = lv_obj_get_child(s_jb_goto_btn, 0);
    lv_label_set_text(s_jb_goto_l1, "GO TO");
    lv_obj_align(s_jb_goto_l1, LV_ALIGN_TOP_MID, 0, 7);
    s_jb_goto_l2 = gts_label(s_jb_goto_btn, 0, 26, JB_BTN_W, "MANUAL", GF_CHIP, GC_TEXT);
    lv_obj_set_style_text_align(s_jb_goto_l2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(s_jb_goto_btn, on_goto_btn, LV_EVENT_CLICKED, NULL);
}

static void jogbar_refresh(void)
{
    char name[32], value[48], range[48], step[24];
    bool has_step, enabled;

    gts_state_jog_target_text(name, sizeof(name), value, sizeof(value),
                              range, sizeof(range), step, sizeof(step),
                              &has_step, &enabled);

    lv_label_set_text(s_jb_name,  name);
    lv_label_set_text(s_jb_value, value);
    lv_label_set_text(s_jb_range, range);

    lv_obj_set_style_opa(s_jb_name,  enabled ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(s_jb_value, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(s_jb_range, enabled ? LV_OPA_COVER : LV_OPA_40, 0);

    gts_state_lock();
    gts_page_t page    = g_gts.page;
    bool       running = g_gts.aos_running;
    gts_state_unlock();

    /* STEP 배지 — P4 에만 */
    bool show_step = has_step && (page == GTS_PAGE_AOS_MANUAL);
    lv_obj_set_hidden(s_jb_step_box, !show_step);
    if (show_step) lv_label_set_text(s_jb_step_val, step);

    /* 전환 버튼 배치
     *   P1 없음 · P2 DEVICE · P3 DEVICE + GO TO MANUAL
     *   P4 DEVICE + GO TO 자동측정 (STEP 배지 좌측) · P5 SCAN            */
    bool show_dev  = (page == GTS_PAGE_GFC || page == GTS_PAGE_AOS_MEAS ||
                      page == GTS_PAGE_AOS_MANUAL);
    bool show_goto = (page == GTS_PAGE_AOS_MEAS || page == GTS_PAGE_AOS_MANUAL ||
                      page == GTS_PAGE_WIFI);

    lv_obj_set_hidden(s_jb_device_btn, !show_dev);
    lv_obj_set_hidden(s_jb_goto_btn,   !show_goto);

    int right = show_step ? (JB_STEP_X - JB_GAP) : JB_RIGHT;
    int leftmost = JB_RIGHT;

    if (show_goto) {
        lv_obj_set_pos(s_jb_goto_btn, right - JB_BTN_W, JB_BTN_Y);
        leftmost = right - JB_BTN_W;
        right -= (JB_BTN_W + JB_GAP);
    }
    if (show_dev) {
        lv_obj_set_pos(s_jb_device_btn, right - JB_BTN_W, JB_BTN_Y);
        leftmost = right - JB_BTN_W;
    }

    /* 값·범위 텍스트가 버튼을 파고들지 않게 폭을 잘라 준다.
     * 버튼이 140 으로 커지면서 여유가 줄었다.                        */
    lv_obj_update_layout(s_jb_value);
    lv_coord_t vw = lv_obj_get_width(s_jb_value);
    int range_x = 171 + vw + 7;
    int avail   = leftmost - JB_GAP - range_x;
    if (avail < 40) avail = 40;
    lv_obj_set_pos(s_jb_range, range_x, 30);
    lv_obj_set_width(s_jb_range, avail);
    lv_label_set_long_mode(s_jb_range, LV_LABEL_LONG_MODE_CLIP);

    if (show_goto) {
        const char *l2;
        bool en = true;
        if (page == GTS_PAGE_AOS_MEAS) {
            l2 = "MANUAL >";
            en = !running;              /* 측정 중 전환 금지 (사양서 5.3) */
        } else if (page == GTS_PAGE_AOS_MANUAL) {
            l2 = "MEASURE";
        } else {
            l2 = "RESCAN";
        }
        lv_label_set_text(s_jb_goto_l1, (page == GTS_PAGE_WIFI) ? "SCAN" : "GO TO");
        lv_label_set_text(s_jb_goto_l2, l2);
        gts_set_enabled(s_jb_goto_btn, en, LV_OPA_40);
    }
}

/* ════════════════════════════════════════════════════════════════════
 * 화면 전환
 * ════════════════════════════════════════════════════════════════════ */
void gts_ui_goto(gts_page_t page)
{
    if (page >= GTS_PAGE_COUNT) return;

    gts_state_lock();
    g_gts.page = page;
    gts_state_unlock();

    for (int i = 0; i < GTS_PAGE_COUNT; i++) {
        if (!s_page[i]) continue;
        if (i == (int)page) lv_obj_set_hidden(s_page[i], false);
        else                lv_obj_set_hidden(s_page[i], true);
    }

    ESP_LOGI(TAG, "page -> %d", (int)page);
    gts_ui_refresh();
}

void gts_ui_leave_device(void)
{
    /* 진행 중 동작을 먼저 정지시킨 뒤 해제한다 (사양서 5.2/5.3) */
    gts_state_lock();
    bool was_auto = g_gts.gfc_auto_run;
    bool was_meas = g_gts.aos_running;
    gts_state_unlock();

    if (was_auto) gts_proto_gfc_auto_run(false);
    if (was_meas) gts_proto_aos_meas_run(false);
    gts_proto_disconnect();

    gts_state_lock();
    g_gts.connect_ok   = false;
    g_gts.link         = GTS_LINK_IDLE;
    g_gts.gfc_auto_run = false;
    g_gts.gfc_pump_on  = false;
    g_gts.aos_running  = false;
    gts_state_unlock();

    gts_ui_goto(GTS_PAGE_DEVICE);
}

void gts_ui_refresh(void)
{
    topbar_refresh();
    jogbar_refresh();

    gts_state_lock();
    gts_page_t p = g_gts.page;
    gts_state_unlock();

    switch (p) {
        case GTS_PAGE_DEVICE:     gts_p1_refresh(); break;
        case GTS_PAGE_GFC:        gts_p2_refresh(); break;
        case GTS_PAGE_AOS_MEAS:   gts_p3_refresh(); break;
        case GTS_PAGE_AOS_MANUAL: gts_p4_refresh(); break;
        case GTS_PAGE_WIFI:       gts_p5_refresh(); break;
        default: break;
    }
}

/* ── 주기 갱신 타이머 (LVGL 태스크) ─────────────────────────────── */
static void ui_tick(lv_timer_t *t)
{
    (void)t;

    /* 오프라인 모드에서는 서버가 보내 줄 진행 상태를 콘솔이 만든다.
     * GTS_OFFLINE_MODE 가 0 이면 빈 함수다.                         */
    gts_sim_tick(100);

    /* ── P1 ↔ P5 자동 전환 ───────────────────────────────────────
     * 요구사항: "Wifi 연결이 안 되면 1번 화면에서 5번으로 자동 변경.
     * 이 화면에서 Wifi 가 연결되면 1번 화면으로 전환."
     * P2~P4 작업 중에는 화면을 뺏지 않는다 — 측정이나 펌프 운전 도중
     * 갑자기 화면이 바뀌면 곤란하기 때문. 그때는 상단바의 NO WIFI 표시로
     * 알리고, 사용자가 그 표시를 누르면 P5 로 간다.                 */
    gts_state_lock();
    gts_page_t      page = g_gts.page;
    gts_wifi_conn_t wst  = g_gts.wifi_state;
    gts_state_unlock();

#if GTS_OFFLINE_MODE
    /* 오프라인 벤치에서는 Wi-Fi 가 없는 게 정상이다. 자동으로 P5 에
     * 붙잡히면 P1~P4 를 볼 수 없으므로 자동 전환을 끈다.
     * P5 는 상단바의 Wi-Fi 표시를 눌러 들어갈 수 있다.              */
    (void)page; (void)wst;
#else
    if (page == GTS_PAGE_DEVICE && wst == GTS_WIFI_DISCONNECTED) {
        gts_ui_goto(GTS_PAGE_WIFI);
    } else if (page == GTS_PAGE_WIFI && wst == GTS_WIFI_CONNECTED) {
        gts_ui_goto(GTS_PAGE_DEVICE);
    }
#endif

    static int beat;
    if (++beat >= 5) { beat = 0; gts_state_mark_dirty(); }  /* 500 ms 보장 갱신 */

    if (gts_state_take_dirty()) gts_ui_refresh();
}

/* ════════════════════════════════════════════════════════════════════
 * 생성
 * ════════════════════════════════════════════════════════════════════ */
void gts_ui_create(lv_display_t *disp)
{
    gts_theme_init();

    s_root = lv_display_get_screen_active(disp);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_root, GC_BG, 0);
    lv_obj_set_style_text_color(s_root, GC_TEXT, 0);
    lv_obj_set_scrollable(s_root, false);

    topbar_create(s_root);

    s_content = gts_group(s_root, 0, GL_CONTENT_Y, GTS_SCR_W, GL_CONTENT_H);

    jogbar_create(s_root);

    s_page[GTS_PAGE_DEVICE]     = gts_p1_create(s_content);
    s_page[GTS_PAGE_GFC]        = gts_p2_create(s_content);
    s_page[GTS_PAGE_AOS_MEAS]   = gts_p3_create(s_content);
    s_page[GTS_PAGE_AOS_MANUAL] = gts_p4_create(s_content);
    s_page[GTS_PAGE_WIFI]       = gts_p5_create(s_content);

    lv_timer_create(ui_tick, 100, NULL);

    gts_ui_goto(GTS_PAGE_DEVICE);
    ESP_LOGI(TAG, "UI ready (%dx%d)", GTS_SCR_W, GTS_SCR_H);
}
