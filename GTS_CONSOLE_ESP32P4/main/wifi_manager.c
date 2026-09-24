/*
 * wifi_manager.c — 등록 지점 기반 Wi-Fi 관리
 */
#include "wifi_manager.h"
#include "gts_config.h"
#include "gts_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"

#if GTS_WIFI_SNTP
#include "esp_netif_sntp.h"
#endif

static const char *TAG = "WIFI";

#define NVS_KEY_IDX   "last_idx"
#define NVS_KEY_TIME  "last_time"

static bool     s_started;
static uint8_t  s_target;          /* 지금 붙으려는 지점 */
static uint8_t  s_retry;
static bool     s_manual_stop;     /* 사용자가 끊었으면 자동 재접속 안 함 */

/* ════════════════════════════════════════════════════════════════════
 * NVS — 최근 접속 지점
 * ════════════════════════════════════════════════════════════════════ */
static void nvs_save_last(uint8_t idx)
{
    nvs_handle_t h;
    if (nvs_open(GTS_WIFI_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    int64_t now = (int64_t)time(NULL);
    nvs_set_u8(h, NVS_KEY_IDX, idx);
    nvs_set_i64(h, NVS_KEY_TIME, now);
    nvs_commit(h);
    nvs_close(h);

    gts_state_lock();
    g_gts.wifi_last_idx  = idx;
    g_gts.wifi_last_time = now;
    gts_state_unlock();

    ESP_LOGI(TAG, "최근 접속 지점 저장: %u (%s)", idx, GTS_WIFI_NET[idx].name);
}

static void nvs_load_last(void)
{
    uint8_t idx = 0xFF;
    int64_t t   = 0;

    nvs_handle_t h;
    if (nvs_open(GTS_WIFI_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, NVS_KEY_IDX, &idx) != ESP_OK) idx = 0xFF;
        if (nvs_get_i64(h, NVS_KEY_TIME, &t) != ESP_OK) t = 0;
        nvs_close(h);
    }
    if (idx >= GTS_WIFI_NET_COUNT) idx = 0xFF;

    gts_state_lock();
    g_gts.wifi_last_idx  = idx;
    g_gts.wifi_last_time = t;
    g_gts.wifi_sel       = (idx == 0xFF) ? 0 : idx;
    gts_state_unlock();
}

/* ════════════════════════════════════════════════════════════════════
 * 상태 갱신 헬퍼
 * ════════════════════════════════════════════════════════════════════ */
static void set_state(gts_wifi_conn_t st)
{
    gts_state_lock();
    g_gts.wifi_state = st;
    if (st != GTS_WIFI_CONNECTED) {
        g_gts.wifi_conn_idx = 0xFF;
        g_gts.wifi_ip[0]    = '\0';
        g_gts.wifi_rssi     = 0;
    }
    gts_state_unlock();
    gts_state_mark_dirty();
}

/* ════════════════════════════════════════════════════════════════════
 * 접속
 * ════════════════════════════════════════════════════════════════════ */
static void apply_and_connect(uint8_t idx)
{
    const gts_wifi_net_t *n = &GTS_WIFI_NET[idx];

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid,     n->ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, n->pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode  = GTS_WIFI_AUTH_THRESHOLD;
    cfg.sta.sae_pwe_h2e         = GTS_WIFI_SAE_MODE;
    strncpy((char *)cfg.sta.sae_h2e_identifier, GTS_WIFI_SAE_H2E_ID,
            sizeof(cfg.sta.sae_h2e_identifier) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    s_target      = idx;
    s_retry       = 0;
    s_manual_stop = false;

    set_state(GTS_WIFI_CONNECTING);
    ESP_LOGI(TAG, "접속 시도: [%u] %s (%s)", idx, n->name, n->ssid);
    esp_wifi_connect();
}

/* ════════════════════════════════════════════════════════════════════
 * 이벤트
 * ════════════════════════════════════════════════════════════════════ */
static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg; (void)base;

    switch (id) {

    case WIFI_EVENT_STA_START:
        /* start 직후의 connect 는 apply_and_connect 가 이미 불렀다 */
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        if (s_manual_stop) { set_state(GTS_WIFI_DISCONNECTED); break; }

        if (s_retry < GTS_WIFI_MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "재시도 %u/%u — %s",
                     s_retry, GTS_WIFI_MAX_RETRY, GTS_WIFI_NET[s_target].ssid);
            set_state(GTS_WIFI_CONNECTING);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "접속 실패: %s", GTS_WIFI_NET[s_target].ssid);
            set_state(GTS_WIFI_DISCONNECTED);
        }
        break;
    }

    case WIFI_EVENT_SCAN_DONE: {
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        if (n > 24) n = 24;

        /* wifi_ap_record_t 는 80 byte 가 넘는다. 이벤트 태스크 스택에
         * 배열로 잡으면 넘치므로 힙에서 받는다.                      */
        wifi_ap_record_t *ap = NULL;
        if (n) {
            ap = malloc(sizeof(wifi_ap_record_t) * n);
            if (!ap || esp_wifi_scan_get_ap_records(&n, ap) != ESP_OK) n = 0;
        }
        if (!n) esp_wifi_clear_ap_list();

        gts_state_lock();
        uint8_t found = 0;
        for (int i = 0; i < GTS_WIFI_NET_COUNT; i++) {
            g_gts.wifi_found[i]     = false;
            g_gts.wifi_rssi_list[i] = 0;
            for (int j = 0; j < n; j++) {
                if (strcmp((const char *)ap[j].ssid, GTS_WIFI_NET[i].ssid) == 0) {
                    g_gts.wifi_found[i]     = true;
                    g_gts.wifi_rssi_list[i] = ap[j].rssi;
                    found++;
                    break;
                }
            }
        }
        g_gts.wifi_scan_found = found;
        g_gts.wifi_scan_time  = (int64_t)time(NULL);
        g_gts.wifi_scanning   = false;
        gts_state_unlock();
        gts_state_mark_dirty();

        free(ap);
        ESP_LOGI(TAG, "스캔 완료 — AP %u개 중 등록 지점 %u개", n, found);
        break;
    }

    default: break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;

    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));

    wifi_ap_record_t ap;
    int8_t rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;

    gts_state_lock();
    g_gts.wifi_state    = GTS_WIFI_CONNECTED;
    g_gts.wifi_conn_idx = s_target;
    g_gts.wifi_rssi     = rssi;
    strncpy(g_gts.wifi_ip, ip, sizeof(g_gts.wifi_ip) - 1);
    g_gts.wifi_ip[sizeof(g_gts.wifi_ip) - 1] = '\0';
    gts_state_unlock();
    gts_state_mark_dirty();

    s_retry = 0;
    ESP_LOGI(TAG, "연결됨: %s  IP %s  RSSI %d",
             GTS_WIFI_NET[s_target].ssid, ip, rssi);

    nvs_save_last(s_target);

