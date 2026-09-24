#ifndef __HMAP__
#define __HMAP__

#ifdef __HMAP_H__
	#define EXT_HMAP
#else
	#define EXT_HMAP extern
#endif
#include "mType.h"
#include "command.h"   // HMAP_LFV_POINTS_MAX / HMAP_CV_LINES_MAX

// =========================================================
// Heatmap 측정 (구 twin.h).
//
//  Twin(2셀 동시) → Single(단일셀) 전환:
//    Air 와 Target 을 한 번에 재지 않고 각각 별도로 측정한다.
//    따라서 1 point 당 저장값은 Is_P 1채널뿐이며,
//    Idf(= Target_P - Air_P) 는 서버/PC 가 계산한다.
//
//  버퍼 용량 상수는 command.h 의 HMAP_LFV_POINTS_MAX / HMAP_CV_LINES_MAX.
//  ★ MyCTL.h 의 map_buf 배열 차원과 반드시 같아야 한다.
// =========================================================

// MCP3202 채널 배치 (구 Twin 하드웨어 기준)
//   MCP3202_Read_2ch_Avg(result, delay) 가
//     result[0] = Air (reference cell)
//     result[1] = Gas (sample cell)
//   로 채운다.
#define HMAP_CH_AIR              0
#define HMAP_CH_GAS              1

// Single 시스템에서 실제로 쓰는 셀.
//   Twin 은 두 셀을 동시에 읽었지만, Single 은 셀이 하나다.
//   Air run 이든 Target run 이든 같은 셀로 측정하고, 흘리는 가스만 바꾼다.
//   → 시료가 흐르는 sample cell(= 구 Gas 채널)을 쓴다.
//   ⚠️ 실제 배선이 다르면 이 한 줄만 HMAP_CH_AIR 로 바꾼다.
#define HMAP_IS_P_CHANNEL        HMAP_CH_GAS

// CMD_HMAP_DATA 페이로드 최대 크기 = 8 + noCV*noLFV*2
#define HMAP_DATA_PAYLOAD_MAX   (8 + HMAP_CV_LINES_MAX * HMAP_LFV_POINTS_MAX * 2)

// ★ 컴파일 타임 가드 — UART_PC.c 의 SOut[4000+10] 을 넘지 않는지 검사한다.
//   구 코드는 이 검사가 없어서, 규격 기본 격자(51x21, 2ch)로 0x86 을 보내면
//   8 + 51*21*4 = 8,572 byte 를 4,010 byte 버퍼에 쓰게 되어 있었다.
//   실제로는 16x11 만 써서 우연히 살아 있었을 뿐이다.
//   1채널로 바뀐 지금 최대치는 8 + 21*51*2 = 2,150 byte 로 안전하다.
//   (아래 배열 크기가 음수가 되면 컴파일이 멈춘다)
typedef char hmap_payload_fits_in_SOut[(HMAP_DATA_PAYLOAD_MAX <= 4000) ? 1 : -1];

// =========================================================
// 측정 격자 설정 (__hmap_cfg)
//
//  근거: DOC/GTS_DB_Design/GTS_DB_Design.md 6·7·10 항
//  구 PCSW 는 Filter Type 에서 HV 범위를 계산해 냈지만(mStruct_B.cs),
//  이제는 위 문서의 값을 default 로 박아두고 필요하면 대시보드에서 바꾼다.
//  실제로 바꿀 일이 있는 건 사실상 HV 뿐이고 나머지는 고정에 가깝다.
//
//  저장: 기존 SPI EEPROM (EEPROM.c). Flash 아님.
// =========================================================

#define HMAP_SX_N                10      // Frq  (Section X)
#define HMAP_SY_N                10      // HV   (Section Y)
#define HMAP_HX_N                 4      // Duty (Heatmap X)
#define HMAP_HY_N                 4      // LFF  (Heatmap Y)
#define HMAP_LFV_N               16      // LF Volt 축
#define HMAP_CV_N                11      // CV 축

