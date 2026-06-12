/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "dma.h"
#include "ipcc.h"
#include "openamp.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "task.h"
#include "queue.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    LED_CMD_RED   = 1,
    LED_CMD_GREEN,
    LED_CMD_BLUE,
    LED_CMD_OFF,
} LedCmd_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUM_LEDS         12  // 保持24以适配防发烫逻辑
#define WS2812_BITS      24
#define WS2812_RESET_LEN 80
#define WS2812_BUF_LEN   (NUM_LEDS * WS2812_BITS + WS2812_RESET_LEN)
#define FW_VERSION       "FW:RPMSG_WS2812_20260612_1410\r\n"

#define MAX_BRIGHTNESS   40  // 限制最大亮度（0-255），防止硬件发烫烧毁
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
struct rpmsg_endpoint rp_endpoint;
QueueHandle_t xLedQueue  = NULL;
QueueHandle_t xBeepQueue = NULL;

uint32_t ws2812_buf[WS2812_BUF_LEN]; // 使用 uint32_t 以匹配 HAL 库 DMA 数据宽度

volatile uint8_t ws2812_dma_done = 1;
volatile uint32_t ws2812_tx_count = 0;
volatile uint32_t ws2812_dma_done_count = 0;
volatile uint32_t ws2812_timeout_count = 0;
volatile uint32_t ws2812_start_fail_count = 0;
volatile uint32_t ws2812_last_cmd = 0;
volatile HAL_StatusTypeDef ws2812_last_status = HAL_OK;

// 动态记录占空比，用于自适应不同时钟频率
uint32_t g_ccr_0 = 0;
uint32_t g_ccr_1 = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
void vTaskOpenAMP_Poll  (void *pvParameters);
void vTaskWS2812B_Ctrl  (void *pvParameters);
void vTaskBuzzer_Ctrl   (void *pvParameters);

static void WS2812_FillColor(uint8_t r, uint8_t g, uint8_t b);
static void WS2812_Send(void);
static HAL_StatusTypeDef WS2812_Transmit(void);
static BaseType_t QueueCommand(QueueHandle_t queue, uint32_t action);
static void WS2812_PinAsGpio(GPIO_PinState state);
static void WS2812_PinAsTimer(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ============================================================
 * WS2812B 驱动实现 (结合防发烫控制)
 * ============================================================ */
static void WS2812_FillColor(uint8_t r, uint8_t g, uint8_t b)
{
    // 强制限制亮度，防止硬件发烫烧毁
    r = (r > MAX_BRIGHTNESS) ? MAX_BRIGHTNESS : r;
    g = (g > MAX_BRIGHTNESS) ? MAX_BRIGHTNESS : g;
    b = (b > MAX_BRIGHTNESS) ? MAX_BRIGHTNESS : b;

    uint32_t color_grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    uint32_t idx = 0;

    for (uint32_t led = 0; led < NUM_LEDS; led++)
    {
        for (int8_t bit = 23; bit >= 0; bit--)
        {
            ws2812_buf[idx++] = ((color_grb >> bit) & 0x01u) ? g_ccr_1 : g_ccr_0;
        }
    }
    for (uint32_t i = 0; i < WS2812_RESET_LEN; i++)
    {
        ws2812_buf[idx++] = 0U;
    }
}

static void WS2812_Send(void)
{
    extern TIM_HandleTypeDef htim2;
    ws2812_dma_done = 0;
    HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_3,
                           (uint32_t *)ws2812_buf,
                           WS2812_BUF_LEN);
    // 626个周期 @ 800kHz ≈ 782µs，等待2ms足够
        vTaskDelay(pdMS_TO_TICKS(2));
        HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
        ws2812_dma_done = 1;
}

static BaseType_t QueueCommand(QueueHandle_t queue, uint32_t action)
{
    if (queue == NULL)
    {
        return pdFAIL;
    }

    return xQueueSend(queue, &action, 0);
}

static void WS2812_PinAsGpio(GPIO_PinState state)
{
    extern TIM_HandleTypeDef htim2;
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

static void WS2812_PinAsTimer(void)
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

static HAL_StatusTypeDef WS2812_Transmit(void)
{
    extern TIM_HandleTypeDef htim2;
    HAL_StatusTypeDef status;
    TickType_t start_tick;

    ws2812_tx_count++;
    HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0);

    ws2812_dma_done = 0;
    status = HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_3,
                                   (uint32_t *)ws2812_buf,
                                   WS2812_BUF_LEN);
    ws2812_last_status = status;
    if (status != HAL_OK)
    {
        ws2812_start_fail_count++;
        ws2812_dma_done = 1;
        return status;
    }

    start_tick = xTaskGetTickCount();
    while (ws2812_dma_done == 0)
    {
        if ((xTaskGetTickCount() - start_tick) > pdMS_TO_TICKS(10))
        {
            HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0);
            ws2812_timeout_count++;
            ws2812_last_status = HAL_TIMEOUT;
            ws2812_dma_done = 1;
            return HAL_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0);
    ws2812_last_status = HAL_OK;
    return HAL_OK;
}

