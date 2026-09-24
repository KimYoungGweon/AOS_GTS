/*
 * gts_gfc_udp.c — GFC (Gas Flow Controller) ESP32 게이트웨이
 *
 *   ┌──────────────┐  UART2 115200  ┌────────┐   WiFi/UDP 5501   ┌────────┐
 *   │ STM32 보드    │◀──────────────▶│ ESP32  │◀─────────────────▶│ 서버    │
 *   │ Pump 3ch      │ 0x70/71/72     │        │ 0x30~0x3A         │        │
 *   │ TVOC 2ch ADC  │ 1byte + CS     │        │ 2byte + CRC16     │        │
 *   └──────────────┘                 └────────┘                   └────────┘
 *
 * 규격: DOC/GTS_GFC_UDP.md
 * 역할 분담
 *   gfc_proto.c : UDP 프레임 조립/해석 (순수 함수)
 *   gfc_uart.c  : STM32 UART 게이트웨이 + 자동송신 감지/폴링
 *   gfc_ctrl.c  : 주입 시퀀스 + LED
 *   이 파일     : Wi-Fi, 소켓, 명령 디스패치, 주기 상향 송신
 *
 * 소켓은 로컬 포트를 5501 로 고정(bind)한다. 서버는 상향 패킷을 기다릴
 * 필요 없이 (GFC IP, 5501) 로 언제든 하향 명령을 보낼 수 있다.
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

#include "gfc_config.h"
#include "gfc_proto.h"
#include "gfc_uart.h"
#include "gfc_ctrl.h"

static const char *TAG = "GTS_GFC";

static int                s_sock = -1;
static struct sockaddr_in s_dest;
static uint16_t           s_seq;
static bool               s_wifi_up;
static char               s_ip[16] = "0.0.0.0";
static int64_t            s_last_srv_us;      /* 서버에서 마지막으로 받은 시각 */

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
        gfc_ctrl_set_link(GFC_LINK_NO_WIFI);
        ESP_LOGW(TAG, "WiFi disconnected. Reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_wifi_up = true;
        gfc_ctrl_set_link(GFC_LINK_NO_SERVER);
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
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, GFC_HOSTNAME));

#if GFC_USE_STATIC_IP
    /* DHCP 를 끄고 고정 IP. 충돌을 확실히 없애는 방법이다. */
    esp_netif_dhcpc_stop(netif);

    esp_netif_ip_info_t ipi = {0};
    esp_netif_str_to_ip4(GFC_STATIC_IP,   &ipi.ip);
    esp_netif_str_to_ip4(GFC_STATIC_MASK, &ipi.netmask);
    esp_netif_str_to_ip4(GFC_STATIC_GW,   &ipi.gw);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ipi));

    esp_netif_dns_info_t dns = {0};
    esp_netif_str_to_ip4(GFC_STATIC_DNS, &dns.ip.u_addr.ip4);
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);

    ESP_LOGI(TAG, "static IP GFC_STATIC_IP=%s", GFC_STATIC_IP);
#endif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wc = {
        .sta = { .ssid = GFC_WIFI_SSID, .password = GFC_WIFI_PASS },
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
             GFC_WIFI_SSID, GFC_HOSTNAME,
             GFC_USE_STATIC_IP ? "static" : "dhcp");
}

/* ════════════════════════════════════════════════════════════════════
 * 송신
 * ════════════════════════════════════════════════════════════════════ */

