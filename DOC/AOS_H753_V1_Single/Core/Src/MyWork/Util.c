#define 	__UTIL_H__
#include	"Util.h"
#undef	__UTIL_H__
#include "stm32h7xx_hal.h"


void delay_us(u32 us)
{
    volatile uint32_t count;
    while(us--)
    {
        count = 120; // 1us 당 약 120회 반복 (480MHz 기준)
        while(count--) {
            __NOP(); // 최적화 방지용 (필요시)
        }
    }
}

void delay_ms(u32 ms)
{
	HAL_Delay(ms);
}

void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // DWT 활성화
    DWT->CYCCNT = 0;                                // 카운터 리셋
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;            // 카운터 시작
}

u32 DWT_GetCycles(void)
{
    return DWT->CYCCNT;
}
