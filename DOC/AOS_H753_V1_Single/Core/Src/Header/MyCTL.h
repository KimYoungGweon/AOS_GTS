#ifndef __MYCTL__
#define __MYCTL__

#ifdef __MYCTL_H__
#define EXT_MYCTL
#else
#define EXT_MYCTL extern
#endif

#define CMD_QUEUE_SIZE  4096

#include "mType.h"
#include "command.h"   // HMAP_LFV_POINTS_MAX / HMAP_CV_LINES_MAX (map_buf 차원)


#define NUM_HISTORY 10

#define ADC_BUFFER_SIZE	1000

EXT_MYCTL u16 adc_buffer[1000];


typedef enum
{
	eCV_HV = 0,
	eCV_FRQ,
	eFRQ_HV,
	eLFM_FRQ_LF_Volt,

} eScan_Type;

typedef enum
{
	eTriangle=0,
	eRamp,
	eRamp1_9,
	eRamp2_8,
	eRamp3_7,
	eSine,
	eSquare,
	eTPZ05_95,
	eTPZ1_9,
	eTPZ2_8,
	eTPZ3_7,
	eARB
} eLF_Type;



typedef enum
{
	eLCD_HV=0,
	eLCD_FRQ,
	eLCD_DUTY,
	eLCD_CV,
	eLCD_FAN,
	eLCD_HF_MOD,
	eLCD_LF_MOD,
	eLCD_LF_MOD_AMP,
	eLCD_HV_VS,
	eLCD_BIAS_VS,
} __LCD_VIEW;


typedef struct
{
	u16	Main;
	u16	RS232_SendCount;
	u16	RS232M;

	u16	msec;
	u16	LED;
	u16	BTN;
	u16 DISP;

	u16	uart1;
	//u16	uart5;

	u16 adc;

	u16 UART_SEND;

	u16 Delay_100us;

	u16 Jog_Btn;
	u16 Jog;

	u16 adc_int_avg;

	u32 dma_count;
	u32 dma_count2;
	u32 dma_proc_time_10times;

	u16	set;


	u16 delay_cnt;
	u16 wait_delay_cnt;

	u16 uart_err_check;
}__tm;



typedef struct
{
	u8	Hour;
	u8	Min;
	u8	Sec;
	u16	ProcMin;
	u8	OLD_Sec;
	u8	Hour_Count;

	u16 On_sec;
	u16 Off_sec;
	u32 Total_proc_sec;
}__CLOCK;




typedef struct
{
	u8 HOST_Received;
	u8 UART5_Received;

	u8 REC_OK;


	u8	Meas;
	u8	adc;
	u8	Send;
	u8	StatusSend;
   	u16 LED;

   	u8	ADC_Disp;
   	u8	SCAN_START;
   	u8	D_SCAN_START;

   	u8	UART_ERROR_RECOVER;

   	u8 HMAP_SCAN_START;
   	u8 HMAP_POINT_START;
}__FLAG;





typedef struct
{
   	u8 FLAG;
   	float pTime;
}__debug;





typedef struct
{
   	u8 	ch;
	u32 proc_sec;
}__log;


//
#define FullScanMaxNo	50
typedef struct
{
	u8	type;
	u8	noX;
	u8	noY;
	u16 w_delay;

	float xVal[FullScanMaxNo];
	float yVal[FullScanMaxNo];
	u16	Curr[FullScanMaxNo][FullScanMaxNo];

	u8 	IsShallowMask;
	u8	ShallowType;
	s16	Shallow[FullScanMaxNo][FullScanMaxNo];
}__scan_Full;


typedef struct
{
	u8		IsStart;
   	u8 		type;
	u16		no;

	float 	startX;
	float 	stepX;

	float 	Start_Frq;
	float 	Step_Frq;

	u16		w_delay;

	float 	duty;

	u16		Curr[1000];
	u16		Curr2[1000];

	float	ret_Volt[1000];

	u8		IsShallowMask;
	u8		ShallowType;
}__scan;


typedef struct{
	float frq;
	float amp;
	u8 type; //triange, ramp
	u8 OnOff;


	u8 count;
	u8 no;
	u16 data[200];
}__low_frq_mod;




typedef struct
{
	u8		ch;
	u8		Jog_W[8];
	float	MaxV[8];
	float	MinV[8];
	float	CV;
	u16		CV_DAC;
	float	RF_HV;
	float	RF_HV_Disp;
	u16		HV_DAC;
	float	Frq;
	float	Duty;

	float	FAN;
	float	FAN_Ohm;

	u8				RF_MOD_OnOff;
	float			RF_MOD_frq;//RF Modulattor Frq

	u8				IsUpdate_LF_MOD;
	__low_frq_mod 	LF_MOD;

	u8		Current_Type;

}__set;

