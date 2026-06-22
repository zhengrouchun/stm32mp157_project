/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ws2812b.h
  * @brief   WS2812B 鐏甫椹卞姩鎺ュ彛锛屽熀浜?TIM2_CH3 PWM + DMA銆?  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __WS2812B_H
#define __WS2812B_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define WS2812B_ARR          260U
#define WS2812B_BIT_0_CCR    84U
#define WS2812B_BIT_1_CCR    178U
#define WS2812B_NUM_LEDS     12U
#define WS2812B_RESET_BITS   50U

void ws2812b_init(void);
void ws2812b_set_all(uint8_t r, uint8_t g, uint8_t b);
void ws2812b_set_one(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
void ws2812b_send(void);
void ws2812b_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __WS2812B_H */
