/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rpmsg_handler.c
  * @brief   RPMsg/OpenAMP 通信模块实现 —— 阶段1：打通收发链路
  *
  *          这一版唯一的目标：验证 A7 -> M4 的字符串能稳定到达，
  *          并且 M4 -> A7 的回复也能被 A7 收到。
  *          不涉及指令解析、不涉及xLedCmdQueue/xBuzzerCmdQueue投递，
  *          这些会在阶段5（指令路由）里补上。
  ******************************************************************************
  */
/* USER CODE END Header */

#include "rpmsg_handler.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "cmsis_os.h"
#include "openamp.h"
#include "virt_uart.h"

/* ========================================================================
 * 私有变量
 * ====================================================================== */

/* 虚拟串口句柄。原来main.c里是全局变量huart0，现在变成本文件的static变量，
 * main.c和其他文件都拿不到它，只能通过本文件暴露的两个接口跟它打交道——
 * 这就是"main.c只暴露极少API"这条铁律的具体体现。 */
static VIRT_UART_HandleTypeDef huart0;

/* 任务句柄。FreeRTOS要求任务创建之后这个句柄要能一直被框架引用，
 * 必须是static/全局，不能是函数内的局部变量。 */
static osThreadId_t RpmsgTaskHandle;

/* osThreadNew的属性结构体同理必须是static：如果是局部变量，
 * 函数返回后这块内存被栈回收，但FreeRTOS内部可能还在引用里面的
 * name字符串指针，会导致后续调试输出乱码甚至跑飞。
 * 这跟README"已知坑"里"DMA缓冲区必须是static"是同一类问题。 */
static const osThreadAttr_t RpmsgTask_attributes = {
  .name = "RpmsgTask",
  .stack_size = 1024 * 4,                          /* 1024字=4096字节，对应README任务表 */
  .priority = (osPriority_t) osPriorityAboveNormal, /* 三个任务里优先级最高，对应README"优先级3" */
};

/* ========================================================================
 * 私有函数声明
 * ====================================================================== */

static void RpmsgPollingTask(void *argument);
static void RPMSG_RxCpltCallback(VIRT_UART_HandleTypeDef *huart);
static void RPMSG_ErrorTrap(void);

/* ========================================================================
 * 对外接口实现
 * ====================================================================== */

void rpmsg_handler_init(void)
{
  /* 初始化虚拟串口：在OpenAMP/RPMsg通道（main.c里MX_OPENAMP_Init已经建好）
   * 之上建立一层"虚拟串口"抽象，对应A7端的 /dev/ttyRPMSG0 设备节点。
   * 这一步不依赖FreeRTOS内核状态，可以放心在osKernelInitialize()之前调用。 */
  if (VIRT_UART_Init(&huart0) != VIRT_UART_OK)
  {
    /* 初始化失败属于致命错误：RPMsg是A7/M4之间唯一的通信渠道，
     * 一旦这里失败，根本没办法把错误告诉A7（消息发不出去），
     * 只能用PD6状态灯做本地报警。 */
    RPMSG_ErrorTrap();
    return;
  }

  /* 注册接收回调：以后A7往/dev/ttyRPMSG0写数据，OpenAMP处理完之后
   * 会调用RPMSG_RxCpltCallback。注意这个回调不是在中断里跑的，
   * 而是在下面RpmsgPollingTask任务调用OPENAMP_check_for_message()时
   * 被同步触发的，所以回调里可以放心用printf，以后也可以放心用
   * 普通版xQueueSend（不需要FromISR后缀那个版本）。 */
  if (VIRT_UART_RegisterCallback(&huart0, VIRT_UART_RXCPLT_CB_ID, RPMSG_RxCpltCallback) != VIRT_UART_OK)
  {
    RPMSG_ErrorTrap();
    return;
  }

  printf("[rpmsg_handler] VIRT_UART init OK.\r\n");
}

