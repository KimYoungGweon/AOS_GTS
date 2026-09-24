#define __DEBUG_H__
#include	"debug.h"
#undef	__DEBUG_H__


#include "MyCTL.h"



void debug_ctl(){
/*
	u16 CV_test_u16=32768;
	u16 CV_test_u16_OLD=32768;
	u8 tGAIN=0;
	u16 adc_test[3];


	switch(MData.debug.FLAG)
	{
		case 0x01:
			CV_Control(MData.CV);
			break;

		case 0x02://SetFrqeuency
			break;
		case 0x05:
			LTC2602_COMMAND(0, CV_test_u16);
			break;
		case 0x06:
			Bias_Selection(0);
			break;
		case 0x07:
			Bias_Selection(1);
			break;
		case 0x08:
			CURRENT_GAIN_SET(tGAIN);
			break;
		case 0x09:
			//u32 fValue = 480000;
			//CAL_FRQ_Save();
			break;

	}
*/
	MData.debug.FLAG=0x00;
}

