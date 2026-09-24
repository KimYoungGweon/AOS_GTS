#define 	__AD7739_H__
#include	"AD7739.h"
#undef	__AD7739_H__

#include "MyGPIOSet.h"
#include "MyCTL.h"
#include "Util.h"




void ADC7739_Init(void ) {

	u8 i,data;

	DO_AD7739_CS(1);
	DO_AD7739_SCK(1);
	delay_ms(5);

	for(i=0;i<8;i++){
		data=48+i;//0x30 ~ 0x37
		write_adc7739_byte(data);//Communications Register set to write of Conv. time Register
		//Speed Select
		//write_adc7739_byte(0xFF);//Conversion Time Set(3ms)
		//write_adc7739_byte(0xAE);//Conversion Time Set(1ms)
		write_adc7739_byte(0x8A);//Conversion Time Set(0.25ms) //4kHz
		delay_us(100);
		data=40+i;//0x28 ~ 0x2F
		write_adc7739_byte(data);//Communications Register set to write of Channel Setup register
		write_adc7739_byte(0x05);//Communications Register set to write of Channel Setup register
		delay_us(100);
	}

	delay_ms(100);

}


void ADC7739_Init2(void ) {

	u8 i;
	u8 reg;
	DO_AD7739_CS2(1);
	DO_AD7739_SCK2(1);
	delay_ms(5);

	for(i=0;i<8;i++){
		reg=0x30+i;//0x30 ~ 0x37
		write_adc7739_byte2(reg);//Communications Register set to write of Conv. time Register
		delay_us(10);
		//Speed Select
		//write_adc7739_byte(0xFF);//Conversion Time Set(3ms)
		//write_adc7739_byte(0xAE);//Conversion Time Set(1ms)
		write_adc7739_byte2(0x8A);//Conversion Time Set(0.25ms) //4kHz
		delay_us(100);

		//reg = (reg*0xF8) | 0x04;
		//data=40+i;//0x28 ~ 0x2F
		reg=0x28+i;//0x28 ~ 0x2F
		write_adc7739_byte2(reg);//Communications Register set to write of Channel Setup register
		delay_us(10);
		write_adc7739_byte2(0x05);//Range -> 0 to 1.25V, Range : 0 to 2.5V 에서 이상하게 동작함. 그래서 1.25Range에서 사용(임
		delay_us(100);
	}



	u8 MD_ZERO_SCALE_CAL = 0x60;
	u8 MD_FULL_SCALE_CAL = 0x70;

	//zero calibration
	reg=MD_ZERO_SCALE_CAL;
	write_adc7739_byte2(reg);//Communications Register set to write of Conv. time Register
	delay_ms(100);

	reg=MD_FULL_SCALE_CAL;
	write_adc7739_byte2(reg);//Communications Register set to write of Channel Setup register
	delay_ms(100);
}


void ADC7739_Init3(void ) {

	u8 i;
	u8 reg;
	DO_AD7739_CS3(1);
	DO_AD7739_SCK3(1);
	delay_ms(5);

	for(i=0;i<8;i++){
		reg=0x30+i;//0x30 ~ 0x37
		write_adc7739_byte3(reg);//Communications Register set to write of Conv. time Register
		delay_us(10);
		//Speed Select
		write_adc7739_byte(0xFF);//Conversion Time Set(3ms)
		//write_adc7739_byte(0xAE);//Conversion Time Set(1ms)
		//write_adc7739_byte3(0x8A);//Conversion Time Set(0.25ms) //4kHz
		delay_us(100);

		//reg = (reg*0xF8) | 0x04;
		//data=40+i;//0x28 ~ 0x2F
		reg=0x28+i;//0x28 ~ 0x2F
		write_adc7739_byte3(reg);//Communications Register set to write of Channel Setup register
		delay_us(10);
		write_adc7739_byte3(0x05);//Range -> 0 to 1.25V, Range : 0 to 2.5V 에서 이상하게 동작함. 그래서 1.25Range에서 사용(임
		delay_us(100);
	}



	u8 MD_ZERO_SCALE_CAL = 0x60;
	u8 MD_FULL_SCALE_CAL = 0x70;

	//zero calibration
	reg=MD_ZERO_SCALE_CAL;
	write_adc7739_byte3(reg);//Communications Register set to write of Conv. time Register
	delay_ms(100);

	reg=MD_FULL_SCALE_CAL;
	write_adc7739_byte3(reg);//Communications Register set to write of Channel Setup register
	delay_ms(100);
}

