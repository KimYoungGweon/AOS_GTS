/*
 * aos_ctrl.c — 파라미터 섀도 + 합치기 + 주기 조회 + LED
 *
 * 100 ms 틱 하나가 세 가지를 한다.
 *   ① 합치기 — 콘솔 jog 로 SET_PARAM 이 연달아 오면 AOS_COALESCE_MS 동안
 *      모았다가 바뀐 그룹만 한 번 내보낸다. 매 변경마다 UART 프레임을
 *      쏘면 38400 bps 가 금방 막힌다.
 *   ② 주기 조회 — 0x02 CMD_SET_QUERY. 0x2A 는 응답이 없으므로 이것이
 *      UART 생존 신호를 겸한다.
 *   ②' 적용 확인 (2026-09-23) — 보낸 뒤 0x02 로 되읽어 섀도와 다르면 재전송.
 *      한가할 때는 섀도를 STM32 실제값으로 맞춘다 (아래 "섀도 동기화").
 *
 * ★ 2026-09-23 수정 — "적용될 때도 있고 안 될 때도 있다" 의 원인
 *   ① 섀도가 부팅 기본값(HV 50, FRQ 500 …)에 머물러 STM32 실제값과 달랐다.
 *      요청값이 섀도와 같으면 "안 바뀜" 으로 보고 UART 를 보내지 않았다.
 *      또 FRQ 만 바꿔도 0x2A 가 섀도의 HV·DUTY·CV 까지 실어 보내 실제값을 되돌렸다.
 *   ② 0x2A/0x52 는 응답이 없어 UART 프레임이 깨지면 그대로 유실됐다.
 *   → 섀도를 실제값에 동기화 + 명령은 값이 같아도 항상 전송 + 되읽어 확인·재전송
 *   ③ LED 16비트 패턴 시프트.
 */
#include "aos_ctrl.h"
#include "aos_config.h"
#include "aos_uart.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "AOS-CTRL";

/* ── LED 패턴 (GFC 와 동일 규격) ─────────────────────────────────── */
#define LED_OFF     0x0000
#define LED_ON      0xFFFF
#define LED_SLOW    0xFF00
#define LED_FAST    0xCCCC
#define LED_BLINK2  0x0005

/* 바뀐 그룹 */
#define DIRTY_CTL   (1u << 0)   /* HV / FRQ / DUTY / CV */
#define DIRTY_LF    (1u << 1)   /* LF on / shape / frq / volt */

static SemaphoreHandle_t s_lock;
static aos_ctrl_state_t  s_st;
static aos_link_t        s_link;
static uint8_t           s_dirty;
static int32_t           s_coalesce_ms;   /* 남은 합치기 시간, <0 이면 대기 없음 */
static bool              s_applied_flag;  /* 서버에 즉시 알릴 것 */

/* 적용 확인 */
static struct {
    bool     active;
    uint8_t  groups;        /* 확인할 그룹 (DIRTY_*) */
    int64_t  sent_ms;
    uint32_t set_count;     /* 보낼 때의 0x02 응답 수 — 이보다 큰 응답만 비교 */
    uint8_t  retries;
    aos_ctrl_state_t want;  /* 보낸 값 */
} s_vfy;
static bool     s_user_cmd;     /* 서버/콘솔 명령으로 dirty 가 된 것 (재시도 카운터 리셋용) */
static uint32_t s_synced_count; /* 섀도 동기화에 쓴 마지막 0x02 응답 번호 */

/* 이벤트 링버퍼 */
#define EVQ_N 8
static struct { uint16_t code; uint32_t a; } s_evq[EVQ_N];
static uint8_t s_ev_r, s_ev_w;

static void ev_push(uint16_t code, uint32_t a)
{
    uint8_t nw = (uint8_t)((s_ev_w + 1) % EVQ_N);
    if (nw == s_ev_r) return;
    s_evq[s_ev_w].code = code;
    s_evq[s_ev_w].a    = a;
    s_ev_w = nw;
}

