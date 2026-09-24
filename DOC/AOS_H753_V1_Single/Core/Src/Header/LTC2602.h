#ifndef __LTC2602__
#define __LTC2602__

#ifdef __LTC2602_H__
	#define EXT_LTC2602
#else
	#define EXT_LTC2602 extern
#endif
#include "mType.h"

EXT_LTC2602 void LTC2602_COMMAND(u8 Ch,u16 inVal);
EXT_LTC2602 void LTC2602_INIT(void);



#endif
