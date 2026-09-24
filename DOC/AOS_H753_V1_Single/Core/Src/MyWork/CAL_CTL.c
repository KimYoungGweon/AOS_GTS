#define _CAL_CTL_H_
#include "CAL_CTL.h"
#undef _CAL_CTL_H_
#include "MyCTL.h"

#include "EEPROM.h"

#define FRQ_CAL_ADDRESS 0x0010
#define HV_DAC_ADDRESS 	0x0200
#define HV_VS_ADDRESS 	0x0600
#define HV_IS_ADDRESS 	0x0800
#define CV_DAC_ADDRESS 	0x0A00
#define FAN_VS_ADDRESS 	0x0C00
#define BIAS_VS_ADDRESS 0x0D00

#define SHALLOW_2D_ADDRESS 	0x1000


void CAL_FRQ_Save(u32 frq){
u16 add;
u8 inB[4];

	add=FRQ_CAL_ADDRESS;
	memcpy(&inB,&frq,4);
	for(u8 i=0;i<4;i++) EEPROM_WRITE(add++, inB[i]);
}

u32 CAL_FRQ_Read( ){
u16 add=0;
u32 ret;
u8 inB[4];

	add=FRQ_CAL_ADDRESS;
	for(u8 i=0;i<4;i++) inB[i] = EEPROM_READ(add++);
	memcpy(&ret, &inB, 4);

	return ret;
}

void CAL_Data_Write(u8 type){
u8 inB[4];
u16 add;
	switch(type)
	{
		case 0:
			add =  HV_DAC_ADDRESS;
			EEPROM_WRITE(add++, CSET.No); //DAC
			for(u8 i=0;i<CSET.No;i++)
			{
				memcpy(&inB,&CSET.X[i],4);
				for(u8 j=0;j<4;j++) EEPROM_WRITE(add++, inB[j]);

				memcpy(&inB,&CSET.Y[i],2);
				for(u8 j=0;j<2;j++) EEPROM_WRITE(add++, inB[j]);
			}
			break;
		case 1:
			add =  HV_VS_ADDRESS;
			EEPROM_WRITE(add++, CSENSE_VS.No); //DAC
			for(u8 i=0;i<CSENSE_VS.No;i++)
			{
				memcpy(&inB,&CSENSE_VS.X[i],2);
				for(u8 j=0;j<2;j++) EEPROM_WRITE(add++, inB[j]);

				memcpy(&inB,&CSENSE_VS.Y[i],4);
				for(u8 j=0;j<4;j++) EEPROM_WRITE(add++, inB[j]);
			}
			break;
		case 2:
			add =  HV_IS_ADDRESS;
			EEPROM_WRITE(add++, CSENSE_IS.No); //DAC
			for(u8 i=0;i<CSENSE_IS.No;i++)
			{
				memcpy(&inB,&CSENSE_IS.X[i],2);
				for(u8 j=0;j<2;j++) EEPROM_WRITE(add++, inB[j]);

				memcpy(&inB,&CSENSE_IS.Y[i],4);
				for(u8 j=0;j<4;j++) EEPROM_WRITE(add++, inB[j]);
			}
			break;
		case 3:
			add =  FAN_VS_ADDRESS;
			EEPROM_WRITE(add++, CSENSE_FAN_VS.No); //DAC
			for(u8 i=0;i<CSENSE_FAN_VS.No;i++)
			{
				memcpy(&inB,&CSENSE_FAN_VS.X[i],2);
				for(u8 j=0;j<2;j++) EEPROM_WRITE(add++, inB[j]);

				memcpy(&inB,&CSENSE_FAN_VS.Y[i],4);
				for(u8 j=0;j<4;j++) EEPROM_WRITE(add++, inB[j]);
			}
			break;
		case 4:
			add =  CV_DAC_ADDRESS;
			EEPROM_WRITE(add++, CSET_CV.No); //DAC
			for(u8 i=0;i<CSET_CV.No;i++)
			{
				memcpy(&inB,&CSET_CV.X[i],4);
				for(u8 j=0;j<4;j++) EEPROM_WRITE(add++, inB[j]) ;

				memcpy(&inB,&CSET_CV.Y[i],2);
				for(u8 j=0;j<2;j++) EEPROM_WRITE(add++, inB[j]) ;
			}
		case 5:
			add =  BIAS_VS_ADDRESS;
			EEPROM_WRITE(add++,  CSENSE_ION_BIAS_VS.No);
			for(u8 i=0;i<CSENSE_ION_BIAS_VS.No;i++)
			{
				memcpy(&inB,&CSENSE_ION_BIAS_VS.X[i],2);
				for(u8 j=0;j<2;j++) EEPROM_WRITE(add++, inB[j]);

				memcpy(&inB,&CSENSE_ION_BIAS_VS.Y[i],4);
				for(u8 j=0;j<4;j++) EEPROM_WRITE(add++, inB[j]);
			}
			break;
	}
}

void CAL_Data_Read(u8 type){
u16 add;
u8 inB[4];

	switch(type)
	{
		case 0:
			add =  HV_DAC_ADDRESS;
			CSET.No = EEPROM_READ(add++);
			for(u8 i=0;i<CSET.No;i++)
			{
				for(u8 j=0;j<4;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSET.X[i], &inB, 4);


				for(u8 j=0;j<2;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSET.Y[i], &inB, 2);
			}
			break;
		case 1:
			add =  HV_VS_ADDRESS;
			CSENSE_VS.No = EEPROM_READ(add++);
			for(u8 i=0;i<CSENSE_VS.No;i++)
			{
				for(u8 j=0;j<2;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSENSE_VS.X[i], &inB, 2);


				for(u8 j=0;j<4;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSENSE_VS.Y[i], &inB, 4);
			}
			break;
		case 2:
			add =  HV_IS_ADDRESS;
			CSENSE_IS.No = EEPROM_READ(add++);
			for(u8 i=0;i<CSENSE_IS.No;i++)
			{
				for(u8 j=0;j<2;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSENSE_IS.X[i], &inB, 2);


				for(u8 j=0;j<4;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSENSE_IS.Y[i], &inB, 4);
			}
			break;
		case 3:
			add =  FAN_VS_ADDRESS;
			CSENSE_FAN_VS.No = EEPROM_READ(add++);
			for(u8 i=0;i<CSENSE_FAN_VS.No;i++)
			{
				for(u8 j=0;j<2;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSENSE_FAN_VS.X[i], &inB, 2);


				for(u8 j=0;j<4;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSENSE_FAN_VS.Y[i], &inB, 4);
			}
			break;
		case 4:
			add =  CV_DAC_ADDRESS;
			CSET_CV.No = EEPROM_READ(add++);
			for(u8 i=0;i<CSET_CV.No;i++)
			{
				for(u8 j=0;j<4;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSET_CV.X[i], &inB, 4);


				for(u8 j=0;j<2;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSET_CV.Y[i], &inB, 2);
			}
			break;
		case 5:
			add =  BIAS_VS_ADDRESS;
			CSENSE_ION_BIAS_VS.No = EEPROM_READ(add++);
			for(u8 i=0;i<CSENSE_ION_BIAS_VS.No;i++)
			{
				for(u8 j=0;j<2;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSENSE_ION_BIAS_VS.X[i], &inB, 2);


				for(u8 j=0;j<4;j++) inB[j] = EEPROM_READ(add++);
				memcpy(&CSENSE_ION_BIAS_VS.Y[i], &inB, 4);
			}
			break;
	}
}