bool aos_ctrl_take_event(uint16_t *code, uint32_t *a)
{
    bool got = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_ev_r != s_ev_w) {
        if (code) *code = s_evq[s_ev_r].code;
        if (a)    *a    = s_evq[s_ev_r].a;
        s_ev_r = (uint8_t)((s_ev_r + 1) % EVQ_N);
        got = true;
    }
    xSemaphoreGive(s_lock);
    return got;
}

bool aos_ctrl_take_applied(void)
{
    bool v;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    v = s_applied_flag;
    s_applied_flag = false;
    xSemaphoreGive(s_lock);
    return v;
}

/* ── LF 파형 번호 변환 ───────────────────────────────────────────────
 * 콘솔과 STM32 의 번호가 다르다 (aos_config.h 의 표 참조).
 * 그대로 넘기면 SQUARE 를 골랐는데 TRIANGLE 이 나온다.               */

uint8_t aos_shape_to_stm32(uint8_t shape)
{
#if AOS_LF_SHAPE_FIXED
    (void)shape;
    return AOS_LF_TYPE_FIXED;           /* 항상 6 eSquare */
#else
    switch (shape) {
    case 0:  return AOS_LF_TYPE_SQUARE;
    case 1:  return AOS_LF_TYPE_SINE;
    case 2:  return AOS_LF_TYPE_TRIANGLE;
    case 3:  return AOS_LF_TYPE_TRAPEZOID;
    default: return AOS_LF_TYPE_SQUARE;
    }
#endif
}

uint8_t aos_shape_from_stm32(uint8_t type)
{
#if AOS_LF_SHAPE_FIXED
    (void)type;
    return 0;                           /* 콘솔 SQUARE */
#else
    switch (type) {
    case AOS_LF_TYPE_SQUARE:    return 0;
    case AOS_LF_TYPE_SINE:      return 1;
    case AOS_LF_TYPE_TRIANGLE:  return 2;
    case 7: case 8: case 9: case 10: return 3;  /* eTPZ* 전부 TRAPEZOID */
    default:                    return 0;       /* eRamp 계열은 콘솔에 없다 */
    }
#endif
}

/* ── 범위 ────────────────────────────────────────────────────────── */

static float clampf(float v, float lo, float hi)
{
    if (!(v == v)) return lo;               /* NaN 방어 */
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* STM32 는 받은 float 를 memcpy 로 그대로 저장한다 — 사실상 같아야 한다 */
static bool feq(float a, float b)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d <= 1e-3f;
}

static const struct { float lo, hi; } RANGE[AOS_P_COUNT] = {
    [AOS_P_HV]      = { AOS_HV_MIN,   AOS_HV_MAX   },
    [AOS_P_FRQ]     = { AOS_FRQ_MIN,  AOS_FRQ_MAX  },
    [AOS_P_DUTY]    = { AOS_DUTY_MIN, AOS_DUTY_MAX },
    [AOS_P_CV]      = { AOS_CV_MIN,   AOS_CV_MAX   },
    [AOS_P_LF_FRQ]  = { AOS_LFF_MIN,  AOS_LFF_MAX  },
    [AOS_P_LF_VOLT] = { AOS_LFV_MIN,  AOS_LFV_MAX  },
};

/* 락을 쥔 상태에서 호출 */
static void mark_dirty(uint8_t bits)
{
    s_dirty |= bits;
    s_coalesce_ms = AOS_COALESCE_MS;    /* 변경이 이어지면 계속 미뤄진다 */
    s_user_cmd = true;
}

/* ── 공개 API ────────────────────────────────────────────────────── */

