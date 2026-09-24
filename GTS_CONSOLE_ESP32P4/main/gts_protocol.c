/*
 * gts_protocol.c — UDP 프레임 조립/해석 + 소켓 송수신
 *
 * 규격: DOC/GTS_UDP_Protocol.md
 */
#include "gts_protocol.h"
#include "gts_config.h"
#include "gts_state.h"

#include <errno.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "GTS-NET";

static int                 s_sock = -1;
static struct sockaddr_in  s_dest;
static SemaphoreHandle_t   s_tx_lock;
static uint16_t            s_seq;

/* 장비 값을 로컬에서 만진 시각 + HOLD.
 *
 * 이 시각까지는 서버가 내려보내는 "현재 상태"를 무시하고 내 조작을 유지한다.
 *   GFC  : GFC_AUTO_STATE 의 run / pump
 *   AOS  : AOS_PARAMS 의 6개 값 + LF
 * 둘 다 같은 문제다 — 명령을 보낸 직후에는 서버가 아직 옛 값을 들고 있어서,
 * 그걸 그대로 받아 쓰면 방금 돌린 jog 가 되돌아간다.
 * (gts_config.h GTS_LOCAL_HOLD_MS)                                      */
static int64_t            s_hold_us;

static void local_hold_touch(void)
{
    s_hold_us = esp_timer_get_time() + (int64_t)GTS_LOCAL_HOLD_MS * 1000;
}

static bool local_hold_active(void)
{
    return esp_timer_get_time() < s_hold_us;
}

/* 마지막 요청 시각 — RTT 계산용. seq 별로 보관한다 (작은 원형 버퍼). */
#define TXLOG_N 16
static struct { uint16_t seq; int64_t us; } s_txlog[TXLOG_N];
static uint8_t s_txlog_w;

/* ════════════════════════════════════════════════════════════════════
 * 순수 함수 — 소켓과 무관
 * ════════════════════════════════════════════════════════════════════ */

uint16_t gts_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001)
                             : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

int gts_frame_build(uint8_t *out, size_t out_cap,
                    uint8_t type, uint8_t id, uint8_t cmd,
                    uint8_t src_type, uint8_t src_id, uint16_t seq,
                    const void *payload, uint16_t payload_len)
{
    if (!out) return -1;
    if (payload_len > GTS_MAX_PAYLOAD) return -1;

    uint16_t size  = (uint16_t)(GTS_HDR_LEN + payload_len);
    size_t   total = (size_t)GTS_FRAME_OVERHEAD + size;
    if (total > out_cap) return -1;

    size_t i = 0;
    out[i++] = GTS_STX;
    out[i++] = type;
    out[i++] = id;
    out[i++] = cmd;
    out[i++] = (uint8_t)(size & 0xFF);
    out[i++] = (uint8_t)(size >> 8);

    /* DATA 공통 헤더 */
    out[i++] = src_type;
    out[i++] = src_id;
    out[i++] = (uint8_t)(seq & 0xFF);
    out[i++] = (uint8_t)(seq >> 8);

    if (payload_len && payload) {
        memcpy(&out[i], payload, payload_len);
        i += payload_len;
    }

    /* CRC 는 STX 를 뺀 TYPE~DATA 구간 */
    uint16_t crc = gts_crc16(&out[1], i - 1);
    out[i++] = (uint8_t)(crc & 0xFF);
    out[i++] = (uint8_t)(crc >> 8);

    return (int)i;
}

