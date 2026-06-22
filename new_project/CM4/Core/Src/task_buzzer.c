/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    task_buzzer.c
  * @brief   蜂鸣器控制任务，低电平触发 PG7。
  ******************************************************************************
  */
/* USER CODE END Header */

#include "task_buzzer.h"

#include <stdint.h>

#include "cmsis_os2.h"
#include "main.h"
#include "rpmsg_handler.h"

#define BUZZER_ON_LEVEL          GPIO_PIN_RESET
#define BUZZER_OFF_LEVEL         GPIO_PIN_SET
#define BUZZER_SHORT_ON_MS       500U
#define BUZZER_LONG_ON_MS        1000U
#define BUZZER_GAP_MS            200U

static osThreadId_t BuzzerTaskHandle;

static const osThreadAttr_t BuzzerTask_attributes = {
  .name = "BuzzerTask",
  .stack_size = 256U * 4U,
  .priority = (osPriority_t) osPriorityBelowNormal
};

static void BuzzerTask(void *argument);
static void BuzzerSet(uint8_t on);

void task_buzzer_init(void)
{
  /* PG7 已由 CubeMX 配置为蜂鸣器输出。这里仅写默认电平，确保低电平触发蜂鸣器
   * 在任务启动前保持关闭；不调用 HAL_GPIO_Init，不重新配置引脚和时钟。 */
  BuzzerSet(0U);

  BuzzerTaskHandle = osThreadNew(BuzzerTask, NULL, &BuzzerTask_attributes);
  if (BuzzerTaskHandle == NULL)
  {
    rpmsg_send_string("EVENT:ERR_HAL");
  }
}

static void BuzzerTask(void *argument)
{
  uint8_t count;
  uint8_t i;
  uint32_t on_time_ms;

  (void)argument;

  for (;;)
  {
    if (osMessageQueueGet(xBuzzerCmdQueue, &count, NULL, osWaitForever) == osOK)
    {
      if (count == 0U)
      {
        BuzzerSet(0U);
        continue;
      }

      on_time_ms = (count == 1U) ? BUZZER_LONG_ON_MS : BUZZER_SHORT_ON_MS;

      for (i = 0U; i < count; i++)
      {
        BuzzerSet(1U);
        osDelay(on_time_ms);
        BuzzerSet(0U);

        /* 最后一次蜂鸣后不再额外等待，避免 ACK 后的体感反馈拖尾过长。
         * 多次蜂鸣之间保留 200ms 间隔，让 A7 端的“3次提醒”和单次长鸣容易区分。 */
        if ((uint8_t)(i + 1U) < count)
        {
          osDelay(BUZZER_GAP_MS);
        }
      }
    }
  }
}

static void BuzzerSet(uint8_t on)
{
  HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, (on != 0U) ? BUZZER_ON_LEVEL : BUZZER_OFF_LEVEL);
}