// 1Hour 모드 = Full 격자를 3칸 간격으로 솎아낸 4x4
//   HV  idx 0,3,6,9 → 45, 90, 135, 180
//   Frq idx 0,3,6,9 → 200, 400, 600, 800
// (GTS_DB_Design.md 7항과 일치)
#define HMAP_HOUR1_N              4
#define HMAP_HOUR1_STEP           3

// 측정 모드
#define HMAP_MODE_FULL            0      // 10x10 x 4x4 = 1600장
#define HMAP_MODE_HOUR1           1      //  4x4  x 4x4 =  256장
#define HMAP_MODE_SAMPLE          2      // 임의 N세트 (서버가 내려줌)

// EEPROM 배치
//   기존 사용 구간: 0x0010 FRQ_CAL / 0x0200 HV_DAC / 0x0600 HV_VS / 0x0800 HV_IS
//                   0x0A00 CV_DAC / 0x0C00 FAN_VS / 0x0D00 BIAS_VS / 0x1000 SHALLOW_2D
//   → 충분히 떨어진 0x4000 을 쓴다.
#define HMAP_CFG_EE_ADDR     0x4000
#define HMAP_CFG_MAGIC       0x484D4131UL   // 'H','M','A','1'
#define HMAP_CFG_VERSION     1

typedef struct {
    u32 magic;
    u16 version;
    u16 size;                       // sizeof(__hmap_cfg)

    u8  no_sx, no_sy;               // 사용 차원 (기본 10, 10)
    u8  no_hx, no_hy;               // 기본 4, 4
    u8  no_lfv, no_cv;              // 기본 16, 11
    u16 delay_ms;                   // point 측정 대기 (기본 100)

    float frq_list [HMAP_SX_N];     // kHz
    float hv_list  [HMAP_SY_N];     // V   ★ 실질적으로 이것만 바뀐다
    float duty_list[HMAP_HX_N];     // %
    float lff_list [HMAP_HY_N];     // Hz
    float lfv_list [HMAP_LFV_N];    // V
    float cv_list  [HMAP_CV_N];     // V

    u16 chksum;                     // magic~cv_list 의 u16 합
} __hmap_cfg;

EXT_HMAP __hmap_cfg  g_hmap_cfg;

// default 값으로 채운다 (GTS_DB_Design.md 기준)
EXT_HMAP void Hmap_Cfg_SetDefault(__hmap_cfg *c);
// EEPROM 에서 읽는다. magic/version/chksum 이 맞지 않으면 default 로 채우고 0 반환.
EXT_HMAP u8   Hmap_Cfg_Load(void);
// 현재 g_hmap_cfg 를 EEPROM 에 쓴다. 성공 1.
EXT_HMAP u8   Hmap_Cfg_Save(void);
// 값 범위 검사. 이상하면 0.
EXT_HMAP u8   Hmap_Cfg_Validate(const __hmap_cfg *c);
// System_Init 에서 1회 호출 — Load 후 g_hmap_cfg 확정
EXT_HMAP void Hmap_Cfg_Init(void);

// =========================================================
// 측정 순회 (run) — 구 PCSW 의 TWIN_Coarse_Map_Find / TWIN_Sample_Map_Find
//
//  구조: PC 가 1,600번 0x80 을 쏘던 것을 F/W 가 스스로 돌도록 옮겼다.
//        통신이 끊겨도 측정은 계속된다.
//
//  ★ 반드시 "한 장 측정하고 메인 루프로 복귀" 하는 상태기계여야 한다.
//    8시간을 한 함수 안에서 돌면 그동안 ABORT/STATUS 명령이 처리되지 않는다.
//
//  순회 순서는 구 PC 코드와 같다 : sY → sX → hY → hX (hX 가 가장 안쪽)
//    cond_idx = ((sy*no_sx + sx)*no_hy + hy)*no_hx + hx
//    (DB 설계 문서의 cond_idx 정의와 동일)
// =========================================================

