#define __MYCTL_H__
	#include "MyCTL.h"
#undef __MYCTL_H__

#include "main.h"
#include "UART_PC.h"
#include "MyGPIOSet.h"
#include "CAL_CTL.h"
#include "EEPROM.h"
#include "LTC2602.h"
#include "LTC2602B.h"
#include "command.h"
#include "AD7739.h"
#include "Jog.h"
#include "UART_LCD.h"
#include "Util.h"
#include "ScanCTL.h"
#include "AD9850.h"
#include "MCP4161.h"
#include "Shallow.h"
#include "LF_GEN.h"
#include "AD9850.h"
#include "hmap.h"
#include "mcp3202.h"

void TIM2_Int_1msec()
{
	MData.TM.LED++;
	MData.TM.Jog++;
	MData.TM.Jog_Btn++;
	MData.TM.adc++;
	MData.TM.DISP++;
	MData.TM.adc_int_avg++;
	MData.TM.dma_count++;
	MData.TM.set++;
	MData.D_SCAN.tm++;
	MData.TM.msec++;
	MData.SHA_Line.procTime_msec++;


	MData.TM.delay_cnt++;
	MData.TM.wait_delay_cnt++;

	if(MData.TM.msec>=1000)
	{
		MData.TM.msec=0;
		MData.CLOCK.Sec++;
		if(MData.CLOCK.Sec>=60)
		{
			MData.CLOCK.Sec=0;
			MData.CLOCK.Min++;
			if(MData.CLOCK.Min>=60)
			{
				MData.CLOCK.Min=0;
				MData.CLOCK.Hour++;
				if(MData.CLOCK.Hour>=24)
				{
					MData.CLOCK.Hour=0;
				}
			}
		}
	}


	if(MData.START_CTL.IsStartFlag)
	{
		MData.START_CTL.tm++;
		if(MData.START_CTL.tm >2) MData.START_CTL.IsStartFlag=false;
	} else MData.START_CTL.tm=0;

	DO_START(MData.START_CTL.IsStartFlag);

	JOG_READ();

	if(MData.Buzzer.IsOn)
	{
		MData.Buzzer.tm++;
		if(MData.Buzzer.tm >= MData.Buzzer.OnTime) Buzzer_Off();
	}
}

void TIM3_Int_10msec()
{
	MData.TM.uart1+=10;

	MData.TM.uart_err_check += 10;



	if(Rx_MSG_CTL_PC()) MData.FLAG.HOST_Received=true;


}

