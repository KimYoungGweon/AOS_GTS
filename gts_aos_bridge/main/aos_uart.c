/*
 * aos_uart.c — STM32 UART2 게이트웨이 구현
 *
 * 근거: DOC/FAIMs_H753_V1_6_1_TWIN/Core/Src/MyWork/UART_PC.c
 *
 * 태스크 1개 (aos_uart_rx) 가 바이트 스트림을 상태 기계로 파싱한다.
 * 주기 조회(0x02)는 aos_ctrl.c 의 틱이 부른다 — 여기서 태스크를 또
 * 띄우지 않는 이유는, 조회 시점이 파라미터 송신과 엮여 있기 때문이다.
 */
#include "aos_uart.h"
#include "aos_config.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "AOS-UART";

#define UART_PORT   ((uart_port_t)AOS_UART_PORT)

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_tx_lock;
static aos_uart_state_t  s_st;
static int64_t           s_last_rx_us;
static uint8_t           s_fail;

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* ── 비정렬 읽기 헬퍼 ────────────────────────────────────────────────
 * STM32 의 CMD_SET_QUERY 응답은 offset 17/23/27 에 float 이 온다.
 * 4 byte 경계가 아니므로 포인터 캐스팅 대신 memcpy 로 읽는다.        */
static inline float rd_f32(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }

#if AOS_UART_LOG_HEX
/* 프레임을 한 줄 hex 로. 이 브리지가 쓰는 프레임은 길어야 32 byte 남짓이다. */
static void log_hex(const char *dir, const uint8_t *b, size_t n)
{
    char t[3 * 48 + 8];
    size_t k = 0, lim = (n > 48) ? 48 : n;
    for (size_t i = 0; i < lim && k + 4 < sizeof(t); i++)
        k += (size_t)snprintf(&t[k], sizeof(t) - k, i ? " %02X" : "%02X", b[i]);
    if (n > lim) snprintf(&t[k], sizeof(t) - k, " … (%u)", (unsigned)n);
    ESP_LOGI(TAG, "%s %s", dir, t);
}
#endif
static inline uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* ── 프레임 송신 ─────────────────────────────────────────────────── */

static bool uart_send(uint8_t cmd, const uint8_t *data, uint16_t size)
{
#if !AOS_STM32_ENABLE
    /* ★ 여기로 들어오면 UART 로 아무것도 안 나간다.
     * 조용히 true 를 돌려주면 "SET_CONTROL 로그는 뜨는데 장비가 안 움직인다"
     * 는 상황이 되어 배선을 의심하게 된다. 그래서 매번 크게 알린다.      */
    (void)data;
    ESP_LOGW(TAG, "TX 0x%02X size=%u — AOS_STM32_ENABLE=0 이라 실제로 안 보냄",
             cmd, size);
    return true;
#else
    uint8_t  f[AOS_UART_DATA_MAX + 8];
    size_t   i = 0;
    uint32_t sum = 0;

    if (size > AOS_UART_DATA_MAX) return false;

    f[i++] = 0x02;                      /* STX — 체크섬에서 제외 */
    f[i++] = cmd;                       sum += cmd;
    f[i++] = (uint8_t)(size & 0xFF);    sum += (size & 0xFF);
    f[i++] = (uint8_t)(size >> 8);      sum += (size >> 8);
    for (uint16_t k = 0; k < size; k++) {
        f[i++] = data[k];
        sum += data[k];
    }
    f[i++] = (uint8_t)((~sum) & 0xFF);

    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    int n = uart_write_bytes(UART_PORT, (const char *)f, i);
    xSemaphoreGive(s_tx_lock);

    if (n != (int)i) {
        ESP_LOGE(TAG, "write %d/%d", n, (int)i);
        return false;
    }
#if AOS_UART_LOG_HEX
    log_hex("TX→STM32", f, i);
#else
    ESP_LOGD(TAG, "TX 0x%02X size=%u", cmd, size);
#endif
    return true;
#endif
}

bool aos_uart_set_control(float hv, float frq, float duty, float cv)
{
    /* 0x2A CMD_SET_CONTROL — 순서 주의: HV, Frq, Duty, CV.
     * (0x02 읽기 응답은 HV, CV, Frq, Duty 로 순서가 다르다.)          */
    uint8_t d[16];
    memcpy(&d[0],  &hv,   4);
    memcpy(&d[4],  &frq,  4);
    memcpy(&d[8],  &duty, 4);
    memcpy(&d[12], &cv,   4);
    ESP_LOGI(TAG, "SET_CONTROL hv=%.2f frq=%.1f duty=%.2f cv=%.3f",
             (double)hv, (double)frq, (double)duty, (double)cv);
    return uart_send(AOS_UART_SET_CONTROL, d, sizeof(d));
}

bool aos_uart_lf_mod_set(uint8_t type, uint8_t on, float amp, float frq)
{
    /* 0x52 CMD_LF_MOD_SET — type, OnOff, amp(f32), frq(f32) */
    uint8_t d[10];
    d[0] = type;
    d[1] = on ? 1 : 0;
    memcpy(&d[2], &amp, 4);
    memcpy(&d[6], &frq, 4);
    ESP_LOGI(TAG, "LF_MOD_SET type=%u on=%u amp=%.2f frq=%.1f",
             type, d[1], (double)amp, (double)frq);
    return uart_send(AOS_UART_LF_MOD_SET, d, sizeof(d));
}

