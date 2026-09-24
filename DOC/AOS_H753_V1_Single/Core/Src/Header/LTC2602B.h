#ifndef __LTC2602B__
#define __LTC2602B__

#ifdef __LTC2602B_H__
	#define EXT_LTC2602B
#else
	#define EXT_LTC2602B extern
#endif
#include "mType.h"

EXT_LTC2602B void LTC2602B_COMMAND(u8 Ch,u16 inVal);
EXT_LTC2602B void LTC2602B_INIT(void);



#endif
