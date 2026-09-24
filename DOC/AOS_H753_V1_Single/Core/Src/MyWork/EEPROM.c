#define __EEPROM__
#include "EEPROM.h"
#undef 	__EEPROM__

#include "MyCTL.h"
#include "MyGPIOSet.h"
#include "Util.h"
#define	MDelay 1
/////////////


//////////////////////
void EEPROM_INIT(void)
{

	DO_EEPROM_CS(1);
	DO_EEPROM_SCK(0);
	DO_EEPROM_SDI(0);
}


u8 EEPROM_READY(void){
u8 cmd, i, data,ret;
u8 dat[8];

	cmd = 0x05;        //rdsr opcode

	DO_EEPROM_CS(0);
	for(i=0; i<8; i++) {
		ret=((cmd>>(7-i))&0x01);
		DO_EEPROM_SDI(ret);
		DO_EEPROM_SCK(1);
		delay_us(MDelay);
		DO_EEPROM_SCK(0);
		delay_us(MDelay);
	}

	for(i=0;i<8;i++){
		DO_EEPROM_SCK(1);
		delay_us(MDelay);
		dat[i]=DI_EEPROM_SDO;
		DO_EEPROM_SCK(0);
		delay_us(MDelay);
	}


	DO_EEPROM_CS(1);
	data=dat[0]*128+dat[1]*64+dat[2]*32+dat[3]*16;
	data+=dat[4]*8+dat[5]*4+dat[6]*2+dat[7]*1;

	if(dat[7]==0) 	ret=1;	//REady OK
	else			ret=0;


	return ret;
}

void EEPROM_WRITE_CONTROL(u8 inKey){
u8 i;
u8 cmd;

	if(inKey)	cmd=0x06;	//write enable
	else		cmd=0x04;	//write disable


	DO_EEPROM_CS(0); delay_us(MDelay);
	for(i=0;i<8;i++){
		DO_EEPROM_SDI((cmd>>(7-i))&0x01);
		DO_EEPROM_SCK(1);	delay_us(MDelay);
		DO_EEPROM_SCK(0);	delay_us(MDelay);
	}
	DO_EEPROM_CS(1);

}
void EEPROM_WRITE(u16 add, u8 inDat){
u8 i, cmd;



	while(!EEPROM_READY());

	EEPROM_WRITE_CONTROL(1);
	cmd=0x02;

	DO_EEPROM_CS(0);	delay_us(MDelay);
	for(i=0;i<8;i++){
		DO_EEPROM_SDI((cmd>>(7-i))&0x01);
		DO_EEPROM_SCK(1);	delay_us(MDelay);
		DO_EEPROM_SCK(0);	delay_us(MDelay);
	}

	for(i=0;i<16;i++){
		DO_EEPROM_SDI((add>>(15-i))&0x01);
		DO_EEPROM_SCK(1);	delay_us(MDelay);
		DO_EEPROM_SCK(0);	delay_us(MDelay);
	}

	for(i=0;i<8;i++){
		DO_EEPROM_SDI((inDat>>(7-i))&0x01);
		DO_EEPROM_SCK(1);	delay_us(MDelay);
		DO_EEPROM_SCK(0);	delay_us(MDelay);
	}
	DO_EEPROM_CS(1);
}

u8 EEPROM_READ(u16 add){
u8 i,data,ret;
u8 cmd;


	while(!EEPROM_READY());

	cmd=0x03;

	DO_EEPROM_CS(0);	delay_us(MDelay);
	for(i=0;i<8;i++){
		DO_EEPROM_SDI((cmd>>(7-i))&0x01);
		DO_EEPROM_SCK(1);	delay_us(MDelay);
		DO_EEPROM_SCK(0);	delay_us(MDelay);
	}

	for(i=0;i<16;i++){
		DO_EEPROM_SDI((add>>(15-i))&0x01);
		DO_EEPROM_SCK(1);	delay_us(MDelay);
		DO_EEPROM_SCK(0);	delay_us(MDelay);
	}
	data=0;
	for(i=0;i<8;i++){
		DO_EEPROM_SCK(1);	delay_us(MDelay);
		ret=DI_EEPROM_SDO;
		ret=ret<<(7-i);
		data+=ret;
		DO_EEPROM_SCK(0);	delay_us(MDelay);
	}
	DO_EEPROM_CS(1);
	return data;
}