#if GTS_WIFI_SNTP
    /* 시각이 아직이면 한 번만 맞춘다 — P5 의 "최근 접속 HH:MM" 용 */
    static bool sntp_done;
    if (!sntp_done) {
        sntp_done = true;
        setenv("TZ", GTS_WIFI_TZ, 1);
        tzset();
        esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG(GTS_WIFI_SNTP_SERVER);
        c.start = true;
        c.sync_cb = NULL;
        if (esp_netif_sntp_init(&c) != ESP_OK)
            ESP_LOGW(TAG, "SNTP 초기화 실패 — 시각 표시는 생략된다");
    }
#endif
}

/* ════════════════════════════════════════════════════════════════════
 * 공개 API
 * ════════════════════════════════════════════════════════════════════ */
esp_err_t wifi_manager_start(void)
{
    if (s_started) return ESP_OK;

    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    nvs_load_last();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_started = true;

    uint8_t first;
    gts_state_lock();
    first = (g_gts.wifi_last_idx == 0xFF) ? 0 : g_gts.wifi_last_idx;
    gts_state_unlock();

    apply_and_connect(first);
    return ESP_OK;
}

void wifi_manager_connect(uint8_t idx)
{
    if (!s_started || idx >= GTS_WIFI_NET_COUNT) return;
    s_manual_stop = true;            /* 끊김 이벤트로 재시도하지 않도록 */
    esp_wifi_disconnect();
    apply_and_connect(idx);          /* 여기서 s_manual_stop 을 되돌린다 */
}

void wifi_manager_disconnect(void)
{
    if (!s_started) return;
    s_manual_stop = true;
    esp_wifi_disconnect();
    set_state(GTS_WIFI_DISCONNECTED);
}

void wifi_manager_scan(void)
{
    if (!s_started) return;

    gts_state_lock();
    bool busy = g_gts.wifi_scanning;
    if (!busy) g_gts.wifi_scanning = true;
    gts_state_unlock();
    if (busy) return;

    gts_state_mark_dirty();

    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, false) != ESP_OK) {
        gts_state_lock();
        g_gts.wifi_scanning = false;
        gts_state_unlock();
        gts_state_mark_dirty();
    }
}

bool wifi_manager_is_connected(void)
{
    gts_state_lock();
    bool c = (g_gts.wifi_state == GTS_WIFI_CONNECTED);
    gts_state_unlock();
    return c;
}

int8_t wifi_manager_get_rssi(void)
{
    wifi_ap_record_t ap;
    return (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
}
