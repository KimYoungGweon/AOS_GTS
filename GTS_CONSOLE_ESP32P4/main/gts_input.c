/*
 * gts_input.c — Encoder(Jog) / Jog S/W
 */
#include "gts_input.h"
#include "gts_config.h"
#include "gts_state.h"
#include "gts_protocol.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "GTS-ENC";

static uint8_t       s_phase;          /* 직전 A/B 조합 0~3 */
static volatile int  s_delta;          /* 아직 반영 안 한 detent 수 */
static volatile bool s_sw_pressed;

static int64_t s_last_change_us;       /* 마지막 값 변경 시각 */
static bool    s_tx_pending;           /* 디바운스 대기 중 */

/* ── GPIO ────────────────────────────────────────────────────────── */
static void encoder_gpio_init(void)
{
    gpio_config_t ab = {
        .pin_bit_mask = (1ULL << GTS_PIN_ENC_A) | (1ULL << GTS_PIN_ENC_B),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&ab));

    gpio_config_t sw = {
        .pin_bit_mask = (1ULL << GTS_PIN_ENC_SW),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&sw));

    s_phase = (uint8_t)(gpio_get_level(GTS_PIN_ENC_B)
                      + gpio_get_level(GTS_PIN_ENC_A) * 2);
}

/* 데모 코드의 JOG_READ() 와 동일한 상태 전이 판정 */
static void jog_read(void)
{
    uint8_t b2 = (uint8_t)gpio_get_level(GTS_PIN_ENC_A);
    uint8_t b1 = (uint8_t)gpio_get_level(GTS_PIN_ENC_B);
    uint8_t cur = (uint8_t)(b1 + b2 * 2);

    if (cur == s_phase) return;

    if (cur == 1 || cur == 2) {
        if (cur == 1) {
            if (s_phase == 0) s_delta--;
            if (s_phase == 3) s_delta++;
        } else {
            if (s_phase == 3) s_delta--;
            if (s_phase == 0) s_delta++;
        }
    }
    s_phase = cur;
}

/* ── 값 변경 송신 ────────────────────────────────────────────────── */

/** 현재 화면의 선택 값 1개를 서버로 보낸다. */
static void send_current_value(void)
{
    gts_page_t page;
    gts_state_lock();
    page = g_gts.page;
    gts_state_unlock();

    switch (page) {

    case GTS_PAGE_DEVICE: {
        /* 사양서 5.1: 장치를 고르면 바로 테스트 커넥션을 건다.
         * Jog 를 연속으로 돌리는 동안에는 디바운스가 눌러 두므로,
         * 손을 뗀 뒤 최종 ID 로 한 번만 나간다.                     */
        uint8_t t, id;
        gts_state_lock();
        t  = (uint8_t)g_gts.dev_type;
        id = g_gts.dev_id;
        gts_state_unlock();
        gts_proto_connect(t, id);
        break;
    }

    case GTS_PAGE_GFC: {
        uint16_t st, cy;
        gts_state_lock();
        st = g_gts.gfc_start_ds;
        cy = g_gts.gfc_cycle_ds;
        gts_state_unlock();
        gts_proto_gfc_set_times(st, cy);
        break;
    }

    case GTS_PAGE_AOS_MEAS: {
        uint8_t t;
        gts_state_lock();
        t = (uint8_t)g_gts.aos_type;
        gts_state_unlock();
        gts_proto_aos_set_type(t);
        break;
    }

    case GTS_PAGE_AOS_MANUAL: {
        uint8_t id;
        float   v;
        gts_state_lock();
        id = (uint8_t)g_gts.param_sel;
        v  = g_gts.param[g_gts.param_sel].value;
        gts_state_unlock();
        gts_proto_aos_set_param(id, v);
        break;
    }

    default: break;
    }
}

void gts_input_flush_now(void)
{
    s_tx_pending = false;
    send_current_value();
}

/* ── 폴링 태스크 ─────────────────────────────────────────────────── */
static void encoder_task(void *arg)
{
    (void)arg;

    int last_sw   = gpio_get_level(GTS_PIN_ENC_SW);
    int sw_stable = 0;

    while (1) {
        /* 1 ms 폴링 — 4상 디코딩에 충분하다 (데모에서 검증) */
        jog_read();

        if (s_delta != 0) {
            int d = s_delta;
            s_delta = 0;

            /* detent 1개씩 적용해야 clamp 와 순환이 정확하다 */
            int step = (d > 0) ? 1 : -1;
            for (int i = 0; i < (d > 0 ? d : -d); i++) {
                gts_state_jog(step);
            }

            s_last_change_us = esp_timer_get_time();
            s_tx_pending     = true;
        }

        /* 디바운스 만료 → 최종값 1회 송신 */
        if (s_tx_pending) {
            int64_t age = esp_timer_get_time() - s_last_change_us;
            if (age >= (int64_t)GTS_JOG_DEBOUNCE_MS * 1000) {
                s_tx_pending = false;
                send_current_value();
            }
        }

        /* Jog S/W — 10 ms 안정화 */
        int sw = gpio_get_level(GTS_PIN_ENC_SW);
        if (sw != last_sw) {
            if (++sw_stable >= 10) {
                last_sw      = sw;
                sw_stable    = 0;
                s_sw_pressed = (sw == 0);

                if (s_sw_pressed) {
                    gts_state_jog_sw();     /* step 순환 */
                    ESP_LOGI(TAG, "SW pressed — step cycled");
                }
            }
        } else {
            sw_stable = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

bool gts_input_sw_pressed(void) { return s_sw_pressed; }

void gts_input_start(void)
{
    encoder_gpio_init();
    xTaskCreate(encoder_task, "gts_enc", GTS_ENC_TASK_STACK, NULL,
                GTS_ENC_TASK_PRIO, NULL);
    ESP_LOGI(TAG, "encoder started (A=%d B=%d SW=%d)",
             GTS_PIN_ENC_A, GTS_PIN_ENC_B, GTS_PIN_ENC_SW);
}
