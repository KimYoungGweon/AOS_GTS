#ifndef   __UTIL__
#define   __UTIL__

#ifdef __UTIL_H__
	#define UTIL_EXT
#else
	#define UTIL_EXT extern
#endif

#include "mType.h"


UTIL_EXT void delay_ms(u32 ms);
UTIL_EXT void delay_us(u32 us);
UTIL_EXT void DWT_Init(void);
UTIL_EXT u32 DWT_GetCycles(void);
#endif

