/*
 * gfc_uart.c — STM32 UART2 게이트웨이 구현
 *
 * 규격: DOC/GTS_GFC_UDP.md 5절 / 8절(FR-2 자동송신 감지 + 폴링 폴백)
 *
 * 태스크 2개
 *   gfc_uart_rx   : 바이트 스트림을 상태 기계로 파싱해 캐시를 갱신한다.
 *   gfc_uart_poll : 부팅 후 3초 동안 관찰 → 자동 송신이 없으면 1초 주기로
 *                   0x72 를 던진다. 자동 송신이 15초 끊기면 다시 폴링.
 */
#include "gfc_uart.h"
#include "gfc_config.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "GFC-UART";

#define UART_PORT   ((uart_port_t)GFC_UART_PORT)

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_tx_lock;
static gfc_uart_state_t  s_st;
static int64_t           s_last_rx_us;
static uint8_t           s_fail;
static volatile bool     s_expedite;   /* 다음 조회를 앞당긴다 */

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* ── 프레임 송신 ─────────────────────────────────────────────────── */

static bool uart_send(uint8_t cmd, const uint8_t *data, uint8_t size)
{
#if !GFC_STM32_ENABLE
    (void)cmd; (void)data; (void)size;
    return true;                    /* 보드 없이 경로만 볼 때 */
#else
    uint8_t f[GFC_UART_MAX_FRAME];
    size_t  i = 0;
    uint32_t sum = 0;

    f[i++] = 0x02;                  /* STX — 체크섬에서 제외 */
    f[i++] = cmd;   sum += cmd;
    f[i++] = size;  sum += size;
    for (uint8_t k = 0; k < size; k++) {
        f[i++] = data[k];
        sum += data[k];
    }
    f[i++] = (uint8_t)(255 - (sum % 256));

    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    int n = uart_write_bytes(UART_PORT, (const char *)f, i);
    xSemaphoreGive(s_tx_lock);

    if (n != (int)i) {
        ESP_LOGE(TAG, "write %d/%d", n, (int)i);
        return false;
    }
    ESP_LOGD(TAG, "TX 0x%02X size=%u", cmd, size);
    return true;
#endif
}

bool gfc_uart_pump_set(uint8_t p1, uint8_t p2, uint8_t p3)
{
    /* DOC 2.2-4: 기존 PC 코드가 Pump3 를 항상 Pump2 로 덮어썼다.
     * 3채널 독립 제어는 PCB 확정 후(U1) — 지금은 그 동작을 유지한다. */
    uint8_t d[3] = { p1 ? 1 : 0, p2 ? 1 : 0, p3 ? 1 : 0 };
    ESP_LOGI(TAG, "PUMP_SET %u %u %u", d[0], d[1], d[2]);
    bool ok = uart_send(GFC_UART_CMD_PUMP_SET, d, 3);

    /* 캐시를 명령한 값으로 낙관적 선반영한다.
     *
     * 이게 없으면 상향 SENSOR_DATA 가 STM32 폴링(1초)을 기다려야 갱신되고,
     * 명령 직후 보내는 SENSOR_DATA 가 오히려 낡은 펌프 상태를 서버에 퍼뜨린다.
     * STM32 가 명령을 무시했다면 다음 0x71/0x72 응답이 이 값을 덮어쓰므로
     * 틀린 상태가 오래 남지는 않는다. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.pump1 = d[0]; s_st.pump2 = d[1]; s_st.pump3 = d[2];
    xSemaphoreGive(s_lock);

    s_expedite = true;      /* 폴링 주기를 기다리지 말고 곧 확인하라 */
    return ok;
}

bool gfc_uart_pump_query(void)   { return uart_send(GFC_UART_CMD_PUMP_QUERY, NULL, 0); }
bool gfc_uart_sensor_query(void) { return uart_send(GFC_UART_CMD_SENSOR_QUERY, NULL, 0); }

/* ── 수신 프레임 반영 ────────────────────────────────────────────── */

static inline uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void apply_frame(uint8_t cmd, const uint8_t *d, uint8_t size)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    switch (cmd) {
    case GFC_UART_CMD_PUMP_SET:         /* 에코로 돌아오는 경우 대비 */
    case GFC_UART_CMD_PUMP_QUERY:
        if (size >= 3) {
            s_st.pump1 = d[0]; s_st.pump2 = d[1]; s_st.pump3 = d[2];
        }
        break;

    case GFC_UART_CMD_SENSOR_QUERY:
        if (size >= 11) {
            s_st.pump1 = d[0]; s_st.pump2 = d[1]; s_st.pump3 = d[2];
            s_st.co2_1 = rd_u16(&d[3]);     /* 예약 — U2 미확인 */
            s_st.raw1  = rd_u16(&d[5]);
            s_st.co2_2 = rd_u16(&d[7]);
            s_st.raw2  = rd_u16(&d[9]);
        }
        break;

    default:
        ESP_LOGD(TAG, "unhandled uart cmd 0x%02X", cmd);
        break;
    }

    s_st.link_ok = true;
    s_st.rx_frames++;
    xSemaphoreGive(s_lock);

    s_last_rx_us = esp_timer_get_time();
    s_fail = 0;
}

/* ── 수신 상태 기계 ──────────────────────────────────────────────── */

