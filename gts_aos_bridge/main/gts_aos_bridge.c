/*
 * gts_aos_bridge.c — AOS (FAIMs Twin) ESP32 게이트웨이
 *
 *  ┌──────────────┐  UART2 38400  ┌────────┐   WiFi/UDP 5500   ┌────────┐
 *  │ STM32 H753    │◀─────────────▶│ ESP32  │◀─────────────────▶│ 서버    │
 *  │ HV/Frq/Duty/CV│ 0x2A/0x52/0x02│        │ 0x40~0x4F         │        │
 *  │ LF Modulator  │ 2byte SIZE +CS│        │ 2byte SIZE +CRC16 └────────┘
 *  └──────────────┘                └────────┘
 *
 * 이번 범위는 콘솔 P4 (AOS Manual) 의 7가지다.
 *   HV · FRQ · DUTY · CV · LF On/Off · LF_FRQ · LF_VOLT
 * 측정(Twin scan / heatmap)은 범위 밖이다.
 *
 * 역할 분담
 *   aos_proto.c : UDP 프레임 조립/해석 (GFC 와 동일 규격, 순수 함수)
 *   aos_uart.c  : STM32 UART 게이트웨이 (38400, SIZE 2byte)
 *   aos_ctrl.c  : 파라미터 섀도 + 합치기 + 주기 조회 + LED
 *   이 파일     : Wi-Fi, 소켓, 명령 디스패치, 주기 상향
 */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "aos_config.h"
#include "aos_proto.h"
#include "aos_uart.h"
#include "aos_ctrl.h"

static const char *TAG = "GTS_AOS";

static int                s_sock = -1;
static struct sockaddr_in s_dest;
static uint16_t           s_seq;
static bool               s_wifi_up;
static char               s_ip[16] = "0.0.0.0";
static int64_t            s_last_srv_us;

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* ════════════════════════════════════════════════════════════════════
 * Wi-Fi
 * ════════════════════════════════════════════════════════════════════ */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_up = false;
        aos_ctrl_set_link(AOS_LINK_NO_WIFI);
        ESP_LOGW(TAG, "WiFi disconnected. Reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_wifi_up = true;
        aos_ctrl_set_link(AOS_LINK_NO_SERVER);
        ESP_LOGI(TAG, "WiFi connected. IP=%s", s_ip);
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    /* 보드마다 다른 hostname. 기본값은 모두 "espressif" 라서, 공유기가
     * hostname 으로 DHCP 리스를 관리하면 두 보드가 같은 IP 를 받는다. */
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, AOS_HOSTNAME));

#if AOS_USE_STATIC_IP
    /* DHCP 를 끄고 고정 IP. 충돌을 확실히 없애는 방법이다. */
    esp_netif_dhcpc_stop(netif);

    esp_netif_ip_info_t ipi = {0};
    esp_netif_str_to_ip4(AOS_STATIC_IP,   &ipi.ip);
    esp_netif_str_to_ip4(AOS_STATIC_MASK, &ipi.netmask);
    esp_netif_str_to_ip4(AOS_STATIC_GW,   &ipi.gw);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ipi));

    esp_netif_dns_info_t dns = {0};
    esp_netif_str_to_ip4(AOS_STATIC_DNS, &dns.ip.u_addr.ip4);
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);

    ESP_LOGI(TAG, "static IP AOS_STATIC_IP=%s", AOS_STATIC_IP);
#endif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wc = {
        .sta = { .ssid = AOS_WIFI_SSID, .password = AOS_WIFI_PASS },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        ESP_LOGI(TAG, "MAC %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    ESP_LOGI(TAG, "WiFi init done (SSID=%s hostname=%s %s)",
             AOS_WIFI_SSID, AOS_HOSTNAME,
             AOS_USE_STATIC_IP ? "static" : "dhcp");
}

/* ════════════════════════════════════════════════════════════════════
 * 송신
 * ════════════════════════════════════════════════════════════════════ */

