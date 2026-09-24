#define 	__SCANCTL_H__
#include	"ScanCTL.h"
#undef	__SCANCTL_H__

#include "MyCTL.h"
#include "util.h"
#include "math.h"
#include "UART_PC.h"
#include "main.h"
#include "command.h"
#include "AD7739.h"
#include "CAL_CTL.h"
#include "LF_GEN.h"
#include "mcp3202.h"

u16 avg_adc_nega=0;

void FAIMs_SCAN_CTL_FullRange_Mode(){
float shallow_fValue;
float xVal, yVal;
float duty = MData.SET.Duty;

	for(u16 i=0; i<MData.F_SCAN.noY;i++)
	{
		yVal = MData.F_SCAN.yVal[i];
		for(u16 j=0; j<MData.F_SCAN.noX;j++)
		{
			xVal = MData.F_SCAN.xVal[j];

			switch(MData.F_SCAN.type)
			{
				case eFRQ_HV:
					HV_SET_Control(yVal);
					Wave_Frq_and_Duty_Update(xVal, duty);
					break;
				case eLFM_FRQ_LF_Volt:
					MData.SET.LF_MOD.amp = xVal;
					LF_Modulator_Voltage_Set();
					LF_Frq_Set(yVal);
					break;
				case eCV_HV:
					CV_Control(xVal);
					HV_SET_Control(yVal);
					break;
				case eCV_FRQ:
					Wave_Frq_and_Duty_Update(yVal, duty);
					CV_Control(xVal);
					break;
			}

			if(MData.F_SCAN.IsShallowMask)
			{
				shallow_fValue = MData.F_SCAN.Shallow[i][j]*0.01f;
				if(MData.F_SCAN.ShallowType==0)
				{
					MData.SET.LF_MOD.amp = shallow_fValue;
					LF_Modulator_Voltage_Set();
				}
				else CV_Control(shallow_fValue);
			}

			MData.F_SCAN.Curr[j][j] = Find_Current_Avg_with_Delay_II(MData.F_SCAN.w_delay);
		}
	}

	TxMessage_CTL_PC(CMD_SCAN_FULL_MODE_RESULT);
}


void FAIMs_SCAN_CTL(){

	for(u16 i=0; i<MData.SCAN.no;i++)
	{
		switch(MData.SCAN.type)
		{
			case eFRQ_HV:
				MData.SET.Frq = MData.SCAN.startX + MData.SCAN.stepX*i;
				Wave_Frq_and_Duty_Update(MData.SET.Frq, MData.SET.Duty);
				if(MData.SCAN.IsShallowMask)
				{
					MData.Shallow.ret_Volt =Find_LF_Volt_from_SHALLOW(MData.SET.Frq);
					if(MData.SCAN.ShallowType==0)
					{
						MData.SET.LF_MOD.amp = MData.Shallow.ret_Volt;
						LF_Modulator_Voltage_Set();
					}
					else
					{
						CV_Control(MData.Shallow.ret_Volt);
					}
				}
				break;
			case eLFM_FRQ_LF_Volt:
				MData.SET.LF_MOD.frq = MData.SCAN.startX + MData.SCAN.stepX*i;
				LF_Frq_Set(MData.SET.LF_MOD.frq);
				break;
			case eCV_HV:
			case eCV_FRQ:
				MData.SET.CV = MData.SCAN.startX + MData.SCAN.stepX*i;
				CV_Control(MData.SET.CV);
				break;
		}

		MData.SCAN.Curr[i] = Find_Current_Avg_with_Delay(MData.SCAN.w_delay);
		MData.SCAN.Curr2[i] = avg_adc_nega;
		MData.SCAN.ret_Volt[i] = MData.SET.LF_MOD.amp;
	}

	TxMessage_CTL_PC(CMD_SCAN_RESULT);//PC 에서 Return 이 필요한지 모르겠음.
}


void FAIMs_D_SCAN_CTL(){
u16 wTm=10;
u8 doneFlag=0;
	MData.SCAN.IsStart=true;

	MData.FLAG.SCAN_START=false;


	for(u16 i=0; i<MData.D_SCAN.No;i++)
	{
		Parameter_Set_Control(MData.D_SCAN.PARA[i].hv, MData.D_SCAN.PARA[i].frq, MData.D_SCAN.PARA[i].duty, MData.D_SCAN.PARA[i].cv);

		MData.CUR_STABLE.IsStable=false;
		MData.CUR_STABLE.history_index=0;
		MData.D_SCAN.tm=0;
		MData.D_SCAN.BuffCount=0;
		doneFlag=0;
		wTm=10;

		while(true)
		{
			switch(MData.D_SCAN.Mode)
			{
				case 0:
					if(MData.D_SCAN.tm >= MData.D_SCAN.PARA[i].delay) doneFlag = true;
					break;
				case 1:
					if(MData.D_SCAN.tm >= wTm)
					{
						MData.D_SCAN.PARA[i].d_is[MData.D_SCAN.BuffCount++] = MData.adcAvg_P;
						if(MData.D_SCAN.BuffCount>=17)	doneFlag=true;
						else if(MData.D_SCAN.BuffCount>=10)
						{
							wTm=50;
						} else wTm=10;
					}
					break;
				case 2:
					if(MData.CUR_STABLE.IsStable)
					{
						MData.D_SCAN.PARA[i].det_delay = MData.D_SCAN.tm;
						doneFlag = true;
					}
					break;
			}

			if(doneFlag) break;
		}
		MData.D_SCAN.Curr[i] = MData.adcAvg_P;//MData.Ion_Current;
	}

	MData.SCAN.IsStart=false;
	TxMessage_CTL_PC(CMD_D_SCAN_RESULT);//PC 에서 Return 이 필요한지 모르겠음.
	Buzzer_On(200);
}

