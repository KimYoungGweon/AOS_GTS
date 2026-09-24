/*
 * gfc_ctrl.c — 주입 시퀀스 + LED
 *
 * 시퀀스 (DOC 6.4)
 *   enable=1 → Pump1 On 고정
 *            → init_on_sec 동안 Pump2/3 연속 On
 *            → 이후 period_sec 마다 cycle_on_sec 동안 On 반복
 *
 * 시간 계산은 "경과시간 t 로부터 지금 켜져 있어야 하는가"를 매 tick 다시
 * 계산하는 방식이다. 타이머 드리프트가 누적되지 않고, 설정이 도중에
 * 바뀌어도 다음 tick 부터 바로 새 값이 반영된다.
 *
 *   t < init_on_sec                    → INIT 구간, On
 *   u = t - init_on_sec,  n = u/period, f = u - n*period
 *   n >= 1 && f < cycle_on_sec         → 주기 분사, On
 *   그 외                              → Off, 다음 분사까지 (n+1)*period - u
 *
 * n >= 1 조건은 init 이 끝나자마자 곧바로 한 번 더 분사되는 것을 막는다.
 */
#include "gfc_ctrl.h"
#include "gfc_config.h"
#include "gfc_uart.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "GFC-CTRL";

/* ── LED 패턴 (DOC 7.1) — 100ms 마다 1비트씩 시프트, 1회전 1.6초 ─── */
#define LED_OFF     0x0000
#define LED_ON      0xFFFF
#define LED_SLOW    0xFF00      /* 0.8s 주기 */
#define LED_FAST    0xCCCC      /* 0.2s 주기 */
#define LED_BLINK2  0x0005

static SemaphoreHandle_t s_lock;
static gfc_ctrl_state_t  s_st;
static gfc_link_t        s_link;
static int64_t           s_t0_us;        /* enable 시각 */

/* 이벤트 링버퍼 */
#define EVQ_N 8
static struct { uint16_t code; uint32_t a; } s_evq[EVQ_N];
static uint8_t s_ev_r, s_ev_w;

static void ev_push(uint16_t code, uint32_t a)
{
    uint8_t nw = (uint8_t)((s_ev_w + 1) % EVQ_N);
    if (nw == s_ev_r) return;               /* 가득 차면 버린다 */
    s_evq[s_ev_w].code = code;
    s_evq[s_ev_w].a    = a;
    s_ev_w = nw;
}

bool gfc_ctrl_take_event(uint16_t *code, uint32_t *a)
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

/* ── 펌프 반영 ───────────────────────────────────────────────────── */

/**
 * 락을 쥔 상태에서 호출.
 * @param force true 면 값이 같아도 UART 로 다시 내보낸다.
 *
 * 100 ms 시퀀스 틱은 force=false 로 부른다 — 매 틱 같은 프레임을 쏘면
 * UART 가 의미 없이 가득 찬다.
 * 반대로 서버가 보낸 명시적 PUMP_SET 은 force=true 다. 값이 이미 같아
 * 보여도 STM32 가 앞 프레임을 놓쳤을 수 있고, 같은 값을 다시 쓰는 건
 * 무해하다. 이걸 막아 두면 "두 번째부터 안 먹는" 증상이 된다.
 */
static void apply_pumps_ex(uint8_t p1, uint8_t p2, uint8_t p3, bool force)
{
    bool same = (s_st.pump1 == p1 && s_st.pump2 == p2 && s_st.pump3 == p3);
    s_st.pump1 = p1; s_st.pump2 = p2; s_st.pump3 = p3;
    if (same && !force) return;
    gfc_uart_pump_set(p1, p2, p3);
}

static inline void apply_pumps(uint8_t p1, uint8_t p2, uint8_t p3)
{
    apply_pumps_ex(p1, p2, p3, false);
}

/* ── 공개 API ────────────────────────────────────────────────────── */

void gfc_ctrl_src_set(const gfc_src_set_t *cfg)
{
    if (!cfg) return;

    /* 범위 방어 — 0 이하나 말도 안 되는 값이 들어오면 기본값으로 */
    gfc_src_set_t c = *cfg;
    if (!(c.init_on_sec  >= 0.0f   && c.init_on_sec  <= 3600.0f)) c.init_on_sec  = GFC_SRC_INIT_ON_SEC;
    if (!(c.cycle_on_sec >= 0.0f   && c.cycle_on_sec <= 3600.0f)) c.cycle_on_sec = GFC_SRC_CYCLE_ON_SEC;
    if (!(c.period_sec   >= 1.0f   && c.period_sec   <= 86400.0f)) c.period_sec  = GFC_SRC_PERIOD_SEC;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    bool was = s_st.enable;
    s_st.cfg = c;

    if (c.enable) {
        if (!was) {                     /* 새로 시작 — 시계 리셋 */
            s_t0_us          = esp_timer_get_time();
            s_st.cycle_count = 0;
            s_st.on          = false;
        }
        s_st.enable = true;
        ESP_LOGI(TAG, "SRC start init=%.1fs cycle=%.1fs period=%.1fs",
                 (double)c.init_on_sec, (double)c.cycle_on_sec, (double)c.period_sec);
    } else {
        s_st.enable    = false;
        s_st.in_init   = false;
        s_st.remain_s  = 0.0f;
        s_st.elapsed_s = 0.0f;
        if (s_st.on) { s_st.on = false; ev_push(GFC_EV_SRC_OFF, 0); }
        apply_pumps_ex(0, 0, 0, true);  /* Stop = Pump 전부 Off (반드시 송신) */
        ESP_LOGI(TAG, "SRC stop");
    }

    xSemaphoreGive(s_lock);
}

