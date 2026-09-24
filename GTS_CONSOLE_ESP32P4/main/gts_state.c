/*
 * gts_state.c — 전역 상태 구현
 */
#include "gts_state.h"
#include "gts_config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

gts_state_t g_gts;

static SemaphoreHandle_t s_lock;
static volatile bool     s_dirty;

/* ── 파라미터 스펙 (사양서 5.4) ──────────────────────────────────── */
const gts_param_spec_t GTS_PARAM_SPEC[GTS_P_COUNT] = {
    [GTS_P_HV]      = { "HV",      "고전압",   "V",   0.0f,  200.0f,
                        { 0.01f, 0.1f, 1.0f },   3, 2 },
    [GTS_P_FRQ]     = { "Frq",     "주파수",   "kHz", 200.0f, 800.0f,
                        { 0.1f,  1.0f, 10.0f },  3, 1 },
    /* Duty 20~80 % — STM32 펌웨어의 실제 한계 (MyCTL.c 의 MinV[2]/MaxV[2]).
     * 사양서 초안의 10~50 은 틀린 값이었다 (2026-09-21 확인).          */
    [GTS_P_DUTY]    = { "Duty",    "듀티",   "%",   20.0f,  80.0f,
                        { 0.01f, 0.1f },         2, 2 },
    [GTS_P_CV]      = { "CV",      "제어 전압", "V",   -5.0f,   5.0f,
                        { 0.001f, 0.01f, 0.1f }, 3, 3 },
    [GTS_P_LF_FRQ]  = { "LF_Frq",  "저주파 주파수", "Hz",  50.0f,  200.0f,
                        { 1.0f },                1, 0 },
    [GTS_P_LF_VOLT] = { "LF_Volt", "저주파 전압","V",   0.0f,    5.0f,
                        { 0.01f, 0.1f },         2, 2 },
};

/* ── Wi-Fi 등록 지점 (gts_config.h 의 값을 표로) ─────────────────── */
const gts_wifi_net_t GTS_WIFI_NET[GTS_WIFI_NET_COUNT] = {
    { GTS_WIFI_0_NAME, GTS_WIFI_0_SSID, GTS_WIFI_0_PASS },
    { GTS_WIFI_1_NAME, GTS_WIFI_1_SSID, GTS_WIFI_1_PASS },
    { GTS_WIFI_2_NAME, GTS_WIFI_2_SSID, GTS_WIFI_2_PASS },
};

const char *const GTS_LF_SHAPE_NAME[GTS_LF_SHAPE_COUNT] = {
    "Square", "Sine", "Triangle", "Trapezoid"
};

/* ── 측정 타입 ───────────────────────────────────────────────────── */
uint32_t gts_aos_type_seconds(gts_aos_type_t t)
{
    switch (t) {
        case GTS_AOS_PRETEST: return 5u * 60u;
        case GTS_AOS_1HOUR:   return 1u * 3600u;
        case GTS_AOS_2HOUR:   return 2u * 3600u;
        case GTS_AOS_8HOUR:   return 8u * 3600u;
        default:              return 0;
    }
}

const char *gts_aos_type_name(gts_aos_type_t t)
{
    static const char *n[GTS_AOS_TYPE_COUNT] = {
        "preTest", "1 hour", "2 hour", "8 hour"
    };
    return (t < GTS_AOS_TYPE_COUNT) ? n[t] : "-";
}

const char *gts_aos_type_duration(gts_aos_type_t t)
{
    static const char *d[GTS_AOS_TYPE_COUNT] = {
        "approx. 5 min", "60 min", "120 min", "480 min"
    };
    return (t < GTS_AOS_TYPE_COUNT) ? d[t] : "-";
}

const char *gts_aos_type_note(gts_aos_type_t t)
{
    static const char *d[GTS_AOS_TYPE_COUNT] = {
        "link / sensitivity", "short run", "standard run", "full data"
    };
    return (t < GTS_AOS_TYPE_COUNT) ? d[t] : "";
}