bool gts_frame_parse(const uint8_t *buf, size_t len, gts_frame_t *out)
{
    if (!buf || !out) return false;
    if (len < (size_t)(GTS_FRAME_OVERHEAD + GTS_HDR_LEN)) return false;
    if (buf[0] != GTS_STX) return false;

    uint16_t size = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8));
    if (size < GTS_HDR_LEN) return false;
    if ((size_t)size + GTS_FRAME_OVERHEAD != len) return false;

    uint16_t crc_rx = (uint16_t)(buf[6 + size] | ((uint16_t)buf[7 + size] << 8));
    uint16_t crc_ca = gts_crc16(&buf[1], (size_t)(5 + size));
    if (crc_rx != crc_ca) {
        ESP_LOGW(TAG, "CRC mismatch rx=%04X calc=%04X", crc_rx, crc_ca);
        return false;
    }

    out->type        = buf[1];
    out->id          = buf[2];
    out->cmd         = buf[3];
    out->src_type    = buf[6];
    out->src_id      = buf[7];
    out->seq         = (uint16_t)(buf[8] | ((uint16_t)buf[9] << 8));
    out->payload     = &buf[6 + GTS_HDR_LEN];
    out->payload_len = (uint16_t)(size - GTS_HDR_LEN);
    return true;
}

/* ════════════════════════════════════════════════════════════════════
 * 수신 처리
 * ════════════════════════════════════════════════════════════════════ */

static inline uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline float rd_f32(const uint8_t *p)
{
    float f; uint32_t u = rd_u32(p); memcpy(&f, &u, 4); return f;
}

static void txlog_put(uint16_t seq)
{
    s_txlog[s_txlog_w].seq = seq;
    s_txlog[s_txlog_w].us  = esp_timer_get_time();
    s_txlog_w = (uint8_t)((s_txlog_w + 1) % TXLOG_N);
}

static uint32_t txlog_rtt_ms(uint16_t seq)
{
    for (int i = 0; i < TXLOG_N; i++) {
        if (s_txlog[i].seq == seq && s_txlog[i].us) {
            int64_t dt = esp_timer_get_time() - s_txlog[i].us;
            s_txlog[i].us = 0;
            return (uint32_t)(dt / 1000);
        }
    }
    return 0;
}

/** 링크 성공 신호 — 무응답 카운터를 되돌린다. */
static void link_alive(uint16_t seq)
{
    uint32_t rtt = txlog_rtt_ms(seq);
    gts_state_lock();
    g_gts.fail_count = 0;
    if (rtt) g_gts.rtt_ms = rtt;
    if (g_gts.link != GTS_LINK_ONLINE && g_gts.connect_ok)
        g_gts.link = GTS_LINK_ONLINE;
    gts_state_unlock();
}