////////// Cal. 수정코드


void Set_Cal_Default_Factor_DAC_HV( ){
u16 add=0;
	//CSET.X[add] =0;		CSET.Y[add] = 0;		add++;
	//CSET.X[add] =600.0;	CSET.Y[add] = 65535;	add++;

	switch(MData.SN)
	{
		case 0:	//KBSI 2025-12-24
			CSET.X[add] = 40.7;			CSET.Y[add] = 0;			add++;//minvalue
			CSET.X[add] = 42.3;			CSET.Y[add] = 4478;			add++;
			CSET.X[add] = 44.4;			CSET.Y[add] = 4696;			add++;
			CSET.X[add] = 49.5;			CSET.Y[add] = 5251;			add++;
			CSET.X[add] = 75.5;			CSET.Y[add] = 8223;			add++;
			CSET.X[add] = 99.4;			CSET.Y[add] = 11007;		add++;
			CSET.X[add] = 149.7;		CSET.Y[add] = 16866;		add++;
			CSET.X[add] = 200.0;		CSET.Y[add] = 22842;		add++;
			CSET.X[add] = 249.0;		CSET.Y[add] = 28499;		add++;
			CSET.X[add] = 303.0;		CSET.Y[add] = 34834;		add++;
			CSET.X[add] = 352.0;		CSET.Y[add] = 40515;		add++;
			CSET.X[add] = 400.0;		CSET.Y[add] = 46192;		add++;
			CSET.X[add] = 450.0;		CSET.Y[add] = 52082;		add++;
			CSET.X[add] = 500.0;		CSET.Y[add] = 57882;		add++;
			CSET.X[add] = 520.0;		CSET.Y[add] = 60195;		add++;
			CSET.X[add] = 535.0;		CSET.Y[add] = 61126;		add++;
			CSET.X[add] = 551.0;		CSET.Y[add] = 61358;		add++;
			CSET.No = add;
			break;

		case 1:	//FTLAB V1.6.0-1 2026-01-04
			CSET.X[add] = 43.9;			CSET.Y[add] = 0;			add++;//minvalue
			CSET.X[add] = 49.9;			CSET.Y[add] = 4978;			add++;
			CSET.X[add] = 75.0;			CSET.Y[add] = 7668;			add++;
			CSET.X[add] = 99.4;			CSET.Y[add] = 10351;		add++;
			CSET.X[add] = 149.3;		CSET.Y[add] = 15852;		add++;
			CSET.X[add] = 199.3;		CSET.Y[add] = 21364;		add++;
			CSET.X[add] = 250.0;		CSET.Y[add] = 27077;		add++;
			CSET.X[add] = 300.0;		CSET.Y[add] = 32575;		add++;
			CSET.X[add] = 350.0;		CSET.Y[add] = 38195;		add++;
			CSET.X[add] = 400.0;		CSET.Y[add] = 43772;		add++;
			CSET.X[add] = 450.0;		CSET.Y[add] = 49382;		add++;
			CSET.X[add] = 500.0;		CSET.Y[add] = 54977;		add++;
			CSET.X[add] = 520.0;		CSET.Y[add] = 57301;		add++;
			CSET.X[add] = 543.0;		CSET.Y[add] = 60121;		add++; //Max Voltage Check 요망...
			CSET.X[add] = 549.0;		CSET.Y[add] = 61927;		add++;
			CSET.X[add] = 560.0;		CSET.Y[add] = 65535;		add++;
			CSET.No = add;
			break;
		case 2:	//FTLAB V1.7.0-1 2026-02-04
		case 3:	//FTLAB V1.7.0-1 2026-02-04
			CSET.X[add] = 0;			CSET.Y[add] = 0;			add++;//minvalue
			CSET.X[add] = 51.2;			CSET.Y[add] = 1;			add++;
			CSET.X[add] = 52.4;			CSET.Y[add] = 4726;			add++;
			CSET.X[add] = 75.3;			CSET.Y[add] = 7022;			add++;
			CSET.X[add] = 100.5;		CSET.Y[add] = 9562;			add++;
			CSET.X[add] = 150.0;		CSET.Y[add] = 14571;		add++;
			CSET.X[add] = 199.8;		CSET.Y[add] = 19634;		add++;
			CSET.X[add] = 250.0;		CSET.Y[add] = 24804;		add++;
			CSET.X[add] = 300.0;		CSET.Y[add] = 29906;		add++;
			CSET.X[add] = 351.0;		CSET.Y[add] = 35065;		add++;
			CSET.X[add] = 400.0;		CSET.Y[add] = 40167;		add++;
			CSET.X[add] = 450.0;		CSET.Y[add] = 45245;		add++;
			CSET.X[add] = 500.0;		CSET.Y[add] = 50315;		add++;
			CSET.X[add] = 525.0;		CSET.Y[add] = 52894;		add++;
			CSET.X[add] = 550.0;		CSET.Y[add] = 55446;		add++; //Max Voltage Check 요망...
			CSET.X[add] = 560.0;		CSET.Y[add] = 56490;		add++;
			CSET.No = add;
			break;
	}


}


