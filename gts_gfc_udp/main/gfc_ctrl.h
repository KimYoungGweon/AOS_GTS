/*
 * gfc_ctrl.h — 주입(Source) 제어 시퀀스 + LED 표시
 *
 * 규격: DOC/GTS_GFC_UDP.md 6.4(src_set_t) / 7.1(LED)
 *
 * 제어 루프는 네트워크와 무관하게 돌아간다 — 서버가 끊겨도 진행 중인
 * 주입은 계속된다 (DOC 9.4 "필수 안전장치").
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfc_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 서버 링크 상태 (GREEN LED) ──────────────────────────────────── */
typedef enum {
    GFC_LINK_NO_WIFI = 0,   /* OFF        */
    GFC_LINK_NO_SERVER,     /* SLOW 0.8s  */
    GFC_LINK_OK,            /* ON         */
} gfc_link_t;

/** 주입 시퀀스 스냅샷 */
typedef struct {
    bool     enable;        /* 시퀀스 동작 중                       */
    bool     on;            /* 지금 분사 중(Pump2/3 On)             */
    bool     in_init;       /* 초기 주입 구간인가                   */
    float    remain_s;      /* 다음 분사까지 남은 시간 [s]          */
    float    elapsed_s;     /* enable 이후 경과 [s]                 */
    uint32_t cycle_count;   /* 주기 분사 횟수                       */
    uint8_t  pump1, pump2, pump3;
    gfc_src_set_t cfg;
} gfc_ctrl_state_t;

/** GPIO/타이머 초기화. 부팅 시 1회 (gfc_uart_init 뒤에 호출). */
bool gfc_ctrl_init(void);

/** 0x34 SRC_SET 적용. enable=0 이면 즉시 전 펌프 Off. */
void gfc_ctrl_src_set(const gfc_src_set_t *cfg);

/** 0x30 PUMP_SET 적용 — 수동 조작이므로 주입 시퀀스를 먼저 멈춘다. */
void gfc_ctrl_pump_set(uint8_t p1, uint8_t p2, uint8_t p3);

/** 현재 상태 스냅샷. */
void gfc_ctrl_get(gfc_ctrl_state_t *out);

/** 서버 링크 상태를 알려 준다 (GREEN LED 용). */
void gfc_ctrl_set_link(gfc_link_t link);

/**
 * 쌓인 이벤트를 하나 꺼낸다 (SRC_ON / SRC_OFF / POLL_MODE …).
 * @return true 면 code/a 가 유효. 없으면 false.
 */
bool gfc_ctrl_take_event(uint16_t *code, uint32_t *a);

#ifdef __cplusplus
}
#endif