typedef struct
{
	u8 		ch;
	u16		HV_Vs_adc;
	u16		HV_Is_adc;
	u16		FAN_Vs_adc;
	u16		ION_Bias_Vs_adc;
	float	HV_Vs;
	float	HV_Is;
	float	FAN_Vs;
	float	ION_Bias_Vs;

	u32		adc_test;


}__sense;


typedef struct
{

	u8		IsStartFlag;
	u16		tm;
}__start;

typedef struct
{

	u8		IsOn;
	u16		OnTime;
	u16 	tm;
}__buzzer;

typedef struct
{
	float hv;
	float frq;
	float duty;
	float cv;
	u16 	delay;
	u16 d_is[20];
	u16	det_delay;

}__d_para;


typedef struct
{
	u8 No;
	u8 xNo;
	u8 yNo;
	__d_para PARA[10*10];

	u16 	Curr[10*10];

	u16 tm;
	u8 BufferDoneFlag;
	u8 BuffCount;
	u8	Mode; //0 Normal, 1->Delay Check, 2-> Smart Delay
}__d_scan;


typedef struct
{
	float avg_history[NUM_HISTORY];
	float std_dev[NUM_HISTORY];
	u8	 history_index;
	u8 IsStable;
	float STABLE_AVG_THRESH;
	float STABLE_STD_THRESH;
}__curr_statble;


typedef struct
{
	//scan Value
	float HV;
	float Frq;

	//set value
	u16 min_IS;
	u16 max_IS;

	//fixed values
	float duty;
	float CV;
	float FAN;
	u8	LF_Type;
	float LF_Frq;
	float ret_Volt;

	u16 w_delay;

	//vaiable
	u8 type; //0-> LF voltage, 1-> CV, 2-> Duty,
	float val_min;			//50mV +-5mV(%)
	float val_max;


	//Finding Method
	u8 F_Method;//0->
	u8	ScanStep;

	//result value

	u8 IsStart;
	u32 procTime_msec;

	u8 IsFound;
	u16 IS;
	float val_ret;

}__shallow;


typedef struct{

	u8 type;//0-> LF, 1-> CV
	u8 IsNegative;//0 Posi. 1->Nega

	float sVolt;
	float fVolt;
	float stepVolt;
	u16   wait_delay;


	u16 Ref_Is_adc;
	u16 Current_max;

	u16 Current;
	float ret_Volt;



	u8 IsFound;
	u32 procTime_msec;

	//float 	LF_Volt_Result;
	u16		Current_Result;

	u8	IsStart;

}__shallow_single;


typedef struct{

	u8 type;//0, 1,2 찾는 다양한 방법을 정의한다.

	float sVolt;
	float fVolt;
	float stepVolt;
	u16   wait_delay;

	u8		no;
	float	duty;
	float	ret_volt[25];

	u16		Is[25];

	u16 ref_Is;

	u16 Current;

	u8 IsFound[25];
	u32 procTime_msec;

	u8	IsStart;

}__shallow_line;