void gfc_ctrl_pump_set(uint8_t p1, uint8_t p2, uint8_t p3)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_st.enable) {                  /* 수동 조작이 시퀀스를 이긴다 */
        s_st.enable      = false;
        s_st.cfg.enable  = 0;
        s_st.in_init     = false;
        s_st.remain_s    = 0.0f;
        if (s_st.on) { s_st.on = false; ev_push(GFC_EV_SRC_OFF, 0); }
        ESP_LOGI(TAG, "manual pump -> SRC sequence stopped");
    }
    apply_pumps_ex(p1 ? 1 : 0, p2 ? 1 : 0, p3 ? 1 : 0, true);

    xSemaphoreGive(s_lock);
}

void gfc_ctrl_get(gfc_ctrl_state_t *out)
{
    if (!out) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_lock);
}

void gfc_ctrl_set_link(gfc_link_t link) { s_link = link; }

/* ── 100ms 틱 — 시퀀스 + LED ─────────────────────────────────────── */

static void gfc_ctrl_task(void *arg)
{
    (void)arg;
    uint16_t red_pat = LED_OFF, grn_pat = LED_OFF;
    uint8_t  bit = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(GFC_CTRL_TICK_MS));

        /* ── 시퀀스 ─────────────────────────────────────────────── */
        xSemaphoreTake(s_lock, portMAX_DELAY);

        if (s_st.enable) {
            float t  = (float)((esp_timer_get_time() - s_t0_us) / 1000) / 1000.0f;
            float ii = s_st.cfg.init_on_sec;
            float cy = s_st.cfg.cycle_on_sec;
            float pd = s_st.cfg.period_sec;

            bool  on;
            float remain;

            if (t < ii) {                       /* 초기 주입 구간 */
                s_st.in_init = true;
                on     = true;
                remain = 0.0f;
            } else {
                s_st.in_init = false;
                float u = t - ii;
                uint32_t n = (uint32_t)(u / pd);
                float    f = u - (float)n * pd;

                on = (n >= 1u) && (f < cy);
                remain = on ? 0.0f : ((float)(n + 1u) * pd - u);

                if (on && n != s_st.cycle_count) s_st.cycle_count = n;
            }

            s_st.elapsed_s = t;
            s_st.remain_s  = remain;

            if (on != s_st.on) {
                s_st.on = on;
                ev_push(on ? GFC_EV_SRC_ON : GFC_EV_SRC_OFF,
                        (uint32_t)(t * 1000.0f));
            }
            /* Pump1 은 시퀀스가 도는 동안 항상 On (DOC 2.2-6) */
            apply_pumps(1, on ? 1 : 0, on ? 1 : 0);
        }

        bool     ena   = s_st.enable;
        bool     onnow = s_st.on;
        uint8_t  p2    = s_st.pump2;

        xSemaphoreGive(s_lock);

        /* ── LED 패턴 선택 ──────────────────────────────────────── */
        gfc_uart_state_t u;
        gfc_uart_get(&u);

        if (!u.link_ok)          red_pat = LED_BLINK2;   /* 에러 최우선 */
        else if (onnow)          red_pat = LED_ON;       /* 분사 중     */
        else if (ena)            red_pat = LED_SLOW;     /* 주입 대기   */
        else if (p2)             red_pat = LED_FAST;     /* 수동 Pump   */
        else                     red_pat = LED_OFF;

        switch (s_link) {
        case GFC_LINK_OK:        grn_pat = LED_ON;    break;
        case GFC_LINK_NO_SERVER: grn_pat = LED_SLOW;  break;
        default:                 grn_pat = LED_OFF;   break;
        }

        gpio_set_level(GFC_PIN_LED_RED,   (red_pat >> bit) & 1u);
        gpio_set_level(GFC_PIN_LED_GREEN, (grn_pat >> bit) & 1u);
        bit = (uint8_t)((bit + 1) % 16);
    }
}

bool gfc_ctrl_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_st, 0, sizeof(s_st));
    s_st.cfg.init_on_sec  = GFC_SRC_INIT_ON_SEC;
    s_st.cfg.cycle_on_sec = GFC_SRC_CYCLE_ON_SEC;
    s_st.cfg.period_sec   = GFC_SRC_PERIOD_SEC;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << GFC_PIN_LED_RED) | (1ULL << GFC_PIN_LED_GREEN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(GFC_PIN_LED_RED, 0);
    gpio_set_level(GFC_PIN_LED_GREEN, 0);

    xTaskCreate(gfc_ctrl_task, "gfc_ctrl", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "ctrl ready (RED=%d GREEN=%d)",
             GFC_PIN_LED_RED, GFC_PIN_LED_GREEN);
    return true;
}