static bool udp_send(uint8_t cmd, uint16_t seq,
                     const void *payload, uint16_t len)
{
    if (s_sock < 0) return false;

    uint8_t f[GFC_MAX_FRAME];
    int n = gfc_frame_build(f, sizeof(f), GFC_DTYPE, GFC_DID, cmd, seq,
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
    ESP_LOGD(TAG, "TX cmd=0x%02X seq=%u len=%d", cmd, seq, n);
    return true;
}

/** 상향 자발 송신 — 우리 쪽 seq 를 붙인다. */
static bool udp_push(uint8_t cmd, const void *payload, uint16_t len)
{
    return udp_send(cmd, ++s_seq, payload, len);
}

static void send_ack(uint8_t ack_cmd, uint8_t result, uint16_t ack_seq)
{
    gfc_ack_t a = { .ack_cmd = ack_cmd, .result = result, .ack_seq = ack_seq };
    udp_send(GFC_CMD_ACK, ack_seq, &a, sizeof(a));
}

static void send_hello(void)
{
    gfc_hello_t h;
    memset(&h, 0, sizeof(h));
    snprintf(h.model, sizeof(h.model), "GTS-GFC");
    snprintf(h.fw,    sizeof(h.fw),    "0.1.0");
    esp_read_mac(h.mac, ESP_MAC_WIFI_STA);
    h.dtype  = GFC_DTYPE;
    h.did    = GFC_DID;
    h.uptime = (uint32_t)(now_ms() / 1000);
    snprintf(h.ip, sizeof(h.ip), "%s", s_ip);
    udp_push(GFC_CMD_HELLO, &h, sizeof(h));
}

static void fill_sensor(gfc_sensor_data_t *s)
{
    gfc_uart_state_t u;
    gfc_ctrl_state_t c;
    gfc_uart_get(&u);
    gfc_ctrl_get(&c);

    memset(s, 0, sizeof(*s));
    s->uptime      = (uint32_t)(now_ms() / 1000);
    s->ts          = s->uptime;             /* NTP 붙기 전까지는 uptime */
    s->raw1        = u.raw1;
    s->raw2        = u.raw2;
    s->co2_1       = u.co2_1;
    s->co2_2       = u.co2_2;
    s->volt1       = gfc_uart_raw_to_volt(u.raw1);
    s->volt2       = gfc_uart_raw_to_volt(u.raw2);
    s->src_remain  = c.remain_s;
    s->src_elapsed = c.elapsed_s;

    /* 펌프는 제어기가 의도한 값이 아니라 STM32 가 보고한 값을 싣는다.
     * (UART 가 끊기면 의도와 실제가 갈라지는 걸 서버가 봐야 한다)     */
    s->pump1 = u.link_ok ? u.pump1 : c.pump1;
    s->pump2 = u.link_ok ? u.pump2 : c.pump2;
    s->pump3 = u.link_ok ? u.pump3 : c.pump3;

    uint8_t fl = 0;
    if (c.enable)    fl |= GFC_FLAG_SRC_ENABLE;
    if (c.on)        fl |= GFC_FLAG_SRC_ON;
    if (c.in_init)   fl |= GFC_FLAG_SRC_INIT;   /* 서버가 주기 분사만 세도록 */
    if (u.link_ok)   fl |= GFC_FLAG_UART_OK;
    if (u.auto_push) fl |= GFC_FLAG_AUTO_PUSH;
    s->flags = fl;

    uint16_t err = 0;
    if (u.poll_mode) err |= GFC_ERR_POLL_MODE;
    if (!u.link_ok)  err |= GFC_ERR_UART_LOST;
    s->err = err;

    wifi_ap_record_t ap;
    s->rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
}

static void send_sensor(uint16_t seq)
{
    gfc_sensor_data_t s;
    fill_sensor(&s);
    if (seq) udp_send(GFC_CMD_SENSOR_DATA, seq, &s, sizeof(s));
    else     udp_push(GFC_CMD_SENSOR_DATA, &s, sizeof(s));
}

/* ════════════════════════════════════════════════════════════════════
 * 수신 디스패치
 * ════════════════════════════════════════════════════════════════════ */

static void handle_frame(const gfc_frame_t *f)
{
    /* 우리 앞으로 온 것만 처리 */
    if (f->dtype != GFC_DTYPE && f->dtype != GFC_DEV_BCAST) return;
    if (f->did   != GFC_DID   && f->did   != GFC_DEV_BCAST) return;

    s_last_srv_us = esp_timer_get_time();
    ESP_LOGI(TAG, "RX cmd=0x%02X seq=%u size=%u", f->cmd, f->seq, f->payload_len);

    switch (f->cmd) {

    case GFC_CMD_PING:
        send_ack(f->cmd, GFC_RES_OK, f->seq);
        break;

    case GFC_CMD_DISCOVER:
        send_hello();
        break;

    case GFC_CMD_ACK:                       /* 서버가 우리 상향을 확인 */
        break;

    case GFC_CMD_PUMP_SET: {
        if (f->payload_len < sizeof(gfc_pump_set_t)) {
            send_ack(f->cmd, GFC_RES_BAD_SIZE, f->seq);
            break;
        }
        gfc_pump_set_t p;
        memcpy(&p, f->payload, sizeof(p));
        gfc_ctrl_pump_set(p.pump1, p.pump2, p.pump3);
        send_ack(f->cmd, GFC_RES_OK, f->seq);
        send_sensor(0);                     /* 바뀐 상태를 바로 알린다 */
        break;
    }

    case GFC_CMD_PUMP_QUERY: {
        gfc_uart_state_t u;
        gfc_uart_get(&u);
        gfc_uart_pump_query();              /* STM32 에도 실제 조회 */
        gfc_pump_set_t p = { u.pump1, u.pump2, u.pump3, 0 };
        udp_send(GFC_CMD_PUMP_QUERY, f->seq, &p, sizeof(p));
        break;
    }

    case GFC_CMD_SENSOR_DATA:               /* 요청(SIZE=0) → 즉시 응답 */
        send_sensor(f->seq);
        break;

    case GFC_CMD_SRC_SET: {
        if (f->payload_len < sizeof(gfc_src_set_t)) {
            send_ack(f->cmd, GFC_RES_BAD_SIZE, f->seq);
            break;
        }
        gfc_src_set_t c;
        memcpy(&c, f->payload, sizeof(c));
        gfc_ctrl_src_set(&c);
        send_ack(f->cmd, GFC_RES_OK, f->seq);
        send_sensor(0);
        break;
    }

    case GFC_CMD_CFG_QUERY: {               /* 현재 주입 설정을 돌려준다 */
        gfc_ctrl_state_t c;
        gfc_ctrl_get(&c);
        gfc_src_set_t cfg = c.cfg;
        cfg.enable = c.enable ? 1 : 0;
        udp_send(GFC_CMD_SRC_SET, f->seq, &cfg, sizeof(cfg));
        break;
    }

    case GFC_CMD_SYS: {
        uint8_t op = (f->payload_len >= 1) ? f->payload[0] : 0;
        send_ack(f->cmd, GFC_RES_OK, f->seq);
        if (op == 1) {                      /* 1 = reboot */
            ESP_LOGW(TAG, "reboot by server");
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "unknown cmd 0x%02X", f->cmd);
        send_ack(f->cmd, GFC_RES_UNKNOWN_CMD, f->seq);
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
    local.sin_port        = htons(GFC_LOCAL_PORT);
    if (bind(s_sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        ESP_LOGE(TAG, "bind %d failed errno=%d", GFC_LOCAL_PORT, errno);
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = 100 * 1000 };
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&s_dest, 0, sizeof(s_dest));
    s_dest.sin_family = AF_INET;
    s_dest.sin_port   = htons(GFC_SERVER_PORT);
    s_dest.sin_addr.s_addr = inet_addr(GFC_SERVER_IP);

    ESP_LOGI(TAG, "UDP %s:%d <- local %d  (DTYPE=%d DID=%d)",
             GFC_SERVER_IP, GFC_SERVER_PORT, GFC_LOCAL_PORT,
             GFC_DTYPE, GFC_DID);

    send_hello();
    {
        gfc_event_t ev = { .ts = (uint32_t)(now_ms() / 1000),
                           .code = GFC_EV_BOOT };
        udp_push(GFC_CMD_EVENT, &ev, sizeof(ev));
    }

    int64_t t_hello = now_ms();
    int64_t t_sens  = now_ms();
    uint8_t rx[GFC_MAX_FRAME];

    while (1) {
        struct sockaddr_storage from;
        socklen_t flen = sizeof(from);

        int n = recvfrom(s_sock, rx, sizeof(rx), 0,
                         (struct sockaddr *)&from, &flen);
        if (n > 0) {
            gfc_frame_t f;
            if (gfc_frame_parse(rx, (size_t)n, &f)) handle_frame(&f);
            else ESP_LOGW(TAG, "bad frame (%d byte)", n);
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG, "recvfrom errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        int64_t t = now_ms();

        /* 주기 상향 */
        if (t - t_sens >= GFC_SENSOR_PERIOD_MS) {
            t_sens = t;
            send_sensor(0);
        }
        if (t - t_hello >= GFC_HELLO_PERIOD_MS) {
            t_hello = t;
            send_hello();
        }

        /* 이벤트 배출 — SRC_ON/OFF 가 서버의 구형파 재구성 근거다 */
        uint16_t code; uint32_t a;
        while (gfc_ctrl_take_event(&code, &a)) {
            gfc_event_t ev = { .ts = (uint32_t)(t / 1000), .code = code, .a = a };
            udp_push(GFC_CMD_EVENT, &ev, sizeof(ev));
        }

        /* 서버 링크 판정 — 상향 3주기 동안 아무 응답도 없으면 무응답 */
        bool srv_ok = s_last_srv_us &&
                      (t - s_last_srv_us / 1000) < (GFC_SENSOR_PERIOD_MS * 5);
        gfc_ctrl_set_link(!s_wifi_up ? GFC_LINK_NO_WIFI
                                     : (srv_ok ? GFC_LINK_OK : GFC_LINK_NO_SERVER));
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

    gfc_uart_init();
    gfc_ctrl_init();
    wifi_init_sta();

    xTaskCreate(udp_task, "gfc_udp", 6144, NULL, 5, NULL);
}
