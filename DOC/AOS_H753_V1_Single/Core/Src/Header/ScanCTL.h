#ifndef __SCANCTL__
#define __SCANCTL__

#ifdef __SCANCTL_H__
	#define EXT_SCANCTL
#else
	#define EXT_SCANCTL extern
#endif



#include "mType.h"

EXT_SCANCTL void FAIMs_SCAN_CTL();
EXT_SCANCTL void FAIMs_D_SCAN_CTL();
EXT_SCANCTL void Ion_Current_Read();
EXT_SCANCTL void ADC_Buffer_Done_Control();


//EXT_SCANCTL void FAIMs_Shallow_Find();

EXT_SCANCTL u16 Find_Current_Avg_with_Delay(u16 delay);
EXT_SCANCTL u16 Find_Current_Avg_with_Delay_II(u16 delay);

EXT_SCANCTL void FAIMs_SCAN_CTL_FullRange_Mode();


#endif
