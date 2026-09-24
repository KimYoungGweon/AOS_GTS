/*
 * gts_ui_p1.c — PAGE 1 · Device Select / 장치 선택 (사양서 5.1)
 *
 * 좌표는 사양서의 절대 좌표에서 콘텐츠 영역 시작(y=44)을 뺀 값이다.
 * CY() 매크로가 그 변환을 한다 — 사양서 표와 1:1 로 대조하기 위해
 * 표의 숫자를 그대로 써 넣었다.
 */
#include "gts_ui.h"
#include "gts_theme.h"
#include "gts_config.h"
#include "gts_protocol.h"

#include <stdio.h>
#include <string.h>

#define CY(y) ((y) - GL_CONTENT_Y)

#define ID_COLS   5
#define ID_ROWS   4
#define ID_W      55
#define ID_H      44
#define ID_PITCH_X 61
#define ID_PITCH_Y 50

static lv_obj_t *s_root;
static lv_obj_t *s_btn_aos, *s_btn_gfc;
static lv_obj_t *s_btn_id[20];
static lv_obj_t *s_row_val[6];
static lv_obj_t *s_btn_retest, *s_btn_enter;

/* ── 이벤트 ──────────────────────────────────────────────────────── */
/* 사양서 5.1: "선택을 하면 장비와 테스트 컨넥션을 하여 장비상태를 읽어옴".
 * 종류든 번호든 바뀌면 바로 CONNECT 를 보낸다. Jog 로 번호를 돌릴 때는
 * gts_input.c 가 50 ms 디바운스 뒤에 같은 일을 한다.                 */
static void connect_to_selected(void)
{
    uint8_t t, id;
    gts_state_lock();
    t  = (uint8_t)g_gts.dev_type;
    id = g_gts.dev_id;
    gts_state_unlock();
    gts_proto_connect(t, id);
}

static void on_type(lv_event_t *e)
{
    gts_dev_type_t t = (gts_dev_type_t)(intptr_t)lv_event_get_user_data(e);

    gts_state_lock();
    g_gts.dev_type   = t;
    g_gts.connect_ok = false;
    g_gts.link       = GTS_LINK_IDLE;
    gts_state_unlock();

    connect_to_selected();
    gts_state_mark_dirty();
}

static void on_id(lv_event_t *e)
{
    uint8_t id = (uint8_t)(intptr_t)lv_event_get_user_data(e);

    gts_state_lock();
    g_gts.dev_id     = id;
    g_gts.connect_ok = false;
    g_gts.link       = GTS_LINK_IDLE;
    gts_state_unlock();

    connect_to_selected();
    gts_state_mark_dirty();
}

static void on_retest(lv_event_t *e)
{
    (void)e;
    connect_to_selected();
}

static void on_enter(lv_event_t *e)
{
    (void)e;
    bool ok;
    gts_dev_type_t t;

    gts_state_lock();
    ok = g_gts.connect_ok;
    t  = g_gts.dev_type;
    gts_state_unlock();

    if (!ok) return;

    if (t == GTS_DEV_GFC) {
        gts_proto_gfc_set_mode((uint8_t)GTS_GFC_AUTO);
        gts_ui_goto(GTS_PAGE_GFC);
    } else {
        /* 사양서 Rev 0.2 / Programming.md 추가변경 5:
         * AOS 는 Manual Mode 로 먼저 들어간다 (예전엔 가스 측정 화면). */
        gts_proto_aos_get_params();
        gts_ui_goto(GTS_PAGE_AOS_MANUAL);
    }
}

/* ── 생성 ────────────────────────────────────────────────────────── */
static void info_row(lv_obj_t *card, int idx, const char *key)
{
    /* 카드 기준 상대 좌표: 카드는 (324,56), 행은 (339, 96 + 38*idx) */
    int x = 339 - 324;
    int y = (96 - 56) + 38 * idx;
    int w = 432;

    gts_label(card, x, y + 10, 200, key, GF_SMALL, GC_N600);
    s_row_val[idx] = gts_label_right(card, x + w - 260, y + 8, 260,
                                     "-", GF_BODY, GC_TEXT);
    gts_fill(card, x, y + 37, w, 1, GC_DIVIDER);
}

