/*
 * main.c — GTS Console (ESP32-P4 + JC4880P443C)
 *
 * 하드웨어 초기화(MIPI DSI · ST7701 · GT911 · LVGL)는 검증된
 * JC4880P443C_Demo 의 코드를 그대로 가져왔다. 달라진 곳은 두 군데다.
 *
 *  1) 화면 회전 — 패널은 480x800 세로지만 사양서는 800x480 가로다.
 *     LVGL 디스플레이를 ROTATION_90 으로 두고, flush 콜백에서 렌더
 *     결과를 직접 90도 돌려 패널에 넘긴다. LVGL 9.6 은 partial 모드
 *     에서 소프트웨어 회전을 자동으로 해 주지 않기 때문이다.
 *     터치 좌표 변환은 LVGL 이 알아서 한다(indev_pointer_proc).
 *
 *  2) MQTT 제거, UDP 프로토콜(gts_protocol) + Jog(gts_input) 추가.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "lvgl.h"

#include "jc4880p443c.h"
#include "gts_config.h"
#include "gts_state.h"
#include "gts_protocol.h"
#include "gts_input.h"
#include "gts_rotate.h"
#include "gts_ui.h"
#include "wifi_manager.h"

static const char *TAG = "GTS";

static uint16_t s_panel_w, s_panel_h;

#define BL_LEDC_CH        0
#define BL_LEDC_TIMER     LEDC_TIMER_1
#define BL_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BL_PWM_FREQ_HZ    20000
#define BL_PWM_RES        LEDC_TIMER_10_BIT

#define LVGL_TICK_MS      2
#define LVGL_MAX_DELAY_MS 500
#define LVGL_MIN_DELAY_MS 1
/* 부분 렌더 버퍼: 논리 가로 800 x 30 줄. 데모(480x50)와 픽셀 수가 같다. */
#define LVGL_BUF_PX       (GTS_SCR_W * 30)

/* ── 백라이트 ────────────────────────────────────────────────────── */
static esp_err_t backlight_init(void)
{
    ledc_timer_config_t lt = {
        .speed_mode      = BL_LEDC_MODE,
        .duty_resolution = BL_PWM_RES,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&lt), TAG, "LEDC timer");

    ledc_channel_config_t lc = {
        .gpio_num   = GTS_PIN_LCD_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CH,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&lc), TAG, "LEDC channel");
    return ESP_OK;
}

static esp_err_t backlight_set(int pct)
{
    if (pct > 100) pct = 100;
    if (pct < 0)   pct = 0;
    uint32_t duty = (1023u * (uint32_t)pct) / 100u;
    ESP_RETURN_ON_ERROR(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CH, duty), TAG, "duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CH), TAG, "update");
    return ESP_OK;
}

/* ── MIPI PHY 전원 ───────────────────────────────────────────────── */
static esp_err_t mipi_phy_power_init(void)
{
    static esp_ldo_channel_handle_t chan = NULL;
    esp_ldo_channel_config_t cfg = {
        .chan_id    = GTS_MIPI_LDO_CHAN,
        .voltage_mv = GTS_MIPI_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&cfg, &chan), TAG, "LDO");
    return ESP_OK;
}

/* ── 디스플레이 ──────────────────────────────────────────────────── */
static esp_err_t display_init(esp_lcd_panel_handle_t *ret_panel,
                              esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;
    esp_lcd_dsi_bus_handle_t  dsi_bus = NULL;
    esp_lcd_panel_io_handle_t io      = NULL;
    esp_lcd_panel_handle_t    panel   = NULL;

    const st7701_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    jc4880p443c_get_init_cmds(&init_cmds, &init_cmds_size);

    uint32_t lane_bit_rate; uint8_t num_lanes;
    jc4880p443c_get_dsi_config(&lane_bit_rate, &num_lanes);

    uint32_t pclk_mhz; uint16_t hbp, hfp, vbp, vfp;
    jc4880p443c_get_timing(&pclk_mhz, &hbp, &hfp, &vbp, &vfp);
    jc4880p443c_get_resolution(&s_panel_w, &s_panel_h);

    ESP_GOTO_ON_ERROR(backlight_init(),      err, TAG, "backlight");
    ESP_GOTO_ON_ERROR(mipi_phy_power_init(), err, TAG, "mipi pwr");

    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = num_lanes,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = lane_bit_rate,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), err, TAG, "dsi bus");

    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io),
                      err, TAG, "dbi io");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = pclk_mhz,
        .virtual_channel    = 0,
        .in_color_format    = LCD_COLOR_FMT_RGB888,
        .num_fbs            = 2,
        .video_timing = {
            .h_size            = s_panel_w,
            .v_size            = s_panel_h,
            .hsync_pulse_width = 12,
            .hsync_back_porch  = hbp,
            .hsync_front_porch = hfp,
            .vsync_pulse_width = 2,
            .vsync_back_porch  = vbp,
            .vsync_front_porch = vfp,
        },
    };

    st7701_vendor_config_t vendor_cfg = {
        .init_cmds      = init_cmds,
        .init_cmds_size = init_cmds_size,
        .mipi_config    = { .dsi_bus = dsi_bus, .dpi_config = &dpi_cfg },
        .flags          = { .use_mipi_interface = 1 },
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GTS_PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
        .vendor_config  = &vendor_cfg,
    };

    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7701(io, &panel_cfg, &panel),
                      err, TAG, "st7701");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(panel),             err, TAG, "reset");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(panel),              err, TAG, "init");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), err, TAG, "on");

    *ret_panel = panel;
    *ret_io    = io;
    return ESP_OK;

