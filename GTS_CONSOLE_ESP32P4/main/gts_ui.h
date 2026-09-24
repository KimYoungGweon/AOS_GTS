/*
 * gts_ui.h — 화면 골격 (상단바 · 콘텐츠 · 조그바) 와 화면 전환
 *
 * 사양서 2절대로 상단바와 조그바는 한 번만 만들고 내용만 교체한다.
 * 화면 4종은 콘텐츠 영역(0,44,800,372) 안의 컨테이너 4개로 만들어
 * 보이기/숨기기만 전환한다 — lv_screen_load 를 쓰면 상단바·조그바를
 * 화면마다 다시 만들어야 하기 때문이다.
 *
 * 스레드
 * ──────
 * 이 파일의 모든 함수는 LVGL 태스크에서만 호출한다.
 * 다른 태스크는 gts_state 만 갱신하고 dirty 플래그를 세우면 된다.
 */
#pragma once

#include "lvgl.h"
#include "gts_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 전체 UI 생성. lv_display 생성 직후 1회. */
void gts_ui_create(lv_display_t *disp);

/** 화면 전환. */
void gts_ui_goto(gts_page_t page);

/** 현재 장치 연결을 끊고 P1 으로 복귀 ("DEVICE / 장치 변경"). */
void gts_ui_leave_device(void);

/** 상단바·조그바·현재 화면을 즉시 다시 그린다. */
void gts_ui_refresh(void);

/* ── 각 화면 모듈 ────────────────────────────────────────────────── */
lv_obj_t *gts_p1_create(lv_obj_t *parent);   void gts_p1_refresh(void);
lv_obj_t *gts_p2_create(lv_obj_t *parent);   void gts_p2_refresh(void);
lv_obj_t *gts_p3_create(lv_obj_t *parent);   void gts_p3_refresh(void);
lv_obj_t *gts_p4_create(lv_obj_t *parent);   void gts_p4_refresh(void);
lv_obj_t *gts_p5_create(lv_obj_t *parent);   void gts_p5_refresh(void);

#ifdef __cplusplus
}
#endif