void Set_Cal_Default_Factor_ADC_VS_HV( ){
u16 add=0;

	//CSENSE_VS.No=2;
	//CSENSE_VS.X[0]=0;		CSENSE_VS.Y[0]=0.0;
	//CSENSE_VS.X[1]=65535;	CSENSE_VS.Y[1]=600.0f;

	switch(MData.SN)
	{
		case 0:	//KBSI 2025.12.25
			CSENSE_VS.X[add]=0;			CSENSE_VS.Y[add]=0; 		add++;
			CSENSE_VS.X[add]=330;		CSENSE_VS.Y[add]=42.0; 		add++;
			CSENSE_VS.X[add]=385;		CSENSE_VS.Y[add]=44.5; 		add++;
			CSENSE_VS.X[add]=500;		CSENSE_VS.Y[add]=49.9; 		add++;
			CSENSE_VS.X[add]=1070;		CSENSE_VS.Y[add]=75.2; 		add++;
			CSENSE_VS.X[add]=1623;		CSENSE_VS.Y[add]=100.0;		add++;
			CSENSE_VS.X[add]=2752;		CSENSE_VS.Y[add]=150.0; 	add++;
			CSENSE_VS.X[add]=3910;		CSENSE_VS.Y[add]=200.0; 	add++;
			CSENSE_VS.X[add]=6240;		CSENSE_VS.Y[add]=303.0; 	add++;
			CSENSE_VS.X[add]=7330;		CSENSE_VS.Y[add]=352.0; 	add++;
			CSENSE_VS.X[add]=8454;		CSENSE_VS.Y[add]=400.0; 	add++;
			CSENSE_VS.X[add]=9600;		CSENSE_VS.Y[add]=450.0; 	add++;
			CSENSE_VS.X[add]=10725;		CSENSE_VS.Y[add]=500.0; 	add++;
			CSENSE_VS.X[add]=11192;		CSENSE_VS.Y[add]=520.0; 	add++;
			CSENSE_VS.X[add]=11540;		CSENSE_VS.Y[add]=535.0; 	add++;
			CSENSE_VS.X[add]=11900;		CSENSE_VS.Y[add]=551.0; 	add++;
			CSENSE_VS.No=add;
			break;
		case 1:	//FTLAB V1.6.0-1 2026.01.05
			CSENSE_VS.X[add]=0;			CSENSE_VS.Y[add]=0; 		add++;
			CSENSE_VS.X[add]=286;		CSENSE_VS.Y[add]=43.9; 		add++;
			CSENSE_VS.X[add]=412;		CSENSE_VS.Y[add]=49.9; 		add++;
			CSENSE_VS.X[add]=940;		CSENSE_VS.Y[add]=75.0; 		add++;
			CSENSE_VS.X[add]=1462;		CSENSE_VS.Y[add]=99.4;		add++;
			CSENSE_VS.X[add]=2534;		CSENSE_VS.Y[add]=149.3; 	add++;
			CSENSE_VS.X[add]=3608;		CSENSE_VS.Y[add]=199.3; 	add++;
			CSENSE_VS.X[add]=4720;		CSENSE_VS.Y[add]=250.0; 	add++;
			CSENSE_VS.X[add]=5789;		CSENSE_VS.Y[add]=300.0; 	add++;
			CSENSE_VS.X[add]=6884;		CSENSE_VS.Y[add]=350.0; 	add++;
			CSENSE_VS.X[add]=7970;		CSENSE_VS.Y[add]=400.0; 	add++;
			CSENSE_VS.X[add]=9060;		CSENSE_VS.Y[add]=450.0; 	add++;
			CSENSE_VS.X[add]=10150;		CSENSE_VS.Y[add]=500.0; 	add++;
			CSENSE_VS.X[add]=10600;		CSENSE_VS.Y[add]=520.0; 	add++;
			CSENSE_VS.X[add]=11114;		CSENSE_VS.Y[add]=543.0; 	add++;
			CSENSE_VS.X[add]=11265;		CSENSE_VS.Y[add]=549.0; 	add++;
			CSENSE_VS.X[add]=12000;		CSENSE_VS.Y[add]=550.0; 	add++;
			CSENSE_VS.No=add;
			break;
		case 2:	//FTLAB V1.7.0-1 2026.02.04
			CSENSE_VS.X[add]=0;			CSENSE_VS.Y[add]=0; 		add++;
			CSENSE_VS.X[add]=347;		CSENSE_VS.Y[add]=50.8; 		add++;
			CSENSE_VS.X[add]=378;		CSENSE_VS.Y[add]=52.4; 		add++;
			CSENSE_VS.X[add]=825;		CSENSE_VS.Y[add]=75.3; 		add++;
			CSENSE_VS.X[add]=1322;		CSENSE_VS.Y[add]=100.5;		add++;
			CSENSE_VS.X[add]=2298;		CSENSE_VS.Y[add]=150.0; 	add++;
			CSENSE_VS.X[add]=3283;		CSENSE_VS.Y[add]=199.8; 	add++;
			CSENSE_VS.X[add]=4291;		CSENSE_VS.Y[add]=250.0; 	add++;
			CSENSE_VS.X[add]=5283;		CSENSE_VS.Y[add]=300.0; 	add++;
			CSENSE_VS.X[add]=6287;		CSENSE_VS.Y[add]=351.0; 	add++;
			CSENSE_VS.X[add]=7278;		CSENSE_VS.Y[add]=400.0; 	add++;
			CSENSE_VS.X[add]=8263;		CSENSE_VS.Y[add]=450.0; 	add++;
			CSENSE_VS.X[add]=9248;		CSENSE_VS.Y[add]=500.0; 	add++;
			CSENSE_VS.X[add]=9747;		CSENSE_VS.Y[add]=525.0; 	add++;
			CSENSE_VS.X[add]=10244;		CSENSE_VS.Y[add]=550.0; 	add++;
			CSENSE_VS.X[add]=10445;		CSENSE_VS.Y[add]=560.0; 	add++;
			CSENSE_VS.No=add;
			break;
		case 3:	//FTLAB V1.7.0-1 2026.02.26
			CSENSE_VS.X[add]=0;			CSENSE_VS.Y[add]=0; 		add++;
			CSENSE_VS.X[add]=347;		CSENSE_VS.Y[add]=50.8; 		add++;
			CSENSE_VS.X[add]=378;		CSENSE_VS.Y[add]=52.4; 		add++;
			CSENSE_VS.X[add]=825;		CSENSE_VS.Y[add]=75.3; 		add++;
			CSENSE_VS.X[add]=1322;		CSENSE_VS.Y[add]=100.5;		add++;
			CSENSE_VS.X[add]=2298;		CSENSE_VS.Y[add]=150.0; 	add++;
			CSENSE_VS.X[add]=3283;		CSENSE_VS.Y[add]=199.8; 	add++;
			CSENSE_VS.X[add]=4291;		CSENSE_VS.Y[add]=250.0; 	add++;
			CSENSE_VS.X[add]=5283;		CSENSE_VS.Y[add]=300.0; 	add++;
			CSENSE_VS.X[add]=6287;		CSENSE_VS.Y[add]=351.0; 	add++;
			CSENSE_VS.X[add]=7278;		CSENSE_VS.Y[add]=400.0; 	add++;
			CSENSE_VS.X[add]=8263;		CSENSE_VS.Y[add]=450.0; 	add++;
			CSENSE_VS.X[add]=9248;		CSENSE_VS.Y[add]=500.0; 	add++;
			CSENSE_VS.X[add]=9747;		CSENSE_VS.Y[add]=525.0; 	add++;
			CSENSE_VS.X[add]=10244;		CSENSE_VS.Y[add]=550.0; 	add++;
			CSENSE_VS.X[add]=10445;		CSENSE_VS.Y[add]=560.0; 	add++;
			CSENSE_VS.No=add;
			break;
	}

}