err:
    if (panel)   esp_lcd_panel_del(panel);
    if (io)      esp_lcd_panel_io_del(io);
    if (dsi_bus) esp_lcd_del_dsi_bus(dsi_bus);
    return ret;
}

/* ── 터치 ────────────────────────────────────────────────────────── */
static esp_err_t touch_init(esp_lcd_touch_handle_t *ret_tp)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = GTS_TOUCH_I2C_PORT,
        .sda_io_num        = GTS_PIN_TOUCH_SDA,
        .scl_io_num        = GTS_PIN_TOUCH_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c bus");

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = GTS_TOUCH_I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &tp_io_cfg, &tp_io),
                        TAG, "touch io");

    esp_lcd_touch_io_gt911_config_t gt911_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
    };

    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = s_panel_w,      /* 물리 패널 기준 480 */
        .y_max        = s_panel_h,      /* 물리 패널 기준 800 */
        .rst_gpio_num = GTS_PIN_TOUCH_RST,
        .int_gpio_num = GTS_PIN_TOUCH_INT,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .driver_data  = &gt911_cfg,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, ret_tp),
                        TAG, "gt911");
    ESP_LOGI(TAG, "GT911 ready (SDA=%d SCL=%d)", GTS_PIN_TOUCH_SDA, GTS_PIN_TOUCH_SCL);
    return ESP_OK;
}

/* ── LVGL ────────────────────────────────────────────────────────── */
static void lvgl_tick_cb(void *arg) { (void)arg; lv_tick_inc(LVGL_TICK_MS); }

static esp_err_t lvgl_tick_init(void)
{
    const esp_timer_create_args_t args = {
        .callback = lvgl_tick_cb, .name = "lvgl_tick"
    };
    esp_timer_handle_t t = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &t), TAG, "tick timer");
    return esp_timer_start_periodic(t, LVGL_TICK_MS * 1000);
}

static uint8_t *s_rot_buf;      /* 회전 결과 버퍼 (RGB888) */

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);

    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);

    /* LVGL 은 버퍼 행을 LV_DRAW_BUF_STRIDE_ALIGN 으로 정렬할 수 있다.
     * w * 3 이라고 가정하면 정렬이 1 이 아닐 때 화면이 어긋난다.      */
    int32_t stride = (int32_t)lv_draw_buf_width_to_stride(
                         (uint32_t)w, lv_display_get_color_format(disp));

    /* lv_display_rotation_t 와 gts_rot_t 는 값이 같지만, 명시적으로 옮겨
     * 둬야 LVGL 이 enum 을 바꿔도 조용히 틀리지 않는다.               */
    gts_rot_t rot;
    switch (lv_display_get_rotation(disp)) {
        case LV_DISPLAY_ROTATION_90:  rot = GTS_ROT_90;  break;
        case LV_DISPLAY_ROTATION_180: rot = GTS_ROT_180; break;
        case LV_DISPLAY_ROTATION_270: rot = GTS_ROT_270; break;
        default:                      rot = GTS_ROT_0;   break;
    }

    lv_area_t phys = *area;
    lv_display_rotate_area(disp, &phys);

    gts_rotate_rgb888(px_map, s_rot_buf, w, h, stride, rot);

    esp_lcd_panel_draw_bitmap(panel,
                              phys.x1, phys.y1,
                              phys.x2 + 1, phys.y2 + 1,
                              s_rot_buf);
    lv_display_flush_ready(disp);
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(tp);

    esp_lcd_touch_point_data_t point;
    uint8_t cnt = 0;
    esp_lcd_touch_get_data(tp, &point, &cnt, 1);

    if (cnt > 0) {
        /* 물리 좌표 그대로 넘긴다. 회전 변환은 LVGL 이 한다. */
        data->point.x = point.x;
        data->point.y = point.y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}

