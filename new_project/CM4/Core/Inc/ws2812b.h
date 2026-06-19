<<<<<<< HEAD
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ws2812b.h
  * @brief   WS2812B 灯带驱动接口，基于 TIM2_CH3 PWM + DMA。
  ******************************************************************************
  */
/* USER CODE END Header */

=======
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
#ifndef __WS2812B_H
#define __WS2812B_H

#ifdef __cplusplus
extern "C" {
#endif

<<<<<<< HEAD
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
=======
#include "main.h"

#define WS2812B_LED_COUNT      12U
#define WS2812B_MAX_BRIGHTNESS 8U

typedef enum
{
  WS2812B_COLOR_RED = 1,
  WS2812B_COLOR_GREEN,
  WS2812B_COLOR_BLUE,
  WS2812B_COLOR_OFF,
} WS2812B_ColorCmd_t;

extern volatile uint8_t ws2812b_dma_done;
extern volatile uint32_t ws2812b_tx_count;
extern volatile uint32_t ws2812b_dma_done_count;
extern volatile uint32_t ws2812b_timeout_count;
extern volatile uint32_t ws2812b_start_fail_count;
extern volatile HAL_StatusTypeDef ws2812b_last_status;

void WS2812B_Init(void);
void WS2812B_SetPixelColor(uint16_t index, uint8_t red, uint8_t green, uint8_t blue);
void WS2812B_FillColor(uint8_t red, uint8_t green, uint8_t blue);
HAL_StatusTypeDef WS2812B_Show(void);
HAL_StatusTypeDef WS2812B_Command(WS2812B_ColorCmd_t command);
void WS2812B_Test(void);
void WS2812B_Rainbow(void);
void WS2812B_RainbowStep(uint8_t step);
void WS2812B_PinAsGpio(GPIO_PinState state);
void WS2812B_PinAsTimer(void);
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9

#ifdef __cplusplus
}
#endif

#endif /* __WS2812B_H */