void Set_Cal_Default_Factor_ADC_VS_FAN(){
u16 add=0;

	//CSENSE_FAN_VS.X[add]=0;		CSENSE_FAN_VS.Y[add]=0.0;	add++;
	//CSENSE_FAN_VS.X[add]=47000;	CSENSE_FAN_VS.Y[add]=20.0;	add++;


	switch(MData.SN)
	{
		case 0:	//KBSI : 2025-12-24
			CSENSE_FAN_VS.X[add]=0;		CSENSE_FAN_VS.Y[add]=0.0;	add++;
			CSENSE_FAN_VS.X[add]=10837;	CSENSE_FAN_VS.Y[add]=4.38;	add++;
			CSENSE_FAN_VS.X[add]=14500;	CSENSE_FAN_VS.Y[add]=6.37;	add++;
			CSENSE_FAN_VS.X[add]=17688;	CSENSE_FAN_VS.Y[add]=7.71;	add++;
			CSENSE_FAN_VS.X[add]=22189;	CSENSE_FAN_VS.Y[add]=9.61;	add++;
			CSENSE_FAN_VS.X[add]=25502;	CSENSE_FAN_VS.Y[add]=11.0;	add++;
			CSENSE_FAN_VS.X[add]=28737;	CSENSE_FAN_VS.Y[add]=12.37;	add++;
			CSENSE_FAN_VS.X[add]=31397;	CSENSE_FAN_VS.Y[add]=13.49;	add++;
			CSENSE_FAN_VS.X[add]=34652;	CSENSE_FAN_VS.Y[add]=14.86;	add++;
			CSENSE_FAN_VS.X[add]=37441;	CSENSE_FAN_VS.Y[add]=16.03;	add++;
			CSENSE_FAN_VS.X[add]=43860;	CSENSE_FAN_VS.Y[add]=18.74;	add++;
			CSENSE_FAN_VS.X[add]=46914;	CSENSE_FAN_VS.Y[add]=20.0;	add++;
			CSENSE_FAN_VS.X[add]=55857;	CSENSE_FAN_VS.Y[add]=24.0;	add++;
			CSENSE_FAN_VS.No=add;
			break;
		case 1:	//FTLAB V1.6.0-1 2026-01-04
		case 2:	//FTLAB V1.7.0-1 2026-02-04
		case 3:	//FTLAB V1.7.0-1 2026-02-04
			CSENSE_FAN_VS.X[add]=0;		CSENSE_FAN_VS.Y[add]=0.0;	add++;
			CSENSE_FAN_VS.X[add]=2312;	CSENSE_FAN_VS.Y[add]=1.237;	add++;
			CSENSE_FAN_VS.X[add]=9800;	CSENSE_FAN_VS.Y[add]=4.37;	add++;
			CSENSE_FAN_VS.X[add]=14588;	CSENSE_FAN_VS.Y[add]=6.38;	add++;
			CSENSE_FAN_VS.X[add]=17710;	CSENSE_FAN_VS.Y[add]=7.70;	add++;
			CSENSE_FAN_VS.X[add]=22240;	CSENSE_FAN_VS.Y[add]=9.60;	add++;
			CSENSE_FAN_VS.X[add]=25540;	CSENSE_FAN_VS.Y[add]=11.0;	add++;
			CSENSE_FAN_VS.X[add]=28770;	CSENSE_FAN_VS.Y[add]=12.35;	add++;
			CSENSE_FAN_VS.X[add]=31533;	CSENSE_FAN_VS.Y[add]=13.51;	add++;
			CSENSE_FAN_VS.X[add]=34923;	CSENSE_FAN_VS.Y[add]=14.93;	add++;
			CSENSE_FAN_VS.X[add]=37450;	CSENSE_FAN_VS.Y[add]=16.00;	add++;
			CSENSE_FAN_VS.X[add]=42265;	CSENSE_FAN_VS.Y[add]=18.02;	add++;
			CSENSE_FAN_VS.X[add]=47030;	CSENSE_FAN_VS.Y[add]=20.02;	add++;
			CSENSE_FAN_VS.X[add]=56473;	CSENSE_FAN_VS.Y[add]=23.99;	add++;
			CSENSE_FAN_VS.No=add;
			break;
	}
}