/* ── 생명주기 ────────────────────────────────────────────────────── */
void gts_state_init(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    configASSERT(s_lock);

    memset(&g_gts, 0, sizeof(g_gts));

    g_gts.page      = GTS_PAGE_DEVICE;
    g_gts.my_id     = GTS_MY_ID_DEFAULT;
    g_gts.dev_type  = GTS_DEV_AOS;
    g_gts.dev_id    = 1;
    g_gts.link      = GTS_LINK_IDLE;

    g_gts.wifi_state    = GTS_WIFI_DISCONNECTED;
    g_gts.wifi_sel      = 0;
    g_gts.wifi_conn_idx = 0xFF;
    g_gts.wifi_last_idx = 0xFF;

    /* GFC 기본값 — 사양서: 기본 선택은 AUTO */
    g_gts.gfc_mode     = GTS_GFC_AUTO;
    g_gts.gfc_sel      = GTS_GFC_SEL_START;
    g_gts.gfc_start_ds = 30;    /* 3.0 sec  */
    g_gts.gfc_cycle_ds = 50;    /* 5.0 sec  */

    /* AOS 측정 */
    g_gts.aos_type  = GTS_AOS_PRETEST;
    g_gts.aos_total_s = gts_aos_type_seconds(GTS_AOS_PRETEST);

    /* Manual 파라미터 — 각 범위의 안전한 시작값 */
    g_gts.param[GTS_P_HV].value      = 0.0f;
    g_gts.param[GTS_P_FRQ].value     = 200.0f;
    g_gts.param[GTS_P_DUTY].value    = 20.0f;
    g_gts.param[GTS_P_CV].value      = 0.0f;
    g_gts.param[GTS_P_LF_FRQ].value  = 50.0f;
    g_gts.param[GTS_P_LF_VOLT].value = 0.0f;
    for (int i = 0; i < GTS_P_COUNT; i++) g_gts.param[i].step_idx = 0;

    g_gts.param_sel = GTS_P_HV;
    g_gts.lf_on     = false;
    g_gts.lf_shape  = GTS_LF_SQUARE;

    s_dirty = true;
}

