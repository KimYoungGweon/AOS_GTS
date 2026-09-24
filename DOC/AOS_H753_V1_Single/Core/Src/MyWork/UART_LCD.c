#define _LCD_CTL_H_
#include "UART_LCD.h"
#undef _LCD_CTL_H_

#include "MyCTL.h"
#include "main.h"
#include "util.h"

void LCD_PAGE_CHANGE(u8 page){
char cOut[64];
	sprintf(cOut,"page %d",page);
	TxMSG_DISP(strlen(cOut), cOut);
}


void LCD_VERSION_VIEW(){
char cOut[64];
	sprintf(cOut, "t0.txt=\"%s\"","0.2.0");
	TxMSG_DISP(strlen(cOut), cOut);
}

void LCD_MODE_Set(u8 type){
char cOut[64];

	switch(type)
	{
		case 0:
			//enable
			sprintf(cOut,"t6.pco=%d", 65504);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);


			sprintf(cOut,"tLF_MOD_Frq.pco=%d", 2016);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tLF_MOD_Amp.pco=%d", 2016);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tLF_MOD_Type.pco=%d", 2016);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tLF_MOD_OnOff.pco=%d", 2016);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			//disable
			sprintf(cOut,"t4.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tRF_MOD_Frq.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tRF_MOD_OnOff.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);
			break;
		case 1:
			//disable
			sprintf(cOut,"t6.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);


			sprintf(cOut,"tLF_MOD_Frq.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tLF_MOD_Amp.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tLF_MOD_Type.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tLF_MOD_OnOff.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			//enable
			sprintf(cOut,"t4.pco=%d", 65504);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tRF_MOD_Frq.pco=%d", 2016);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tRF_MOD_OnOff.pco=%d", 2016);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);
			break;
		case 2:
			sprintf(cOut,"t6.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);


			sprintf(cOut,"tLF_MOD_Frq.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tLF_MOD_Amp.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tLF_MOD_Type.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tLF_MOD_OnOff.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			//disable
			sprintf(cOut,"t4.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tRF_MOD_Frq.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);

			sprintf(cOut,"tRF_MOD_OnOff.pco=%d", 33808);
			TxMSG_DISP(strlen(cOut), cOut);			delay_ms(50);
			break;
	}


}


void LCD_Weight(u8 weight){
char cOut[64];

	switch(MData.SET.ch)
	{
		case eLCD_HV://HV
			switch(weight)
			{
				case 0: sprintf(cOut, "tW.txt=\"HV Set    0.1V\""); break; //RF-HV -> HV
				case 1: sprintf(cOut, "tW.txt=\"HV Set      1V\""); break;
				case 2: sprintf(cOut, "tW.txt=\"HV Set      5V\""); break;
			}
			break;

		case eLCD_FRQ://Frq
			switch(weight)
			{
				case 0: sprintf(cOut, "tW.txt=\"Frq Set      0.5kHz\""); break;
				case 1: sprintf(cOut, "tW.txt=\"Frq Set      1kHz\""); break;
				case 2: sprintf(cOut, "tW.txt=\"Frq Set      2kHz\""); break;
				case 3: sprintf(cOut, "tW.txt=\"Frq Set      5kHz\""); break;
				case 4: sprintf(cOut, "tW.txt=\"Frq Set      10kHz\""); break;
			}
			break;

		case eLCD_DUTY://Duty
			switch(weight)
			{
				case 0: sprintf(cOut, "tW.txt=\"Duty Set     0.1%%\""); break;
				case 1: sprintf(cOut, "tW.txt=\"Duty Set     0.5%%\""); break;
				case 2: sprintf(cOut, "tW.txt=\"Duty Set     1.0%%\""); break;
			}
			break;

		case eLCD_CV://CV
			switch(weight)
			{
				case 0: sprintf(cOut, "tW.txt=\"CV Set       0.001V\""); break;
				case 1: sprintf(cOut, "tW.txt=\"CV Set       0.01V\""); break;
				case 2: sprintf(cOut, "tW.txt=\"CV Set       0.05V\""); break;
			}
			break;

		case eLCD_FAN://FAN SET
			sprintf(cOut, "tW.txt=\"FAN Set     0.1V\"");
			break;

		case eLCD_HF_MOD://RF MOD Frq
			switch(weight)
			{
				case 0: sprintf(cOut, "tW.txt=\"RF MOD Frq   0.001kHz\""); break;
				case 1: sprintf(cOut, "tW.txt=\"RF MOD Frq   0.010kHz\""); break;
				case 2: sprintf(cOut, "tW.txt=\"RF MOD Frq   0.100kHz\""); break;
				case 3: sprintf(cOut, "tW.txt=\"RF MOD Frq   1.000kHz\""); break;
				case 4: sprintf(cOut, "tW.txt=\"RF MOD Frq   10.00kHz\""); break;
				case 5: sprintf(cOut, "tW.txt=\"RF MOD Frq  100.00kHz\""); break;
			}
			break;

		case eLCD_LF_MOD://LF MOD frq
			sprintf(cOut, "tW.txt=\"LF MOD Frq   1Hz\"");
			break;

		case eLCD_LF_MOD_AMP://LF MOD AMP

			switch(weight)
			{
				case 0: sprintf(cOut, "tW.txt=\"LF MOD Amp   0.01V\""); break;
				case 1: sprintf(cOut, "tW.txt=\"LF MOD Amp    0.1V\""); break;
			}
			break;
	}

	TxMSG_DISP(strlen(cOut), cOut);
}


////display Control
void LCD_Data_View_All( )
{

	for(u8 i=0;i<9;i++)
	{
		LCD_Data_View(i);
		delay_ms(20);
	}
	LCD_Weight(MData.SET.Jog_W[MData.SET.ch]);


	//Text_Color_Change(0, 1);

}




void LCD_Data_View(u8 type){
char cOut[64];

	switch(type)
	{

		case eLCD_HV:	MData.SET.RF_HV_Disp = MData.SET.RF_HV;
						sprintf(cOut, "tHV_Set.txt=\"%3.1f\"",MData.SET.RF_HV);	break;
		case eLCD_FRQ:	sprintf(cOut, "tFRQ.txt=\"%3.1fkHz\"",MData.SET.Frq);	break;
		case eLCD_DUTY:	sprintf(cOut, "tDuty.txt=\"%3.1f%%\"",MData.SET.Duty);	break;
		case eLCD_CV:	sprintf(cOut, "tCV.txt=\"%1.3fV\"",MData.SET.CV);		break;

		//case eLCD_FAN:	sprintf(cOut, "tFAN.txt=\"%3.1f(%3.1f)V\"",MData.SET.FAN, MData.SENSE.FAN_Vs);break;
		case eLCD_FAN:	sprintf(cOut, "tFAN.txt=\"%3.1fV\"", MData.SENSE.FAN_Vs);break;

		case eLCD_HF_MOD:
			sprintf(cOut, "tRF_MOD_Frq.txt=\"%4.3f\"",MData.SET.RF_MOD_frq);
			TxMSG_DISP(strlen(cOut), cOut);
			delay_ms(50);
			if(MData.SET.RF_MOD_OnOff)	sprintf(cOut, "tRF_MOD_OnOff.txt=\"On\"");
			else 						sprintf(cOut, "tRF_MOD_OnOff.txt=\"Off\"");
			break;

		case eLCD_LF_MOD:
			sprintf(cOut, "tLF_MOD_Amp.txt=\"%2.2fV\"",MData.SET.LF_MOD.amp);
			TxMSG_DISP(strlen(cOut), cOut);	delay_ms(50);

			sprintf(cOut, "tLF_MOD_Frq.txt=\"%3.0fHz\"",MData.SET.LF_MOD.frq);
			TxMSG_DISP(strlen(cOut), cOut);	delay_ms(50);

			switch(MData.SET.LF_MOD.type)
			{
				case 0:sprintf(cOut, "tLF_MOD_Type.txt=\"Triangle\""); break;
				case 1:sprintf(cOut, "tLF_MOD_Type.txt=\"Ramp\""); break;
				case 2:sprintf(cOut, "tLF_MOD_Type.txt=\"Ramp(1:9)\""); break;
				case 3:sprintf(cOut, "tLF_MOD_Type.txt=\"Ramp(2:8)\""); break;
				case 4:sprintf(cOut, "tLF_MOD_Type.txt=\"Ramp(3:7)\""); break;
				case 5:sprintf(cOut, "tLF_MOD_Type.txt=\"Sine\""); break;
				case 6:sprintf(cOut, "tLF_MOD_Type.txt=\"Square\""); break;
				case 7:sprintf(cOut, "tLF_MOD_Type.txt=\"TPZ(0.5:9.5)\""); break;
				case 8:sprintf(cOut, "tLF_MOD_Type.txt=\"TPZ(1:9)\""); break;
				case 9:sprintf(cOut, "tLF_MOD_Type.txt=\"TPZ(2:8)\""); break;
				case 10:sprintf(cOut, "tLF_MOD_Type.txt=\"TPZ(3:7)\""); break;
			}
			TxMSG_DISP(strlen(cOut), cOut);	delay_ms(50);

			if(MData.SET.LF_MOD.OnOff) 	sprintf(cOut, "tLF_MOD_OnOff.txt=\"On\"");
			else					 	sprintf(cOut, "tLF_MOD_OnOff.txt=\"Off\"");
			break;

		case eLCD_HV_VS:
			if(MData.Cal_Mode) 	sprintf(cOut, "tHV_Vs.txt=\"%d\"",MData.SENSE.HV_Vs_adc);
			else				sprintf(cOut, "tHV_Vs.txt=\"%3.1f\"",MData.SENSE.HV_Vs);
			break;
		case eLCD_BIAS_VS:
			if(MData.Cal_Mode) 	sprintf(cOut, "tBIAS.txt=\"%d\"",MData.SENSE.ION_Bias_Vs_adc);
			else				sprintf(cOut, "tBIAS.txt=\"%3.1f\"",MData.SENSE.ION_Bias_Vs);
			break;
	}
	TxMSG_DISP(strlen(cOut), cOut);
}



void Text_Color_Change(u8 ch, u8 value){
char cOut[64];
u16 pVal;

	if(value==1)	pVal = 65504;
	else			pVal = 2024;

	switch(ch)
	{
		case 0: sprintf(cOut,"tCV.pco=%d", pVal);		break;
		case 1: sprintf(cOut,"tHV_Set.pco=%d",pVal);	break;
		case 2: sprintf(cOut,"tFRQ.pco=%d", pVal);		break;
		case 3: sprintf(cOut,"tDUTY.pco=%d", pVal);		break;
	}

	TxMSG_DISP(strlen(cOut), cOut);
}


void LCD_ERROR( )
{


}




void TxMSG_DISP(u8 size, char *buf){
u8 cOUT[3];
u8 data[255];

	for(u8 i=0;i<3;i++) cOUT[i] = 0xff;
	for(u8 i=0;i<size;i++) data[i] = (u8)buf[i];

	UART_Data_Send_LCD(size, data);
	UART_Data_Send_LCD(3, cOUT);
}
