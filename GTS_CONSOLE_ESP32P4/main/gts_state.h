/*
 * gts_state.h — 콘솔 전역 상태 모델
 *
 * 사양서 7절 "전역 상태" 표를 그대로 구조체로 옮긴 것이다.
 *
 * 소유권
 * ──────
 *   쓰기 : 터치 이벤트(LVGL 태스크), Jog 태스크, 네트워크 수신 태스크
 *   읽기 : LVGL 갱신 타이머
 * 여러 태스크가 쓰므로 모든 접근은 gts_state_lock()/unlock() 으로 감싼다.
 * 단, 화면 그리기는 lock 밖에서 하고 lock 안에서는 값 복사만 한다.
 * (LVGL 호출을 lock 안에서 하면 네트워크 태스크를 오래 막는다.)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gts_config.h"
#include "gts_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 화면 ID ─────────────────────────────────────────────────────── */
typedef enum {
    GTS_PAGE_DEVICE = 0,    /* P1 Device Select   */
    GTS_PAGE_GFC,           /* P2 GFC Mode        */
    GTS_PAGE_AOS_MEAS,      /* P3 AOS 가스 측정   */
    GTS_PAGE_AOS_MANUAL,    /* P4 AOS Manual      */
    GTS_PAGE_WIFI,          /* P5 WiFi 접속 List  */
    GTS_PAGE_COUNT,
} gts_page_t;

/* ── 링크 상태 ───────────────────────────────────────────────────── */
typedef enum {
    GTS_LINK_IDLE = 0,
    GTS_LINK_TESTING,
    GTS_LINK_ONLINE,
    GTS_LINK_NO_REPLY,
} gts_link_t;

/* ── Wi-Fi (P5) ──────────────────────────────────────────────────── */
typedef enum {
    GTS_WIFI_DISCONNECTED = 0,
    GTS_WIFI_CONNECTING,
    GTS_WIFI_CONNECTED,
} gts_wifi_conn_t;

/** 등록 지점 1곳. 표는 gts_config.h 에서 온다. */
typedef struct {
    const char *name;   /* 장소 이름 — "GwangGyo (1006)" */
    const char *ssid;
    const char *pass;
} gts_wifi_net_t;

extern const gts_wifi_net_t GTS_WIFI_NET[GTS_WIFI_NET_COUNT];

/* ── GFC ─────────────────────────────────────────────────────────── */
typedef enum { GTS_GFC_MANUAL = 0, GTS_GFC_AUTO = 1 } gts_gfc_mode_t;

/* Auto 화면의 두 시간 타일 = jog 대상 후보 */
typedef enum {
    GTS_GFC_SEL_START = 0,  /* Start · Pump On Time      */
    GTS_GFC_SEL_CYCLE = 1,  /* 10min Cycle · Pump On Time*/
    GTS_GFC_SEL_COUNT
} gts_gfc_sel_t;

/* ── AOS 측정 타입 ───────────────────────────────────────────────── */
typedef enum {
    GTS_AOS_PRETEST = 0,
    GTS_AOS_1HOUR   = 1,
    GTS_AOS_2HOUR   = 2,
    GTS_AOS_8HOUR   = 3,
    GTS_AOS_TYPE_COUNT
} gts_aos_type_t;

/* ── AOS Manual 6종 파라미터 ─────────────────────────────────────── */
typedef enum {
    GTS_P_HV = 0,
    GTS_P_FRQ,
    GTS_P_DUTY,
    GTS_P_CV,
    GTS_P_LF_FRQ,
    GTS_P_LF_VOLT,
    GTS_P_COUNT
} gts_param_id_t;

#define GTS_STEP_MAX 3

/** 파라미터 1종의 불변 스펙 — 범위·step 목록·표시 자리수 */
typedef struct {
    const char *name;           /* "HV"            */
    const char *name_kr;        /* "고전압"        */
    const char *unit;           /* "V"             */
    float       min;
    float       max;
    float       steps[GTS_STEP_MAX];
    uint8_t     step_count;
    uint8_t     decimals;
} gts_param_spec_t;

/** 파라미터 1종의 가변 상태 */
typedef struct {
    float   value;
    uint8_t step_idx;
} gts_param_t;

extern const gts_param_spec_t GTS_PARAM_SPEC[GTS_P_COUNT];

/* LF 파형 */
typedef enum {
    GTS_LF_SQUARE = 0, GTS_LF_SINE, GTS_LF_TRIANGLE, GTS_LF_TRAPEZOID,
    GTS_LF_SHAPE_COUNT
} gts_lf_shape_t;

extern const char *const GTS_LF_SHAPE_NAME[GTS_LF_SHAPE_COUNT];

