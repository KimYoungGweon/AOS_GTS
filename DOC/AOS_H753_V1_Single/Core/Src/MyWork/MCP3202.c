/*
 * MCP3202.c
 *
 *  Created on: May 8, 2026
 *      Author: vdskim
 */

#define 	__MCP3202_H__
#include	"MCP3202.h"
#undef	__MCP3202_H__

#include "MyGPIOSet.h"
#include "Util.h"
#include "main.h"


// =========================================================
// Pin macros
// =========================================================
#define CS1_LOW()    HAL_GPIO_WritePin(MCP_CS1_PORT, MCP_CS1_PIN, GPIO_PIN_RESET)
#define CS1_HIGH()   HAL_GPIO_WritePin(MCP_CS1_PORT, MCP_CS1_PIN, GPIO_PIN_SET)
#define CS2_LOW()    HAL_GPIO_WritePin(MCP_CS2_PORT, MCP_CS2_PIN, GPIO_PIN_RESET)
#define CS2_HIGH()   HAL_GPIO_WritePin(MCP_CS2_PORT, MCP_CS2_PIN, GPIO_PIN_SET)

#define CLK_LOW()    HAL_GPIO_WritePin(MCP_CLK_PORT, MCP_CLK_PIN, GPIO_PIN_RESET)
#define CLK_HIGH()   HAL_GPIO_WritePin(MCP_CLK_PORT, MCP_CLK_PIN, GPIO_PIN_SET)

#define DIN_LOW()    HAL_GPIO_WritePin(MCP_DIN_PORT, MCP_DIN_PIN, GPIO_PIN_RESET)
#define DIN_HIGH()   HAL_GPIO_WritePin(MCP_DIN_PORT, MCP_DIN_PIN, GPIO_PIN_SET)

#define DOUT1_READ() HAL_GPIO_ReadPin(MCP_DOUT1_PORT, MCP_DOUT1_PIN)
#define DOUT2_READ() HAL_GPIO_ReadPin(MCP_DOUT2_PORT, MCP_DOUT2_PIN)

// =========================================================
// Clock pulse: half-period = 1us → ~500kHz SPI clock
// (MCP3202 @ 5V supports up to 1.8MHz, 500kHz is safe)
// =========================================================
static inline void clk_pulse(void)
{
    CLK_HIGH();
    delay_us(1);
    CLK_LOW();
    delay_us(1);
}

// =========================================================
// Init: set idle state of all pins
// (GPIO mode/speed must be configured in CubeMX beforehand)
// =========================================================



void MCP3202_Init(void)
{
    CS1_HIGH();
    CS2_HIGH();
    CLK_LOW();
    DIN_LOW();
    delay_us(10);   // Settling time
}

// =========================================================
// Send 1 bit on DIN with one clock pulse
// MCP3202 latches DIN on rising edge of CLK
// =========================================================
void mcp_send_bit(uint8_t bit)
{
    if (bit) DIN_HIGH(); else DIN_LOW();
    delay_us(1);          // setup time before rising edge
    CLK_HIGH();
    delay_us(1);
    CLK_LOW();
    delay_us(1);
}

// =========================================================
// Receive 1 bit from DOUT1 / DOUT2
// MCP3202 outputs on falling edge of CLK,
// so we sample after CLK_LOW (or just before next CLK_HIGH).
// =========================================================
uint8_t mcp_recv_bit_dout1(void)
{
    CLK_HIGH();
    delay_us(1);
    CLK_LOW();
    delay_us(1);
    return (DOUT1_READ() == GPIO_PIN_SET) ? 1 : 0;
}

uint8_t mcp_recv_bit_dout2(void)
{
    CLK_HIGH();
    delay_us(1);
    CLK_LOW();
    delay_us(1);
    return (DOUT2_READ() == GPIO_PIN_SET) ? 1 : 0;
}

