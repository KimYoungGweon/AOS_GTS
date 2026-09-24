#ifndef __DEBUG__
#define __DEBUG__

#ifdef __DEBUG_H__
	#define EXT_DEBUG
#else
	#define EXT_DEBUG extern
#endif
#include "mType.h"


EXT_DEBUG void debug_ctl();

#endif