void System_Init()
{

	MData.SN=3;//0=KBSI, 1=FTLAB V1.6.0//2-> V1.7.0, 3

	/*
	CV : 1.SN = 2 와 SN = 3 동일한 값으로 해도 거의 동일함.
	*/


	DO_BIAS(true);

	Buzzer_On(200);

	EEPROM_INIT();
	LTC2602_INIT();

	Hmap_Cfg_Init();    // 측정 격자 설정 — EEPROM 에서 복원, 없으면 default




	Set_Cal_Default_Factor_DAC_HV();
	Set_Cal_Default_Factor_DAC_CV();



	Set_Cal_Default_Factor_ADC_VS_FAN();

	Set_Cal_Default_Factor_ADC_ION_BIAS();

	Set_Cal_Default_Factor_ADC_VS_HV();
	Set_Cal_Default_Factor_ADC_IS_HV();


	//Set_Cal_Default_Factor_MCP4161_FAN();

	for(u8 i=0;i<6;i++) CAL_Data_Write(i);
	for(u8 i=0;i<6;i++) CAL_Data_Read(i);






	//MinMax Set
	MData.SET.CV = 0.0f;
	MData.SET.RF_HV = 50.0f;
	MData.SET.Frq = 500;
	MData.SET.Duty = 50.0;
	MData.SET.RF_MOD_frq = 4000;
	MData.SET.LF_MOD.amp=2;
	MData.SET.LF_MOD.frq=200;
	MData.SET.LF_MOD.type=5;
	MData.SET.LF_MOD.no=100;

	MData.SET.FAN=12.0;


	//MData.SET.MinV[0] = 50.0f; 	MData.SET.MaxV[0] = 550.0f;//V
	MData.SET.MinV[0] = 30.0f; 	MData.SET.MaxV[0] = 560.0f;//HV for cal.

	MData.SET.MinV[1] = 200.0f; MData.SET.MaxV[1] = 800.0f;//kHz
	MData.SET.MinV[2] = 20.0f; 	MData.SET.MaxV[2] = 80.0f;//duty

	MData.SET.MinV[3] = -5.0f; 	MData.SET.MaxV[3] = 5.0f;//CV	//기초연

	MData.SET.MinV[4] = 0; 		MData.SET.MaxV[4] = 24.0f;//FAN 사용하지 않음.
	MData.SET.MinV[5] = 1000.0f;MData.SET.MaxV[5] = 30000.0f;//HF MOD Frq(1MHz  ~ 30MHz)
	MData.SET.MinV[6] = 30.0f; 	MData.SET.MaxV[6] = 500.0f;//LF Frq
	MData.SET.MinV[7] = 0.0f; 	MData.SET.MaxV[7] = 5.0f;//LF Amp


	//MCP4161_Init();

	LTC2602B_INIT();

	CV_Control(MData.SET.CV);
	HV_SET_Control(MData.SET.RF_HV);
	Wave_Frq_and_Duty_Init( );

	MData.SET.RF_MOD_frq = 4000000;
	MData.SET.LF_MOD.OnOff = false;//false;

	MData.SET.LF_MOD.type=6;
	MData.SET.LF_MOD.no=100;
	MData.SET.LF_MOD.amp=2.5;//Voltage
	MData.SET.LF_MOD.frq=200;//HZ


	//KBSI Setting
	//MData.SN=0;//KBSI
	//MData.MODULATED_MODE = 2; //2-> KBSI

	//FTLAB Setting

	MData.MODULATED_MODE = 0;

	//modulation RF
	switch(MData.MODULATED_MODE)
	{
		case 0://LF Mode
			LF_Modulator_Set(MData.SET.LF_MOD.type);
			break;
		case 1://HF Mode
			ad9850_init_and_set_sine(0);//stop
			break;
		case 2: //NONE  기초연모드
			break;

	}


	MData.SET.ch = 0;//HV
	MData.SET.Jog_W[0] = 0;
	MData.SET.Jog_W[1] = 1;
	MData.SET.Jog_W[2] = 0;
	MData.SET.Jog_W[3] = 1;

	//CAL_FRQ_Save(MData.BUS_CLOCK_CAL);
	MData.BUS_CLOCK_CAL = CAL_FRQ_Read();

	MData.BUS_CLOCK_CAL=480000;

	//MData.ION_SELECT=0;
	///ION_SELECTION(MData.ION_SELECT);
	//BIAS_SELECTION(0);


	LCD_PAGE_CHANGE(2);
	delay_ms(1000);
	LCD_Data_View_All();


	MData.SET.Current_Type= 3;//Positive Current 1-> Negative Current

	//0 ->LF Mode
	//1-> HF Mode
	//2-> No Modulate
	//if(MData.IsLF_MODE) LCD_MODE_Set(0);
	//else LCD_MODE_Set(1);

	LCD_MODE_Set(MData.MODULATED_MODE);

	//Shallow_Function_Data_Read();//사용하지 않음.


	ADC7739_Init();
	MData.IsADC_MCP3202=true;

	if(MData.IsADC_MCP3202)
	{
		MCP3202_Init();
	}
	else
	{
		ADC7739_Init2();	delay_ms(10);
		ADC7739_Init3();
	}
	delay_ms(500);
	DO_BIAS(false);//off
}

void Buzzer_On(u16 onTime)
{
	DO_BUZZER(true);
	MData.Buzzer.IsOn = true;
	MData.Buzzer.OnTime = onTime;
	MData.Buzzer.tm = 0;
}

void Buzzer_Off()
{
	DO_BUZZER(false);
	MData.Buzzer.IsOn = false;
	MData.Buzzer.tm = 0;
}


