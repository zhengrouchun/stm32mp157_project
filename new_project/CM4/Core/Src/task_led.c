/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    task_led.c
  * @brief   WS2812B LED 控制任务，根据 RPMsg 投递的颜色命令刷新灯带。
  ******************************************************************************
  */
/* USER CODE END Header */

#include "task_led.h"

#include "cmsis_os2.h"
#include "rpmsg_handler.h"
#include "ws2812b.h"

static osThreadId_t LedTaskHandle;

static const osThreadAttr_t LedTask_attributes = {
  .name = "LedTask",
  .stack_size = 512U * 4U,
  .priority = (osPriority_t) osPriorityNormal
};

static void LedTask(void *argument);

void task_led_init(void)
{
  /* WS2812B 同步信号量必须在内核初始化之后创建，因此把驱动 init 放在任务模块 init 内。
   * main.c 只调用 task_led_init()，不需要知道灯带底层使用 TIM2、DMA 或信号量。 */
  ws2812b_init();

  LedTaskHandle = osThreadNew(LedTask, NULL, &LedTask_attributes);
  if (LedTaskHandle == NULL)
  {
    rpmsg_send_string("EVENT:ERR_HAL");
  }
}

static void LedTask(void *argument)
{
  LedCmd_t cmd;

  (void)argument;

  for (;;)
  {
    if (osMessageQueueGet(xLedCmdQueue, &cmd, NULL, osWaitForever) == osOK)
    {
      switch (cmd)
      {
        case LED_CMD_RED:
          ws2812b_set_all(8U, 0U, 0U);
          break;

        case LED_CMD_GREEN:
          ws2812b_set_all(0U, 8U, 0U);
          break;

        case LED_CMD_BLUE:
          ws2812b_set_all(0U, 0U, 8U);
          break;

        case LED_CMD_OFF:
        default:
          ws2812b_clear();
          break;
      }

      /* README 里原先要求用 PD6 表示收到 LED 指令；你已明确要求去掉状态灯，
       * 所以任务只刷新灯带，不再访问任何状态 LED GPIO，避免与后续硬件分配冲突。 */
      ws2812b_send();
    }
  }
}