// =========================================================
// Low-level: read a single channel from a specific ADC
// adc_num : 1 = ADC #1 (CS1, DOUT1)
//           2 = ADC #2 (CS2, DOUT2)
// channel : 0 = CH0, 1 = CH1  (single-ended)
// returns : 12-bit raw value (0~4095)
// =========================================================
uint16_t mcp_read_raw(uint8_t adc_num, uint8_t channel)
{
    uint16_t result = 0;

    // ---- Select ADC ----
    if (adc_num == 1) CS1_LOW(); else CS2_LOW();
    delay_us(1);

    // ---- Send 4 control bits ----
    // (MCP3202 datasheet, single-ended mode)
    //   1) START   = 1
    //   2) SGL/DIFF= 1  (single-ended)
    //   3) ODD/SIGN= channel (0 = CH0, 1 = CH1)
    //   4) MSBF    = 1  (MSB first)
    mcp_send_bit(1);
    mcp_send_bit(1);
    mcp_send_bit(channel & 0x01);
    mcp_send_bit(1);

    // ---- Receive: 1 NULL bit + 12 data bits ----
    if (adc_num == 1) {
        //(void)mcp_recv_bit_dout1();              // NULL bit (discard)  이걸 사용하면 값이 이상해

        for (int i = 11; i >= 0; i--) {
            result |= ((uint16_t)mcp_recv_bit_dout1()) << i;
        }
    } else {
        //(void)mcp_recv_bit_dout2();				// NULL bit (discard)  이걸 사용하면 값이 이상해
        for (int i = 11; i >= 0; i--) {
            result |= ((uint16_t)mcp_recv_bit_dout2()) << i;
        }
    }

    // ---- Deselect ----
    if (adc_num == 1) CS1_HIGH(); else CS2_HIGH();
    delay_us(1);

    return result & 0x0FFF;
}

// =========================================================
// Public: read by logical channel
// =========================================================

uint16_t MCP3202_Read(MCP_LogicalChannel ch)
{
    switch (ch) {
        case MCP_CH_AIR_PLUS:  return mcp_read_raw(1, 0);
        case MCP_CH_AIR_MINUS: return mcp_read_raw(1, 1);
        case MCP_CH_GAS_PLUS:  return mcp_read_raw(2, 0);
        case MCP_CH_GAS_MINUS: return mcp_read_raw(2, 1);
        default:               return 0;
    }
}


// =========================================================
// Public: read all 4 channels (one shot each)
// Order: Air+, Air-, Gas+, Gas-
// =========================================================
/*void MCP3202_Read_4ch(uint16_t *air_plus, uint16_t *air_minus,
                      uint16_t *gas_plus,  uint16_t *gas_minus)
{
    *air_plus  = mcp_read_raw(1, 0);
    *air_minus = mcp_read_raw(1, 1);
    *gas_plus  = mcp_read_raw(2, 0);
    *gas_minus = mcp_read_raw(2, 1);
}
*/
void MCP3202_Read_2ch(uint16_t *air_plus, uint16_t *gas_plus)
{
    *air_plus  = mcp_read_raw(1, 0);
    *gas_plus  = mcp_read_raw(2, 0);
}

// =========================================================
// Public: read all 4 channels with averaging
// =========================================================
/*void MCP3202_Read_4ch_Avg(uint16_t result[4], uint8_t avg_count,
                          uint16_t delay_us_between)
{
    uint32_t sum[4] = {0, 0, 0, 0};

    if (avg_count == 0) avg_count = 1;

    for (uint8_t i = 0; i < avg_count; i++) {
        uint16_t ap, am, gp, gm;
        MCP3202_Read_4ch(&ap, &am, &gp, &gm);
        sum[MCP_IDX_AIR_PLUS]  += ap;
        sum[MCP_IDX_AIR_MINUS] += am;
        sum[MCP_IDX_GAS_PLUS]  += gp;
        sum[MCP_IDX_GAS_MINUS] += gm;

        if (delay_us_between > 0) {
            // delay_us takes uint, fine to pass as is
            delay_us(delay_us_between);
        }
    }

    result[MCP_IDX_AIR_PLUS]  = (uint16_t)(sum[MCP_IDX_AIR_PLUS]  / avg_count);
    result[MCP_IDX_AIR_MINUS] = (uint16_t)(sum[MCP_IDX_AIR_MINUS] / avg_count);
    result[MCP_IDX_GAS_PLUS]  = (uint16_t)(sum[MCP_IDX_GAS_PLUS]  / avg_count);
    result[MCP_IDX_GAS_MINUS] = (uint16_t)(sum[MCP_IDX_GAS_MINUS] / avg_count);
}
*/

void MCP3202_Read_2ch_Avg(uint16_t result[2], uint16_t delay_ms)
{
    uint32_t sum[2] = {0, 0};
    u16	count=0;
    uint16_t ap, gp;
    MData.TM.delay_cnt=0;
	for(;;)
    {
    	MCP3202_Read_2ch(&ap, &gp);
		sum[0]  += ap;
		sum[1]  += gp;
		count++;
		delay_us(20);
		if(MData.TM.delay_cnt>=delay_ms) break;

    }
    result[0]  = (uint16_t)(sum[0]  / count);
    result[1]  = (uint16_t)(sum[1]  / count);
}