int ch=0;
void Ion_Current_Read(){
//uint32_t sum = 0;

	/*
	for (int i = 0; i < ADC_BUFFER_SIZE; i++)
	sum += adc_buffer[i];

	MData.adcAvg= (u16)(sum / ADC_BUFFER_SIZE);
	MData.Ion_Current = (float)MData.adcAvg/655235*2.5f;
	MData.Buf_Avg[MData.Buf_Cnt++] = (u16)(sum / ADC_BUFFER_SIZE);
	MData.Buf_Cnt %=10;

	*/
	//MData.adcAvg= AD7739_READ(1);

	//MData.adcAvg_P= AD7739_READ2(MData.SET.Current_Type);
	//MData.adcAvg_N= AD7739_READ2(MData.SET.Current_Type);


	ch++;
	ch%=4;
	switch(ch)
	{
		case 0:
			MData.adcAvg_P	= MCP3202_Read(MCP_CH_AIR_PLUS);
			break;
		case 1:
			MData.adcAvg_N	= MCP3202_Read(MCP_CH_AIR_MINUS);
			break;
		case 2:
			MData.TW_adcAvg_P	= MCP3202_Read(MCP_CH_GAS_PLUS);
			break;
		case 3:
			MData.TW_adcAvg_N	= MCP3202_Read(MCP_CH_GAS_MINUS);
			break;
	}

	/*
	if(ch==0)
	{
		MData.adcAvg_P		= AD7739_READ2(3);	delay_ms(5);
	}
	else
	{

		MData.adcAvg_N		= AD7739_READ2(1);	delay_ms(5);
	}
	*/
}


void ADC_Buffer_Done_Control(){
u8 idx = MData.CUR_STABLE.history_index;
float avg = 0.0f, stddev = 0.0f;

	//ADC Bufferdone 주기는 1.3msec
	//process time -> 295us 정도

	MData.TM.dma_count2++;
	if(MData.TM.dma_count2>=10)
	{
		//10회 buffer를 채우는데 걸리는 시간 (12mec)
		MData.TM.dma_proc_time_10times=MData.TM.dma_count;
		MData.TM.dma_count=0;
		MData.TM.dma_count2=0;
	}


	avg=0;
	for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
		avg += adc_buffer[i];
	}
	avg /= ADC_BUFFER_SIZE;

	MData.D_SCAN.BufferDoneFlag = true;


	if(MData.D_SCAN.Mode==2)
	{
		// 1. 평균값 계산
		//for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
			//avg += adc_buffer[i];
		//}
		//avg /= ADC_BUFFER_SIZE;

		// 2. 표준편차 계산
		for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
			float diff = adc_buffer[i] - avg;
			stddev += diff * diff;
		}
		stddev = sqrtf(stddev / ADC_BUFFER_SIZE);

		// 3. 평균값 히스토리 업데이트
		MData.CUR_STABLE.avg_history[idx] = avg;
		MData.CUR_STABLE.std_dev[idx] = stddev;
		MData.CUR_STABLE.history_index++;
		MData.CUR_STABLE.history_index %= NUM_HISTORY;

		// 4. 안정성 판단 (최근 평균값 변화량)
		float total_diff = 0.0f;
		for (int i = 1; i < NUM_HISTORY; i++) {
			total_diff += fabsf(MData.CUR_STABLE.avg_history[i] - MData.CUR_STABLE.avg_history[i - 1]);
		}
		float avg_diff = total_diff / (NUM_HISTORY - 1);

		// 5. 조건 평가
		if(MData.IsSmartDelay)
		{
			if (avg_diff < MData.CUR_STABLE.STABLE_AVG_THRESH && stddev < MData.CUR_STABLE.STABLE_STD_THRESH) {
				MData.CUR_STABLE.IsStable = true;
			} else {
				MData.CUR_STABLE.IsStable = false;
			}
		} else MData.CUR_STABLE.IsStable=false;
	}

}


u16 avg_cnt=0;


u16 Find_Current_Avg_with_Delay(u16 delay){
u32 sum=0;
u32 sum2=0;

	MData.TM.delay_cnt=0;//reset
	HAL_Delay(10);//wait delay //transient 기간 대기 시간  30ms -> 10msec로 변

	avg_cnt=0;
	while(1)
	{
		sum += AD7739_READ2(3); //positive ch
		delay_us(200);
		sum2 += AD7739_READ2(1); //negative ch
		delay_us(200);
		avg_cnt++;
		if(MData.TM.delay_cnt>=delay) break;
	}

	avg_adc_nega = (u16)(sum2/avg_cnt);
	return (u16)(sum/avg_cnt);
}



u16 Find_Current_Avg_with_Delay_II(u16 delay){
u32 sum=0;
u8 adc_ch=0;
	MData.TM.delay_cnt=0;//reset
	avg_cnt=0; sum=0;

	switch(MData.SET.Current_Type)
	{
		case 0: 	adc_ch=3;	break;
		case 1: 	adc_ch=1;	break;
		default: 	adc_ch=3;	break;
	}

	while(1)
	{
		sum += AD7739_READ2(adc_ch);
		avg_cnt++;
		if(MData.TM.delay_cnt>=delay) break;
	}

	return (u16)(sum/avg_cnt);
}



void Shallow_Single_Point_Find()
{
	HV_SET_Control(MData.SET.RF_HV);
	Wave_Frq_and_Duty_Update(MData.SET.Frq, MData.SET.Duty);
	CV_Control(MData.SET.CV);
	//FAN_Control(MData.SET.FAN);
}
