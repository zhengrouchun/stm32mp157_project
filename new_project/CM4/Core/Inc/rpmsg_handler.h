/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rpmsg_handler.h
  * @brief   RPMsg/OpenAMP 通信模块对外接口。
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __RPMSG_HANDLER_H
#define __RPMSG_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cmsis_os2.h"

typedef enum
{
  LED_CMD_RED = 0,
  LED_CMD_GREEN,
  LED_CMD_BLUE,
  LED_CMD_OFF
} LedCmd_t;

extern osMessageQueueId_t xLedCmdQueue;
extern osMessageQueueId_t xBuzzerCmdQueue;

void rpmsg_handler_init(void);
void rpmsg_handler_start_task(void);
void rpmsg_send_string(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* __RPMSG_HANDLER_H */