void Set_Cal_Default_Factor_ADC_ION_BIAS(){
u16 add=0;

	switch(MData.SN)
	{
		case 0:
			//KBSI : 2025-12-24
			CSENSE_ION_BIAS_VS.X[add]=0;		CSENSE_ION_BIAS_VS.Y[add]=0.0;		add++;
			CSENSE_ION_BIAS_VS.X[add]=14863;	CSENSE_ION_BIAS_VS.Y[add]=59.9;		add++;
			CSENSE_ION_BIAS_VS.X[add]=20105;	CSENSE_ION_BIAS_VS.Y[add]=80.2;		add++;
			CSENSE_ION_BIAS_VS.X[add]=25237;	CSENSE_ION_BIAS_VS.Y[add]=100.2;	add++;
			CSENSE_ION_BIAS_VS.X[add]=22730;	CSENSE_ION_BIAS_VS.Y[add]=109.8;	add++;
			CSENSE_ION_BIAS_VS.X[add]=30380;	CSENSE_ION_BIAS_VS.Y[add]=120.1;	add++;
			CSENSE_ION_BIAS_VS.X[add]=32892;	CSENSE_ION_BIAS_VS.Y[add]=129.9;	add++;
			CSENSE_ION_BIAS_VS.X[add]=35490;	CSENSE_ION_BIAS_VS.Y[add]=139.9;	add++;
			CSENSE_ION_BIAS_VS.No=add;
			break;
		case 1:
		case 2:
		case 3:
			//FTLAB  : 2025-12-24 KBSI와 동
			CSENSE_ION_BIAS_VS.X[add]=0;		CSENSE_ION_BIAS_VS.Y[add]=0.0;		add++;
			CSENSE_ION_BIAS_VS.X[add]=14863;	CSENSE_ION_BIAS_VS.Y[add]=59.9;		add++;
			CSENSE_ION_BIAS_VS.X[add]=20105;	CSENSE_ION_BIAS_VS.Y[add]=80.2;		add++;
			CSENSE_ION_BIAS_VS.X[add]=25237;	CSENSE_ION_BIAS_VS.Y[add]=100.2;	add++;
			CSENSE_ION_BIAS_VS.X[add]=22730;	CSENSE_ION_BIAS_VS.Y[add]=109.8;	add++;
			CSENSE_ION_BIAS_VS.X[add]=30380;	CSENSE_ION_BIAS_VS.Y[add]=120.1;	add++;
			CSENSE_ION_BIAS_VS.X[add]=32892;	CSENSE_ION_BIAS_VS.Y[add]=129.9;	add++;
			CSENSE_ION_BIAS_VS.X[add]=35490;	CSENSE_ION_BIAS_VS.Y[add]=139.9;	add++;
			CSENSE_ION_BIAS_VS.No=add;
			break;
	}

}




