/*
 * gts_input.h — 외부 Encoder(Jog) + Jog S/W 입력
 *
 * 데모 코드(jc4880p443c_demo.c)의 검증된 4상 디코딩과 SW 디바운스를
 * 그대로 가져오되, 회전량을 화면 상태에 적용하고 값 변경을 UDP 로
 * 내보내는 부분을 추가했다.
 *
 * 송신 디바운스
 * ─────────────
 *   Jog 를 빠르게 돌리면 1 detent 마다 패킷을 보내게 되는데, 사양서
 *   6절이 요구한 대로 50 ms 동안 회전이 멈춘 뒤 최종값 1개만 보낸다.
 *   화면은 디바운스와 무관하게 즉시 갱신된다.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** GPIO 설정 + 폴링 태스크 기동. */
void gts_input_start(void);

/** 현재 Jog S/W 눌림 상태 (디버그·표시용). */
bool gts_input_sw_pressed(void);

/**
 * 현재 화면의 선택 값을 지금 즉시 서버로 보낸다.
 * 터치로 값을 바꿨을 때 UI 코드가 직접 호출한다.
 */
void gts_input_flush_now(void);

#ifdef __cplusplus
}
#endif