void Main_CTL()
{
	if(MData.TM.LED>=200)
	{
		MData.TM.LED=0;
		MData.FLAG.LED=true;
	}


	Button_and_JOG_Control();
	ADC_Read_and_Disp();

	SET_Control();

	if(MData.FLAG.HOST_Received)
	{
		RxMessage_CTL_PC();
	}

	if(MData.FLAG.SCAN_START)
	{
		MData.FLAG.SCAN_START=false;//Reset
		switch(MData.ScanType)
		{
			case 0:	FAIMs_SCAN_CTL();	break;
			case 1://Digital Mode 지금은 사용하지 않음
				FAIMs_D_SCAN_CTL();	break;
			case 2://Fast Mode
				FAIMs_SCAN_CTL_FullRange_Mode(); break;
		}
	}
	else if(MData.FLAG.HMAP_SCAN_START)
	{
	    MData.FLAG.HMAP_SCAN_START=false;
	    Hmap_Scan_Run();                  // heatmap sweep
	}
	else if(MData.SHA_Single.IsStart) Find_Shallow_Sinlge_Point();
	else if(MData.SHA_Line.IsStart) Shallow_Find_Line_Control();
	else if(MData.FLAG.HMAP_POINT_START) Hmap_Point_Run();
	// 측정 순회 — 한 번에 heatmap 1장만 하고 복귀한다.
	// 8시간을 한 함수에서 돌면 그동안 ABORT/STATUS 명령이 처리되지 않는다.
	else if(g_hmap_run.state == HMAP_RUN_RUNNING) Hmap_Run_Step();
	else
	{
		if(MData.TM.adc_int_avg>=25)
		{
			MData.TM.adc_int_avg=0;
			Ion_Current_Read();
			if(MData.IsAutoCurrentSend) TxMessage_CTL_PC(CMD_STATUS_QUERY);
		}
	}


	//간혹 uart error 현상발생시 Check 후에 Update 한다.
	if(MData.TM.uart_err_check >= 2000)
	{
		MData.TM.uart_err_check=0;
	}
	//debug_ctl();

}

float ohm;
void SET_Control()
{
	MData.TM.set=0;
	if(MData.SET.CV != MData.OSET.CV)
	{
		CV_Control(MData.SET.CV);
		LCD_Data_View(eLCD_CV);
	}

	if(MData.SET.RF_HV != MData.OSET.RF_HV)
	{
		HV_SET_Control(MData.SET.RF_HV);
		LCD_Data_View(eLCD_HV);
	}
	else
	{
		if(MData.SET.RF_HV_Disp != MData.SET.RF_HV) LCD_Data_View(eLCD_HV);
	}




	if(MData.SENSE.HV_Vs != MData.OSENSE.HV_Vs)
	{
		LCD_Data_View(eLCD_HV_VS);
	}


	if(MData.SET.Frq != MData.OSET.Frq || MData.SET.Duty != MData.OSET.Duty)
	{
		Wave_Frq_and_Duty_Update(MData.SET.Frq, MData.SET.Duty);
		LCD_Data_View(eLCD_FRQ);
		LCD_Data_View(eLCD_DUTY);
	}

	switch(MData.MODULATED_MODE)
	{
		case 0:
			if(MData.SET.LF_MOD.OnOff != MData.OSET.LF_MOD.OnOff || MData.SET.LF_MOD.frq != MData.OSET.LF_MOD.frq || MData.SET.LF_MOD.type != MData.OSET.LF_MOD.type)
			{
				LF_Modulator_Set(MData.SET.LF_MOD.type);
				LCD_Data_View(eLCD_LF_MOD);
			}
			else if(MData.SET.LF_MOD.amp != MData.OSET.LF_MOD.amp )
			{
				LF_Modulator_Voltage_Set();
				LCD_Data_View(eLCD_LF_MOD);
			}
			break;
		case 1:
			if(MData.SET.RF_MOD_OnOff != MData.OSET.RF_MOD_OnOff || MData.SET.RF_MOD_frq != MData.OSET.RF_MOD_frq)
			{
				if(MData.SET.RF_MOD_OnOff) 	ad9850_send_freq(MData.SET.RF_MOD_frq, 124999050);//124999010
				else 						ad9850_send_freq(0, 124999050);//124999010

				LCD_Data_View(eLCD_HF_MOD);
			}
			break;
		case 2:
			break;
	}

	/*if(MData.SET.FAN != MData.OSET.FAN)
	{
		FAN_Control(MData.SET.FAN);
		LCD_Data_View(eLCD_FAN);
	}*/


	if(MData.SET.ch != MData.OSET.ch) LCD_Weight(MData.SET.Jog_W[MData.SET.ch]);
	else
	{
		for(u8 i=0;i<8;i++)
		{
			if(i==MData.SET.ch)
			{
				if(MData.SET.Jog_W[i] != MData.OSET.Jog_W[i])
				{
					LCD_Weight(MData.SET.Jog_W[i]);
				}
			}
		}
	}
	MData.OSET = MData.SET;
}






