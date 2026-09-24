/*
 * gts_ui_p5.c — PAGE 5 · WiFi 접속 List (사양서 5.5)
 *
 * 접속 장소가 2~3곳으로 고정이므로 전체 스캔 목록이 아니라 등록된 지점
 * 목록을 보여준다. 비밀번호 입력 화면은 없다 — SSID·비밀번호는
 * gts_config.h 에 있고, 여기서는 표시와 선택·접속만 한다.
 *
 * "SCAN / 다시 검색" 은 등록 지점이 지금 잡히는지 확인하는 용도다.
 * 스캔 결과는 RSSI 와 "not found" 표시에만 쓴다.
 */
#include "gts_ui.h"
#include "gts_theme.h"
#include "gts_config.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CY(y) ((y) - GL_CONTENT_Y)

#define ROW_W   516
#define ROW_H   74
#define CARD_X  540
#define CARD_W  246

static lv_obj_t *s_root;
static lv_obj_t *s_hdr_right;

typedef struct {
    lv_obj_t *row;
    lv_obj_t *ssid;
    lv_obj_t *note;
    lv_obj_t *badge;
    lv_obj_t *badge_lbl;
    lv_obj_t *rssi;
} netrow_t;

static netrow_t  s_row[GTS_WIFI_NET_COUNT];
static lv_obj_t *s_card_ssid, *s_card_note, *s_card_sig, *s_card_sec, *s_card_ip;
static lv_obj_t *s_btn, *s_hint;

/* ── 이벤트 ──────────────────────────────────────────────────────── */
static void on_row(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    gts_state_lock();
    g_gts.wifi_sel = idx;
    gts_state_unlock();
    gts_state_mark_dirty();
}

static void on_connect(lv_event_t *e)
{
    (void)e;
    uint8_t sel, conn;
    gts_wifi_conn_t st;

    gts_state_lock();
    sel  = g_gts.wifi_sel;
    conn = g_gts.wifi_conn_idx;
    st   = g_gts.wifi_state;
    gts_state_unlock();

    if (st == GTS_WIFI_CONNECTED && conn == sel) wifi_manager_disconnect();
    else                                         wifi_manager_connect(sel);
}

