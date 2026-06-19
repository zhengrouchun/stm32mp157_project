/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rpmsg_handler.c
  * @brief   RPMsg/OpenAMP 通信模块，实现 A7 指令解析、ACK 回复和任务路由。
  ******************************************************************************
  */
/* USER CODE END Header */

#include "rpmsg_handler.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cmsis_os2.h"
#include "main.h"
#include "openamp.h"
#include "virt_uart.h"

#define RPMSG_RX_BUF_SIZE       128U
#define RPMSG_TX_BUF_SIZE       128U
#define RPMSG_QUEUE_DEPTH       4U
#define RPMSG_POLL_DELAY_MS     5U
#define RPMSG_QUEUE_TIMEOUT_MS  10U

osMessageQueueId_t xLedCmdQueue;
osMessageQueueId_t xBuzzerCmdQueue;

static VIRT_UART_HandleTypeDef huart0;
static osThreadId_t RpmsgTaskHandle;
static osMutexId_t RpmsgTxMutex;
static uint8_t rpmsg_ready;

static const osThreadAttr_t RpmsgTask_attributes = {
  .name = "RpmsgTask",
  .stack_size = 1024U * 4U,
  .priority = (osPriority_t)osPriorityAboveNormal
};

static const osMutexAttr_t RpmsgTxMutex_attributes = {
  .name = "RpmsgTxMutex"
};

static void RpmsgPollingTask(void *argument);
static void RPMSG_RxCpltCallback(VIRT_UART_HandleTypeDef *huart);
static void RPMSG_ParseCommand(char *cmd);
static void RPMSG_TrimLineEnd(char *cmd);
static void RPMSG_SendAckForLed(LedCmd_t cmd);
static void RPMSG_SendUnknown(void);
static uint8_t RPMSG_PostLedCommand(LedCmd_t cmd);
static uint8_t RPMSG_PostBuzzerCommand(uint8_t count);
static void RPMSG_ErrorTrap(void);

void rpmsg_handler_init(void)
{
  if (VIRT_UART_Init(&huart0) != VIRT_UART_OK)
  {
    RPMSG_ErrorTrap();
    return;
  }

  if (VIRT_UART_RegisterCallback(&huart0, VIRT_UART_RXCPLT_CB_ID, RPMSG_RxCpltCallback) != VIRT_UART_OK)
  {
    RPMSG_ErrorTrap();
    return;
  }

  rpmsg_ready = 1U;
  printf("[rpmsg_handler] VIRT_UART ready.\r\n");
}

void rpmsg_handler_start_task(void)
{
  xLedCmdQueue = osMessageQueueNew(RPMSG_QUEUE_DEPTH, sizeof(LedCmd_t), NULL);
  xBuzzerCmdQueue = osMessageQueueNew(RPMSG_QUEUE_DEPTH, sizeof(uint8_t), NULL);
  RpmsgTxMutex = osMutexNew(&RpmsgTxMutex_attributes);

  if ((xLedCmdQueue == NULL) || (xBuzzerCmdQueue == NULL) || (RpmsgTxMutex == NULL))
  {
    RPMSG_ErrorTrap();
    return;
  }

  RpmsgTaskHandle = osThreadNew(RpmsgPollingTask, NULL, &RpmsgTask_attributes);
  if (RpmsgTaskHandle == NULL)
  {
    RPMSG_ErrorTrap();
  }
}

void rpmsg_send_string(const char *str)
{
  static char tx_buf[RPMSG_TX_BUF_SIZE];
  size_t len;

  if ((str == NULL) || (rpmsg_ready == 0U))
  {
    return;
  }

  if (RpmsgTxMutex != NULL)
  {
    (void)osMutexAcquire(RpmsgTxMutex, osWaitForever);
  }

  len = strlen(str);
  if (len >= (RPMSG_TX_BUF_SIZE - 2U))
  {
    len = RPMSG_TX_BUF_SIZE - 2U;
  }

  memcpy(tx_buf, str, len);

  /* A7 端 ttyRPMSG 按行读取更稳定，所以 M4 的所有 ACK/EVENT 都统一补 '\n'。
   * 上层调用者不需要关心行结束符，避免不同任务各自拼接造成协议不一致。 */
  tx_buf[len] = '\n';
  tx_buf[len + 1U] = '\0';

  if (is_rpmsg_ept_ready(&huart0.ept))
  {
    /* 对 Linux rpmsg-tty 来说，远端 tty 的地址要等收到第一包后才确定。
     * 使用 rpmsg_sendto 显式带上 dest_addr，比依赖 VIRT_UART_Transmit 内部的
     * 默认目的地址更直观，也方便定位 ACK 发不回 A7 的问题。 */
    (void)rpmsg_sendto(&huart0.ept, (uint8_t *)tx_buf, (int)(len + 1U), huart0.ept.dest_addr);
  }

  if (RpmsgTxMutex != NULL)
  {
    (void)osMutexRelease(RpmsgTxMutex);
  }
}

static void RpmsgPollingTask(void *argument)
{
  (void)argument;

  for (;;)
  {
    OPENAMP_check_for_message();

    /* RPMsg 接收回调由 OPENAMP_check_for_message() 驱动，不是硬中断自动触发。
     * 5ms 轮询能满足 README 要求的“不超过10ms”，同时不会让 M4 长时间空转。 */
    osDelay(RPMSG_POLL_DELAY_MS);
  }
}