void Set_Cal_Default_Factor_DAC_CV( ){
u8 add=0;

/*
	CSET_CV.X[add] = -12;	CSET_CV.Y[add] = 0;			add++;
	CSET_CV.X[add] = 12;	CSET_CV.Y[add] = 65535;		add++;
	CSET_CV.No=add;
*/

	switch(MData.SN)
	{
		case 0: //KBSI 중간에 오류가 있었음. 필요시 KBSI upgrade 할것.
			CSET_CV.X[add] = -5.0;		CSET_CV.Y[add] = 0;			add++;
			CSET_CV.X[add] = -4.99;		CSET_CV.Y[add] = 583;		add++;
			CSET_CV.X[add] = -3.98;		CSET_CV.Y[add] = 6713;		add++;
			CSET_CV.X[add] = -3.00;		CSET_CV.Y[add] = 13135;		add++;
			CSET_CV.X[add] = -2.02;		CSET_CV.Y[add] = 19337;		add++;
			CSET_CV.X[add] = -1.037;	CSET_CV.Y[add] = 25979;		add++;
			CSET_CV.X[add] = -0.5;		CSET_CV.Y[add] = 29490;		add++;//bug... 수정
			CSET_CV.X[add] = -0.250;	CSET_CV.Y[add] = 31128;		add++;
			CSET_CV.X[add] = -0.1002;	CSET_CV.Y[add] = 32107;		add++;
			CSET_CV.X[add] = -0.052;	CSET_CV.Y[add] = 32435;		add++;
			CSET_CV.X[add] = -0.020;	CSET_CV.Y[add] = 32631;		add++;//bug
			CSET_CV.X[add] = -0.0001;	CSET_CV.Y[add] = 32762;		add++;
			CSET_CV.X[add] = 0.0206;	CSET_CV.Y[add] = 32898;		add++;
			CSET_CV.X[add] = 0.0504;	CSET_CV.Y[add] = 33094;		add++;
			CSET_CV.X[add] = 0.1004;	CSET_CV.Y[add] = 33422;		add++;
			CSET_CV.X[add] = 0.204;		CSET_CV.Y[add] = 34109;		add++;
			CSET_CV.X[add] = 0.340;		CSET_CV.Y[add] = 34993;		add++;
			CSET_CV.X[add] = 0.500;		CSET_CV.Y[add] = 36041;		add++;
			CSET_CV.X[add] = 0.750;		CSET_CV.Y[add] = 37677;		add++;
			CSET_CV.X[add] = 1.000;		CSET_CV.Y[add] = 39341;		add++;
			CSET_CV.X[add] = 1.500;		CSET_CV.Y[add] = 42587;		add++;
			CSET_CV.X[add] = 2.000;		CSET_CV.Y[add] = 45926;		add++;
			CSET_CV.X[add] = 3.000;		CSET_CV.Y[add] = 52473;		add++;
			CSET_CV.X[add] = 4.010;		CSET_CV.Y[add] = 59020;		add++;
			CSET_CV.X[add] = 4.920;		CSET_CV.Y[add] = 61966;		add++;
			CSET_CV.X[add] = 4.930;		CSET_CV.Y[add] = 65535;		add++;
			CSET_CV.No=add;
			break;
		case 1: //FTLAB V1.6.0-1
			CSET_CV.X[add] = -5.0;		CSET_CV.Y[add] = 0;			add++;
			CSET_CV.X[add] = -4.99;		CSET_CV.Y[add] = 583;		add++;
			CSET_CV.X[add] = -3.98;		CSET_CV.Y[add] = 6713;		add++;
			CSET_CV.X[add] = -3.00;		CSET_CV.Y[add] = 13135;		add++;
			CSET_CV.X[add] = -2.02;		CSET_CV.Y[add] = 19337;		add++;
			CSET_CV.X[add] = -1.004;	CSET_CV.Y[add] = 26220;		add++;
			CSET_CV.X[add] = -0.507;	CSET_CV.Y[add] = 29489;		add++;
			CSET_CV.X[add] = -0.247;	CSET_CV.Y[add] = 31193;		add++;
			CSET_CV.X[add] = -0.098;	CSET_CV.Y[add] = 32176;		add++;
			CSET_CV.X[add] = -0.058;	CSET_CV.Y[add] = 32434;		add++;
			CSET_CV.X[add] = -0.025;	CSET_CV.Y[add] = 32657;		add++;
			CSET_CV.X[add] = -0.009;	CSET_CV.Y[add] = 32762;		add++;
			CSET_CV.X[add] = 0.0198;	CSET_CV.Y[add] = 32953;		add++;
			CSET_CV.X[add] = 0.0498;	CSET_CV.Y[add] = 33150;		add++;
			CSET_CV.X[add] = 0.1001;	CSET_CV.Y[add] = 33479;		add++;
			CSET_CV.X[add] = 0.2002;	CSET_CV.Y[add] = 34135;		add++;
			CSET_CV.X[add] = 0.307;		CSET_CV.Y[add] = 34837;		add++;
			CSET_CV.X[add] = 0.500;		CSET_CV.Y[add] = 36106;		add++;
			CSET_CV.X[add] = 0.750;		CSET_CV.Y[add] = 37743;		add++;
			CSET_CV.X[add] = 1.000;		CSET_CV.Y[add] = 39386;		add++;
			CSET_CV.X[add] = 1.500;		CSET_CV.Y[add] = 42667;		add++;
			CSET_CV.X[add] = 2.000;		CSET_CV.Y[add] = 45939;		add++;
			CSET_CV.X[add] = 3.000;		CSET_CV.Y[add] = 52505;		add++;
			CSET_CV.X[add] = 4.000;		CSET_CV.Y[add] = 59139;		add++;
			CSET_CV.X[add] = 4.450;		CSET_CV.Y[add] = 61910;		add++;
			CSET_CV.X[add] = 5.0;		CSET_CV.Y[add] = 65535;		add++;
			CSET_CV.No=add;
			break;
		case 2: //FTLAB V1.7.0
		case 3: //FTLAB V1.7.0
			CSET_CV.X[add] = -5.0;		CSET_CV.Y[add] = 0;			add++;
			CSET_CV.X[add] = -4.99;		CSET_CV.Y[add] = 583;		add++;
			CSET_CV.X[add] = -3.98;		CSET_CV.Y[add] = 6713;		add++;
			CSET_CV.X[add] = -3.00;		CSET_CV.Y[add] = 13135;		add++;
			CSET_CV.X[add] = -2.02;		CSET_CV.Y[add] = 19337;		add++;
			CSET_CV.X[add] = -1.006;	CSET_CV.Y[add] = 26220;		add++;
			CSET_CV.X[add] = -0.506;	CSET_CV.Y[add] = 29489;		add++;
			CSET_CV.X[add] = -0.206;	CSET_CV.Y[add] = 31454;		add++;
			CSET_CV.X[add] = -0.1005;	CSET_CV.Y[add] = 32142;		add++;
			CSET_CV.X[add] = -0.0502;	CSET_CV.Y[add] = 32471;		add++;
			CSET_CV.X[add] = -0.0258;	CSET_CV.Y[add] = 32630;		add++;
			CSET_CV.X[add] = -0.0055;	CSET_CV.Y[add] = 32762;		add++;
			CSET_CV.X[add] = 0.0146;	CSET_CV.Y[add] = 32894;		add++;
			CSET_CV.X[add] = 0.0504;	CSET_CV.Y[add] = 33130;		add++;
			CSET_CV.X[add] = 0.1005;	CSET_CV.Y[add] = 33459;		add++;
			CSET_CV.X[add] = 0.200;		CSET_CV.Y[add] = 34109;		add++;
			CSET_CV.X[add] = 0.300;		CSET_CV.Y[add] = 34765;		add++;
			CSET_CV.X[add] = 0.500;		CSET_CV.Y[add] = 36080;		add++;
			CSET_CV.X[add] = 0.750;		CSET_CV.Y[add] = 37710;		add++;
			CSET_CV.X[add] = 1.000;		CSET_CV.Y[add] = 39347;		add++;
			CSET_CV.X[add] = 1.500;		CSET_CV.Y[add] = 42667;		add++;
			CSET_CV.X[add] = 2.000;		CSET_CV.Y[add] = 45939;		add++;
			CSET_CV.X[add] = 3.000;		CSET_CV.Y[add] = 52505;		add++;
			CSET_CV.X[add] = 4.000;		CSET_CV.Y[add] = 59139;		add++;
			CSET_CV.X[add] = 4.450;		CSET_CV.Y[add] = 61910;		add++;
			CSET_CV.X[add] = 5.0;		CSET_CV.Y[add] = 65535;		add++;
			CSET_CV.No=add;
			break;
	}

}





float Find_Cal_Result_for_ADC_VS_HV(u16 inVal){
u8 i;
float Slope,B;
float CX1,CX2,CY1,CY2;
u8 no = CSENSE_VS.No;
float ret;


	if(inVal>=CSENSE_VS.X[no-1])
	{//최대값
		CY2=CSENSE_VS.Y[no-1];
		CY1=CSENSE_VS.Y[no-2];
		CX2=(float)CSENSE_VS.X[no-1];
		CX1=(float)CSENSE_VS.X[no-2];
	}
	else if(inVal<=CSENSE_VS.X[0])
	{								//최소값
		CY2=CSENSE_VS.Y[1];
		CY1=CSENSE_VS.Y[0];
		CX2=(float)CSENSE_VS.X[1];
		CX1=(float)CSENSE_VS.X[0];
	}
	else
	{
		for(i=1;i<no;i++)
		{
			if(inVal<=CSENSE_VS.X[i])
			{
				CY2=CSENSE_VS.Y[i];
				CY1=CSENSE_VS.Y[i-1];
				CX2=(float)CSENSE_VS.X[i];
				CX1=(float)CSENSE_VS.X[i-1];
				break;
			}
		}
	}

	Slope=(CY2-CY1)/(CX2-CX1);
	B=CY1-Slope*CX1;

	ret=Slope*inVal+B;

	if(ret<0.0) ret=0.0f;
	return ret;
}