u8 read_adc7739_byte(void){	//8Bit Data READ

	u8 i, data;
	DO_AD7739_CS(0);
	data=0;
	for(i=0;i<8;i++) {
		DO_AD7739_SCK(0);
		if(DI_AD7739_SDO) 	data=data+1;
		if(i<7) data=data<<1;
		delay_us(1);
		DO_AD7739_SCK(1);
   	}
	DO_AD7739_CS(1);
	return data;

}

u8 read_adc7739_byte2(void){	//8Bit Data READ

	u8 i, data;
	DO_AD7739_CS2(0);
	data=0;
	for(i=0;i<8;i++) {
		DO_AD7739_SCK2(0);
		if(DI_AD7739_SDO2) 	data=data+1;
		if(i<7) data=data<<1;
		delay_us(1);
		DO_AD7739_SCK2(1);
   	}
	DO_AD7739_CS2(1);
	return data;

}

u8 read_adc7739_byte3(void){	//8Bit Data READ

	u8 i, data;
	DO_AD7739_CS3(0);
	data=0;
	for(i=0;i<8;i++) {
		DO_AD7739_SCK3(0);
		if(DI_AD7739_SDO3) 	data=data+1;
		if(i<7) data=data<<1;
		delay_us(1);
		DO_AD7739_SCK3(1);
   	}
	DO_AD7739_CS3(1);
	return data;

}

u16 read_adc7739_word(void){	//16Bit Data READ


	u8 i;
	u16 data;

	DO_AD7739_CS(0);
	data=0;
	for(i=0;i<16;i++) {
		DO_AD7739_SCK(0);
		if(DI_AD7739_SDO) data=data+1;
		if(i<15)	data=data<<1;
		delay_us(1);
		DO_AD7739_SCK(1);
   	}

	DO_AD7739_CS(1);
	return data;


}


u16 read_adc7739_word2(void){	//16Bit Data READ


	u8 i;
	u16 data;

	DO_AD7739_CS2(0);
	data=0;
	for(i=0;i<16;i++) {
		DO_AD7739_SCK2(0);
		if(DI_AD7739_SDO2) data=data+1;
		if(i<15)	data=data<<1;
		delay_us(1);
		DO_AD7739_SCK2(1);
   	}

	DO_AD7739_CS2(1);
	return data;


}


u16 read_adc7739_word3(void){	//16Bit Data READ


	u8 i;
	u16 data;

	DO_AD7739_CS3(0);
	data=0;
	for(i=0;i<16;i++) {
		DO_AD7739_SCK3(0);
		if(DI_AD7739_SDO3) data=data+1;
		if(i<15)	data=data<<1;
		delay_us(1);
		DO_AD7739_SCK3(1);
   	}

	DO_AD7739_CS3(1);
	return data;


}



void write_adc7739_byte(u8 data){


	u8 i,Od;
	DO_AD7739_CS(0);
	for(i=0;i<8;i++) {
		DO_AD7739_SCK(0);
		delay_us(1);
		Od=(data>>(7-i))&0x01;	//MSB to LSB
		DO_AD7739_SDI(Od);
		DO_AD7739_SCK(1);
	}
	DO_AD7739_CS(1);
	delay_us(2);
}



void write_adc7739_byte2(u8 data){


	u8 i,Od;
	DO_AD7739_CS2(0);
	for(i=0;i<8;i++) {
		DO_AD7739_SCK2(0);
		delay_us(1);
		Od=(data>>(7-i))&0x01;	//MSB to LSB
		DO_AD7739_SDI2(Od);
		DO_AD7739_SCK2(1);
	}
	DO_AD7739_CS2(1);
	delay_us(2);
}

void write_adc7739_byte3(u8 data){


	u8 i,Od;
	DO_AD7739_CS3(0);
	for(i=0;i<8;i++) {
		DO_AD7739_SCK3(0);
		delay_us(1);
		Od=(data>>(7-i))&0x01;	//MSB to LSB
		DO_AD7739_SDI3(Od);
		DO_AD7739_SCK3(1);
	}
	DO_AD7739_CS3(1);
	delay_us(2);
}




