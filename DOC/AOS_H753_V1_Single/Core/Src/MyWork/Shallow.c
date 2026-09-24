#define 	__SHALLOW_H__
#include	"Shallow.h"
#undef	__SHALLOW_H__

#include "MyCTL.h"
#include "util.h"
#include "math.h"

#include "main.h"
#include "command.h"


#include "UART_PC.h"
#include "ScanCTL.h"
#include "LF_GEN.h"

u8 IsFound;
void Shallow_Find_Line_Control(){
u8 no;

float sVolt;
float fVolt;

		no = MData.SHA_Line.no;
		if(no==0)
		{
			sVolt = MData.SHA_Line.sVolt;
			fVolt = MData.SHA_Line.fVolt;
		}
		else
		{
			sVolt = MData.SHA_Line.ret_volt[no-1] - 0.3f;
			fVolt = MData.SHA_Line.ret_volt[no-1] + 0.3f;
			if(sVolt<0) sVolt=0.0f;
			if(fVolt>5) fVolt=5.0f;
		}

		MData.SET.Frq = 200.0 + 25*no;
		MData.SET.Duty = MData.SHA_Line.duty;
		Wave_Frq_and_Duty_Update(MData.SET.Frq, MData.SET.Duty);
		delay_ms(100);

		MData.SHA_Line.ret_volt[no] =  Smart_Find_Shallow_Single_Point(sVolt, fVolt, MData.SHA_Line.ref_Is, MData.SHA_Line.wait_delay);
		MData.SHA_Line.Is[no] = MData.SHA_Single.Current;
		MData.SHA_Line.IsFound[no]= MData.SHA_Single.IsFound;
		// Send Data
		TxMessage_CTL_PC(CMD_SHALLOW_LINE_RESULT);
		MData.SHA_Line.no++;

		if(MData.SHA_Line.no==25)
		{
			delay_ms(100);
			MData.SHA_Line.IsStart=false;
			TxMessage_CTL_PC(CMD_SHALLOW_LINE_RESULT_ALL);
			Buzzer_On(500);
		}
		Buzzer_On(20);
}




float Smart_Find_Shallow_Single_Point(float sVolt, float fVolt, u16 refCurrent, u16 delay){
float ret=0.0f;
float volt;
float stepVolt;
u8 err=0;

	MData.SHA_Single.IsFound=false;

	//step 1

	stepVolt = 0.1f;
	volt = sVolt;

	while(1)
	{
		if(MData.SHA_Single.type==0) LF_Volt_Set(volt);
		else {
			if(MData.SHA_Single.IsNegative) CV_Control(volt*(-1.0f));
			else CV_Control(volt);
		}

		MData.SHA_Single.Current = Find_Current_Avg_with_Delay(delay);
		if(MData.SHA_Single.Current < refCurrent)
		{
			fVolt = volt + stepVolt;
			sVolt = volt - stepVolt;


			if(fVolt>5) fVolt=5;
			if(sVolt<0) sVolt=0;
			break;
		}
		else if(volt > fVolt)//error
		{
			err++;
			ret = volt;
			break;
		}
		volt += stepVolt;
	}

	if(err>0) return ret;

	//step 2
	stepVolt = 0.01;
	volt = sVolt;
	while(1)
	{

		if(MData.SHA_Single.type==0) 	LF_Volt_Set(volt);
		else {
			if(MData.SHA_Single.IsNegative) CV_Control(volt*(-1.0f));
			else CV_Control(volt);
		}


		MData.SHA_Single.Current = Find_Current_Avg_with_Delay(delay);

		if(MData.SHA_Single.Current < refCurrent)
		{
			fVolt = volt + stepVolt;
			sVolt = volt - stepVolt;
			if(fVolt>5) fVolt=5;
			if(sVolt<0) sVolt=0;
			break;
		}
		else if(volt > fVolt)//error
		{
			err++;
			ret = volt;
			break;
		}
		volt += stepVolt;
	}

	if(err>0) return ret;


	//step 3
	stepVolt = 0.002;
	volt = sVolt;
	while(1)
	{
		if(MData.SHA_Single.type==0) 	LF_Volt_Set(volt);
		else
		{
			if(MData.SHA_Single.IsNegative) CV_Control(volt*(-1.0f));
			else CV_Control(volt);
		}

		MData.SHA_Single.Current = Find_Current_Avg_with_Delay(delay);
		if(MData.SHA_Single.Current < refCurrent)
		{
			ret = volt - stepVolt/2;
			break;
		}
		else if(volt > fVolt)//error
		{
			err++;
			ret = volt;
			break;
		}
		volt += stepVolt;
	}
	if(err==0) MData.SHA_Single.IsFound=true;
	return ret;
}



void Find_Shallow_Sinlge_Point(){

	MData.SHA_Single.IsStart=false;
	MData.SHA_Single.IsFound=false;

	u16 RefCurrent = MData.SHA_Single.Ref_Is_adc;

	MData.SHA_Single.ret_Volt =  Smart_Find_Shallow_Single_Point(MData.SHA_Single.sVolt, MData.SHA_Single.fVolt, RefCurrent, MData.SHA_Single.wait_delay);

	if(MData.SHA_Single.ret_Volt>=5.0) MData.SHA_Single.ret_Volt=5.0f;
	else if(MData.SHA_Single.ret_Volt <= 0.0f) MData.SHA_Single.ret_Volt=0.0f;
	TxMessage_CTL_PC(CMD_SHALLOW_SINGLE_POINT_FIND);
	Buzzer_On(50);
}



void LF_Volt_Set(float value)
{

	MData.SHA_Single.ret_Volt = value;
	MData.SET.LF_MOD.amp = value;

	LF_Modulator_Voltage_Set();
}




/////////////////////////////////

void FAIMs_Shallow_Set()
{

	MData.SET.RF_HV = MData.Shallow.HV;
	MData.SET.Frq = MData.Shallow.Frq;
	MData.SET.Duty = MData.Shallow.duty;
	MData.SET.CV = MData.Shallow.CV;
	MData.SET.FAN = MData.Shallow.FAN;

	MData.SET.LF_MOD.OnOff = true;
	MData.SET.LF_MOD.type  = MData.Shallow.LF_Type;
	MData.SET.LF_MOD.frq = MData.Shallow.LF_Frq;
	MData.SET.LF_MOD.amp = MData.Shallow.ret_Volt;


	HV_SET_Control(MData.SET.RF_HV);
	Wave_Frq_and_Duty_Update(MData.SET.Frq, MData.SET.Duty);
	CV_Control(MData.SET.CV);
	FAN_Control(MData.SET.FAN);
	LF_Modulator_Set(MData.SET.LF_MOD.type);

}