void ADC_Read_and_Disp()
{
	if(MData.TM.adc >= 30)
	{
		MData.TM.adc=0;
		/* V1.6.x
		switch(MData.SENSE.ch)
		{
			case 0:
				MData.SENSE.HV_Vs_adc = AD7739_READ(0);
				MData.SENSE.HV_Vs = Find_Cal_Result_for_ADC_VS_HV(MData.SENSE.HV_Vs_adc);
				break;
			case 1:
				MData.SENSE.FAN_Vs_adc = AD7739_READ(2);
				MData.SENSE.FAN_Vs = Find_Cal_Result_for_ADC_VS_FAN(MData.SENSE.FAN_Vs_adc);
				break;
			case 2:
				MData.SENSE.ION_Bias_Vs_adc = AD7739_READ(3);
				MData.SENSE.ION_Bias_Vs = Find_Cal_Result_for_ADC_VS_ION_BIAS(MData.SENSE.ION_Bias_Vs_adc);
				break;
		}

		MData.SENSE.ch++;
		MData.SENSE.ch %=3;
		*/
		//V1.7.x
		switch(MData.SENSE.ch)
		{
			case 0:
				MData.SENSE.HV_Vs_adc = AD7739_READ(0);
				MData.SENSE.HV_Vs = Find_Cal_Result_for_ADC_VS_HV(MData.SENSE.HV_Vs_adc);
				break;
			case 1:
				MData.SENSE.FAN_Vs_adc = AD7739_READ(1);
				MData.SENSE.FAN_Vs = Find_Cal_Result_for_ADC_VS_FAN(MData.SENSE.FAN_Vs_adc);
				break;
		}

		MData.SENSE.ch++;
		MData.SENSE.ch %=2;

	}

	if(MData.TM.DISP >= 200)
	{
		MData.TM.DISP=0;
		if(MData.SENSE.HV_Vs != MData.OSENSE.HV_Vs) 			LCD_Data_View(11);
		if(MData.SENSE.FAN_Vs != MData.OSENSE.FAN_Vs) 			LCD_Data_View(4);
		//if(MData.SENSE.ION_Bias_Vs != MData.OSENSE.ION_Bias_Vs) LCD_Data_View(eLCD_BIAS_VS);
		MData.OSENSE = MData.SENSE;
	}
}

void Parameter_Set_Control(float hv, float frq, float duty, float cv){
u16 value;

	value=Find_Cal_Result_for_DAC_CV(hv);
	LTC2602_COMMAND(0, value);

	value=Find_Cal_Result_for_DAC_HV(cv);
	LTC2602_COMMAND(1, (u16)value);

	Wave_Frq_and_Duty_Update(frq, duty);
	MData.START_CTL.IsStartFlag=true;
}



#define HV_Step 30.0

void HV_SET_Control(float volt){
float tVolt;

	if(MData.SET.RF_HV>MData.OSET.RF_HV)
	{
		if((MData.SET.RF_HV-MData.OSET.RF_HV)>HV_Step)
		{
			u8 no = (u8)((MData.SET.RF_HV-MData.OSET.RF_HV)/HV_Step);
			tVolt = MData.OSET.RF_HV;
			for(u8 i = 0 ; i < no ; i++)
			{
				tVolt += (i+1)*HV_Step;
				HV_Volt_Set(tVolt);
				delay_ms(100);
			}
			HV_Volt_Set(volt);
		}
		else HV_Volt_Set(volt);
	}
	else HV_Volt_Set(volt);


	MData.START_CTL.IsStartFlag=true;
}