u16 AD7739_READ(u8 CH){

	u16 AD7739R;
	u8 data;
	u16 RetValue;
	u8 RetStatus;
	u8 OVR, PSIGN;
	u16 Dcnt;

	OVR=0;
	PSIGN=0;

	data=0x38+CH; //0x38(56) ~ 0x3F(CH0 to CH7)
	write_adc7739_byte(data);	//Communication Register to Mode
	//write_adc7739_byte(DS,0x40);	//Mode Register Set
	write_adc7739_byte(0x48);	//Mode Register Set(Dump Mode for status read);



	Dcnt=0;
	while(1){
		if(DI_AD7739_RDY == 0) break;//Ready Port
		delay_us(10);
		Dcnt+=10;
		if(Dcnt>5000){//About 1msec Conversion Time
			return 0;//Error

		}
	}


	data=0x48+CH;		//0x48(72) ~ 0x4F(CH0 to CH7)
	write_adc7739_byte(data);	//Communication Register to READ


//---------- READ STATUS ____________//
	RetStatus=read_adc7739_byte( );
	OVR=RetStatus %2; //7th Bit(LSB)
	RetStatus=RetStatus/2;
	PSIGN=RetStatus %2;	//6th Bit
//______________________________________//

	RetValue=read_adc7739_word( );
	AD7739R=(u16)RetValue;

	if(OVR) AD7739R=65535; //Over Ranage +2.5V
	if(PSIGN) AD7739R=0; //Negative
	return AD7739R;
}





u16 AD7739_READ2(u8 CH){

u16 AD7739R;
u8 data;
u16 RetValue;
u8 RetStatus;
u8 OVR, PSIGN;
u16 Dcnt;

	OVR=0;
	PSIGN=0;

	data=0x38+CH; //0x38(56) ~ 0x3F(CH0 to CH7)
	write_adc7739_byte2(data);	//Communication Register to Mode
	//write_adc7739_byte(DS,0x40);	//Mode Register Set
	write_adc7739_byte2(0x48);	//Mode Register Set(Dump Mode for status read);



	Dcnt=0;
	while(1){
		if(DI_AD7739_RDY2 == 0) break;//Ready Port
		delay_us(10);
		Dcnt+=10;
		if(Dcnt>5000){//About 1msec Conversion Time
			return 0;//Error

		}
	}


	data=0x48+CH;		//0x48(72) ~ 0x4F(CH0 to CH7)
	write_adc7739_byte2(data);	//Communication Register to READ


//---------- READ STATUS ____________//
	RetStatus=read_adc7739_byte2( );
	OVR=RetStatus %2; //7th Bit(LSB)
	RetStatus=RetStatus/2;
	PSIGN=RetStatus %2;	//6th Bit
//______________________________________//

	RetValue=read_adc7739_word2( );
	AD7739R=(u16)RetValue;

	if(OVR) AD7739R=65535; //Over Ranage +2.5V
	if(PSIGN) AD7739R=0; //Negative
	return AD7739R;
}



u16 AD7739_READ3(u8 CH){

u16 AD7739R;
u8 data;
u16 RetValue;
u8 RetStatus;
u8 OVR, PSIGN;
u16 Dcnt;

	OVR=0;
	PSIGN=0;

	data=0x38+CH; //0x38(56) ~ 0x3F(CH0 to CH7)
	write_adc7739_byte3(data);	//Communication Register to Mode
	//write_adc7739_byte(DS,0x40);	//Mode Register Set
	write_adc7739_byte3(0x48);	//Mode Register Set(Dump Mode for status read);



	Dcnt=0;
	while(1){
		if(DI_AD7739_RDY3 == 0) break;//Ready Port
		delay_us(10);
		Dcnt+=10;
		if(Dcnt>5000){//About 1msec Conversion Time
			return 0;//Error

		}
	}



	data=0x48+CH;		//0x48(72) ~ 0x4F(CH0 to CH7)
	write_adc7739_byte3(data);	//Communication Register to READ


//---------- READ STATUS ____________//
	RetStatus=read_adc7739_byte3( );
	OVR=RetStatus %2; //7th Bit(LSB)
	RetStatus=RetStatus/2;
	PSIGN=RetStatus %2;	//6th Bit
//______________________________________//

	RetValue=read_adc7739_word3( );
	AD7739R=(u16)RetValue;

	if(OVR) AD7739R=65535; //Over Ranage +2.5V
	if(PSIGN) AD7739R=0; //Negative
	return AD7739R;
}

