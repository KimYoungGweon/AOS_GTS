
#define 	__HMAP_H__
#include	"hmap.h"
#undef	__HMAP_H__

#include "mcp3202.h"
#include "MyCTL.h"
#include <string.h>
#include "command.h"
#include "util.h"
#include "MyGPIOSet.h"

// =========================================================
// Heatmap 측정 (구 Twin.c)
//
//  구 코드 대비 바뀐 점
//   1) 개명            : CMD_TWIN_* → CMD_HMAP_*, MData.TWIN → MData.HMAP
//   2) 측정 1채널      : Air+/Gas+ 동시 2ch → Is_P 1ch (Air 는 별도 run)
//   3) HV/Frq float    : u16 이면 95.55556 → 95, 266.6667 → 266 으로 잘렸다
//   4) START 44 byte   : 구 40 byte(HV/Frq u16) 에서 규격대로 전환
//   5) 버퍼 overrun 수정: 구 line_all_buf[11][15][2] 에 LFV 16 을 써서
//                         [i][15] 가 [i+1][0] 을 침범했다. 이제 클램프 상수와
//                         배열 차원이 같은 map_buf 를 쓴다.
//   6) legacy 라인 전송(0x81) 삭제 — batch(0x86) 로 일원화
// =========================================================

// ========== 본인 환경의 기존 함수들 (extern) ==========
extern void HV_SET_Control(float v);
extern void CV_Control(float v);
extern void Wave_Frq_and_Duty_Update(float frq, float duty);
extern void LF_Modulator_Voltage_Set(void);
extern void LF_Frq_Set(float frq);
extern void TxMessage_CTL_PC(u8 cmd);
// =====================================================

// =========================================================
// LE byte helpers
// =========================================================
static u16 rd_u16_le(const u8 *p) {
    return (u16)p[0] | ((u16)p[1] << 8);
}
static u32 rd_u32_le(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8)
         | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static float rd_f32_le(const u8 *p) {
    u32 u = rd_u32_le(p);
    float f;
    memcpy(&f, &u, 4);      // 비정렬 접근 방지 — 반드시 memcpy
    return f;
}

// =========================================================
// Apply common (fixed-during-scan) parameters
// =========================================================
static void apply_common(void)
{
    HV_SET_Control(MData.HMAP.hv);
    Wave_Frq_and_Duty_Update(MData.HMAP.frq, MData.HMAP.duty);
    LF_Frq_Set((float)MData.HMAP.lff);
    CV_Control(MData.HMAP.cv);

    // ⚠️ LF_Waveform 적용 (본인 환경에 맞게 추가)
    // 예: MData.SET.LF_MOD.waveform = MData.HMAP.lf_waveform;
    //     LF_Modulator_Waveform_Update();
}

// =========================================================
// Set LFV (LF amplitude)
// =========================================================
static void set_lfv(float lfv)
{
    MData.SET.LF_MOD.amp = lfv;
    LF_Modulator_Voltage_Set();
}

// =========================================================
// Single-cell measurement — Is_P 1채널
//   MCP3202 는 두 라인(DOUT1/DOUT2)을 한 번에 읽는 구조라 기존 2ch API 를
//   그대로 쓰고, 그중 어느 쪽을 Is_P 로 삼을지만 hmap.h 에서 고른다.
//     result[0] = Air (ref cell) / result[1] = Gas (sample cell)
//   Single 은 셀이 하나이므로 HMAP_IS_P_CHANNEL 한 채널만 저장한다.
// =========================================================
static u16 hmap_measure_1ch(u16 delay_ms)
{
    u16 adc[2];
    MCP3202_Read_2ch_Avg(adc, delay_ms);
    return adc[HMAP_IS_P_CHANNEL];
}