// =========================================================
// Heatmap 측정 상태 (구 __twin).  Single 시스템 전환.
//   - 측정 채널이 2ch(Air+/Gas+) → 1ch(Is_P) 로 줄었다.
//     Air 는 별도 run 으로 따로 측정하므로 동시에 잡지 않는다.
//   - hv/frq 가 u16 → float. HV_List 의 95.55556, Frq_List 의 266.6667 같은
//     값이 u16 에서는 95 / 266 으로 잘렸다.
// =========================================================
typedef struct {
    // CMD_HMAP_START parameters (from PC/서버, 44 byte)
    float hv;                // ★ u16 → float (정밀도 손실 제거)
    float frq;               // ★ u16 → float
    float duty;
    u8  lf_waveform;
    u16 lff;
    u16 delay_ms;
    float lfv_start, lfv_stop, lfv_step;
    float cv_start,  cv_stop,  cv_step;
    float cv;
    u8  section_y, section_x;
    // Heatmap 좌표 — run 순회 시 0x86 헤더로 되돌려 보내는 식별자.
    // 한 Section 안에 Duty x LFF 장이 있으므로 Section 좌표만으로는 구분이 안 된다.
    u8  heatmap_y, heatmap_x;

    // Computed sweep counts (from start/stop/step)
    u16 lfv_count;           // <= HMAP_LFV_POINTS_MAX
    u16 cv_count;            // <= HMAP_CV_LINES_MAX

    // ★ 측정 버퍼 : map_buf[cv_idx][lfv_idx] = Is_P (ADC raw)
    //   차원은 반드시 command.h 의 HMAP_*_MAX 와 같아야 한다.
    //   구 코드는 line_all_buf[11][15][2] 였는데 LFV 를 16 까지 썼다.
    //   → [i][15] 가 [i+1][0] 을 덮어써서, 모든 행의 마지막 값이
    //     다음 행의 첫 값으로 나오는 손상이 있었다 (샘플 데이터에서 100% 재현).
    u16 map_buf[HMAP_CV_LINES_MAX][HMAP_LFV_POINTS_MAX];

    // Send context (used by TxMessage_CTL_PC)
    u8  cur_line_index;      // current CV line being sent
    u16 cur_point_count;     // usually = lfv_count

    // POINT measurement result (CMD_HMAP_POINT_REQ)
    u8  point_status;        // 0=OK, 1=error
    u16 point_adc;           // ★ 1ch 로 축소 (구 point_adc[4])

    // HMAP_DONE info
    u8  done_status;         // 0=OK, 1=abort, 2=error
    u16 done_lines_done;
    u32 done_elapsed_ms;

    // Runtime flags
    volatile u8 scan_running;
    volatile u8 abort_flag;

    u32 point_count;
    u8	rec_flag;
} __hmap;


typedef struct
{
	__tm 		TM;
	__CLOCK 	CLOCK;

	__FLAG		FLAG;


	__debug		debug;



	u32			ProcTime_ms;//




	float 		CV;
	float 		CV_Old;

	u16			adcIS[5];
	u16			adcAvg_P;
	u16			adcAvg_N;

	u16			TW_adcAvg_P;
	u16			TW_adcAvg_N;

	u8			adcCnt;
	float		Ion_Current;

	u16			SendCountNo;

	u16			Buf_Avg[10];
	u8			Buf_Cnt;


	__scan		SCAN;

	__scan_Full	F_SCAN;

	//u8			CURR_GAIN;
	u8			ION_SELECT;




	u8			Bias_Type;
	u8			Cal_Mode;

   	u32			BUS_CLOCK_CAL;


   	__set		SET;
   	__set		OSET;

   	__sense		SENSE;
   	__sense		OSENSE;

   	__start		START_CTL;
   	__buzzer	Buzzer;

   	__d_scan	D_SCAN;
   	__curr_statble	CUR_STABLE;
   	u8 			IsSmartDelay;
   	u8 			ScanType;

   	__set		R_SET;


   	__shallow	Shallow;

   	__shallow_single 	SHA_Single;
   	__shallow_line		SHA_Line;

   	u8			IsAutoCurrentSend;

   	u16			UART_RECOVER_Count;

   	u8			MODULATED_MODE;
   	u8			SN;


   	u8			BIAS_OnOff;
   	u8			IsADC_MCP3202;

   	__hmap	HMAP;
}__mdata;




typedef struct
{
	u8 xNo;
	u8 yNo;
	float data[25][11];
}__shallow_mask;



EXT_MYCTL __shallow_mask cMask;
EXT_MYCTL __mdata MData;

EXT_MYCTL void System_Init();
EXT_MYCTL void Main_CTL();
EXT_MYCTL void TIM2_Int_1msec();
EXT_MYCTL void TIM3_Int_10msec();
EXT_MYCTL void FRQ_Test();
EXT_MYCTL void Buzzer_On(u16 onTime);
EXT_MYCTL void Buzzer_Off();


EXT_MYCTL void CV_Control(float volt);
EXT_MYCTL void Bias_Selection(u8 type);



EXT_MYCTL u16 DMA_ADC_Average();

EXT_MYCTL void CURRENT_GAIN_SET(u8 gain);
EXT_MYCTL void HV_SET_Control(float volt);
EXT_MYCTL void ADC_Read_and_Disp();
EXT_MYCTL void ION_SELECTION(u8 type);
EXT_MYCTL void BIAS_SELECTION(u8 type);
EXT_MYCTL void SET_Control();

EXT_MYCTL void Parameter_Set_Control(float hv, float frq, float duty, float cv);
EXT_MYCTL void FAN_Control(float volt);
EXT_MYCTL void HV_Volt_Set(float volt);


#endif