bool aos_ctrl_set_param(uint8_t param_id, float value)
{
    if (param_id >= AOS_P_COUNT) return false;

    float v = clampf(value, RANGE[param_id].lo, RANGE[param_id].hi);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    switch (param_id) {
    /* 값이 섀도와 같아도 항상 보낸다 — 섀도가 실제와 어긋났을 수 있다 (GFC E10 과 같은 원칙) */
    case AOS_P_HV:      s_st.hv      = v; mark_dirty(DIRTY_CTL); break;
    case AOS_P_FRQ:     s_st.frq     = v; mark_dirty(DIRTY_CTL); break;
    case AOS_P_DUTY:    s_st.duty    = v; mark_dirty(DIRTY_CTL); break;
    case AOS_P_CV:      s_st.cv      = v; mark_dirty(DIRTY_CTL); break;
    case AOS_P_LF_FRQ:  s_st.lf_frq  = v; mark_dirty(DIRTY_LF);  break;
    case AOS_P_LF_VOLT: s_st.lf_volt = v; mark_dirty(DIRTY_LF);  break;
    default: break;
    }
    xSemaphoreGive(s_lock);

    if (v != value)
        ESP_LOGW(TAG, "param %u clamp %.4f -> %.4f", param_id,
                 (double)value, (double)v);
    return true;
}

void aos_ctrl_set_lf_mode(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.lf_on = on ? 1 : 0;
    mark_dirty(DIRTY_LF);
    xSemaphoreGive(s_lock);
}

void aos_ctrl_set_lf_shape(uint8_t shape)
{
#if AOS_LF_SHAPE_FIXED
    /* STM32 파형은 6 eSquare 로 고정이다. 콘솔이 무엇을 고르든 바뀌지 않으므로
     * UART 로 다시 쏘지 않고, 섀도도 SQUARE 로 둔다 — 화면이 실제와 맞아야 한다. */
    (void)shape;
    ESP_LOGW(TAG, "LF shape 는 eSquare 로 고정 — 요청(%u) 무시", shape);
#else
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_st.lf_shape != shape) { s_st.lf_shape = shape; mark_dirty(DIRTY_LF); }
    xSemaphoreGive(s_lock);
#endif
}

void aos_ctrl_get(aos_ctrl_state_t *out)
{
    if (!out) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_lock);
}

void aos_ctrl_fill_params(aos_params_t *out)
{
    if (!out) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->hv       = s_st.hv;
    out->frq      = s_st.frq;
    out->duty     = s_st.duty;
    out->cv       = s_st.cv;
    out->lf_frq   = s_st.lf_frq;
    out->lf_volt  = s_st.lf_volt;
    out->lf_on    = s_st.lf_on;
#if AOS_LF_SHAPE_FIXED
    out->lf_shape = 0;                  /* 실제 장비는 항상 SQUARE */
#else
    out->lf_shape = s_st.lf_shape;
#endif
    xSemaphoreGive(s_lock);
}

void aos_ctrl_set_link(aos_link_t link) { s_link = link; }

/* ── 100 ms 틱 ───────────────────────────────────────────────────── */

