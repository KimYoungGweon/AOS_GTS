#ifndef __AD7739__
#define __AD7739__

#ifdef __AD7739_H__
	#define EXT_AD7739
#else
	#define EXT_AD7739 extern
#endif



#include "mType.h"

EXT_AD7739 void ADC7739_Init(void );
EXT_AD7739 u8 read_adc7739_byte(void);
EXT_AD7739 u16 read_adc7739_word(void);
EXT_AD7739 void write_adc7739_byte(u8 data);
EXT_AD7739 u16 AD7739_READ(u8 CH);

EXT_AD7739 void ADC7739_Init2(void );
EXT_AD7739 u8 read_adc7739_byte2(void);
EXT_AD7739 u16 read_adc7739_word2(void);
EXT_AD7739 void write_adc7739_byte2(u8 data);
EXT_AD7739 u16 AD7739_READ2(u8 CH);


EXT_AD7739 void ADC7739_Init3(void );
EXT_AD7739 u8 read_adc7739_byte3(void);
EXT_AD7739 u16 read_adc7739_word3(void);
EXT_AD7739 void write_adc7739_byte3(u8 data);
EXT_AD7739 u16 AD7739_READ3(u8 CH);

#endif