static void handle_frame(const gts_frame_t *f)
{
    switch (f->cmd) {

    case GTS_CMD_CONNECT_ACK:
    case GTS_CMD_STATUS_RESP: {
        if (f->payload_len < 12) break;
        uint8_t  result   = f->payload[0];
        uint8_t  state    = f->payload[1];
        uint16_t fw       = rd_u16(&f->payload[2]);
        uint32_t uptime   = rd_u32(&f->payload[4]);
        uint32_t lastrep  = rd_u32(&f->payload[8]);

        gts_state_lock();
        g_gts.dev_state     = state;
        g_gts.dev_fw_ver    = fw;
        g_gts.dev_uptime_s  = uptime;
        g_gts.last_reply_ms = lastrep;
        g_gts.connect_ok    = (result == 0);
        g_gts.link          = (result == 0) ? GTS_LINK_ONLINE : GTS_LINK_NO_REPLY;
        g_gts.fail_count    = 0;
        gts_state_unlock();

        link_alive(f->seq);
        ESP_LOGI(TAG, "CONNECT_ACK result=%u state=%u fw=%04X", result, state, fw);
        break;
    }

    case GTS_CMD_DISCONNECT_ACK:
        gts_state_lock();
        g_gts.connect_ok = false;
        g_gts.link       = GTS_LINK_IDLE;
        gts_state_unlock();
        break;

    case GTS_CMD_PONG:
        link_alive(f->seq);
        if (f->payload_len >= 1) {
            gts_state_lock();
            g_gts.dev_state = f->payload[0];
            gts_state_unlock();
        }
        break;

    case GTS_CMD_GFC_AUTO_STATE: {
        if (f->payload_len < 8) break;
        bool hold = local_hold_active();    /* 방금 내가 누른 게 이긴다 */
        gts_state_lock();
        if (!hold) {
            g_gts.gfc_auto_run = (f->payload[0] != 0);
            g_gts.gfc_pump_on  = (f->payload[1] != 0);
        }
        g_gts.gfc_remain_ds   = rd_u16(&f->payload[2]);
        g_gts.gfc_cycle_count = rd_u32(&f->payload[4]);
        g_gts.fail_count      = 0;
        gts_state_unlock();
        break;
    }

    case GTS_CMD_AOS_MEAS_STATE: {
        if (f->payload_len < 16) break;
        gts_state_lock();
        g_gts.aos_running   = (f->payload[0] != 0);
        g_gts.aos_type      = (gts_aos_type_t)f->payload[1];
        g_gts.aos_done      = (f->payload[2] != 0);
        g_gts.aos_elapsed_s = rd_u32(&f->payload[4]);
        g_gts.aos_total_s   = rd_u32(&f->payload[8]);
        g_gts.aos_samples   = rd_u32(&f->payload[12]);
        if (g_gts.aos_done) g_gts.aos_running = false;   /* 자동 Stop */
        g_gts.fail_count    = 0;
        gts_state_unlock();
        break;
    }

    case GTS_CMD_AOS_PARAMS: {
        if (f->payload_len < 26) break;
        /* jog 를 돌리는 중이면 서버가 보낸 옛 값으로 덮어쓰지 않는다.
         * P4 진입 시의 AOS_GET_PARAMS 응답은 hold 가 걸려 있지 않아
         * 정상적으로 반영된다.                                         */
        if (local_hold_active()) { link_alive(f->seq); break; }
        gts_state_lock();
        g_gts.param[GTS_P_HV].value      = rd_f32(&f->payload[0]);
        g_gts.param[GTS_P_FRQ].value     = rd_f32(&f->payload[4]);
        g_gts.param[GTS_P_DUTY].value    = rd_f32(&f->payload[8]);
        g_gts.param[GTS_P_CV].value      = rd_f32(&f->payload[12]);
        g_gts.param[GTS_P_LF_FRQ].value  = rd_f32(&f->payload[16]);
        g_gts.param[GTS_P_LF_VOLT].value = rd_f32(&f->payload[20]);
        g_gts.lf_on    = (f->payload[24] != 0);
        g_gts.lf_shape = (gts_lf_shape_t)(f->payload[25] % GTS_LF_SHAPE_COUNT);
        for (int i = 0; i < GTS_P_COUNT; i++)
            g_gts.param[i].value = gts_param_clamp((gts_param_id_t)i,
                                                   g_gts.param[i].value);
        gts_state_unlock();
        link_alive(f->seq);
        break;
    }

    case GTS_CMD_ACK:
        link_alive(f->seq);
        break;

    case GTS_CMD_NAK:
        if (f->payload_len >= 2)
            ESP_LOGW(TAG, "NAK cmd=0x%02X err=%u", f->payload[0], f->payload[1]);
        link_alive(f->seq);
        break;

    default:
        ESP_LOGD(TAG, "unhandled cmd 0x%02X", f->cmd);
        break;
    }

    gts_state_mark_dirty();
}

/* ════════════════════════════════════════════════════════════════════
 * 송신
 * ════════════════════════════════════════════════════════════════════ */

bool gts_proto_send_to(uint8_t type, uint8_t id,
                       uint8_t cmd, const void *payload, uint16_t payload_len)
{
    if (s_sock < 0) return false;

    uint8_t frame[GTS_MAX_FRAME];
    bool ok = false;

    xSemaphoreTake(s_tx_lock, portMAX_DELAY);

    uint16_t seq = ++s_seq;
    uint8_t  my  = GTS_MY_ID_DEFAULT;

    gts_state_lock();
    my = g_gts.my_id;
    gts_state_unlock();

    int n = gts_frame_build(frame, sizeof(frame), type, id, cmd,
                            GTS_DEV_CONSOLE, my, seq, payload, payload_len);
    if (n > 0) {
        txlog_put(seq);
        int err = sendto(s_sock, frame, (size_t)n, 0,
                         (struct sockaddr *)&s_dest, sizeof(s_dest));
        if (err < 0) {
            ESP_LOGE(TAG, "sendto cmd=0x%02X failed: errno %d", cmd, errno);
        } else {
            ok = true;
            ESP_LOGD(TAG, "TX cmd=0x%02X seq=%u len=%d", cmd, seq, n);
        }
    } else {
        ESP_LOGE(TAG, "frame build failed cmd=0x%02X len=%u", cmd, payload_len);
    }

    xSemaphoreGive(s_tx_lock);
    return ok;
}