bool aos_uart_set_query(void)   { return uart_send(AOS_UART_SET_QUERY, NULL, 0); }

bool aos_uart_auto_status(bool on)
{
    uint8_t v = on ? 1 : 0;
    return uart_send(AOS_UART_AUTO_STATUS_ONOFF, &v, 1);
}

/* ── 수신 프레임 반영 ────────────────────────────────────────────── */

static void apply_frame(uint8_t cmd, const uint8_t *d, uint16_t size)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    switch (cmd) {

    case AOS_UART_SET_QUERY:
        /* TxMessage_CTL_PC(CMD_SET_QUERY) 가 만드는 32 byte.
         *   0  f32 RF_HV        16 u8  RF_MOD_OnOff
         *   4  f32 CV           17 f32 RF_MOD_frq      ← 비정렬
         *   8  f32 Frq          21 u8  LF_MOD.OnOff
         *  12  f32 Duty         22 u8  LF_MOD.type
         *                       23 f32 LF_MOD.amp      ← 비정렬
         *                       27 f32 LF_MOD.frq      ← 비정렬
         *                       31 u8  Current_Type                    */
        if (size >= 32) {
            s_st.hv           = rd_f32(&d[0]);
            s_st.cv           = rd_f32(&d[4]);
            s_st.frq          = rd_f32(&d[8]);
            s_st.duty         = rd_f32(&d[12]);
            s_st.rf_mod_on    = d[16];
            s_st.rf_mod_frq   = rd_f32(&d[17]);
            s_st.lf_on        = d[21];
            s_st.lf_type      = d[22];
            s_st.lf_amp       = rd_f32(&d[23]);
            s_st.lf_frq       = rd_f32(&d[27]);
            s_st.current_type = d[31];
            s_st.have_set     = true;
            s_st.set_count++;
        }
        break;

    case AOS_UART_STATUS_QUERY:
        /*  0 u8  device(0x01)   9  f32 HV_Vs
         *  1 u16 adcAvg_P       13 f32 FAN_Vs
         *  3 u16 adcAvg_N       17 f32 ION_Bias_Vs
         *  5 u16 TW_adcAvg_P
         *  7 u16 TW_adcAvg_N                                            */
        if (size >= 13) {
            s_st.adc_p    = rd_u16(&d[1]);
            s_st.adc_n    = rd_u16(&d[3]);
            s_st.tw_adc_p = rd_u16(&d[5]);
            s_st.tw_adc_n = rd_u16(&d[7]);
            s_st.hv_sense = rd_f32(&d[9]);
            s_st.cur_count++;
        }
        break;

    case AOS_UART_CV_STATUS:
        /* 0 u16 adcAvg_P, 2 f32 CV, 6 u16 SendCountNo, 8.. Buf_Avg[10] */
        if (size >= 6) {
            s_st.adc_p = rd_u16(&d[0]);
            s_st.cv    = rd_f32(&d[2]);
        }
        break;

    case AOS_UART_RECEIVED_OK:
        break;

    default:
        ESP_LOGD(TAG, "unhandled uart cmd 0x%02X size=%u", cmd, size);
        break;
    }

    if (cmd != AOS_UART_RECEIVED_OK && cmd != AOS_UART_SET_QUERY &&
        cmd != AOS_UART_STATUS_QUERY && cmd != AOS_UART_CV_STATUS)
        s_st.busy_count++;
    s_st.link_ok = true;
    s_st.rx_frames++;
    xSemaphoreGive(s_lock);

    s_last_rx_us = esp_timer_get_time();
    s_fail = 0;
}

/* ── 수신 상태 기계 (STM32 의 Rx_MSG_CTL_PC 와 같은 순서) ────────── */

