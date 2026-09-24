#define __JOG_H__
#include	"Jog.h"
#undef	__JOG_H__

#include "MyCTL.h"
#include "MyGPIOSet.h"
#include "Util.h"
#include "main.h"
#include "UART_LCD.h"

s16 Jog_Value=0;
void Button_and_JOG_Control()
{

	if(MData.TM.Jog_Btn>=500){
		if(JOG_Read_Button()) 	MData.TM.Jog_Btn=0;
		else					MData.TM.Jog_Btn=300;
	}

	if(MData.TM.Jog>=10)
	{	//50ms
		MData.TM.Jog=0;
		if(Jog_Value!=0) Arrangement_Jog_Control( );
	}




}

u8 MJog=0;
void JOG_READ(void){
u8 bit1,bit2,ret;

	bit1=DI_JOG1;
	bit2=DI_JOG2;
	ret=bit1*1+bit2*2; //0,1,2,3
	if(MJog != ret){
		if(ret == 1 || ret ==2){
			if(ret==1){
				if(MJog==0) JOG_COUNT(0);	//Increase
				if(MJog==3) JOG_COUNT(1);	//decrease
			} else{
				if(MJog==3) JOG_COUNT(0);	//Increase
				if(MJog==0) JOG_COUNT(1);	//decrease
			}
		}
		MJog=ret;
	}
}

void JOG_COUNT(u8 inKey){
	if(inKey)	Jog_Value--;
	else 		Jog_Value++;
}

u8 JOG_Read_Button(void){		//JOG BUTTON READ
u8 ch = MData.SET.ch;
u16 wCnt=0;
	if(DI_BTN1)
	{
		MData.SET.ch++;

		if(MData.SET.ch==4) MData.SET.ch=5;


		switch(MData.MODULATED_MODE)
		{
			case 0:
				if(MData.SET.ch==5) MData.SET.ch=6;
				MData.SET.ch %=8;
				break;
			case 1:
				if(MData.SET.ch==6) MData.SET.ch=0;
				break;
			case 2:
				if(MData.SET.ch==5) MData.SET.ch=0;
				break;
		}
		Buzzer_On(50);
		return 1;
	}
	else if(DI_BTN2)
	{
		if(MData.MODULATED_MODE==0)
		{
			if(MData.SET.ch ==6 || MData.SET.ch == 7)
			{//LF Mode
				MData.SET.LF_MOD.type++;
				MData.SET.LF_MOD.type %=11;
				Buzzer_On(50);
			}

			Buzzer_On(50);
		}
		return 1;
	}
	else if(DI_BTN3)
	{
		while(1)
		{
			wCnt+=10;
			if(!DI_BTN3) break;
			if(wCnt>=3000) break;
			delay_ms(10);
		}

		if(wCnt>=3000)
		{
			MData.Cal_Mode++;
			MData.Cal_Mode %=2;
			Buzzer_On(200);
		}
		return 1;
	}
	else if(DI_BTN5)
	{
		switch(MData.SET.ch)
		{
			case 5:
				if(MData.MODULATED_MODE==1)
				{
					MData.SET.RF_MOD_OnOff++;
					MData.SET.RF_MOD_OnOff %=2;
					Buzzer_On(50);
				}
				break;
			case 6:
			case 7:
				if(MData.MODULATED_MODE==0)
				{
					MData.SET.LF_MOD.OnOff++;
					MData.SET.LF_MOD.OnOff %=2;
					Buzzer_On(50);
				}
				break;
			default:
				break;
		}
		return 1;
	}
	else if(!DI_JOG_BTN){

		switch(ch)
		{
			case 0://hv
				MData.SET.Jog_W[ch]++;
				MData.SET.Jog_W[ch] %= 3;
				break;
			case 1://Frq
				MData.SET.Jog_W[ch]++;
				MData.SET.Jog_W[ch] %= 5;
				break;
			case 2://Duty
				MData.SET.Jog_W[ch]++;
				MData.SET.Jog_W[ch] %= 3;
				break;
			case 3://cv
				MData.SET.Jog_W[ch]++;
				MData.SET.Jog_W[ch] %= 3;
				break;
			case 4://FAN  --> 삭제
				MData.SET.Jog_W[ch]=0;
				break;
			case 5://RF-MOD FRq
				MData.SET.Jog_W[ch]++;
				MData.SET.Jog_W[ch] %= 6;
				break;
			case 6://LF-MOD frq
				MData.SET.Jog_W[ch]=0;
				break;
			case 7://LF-MOD AMP
				MData.SET.Jog_W[ch]=0;
				break;
		}
		Buzzer_On(50);
		return 1;
	}
	return 0;
}