/* ── 전역 상태 ───────────────────────────────────────────────────── */
typedef struct {
    /* 공통 */
    gts_page_t      page;
    uint8_t         my_id;              /* 1~20, 콘솔 자기 ID           */
    gts_dev_type_t  dev_type;           /* P1 에서 선택                 */
    uint8_t         dev_id;             /* 1~20                         */
    gts_link_t      link;
    uint8_t         fail_count;         /* 연속 무응답 횟수             */
    uint32_t        rtt_ms;
    /* Wi-Fi — wifi_manager.c 가 쓰고 P5 와 상단바가 읽는다 */
    gts_wifi_conn_t wifi_state;
    uint8_t         wifi_sel;                       /* P5 선택 인덱스      */
    uint8_t         wifi_conn_idx;                  /* 연결된 지점, 0xFF 없음 */
    uint8_t         wifi_last_idx;                  /* NVS 의 최근 접속 지점 */
    int64_t         wifi_last_time;                 /* NVS 의 접속 시각(epoch) */
    bool            wifi_found[GTS_WIFI_NET_COUNT]; /* 스캔에서 보였는지   */
    int8_t          wifi_rssi_list[GTS_WIFI_NET_COUNT];
    bool            wifi_scanning;
    uint8_t         wifi_scan_found;                /* 등록 지점 중 몇 개  */
    int64_t         wifi_scan_time;                 /* 마지막 스캔 시각    */
    char            wifi_ip[16];
    int8_t          wifi_rssi;                      /* 연결된 AP 의 RSSI   */

    /* P1 Connection Test 카드 */
    uint8_t         dev_state;          /* 0 IDLE 1 RUN 2 ERROR 3 OFFLINE */
    uint16_t        dev_fw_ver;
    uint32_t        dev_uptime_s;
    uint32_t        last_reply_ms;
    bool            connect_ok;         /* ENTER CONTROL 활성 조건      */

    /* P2 GFC */
    gts_gfc_mode_t  gfc_mode;
    gts_gfc_sel_t   gfc_sel;            /* jog 대상 시간 타일           */
    uint16_t        gfc_start_ds;       /* 1~600 (0.1 sec 단위)         */
    uint16_t        gfc_cycle_ds;       /* 1~600                        */
    bool            gfc_auto_run;
    bool            gfc_pump_on;
    uint16_t        gfc_remain_ds;      /* 다음 분사까지 남은 시간      */
    uint32_t        gfc_cycle_count;

    /* P3 AOS 측정 */
    gts_aos_type_t  aos_type;
    bool            aos_running;
    bool            aos_done;
    uint32_t        aos_elapsed_s;
    uint32_t        aos_total_s;
    uint32_t        aos_samples;

    /* P4 AOS Manual */
    gts_param_t     param[GTS_P_COUNT];
    gts_param_id_t  param_sel;          /* jog 대상 타일                */
    bool            lf_on;
    gts_lf_shape_t  lf_shape;
} gts_state_t;

/** 전역 상태 실체. 직접 접근하지 말고 lock/unlock 사이에서만 만질 것. */
extern gts_state_t g_gts;

/* ── 생명주기 ────────────────────────────────────────────────────── */
void gts_state_init(void);
void gts_state_lock(void);
void gts_state_unlock(void);

/** 상태가 바뀌었음을 UI 에 알린다 (LVGL 타이머가 dirty 플래그를 본다). */
void gts_state_mark_dirty(void);
/** dirty 플래그를 읽고 지운다. LVGL 타이머 전용. */
bool gts_state_take_dirty(void);

/* ── 조작 (lock 내부에서 스스로 처리) ────────────────────────────── */

/** 현재 화면의 jog 대상에 dir(±1) 만큼 step 을 적용한다. */
void gts_state_jog(int dir);

/** Jog S/W 누름 — 현재 화면의 선택 항목 step 을 순환시킨다. */
void gts_state_jog_sw(void);

/** AOS Manual 파라미터 값 clamp + 반올림. */
float gts_param_clamp(gts_param_id_t id, float v);

/** 현재 jog 대상의 표시용 정보. UI 조그바가 쓴다. */
void gts_state_jog_target_text(char *name, size_t name_cap,
                               char *value, size_t value_cap,
                               char *range, size_t range_cap,
                               char *step, size_t step_cap,
                               bool *has_step, bool *enabled);

/** 측정 타입별 총 소요시간(초). */
uint32_t gts_aos_type_seconds(gts_aos_type_t t);
const char *gts_aos_type_name(gts_aos_type_t t);
const char *gts_aos_type_duration(gts_aos_type_t t);
const char *gts_aos_type_note(gts_aos_type_t t);

#ifdef __cplusplus
}
#endif