void rpmsg_handler_start_task(void)
{
  /* 这是整个文件最关键的一步：之前main.c的bug是
   * OPENAMP_check_for_message()被放在osKernelStart()后面那个
   * 永远跑不到的死循环里，导致A7发的消息根本不会被处理。
   * 现在把它放进一个真正会被FreeRTOS调度执行的任务里，问题解决。 */
  RpmsgTaskHandle = osThreadNew(RpmsgPollingTask, NULL, &RpmsgTask_attributes);

  if (RpmsgTaskHandle == NULL)
  {
    /* 任务创建失败通常是堆内存不够（FreeRTOSConfig.h里的
     * configTOTAL_HEAP_SIZE太小），同样是致命错误。 */
    RPMSG_ErrorTrap();
  }
}

void rpmsg_send_string(const char *str)
{
  /* 用静态缓冲区拼接字符串+换行符：
   * 1. 必须static——避免局部数组在函数返回后被栈覆盖，
   *    而VIRT_UART_Transmit拿到的是指针，传输不一定是瞬间完成的。
   * 2. 128字节足够装下目前协议里最长的指令/ACK/EVENT字符串。
   * 3. 阶段1还没加互斥锁：目前只有这一个任务会调用这个函数，
   *    安全。等阶段4/5接入task_led、task_buzzer之后，如果发现
   *    多任务同时调这个函数导致内容错乱（粘包），需要在这里
   *    加osMutexId_t保护，到时候会专门处理。 */
  static char tx_buf[128];
  size_t len;

  len = strlen(str);
  if (len >= sizeof(tx_buf) - 2) /* 留1字节给\n，1字节给安全余量 */
  {
    len = sizeof(tx_buf) - 2;
  }

  memcpy(tx_buf, str, len);
  tx_buf[len] = '\n';
  tx_buf[len + 1] = '\0';

  VIRT_UART_Transmit(&huart0, (uint8_t *) tx_buf, (uint16_t) (len + 1));
}

/* ========================================================================
 * 私有函数实现
 * ====================================================================== */

/**
 * @brief  RPMsg轮询任务主体：不停检查A7有没有发新消息。
 */
static void RpmsgPollingTask(void *argument)
{
  (void) argument;

  for (;;)
  {
    OPENAMP_check_for_message();

    /* 轮询间隔不能太长：README"已知坑"里写了"RPMsg轮询任务延时
     * 不超过10ms"，否则A7端会感觉到明显的指令响应延迟。
     * 用5ms留一点余量，CPU也不会被占满。 */
    osDelay(5);
  }
}

/**
 * @brief  虚拟串口接收完成回调。
 *         阶段1版本：原样打印收到的内容+回一条ACK，验证链路通不通。
 *         阶段5会在这里加上去除\r\n、strncmp比对指令、
 *         投递xLedCmdQueue/xBuzzerCmdQueue、回真正ACK的逻辑。
 */
static void RPMSG_RxCpltCallback(VIRT_UART_HandleTypeDef *huart)
{
  /* huart->pRxBuffPtr指向收到的数据，huart->RxXferSize是字节数。
   * 注意这块数据不一定以\0结尾，不能直接当C字符串用%s打印，
   * 必须用RxXferSize限定长度，用%.*s这种写法，否则printf可能
   * 越界读到脏数据甚至触发HardFault。 */
  printf("[rpmsg_handler] RX (%d bytes): %.*s\r\n",
         huart->RxXferSize,
         (int) huart->RxXferSize,
         (char *) huart->pRxBuffPtr);

  /* 阶段1先回一个最简单的"收到了"，方便你在A7端立刻看到双向链路都通，
   * 不用等阶段5指令解析做完才能验证。 */
  rpmsg_send_string("ACK:RAW_RECEIVED");
}

/**
 * @brief  致命错误处理：点亮PD6状态灯并永久停在这里。
 *         对应README开发规范："HAL返回值非HAL_OK时，翻转PD6 LED报错"。
 *         这里选"常亮+卡死"而不是"闪烁"，方便和阶段5里"未知指令"
 *         之类的非致命错误（那种应该用闪烁）做区分。
 */
static void RPMSG_ErrorTrap(void)
{
  /* PD6低电平点亮，参考README 3.2引脚表。 */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, GPIO_PIN_RESET);

  for (;;)
  {
    /* 卡死在这里。如果是在rpmsg_handler_init()里失败的，
     * 此时调度器还没启动，不能调用osDelay，只能用空循环。 */
  }
}
