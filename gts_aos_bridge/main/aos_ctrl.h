/*
 * aos_ctrl.h — Manual 파라미터 7종 섀도 + STM32 적용 + LED
 *
 * 콘솔 P4 (AOS Manual) 가 다루는 값과 1:1 이다.
 *
 *   HV · FRQ · DUTY · CV · LF_FRQ · LF_VOLT  (6종)
 *   + LF On/Off                              (7번째)
 *   ( LF 파형(shape) 은 0x52 가 어차피 같이 실어 보내므로 함께 보관한다 )
 *
 * Twin 프로토콜에는 "HV 만 바꿔라" 같은 개별 명령이 없고, 대신 STM32 의
 * SET_Control() 이 메인 루프에서 MData.SET.* 변화를 감지해 적용한다.
 * 그래서 브리지는 섀도를 들고 있다가 바뀐 그룹만 다시 내보낸다.
 *
 *   HV/FRQ/DUTY/CV 중 하나라도 바뀜  → 0x2A CMD_SET_CONTROL (16 byte)
 *   LF 관련 하나라도 바뀜            → 0x52 CMD_LF_MOD_SET  (10 byte)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "aos_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 서버 링크 상태 (GREEN LED) ──────────────────────────────────── */
typedef enum {
    AOS_LINK_NO_WIFI = 0,   /* OFF        */
    AOS_LINK_NO_SERVER,     /* SLOW 0.8s  */
    AOS_LINK_OK,            /* ON         */
} aos_link_t;

/** 섀도 스냅샷 */
typedef struct {
    float    hv, frq, duty, cv;
    float    lf_frq, lf_volt;
    uint8_t  lf_on;
    uint8_t  lf_shape;
    bool     applied;       /* 한 번이라도 STM32 로 내보냈는가 */
    uint32_t apply_count;
} aos_ctrl_state_t;

/** GPIO/태스크 초기화. aos_uart_init() 뒤에 호출. */
bool aos_ctrl_init(void);

/**
 * 파라미터 하나를 바꾼다 (콘솔 0x40 AOS_SET_PARAM 에 대응).
 * 범위를 벗어나면 clamp 한다.
 * @return false 면 param_id 가 범위 밖.
 */
bool aos_ctrl_set_param(uint8_t param_id, float value);

/** LF On/Off (콘솔 0x41). */
void aos_ctrl_set_lf_mode(bool on);

/** LF 파형 (콘솔 0x42). */
void aos_ctrl_set_lf_shape(uint8_t shape);

/** 현재 섀도 스냅샷. */
void aos_ctrl_get(aos_ctrl_state_t *out);

/** 서버에 올릴 파라미터 묶음 (콘솔 0xC3 과 같은 26 byte 배치). */
void aos_ctrl_fill_params(aos_params_t *out);

/** 서버 링크 상태를 알려 준다 (GREEN LED 용). */
void aos_ctrl_set_link(aos_link_t link);

/** 쌓인 이벤트를 하나 꺼낸다. */
bool aos_ctrl_take_event(uint16_t *code, uint32_t *a);

/** 콘솔 shape(0~3) → STM32 eLF_Type. 모르는 값은 SQUARE 로. */
uint8_t aos_shape_to_stm32(uint8_t shape);

/** STM32 eLF_Type → 콘솔 shape(0~3). 콘솔에 없는 파형은 가장 가까운 것으로. */
uint8_t aos_shape_from_stm32(uint8_t type);

/** 방금 STM32 로 값을 내보냈는지 — 서버에 즉시 알릴지 판단용. */
bool aos_ctrl_take_applied(void);

#ifdef __cplusplus
}
#endif
