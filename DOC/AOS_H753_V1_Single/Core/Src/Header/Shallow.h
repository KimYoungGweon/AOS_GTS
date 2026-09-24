#ifndef __SHALLOW__
#define __SHALLOWL__

#ifdef __SHALLOW_H__
	#define EXT_SHALLOW
#else
	#define EXT_SHALLOW extern
#endif

#include "mType.h"

EXT_SHALLOW void Find_Shallow_Sinlge_Point();
//EXT_SHALLOW void FAIMs_Shallow_Set();
//EXT_SHALLOW void FAIMs_Shallow_Find();

EXT_SHALLOW void LF_Volt_Set(float value);
//EXT_SHALLOW float Correct_Start_Position_for_Shallow_Single(float value);

EXT_SHALLOW float Smart_Find_Shallow_Single_Point(float sVolt, float fVolt, u16 refCurrent, u16 delay);
EXT_SHALLOW void Shallow_Find_Line_Control();

#endif /* SRC_HEADER_SHALLOW_H_ */