bool gts_proto_send(uint8_t cmd, const void *payload, uint16_t payload_len)
{
    uint8_t type, id;
    gts_state_lock();
    type = (uint8_t)g_gts.dev_type;
    id   = g_gts.dev_id;
    gts_state_unlock();
    return gts_proto_send_to(type, id, cmd, payload, payload_len);
}

/* ── 상위 레벨 헬퍼 ──────────────────────────────────────────────── */

bool gts_proto_connect(uint8_t type, uint8_t id)
{
    gts_state_lock();
    g_gts.link       = GTS_LINK_TESTING;
    g_gts.connect_ok = false;
    gts_state_unlock();
    gts_state_mark_dirty();

    bool sent = gts_proto_send_to(type, id, GTS_CMD_CONNECT, NULL, 0);

#if GTS_OFFLINE_MODE
    /* 서버 송신이 아직 없다. CONNECT_ACK 을 받은 것처럼 처리해
     * ENTER CONTROL 을 열어 준다 (화면 검증용).                       */
    gts_state_lock();
    g_gts.connect_ok    = true;
    g_gts.link          = GTS_LINK_ONLINE;
    g_gts.fail_count    = 0;
    g_gts.dev_state     = 0;        /* IDLE */
    g_gts.dev_fw_ver    = 0x0100;
    g_gts.dev_uptime_s  = 0;
    g_gts.last_reply_ms = 0;
    g_gts.rtt_ms        = 0;
    gts_state_unlock();
    gts_state_mark_dirty();
    ESP_LOGW(TAG, "OFFLINE MODE — CONNECT_ACK 없이 연결된 것으로 처리");
#endif

    return sent;
}

bool gts_proto_disconnect(void) { return gts_proto_send(GTS_CMD_DISCONNECT, NULL, 0); }
bool gts_proto_ping(void)       { return gts_proto_send(GTS_CMD_PING, NULL, 0); }

bool gts_proto_gfc_set_mode(uint8_t mode)
{
    local_hold_touch();
    return gts_proto_send(GTS_CMD_GFC_SET_MODE, &mode, 1);
}

bool gts_proto_gfc_set_pump(bool on)
{
    uint8_t v = on ? 1 : 0;
    local_hold_touch();
    return gts_proto_send(GTS_CMD_GFC_SET_PUMP, &v, 1);
}

bool gts_proto_gfc_set_times(uint16_t start_ds, uint16_t cycle_ds)
{
    uint8_t p[4] = {
        (uint8_t)(start_ds & 0xFF), (uint8_t)(start_ds >> 8),
        (uint8_t)(cycle_ds & 0xFF), (uint8_t)(cycle_ds >> 8),
    };
    return gts_proto_send(GTS_CMD_GFC_SET_TIMES, p, sizeof(p));
}

bool gts_proto_gfc_auto_run(bool run)
{
    uint8_t v = run ? 1 : 0;
    local_hold_touch();
    return gts_proto_send(GTS_CMD_GFC_AUTO_RUN, &v, 1);
}

bool gts_proto_aos_set_type(uint8_t type)
{
    return gts_proto_send(GTS_CMD_AOS_SET_TYPE, &type, 1);
}

bool gts_proto_aos_meas_run(bool run)
{
    uint8_t v = run ? 1 : 0;
    return gts_proto_send(GTS_CMD_AOS_MEAS_RUN, &v, 1);
}

bool gts_proto_aos_set_param(uint8_t param_id, float value)
{
    uint8_t p[8] = { param_id, 0, 0, 0, 0, 0, 0, 0 };
    memcpy(&p[4], &value, 4);   /* IEEE-754 LE */
    local_hold_touch();
    return gts_proto_send(GTS_CMD_AOS_SET_PARAM, p, sizeof(p));
}

