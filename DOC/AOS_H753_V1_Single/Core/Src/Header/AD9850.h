#ifndef __AD9850__
#define __AD9850__
#ifdef __AD9833_H__
	#define EXT_AD9850
#else
	#define EXT_AD9850 extern
#endif

#include "main.h"
#include "mType.h"


#define MASTER_CLOCK_CORRECTION 124999010 //125000000

EXT_AD9850 void ad9850_delay(void);
EXT_AD9850 void ad9850_pulse(GPIO_TypeDef* port, uint16_t pin);
EXT_AD9850 void ad9850_send_freq(u32 freq, u32 clk);
EXT_AD9850 void ad9850_reset(void);
EXT_AD9850 void ad9850_init_and_set_sine(u32 freq);







#endif /* SRC_HEADER_AD9850_H_ */