// =========================================================
// CMD handler: CMD_HMAP_START (0x80) — 44 byte
//
//  off  size type   field
//    0   4   float  HV
//    4   4   float  Frq
//    8   4   float  Duty
//   12   1   u8     LF_Waveform
//   13   1   u8     reserved
//   14   2   u16    LFF
//   16   2   u16    delay_ms
//   18   4   float  LFV_Start
//   22   4   float  LFV_Stop
//   26   4   float  LFV_Step
//   30   4   float  CV_Start
//   34   4   float  CV_Stop
//   38   4   float  CV_Step
//   42   1   u8     SectionY
//   43   1   u8     SectionX
// =========================================================
void Hmap_OnReceive_Start(const u8 *data)
{
    // Reject if any mode is running
    if (MData.FLAG.SCAN_START)         return;
    if (MData.FLAG.HMAP_SCAN_START)    return;
    if (MData.FLAG.HMAP_POINT_START)   return;
    if (MData.HMAP.scan_running)       return;
    if (MData.SHA_Single.IsStart)      return;
    if (MData.SHA_Line.IsStart)        return;

    MData.HMAP.hv          = rd_f32_le(&data[0]);
    MData.HMAP.frq         = rd_f32_le(&data[4]);
    MData.HMAP.duty        = rd_f32_le(&data[8]);
    MData.HMAP.lf_waveform = data[12];
    // data[13] reserved
    MData.HMAP.lff         = rd_u16_le(&data[14]);
    MData.HMAP.delay_ms    = rd_u16_le(&data[16]);

    MData.HMAP.lfv_start   = rd_f32_le(&data[18]);
    MData.HMAP.lfv_stop    = rd_f32_le(&data[22]);
    MData.HMAP.lfv_step    = rd_f32_le(&data[26]);
    MData.HMAP.cv_start    = rd_f32_le(&data[30]);
    MData.HMAP.cv_stop     = rd_f32_le(&data[34]);
    MData.HMAP.cv_step     = rd_f32_le(&data[38]);
    MData.HMAP.section_y   = data[42];
    MData.HMAP.section_x   = data[43];
    // 단발 요청에는 Heatmap 좌표 개념이 없다. run 순회가 아니므로 0.
    MData.HMAP.heatmap_y   = 0;
    MData.HMAP.heatmap_x   = 0;

    // Compute counts (round to nearest, +1 for inclusive endpoints)
    MData.HMAP.lfv_count = (u16)((MData.HMAP.lfv_stop - MData.HMAP.lfv_start)
                                  / MData.HMAP.lfv_step + 1.5f);
    MData.HMAP.cv_count  = (u16)((MData.HMAP.cv_stop  - MData.HMAP.cv_start)
                                  / MData.HMAP.cv_step  + 1.5f);

    // ★ 클램프 상수는 map_buf 의 실제 배열 차원과 같다 (구 버그 재발 방지)
    if (MData.HMAP.lfv_count > HMAP_LFV_POINTS_MAX) MData.HMAP.lfv_count = HMAP_LFV_POINTS_MAX;
    if (MData.HMAP.cv_count  > HMAP_CV_LINES_MAX)   MData.HMAP.cv_count  = HMAP_CV_LINES_MAX;
    if (MData.HMAP.lfv_count == 0) MData.HMAP.lfv_count = 1;
    if (MData.HMAP.cv_count  == 0) MData.HMAP.cv_count  = 1;

    MData.HMAP.abort_flag      = 0;
    MData.FLAG.HMAP_SCAN_START = 1;   // main loop trigger
}

// =========================================================
// CMD handler: CMD_HMAP_POINT_REQ (0x82) — 26 byte
//
//  off  size type   field
//    0   4   float  HV
//    4   4   float  Frq
//    8   4   float  Duty
//   12   1   u8     LF_Waveform
//   13   1   u8     reserved
//   14   2   u16    LFF
//   16   2   u16    delay_ms
//   18   4   float  LFV
//   22   4   float  CV
// =========================================================
void Hmap_OnReceive_PointReq(const u8 *data)
{
    MData.HMAP.point_status = 1;

    MData.HMAP.hv          = rd_f32_le(&data[0]);
    MData.HMAP.frq         = rd_f32_le(&data[4]);
    MData.HMAP.duty        = rd_f32_le(&data[8]);
    MData.HMAP.lf_waveform = data[12];
    // data[13] reserved
    MData.HMAP.lff         = rd_u16_le(&data[14]);
    MData.HMAP.delay_ms    = rd_u16_le(&data[16]);
    MData.HMAP.lfv_start   = rd_f32_le(&data[18]);   // point LFV
    MData.HMAP.cv_start    = rd_f32_le(&data[22]);   // point CV
    MData.HMAP.cv          = MData.HMAP.cv_start;

    MData.SET.RF_HV = MData.HMAP.hv;
    MData.SET.Frq   = MData.HMAP.frq;
    MData.SET.Duty  = MData.HMAP.duty;
    MData.SET.CV    = MData.HMAP.cv;

    if (MData.HMAP.lf_waveform == 0) MData.SET.LF_MOD.type = 6;   // eSquare

    MData.SET.LF_MOD.frq = MData.HMAP.lff;
    MData.SET.LF_MOD.amp = MData.HMAP.lfv_start;

    MData.OSET = MData.SET;

    MData.FLAG.HMAP_POINT_START = true;
}