static void aos_ctrl_task(void *arg)
{
    (void)arg;
    uint16_t red_pat = LED_OFF, grn_pat = LED_OFF;
    uint8_t  bit = 0;
    int32_t  query_ms = 0;
    int32_t  health_ms = 0;
    bool     query_pending = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(AOS_LED_TICK_MS));

        /* ── ① 합치기 → STM32 송신 ─────────────────────────────── */
        uint8_t  send = 0;
        float    hv = 0, frq = 0, duty = 0, cv = 0, lfv = 0, lff = 0;
        uint8_t  lf_on = 0, lf_shape = 0;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_dirty) {
            s_coalesce_ms -= AOS_LED_TICK_MS;
            if (s_coalesce_ms <= 0) {
                send  = s_dirty;
                s_dirty = 0;
                hv = s_st.hv; frq = s_st.frq; duty = s_st.duty; cv = s_st.cv;
                lfv = s_st.lf_volt; lff = s_st.lf_frq;
                lf_on = s_st.lf_on; lf_shape = s_st.lf_shape;
                s_st.applied = true;
                s_st.apply_count++;
                /* 서버 알림(send_params)은 확인이 끝난 뒤에 — 지금 올리면 옛 실제값이 간다 */
                if (s_user_cmd) { s_vfy.retries = 0; s_user_cmd = false; }
                s_vfy.groups  = (uint8_t)((s_vfy.active ? s_vfy.groups : 0) | send);
                s_vfy.want    = s_st;
                s_vfy.active  = true;
            }
        }
        xSemaphoreGive(s_lock);

        if (send & DIRTY_CTL) {
            aos_uart_set_control(hv, frq, duty, cv);
            ev_push(AOS_EV_PARAM_APPLY, 0x2A);
        }
        if (send & DIRTY_LF) {
            /* LF Off 여도 amp/frq 는 사용자가 맞춰 둔 값 그대로 보낸다.
             * STM32 의 LF_MOD.OnOff 가 실제 출력을 끊는다 —
             * 다시 On 하면 값이 그대로 살아난다.                        */
            aos_uart_lf_mod_set(aos_shape_to_stm32(lf_shape), lf_on, lfv, lff);
            ev_push(AOS_EV_PARAM_APPLY, 0x52);
        }
        if (send) {
            aos_uart_state_t u0;
            aos_uart_get(&u0);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_vfy.sent_ms   = esp_timer_get_time() / 1000;
            s_vfy.set_count = u0.set_count;
            xSemaphoreGive(s_lock);
            /* VERIFY_DELAY 뒤에 0x02 가 나가도록 */
            query_ms = AOS_QUERY_PERIOD_MS - AOS_VERIFY_DELAY_MS;
        }

        /* ── ②' 적용 확인 / 섀도 동기화 ─────────────────────────── */
        {
            aos_uart_state_t u1;
            aos_uart_get(&u1);
            int64_t now = esp_timer_get_time() / 1000;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (s_vfy.active && !s_dirty && u1.have_set && u1.link_ok
                && u1.set_count > s_vfy.set_count
                && now - s_vfy.sent_ms >= AOS_VERIFY_DELAY_MS) {
                const aos_ctrl_state_t *w = &s_vfy.want;
                bool bad_ctl = (s_vfy.groups & DIRTY_CTL) &&
                    (!feq(u1.hv, w->hv) || !feq(u1.frq, w->frq) ||
                     !feq(u1.duty, w->duty) || !feq(u1.cv, w->cv));
                bool bad_lf = (s_vfy.groups & DIRTY_LF) &&
                    (u1.lf_on != w->lf_on || !feq(u1.lf_amp, w->lf_volt) ||
                     !feq(u1.lf_frq, w->lf_frq));
                if (!bad_ctl && !bad_lf) {
                    if (s_vfy.retries)
                        ESP_LOGI(TAG, "적용 확인 OK (재전송 %u회)", (unsigned)s_vfy.retries);
                    s_vfy.active = false;
                    s_applied_flag = true;          /* 이제 실제값이 맞다 → 서버에 즉시 알림 */
                } else if (s_vfy.retries < AOS_APPLY_RETRY) {
                    s_vfy.retries++;
                    ESP_LOGW(TAG, "적용 불일치 (%s%s) — 재전송 %u/%u",
                             bad_ctl ? "0x2A " : "", bad_lf ? "0x52" : "",
                             (unsigned)s_vfy.retries, (unsigned)AOS_APPLY_RETRY);
                    s_dirty |= (uint8_t)((bad_ctl ? DIRTY_CTL : 0) | (bad_lf ? DIRTY_LF : 0));
                    s_coalesce_ms = 0;              /* 다음 틱에 바로 */
                } else {
                    ESP_LOGE(TAG, "적용 실패 — %u회 재전송 후에도 STM32 값 불일치", (unsigned)AOS_APPLY_RETRY);
                    ev_push(AOS_EV_APPLY_FAIL, bad_ctl ? 0x2A : 0x52);
                    s_vfy.active = false;
                    s_applied_flag = true;          /* 실제값을 그대로 보여 준다 */
                }
            }
            /* STM32 가 응답하지 않으면(링크 끊김) 확인을 오래 붙잡지 않는다 */
            if (s_vfy.active && !s_dirty && now - s_vfy.sent_ms > 3000) {
                ESP_LOGW(TAG, "적용 확인 시간 초과 — 0x02 응답 없음");
                s_vfy.active = false;
                s_applied_flag = true;
            }
            /* 섀도 동기화 — 보낼 것도, 확인할 것도 없으면 섀도 = STM32 실제값.
             * 전면 jog·재부팅 등으로 실제가 바뀌어도 다음 명령이 옛 값을 싣지 않는다. */
            if (!s_vfy.active && !s_dirty && u1.have_set && u1.link_ok
                && u1.set_count != s_synced_count) {
                s_synced_count = u1.set_count;
                s_st.hv      = u1.hv;
                s_st.frq     = u1.frq;
                s_st.duty    = u1.duty;
                s_st.cv      = u1.cv;
                s_st.lf_on   = u1.lf_on;
                s_st.lf_volt = u1.lf_amp;
                s_st.lf_frq  = u1.lf_frq;
            }
            xSemaphoreGive(s_lock);
        }

        /* ── ② 주기 조회 (0x02) ────────────────────────────────── */
        query_ms += AOS_LED_TICK_MS;
        if (query_ms >= AOS_QUERY_PERIOD_MS) {
            query_ms = 0;
            aos_uart_state_t u;
            aos_uart_get(&u);
            if (query_pending && u.age_ms > (uint32_t)AOS_QUERY_PERIOD_MS)
                aos_uart_note_timeout();
            aos_uart_set_query();
            query_pending = true;
        }