static bool udp_send(uint8_t cmd, uint16_t seq,
                     const void *payload, uint16_t len)
{
    if (s_sock < 0) return false;

    uint8_t f[AOS_MAX_FRAME];
    int n = aos_frame_build(f, sizeof(f), AOS_DTYPE, AOS_DID, cmd, seq,
                            payload, len);
    if (n < 0) {
        ESP_LOGE(TAG, "build fail cmd=0x%02X len=%u", cmd, len);
        return false;
    }

    int e = sendto(s_sock, f, (size_t)n, 0,
                   (struct sockaddr *)&s_dest, sizeof(s_dest));
    if (e < 0) {
        ESP_LOGE(TAG, "sendto cmd=0x%02X errno=%d", cmd, errno);
        return false;
    }
    return true;
}

static bool udp_push(uint8_t cmd, const void *payload, uint16_t len)
{
    return udp_send(cmd, ++s_seq, payload, len);
}

static void send_ack(uint8_t ack_cmd, uint8_t result, uint16_t ack_seq)
{
    aos_ack_t a = { .ack_cmd = ack_cmd, .result = result, .ack_seq = ack_seq };
    udp_send(AOS_CMD_ACK, ack_seq, &a, sizeof(a));
}

static void send_hello(void)
{
    aos_hello_t h;
    memset(&h, 0, sizeof(h));
    snprintf(h.model, sizeof(h.model), "GTS-AOS");
    snprintf(h.fw,    sizeof(h.fw),    "0.1.0");
    esp_read_mac(h.mac, ESP_MAC_WIFI_STA);
    h.dtype  = AOS_DTYPE;
    h.did    = AOS_DID;
    h.uptime = (uint32_t)(now_ms() / 1000);
    snprintf(h.ip, sizeof(h.ip), "%s", s_ip);
    udp_push(AOS_CMD_HELLO, &h, sizeof(h));
}

/**
 * 서버에 올릴 파라미터.
 *
 * STM32 가 0x02 응답을 준 적이 있으면 **장비가 실제로 들고 있는 값**을 싣는다.
 * 없으면(보드 미연결·부팅 직후) 섀도를 싣는다. 콘솔이 화면에 띄우는 값이
 * 실제와 갈라지는 걸 서버가 볼 수 있어야 한다.
 */
static void fill_params(aos_params_t *p)
{
    aos_uart_state_t u;
    aos_uart_get(&u);

    aos_ctrl_fill_params(p);            /* 섀도를 먼저 채우고 */

    if (u.have_set && u.link_ok) {      /* 장비 값으로 덮어쓴다 */
        p->hv       = u.hv;
        p->frq      = u.frq;
        p->duty     = u.duty;
        p->cv       = u.cv;
        p->lf_frq   = u.lf_frq;
        p->lf_volt  = u.lf_amp;
        p->lf_on    = u.lf_on;
        p->lf_shape = aos_shape_from_stm32(u.lf_type);  /* 번호 체계가 다르다 */
    }
}

static void send_params(uint16_t seq)
{
    aos_params_t p;
    fill_params(&p);
    if (seq) udp_send(AOS_CMD_PARAMS, seq, &p, sizeof(p));
    else     udp_push(AOS_CMD_PARAMS, &p, sizeof(p));
}

/* ── 전류값 살아 있음 판단 + 자동 상태 송신 재요청 (2026-09-23) ────────────
 * 서버가 "0 이 온 것" 과 "안 온 것" 을 구별할 수 있도록 STATUS err 에 표시한다.   */
static uint32_t s_cur_seen, s_busy_seen;
static int64_t  s_cur_t, s_busy_t, s_auto_t;
static int64_t  s_auto_gap = AOS_AUTO_STATUS_RETRY_MS;

