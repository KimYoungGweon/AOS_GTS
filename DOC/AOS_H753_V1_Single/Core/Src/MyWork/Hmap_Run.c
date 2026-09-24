
#include "hmap.h"
#include "MyCTL.h"
#include "MyGPIOSet.h"
#include "command.h"
#include "Util.h"
#include <string.h>

// =========================================================
// 측정 순회 엔진
//
//  구 PCSW 의 TWIN_Coarse_Map_Find()(sMain_TwinSystem.cs:208) 와
//  TWIN_Sample_Map_Find()(:391) 을 F/W 로 옮긴 것.
//
//  구 PC 루프 :  for sY { Bias_Reset(); for sX { for hY { for hX { 1장 측정 }}}}
//  신 F/W     :  Hmap_Run_Step() 이 호출될 때마다 1장씩. Bias 는 1시간 타이머.
//
//  한 장(약 17초)마다 메인 루프로 복귀하므로 측정 중에도 ABORT/STATUS 가 먹는다.
// =========================================================

extern void TxMessage_CTL_PC(u8 cmd);

// ---------------------------------------------------------
// Bias ON → 3초 → OFF
//   구 PCSW sMain_Comm.cs:1093 Bias_Reset() 과 동일한 순서.
//   (CMD_BIAS_ONOFF 0x01 → delay 3000 → 0x00)
// ---------------------------------------------------------
void Hmap_Bias_Reset(void)
{
    MData.BIAS_OnOff = 1;
    DO_BIAS(1);
    delay_ms(HMAP_BIAS_PULSE_MS);
    MData.BIAS_OnOff = 0;
    DO_BIAS(0);
}

// ---------------------------------------------------------
// cond_idx → (sy, sx, hy, hx)
//   ((sy*no_sx + sx)*no_hy + hy)*no_hx + hx  의 역산
// ---------------------------------------------------------
static void idx_to_coord(const __hmap_run *r, u16 idx,
                         u8 *sy, u8 *sx, u8 *hy, u8 *hx)
{
    u16 nhx = g_hmap_cfg.no_hx;
    u16 nhy = g_hmap_cfg.no_hy;
    u16 nsx = r->no_sx;

    *hx = (u8)(idx % nhx);            idx /= nhx;
    *hy = (u8)(idx % nhy);            idx /= nhy;
    *sx = (u8)(idx % nsx);            idx /= nsx;
    *sy = (u8)idx;
}

// ---------------------------------------------------------
u8 Hmap_Run_Start(u8 mode)
{
    __hmap_run *r = &g_hmap_run;

    if (r->state == HMAP_RUN_RUNNING) return 0;   // 이미 진행 중
    if (MData.HMAP.scan_running)      return 0;
    if (MData.FLAG.SCAN_START)        return 0;
    if (MData.SHA_Single.IsStart)     return 0;
    if (MData.SHA_Line.IsStart)       return 0;

    // PAUSED 상태에서 같은 모드로 다시 START 하면 이어서 간다 (재개)
    if (r->state == HMAP_RUN_PAUSED && r->mode == mode) {
        r->abort_req = 0;
        r->pause_req = 0;
        r->state     = HMAP_RUN_RUNNING;
        return 1;
    }

    switch (mode) {
    case HMAP_MODE_FULL:
        r->no_sx    = g_hmap_cfg.no_sx;
        r->no_sy    = g_hmap_cfg.no_sy;
        r->sec_step = 1;
        break;

    case HMAP_MODE_HOUR1:
        // Full 격자를 HMAP_HOUR1_STEP 간격으로 솎는다 (idx 0,3,6,9)
        if (g_hmap_cfg.no_sx < (HMAP_HOUR1_N - 1) * HMAP_HOUR1_STEP + 1) return 0;
        if (g_hmap_cfg.no_sy < (HMAP_HOUR1_N - 1) * HMAP_HOUR1_STEP + 1) return 0;
        r->no_sx    = HMAP_HOUR1_N;
        r->no_sy    = HMAP_HOUR1_N;
        r->sec_step = HMAP_HOUR1_STEP;
        break;

    case HMAP_MODE_SAMPLE:
        if (r->sample_count == 0) return 0;   // 세트를 먼저 받아야 한다
        r->no_sx    = 0;
        r->no_sy    = 0;
        r->sec_step = 1;
        break;

    default:
        return 0;
    }

    r->mode  = mode;
    r->total = (mode == HMAP_MODE_SAMPLE)
             ? r->sample_count
             : (u16)((u16)r->no_sy * r->no_sx * g_hmap_cfg.no_hy * g_hmap_cfg.no_hx);
    if (r->total == 0) return 0;

    r->index      = 0;
    r->done_count = 0;
    r->sy = r->sx = r->hy = r->hx = 0;
    r->abort_req  = 0;
    r->pause_req  = 0;
    r->t_start    = HAL_GetTick();

    // 시작 시 1회 — 이후 1시간 주기
    Hmap_Bias_Reset();
    r->t_bias = HAL_GetTick();

    r->state = HMAP_RUN_RUNNING;
    return 1;
}

