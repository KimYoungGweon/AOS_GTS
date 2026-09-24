#ifndef __EEPROM__
#define __EEPROM__

#ifdef __EEPROM_H__
	#define EXT_EEPROM
#else
	#define EXT_EEPROM extern
#endif

#include "mType.h"



EXT_EEPROM	void EEPROM_INIT(void);
EXT_EEPROM void EEPROM_WRITE(u16 add, u8 inDat);
EXT_EEPROM	u8   EEPROM_READ(u16 add);
EXT_EEPROM	void EEPROM_WRITE_CONTROL(u8 inKey);

#endif