static void gfc_uart_rx_task(void *arg)
{
    (void)arg;
    enum { S_STX, S_CMD, S_SIZE, S_DATA, S_CS } st = S_STX;
    uint8_t  cmd = 0, size = 0, cnt = 0;
    uint32_t sum = 0;
    uint8_t  data[256];
    uint8_t  b;

    while (1) {
        int n = uart_read_bytes(UART_PORT, &b, 1, pdMS_TO_TICKS(100));
        if (n != 1) continue;

        switch (st) {
        case S_STX:
            if (b == 0x02) { sum = 0; st = S_CMD; }
            break;

        case S_CMD:
            cmd = b; sum += b; st = S_SIZE;
            break;

        case S_SIZE:
            size = b; sum += b; cnt = 0;
            st = (size == 0) ? S_CS : S_DATA;
            break;

        case S_DATA:
            data[cnt++] = b; sum += b;
            if (cnt >= size) st = S_CS;
            break;

        case S_CS:
            if (b == (uint8_t)(255 - (sum % 256))) {
                apply_frame(cmd, data, size);
            } else {
                ESP_LOGW(TAG, "checksum fail cmd=0x%02X", cmd);
            }
            st = S_STX;             /* 실패해도 다음 STX 로 재동기 */
            break;
        }
    }
}

/* ── 자동송신 감지 + 폴링 폴백 (DOC 8절) ─────────────────────────── */

static void gfc_uart_poll_task(void *arg)
{
    (void)arg;
    const int64_t boot_ms = now_ms();
    bool    observing = true;
    int32_t since_ms  = 0;      /* 마지막 조회 이후 경과 */

    while (1) {
        /* 100 ms 틱으로 돌면서 조회 시점만 따로 센다. 이렇게 해야
         * 펌프 명령 직후(s_expedite) 1초를 기다리지 않고 확인할 수 있다.
         * 0x70 바로 뒤에 0x71 을 붙여 쏘지는 않는다 — STM32 파서가
         * 연속 프레임에 약할 수 있어 한 틱(100 ms) 띄운다. */
        vTaskDelay(pdMS_TO_TICKS(GFC_CTRL_TICK_MS));
        since_ms += GFC_CTRL_TICK_MS;

        bool due  = (since_ms >= GFC_UART_POLL_PERIOD_MS);
        bool fast = false;
        if (s_expedite) { s_expedite = false; due = true; fast = true; }
        if (!due) continue;
        since_ms = 0;

        int64_t t       = now_ms();
        int64_t rx_age  = (s_last_rx_us > 0) ? (t - s_last_rx_us / 1000) : (t - boot_ms);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.age_ms = (uint32_t)(rx_age < 0 ? 0 : rx_age);
        bool polling = s_st.poll_mode;
        xSemaphoreGive(s_lock);

        /* 관찰 구간 — 요청하지 않았는데 프레임이 오면 AUTO_PUSH */
        if (observing) {
            if (t - boot_ms < GFC_UART_AUTOPUSH_WAIT_MS) {
                if (s_last_rx_us > 0) {
                    observing = false;
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    s_st.auto_push = true;
                    s_st.poll_mode = false;
                    xSemaphoreGive(s_lock);
                    ESP_LOGI(TAG, "STM32 auto-push detected");
                }
                continue;
            }
            observing = false;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_st.auto_push = false;
            s_st.poll_mode = true;
            xSemaphoreGive(s_lock);
            ESP_LOGW(TAG, "no auto-push in %d ms -> polling",
                     GFC_UART_AUTOPUSH_WAIT_MS);
            continue;
        }

        /* 자동 송신 모드인데 오래 끊기면 폴링으로 내려간다 */
        if (!polling) {
            /* 자동 송신 모드라도 펌프를 방금 건드렸으면 한 번은 물어본다.
             * STM32 의 다음 자동 프레임을 최대 5초 기다릴 수는 없다. */
            if (fast) gfc_uart_pump_query();
            if (rx_age > GFC_UART_AUTOPUSH_LOSS_MS) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_st.auto_push = false;
                s_st.poll_mode = true;
                xSemaphoreGive(s_lock);
                ESP_LOGW(TAG, "auto-push lost (%lld ms) -> polling", (long long)rx_age);
            }
            continue;
        }

        /* 폴링 모드 — 1초마다 0x72 */
        gfc_uart_sensor_query();

        if (rx_age > (int64_t)GFC_UART_POLL_PERIOD_MS * 3) {
            if (s_fail < 255) s_fail++;
            if (s_fail >= GFC_UART_FAIL_MAX) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (s_st.link_ok) ESP_LOGE(TAG, "STM32 link lost");
                s_st.link_ok = false;
                xSemaphoreGive(s_lock);
            }
        }
    }
}

/* ── 초기화 / 조회 ───────────────────────────────────────────────── */

bool gfc_uart_init(void)
{
    s_lock    = xSemaphoreCreateMutex();
    s_tx_lock = xSemaphoreCreateMutex();
    memset(&s_st, 0, sizeof(s_st));

    const uart_config_t cfg = {
        .baud_rate  = GFC_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t e = uart_driver_install(UART_PORT, 2048, 2048, 0, NULL, 0);
    if (e != ESP_OK) { ESP_LOGE(TAG, "driver_install: %d", e); return false; }
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, GFC_UART_TX_PIN, GFC_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART%d %d 8N1 TX=%d RX=%d (STM32 %s)",
             GFC_UART_PORT, GFC_UART_BAUD, GFC_UART_TX_PIN, GFC_UART_RX_PIN,
             GFC_STM32_ENABLE ? "enabled" : "DISABLED");

    xTaskCreate(gfc_uart_rx_task,   "gfc_uart_rx",   4096, NULL, 6, NULL);
    xTaskCreate(gfc_uart_poll_task, "gfc_uart_poll", 3072, NULL, 4, NULL);
    return true;
}

void gfc_uart_get(gfc_uart_state_t *out)
{
    if (!out) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_lock);
}

float gfc_uart_raw_to_volt(uint16_t raw)
{
    return (float)raw * GFC_ADC_VREF / GFC_ADC_FULL;
}
