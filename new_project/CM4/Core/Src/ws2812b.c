<<<<<<< HEAD
﻿/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ws2812b.c
  * @brief   WS2812B 鐏甫椹卞姩瀹炵幇锛屼娇鐢?TIM2_CH3 PWM + DMA 杈撳嚭涓ユ牸鏃跺簭銆?  ******************************************************************************
  */
/* USER CODE END Header */

#include "ws2812b.h"

#include <string.h>

#include "cmsis_os2.h"
#include "main.h"
#include "tim.h"

extern void rpmsg_send_string(const char *str);

#define WS2812B_BITS_PER_LED     24U
#define WS2812B_DMA_BUF_LEN      ((WS2812B_RESET_BITS * 2U) + (WS2812B_NUM_LEDS * WS2812B_BITS_PER_LED))
#define WS2812B_DMA_TIMEOUT_MS   20U

typedef struct
{
  uint8_t green;
  uint8_t red;
  uint8_t blue;
} Ws2812bPixel_t;

static Ws2812bPixel_t led_pixels[WS2812B_NUM_LEDS];
static uint32_t dma_buf[WS2812B_DMA_BUF_LEN];
static osSemaphoreId_t ws2812b_dma_done_sem;

static void WS2812B_BuildDmaBuffer(void);
static void WS2812B_PutByteToDmaBuffer(uint8_t value, uint32_t *offset);
static void WS2812B_ReportHalError(void);

void ws2812b_init(void)
{
  const osSemaphoreAttr_t sem_attr = {
    .name = "Ws2812DmaSem"
  };

  /* 杩欓噷涓嶅垵濮嬪寲 TIM2/PB10/DMA锛屽洜涓鸿繖浜涜祫婧愮敱 CubeMX 缁熶竴鐢熸垚銆?   * 椹卞姩鍙垱寤哄悓姝ュ璞″苟娓呯┖杞欢鍍忕礌缂撳瓨锛岄伩鍏嶇牬鍧?main.c 涓凡鏈夌殑澶栬閰嶇疆杈圭晫銆?*/
  ws2812b_dma_done_sem = osSemaphoreNew(1U, 0U, &sem_attr);
  memset(led_pixels, 0, sizeof(led_pixels));
  memset(dma_buf, 0, sizeof(dma_buf));
}

void ws2812b_set_all(uint8_t r, uint8_t g, uint8_t b)
{
  uint32_t i;

  for (i = 0U; i < WS2812B_NUM_LEDS; i++)
  {
    ws2812b_set_one((uint8_t)i, r, g, b);
  }
}

void ws2812b_set_one(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
  if (idx >= WS2812B_NUM_LEDS)
  {
    return;
  }

  /* WS2812B 鐨勭嚎涓婂崗璁浐瀹氫负 GRB 椤哄簭锛岃€屼笂灞備笟鍔℃洿鑷劧鍦扮敤 RGB 琛ㄨ揪棰滆壊銆?   * 鍦ㄩ┍鍔ㄨ竟鐣屽畬鎴?RGB->GRB 杞崲锛屽彲浠ヨ LED 浠诲姟鍙叧蹇冣€滅孩/缁?钃濃€濈殑涓氬姟鍚箟锛?   * 涓嶆妸鍏蜂綋鐏彔鍗忚娉勬紡鍒颁换鍔″眰锛屽悗缁浛鎹㈢伅甯﹀瀷鍙锋椂涔熸洿瀹规槗鏀舵暃淇敼鑼冨洿銆?*/
  led_pixels[idx].green = g;
  led_pixels[idx].red = r;
  led_pixels[idx].blue = b;
}

void ws2812b_send(void)
{
  HAL_StatusTypeDef status;

  if (ws2812b_dma_done_sem == NULL)
  {
    WS2812B_ReportHalError();
    return;
  }

  WS2812B_BuildDmaBuffer();

  /* 鍚姩鍓嶅厛鍋滄涓婁竴娆?PWM DMA锛屾槸涓轰簡闃叉涓婁竴甯у紓甯告湭瀹屾垚鏃跺啀娆″惎鍔?DMA銆?   * HAL 浼氱淮鎶?TIM/DMA 鐘舵€佹満锛屽厛 Stop 鍐?Start 鍙互璁╄繛缁懡浠ゅ叿鏈夌‘瀹氳涓猴紱
   * 杩欓噷浠嶇劧鍙皟鐢?HAL API锛屼笉鐩存帴纰?TIM2 瀵勫瓨鍣ㄣ€?*/
  (void)HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);

  status = HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_3, dma_buf, WS2812B_DMA_BUF_LEN);
  if (status != HAL_OK)
  {
    WS2812B_ReportHalError();
    return;
  }

  /* WS2812B 涓€甯ф暟鎹害 12 * 24 * 1.25us锛屽啀鍔犲浣嶄綆鐢靛钩锛岃繙灏忎簬 20ms銆?   * 璁剧疆鏈夐檺绛夊緟鏃堕棿鑰屼笉鏄案涔呴樆濉烇紝鏄负浜?DMA/HAL 鐘舵€佸紓甯告椂涓嶆嫋姝?LED 浠诲姟銆?*/
  if (osSemaphoreAcquire(ws2812b_dma_done_sem, WS2812B_DMA_TIMEOUT_MS) != osOK)
  {
    (void)HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
    WS2812B_ReportHalError();
  }
}