// ---------------------------------------------------------
void Hmap_Run_Ctrl(u8 action)
{
    __hmap_run *r = &g_hmap_run;

    switch (action) {
    case HMAP_CTL_ABORT:
        r->abort_req = 1;
        MData.HMAP.abort_flag = 1;      // 진행 중인 1장도 중단시킨다
        break;

    case HMAP_CTL_PAUSE:
        if (r->state == HMAP_RUN_RUNNING) r->pause_req = 1;
        break;

    case HMAP_CTL_RESUME:
        if (r->state == HMAP_RUN_PAUSED) {
            r->pause_req = 0;
            r->abort_req = 0;
            r->state     = HMAP_RUN_RUNNING;
        }
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------
// Sample 파라미터 세트 등록
//   payload : u8 count, u8 reserved, 그 뒤 count 개의 {hv, frq, duty, lff} float
// ---------------------------------------------------------
u8 Hmap_Run_SetSample(const u8 *data, u16 size)
{
    __hmap_run *r = &g_hmap_run;
    u8  count;
    u16 need;
    u16 p;
    u8  i;

    if (r->state == HMAP_RUN_RUNNING) return 0;
    if (size < 2) return 0;

    count = data[0];
    if (count == 0 || count > HMAP_SAMPLE_MAX) return 0;

    need = (u16)(2 + (u16)count * 16);
    if (size < need) return 0;

    p = 2;
    for (i = 0; i < count; i++) {
        memcpy(&r->sample_hv[i],   &data[p], 4); p += 4;
        memcpy(&r->sample_frq[i],  &data[p], 4); p += 4;
        memcpy(&r->sample_duty[i], &data[p], 4); p += 4;
        memcpy(&r->sample_lff[i],  &data[p], 4); p += 4;
    }
    r->sample_count = count;
    return 1;
}

// ---------------------------------------------------------
// 현재 index 에 해당하는 조건을 MData.HMAP 에 싣는다
// ---------------------------------------------------------
static void load_params_for_index(__hmap_run *r)
{
    const __hmap_cfg *c = &g_hmap_cfg;

    if (r->mode == HMAP_MODE_SAMPLE) {
        u8 i = (u8)r->index;
        r->sy = 0; r->sx = i; r->hy = 0; r->hx = 0;

        MData.HMAP.hv   = r->sample_hv[i];
        MData.HMAP.frq  = r->sample_frq[i];
        MData.HMAP.duty = r->sample_duty[i];
        MData.HMAP.lff  = (u16)(r->sample_lff[i] + 0.5f);
    }
    else {
        idx_to_coord(r, r->index, &r->sy, &r->sx, &r->hy, &r->hx);

        // 1Hour 이면 sec_step 간격으로 솎아낸 위치를 쓴다
        MData.HMAP.frq  = c->frq_list [r->sx * r->sec_step];
        MData.HMAP.hv   = c->hv_list  [r->sy * r->sec_step];
        MData.HMAP.duty = c->duty_list[r->hx];
        MData.HMAP.lff  = (u16)(c->lff_list[r->hy] + 0.5f);
    }

    MData.HMAP.lf_waveform = 0;                 // eSquare 고정 (인수인계 E 시리즈)
    MData.HMAP.delay_ms    = c->delay_ms;

    // LFV / CV 축 — 설정 리스트에서 start/step 을 뽑는다 (등간격 격자)
    MData.HMAP.lfv_start = c->lfv_list[0];
    MData.HMAP.lfv_step  = (c->no_lfv > 1) ? (c->lfv_list[1] - c->lfv_list[0]) : 0.0f;
    MData.HMAP.lfv_count = c->no_lfv;

    MData.HMAP.cv_start  = c->cv_list[0];
    MData.HMAP.cv_step   = (c->no_cv > 1) ? (c->cv_list[1] - c->cv_list[0]) : 0.0f;
    MData.HMAP.cv_count  = c->no_cv;
    MData.HMAP.cv        = MData.HMAP.cv_start;

    // 0x86 헤더로 되돌려 보낼 식별 좌표
    MData.HMAP.section_y = r->sy;
    MData.HMAP.section_x = r->sx;
    MData.HMAP.heatmap_y = r->hy;
    MData.HMAP.heatmap_x = r->hx;
}

// ---------------------------------------------------------
// 메인 루프에서 호출. heatmap 1장 측정 후 복귀.
// ---------------------------------------------------------
void Hmap_Run_Step(void)
{
    __hmap_run *r = &g_hmap_run;

    if (r->state != HMAP_RUN_RUNNING) return;
    if (MData.HMAP.scan_running)      return;   // 방어

    if (r->abort_req) {
        r->abort_req = 0;
        r->state = HMAP_RUN_ABORTED;
        MData.HMAP.done_status = 1;
        TxMessage_CTL_PC(CMD_HMAP_DONE);
        return;
    }

    if (r->pause_req) {
        r->pause_req = 0;
        r->state = HMAP_RUN_PAUSED;
        return;                                  // 진행 상태는 그대로 보존
    }

    if (r->index >= r->total) {
        r->state = HMAP_RUN_DONE;
        MData.HMAP.done_status = 0;
        TxMessage_CTL_PC(CMD_HMAP_DONE);
        return;
    }

    // Bias_Reset — 1시간 주기 (tick wrap 에 안전한 뺄셈 비교)
    if ((u32)(HAL_GetTick() - r->t_bias) >= HMAP_BIAS_PERIOD_MS) {
        Hmap_Bias_Reset();
        r->t_bias = HAL_GetTick();
    }

    load_params_for_index(r);

    MData.HMAP.abort_flag = 0;
    Hmap_Scan_Run();            // 1장 측정 + CMD_HMAP_DATA 송신 (약 17초)

    if (MData.HMAP.done_status == 1) {
        // 측정 도중 ABORT 가 걸렸다
        r->state = HMAP_RUN_ABORTED;
        TxMessage_CTL_PC(CMD_HMAP_DONE);
        return;
    }

    r->done_count++;
    r->index++;
}