bool gts_proto_aos_set_lf_mode(bool on)
{
    uint8_t v = on ? 1 : 0;
    local_hold_touch();
    return gts_proto_send(GTS_CMD_AOS_SET_LF_MODE, &v, 1);
}

bool gts_proto_aos_set_lf_shape(uint8_t shape)
{
    local_hold_touch();
    return gts_proto_send(GTS_CMD_AOS_SET_LF_SHAPE, &shape, 1);
}

bool gts_proto_aos_get_params(void)
{
    return gts_proto_send(GTS_CMD_AOS_GET_PARAMS, NULL, 0);
}

/* ════════════════════════════════════════════════════════════════════
 * 수신 태스크
 * ════════════════════════════════════════════════════════════════════ */

static void gts_net_task(void *arg)
{
    (void)arg;
    uint8_t rx[GTS_MAX_FRAME];

    while (1) {
        struct sockaddr_storage from;
        socklen_t fromlen = sizeof(from);

        int n = recvfrom(s_sock, rx, sizeof(rx), 0,
                         (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        gts_frame_t f;
        if (!gts_frame_parse(rx, (size_t)n, &f)) {
            ESP_LOGW(TAG, "bad frame (%d byte)", n);
            continue;
        }
        handle_frame(&f);
    }
}

/* ── keep-alive + 링크 감시 태스크 ───────────────────────────────── */
static void gts_ping_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(GTS_PING_PERIOD_MS));

        bool     need_ping;
        uint8_t  fails;

        gts_state_lock();
        need_ping = (g_gts.link == GTS_LINK_ONLINE || g_gts.link == GTS_LINK_TESTING);
        if (need_ping) g_gts.fail_count++;      /* 응답이 오면 0 으로 리셋됨 */
        fails = g_gts.fail_count;
        gts_state_unlock();

        if (!need_ping) continue;

#if GTS_OFFLINE_MODE
        /* 응답이 없는 게 정상이므로 링크 끊김 판정을 하지 않는다. */
        gts_state_lock();
        g_gts.fail_count = 0;
        gts_state_unlock();
        gts_proto_ping();       /* 서버 수신부 확인용으로 계속 보낸다 */
        continue;
#endif

        if (fails > GTS_LINK_FAIL_COUNT) {
            /* 연속 무응답 → 링크 끊김. 사양서 7절: P1 복귀 + NO REPLY */
            gts_state_lock();
            g_gts.link        = GTS_LINK_NO_REPLY;
            g_gts.connect_ok  = false;
            g_gts.aos_running = false;
            g_gts.gfc_auto_run = false;
            g_gts.gfc_pump_on  = false;
            g_gts.page        = GTS_PAGE_DEVICE;
            gts_state_unlock();
            gts_state_mark_dirty();
            ESP_LOGW(TAG, "link lost — back to device select");
            continue;
        }

        gts_proto_ping();
    }
}

bool gts_proto_start(void)
{
    if (!s_tx_lock) s_tx_lock = xSemaphoreCreateMutex();

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket failed: errno %d", errno);
        return false;
    }

    /* 수신 타임아웃을 둬 태스크가 영원히 블록되지 않게 한다. */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&s_dest, 0, sizeof(s_dest));
    s_dest.sin_family = AF_INET;
    s_dest.sin_port   = htons(GTS_UDP_SERVER_PORT);
    inet_pton(AF_INET, GTS_UDP_SERVER_IP, &s_dest.sin_addr);

    ESP_LOGI(TAG, "UDP ready -> %s:%d", GTS_UDP_SERVER_IP, GTS_UDP_SERVER_PORT);

    xTaskCreate(gts_net_task,  "gts_net",  GTS_NET_TASK_STACK, NULL,
                GTS_NET_TASK_PRIO, NULL);
    xTaskCreate(gts_ping_task, "gts_ping", 3072, NULL,
                GTS_NET_TASK_PRIO - 1, NULL);
    return true;
}