static void RPMSG_RxCpltCallback(VIRT_UART_HandleTypeDef *huart)
{
  char rx_buf[RPMSG_RX_BUF_SIZE];
  size_t copy_len;

  if ((huart == NULL) || (huart->pRxBuffPtr == NULL))
  {
    return;
  }

  copy_len = (size_t)huart->RxXferSize;
  if (copy_len >= RPMSG_RX_BUF_SIZE)
  {
    copy_len = RPMSG_RX_BUF_SIZE - 1U;
  }

  /* VIRT_UART 给出的接收缓冲不保证以 '\0' 结尾，不能直接 strcmp。
   * 先复制到本地数组并手动补终止符，后续字符串解析才不会越界读。 */
  memcpy(rx_buf, huart->pRxBuffPtr, copy_len);
  rx_buf[copy_len] = '\0';

  RPMSG_TrimLineEnd(rx_buf);
  RPMSG_ParseCommand(rx_buf);
}

static void RPMSG_ParseCommand(char *cmd)
{
  if (cmd == NULL)
  {
    RPMSG_SendUnknown();
    return;
  }

  if (strcmp(cmd, "LED:R") == 0)
  {
    RPMSG_SendAckForLed(LED_CMD_RED);
    (void)RPMSG_PostLedCommand(LED_CMD_RED);
  }
  else if (strcmp(cmd, "LED:G") == 0)
  {
    RPMSG_SendAckForLed(LED_CMD_GREEN);
    (void)RPMSG_PostLedCommand(LED_CMD_GREEN);
  }
  else if (strcmp(cmd, "LED:B") == 0)
  {
    RPMSG_SendAckForLed(LED_CMD_BLUE);
    (void)RPMSG_PostLedCommand(LED_CMD_BLUE);
  }
  else if (strcmp(cmd, "LED:OFF") == 0)
  {
    RPMSG_SendAckForLed(LED_CMD_OFF);
    (void)RPMSG_PostLedCommand(LED_CMD_OFF);
  }
  else if (strcmp(cmd, "BEEP:1") == 0)
  {
    rpmsg_send_string("ACK:BEEP:OK");
    (void)RPMSG_PostBuzzerCommand(1U);
  }
  else if (strcmp(cmd, "BEEP:3") == 0)
  {
    rpmsg_send_string("ACK:BEEP:OK");
    (void)RPMSG_PostBuzzerCommand(3U);
  }
  else if (strcmp(cmd, "STATUS:OK") == 0)
  {
    /* 状态灯 PD6 已按你的要求移除，这条命令仅作为 A7/M4 在线心跳确认。
     * 保留 ACK 能让 A7 端继续使用同一套通信协议，不需要因为删硬件功能而改 UI 逻辑。 */
    rpmsg_send_string("ACK:STATUS:OK");
  }
  else if (strcmp(cmd, "PING") == 0)
  {
    /* PING 是纯通信自检命令，不触发任何外设。
     * 如果 PING 都没有 ACK，问题就集中在固件版本、RPMsg发送路径或A7读取方式；
     * 如果 PING 有 ACK 但 LED/BEEP 没有，再去查外设任务和队列。 */
    rpmsg_send_string("ACK:PING");
  }
  else
  {
    RPMSG_SendUnknown();
  }
}

static void RPMSG_TrimLineEnd(char *cmd)
{
  size_t len;

  len = strlen(cmd);
  while ((len > 0U) && ((cmd[len - 1U] == '\n') || (cmd[len - 1U] == '\r') || (cmd[len - 1U] == ' ')))
  {
    cmd[len - 1U] = '\0';
    len--;
  }
}

static void RPMSG_SendAckForLed(LedCmd_t cmd)
{
  switch (cmd)
  {
    case LED_CMD_RED:
      rpmsg_send_string("ACK:LED:R");
      break;

    case LED_CMD_GREEN:
      rpmsg_send_string("ACK:LED:G");
      break;

    case LED_CMD_BLUE:
      rpmsg_send_string("ACK:LED:B");
      break;

    case LED_CMD_OFF:
    default:
      rpmsg_send_string("ACK:LED:OFF");
      break;
  }
}

static void RPMSG_SendUnknown(void)
{
  rpmsg_send_string("ERR:UNKNOWN_CMD");
}

static uint8_t RPMSG_PostLedCommand(LedCmd_t cmd)
{
  if (xLedCmdQueue == NULL)
  {
    rpmsg_send_string("EVENT:ERR_HAL");
    return 0U;
  }

  if (osMessageQueuePut(xLedCmdQueue, &cmd, 0U, RPMSG_QUEUE_TIMEOUT_MS) != osOK)
  {
    rpmsg_send_string("EVENT:ERR_HAL");
    return 0U;
  }

  return 1U;
}

static uint8_t RPMSG_PostBuzzerCommand(uint8_t count)
{
  if (xBuzzerCmdQueue == NULL)
  {
    rpmsg_send_string("EVENT:ERR_HAL");
    return 0U;
  }

  if (osMessageQueuePut(xBuzzerCmdQueue, &count, 0U, RPMSG_QUEUE_TIMEOUT_MS) != osOK)
  {
    rpmsg_send_string("EVENT:ERR_HAL");
    return 0U;
  }

  return 1U;
}

static void RPMSG_ErrorTrap(void)
{
  /* 已按你的要求去掉 PD6 状态灯，因此致命错误不再访问任何本地报警 GPIO。
   * 这里保持停机，是因为 RPMsg 初始化失败后继续运行只会让 A7 端误以为 M4 在线。 */
  for (;;)
  {
  }
}
