
#define 	__LTC2602B_H__
#include	<LTC2602B.h>
#undef		__LTC2602B_H__

#include "MyGPIOSet.h"
#include "Util.h"
#include "main.h"



//#define DO_LTC2602B_CS(OnOff) 	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, (GPIO_PinState)OnOff)


#define DO_LTC2602B_CS(OnOff) 	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, (GPIO_PinState)OnOff)
#define DO_LTC2602B_CLK(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_12, (GPIO_PinState)OnOff)
#define DO_LTC2602B_SDI(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, (GPIO_PinState)OnOff)



void LTC2602B_INIT(void){
	DO_LTC2602B_CS(1);
}

/*
  //SPI Sen
void LTC2602B_COMMAND(u8 Ch,u16 inVal){
u8 spi_tx_buffer[3];

	DO_LTC2602B_CS(0);
	spi_tx_buffer[0] = 0x30 + Ch;//CMD + Ch
	spi_tx_buffer[1] = inVal / 256;
	spi_tx_buffer[2] = inVal % 256;
	SPI1_Data_Send(3, spi_tx_buffer);
	DO_LTC2602B_CS(1);

}

*/



void delay_clk(u32 no)
{
	for(u32 i=0;i<no;i++)
	{

	}
}

void LTC2602B_COMMAND(u8 Ch,u16 inVal){//CH0, CH1
u8 Cmd[4];
u8 Add[4];
u16 inData;
u8 i,ret;
//u8 pD=0;

	inData=inVal;
	if(inData >=65535) inData=65535;
	Cmd[0]=1; Cmd[1]=1; Cmd[2]=0; Cmd[3]=0;
	//pD = 0b1100;
	switch(Ch){
		case 0:
			Add[0]=0; Add[1]=0; Add[2]=0; Add[3]=0;
			//pD = 0b1100 + 0b0000;
			break;
		case 1:
			Add[0]=1; Add[1]=0; Add[2]=0; Add[3]=0;
			//pD = 0b1100 + 0b1000;
			break;
		default:
			Add[0]=1; Add[1]=1; Add[2]=1; Add[3]=1;
			//pD = 0b1100 + 0b1111;
			break;
	}



	//COMMAND SEND
	DO_LTC2602B_CS(0);
	delay_clk(1);

	for(i=0;i<4;i++){
		DO_LTC2602B_SDI(Cmd[3-i]);
		DO_LTC2602B_CLK(1);
		delay_clk(1);
		DO_LTC2602B_CLK(0);
	}

	//ADD SEND
	for(i=0;i<4;i++){
		DO_LTC2602B_SDI(Add[3-i]);
		DO_LTC2602B_CLK(1);
		delay_clk(1);
		DO_LTC2602B_CLK(0);
	}
	//delay_us(2);
	for(i=0;i<16;i++){
		ret=(inData >>(15-i))&0x01;
		DO_LTC2602B_SDI(ret);
		DO_LTC2602B_CLK(1);
		delay_clk(1);
		DO_LTC2602B_CLK(0);
	}

	delay_clk(1);
	DO_LTC2602B_CS(1);

}