void ws2812b_clear(void)
{
  ws2812b_set_all(0U, 0U, 0U);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if ((htim != NULL) && (htim->Instance == TIM2))
  {
    (void)HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_3);

    if (ws2812b_dma_done_sem != NULL)
    {
      (void)osSemaphoreRelease(ws2812b_dma_done_sem);
    }
  }
}

static void WS2812B_BuildDmaBuffer(void)
{
  uint32_t offset = 0U;
  uint32_t i;

  /* WS2812B 没有独立帧头，只靠总线保持低电平来复位内部接收状态机。
   * 在 remoteproc 调试过程中，如果上一帧被打断或时序曾经错误，灯珠可能停在“半包”状态；
   * 下一帧一上来就是数据位时，第一颗灯会把它误当成旧帧续包，典型现象就是只亮一半、白色或后续不变色。
   * 因此本驱动在数据前后都放置 reset 低电平：前 reset 清空接收状态，后 reset 锁存本帧颜色。
   * 全过程仍然通过 TIM2 PWM + DMA 输出 CCR=0，不直接操作 GPIO，符合只使用 HAL 的约束。 */
  while (offset < WS2812B_RESET_BITS)
  {
    dma_buf[offset] = 0U;
    offset++;
  }

  for (i = 0U; i < WS2812B_NUM_LEDS; i++)
  {
    WS2812B_PutByteToDmaBuffer(led_pixels[i].green, &offset);
    WS2812B_PutByteToDmaBuffer(led_pixels[i].red, &offset);
    WS2812B_PutByteToDmaBuffer(led_pixels[i].blue, &offset);
  }

  /* 当前 .ioc 中 TIM2 输出时钟是 209MHz，ARR=260 时一位周期约 1.249us。
   * 50 个 CCR=0 周期约 62us，满足 WS2812B 数据手册中复位低电平大于 50us 的要求。 */
  while (offset < WS2812B_DMA_BUF_LEN)
  {
    dma_buf[offset] = 0U;
    offset++;
  }
}
static void WS2812B_PutByteToDmaBuffer(uint8_t value, uint32_t *offset)
{
  int8_t bit;

  for (bit = 7; bit >= 0; bit--)
  {
    if ((value & (uint8_t)(1U << (uint8_t)bit)) != 0U)
    {
      dma_buf[*offset] = WS2812B_BIT_1_CCR;
    }
    else
    {
      dma_buf[*offset] = WS2812B_BIT_0_CCR;
    }

    (*offset)++;
  }
}

static void WS2812B_ReportHalError(void)
{
  /* 浣犲凡瑕佹眰鍘绘帀 PD6 鐘舵€佺伅鍔熻兘锛屽洜姝よ繖閲屼笉鍐嶅仛鏈湴 GPIO 鎶ラ敊闂儊銆?   * 濡傛灉 RPMsg 宸茬粡寤虹珛锛屽敖閲忔妸涓ラ噸纭欢閿欒涓婃姤缁?A7锛涘鏋滄鏃堕摼璺湭灏辩华锛?   * rpmsg_send_string 鍐呴儴浼氶潤榛樿繑鍥烇紝閬垮厤閿欒澶勭悊鍐嶅紩鍏ラ樆濉炪€?*/
  rpmsg_send_string("EVENT:ERR_HAL");
}
=======
#include "ws2812b.h"
#include "tim.h"

#define WS2812B_BITS_PER_LED 24U
#define WS2812B_RESET_LEN    80U
#define WS2812B_BUF_LEN      ((WS2812B_LED_COUNT * WS2812B_BITS_PER_LED) + WS2812B_RESET_LEN)
#define WS2812B_CCR_0        35U
#define WS2812B_CCR_1        85U

static uint32_t ws2812b_buffer[WS2812B_BUF_LEN];

volatile uint8_t ws2812b_dma_done = 1U;
volatile uint32_t ws2812b_tx_count = 0U;
volatile uint32_t ws2812b_dma_done_count = 0U;
volatile uint32_t ws2812b_timeout_count = 0U;
volatile uint32_t ws2812b_start_fail_count = 0U;
volatile HAL_StatusTypeDef ws2812b_last_status = HAL_OK;

static uint8_t WS2812B_LimitBrightness(uint8_t value)
{
  return (value > WS2812B_MAX_BRIGHTNESS) ? WS2812B_MAX_BRIGHTNESS : value;
}