static void aos_uart_rx_task(void *arg)
{
    (void)arg;
    enum { S_STX, S_CMD, S_SIZE1, S_SIZE2, S_DATA, S_CS } st = S_STX;
    uint8_t  cmd = 0;
    uint16_t size = 0, cnt = 0;
    uint32_t sum = 0;
    bool     overflow = false;      /* 버퍼보다 큰 프레임은 흘려보낸다 */
    uint8_t  data[AOS_UART_DATA_MAX];
    uint8_t  b;

    while (1) {
        int n = uart_read_bytes(UART_PORT, &b, 1, pdMS_TO_TICKS(100));
        if (n != 1) continue;

        switch (st) {
        case S_STX:
            /* STM32 와 동일 — STX 를 보면 언제나 리셋하고 다시 시작 */
            if (b == 0x02) { sum = 0; st = S_CMD; }
            break;

        case S_CMD:
            cmd = b; sum += b; st = S_SIZE1;
            break;

        case S_SIZE1:
            size = b; sum += b; st = S_SIZE2;
            break;

        case S_SIZE2:
            size |= (uint16_t)b << 8; sum += b;
            cnt = 0;
            overflow = (size > AOS_UART_DATA_MAX);
            if (overflow)
                ESP_LOGW(TAG, "frame too big cmd=0x%02X size=%u — 폐기", cmd, size);
            st = (size == 0) ? S_CS : S_DATA;
            break;

        case S_DATA:
            if (!overflow) data[cnt] = b;
            cnt++; sum += b;
            if (cnt >= size) st = S_CS;
            break;

        case S_CS:
            if (b == (uint8_t)((~sum) & 0xFF)) {
#if AOS_UART_LOG_HEX
                /* ★ 0x03 전류 상태는 대기 중 25ms 마다(초당 40번) 온다. 이걸 줄마다 hex 로 찍으면
                 *   콘솔(UART0) 출력이 못 따라가 이 태스크(우선순위 6)가 CPU 를 다 잡고,
                 *   LED·UDP 송신 태스크가 멈춘다 (2026-09-23 실제 발생). 0x03 은 찍지 않는다.
                 *   값은 1초마다 health 로그와 STATUS(0x45)로 확인한다.                         */
                if (!overflow && cmd != AOS_UART_STATUS_QUERY) {
                    uint8_t hdr[4] = { 0x02, cmd,
                                       (uint8_t)(size & 0xFF), (uint8_t)(size >> 8) };
                    log_hex("RX←STM32 hdr", hdr, 4);
                    if (size) log_hex("RX←STM32 dat", data, size);
                }
#endif
                if (!overflow) apply_frame(cmd, data, size);
                else           s_last_rx_us = esp_timer_get_time();
            } else {
                ESP_LOGW(TAG, "checksum fail cmd=0x%02X size=%u", cmd, size);
            }
            st = S_STX;             /* 실패해도 다음 STX 로 재동기 */
            break;
        }
    }
}

/* ── 초기화 / 조회 ───────────────────────────────────────────────── */

bool aos_uart_init(void)
{
    s_lock    = xSemaphoreCreateMutex();
    s_tx_lock = xSemaphoreCreateMutex();
    memset(&s_st, 0, sizeof(s_st));

    const uart_config_t cfg = {
        .baud_rate  = AOS_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t e = uart_driver_install(UART_PORT, 4096, 2048, 0, NULL, 0);
    if (e != ESP_OK) { ESP_LOGE(TAG, "driver_install: %d", e); return false; }
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, AOS_UART_TX_PIN, AOS_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART%d %d 8N1 TX=%d RX=%d SIZE=2byte (STM32 %s)",
             AOS_UART_PORT, AOS_UART_BAUD, AOS_UART_TX_PIN, AOS_UART_RX_PIN,
             AOS_STM32_ENABLE ? "enabled" : "DISABLED");

    xTaskCreate(aos_uart_rx_task, "aos_uart_rx", 4096, NULL, 6, NULL);
    return true;
}

void aos_uart_get(aos_uart_state_t *out)
{
    if (!out) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t t = now_ms();
    s_st.age_ms = (uint32_t)((s_last_rx_us > 0) ? (t - s_last_rx_us / 1000) : 0);
    *out = s_st;
    xSemaphoreGive(s_lock);
}

/** UART 링크 상태를 한 줄 찍는다. aos_ctrl 의 틱이 주기적으로 부른다. */
void aos_uart_log_health(void)
{
    aos_uart_state_t u;
    aos_uart_get(&u);

    if (u.link_ok && u.have_set) {
        ESP_LOGI(TAG, "link OK  rx=%lu  hv=%.2f cv=%.3f frq=%.1f duty=%.2f "
                      "lf=%u/%u amp=%.2f frq=%.1f  cur#%lu adc=%u/%u/%u/%u",
                 (unsigned long)u.rx_frames,
                 (double)u.hv, (double)u.cv, (double)u.frq, (double)u.duty,
                 u.lf_on, u.lf_type, (double)u.lf_amp, (double)u.lf_frq,
                 (unsigned long)u.cur_count,
                 (unsigned)u.adc_p, (unsigned)u.adc_n, (unsigned)u.tw_adc_p, (unsigned)u.tw_adc_n);
    } else if (u.rx_frames == 0) {
        ESP_LOGE(TAG, "STM32 에서 한 프레임도 못 받았다 (rx=0). "
                      "배선/보레이트 확인 — ESP32 TX%d→STM32 PA3, "
                      "ESP32 RX%d←STM32 PA2, GND 공통, 38400 8N1",
                 AOS_UART_TX_PIN, AOS_UART_RX_PIN);
    } else {
        ESP_LOGW(TAG, "link %s  rx=%lu  마지막 수신 %lu ms 전",
                 u.link_ok ? "OK" : "LOST",
                 (unsigned long)u.rx_frames, (unsigned long)u.age_ms);
    }
}

/** aos_ctrl 의 틱이 무응답 판정을 넘겨 준다. */
void aos_uart_note_timeout(void)
{
    if (s_fail < 255) s_fail++;
    if (s_fail >= AOS_UART_FAIL_MAX) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_st.link_ok) ESP_LOGE(TAG, "STM32 link lost");
        s_st.link_ok = false;
        xSemaphoreGive(s_lock);
    }
}