float Find_Cal_Result_for_ADC_IS_HV(u16 inVal){
u8 i;
float Slope,B;
float CX1,CX2,CY1,CY2;
u8 no = CSENSE_IS.No;
float ret;


	if(inVal>=CSENSE_IS.X[no-1])
	{//최대값
		CY2=CSENSE_IS.Y[no-1];
		CY1=CSENSE_IS.Y[no-2];
		CX2=(float)CSENSE_IS.X[no-1];
		CX1=(float)CSENSE_IS.X[no-2];
	}
	else if(inVal<=CSENSE_IS.X[0])
	{								//최소값
		CY2=CSENSE_IS.Y[1];
		CY1=CSENSE_IS.Y[0];
		CX2=(float)CSENSE_IS.X[1];
		CX1=(float)CSENSE_IS.X[0];
	}
	else
	{
		for(i=1;i<no;i++)
		{
			if(inVal<=CSENSE_IS.X[i])
			{
				CY2=CSENSE_IS.Y[i];
				CY1=CSENSE_IS.Y[i-1];
				CX2=(float)CSENSE_IS.X[i];
				CX1=(float)CSENSE_IS.X[i-1];
				break;
			}
		}
	}


	Slope=(CY2-CY1)/(CX2-CX1);
	B=CY1-Slope*CX1;

	ret=Slope*inVal+B;

	if(ret<0.0) ret=0.0f;
	return ret;
}




u16 Find_Cal_Result_for_DAC_HV(float inVal){
u8 i;
float Slope,B;
float CX1,CX2,CY1,CY2;
u8 no = CSET.No;
float ret;


	if(inVal>=CSET.X[no-1])
	{//최대값
		CY2=(float)CSET.Y[no-1];
		CY1=(float)CSET.Y[no-2];
		CX2=CSET.X[no-1];
		CX1=CSET.X[no-2];
	}
	else if(inVal<=CSET.X[0])
	{								//최소값
		CY2=(float)CSET.Y[1];
		CY1=(float)CSET.Y[0];
		CX2=CSET.X[1];
		CX1=CSET.X[0];
	}
	else
	{
		for(i=1;i<no;i++)
		{
			if(inVal<=CSET.X[i])
			{
				CY2=(float)CSET.Y[i];
				CY1=(float)CSET.Y[i-1];
				CX2=CSET.X[i];
				CX1=CSET.X[i-1];
				break;
			}
		}
	}
	Slope=(CY2-CY1)/(CX2-CX1);
	B=CY1-Slope*CX1;

	ret=Slope*inVal+B;

	if(ret<0.0f) return 0;
	else if(ret > 65535.f) return 65535;
	else return (u16)ret;
}


u16 Find_Cal_Result_for_DAC_CV(float inVal){
u8 i;
float Slope,B;
float CX1,CX2,CY1,CY2;
u8 no = CSET_CV.No;
float ret;


	if(inVal>=CSET_CV.X[no-1])
	{//최대값
		CY2=(float)CSET_CV.Y[no-1];
		CY1=(float)CSET_CV.Y[no-2];
		CX2=CSET_CV.X[no-1];
		CX1=CSET_CV.X[no-2];
	}
	else if(inVal<=CSET_CV.X[0])
	{								//최소값
		CY2=(float)CSET_CV.Y[1];
		CY1=(float)CSET_CV.Y[0];
		CX2=CSET_CV.X[1];
		CX1=CSET_CV.X[0];
	}
	else
	{
		for(i=1;i<no;i++)
		{
			if(inVal<=CSET_CV.X[i])
			{
				CY2=(float)CSET_CV.Y[i];
				CY1=(float)CSET_CV.Y[i-1];
				CX2=CSET_CV.X[i];
				CX1=CSET_CV.X[i-1];
				break;
			}
		}
	}


	Slope=(CY2-CY1)/(CX2-CX1);
	B=CY1-Slope*CX1;

	ret=Slope*inVal+B;

	if(ret<0.0f) return 0;
	else if(ret > 65535.f) return 65535;
	else return (u16)ret;
}



float Find_Cal_Result_for_ADC_VS_FAN(u16 inVal){
u8 i;
float Slope,B;
float CX1,CX2,CY1,CY2;
u8 no = CSENSE_FAN_VS.No;
float ret;


	if(inVal>=CSENSE_FAN_VS.X[no-1])
	{//최대값
		CY2=CSENSE_FAN_VS.Y[no-1];
		CY1=CSENSE_FAN_VS.Y[no-2];
		CX2=(float)CSENSE_FAN_VS.X[no-1];
		CX1=(float)CSENSE_FAN_VS.X[no-2];
	}
	else if(inVal<=CSENSE_FAN_VS.X[0])
	{								//최소값
		CY2=CSENSE_FAN_VS.Y[1];
		CY1=CSENSE_FAN_VS.Y[0];
		CX2=(float)CSENSE_FAN_VS.X[1];
		CX1=(float)CSENSE_FAN_VS.X[0];
	}
	else
	{
		for(i=1;i<no;i++)
		{
			if(inVal<=CSENSE_FAN_VS.X[i])
			{
				CY2=CSENSE_FAN_VS.Y[i];
				CY1=CSENSE_FAN_VS.Y[i-1];
				CX2=(float)CSENSE_FAN_VS.X[i];
				CX1=(float)CSENSE_FAN_VS.X[i-1];
				break;
			}
		}
	}


	Slope=(CY2-CY1)/(CX2-CX1);
	B=CY1-Slope*CX1;

	ret=Slope*inVal+B;

	if(ret<0.0) ret=0.0f;
	return ret;
}