// =========================================================
// CMD handler: CMD_HMAP_ABORT (0x84)
// =========================================================
void Hmap_OnReceive_Abort(void)
{
    MData.HMAP.abort_flag = 1;
    // Effective only if a scan is running.
    // Hmap_Scan_Run polls this between points and lines.
}

// =========================================================
// Sweep execution (called from main loop)
//   Outer loop: CV  (line  index, 0..cv_count-1)
//   Inner loop: LFV (point index, 0..lfv_count-1)
//   → 전체 완료 후 CMD_HMAP_DATA(0x86) 1회 송신
// =========================================================
void Hmap_Scan_Run(void)
{
    MData.HMAP.scan_running = 1;
    u32 t_start    = HAL_GetTick();
    u8  status     = 0;   // 0=OK, 1=abort
    u16 lines_done = 0;

    apply_common();

    for (u16 cv_idx = 0; cv_idx < MData.HMAP.cv_count; cv_idx++) {
        if (MData.HMAP.abort_flag) { status = 1; break; }

        float cv = MData.HMAP.cv_start + cv_idx * MData.HMAP.cv_step;
        CV_Control(cv);

        for (u16 lfv_idx = 0; lfv_idx < MData.HMAP.lfv_count; lfv_idx++)
        {
            if (MData.HMAP.abort_flag) { status = 1; break; }

            float lfv = MData.HMAP.lfv_start + lfv_idx * MData.HMAP.lfv_step;
            set_lfv(lfv);

            MData.HMAP.map_buf[cv_idx][lfv_idx] = hmap_measure_1ch(MData.HMAP.delay_ms);
        }

        MData.HMAP.cur_line_index  = (u8)cv_idx;
        MData.HMAP.cur_point_count = MData.HMAP.lfv_count;
        lines_done++;
    }

    MData.HMAP.done_status     = status;
    MData.HMAP.done_lines_done = lines_done;
    MData.HMAP.done_elapsed_ms = HAL_GetTick() - t_start;

    delay_ms(100);
    Hmap_CMD_Send_CTL(CMD_HMAP_DATA, 2000);

    MData.HMAP.scan_running = 0;
    MData.HMAP.abort_flag   = 0;
}

#define repeat 3
void Hmap_CMD_Send_CTL(u8 cmd, u16 wCnt)
{
	MData.FLAG.HOST_Received = false;

	for (u8 i = 0; i < repeat; i++)
	{
		MData.TM.wait_delay_cnt = 0;
		TxMessage_CTL_PC(cmd);
		while (1)
		{
			if (MData.TM.wait_delay_cnt >= wCnt) break;
			if (MData.FLAG.HOST_Received) return;
		}
	}
}

// =========================================================
// Single point measurement (called from main loop)
// =========================================================
void Hmap_Point_Run(void)
{
	apply_common();
	set_lfv(MData.HMAP.lfv_start);

	MData.HMAP.point_adc = hmap_measure_1ch(MData.HMAP.delay_ms);

	TxMessage_CTL_PC(CMD_HMAP_POINT_DATA);
	MData.HMAP.point_status = 0;
	MData.TM.adc_int_avg = 0;            // update time reset
	MData.FLAG.HMAP_POINT_START = false;
}
