/*
 * MyGPIOSet.h
 *
 *  Created on: Jun 5, 2025
 *      Author: vdskim
 */

#ifndef INC_MYGPIOSET_H_
#define INC_MYGPIOSET_H_

#include "stm32h7xx_hal.h"


#define DO_START(OnOff) 		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, (GPIO_PinState)OnOff) //SINAL

#define DO_LED(OnOff) 			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, (GPIO_PinState)OnOff) //LED
#define DO_YELLOW(OnOff) 		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, (GPIO_PinState)OnOff) //LED
#define DO_BUZZER(OnOff) 		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, (GPIO_PinState)OnOff) //Buzzer



//Bias Relay
#define DO_BIAS_P(OnOff) 		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, (GPIO_PinState)OnOff)
#define DO_BIAS_P2(OnOff) 		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, (GPIO_PinState)OnOff)
#define DO_BIAS_N(OnOff) 		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, (GPIO_PinState)OnOff)
#define DO_BIAS_N2(OnOff) 		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, (GPIO_PinState)OnOff)


//LT2602
#define DO_LTC2602_CS(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13,  (GPIO_PinState)OnOff)
#define DO_LTC2602_CLK(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_12, (GPIO_PinState)OnOff)
#define DO_LTC2602_SDI(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, (GPIO_PinState)OnOff)

//AD7739
#define DO_AD7739_CS(OnOff) 	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_9,  (GPIO_PinState)OnOff)
#define DO_AD7739_SCK(OnOff) 	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_10, (GPIO_PinState)OnOff)
#define DO_AD7739_SDI(OnOff) 	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, (GPIO_PinState)OnOff)
#define DI_AD7739_RDY		 	HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_8)
#define DI_AD7739_SDO		 	HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_14)


//input //C P1, E9,
//output //E 7,8,10
//AD7739 2ND
#define DO_AD7739_CS2(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_7,  (GPIO_PinState)OnOff)
#define DO_AD7739_SCK2(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, (GPIO_PinState)OnOff)
#define DO_AD7739_SDI2(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, (GPIO_PinState)OnOff)
#define DI_AD7739_RDY2		 	HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11)
#define DI_AD7739_SDO2		 	HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_9)


#define DO_AD7739_CS3(OnOff) 	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12,  (GPIO_PinState)OnOff)
#define DI_AD7739_RDY3		 	HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11)
#define DO_AD7739_SCK3(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, (GPIO_PinState)OnOff)	//2 동일
#define DO_AD7739_SDI3(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, (GPIO_PinState)OnOff)	//2 동일
#define DI_AD7739_SDO3		 	HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_9)


//Relay
//#define DO_ION_SELECT0(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_7,  (GPIO_PinState)OnOff) //Ion Select_P_N0
//#define DO_ION_SELECT1(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, (GPIO_PinState)OnOff) //Ion Select_P_N1




//EEPROM
#define DO_EEPROM_CS(OnOff) 	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, (GPIO_PinState)OnOff)
#define DO_EEPROM_SCK(OnOff) 	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, (GPIO_PinState)OnOff)
#define DO_EEPROM_SDI(OnOff) 	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, (GPIO_PinState)OnOff)
#define DI_EEPROM_SDO		 	HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_3)

//JOG
#define DI_JOG_BTN		 	HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3)
#define DI_JOG1			 	HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4)
#define DI_JOG2			 	HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_5)

//Button
#define DI_BTN1		 		HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0)
#define DI_BTN2		 		HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_1)
#define DI_BTN3		 		HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_2)

#define DI_BTN4		 		HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11)
#define DI_BTN5		 		HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12)

//Button LED
#define DO_BTN_LED1(OnOff) 	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3,  (GPIO_PinState)OnOff)
#define DO_BTN_LED2(OnOff) 	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4,  (GPIO_PinState)OnOff)
#define DO_BTN_LED3(OnOff) 	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_5,  (GPIO_PinState)OnOff)

#define DO_BTN_LED4(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0,  (GPIO_PinState)OnOff)
#define DO_BTN_LED5(OnOff) 	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1,  (GPIO_PinState)OnOff)


//Bias OnOff
#define DO_BIAS(OnOff) 		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, (GPIO_PinState)OnOff)




//MCP3202







#endif /* INC_MYGPIOSET_H_ */