lv_obj_t *gts_p1_create(lv_obj_t *parent)
{
    s_root = gts_group(parent, 0, 0, GTS_SCR_W, GL_CONTENT_H);

    /* ── 좌측: 장치 종류 ─────────────────────────────────────── */
    gts_label(s_root, 12, CY(56), 300, "Device Type", GF_LABEL, GC_N600);

    s_btn_aos = gts_button(s_root, 12,  CY(81), 147, 52, "AOS", GF_BTN);
    s_btn_gfc = gts_button(s_root, 167, CY(81), 145, 52, "GFC", GF_BTN);
    lv_obj_add_event_cb(s_btn_aos, on_type, LV_EVENT_CLICKED,
                        (void *)(intptr_t)GTS_DEV_AOS);
    lv_obj_add_event_cb(s_btn_gfc, on_type, LV_EVENT_CLICKED,
                        (void *)(intptr_t)GTS_DEV_GFC);

    /* ── 좌측: 장치 번호 5 x 4 ───────────────────────────────── */
    gts_label(s_root, 12, CY(147), 300, "Device ID  ( 1 - 20 )", GF_LABEL, GC_N600);

    for (int i = 0; i < 20; i++) {
        int c = i % ID_COLS;
        int r = i / ID_COLS;
        char t[4];
        snprintf(t, sizeof(t), "%02d", i + 1);

        s_btn_id[i] = gts_button(s_root,
                                 12 + c * ID_PITCH_X,
                                 CY(172) + r * ID_PITCH_Y,
                                 ID_W, ID_H, t, GF_BODY);
        lv_obj_add_event_cb(s_btn_id[i], on_id, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(i + 1));
    }

    /* ── 우측: Connection Test 카드 ──────────────────────────── */
    lv_obj_t *card = gts_card(s_root, 324, CY(56), 462, 346);
    gts_corner_marks(card, 462, 346);

    gts_label(card, 339 - 324, 69 - 56, 432,
              "Connection Test", GF_LABEL, GC_N600);

    static const char *keys[6] = {
        "Target", "UDP Server", "Link", "Model", "Device State", "Last Reply"
    };
    for (int i = 0; i < 6; i++) info_row(card, i, keys[i]);

    s_btn_retest = gts_button(card, 339 - 324, 343 - 56, 119, 46,
                              "RE-TEST", GF_LABEL);
    s_btn_enter  = gts_button(card, 466 - 324, 343 - 56, 305, 46,
                              "ENTER CONTROL", GF_BTN);
    lv_obj_add_event_cb(s_btn_retest, on_retest, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_btn_enter,  on_enter,  LV_EVENT_CLICKED, NULL);

    return s_root;
}

/* ── 갱신 ────────────────────────────────────────────────────────── */
void gts_p1_refresh(void)
{
    char buf[64];

    gts_state_lock();
    gts_dev_type_t dt   = g_gts.dev_type;
    uint8_t        did  = g_gts.dev_id;
    uint8_t        myid = g_gts.my_id;
    gts_link_t     link = g_gts.link;
    uint32_t       rtt  = g_gts.rtt_ms;
    uint8_t        dst  = g_gts.dev_state;
    uint32_t       lrep = g_gts.last_reply_ms;
    bool           ok   = g_gts.connect_ok;
    gts_state_unlock();

    gts_button_set_fill(s_btn_aos, dt == GTS_DEV_AOS);
    gts_button_set_fill(s_btn_gfc, dt == GTS_DEV_GFC);

    for (int i = 0; i < 20; i++)
        gts_button_set_fill(s_btn_id[i], (uint8_t)(i + 1) == did);

    /* Target */
    snprintf(buf, sizeof(buf), "%s - ID %02u",
             (dt == GTS_DEV_GFC) ? "GFC" : "AOS", did);
    lv_label_set_text(s_row_val[0], buf);

    /* UDP Server */
    lv_label_set_text(s_row_val[1], GTS_UDP_SERVER_IP " : 5502");

    /* Link */
    switch (link) {
        case GTS_LINK_ONLINE:
#if GTS_OFFLINE_MODE
            (void)rtt;
            snprintf(buf, sizeof(buf), "OFFLINE MODE - no server reply");
#else
            snprintf(buf, sizeof(buf), "ONLINE - RTT %u ms", (unsigned)rtt);
#endif
            break;
        case GTS_LINK_TESTING:  snprintf(buf, sizeof(buf), "TESTING..."); break;
        case GTS_LINK_NO_REPLY: snprintf(buf, sizeof(buf), "NO REPLY");   break;
        default:                snprintf(buf, sizeof(buf), "IDLE");       break;
    }
    lv_label_set_text(s_row_val[2], buf);

    /* Model */
    snprintf(buf, sizeof(buf), "GTS CONSOLE ID : %u", myid);
    lv_label_set_text(s_row_val[3], buf);

    /* Device State */
    static const char *dsn[4] = { "IDLE", "RUN", "ERROR", "OFFLINE" };
    snprintf(buf, sizeof(buf), "%s", dsn[dst & 3]);
    lv_label_set_text(s_row_val[4], buf);

    /* Last Reply */
    if (link == GTS_LINK_ONLINE) snprintf(buf, sizeof(buf), "%u ms ago", (unsigned)lrep);
    else                         snprintf(buf, sizeof(buf), "-");
    lv_label_set_text(s_row_val[5], buf);

    /* ENTER CONTROL 은 연결 성공 시에만 (사양서 7절) */
    gts_button_set_fill(s_btn_enter, ok);
    gts_set_enabled(s_btn_enter, ok, LV_OPA_40);
}