#define HMAP_RUN_IDLE            0
#define HMAP_RUN_RUNNING         1
#define HMAP_RUN_PAUSED          2
#define HMAP_RUN_DONE            3
#define HMAP_RUN_ABORTED         4

#define HMAP_CTL_ABORT           0
#define HMAP_CTL_PAUSE           1
#define HMAP_CTL_RESUME          2

#define HMAP_SAMPLE_MAX         16      // Sample 모드 세트 최대 (필요분 8, 여유 포함)

// Bias_Reset 주기 — 구 PCSW 는 sY 행마다 호출했으나 원래 의도는 1시간 주기였다.
#define HMAP_BIAS_PERIOD_MS      3600000UL
#define HMAP_BIAS_PULSE_MS       3000

typedef struct {
    u8  state;                  // HMAP_RUN_*
    u8  mode;                   // HMAP_MODE_*
    u16 total;                  // 총 장수
    u16 index;                  // 다음에 측정할 cond_idx
    u16 done_count;             // 실제로 측정을 마친 장수

    u8  no_sx, no_sy;           // 이 run 의 유효 Section 차원 (1Hour 이면 4,4)
    u8  sec_step;               // 격자 솎기 간격 (Full=1, 1Hour=HMAP_HOUR1_STEP)
    u8  sy, sx, hy, hx;         // 현재 좌표

    u32 t_start;
    u32 t_bias;                 // 마지막 Bias_Reset tick
    u8  abort_req;
    u8  pause_req;

    // Sample 모드 파라미터 세트 (서버가 0x8F 로 내려준다)
    u8  sample_count;
    float sample_hv  [HMAP_SAMPLE_MAX];
    float sample_frq [HMAP_SAMPLE_MAX];
    float sample_duty[HMAP_SAMPLE_MAX];
    float sample_lff [HMAP_SAMPLE_MAX];
} __hmap_run;

EXT_HMAP __hmap_run  g_hmap_run;

// mode 로 run 을 시작한다. 이미 진행 중이거나 파라미터가 이상하면 0.
EXT_HMAP u8   Hmap_Run_Start(u8 mode);
// abort / pause / resume
EXT_HMAP void Hmap_Run_Ctrl(u8 action);
// Sample 모드 파라미터 세트 등록 (측정 전에 받아둔다). 성공 1.
EXT_HMAP u8   Hmap_Run_SetSample(const u8 *data, u16 size);
// 메인 루프에서 호출 — heatmap 1장을 측정하고 곧바로 복귀한다.
EXT_HMAP void Hmap_Run_Step(void);
// Bias ON → 3초 → OFF (구 PCSW 의 Bias_Reset 과 동일)
EXT_HMAP void Hmap_Bias_Reset(void);

// 컴파일 타임 정합성 — 격자가 측정 버퍼를 넘지 않아야 한다
typedef char hmap_grid_fits_in_buf[
    (HMAP_LFV_N <= HMAP_LFV_POINTS_MAX && HMAP_CV_N <= HMAP_CV_LINES_MAX) ? 1 : -1];

// =========================================================
// CMD handlers (called from CMD switch in RS232 receive)
// =========================================================
EXT_HMAP void Hmap_OnReceive_Start(const u8 *data);      // 44 byte payload
EXT_HMAP void Hmap_OnReceive_PointReq(const u8 *data);   // 26 byte payload
EXT_HMAP void Hmap_OnReceive_Abort(void);

// =========================================================
// Sweep / Point execution (called from main loop)
// =========================================================
EXT_HMAP void Hmap_Scan_Run(void);    // when MData.FLAG.HMAP_SCAN_START
EXT_HMAP void Hmap_Point_Run(void);   // when MData.FLAG.HMAP_POINT_START

EXT_HMAP void Hmap_CMD_Send_CTL(u8 cmd, u16 wCnt);

#endif