void HV_Volt_Set(float volt)
{
	MData.OSET.RF_HV = volt;
	MData.SET.HV_DAC =Find_Cal_Result_for_DAC_HV(volt);
	if(MData.SET.HV_DAC>=65535) MData.SET.HV_DAC = 65535;
	LTC2602_COMMAND(1, MData.SET.HV_DAC);
}

void FAN_Control(float volt)
{
	MData.SET.FAN_Ohm = Find_Cal_Result_for_MCP4161_FAN(volt);
	MCP4161_SetWB_Ohms(MData.SET.FAN_Ohm );
}

void CV_Control(float volt){

	MData.OSET.CV = volt;
	MData.SET.CV = volt;
	MData.SET.CV_DAC = Find_Cal_Result_for_DAC_CV(volt);
	if(MData.SET.CV_DAC>=65535) MData.SET.CV_DAC = 65535;
	LTC2602_COMMAND(0, MData.SET.CV_DAC);

	MData.START_CTL.IsStartFlag=true;
}

/*
void Bias_Selection(u8 type)
{
	//all off
	DO_BIAS_P(false);
	DO_BIAS_N(false);

	HAL_Delay(1000);

	if(type==0) DO_BIAS_P(true);
	else		DO_BIAS_N(true);
}
*/

/*
void CURRENT_GAIN_SET(u8 gain)
{
	switch(gain)
	{
		case 0: DO_ION_GAIN0(1);	DO_ION_GAIN1(0);	break;
		case 1: DO_ION_GAIN0(0);	DO_ION_GAIN1(1);	break;
		case 2: DO_ION_GAIN0(0);	DO_ION_GAIN1(0);	break;
	}
}
*/
/*
void ION_SELECTION(u8 type)
{
	DO_ION_SELECT0(0);
	DO_ION_SELECT1(0);
	if(type==0) DO_ION_SELECT0(1);
	else		DO_ION_SELECT1(1);
}
*/

/*
void BIAS_SELECTION(u8 type)
{
	DO_BIAS_P(0);
	DO_BIAS_P2(0);
	DO_BIAS_N(0);
	DO_BIAS_N2(0);
	delay_ms(100);
	if(type==0)
	{
		DO_BIAS_P2(1);
		delay_ms(100);
		DO_BIAS_P(1);

	}
	else
	{
		DO_BIAS_N2(1);
		delay_ms(100);
		DO_BIAS_N(1);
	}
}
*/

/*
void LF_Modulator_DAC_Update(){
u16 value;
	value= MData.SET.LF_MOD.data[MData.SET.LF_MOD.count++];
	MData.SET.LF_MOD.count %= MData.SET.LF_MOD.no;
	LTC2602B_COMMAND(0, value);
}

void LF_Modulator_Set(){
float step;
u16 period;
u16 maxValue = (u16)(MData.SET.LF_MOD.amp/5.0*31768);
u16 midValue = 32768;
	period = 1000000/ MData.SET.LF_MOD.frq/100 ; //1us 단위 period 100단계 -> 100us
	MX_TIM4_Update(period-1);

	switch(MData.SET.LF_MOD.type)
	{
		case 0:
			step = maxValue/50;
			MData.SET.LF_MOD.data[0]=0;
			for(u8 i=0;i<50;i++) MData.SET.LF_MOD.data[i] = step*(25-i) + midValue;
			for(u8 i=0;i<50;i++) MData.SET.LF_MOD.data[i+50] = MData.SET.LF_MOD.data[49-i];
			break;
		case 1:
			step = maxValue/100;
			MData.SET.LF_MOD.data[0]=0;
			for(u8 i=0;i<100;i++) MData.SET.LF_MOD.data[i] = step*(50-i) + midValue;
			break;
	}

	if(MData.SET.LF_MOD.OnOff==0)
	{
		LTC2602B_COMMAND(0, 32768);//출력
		TIM4_OnOff(MData.SET.LF_MOD.OnOff);
	}
	else TIM4_OnOff(MData.SET.LF_MOD.OnOff);
}
*/