/* DMA传输完成回调：停止PWM，让数据线回归低电平（复位状态） */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    extern TIM_HandleTypeDef htim2;
    if (htim->Instance == TIM2)
    {
        ws2812_dma_done = 1;
        ws2812_dma_done_count++;
        // 调试：翻转状态LED
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_6);
    }
}

/* ============================================================
 * OpenAMP 接收回调
 * ============================================================ */
int rpmsg_recv_callback(struct rpmsg_endpoint *ept,
                        void *data, size_t len,
                        uint32_t src, void *priv)
{
    ept->dest_addr = src;

    char cmd[64] = {0};
    uint32_t copy_len = (len < sizeof(cmd) - 1) ? (uint32_t)len : (uint32_t)(sizeof(cmd) - 1);
    memcpy(cmd, data, copy_len);

    uint32_t action;

    /* ---- WS2812B 颜色命令 ---- */
    if (strncmp(cmd, "LED:R", 5) == 0)
    {
        action = LED_CMD_RED;
        if (QueueCommand(xLedQueue, action) == pdPASS)
        {
            OPENAMP_send(ept, "ACK:LED_RED\r\n", 13);
        }
        else
        {
            OPENAMP_send(ept, "ERR:LED_QUEUE\r\n", 15);
        }
    }
    else if (strncmp(cmd, "LED:G", 5) == 0)
    {
        action = LED_CMD_GREEN;
        if (QueueCommand(xLedQueue, action) == pdPASS)
        {
            OPENAMP_send(ept, "ACK:LED_GREEN\r\n", 15);
        }
        else
        {
            OPENAMP_send(ept, "ERR:LED_QUEUE\r\n", 15);
        }
    }
    else if (strncmp(cmd, "LED:B", 5) == 0)
    {
        action = LED_CMD_BLUE;
        if (QueueCommand(xLedQueue, action) == pdPASS)
        {
            OPENAMP_send(ept, "ACK:LED_BLUE\r\n", 14);
        }
        else
        {
            OPENAMP_send(ept, "ERR:LED_QUEUE\r\n", 15);
        }
    }
    else if (strncmp(cmd, "LED:OFF", 7) == 0)
    {
        action = LED_CMD_OFF;
        if (QueueCommand(xLedQueue, action) == pdPASS)
        {
            OPENAMP_send(ept, "ACK:LED_OFF\r\n", 13);
        }
        else
        {
            OPENAMP_send(ept, "ERR:LED_QUEUE\r\n", 15);
        }
    }
    /* ---- 蜂鸣器命令 ---- */
    else if (strncmp(cmd, "BEEP:", 5) == 0)
    {
        int seconds = cmd[5] - '0';
        if (seconds >= 1 && seconds <= 9)
        {
            action = (uint32_t)(seconds * 1000);
            if (QueueCommand(xBeepQueue, action) == pdPASS)
            {
                OPENAMP_send(ept, "ACK:BEEP_ON\r\n", 13);
            }
            else
            {
                OPENAMP_send(ept, "ERR:BEEP_QUEUE\r\n", 16);
            }
        }
        else
        {
            OPENAMP_send(ept, "ERR:BEEP_RANGE(1-9)\r\n", 21);
        }
    }
    /* ---- 连通性测试 ---- */
    else if (strncmp(cmd, "hello", 5) == 0)
    {
        OPENAMP_send(ept, "world\r\n", 7);
    }
    else if (strncmp(cmd, "ver", 3) == 0)
    {
        OPENAMP_send(ept, FW_VERSION, strlen(FW_VERSION));
    }
    else if (strncmp(cmd, "stat", 4) == 0)
    {
        char reply[128];
        int n = snprintf(reply, sizeof(reply),
                         "STAT:cmd=%lu,tx=%lu,done=%lu,fail=%lu,timeout=%lu,status=%d\r\n",
                         ws2812_last_cmd,
                         ws2812_tx_count,
                         ws2812_dma_done_count,
                         ws2812_start_fail_count,
                         ws2812_timeout_count,
                         (int)ws2812_last_status);
        OPENAMP_send(ept, reply, (n > 0) ? (uint32_t)n : 0U);
    }
    else if (strncmp(cmd, "PIN:1", 5) == 0)
    {
        WS2812_PinAsGpio(GPIO_PIN_SET);
        OPENAMP_send(ept, "ACK:PIN_HIGH\r\n", 14);
    }
    else if (strncmp(cmd, "PIN:0", 5) == 0)
    {
        WS2812_PinAsGpio(GPIO_PIN_RESET);
        OPENAMP_send(ept, "ACK:PIN_LOW\r\n", 13);
    }
    else if (strncmp(cmd, "PIN:AF", 6) == 0)
    {
        WS2812_PinAsTimer();
        OPENAMP_send(ept, "ACK:PIN_AF\r\n", 12);
    }
    else
    {
        OPENAMP_send(ept, "ERR:UNKNOWN_CMD\r\n", 17);
    }

    return 0;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  if(IS_ENGINEERING_BOOT_MODE())
  {
    /* Configure the system clock */
    SystemClock_Config();
  }
  else
  {
    /* IPCC initialisation */
    MX_IPCC_Init();
    /* OpenAmp initialisation ---------------------------------*/
    MX_OPENAMP_Init(RPMSG_REMOTE, NULL);
  }

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART3_UART_Init();
  MX_TIM2_Init();

  /* USER CODE BEGIN 2 */
  uint32_t arr_val = 79U;

  TIM2->ARR = arr_val;
  g_ccr_0 = 26U;
  g_ccr_1 = 51U;
  TIM2->EGR = TIM_EGR_UG;
  /* ============================================================
   * OpenAMP 端点与 FreeRTOS 任务初始化
   * ============================================================ */
  OPENAMP_create_endpoint(
      &rp_endpoint,
      "rpmsg-tty-channel",
      RPMSG_ADDR_ANY,
      rpmsg_recv_callback,
      NULL
  );

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDivValue = RCC_HSI_DIV1;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** RCC Clock Config
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_ACLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3|RCC_CLOCKTYPE_PCLK4
                              |RCC_CLOCKTYPE_PCLK5;
  RCC_ClkInitStruct.AXISSInit.AXI_Clock = RCC_AXISSOURCE_HSI;
  RCC_ClkInitStruct.AXISSInit.AXI_Div = RCC_AXI_DIV1;
  RCC_ClkInitStruct.MCUInit.MCU_Clock = RCC_MCUSSOURCE_HSI;
  RCC_ClkInitStruct.MCUInit.MCU_Div = RCC_MCU_DIV1;
  RCC_ClkInitStruct.APB4_Div = RCC_APB4_DIV1;
  RCC_ClkInitStruct.APB5_Div = RCC_APB5_DIV1;
  RCC_ClkInitStruct.APB1_Div = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2_Div = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB3_Div = RCC_APB3_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* 轮询处理 IPCC/OpenAMP 消息的任务 */
void vTaskOpenAMP_Poll(void *pvParameters)
{
    for (;;)
    {
        /* 依赖于工程底层的 VirtIO 缓冲刷新需求，保持轮询 */
        OPENAMP_check_for_message();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* WS2812B 队列命令处理任务 */
void vTaskWS2812B_Ctrl(void *pvParameters)
{
    uint32_t cmd;
    for (;;)
    {
        if (xQueueReceive(xLedQueue, &cmd, portMAX_DELAY) == pdPASS)
        {
            ws2812_last_cmd = cmd;
            switch (cmd)
            {
                case LED_CMD_RED:
                    WS2812_FillColor(255, 0, 0); // 在函数内部已被 MAX_BRIGHTNESS 限制
                    (void)WS2812_Transmit();
                    break;
                case LED_CMD_GREEN:
                    WS2812_FillColor(0, 255, 0);
                    (void)WS2812_Transmit();
                    break;
                case LED_CMD_BLUE:
                    WS2812_FillColor(0, 0, 255);
                    (void)WS2812_Transmit();
                    break;
                case LED_CMD_OFF:
                    WS2812_FillColor(0, 0, 0);
                    (void)WS2812_Transmit();
                    break;
                default:
                    break;
            }
        }
    }
}

/* 蜂鸣器队列命令处理任务 */
void vTaskBuzzer_Ctrl(void *pvParameters)
{
    uint32_t delay_ms;
    for (;;)
    {
        if (xQueueReceive(xBeepQueue, &delay_ms, portMAX_DELAY) == pdPASS)
        {
            HAL_GPIO_WritePin(GPIOG, GPIO_PIN_7, GPIO_PIN_SET);   /* 蜂鸣器ON */
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            HAL_GPIO_WritePin(GPIOG, GPIO_PIN_7, GPIO_PIN_RESET); /* 蜂鸣器OFF */
        }
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  * where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
