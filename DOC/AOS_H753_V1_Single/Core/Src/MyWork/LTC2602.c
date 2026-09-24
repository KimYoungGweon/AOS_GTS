#define 	__LTC2602_H__
#include	"LTC2602.h"
#undef	__LTC2602_H__

#include "MyGPIOSet.h"
#include "Util.h"


void LTC2602_INIT(void){
	DO_LTC2602_CS(1);
	DO_LTC2602_CLK(0);
	DO_LTC2602_SDI(0);
}

void LTC2602_COMMAND(u8 Ch,u16 inVal){//CH0, CH1
u8 Cmd[4];
u8 Add[4];
u16 inData;
u8 i,ret;

	inData=inVal;
	if(inData >=65535) inData=65535;
	Cmd[0]=1; Cmd[1]=1; Cmd[2]=0; Cmd[3]=0;

	switch(Ch){
		case 0:
			Add[0]=0; Add[1]=0; Add[2]=0; Add[3]=0;
			break;
		case 1:
			Add[0]=1; Add[1]=0; Add[2]=0; Add[3]=0;
			break;
		default:
			Add[0]=1; Add[1]=1; Add[2]=1; Add[3]=1;
			break;
	}

	//COMMAND SEND
	DO_LTC2602_CS(0);
	delay_us(1);

	for(i=0;i<4;i++){
		DO_LTC2602_SDI(Cmd[3-i]);
		DO_LTC2602_CLK(1);
		delay_us(2);
		DO_LTC2602_CLK(0);
	}
	delay_us(1);
	//ADD SEND
	for(i=0;i<4;i++){
		DO_LTC2602_SDI(Add[3-i]);
		DO_LTC2602_CLK(1);
		delay_us(2);
		DO_LTC2602_CLK(0);
	}
	delay_us(1);
	for(i=0;i<16;i++){
		ret=(inData >>(15-i))&0x01;
		DO_LTC2602_SDI(ret);
		DO_LTC2602_CLK(1);
		delay_us(2);
		DO_LTC2602_CLK(0);
	}
	delay_us(1);
	DO_LTC2602_CS(1);
}