void gts_state_lock(void)   { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void gts_state_unlock(void) { xSemaphoreGiveRecursive(s_lock); }

void gts_state_mark_dirty(void) { s_dirty = true; }

bool gts_state_take_dirty(void)
{
    bool d = s_dirty;
    s_dirty = false;
    return d;
}

/* ── clamp ───────────────────────────────────────────────────────── */
float gts_param_clamp(gts_param_id_t id, float v)
{
    const gts_param_spec_t *s = &GTS_PARAM_SPEC[id];
    if (v < s->min) v = s->min;
    if (v > s->max) v = s->max;

    /* 표시 자리수로 반올림해 0.1 을 10번 더했을 때 1.0000001 이 되는
     * 누적 오차를 막는다.                                            */
    float scale = powf(10.0f, (float)s->decimals);
    return roundf(v * scale) / scale;
}

/* ── Jog ─────────────────────────────────────────────────────────── */
static uint16_t clamp_ds(int v)
{
    if (v < 1)   v = 1;      /* 0.1 sec */
    if (v > 600) v = 600;    /* 60.0 sec */
    return (uint16_t)v;
}

void gts_state_jog(int dir)
{
    if (dir == 0) return;

    gts_state_lock();

    switch (g_gts.page) {

    case GTS_PAGE_DEVICE: {
        /* 대상 = DEVICE ID, 1↔20 순환 (사양서 5.1) */
        int id = (int)g_gts.dev_id - 1 + dir;
        id = ((id % 20) + 20) % 20;
        g_gts.dev_id = (uint8_t)(id + 1);
        /* ID 를 바꾸면 이전 연결 확인 결과는 무효 */
        g_gts.connect_ok = false;
        g_gts.link       = GTS_LINK_IDLE;
        break;
    }

    case GTS_PAGE_GFC: {
        /* Manual 모드에서는 jog 무시 (사양서 5.2) */
        if (g_gts.gfc_mode != GTS_GFC_AUTO) break;
        uint16_t *t = (g_gts.gfc_sel == GTS_GFC_SEL_START)
                    ? &g_gts.gfc_start_ds : &g_gts.gfc_cycle_ds;
        *t = clamp_ds((int)*t + dir);   /* step 은 0.1 sec 고정 */
        break;
    }

    case GTS_PAGE_AOS_MEAS: {
        /* 대상 = DATA TYPE, 4종 순환 */
        if (g_gts.aos_running) break;   /* 측정 중에는 타입 변경 금지 */
        int t = (int)g_gts.aos_type + dir;
        t = ((t % GTS_AOS_TYPE_COUNT) + GTS_AOS_TYPE_COUNT) % GTS_AOS_TYPE_COUNT;
        g_gts.aos_type   = (gts_aos_type_t)t;
        g_gts.aos_total_s = gts_aos_type_seconds(g_gts.aos_type);
        break;
    }

    case GTS_PAGE_WIFI: {
        /* 대상 = NETWORK, 등록 지점 순환 (사양서 5.5) */
        int n = (int)g_gts.wifi_sel + dir;
        n = ((n % GTS_WIFI_NET_COUNT) + GTS_WIFI_NET_COUNT) % GTS_WIFI_NET_COUNT;
        g_gts.wifi_sel = (uint8_t)n;
        break;
    }

    case GTS_PAGE_AOS_MANUAL: {
        gts_param_id_t id = g_gts.param_sel;
        /* LF_Mode Off 면 LF 항목은 잠근다 (사양서 5.4) */
        if (!g_gts.lf_on && (id == GTS_P_LF_FRQ || id == GTS_P_LF_VOLT)) break;

        const gts_param_spec_t *s = &GTS_PARAM_SPEC[id];
        gts_param_t *p = &g_gts.param[id];
        float step = s->steps[p->step_idx];
        p->value = gts_param_clamp(id, p->value + (float)dir * step);
        break;
    }

    default: break;
    }

    s_dirty = true;
    gts_state_unlock();
}

void gts_state_jog_sw(void)
{
    gts_state_lock();

    /* step 순환은 P4 에만 의미가 있다 (사양서 2.2: STEP 배지는 P4 전용) */
    if (g_gts.page == GTS_PAGE_AOS_MANUAL) {
        gts_param_id_t id = g_gts.param_sel;
        const gts_param_spec_t *s = &GTS_PARAM_SPEC[id];
        gts_param_t *p = &g_gts.param[id];
        if (s->step_count > 1) {
            p->step_idx = (uint8_t)((p->step_idx + 1) % s->step_count);
        }
    }

    s_dirty = true;
    gts_state_unlock();
}

/* ── 조그바 표시 문자열 ──────────────────────────────────────────── */
static void fmt_step(char *out, size_t cap, float step, uint8_t decimals)
{
    snprintf(out, cap, "%.*f", decimals, step);
}

void gts_state_jog_target_text(char *name, size_t name_cap,
                               char *value, size_t value_cap,
                               char *range, size_t range_cap,
                               char *step, size_t step_cap,
                               bool *has_step, bool *enabled)
{
    name[0] = value[0] = range[0] = step[0] = '\0';
    *has_step = false;
    *enabled  = true;

    gts_state_lock();

    switch (g_gts.page) {

    case GTS_PAGE_DEVICE:
        snprintf(name,  name_cap,  "DEVICE ID");
        snprintf(value, value_cap, "%02u", g_gts.dev_id);
        snprintf(range, range_cap, "(1 - 20)");
        break;

    case GTS_PAGE_GFC: {
        if (g_gts.gfc_mode != GTS_GFC_AUTO) {
            snprintf(name,  name_cap,  "-");
            snprintf(value, value_cap, "-");
            *enabled = false;
            break;
        }
        bool st = (g_gts.gfc_sel == GTS_GFC_SEL_START);
        uint16_t ds = st ? g_gts.gfc_start_ds : g_gts.gfc_cycle_ds;
        snprintf(name,  name_cap,  st ? "START TIME" : "CYCLE TIME");
        snprintf(value, value_cap, "%u.%u", ds / 10u, ds % 10u);
        snprintf(range, range_cap, "sec (0.1 - 60)");
        break;
    }

    case GTS_PAGE_AOS_MEAS:
        snprintf(name,  name_cap,  "DATA TYPE");
        snprintf(value, value_cap, "%s", gts_aos_type_name(g_gts.aos_type));
        snprintf(range, range_cap, "(4 types)");
        *enabled = !g_gts.aos_running;
        break;

    case GTS_PAGE_WIFI:
        snprintf(name,  name_cap,  "NETWORK");
        snprintf(value, value_cap, "%s", GTS_WIFI_NET[g_gts.wifi_sel].ssid);
        snprintf(range, range_cap, "%u / %u",
                 (unsigned)(g_gts.wifi_sel + 1), (unsigned)GTS_WIFI_NET_COUNT);
        break;

    case GTS_PAGE_AOS_MANUAL: {
        gts_param_id_t id = g_gts.param_sel;
        const gts_param_spec_t *s = &GTS_PARAM_SPEC[id];
        const gts_param_t *p = &g_gts.param[id];

        snprintf(name,  name_cap,  "%s", s->name);
        snprintf(value, value_cap, "%.*f", s->decimals, p->value);
        snprintf(range, range_cap, "%s (%.*f - %.*f)",
                 s->unit, s->decimals, s->min, s->decimals, s->max);

        char sv[16];
        fmt_step(sv, sizeof(sv), s->steps[p->step_idx], s->decimals);
        snprintf(step, step_cap, "%s", sv);
        *has_step = true;

        if (!g_gts.lf_on && (id == GTS_P_LF_FRQ || id == GTS_P_LF_VOLT))
            *enabled = false;
        break;
    }

    default: break;
    }

    gts_state_unlock();
}
