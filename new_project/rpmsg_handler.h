/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rpmsg_handler.h
  * @brief   RPMsg/OpenAMP 通信模块对外接口
  *          阶段1版本：只打通收发链路，收到任何字符串原样打印+回ACK，
  *          不做指令解析（LED:R / BEEP:N 这类解析逻辑在阶段5补全）。
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __RPMSG_HANDLER_H
#define __RPMSG_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  初始化虚拟串口(VIRT_UART)。只做硬件层面的准备，不创建任何
 *         FreeRTOS对象，所以可以安全地在main.c的 USER CODE BEGIN 2
 *         （即osKernelInitialize()之前）调用。
 */
void rpmsg_handler_init(void);

/**
 * @brief  创建RPMsg轮询任务。这里面用到osThreadNew()，
 *         必须在osKernelInitialize()之后调用，否则会创建失败。
 *         调用位置：freertos.c 的 MX_FREERTOS_Init() 函数里，
 *         /* USER CODE BEGIN RTOS_THREADS */ 区域。
 */
void rpmsg_handler_start_task(void);

/**
 * @brief  M4主动向A7发送一条字符串消息（自动在末尾补\n）。
 *         以后task_led.c / task_buzzer.c上报报错事件、
 *         按键EVENT上报，都会调用这个函数。
 * @param  str  要发送的字符串内容，不需要自己带\n
 */
void rpmsg_send_string(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* __RPMSG_HANDLER_H */
