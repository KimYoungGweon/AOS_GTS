/*
 * gts_sim.c — 오프라인 테스트용 장비 시뮬레이션
 */
#include <stdint.h>
#include "gts_sim.h"
#include "gts_config.h"

#if GTS_OFFLINE_MODE

#include "gts_state.h"

/* GFC 주기 10분 = 6000 deciseconds (사양서 5.2) */
#define CYCLE_DS   6000u

void gts_sim_tick(uint32_t period_ms)
{
    /* 이번 호출이 담당하는 "장비 시간". 밀리초 단위로 누적해 두고
     * 1초/0.1초 경계를 넘을 때만 상태를 움직인다.                     */
    static uint32_t acc_ms;
    acc_ms += period_ms * GTS_OFFLINE_SPEEDUP;

    uint32_t whole_ds = acc_ms / 100u;      /* 0.1 초 단위 */
    if (whole_ds == 0) return;
    acc_ms -= whole_ds * 100u;

    gts_state_lock();

    /* ── AOS 가스 측정 (P3) ───────────────────────────────────────
     * 경과시간을 올리고, 총 시간에 닿으면 서버가 보내 줄 done=1 과
     * 같은 처리를 한다 — running 을 내리고 진행바는 100% 로 남긴다. */
    if (g_gts.aos_running) {
        uint32_t total = g_gts.aos_total_s;
        if (total == 0) total = gts_aos_type_seconds(g_gts.aos_type);

        /* elapsed 는 초 단위. 0.1초 누적분을 초로 환산해 더한다. */
        static uint32_t aos_frac_ds;
        aos_frac_ds += whole_ds;
        uint32_t add_s = aos_frac_ds / 10u;
        aos_frac_ds -= add_s * 10u;

        if (add_s) {
            g_gts.aos_elapsed_s += add_s;
            /* 샘플 수는 초당 4개 정도로 그럴듯하게 */
            g_gts.aos_samples += add_s * 4u;
        }

        if (g_gts.aos_elapsed_s >= total) {
            g_gts.aos_elapsed_s = total;
            g_gts.aos_running   = false;
            g_gts.aos_done      = true;
        }
        gts_state_mark_dirty();
    }

    /* ── GFC Auto (P2) ────────────────────────────────────────────
     * 10분 주기를 카운트다운하고, 남은 시간이 "주기 Pump On Time"
     * 이하로 내려간 구간에서 펌프를 켠다. 0 에 닿으면 주기를 새로
     * 돌리고 카운트를 올린다.                                      */
    if (g_gts.gfc_mode == GTS_GFC_AUTO && g_gts.gfc_auto_run) {
        uint16_t rem = g_gts.gfc_remain_ds;

        if (rem == 0) rem = CYCLE_DS;               /* 시작 직후 */
        if (rem > whole_ds) {
            rem = (uint16_t)(rem - whole_ds);
        } else {
            rem = CYCLE_DS;
            g_gts.gfc_cycle_count++;
        }

        g_gts.gfc_remain_ds = rem;
        g_gts.gfc_pump_on   = (rem <= g_gts.gfc_cycle_ds);
        gts_state_mark_dirty();
    }

    gts_state_unlock();
}

#else   /* GTS_OFFLINE_MODE == 0 — 서버가 상태를 보내 준다 */

void gts_sim_tick(uint32_t period_ms) { (void)period_ms; }

#endif
