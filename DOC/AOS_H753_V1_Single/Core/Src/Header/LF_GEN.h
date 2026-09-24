#ifndef __LFGEN__
#define __LFGEN__

#ifdef __LFGEN_H__
	#define EXT_LFGEN
#else
	#define EXT_LFGEN extern
#endif

#include "mType.h"
#include "MyCTL.h"


EXT_LFGEN void generateSquareMid(u8 total_count, float Vpp);

EXT_LFGEN void generateTrapezoidMid(int total_count, int rise_count, int fall_count, float Vpp);

EXT_LFGEN void generateSineMid(u8 total_count, float Vpp);

EXT_LFGEN void generateRampMid(u8 total_count, u8 up_count, u8 down_count, float Vpp);


EXT_LFGEN void LF_Modulator_DAC_Update();
EXT_LFGEN void LF_Modulator_Set(eLF_Type type);
EXT_LFGEN void LF_Modulator_Voltage_Set();
EXT_LFGEN void LF_Frq_Set(float frq);

#endif