static lv_display_t *lvgl_display_init(esp_lcd_panel_handle_t panel)
{
    lv_init();
    ESP_ERROR_CHECK(lvgl_tick_init());

    /* 물리 해상도로 만든 뒤 90도 회전 → 논리 800x480 */
    lv_display_t *disp = lv_display_create(s_panel_w, s_panel_h);
    assert(disp);

    size_t buf_bytes = (size_t)LVGL_BUF_PX * 3;   /* RGB888 */
    /* 정렬 패딩이 붙어도 넘치지 않도록 여유를 둔다 */
    size_t rot_bytes = buf_bytes + 3 * GTS_SCR_W;

    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA);
    assert(buf1 && buf2);

    /* 회전 버퍼는 CPU 만 읽으므로 PSRAM 으로 충분하다. */
    s_rot_buf = heap_caps_malloc(rot_bytes, MALLOC_CAP_SPIRAM);
    if (!s_rot_buf) s_rot_buf = heap_caps_malloc(rot_bytes, MALLOC_CAP_DEFAULT);
    assert(s_rot_buf);

    lv_display_set_buffers(disp, buf1, buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_user_data(disp, panel);
#if   GTS_ROTATION_DEG == 90
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
#elif GTS_ROTATION_DEG == 180
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
#elif GTS_ROTATION_DEG == 270
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
#else
#error "GTS_ROTATION_DEG must be 90, 180 or 270"
#endif

    ESP_LOGI(TAG, "LVGL display %ldx%ld (panel %ux%u, rotated %d deg)",
             (long)lv_display_get_horizontal_resolution(disp),
             (long)lv_display_get_vertical_resolution(disp),
             s_panel_w, s_panel_h, GTS_ROTATION_DEG);
    return disp;
}

static void lvgl_task(void *arg)
{
    (void)arg;
    uint32_t d = LVGL_MAX_DELAY_MS;
    while (1) {
        d = lv_timer_handler();
        if (d > LVGL_MAX_DELAY_MS) d = LVGL_MAX_DELAY_MS;
        if (d < LVGL_MIN_DELAY_MS) d = LVGL_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(d));
    }
}

/* ════════════════════════════════════════════════════════════════════
 * app_main
 * ════════════════════════════════════════════════════════════════════ */
void app_main(void)
{
    ESP_LOGI(TAG, "GTS Console — ESP32-P4 / JC4880P443C");

    gts_state_init();

    esp_lcd_panel_handle_t    panel = NULL;
    esp_lcd_panel_io_handle_t io    = NULL;

    if (display_init(&panel, &io) != ESP_OK) {
        ESP_LOGE(TAG, "display init failed — halting");
        return;
    }
    ESP_ERROR_CHECK(backlight_set(90));

    lv_display_t *disp = lvgl_display_init(panel);
    assert(disp);

    esp_lcd_touch_handle_t tp = NULL;
    if (touch_init(&tp) == ESP_OK) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
        lv_indev_set_user_data(indev, tp);
        lv_indev_set_display(indev, disp);
    } else {
        ESP_LOGW(TAG, "touch init failed — display-only mode");
    }

    ESP_LOGI(TAG, "heap before UI: internal %u, psram %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    gts_ui_create(disp);

    /* LVGL 은 CLIB malloc 을 쓰므로 위젯이 이 힙에서 나온다.
     * 여기서 내부 RAM 이 바닥을 보이면 렌더 중 할당 실패로 죽는다.   */
    ESP_LOGI(TAG, "heap after  UI: internal %u, psram %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* LVGL 태스크를 Wi-Fi 보다 먼저 띄워야 연결 대기 중에도 화면이
     * 갱신된다 (데모와 같은 이유).                                   */
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", GTS_LVGL_TASK_STACK, NULL,
                            GTS_LVGL_TASK_PRIO, NULL, GTS_LVGL_TASK_CORE);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Jog 는 네트워크와 무관하게 동작한다 */
    gts_input_start();

    /* Wi-Fi 는 논블로킹이다 — 등록 지점 접속을 시작만 하고 바로 돌아온다.
     * 결과는 이벤트 핸들러가 g_gts.wifi_* 에 쓰고, P5 가 그것을 보여준다.
     * 연결 전에 UDP 소켓을 열어 둬도 sendto 가 실패할 뿐이라 문제없다. */
    if (wifi_manager_start() != ESP_OK)
        ESP_LOGW(TAG, "WiFi 초기화 실패 — UDP 는 동작하지 않는다");

    gts_proto_start();

    ESP_LOGI(TAG, "running");
}
