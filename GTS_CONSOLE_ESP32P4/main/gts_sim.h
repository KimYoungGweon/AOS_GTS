/*
 * gts_sim.h — 오프라인 테스트용 장비 시뮬레이션
 *
 * 서버가 보내 줄 GFC_AUTO_STATE / AOS_MEAS_STATE 를 콘솔이 대신 만들어
 * 화면이 움직이게 한다. gts_config.h 의 GTS_OFFLINE_MODE 가 0 이면
 * 이 파일의 내용은 통째로 비어 있는 함수가 된다 — 서버가 붙으면
 * 아무것도 지우지 않아도 된다.
 *
 * 임시 발판이므로 상태 모델이나 프로토콜에는 손대지 않는다.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** LVGL 갱신 타이머에서 주기적으로 호출. period_ms 는 호출 간격. */
void gts_sim_tick(uint32_t period_ms);

#ifdef __cplusplus
}
#endif
