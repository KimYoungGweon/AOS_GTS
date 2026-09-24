#ifndef _CAL_CTL_
#define _CAL_CTL_

#ifdef _CAL_CTL_H_
	#define EXT_CALCTL
#else
	#define EXT_CALCTL extern
#endif

#include "mType.h"

#define CAL_NO	40



typedef struct{
	u8 		No;
	float 	X[CAL_NO];//real Voltage
	u16 	Y[CAL_NO];//Output DAC Value
}_CAL_FACT_SET;



typedef struct{
	u8 		No;
	float 	X[CAL_NO];//real Voltage
	float 	Y[CAL_NO];//Output float ohm
}_CAL_FACT_SET2;

typedef struct{
	u8 		No;
	u16 	X[CAL_NO];//ADC Value
	float 	Y[CAL_NO];//Output(real Voltage
}_CAL_FACT_VS;


EXT_CALCTL u16 	 Find_Cal_Result_for_DAC_HV(float inVal);
EXT_CALCTL float Find_Cal_Result_for_ADC_VS_HV(u16 inVal);
EXT_CALCTL float Find_Cal_Result_for_ADC_IS_HV(u16 inVal);
EXT_CALCTL float Find_Cal_Result_for_ADC_VS_FAN(u16 inVal);
EXT_CALCTL float Find_Cal_Result_for_ADC_VS_ION_BIAS(u16 inVal);
EXT_CALCTL u16 	Find_Cal_Result_for_DAC_CV(float inVal);

EXT_CALCTL float Find_Cal_Result_for_MCP4161_FAN(float inVal);


EXT_CALCTL void Set_Cal_Default_Factor_DAC_HV( );
EXT_CALCTL void Set_Cal_Default_Factor_DAC_CV( );
EXT_CALCTL void Set_Cal_Default_Factor_ADC_VS_HV( );
EXT_CALCTL void Set_Cal_Default_Factor_ADC_IS_HV( );
EXT_CALCTL void Set_Cal_Default_Factor_ADC_VS_FAN( );
EXT_CALCTL void Set_Cal_Default_Factor_ADC_ION_BIAS( );

EXT_CALCTL void Set_Cal_Default_Factor_MCP4161_FAN( );




EXT_CALCTL _CAL_FACT_VS 	CVS_T;
EXT_CALCTL _CAL_FACT_SET 	CSET_T;

EXT_CALCTL _CAL_FACT_VS 	CSENSE_FAN_VS;
EXT_CALCTL _CAL_FACT_VS 	CSENSE_IS;
EXT_CALCTL _CAL_FACT_VS 	CSENSE_VS;
EXT_CALCTL _CAL_FACT_VS 	CSENSE_ION_BIAS_VS;

EXT_CALCTL _CAL_FACT_SET 	CSET;
EXT_CALCTL _CAL_FACT_SET 	CSET_CV;
EXT_CALCTL _CAL_FACT_SET2 	CSET_FAN;


EXT_CALCTL void CAL_Data_Write(u8 type);
EXT_CALCTL void CAL_Data_Read(u8 type);
EXT_CALCTL void CAL_for_CV_Save();
EXT_CALCTL void CAL_for_CV_Read();
EXT_CALCTL u32 CAL_FRQ_Read( );
EXT_CALCTL void CAL_FRQ_Save(u32 frq);





/*
#define CAL2D_X_MAX 20
#define CAL2D_Y_MAX 20

typedef struct {
    u8    nx;
    u8    ny;
    float X[CAL2D_X_MAX];
    float Y[CAL2D_Y_MAX];
    float Z[CAL2D_Y_MAX][CAL2D_X_MAX]; // Z[y][x]
} _CAL_FACT_SET2D;


EXT_CALCTL _CAL_FACT_SET2D CSET_2D;
EXT_CALCTL float Find_2D_LF_Volt(float xFrq, float yVolt);
EXT_CALCTL u8 upper_index_clamped(const float *A, u8 n, float v);
EXT_CALCTL void Shallow_Function_Data_Read();
EXT_CALCTL void Shallow_Function_Data_Write();
*/



#define CAL_SHALLOW_NO	 40
typedef struct{
	u8 		no;
	float 	X[CAL_SHALLOW_NO];
	float 	Y[CAL_SHALLOW_NO];
}__CAL_FACT_SHALLOW;

EXT_CALCTL __CAL_FACT_SHALLOW CSHALLOW;

EXT_CALCTL float Find_LF_Volt_from_SHALLOW(float yVolt);

#endif
