#ifndef __JOG__
#define __JOG__

#ifdef __JOG_H__
	#define EXT_JOG
#else
	#define EXT_JOG extern
#endif
#include "mType.h"


EXT_JOG void Button_and_JOG_Control();
EXT_JOG void JOG_READ(void);
EXT_JOG void JOG_COUNT(u8 inKey);
EXT_JOG u8 JOG_Read_Button(void);
EXT_JOG void Arrangement_Jog_Control(void);

#endif