static bool current_fresh(const aos_uart_state_t *u)
{
    int64_t t = now_ms();
    if (u->cur_count != s_cur_seen) {
        s_cur_seen = u->cur_count; s_cur_t = t;
        s_auto_gap = AOS_AUTO_STATUS_RETRY_MS;          /* 살아났으니 재요청 간격 초기화 */
    }
    if (u->busy_count != s_busy_seen) { s_busy_seen = u->busy_count; s_busy_t = t; }
    bool fresh = s_cur_t && (t - s_cur_t) < AOS_CUR_STALE_MS;
#if AOS_AUTO_STATUS
    /* 한가함(측정 프레임 3초 없음) + 전류 없음 + UART 는 살아 있음 → 자동 송신이 꺼진 것 */
    bool idle = (t - s_busy_t) >= AOS_CUR_STALE_MS;
    if (!fresh && idle && u->link_ok && (t - s_auto_t) >= s_auto_gap) {
        s_auto_t = t;
        ESP_LOGW(TAG, "전류(0x03) %lld ms 없음 → 0x04 자동 상태 송신 재요청 (다음 간격 %lld ms)",
                 (long long)(s_cur_t ? t - s_cur_t : -1), (long long)s_auto_gap);
        aos_uart_auto_status(true);
        s_auto_gap *= 2;
        if (s_auto_gap > AOS_AUTO_STATUS_RETRY_MAX_MS) s_auto_gap = AOS_AUTO_STATUS_RETRY_MAX_MS;
    }
#endif
    return fresh;
}

static void send_status(void)
{
    aos_uart_state_t u;
    aos_ctrl_state_t c;
    aos_uart_get(&u);
    aos_ctrl_get(&c);

    aos_status_t s;
    memset(&s, 0, sizeof(s));
    s.uptime      = (uint32_t)(now_ms() / 1000);
    s.ts          = s.uptime;
    s.air_p       = u.adc_p;
    s.air_n       = u.adc_n;
    s.gas_p       = u.tw_adc_p;
    s.gas_n       = u.tw_adc_n;
    s.point_count = u.rx_frames;

    uint8_t fl = 0;
    if (c.lf_on)   fl |= AOS_FLAG_LF_ON;
    if (c.applied) fl |= AOS_FLAG_APPLIED;
    if (u.link_ok) fl |= AOS_FLAG_UART_OK;
    s.flags = fl;

    uint16_t err = 0;
    if (!u.link_ok)  err |= AOS_ERR_UART_LOST;
    if (!u.have_set) err |= AOS_ERR_NO_REPLY;
    if (!current_fresh(&u)) err |= AOS_ERR_NO_CURRENT;
    s.err = err;

    wifi_ap_record_t ap;
    s.rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;

    udp_push(AOS_CMD_STATUS, &s, sizeof(s));
}

/* ════════════════════════════════════════════════════════════════════
 * 수신 디스패치
 * ════════════════════════════════════════════════════════════════════ */