static void WS2812B_FillResetCode(void)
{
  uint32_t start = WS2812B_LED_COUNT * WS2812B_BITS_PER_LED;

  for (uint32_t i = 0; i < WS2812B_RESET_LEN; i++)
  {
    ws2812b_buffer[start + i] = 0U;
  }
}

static void WS2812B_Wheel(uint8_t pos, uint8_t *red, uint8_t *green, uint8_t *blue)
{
  pos = 255U - pos;
  if (pos < 85U)
  {
    *red = 255U - (pos * 3U);
    *green = 0U;
    *blue = pos * 3U;
  }
  else if (pos < 170U)
  {
    pos -= 85U;
    *red = 0U;
    *green = pos * 3U;
    *blue = 255U - (pos * 3U);
  }
  else
  {
    pos -= 170U;
    *red = pos * 3U;
    *green = 255U - (pos * 3U);
    *blue = 0U;
  }
}

void WS2812B_Init(void)
{
  WS2812B_FillColor(0U, 0U, 0U);
  (void)WS2812B_Show();
}

void WS2812B_SetPixelColor(uint16_t index, uint8_t red, uint8_t green, uint8_t blue)
{
  if (index >= WS2812B_LED_COUNT)
  {
    return;
  }

  red = WS2812B_LimitBrightness(red);
  green = WS2812B_LimitBrightness(green);
  blue = WS2812B_LimitBrightness(blue);

  uint32_t color_grb = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
  uint32_t buffer_index = (uint32_t)index * WS2812B_BITS_PER_LED;

  for (int8_t bit = 23; bit >= 0; bit--)
  {
    ws2812b_buffer[buffer_index++] = ((color_grb >> bit) & 0x01U) ? WS2812B_CCR_1 : WS2812B_CCR_0;
  }
}

void WS2812B_FillColor(uint8_t red, uint8_t green, uint8_t blue)
{
  for (uint16_t i = 0; i < WS2812B_LED_COUNT; i++)
  {
    WS2812B_SetPixelColor(i, red, green, blue);
  }
}

HAL_StatusTypeDef WS2812B_Show(void)
{
  HAL_StatusTypeDef status;
  uint32_t start_tick;

  WS2812B_FillResetCode();
  ws2812b_tx_count++;
  WS2812B_PinAsTimer();

  HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0U);

  ws2812b_dma_done = 0U;
  status = HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_3, ws2812b_buffer, WS2812B_BUF_LEN);
  ws2812b_last_status = status;
  if (status != HAL_OK)
  {
    ws2812b_start_fail_count++;
    ws2812b_dma_done = 1U;
    return status;
  }

  start_tick = HAL_GetTick();
  while (ws2812b_dma_done == 0U)
  {
    if ((HAL_GetTick() - start_tick) > 50U)
    {
      HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
      __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0U);
      ws2812b_timeout_count++;
      ws2812b_last_status = HAL_TIMEOUT;
      ws2812b_dma_done = 1U;
      return HAL_TIMEOUT;
    }
  }

  HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0U);
  ws2812b_last_status = HAL_OK;
  return HAL_OK;
}

HAL_StatusTypeDef WS2812B_Command(WS2812B_ColorCmd_t command)
{
  switch (command)
  {
    case WS2812B_COLOR_RED:
      WS2812B_FillColor(255U, 0U, 0U);
      break;
    case WS2812B_COLOR_GREEN:
      WS2812B_FillColor(0U, 255U, 0U);
      break;
    case WS2812B_COLOR_BLUE:
      WS2812B_FillColor(0U, 0U, 255U);
      break;
    case WS2812B_COLOR_OFF:
      WS2812B_FillColor(0U, 0U, 0U);
      break;
    default:
      return HAL_ERROR;
  }

  return WS2812B_Show();
}

void WS2812B_Test(void)
{
  WS2812B_FillColor(0U, 0U, 0U);
  WS2812B_SetPixelColor(0U, WS2812B_MAX_BRIGHTNESS, 0U, 0U);
  (void)WS2812B_Show();
}

void WS2812B_RainbowStep(uint8_t step)
{
  static uint8_t color_offset = 0U;
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  for (uint16_t i = 0; i < WS2812B_LED_COUNT; i++)
  {
    WS2812B_Wheel((uint8_t)(((i * 256U) / WS2812B_LED_COUNT + color_offset) & 0xFFU),
                  &red, &green, &blue);
    WS2812B_SetPixelColor(i, red, green, blue);
  }

  color_offset += step;
  (void)WS2812B_Show();
}

void WS2812B_Rainbow(void)
{
  WS2812B_RainbowStep(2U);
}

void WS2812B_PinAsGpio(GPIO_PinState state)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
  __HAL_RCC_GPIOB_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, state);
}

void WS2812B_PinAsTimer(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    ws2812b_dma_done = 1U;
    ws2812b_dma_done_count++;
  }
}
>>>>>>> 042c0689b3d30a66b27a2a4342a332c9bfc9e0a9