float Find_Cal_Result_for_ADC_VS_ION_BIAS(u16 inVal){
u8 i;
float Slope,B;
float CX1,CX2,CY1,CY2;
u8 no =  CSENSE_ION_BIAS_VS.No;
float ret;


	if(inVal>=CSENSE_ION_BIAS_VS.X[no-1])
	{//최대값
		CY2=CSENSE_ION_BIAS_VS.Y[no-1];
		CY1=CSENSE_ION_BIAS_VS.Y[no-2];
		CX2=(float)CSENSE_ION_BIAS_VS.X[no-1];
		CX1=(float)CSENSE_ION_BIAS_VS.X[no-2];
	}
	else if(inVal<=CSENSE_ION_BIAS_VS.X[0])
	{								//최소값
		CY2=CSENSE_ION_BIAS_VS.Y[1];
		CY1=CSENSE_ION_BIAS_VS.Y[0];
		CX2=(float)CSENSE_ION_BIAS_VS.X[1];
		CX1=(float)CSENSE_ION_BIAS_VS.X[0];
	}
	else
	{
		for(i=1;i<no;i++)
		{
			if(inVal<=CSENSE_ION_BIAS_VS.X[i])
			{
				CY2=CSENSE_ION_BIAS_VS.Y[i];
				CY1=CSENSE_ION_BIAS_VS.Y[i-1];
				CX2=(float)CSENSE_ION_BIAS_VS.X[i];
				CX1=(float)CSENSE_ION_BIAS_VS.X[i-1];
				break;
			}
		}
	}


	Slope=(CY2-CY1)/(CX2-CX1);
	B=CY1-Slope*CX1;

	ret=Slope*inVal+B;

	if(ret<0.0) ret=0.0f;
	return ret;
}

float Find_Cal_Result_for_MCP4161_FAN(float inVal)
{
	u8 i;
	float Slope,B;
	float CX1,CX2,CY1,CY2;
	u8 no = CSET_FAN.No;
	float ret;


	if(inVal>=CSET_FAN.X[no-1])
	{//최대값
		CY2=(float)CSET_FAN.Y[no-1];
		CY1=(float)CSET_FAN.Y[no-2];
		CX2=CSET_FAN.X[no-1];
		CX1=CSET_FAN.X[no-2];
	}
	else if(inVal<=CSET_FAN.X[0])
	{								//최소값
		CY2=(float)CSET_FAN.Y[1];
		CY1=(float)CSET_FAN.Y[0];
		CX2=CSET_FAN.X[1];
		CX1=CSET_FAN.X[0];
	}
	else
	{
		for(i=1;i<no;i++)
		{
			if(inVal<=CSET_FAN.X[i])
			{
				CY2=(float)CSET_FAN.Y[i];
				CY1=(float)CSET_FAN.Y[i-1];
				CX2=CSET_FAN.X[i];
				CX1=CSET_FAN.X[i-1];
				break;
			}
		}
	}


	Slope=(CY2-CY1)/(CX2-CX1);
	B=CY1-Slope*CX1;

	ret=Slope*inVal+B;

	if(ret<0.0f) return 0;
	else if(ret > 5000.0f) return 5000.0f;
	else return ret;
}












float Find_LF_Volt_from_SHALLOW(float yVolt){
u8 i;
float Slope,B;
float CX1,CX2,CY1,CY2;
u8 no = CSHALLOW.no;
float ret;


	if(yVolt>=CSHALLOW.X[no-1])
	{//최대값
		CY2=CSHALLOW.Y[no-1];
		CY1=CSHALLOW.Y[no-2];
		CX2=CSHALLOW.X[no-1];
		CX1=CSHALLOW.X[no-2];
	}
	else if(yVolt<=CSHALLOW.X[0])
	{								//최소값
		CY2=CSHALLOW.Y[1];
		CY1=CSHALLOW.Y[0];
		CX2=CSHALLOW.X[1];
		CX1=CSHALLOW.X[0];
	}
	else
	{
		for(i=1;i<no;i++)
		{
			if(yVolt<=CSHALLOW.X[i])
			{
				CY2=CSHALLOW.Y[i];
				CY1=CSHALLOW.Y[i-1];
				CX2=CSHALLOW.X[i];
				CX1=CSHALLOW.X[i-1];
				break;
			}
		}
	}

	Slope=(CY2-CY1)/(CX2-CX1);
	B=CY1-Slope*CX1;

	ret=Slope*yVolt+B;

	if(ret<0.0f) return 0;
	else if(ret > 5.0f) return 5.0f;//Max Value
	else return ret;
}



/////////////
/*
void Set_Cal_Default_Factor_MCP4161_FAN( ){
u8 add=0;

	//CSET_FAN.X[add] = 0;	CSET_FAN.Y[add] = 0.0;			add++;
	//CSET_FAN.X[add] = 20;	CSET_FAN.Y[add] = 5000;			add++;

	CSET_FAN.X[add] = 3.48;		CSET_FAN.Y[add] = 929;			add++;
	CSET_FAN.X[add] = 4.13;		CSET_FAN.Y[add] = 2029;			add++;
	CSET_FAN.X[add] = 5.02;		CSET_FAN.Y[add] = 2894;			add++;
	CSET_FAN.X[add] = 7.05;		CSET_FAN.Y[add] = 3862;			add++;
	CSET_FAN.X[add] = 8.01;		CSET_FAN.Y[add] = 4126;			add++;
	CSET_FAN.X[add] = 9.1;		CSET_FAN.Y[add] = 4326;			add++;
	CSET_FAN.X[add] = 10.16;	CSET_FAN.Y[add] = 4494;			add++;
	CSET_FAN.X[add] = 11.18;	CSET_FAN.Y[add] = 4597;			add++;
	CSET_FAN.X[add] = 12.21;	CSET_FAN.Y[add] = 4695;			add++;
	CSET_FAN.X[add] = 13.19;	CSET_FAN.Y[add] = 4772;			add++;
	CSET_FAN.X[add] = 14.06;	CSET_FAN.Y[add] = 4836;			add++;
	CSET_FAN.X[add] = 15.06;	CSET_FAN.Y[add] = 4892;			add++;
	CSET_FAN.X[add] = 16.29;	CSET_FAN.Y[add] = 4949;			add++;
	CSET_FAN.X[add] = 16.73;	CSET_FAN.Y[add] = 4975;			add++;
	CSET_FAN.X[add] = 17.20;	CSET_FAN.Y[add] = 4988;			add++;
	CSET_FAN.No=add;
}
*/


//not used
void Set_Cal_Default_Factor_ADC_IS_HV(){
//u8 add=0;
	CSENSE_IS.No=2;
	CSENSE_IS.X[0]=0;		CSENSE_IS.Y[0]=0.0;
	CSENSE_IS.X[1]=65535;	CSENSE_IS.Y[1]=50.0;//mA

}