static void handle_frame(const aos_frame_t *f)
{
    if (f->dtype != AOS_DTYPE && f->dtype != AOS_DEV_BCAST) return;
    if (f->did   != AOS_DID   && f->did   != AOS_DEV_BCAST) return;

    s_last_srv_us = esp_timer_get_time();

    switch (f->cmd) {

    case AOS_CMD_PING:
        send_ack(f->cmd, AOS_RES_OK, f->seq);
        break;

    case AOS_CMD_DISCOVER:
        send_hello();
        break;

    case AOS_CMD_ACK:
        break;

    case AOS_CMD_PARAM_SET: {
        if (f->payload_len < sizeof(aos_param_set_t)) {
            send_ack(f->cmd, AOS_RES_BAD_SIZE, f->seq);
            break;
        }
        aos_param_set_t p;
        memcpy(&p, f->payload, sizeof(p));
        ESP_LOGI(TAG, "PARAM_SET id=%u value=%.4f", p.param_id, (double)p.value);
        if (!aos_ctrl_set_param(p.param_id, p.value)) {
            send_ack(f->cmd, AOS_RES_RANGE, f->seq);
            break;
        }
        send_ack(f->cmd, AOS_RES_OK, f->seq);
        break;
    }

    case AOS_CMD_LF_MODE: {
        if (f->payload_len < 1) { send_ack(f->cmd, AOS_RES_BAD_SIZE, f->seq); break; }
        ESP_LOGI(TAG, "LF_MODE %u", f->payload[0]);
        aos_ctrl_set_lf_mode(f->payload[0] != 0);
        send_ack(f->cmd, AOS_RES_OK, f->seq);
        break;
    }

    case AOS_CMD_LF_SHAPE: {
        if (f->payload_len < 1) { send_ack(f->cmd, AOS_RES_BAD_SIZE, f->seq); break; }
        ESP_LOGI(TAG, "LF_SHAPE %u", f->payload[0]);
        aos_ctrl_set_lf_shape(f->payload[0]);
        send_ack(f->cmd, AOS_RES_OK, f->seq);
        break;
    }

    case AOS_CMD_PARAMS_QUERY:
        send_params(f->seq);
        break;

    case AOS_CMD_SYS: {
        uint8_t op = (f->payload_len >= 1) ? f->payload[0] : 0;
        send_ack(f->cmd, AOS_RES_OK, f->seq);
        if (op == 1) {
            ESP_LOGW(TAG, "reboot by server");
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "unknown cmd 0x%02X", f->cmd);
        send_ack(f->cmd, AOS_RES_UNKNOWN_CMD, f->seq);
        break;
    }
}

/* ════════════════════════════════════════════════════════════════════
 * UDP 태스크
 * ════════════════════════════════════════════════════════════════════ */

static void udp_task(void *arg)
{
    (void)arg;

    while (!s_wifi_up) vTaskDelay(pdMS_TO_TICKS(200));

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket failed errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in local = {0};
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port        = htons(AOS_LOCAL_PORT);
    if (bind(s_sock, (struct sockaddr *)&local, sizeof(local)) < 0)
        ESP_LOGE(TAG, "bind %d failed errno=%d", AOS_LOCAL_PORT, errno);

    struct timeval tv = { .tv_sec = 0, .tv_usec = 100 * 1000 };
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&s_dest, 0, sizeof(s_dest));
    s_dest.sin_family      = AF_INET;
    s_dest.sin_port        = htons(AOS_SERVER_PORT);
    s_dest.sin_addr.s_addr = inet_addr(AOS_SERVER_IP);

    ESP_LOGI(TAG, "UDP %s:%d <- local %d  (DTYPE=%d DID=%d)",
             AOS_SERVER_IP, AOS_SERVER_PORT, AOS_LOCAL_PORT,
             AOS_DTYPE, AOS_DID);

    send_hello();
    {
        aos_event_t ev = { .ts = (uint32_t)(now_ms() / 1000),
                           .code = AOS_EV_BOOT };
        udp_push(AOS_CMD_EVENT, &ev, sizeof(ev));
    }

#if AOS_AUTO_STATUS
    aos_uart_auto_status(true);
    s_auto_t = now_ms();            /* 바로 다시 보내지 않도록 */
#endif

    int64_t t_hello  = now_ms();
    int64_t t_status = now_ms();
    uint8_t rx[AOS_MAX_FRAME];

    while (1) {
        struct sockaddr_storage from;
        socklen_t flen = sizeof(from);

        int n = recvfrom(s_sock, rx, sizeof(rx), 0,
                         (struct sockaddr *)&from, &flen);
        if (n > 0) {
            aos_frame_t f;
            if (aos_frame_parse(rx, (size_t)n, &f)) handle_frame(&f);
            else ESP_LOGW(TAG, "bad frame (%d byte)", n);
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG, "recvfrom errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        int64_t t = now_ms();

        /* 값을 STM32 로 내보낸 직후에는 주기를 기다리지 않고 바로 알린다 */
        if (aos_ctrl_take_applied()) send_params(0);

        if (t - t_status >= AOS_STATUS_PERIOD_MS) {
            t_status = t;
            send_status();
            send_params(0);
        }
        if (t - t_hello >= AOS_HELLO_PERIOD_MS) {
            t_hello = t;
            send_hello();
        }

        uint16_t code; uint32_t a;
        while (aos_ctrl_take_event(&code, &a)) {
            aos_event_t ev = { .ts = (uint32_t)(t / 1000), .code = code, .a = a };
            udp_push(AOS_CMD_EVENT, &ev, sizeof(ev));
        }

        bool srv_ok = s_last_srv_us &&
                      (t - s_last_srv_us / 1000) < (AOS_STATUS_PERIOD_MS * 5);
        aos_ctrl_set_link(!s_wifi_up ? AOS_LINK_NO_WIFI
                                     : (srv_ok ? AOS_LINK_OK : AOS_LINK_NO_SERVER));
    }
}

/* ════════════════════════════════════════════════════════════════════ */

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    aos_uart_init();
    aos_ctrl_init();
    wifi_init_sta();

    xTaskCreate(udp_task, "aos_udp", 6144, NULL, 5, NULL);
}
