/*
 * AD9833.c
 *
 *  Created on: Jul 25, 2025
 *      Author: vdskim
 */
#define __AD9850_H__
#include	"AD9850.h"
#undef	__AD9850_H__

#include "MyGPIOSet.h"
#include "util.h"

#include "main.h"


#define FQ_UD_PIN   GPIO_PIN_12
#define FQ_UD_PORT  GPIOB

#define RESET_PIN   GPIO_PIN_13
#define RESET_PORT  GPIOB

#define W_CLK_PIN   GPIO_PIN_14
#define W_CLK_PORT  GPIOB

#define DATA_PIN    GPIO_PIN_15
#define DATA_PORT   GPIOB




void ad9850_delay(void) {
	delay_us(1);
}

void ad9850_pulse(GPIO_TypeDef* port, uint16_t pin) {
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    ad9850_delay();
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    ad9850_delay();
}

// AD9850 Sine/Square 파형, 주파수 설정 함수


void ad9850_send_freq(u32 freq, u32 clk){
u32 ftw = (u32)(((double)freq * 4294967296.0) / clk);

    // 4바이트 FTW (LSB first)
    for (int i = 0; i < 4; i++) {
        uint8_t b = (ftw >> (8 * i)) & 0xFF;
        for (int j = 0; j < 8; j++) {
            HAL_GPIO_WritePin(DATA_PORT, DATA_PIN, (b & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            ad9850_pulse(W_CLK_PORT, W_CLK_PIN);
            b >>= 1;
        }
    }
    // 5번째 바이트: Control byte (0x00 = Sine, 0x01 = Square)
    u8 control_byte=0x00;
    for (int j = 0; j < 8; j++) {
        HAL_GPIO_WritePin(DATA_PORT, DATA_PIN, (control_byte & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        ad9850_pulse(W_CLK_PORT, W_CLK_PIN);
        control_byte >>= 1;
    }
    ad9850_pulse(FQ_UD_PORT, FQ_UD_PIN); // Latch
}

// 리셋 시퀀스
void ad9850_reset(void) {
    ad9850_pulse(RESET_PORT, RESET_PIN);
    ad9850_pulse(W_CLK_PORT, W_CLK_PIN);
    ad9850_pulse(FQ_UD_PORT, FQ_UD_PIN);
}

// 초기화/예시 사용법
void ad9850_init_and_set_sine(u32 freq) {
    ad9850_reset();
    ad9850_send_freq(freq, 124999010); // 125MHz 클럭, Sine wave (control_byte=0)
}