#if AOS_UART_HEALTH_MS
        health_ms += AOS_LED_TICK_MS;
        if (health_ms >= AOS_UART_HEALTH_MS) {
            health_ms = 0;
            aos_uart_log_health();
        }
#endif

        /* ── ③ LED ─────────────────────────────────────────────── */
        aos_uart_state_t u;
        aos_ctrl_state_t c;
        aos_uart_get(&u);
        aos_ctrl_get(&c);

        if (!u.link_ok)      red_pat = LED_BLINK2;  /* STM32 무응답 (최우선) */
        else if (c.lf_on)    red_pat = LED_ON;      /* LF 변조 On            */
        else if (c.applied)  red_pat = LED_SLOW;    /* 설정은 나갔음         */
        else                 red_pat = LED_OFF;     /* 아직 아무것도 안 보냄 */

        switch (s_link) {
        case AOS_LINK_OK:        grn_pat = LED_ON;    break;
        case AOS_LINK_NO_SERVER: grn_pat = LED_SLOW;  break;
        default:                 grn_pat = LED_OFF;   break;
        }

        gpio_set_level(AOS_PIN_LED_RED,   (red_pat >> bit) & 1u);
        gpio_set_level(AOS_PIN_LED_GREEN, (grn_pat >> bit) & 1u);
        bit = (uint8_t)((bit + 1) % 16);
    }
}

bool aos_ctrl_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_st, 0, sizeof(s_st));
    s_st.hv      = AOS_DEF_HV;
    s_st.frq     = AOS_DEF_FRQ;
    s_st.duty    = AOS_DEF_DUTY;
    s_st.cv      = AOS_DEF_CV;
    s_st.lf_frq   = AOS_DEF_LFF;
    s_st.lf_volt  = AOS_DEF_LFV;
    s_st.lf_on    = 0;
    s_st.lf_shape = AOS_DEF_LF_SHAPE;
    s_dirty      = 0;            /* 부팅만으로 STM32 를 건드리지 않는다 */

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << AOS_PIN_LED_RED) | (1ULL << AOS_PIN_LED_GREEN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(AOS_PIN_LED_RED, 0);
    gpio_set_level(AOS_PIN_LED_GREEN, 0);

    xTaskCreate(aos_ctrl_task, "aos_ctrl", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "ctrl ready (RED=%d GREEN=%d)",
             AOS_PIN_LED_RED, AOS_PIN_LED_GREEN);
    return true;
}
