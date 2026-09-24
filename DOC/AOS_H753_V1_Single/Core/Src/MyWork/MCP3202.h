#ifndef __MCP3202__
#define __MCP3202__

#ifdef __MCP320_H__
	#define EXT_MCP320
#else
	#define EXT_MCP3202 extern
#endif
#include "mType.h"

#include "stm32h7xx_hal.h"
#include <stdint.h>


#define MCP_CS1_PORT      GPIOE
#define MCP_CS1_PIN       GPIO_PIN_7


// CS2: ADC #2 Chip Select  (Gas channels)
#define MCP_CS2_PORT      GPIOB
#define MCP_CS2_PIN       GPIO_PIN_12

// DOUT1: ADC #1 Data Out (MCU input)
#define MCP_DOUT1_PORT    GPIOC
#define MCP_DOUT1_PIN     GPIO_PIN_11

// DOUT2: ADC #2 Data Out (MCU input)
#define MCP_DOUT2_PORT    GPIOB
#define MCP_DOUT2_PIN     GPIO_PIN_11

// CLK: shared clock (MCU output)
#define MCP_CLK_PORT      GPIOE
#define MCP_CLK_PIN       GPIO_PIN_8

// DIN: shared data in (MCU output to ADC)
#define MCP_DIN_PORT      GPIOE
#define MCP_DIN_PIN       GPIO_PIN_10

typedef enum {
    MCP_CH_AIR_PLUS  = 0,   // ADC #1, CH0
    MCP_CH_AIR_MINUS = 1,   // ADC #1, CH1
    MCP_CH_GAS_PLUS  = 2,   // ADC #2, CH0
    MCP_CH_GAS_MINUS = 3,   // ADC #2, CH1
} MCP_LogicalChannel;

// Index constants for the result[4] array used by averaging API
#define MCP_IDX_AIR_PLUS    0
#define MCP_IDX_AIR_MINUS   1
#define MCP_IDX_GAS_PLUS    2
#define MCP_IDX_GAS_MINUS   3


EXT_MCP3202 void MCP3202_Init(void);
EXT_MCP3202 void mcp_send_bit(uint8_t bit);
EXT_MCP3202 uint8_t mcp_recv_bit_dout1(void);
EXT_MCP3202 uint8_t mcp_recv_bit_dout2(void);
EXT_MCP3202 uint16_t mcp_read_raw(uint8_t adc_num, uint8_t channel);
EXT_MCP3202 uint16_t MCP3202_Read(MCP_LogicalChannel ch);
//EXT_MCP3202 void MCP3202_Read_4ch(uint16_t *air_plus, uint16_t *air_minus, uint16_t *gas_plus,  uint16_t *gas_minus);
//EXT_MCP3202 void MCP3202_Read_4ch_Avg(uint16_t result[4], uint8_t avg_count,uint16_t delay_us_between);

EXT_MCP3202 void MCP3202_Read_2ch(uint16_t *air_plus, uint16_t *gas_plus);
EXT_MCP3202 void MCP3202_Read_2ch_Avg(uint16_t result[2], uint16_t delay_ms);

#endif