void Arrangement_Jog_Control(void){
float w_fact;
u8 ch = MData.SET.ch;

	switch(ch)
	{
		case 0://RF HV
			switch(MData.SET.Jog_W[ch])
			{
				case 0:	w_fact=0.1f;	break;//0.1V
				case 1:	w_fact=1.0f;	break;//0.5V
				case 2:	w_fact=5.0f;	break;//5.0V
			}
			if((MData.SET.RF_HV+Jog_Value*w_fact)>MData.SET.MaxV[ch])		MData.SET.RF_HV=MData.SET.MaxV[ch];
			else if((MData.SET.RF_HV+Jog_Value*w_fact)<MData.SET.MinV[ch]) 	MData.SET.RF_HV=MData.SET.MinV[ch];
			else 															MData.SET.RF_HV+=Jog_Value*w_fact;
			break;

		case 1://Frq
			switch(MData.SET.Jog_W[ch])
			{
				case 0:	w_fact=0.5f;	break;
				case 1:	w_fact=1.0f;	break;
				case 2:	w_fact=2.0f;	break;
				case 3:	w_fact=5.0f;	break;
				case 4:	w_fact=10.0f;	break;
			}
			if((MData.SET.Frq+Jog_Value*w_fact)>MData.SET.MaxV[ch])			MData.SET.Frq=MData.SET.MaxV[ch];
			else if((MData.SET.Frq+Jog_Value*w_fact)<MData.SET.MinV[ch]) 	MData.SET.Frq=MData.SET.MinV[ch];
			else 															MData.SET.Frq+=Jog_Value*w_fact;
			break;
		case 2://Duty
			switch(MData.SET.Jog_W[ch])
			{
				case 0:	w_fact=0.1f;	break;
				case 1:	w_fact=0.5f;	break;
				case 2:	w_fact=1.0f;	break;
			}
			if((MData.SET.Duty+Jog_Value*w_fact)>MData.SET.MaxV[ch])		MData.SET.Duty=MData.SET.MaxV[ch];
			else if((MData.SET.Duty+Jog_Value*w_fact)<MData.SET.MinV[ch]) 	MData.SET.Duty=MData.SET.MinV[ch];
			else 															MData.SET.Duty+=Jog_Value*w_fact;
			break;

		case 3://CV
			switch(MData.SET.Jog_W[ch])
			{
				case 0:	w_fact=0.001f;	break;//0.001V
				case 1:	w_fact=0.01f;	break;//0.01V
				case 2:	w_fact=0.10f;	break;//0.05V
			}
			if((MData.SET.CV+Jog_Value*w_fact)>MData.SET.MaxV[ch])		MData.SET.CV=MData.SET.MaxV[ch];
			else if((MData.SET.CV+Jog_Value*w_fact)<MData.SET.MinV[ch]) MData.SET.CV=MData.SET.MinV[ch];
			else 														MData.SET.CV+=Jog_Value*w_fact;
			break;
		case 4://FAN
			w_fact=0.1f;
			if((MData.SET.FAN+Jog_Value*w_fact)>MData.SET.MaxV[ch])			MData.SET.FAN=MData.SET.MaxV[ch];
			else if((MData.SET.FAN+Jog_Value*w_fact)<MData.SET.MinV[ch]) 	MData.SET.FAN=MData.SET.MinV[ch];
			else 															MData.SET.FAN+=Jog_Value*w_fact;
			break;
		case 5://rf Frq
			switch(MData.SET.Jog_W[ch])
			{
				case 0:	w_fact=0.001f;	break;
				case 1:	w_fact=0.01f;	break;
				case 2:	w_fact=0.1f;	break;
				case 3:	w_fact=1.0f;	break;
				case 4:	w_fact=10.0f;	break;
				case 5:	w_fact=100.0f;	break;
			}
			if((MData.SET.RF_MOD_frq+Jog_Value*w_fact)>MData.SET.MaxV[ch])			MData.SET.RF_MOD_frq=MData.SET.MaxV[ch];
			else if((MData.SET.RF_MOD_frq+Jog_Value*w_fact)<MData.SET.MinV[ch]) 	MData.SET.RF_MOD_frq=MData.SET.MinV[ch];
			else 																	MData.SET.RF_MOD_frq+=Jog_Value*w_fact;
			break;
		case 6://LF Frq
			w_fact=1.0f;
			if((MData.SET.LF_MOD.frq+Jog_Value*w_fact)>MData.SET.MaxV[ch])			MData.SET.LF_MOD.frq=MData.SET.MaxV[ch];
			else if((MData.SET.LF_MOD.frq+Jog_Value*w_fact)<MData.SET.MinV[ch]) 	MData.SET.LF_MOD.frq=MData.SET.MinV[ch];
			else 																	MData.SET.LF_MOD.frq+=Jog_Value*w_fact;
			break;
		case 7://LF Amp
			w_fact=0.01f;
			if((MData.SET.LF_MOD.amp+Jog_Value*w_fact)>MData.SET.MaxV[ch])			MData.SET.LF_MOD.amp=MData.SET.MaxV[ch];
			else if((MData.SET.LF_MOD.amp+Jog_Value*w_fact)<MData.SET.MinV[ch]) 	MData.SET.LF_MOD.amp=MData.SET.MinV[ch];
			else 																	MData.SET.LF_MOD.amp+=Jog_Value*w_fact;
			break;
	}

	Jog_Value=0;
}