/* ── 생성 ────────────────────────────────────────────────────────── */
static void make_row(lv_obj_t *parent, uint8_t i, int y)
{
    netrow_t *r = &s_row[i];

    r->row = gts_card(parent, 12, CY(y), ROW_W, ROW_H);
    lv_obj_set_clickable(r->row, true);
    lv_obj_add_event_cb(r->row, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    r->ssid = gts_label(r->row, 14, 10, 300,
                        GTS_WIFI_NET[i].ssid, GF_CARD_TITLE, GC_TEXT);
    r->note = gts_label(r->row, 14, 46, 320,
                        GTS_WIFI_NET[i].name, GF_BODY, GC_N600);

    /* 보안·연결 배지 */
    r->badge     = gts_fill(r->row, ROW_W - 14 - 108 - 8 - 96, 42, 96, 24, GC_N200);
    r->badge_lbl = gts_label(r->badge, 0, 3, 96, "WPA2", GF_LABEL, GC_N600);
    lv_obj_set_style_text_align(r->badge_lbl, LV_TEXT_ALIGN_CENTER, 0);

    /* RSSI — 폭 108 우측 정렬 */
    r->rssi = gts_label_right(r->row, ROW_W - 14 - 108, 22, 108,
                              "--", GF_BTN, GC_TEXT);
}

lv_obj_t *gts_p5_create(lv_obj_t *parent)
{
    s_root = gts_group(parent, 0, 0, GTS_SCR_W, GL_CONTENT_H);

    gts_label(s_root, 12, CY(56), 300, "Saved Networks", GF_LABEL, GC_N600);
    s_hdr_right = gts_label_right(s_root, 240, CY(56), 288, "", GF_LABEL, GC_N600);

    static const int row_y[3] = { 81, 161, 241 };
    for (uint8_t i = 0; i < GTS_WIFI_NET_COUNT && i < 3; i++)
        make_row(s_root, i, row_y[i]);

    /* ── Selected 카드 ───────────────────────────────────────── */
    lv_obj_t *card = gts_card(s_root, CARD_X, CY(56), CARD_W, 346);
    gts_corner_marks(card, CARD_W, 346);

    gts_label(card, 15, 14, 216, "Selected", GF_LABEL, GC_N600);
    s_card_ssid = gts_label(card, 15, 36, 216, "-", GF_CARD_TITLE, GC_TEXT);
    s_card_note = gts_label(card, 15, 72, 216, "-", GF_BODY, GC_N600);

    gts_fill(card, 15, 104, 216, 1, GC_DIVIDER);

    gts_label(card, 15, 116, 100, "Signal", GF_BODY, GC_N600);
    s_card_sig = gts_label_right(card, 115, 116, 116, "--", GF_BODY, GC_TEXT);

    gts_label(card, 15, 144, 100, "Security", GF_BODY, GC_N600);
    s_card_sec = gts_label_right(card, 115, 144, 116, "WPA2", GF_BODY, GC_TEXT);

    gts_label(card, 15, 172, 100, "IP", GF_BODY, GC_N600);
    s_card_ip = gts_label_right(card, 95, 172, 136, "-", GF_BODY, GC_TEXT);

    s_btn = gts_button(card, 555 - CARD_X, 322 - 56, 216, 66, "CONNECT", GF_BTN);
    lv_obj_add_event_cb(s_btn, on_connect, LV_EVENT_CLICKED, NULL);

    s_hint = gts_label(card, 555 - CARD_X, 394 - 56, 216,
                       "연결되면 PAGE 1 로 자동 전환", GF_LABEL, GC_N600);

    return s_root;
}

/* ── 갱신 ────────────────────────────────────────────────────────── */
static void fmt_clock(char *out, size_t cap, int64_t epoch)
{
    /* SNTP 가 붙기 전이면 1970 이 나온다 — 그때는 시각을 숨긴다. */
    if (epoch < 1000000000LL) { out[0] = '\0'; return; }
    time_t t = (time_t)epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(out, cap, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

void gts_p5_refresh(void)
{
    char buf[64], clk[8];

    gts_state_lock();
    uint8_t  sel   = g_gts.wifi_sel;
    uint8_t  conn  = g_gts.wifi_conn_idx;
    uint8_t  last  = g_gts.wifi_last_idx;
    int64_t  ltime = g_gts.wifi_last_time;
    gts_wifi_conn_t st = g_gts.wifi_state;
    bool     scanning  = g_gts.wifi_scanning;
    uint8_t  nfound    = g_gts.wifi_scan_found;
    int64_t  stime     = g_gts.wifi_scan_time;
    bool     found[GTS_WIFI_NET_COUNT];
    int8_t   rssi[GTS_WIFI_NET_COUNT];
    memcpy(found, g_gts.wifi_found, sizeof(found));
    memcpy(rssi,  g_gts.wifi_rssi_list, sizeof(rssi));
    int8_t   conn_rssi = g_gts.wifi_rssi;
    char     ip[16];
    memcpy(ip, g_gts.wifi_ip, sizeof(ip));
    gts_state_unlock();

    /* 머리말 우측 — "n found · 시각" */
    if (scanning) {
        snprintf(buf, sizeof(buf), "scanning...");
    } else if (stime) {
        fmt_clock(clk, sizeof(clk), stime);
        snprintf(buf, sizeof(buf), "%u found%s%s",
                 (unsigned)nfound, clk[0] ? " - " : "", clk);
    } else {
        snprintf(buf, sizeof(buf), "not scanned");
    }
    lv_label_set_text(s_hdr_right, buf);

    for (uint8_t i = 0; i < GTS_WIFI_NET_COUNT && i < 3; i++) {
        netrow_t *r = &s_row[i];
        bool is_sel  = (i == sel);
        bool is_conn = (st == GTS_WIFI_CONNECTED && i == conn);

        gts_set_selected(r->row, is_sel);

        /* 설명 줄 — 장소 이름 + 최근 접속 표시 */
        if (i == last && ltime) {
            fmt_clock(clk, sizeof(clk), ltime);
            if (clk[0]) snprintf(buf, sizeof(buf), "%s - 최근 접속 %s",
                                 GTS_WIFI_NET[i].name, clk);
            else        snprintf(buf, sizeof(buf), "%s - 최근 접속",
                                 GTS_WIFI_NET[i].name);
        } else {
            snprintf(buf, sizeof(buf), "%s - 등록됨", GTS_WIFI_NET[i].name);
        }
        lv_label_set_text(r->note, buf);

        /* 배지 — 연결됨 / 연결 중 / WPA2 */
        const char *bl = "WPA2";
        lv_color_t bg = GC_N200, fg = GC_N600;
        if (is_conn)                                   { bl = "CONNECTED"; bg = GC_ACCENT;  fg = GC_BG; }
        else if (st == GTS_WIFI_CONNECTING && i == sel) { bl = "CONNECTING"; bg = GC_N300;  fg = GC_TEXT; }
        lv_label_set_text(r->badge_lbl, bl);
        lv_obj_set_style_bg_color(r->badge, bg, 0);
        lv_obj_set_style_text_color(r->badge_lbl, fg, 0);

        /* RSSI — 연결된 지점은 실시간 값, 나머지는 마지막 스캔 값 */
        int8_t v = is_conn ? conn_rssi : rssi[i];
        if (is_conn || found[i]) snprintf(buf, sizeof(buf), "%d dBm", v);
        else if (stime)          snprintf(buf, sizeof(buf), "--");
        else                     snprintf(buf, sizeof(buf), "--");
        lv_label_set_text(r->rssi, buf);

        /* 스캔했는데 안 잡힌 지점은 흐리게 (선택은 계속 가능) */
        lv_obj_set_style_opa(r->row,
            (stime && !found[i] && !is_conn) ? LV_OPA_50 : LV_OPA_COVER, 0);
    }

    /* ── Selected 카드 ───────────────────────────────────────── */
    lv_label_set_text(s_card_ssid, GTS_WIFI_NET[sel].ssid);
    lv_label_set_text(s_card_note, GTS_WIFI_NET[sel].name);

    bool sel_conn = (st == GTS_WIFI_CONNECTED && conn == sel);
    if (sel_conn || found[sel]) {
        snprintf(buf, sizeof(buf), "%d dBm", sel_conn ? conn_rssi : rssi[sel]);
    } else {
        snprintf(buf, sizeof(buf), stime ? "not found" : "--");
    }
    lv_label_set_text(s_card_sig, buf);
    lv_label_set_text(s_card_sec, "WPA2");
    lv_label_set_text(s_card_ip, sel_conn && ip[0] ? ip : "-");

    if (sel_conn) {
        gts_button_set_text(s_btn, "DISCONNECT");
        gts_button_set_colors(s_btn, GC_ACCENT_900, GC_BG, GC_ACCENT_900);
        lv_label_set_text(s_hint, "연결됨 - PAGE 1 로 이동합니다");
    } else if (st == GTS_WIFI_CONNECTING) {
        gts_button_set_text(s_btn, "CONNECTING...");
        gts_button_set_fill(s_btn, true);
        lv_label_set_text(s_hint, "접속 중...");
    } else {
        gts_button_set_text(s_btn, "CONNECT");
        gts_button_set_fill(s_btn, true);
        lv_label_set_text(s_hint, "연결되면 PAGE 1 로 자동 전환");
    }
}
