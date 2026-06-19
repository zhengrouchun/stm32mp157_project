# STM32MP157 M4核固件开发指南（队员A专用）

> **本文件为 Codex/AI 编程助手的上下文注入文档。**
> 本文档仅包含 M4 端固件开发与核间通信对接需求。

---

## 一、角色设定（Role Definition）

你是一位专业的 STM32 嵌入式固件工程师。你的任务是为 **STM32MP157** 开发板的 **Cortex-M4 核心**
编写 FreeRTOS 固件，实现硬件外设控制与核间通信。

### 你必须遵守的铁律（绝不违背）

1. **只使用 STM32 HAL 库**，严禁使用标准库（StdPeriph）或直接操作寄存器（如 `GPIOG->ODR = ...`）。
2. **操作系统为 FreeRTOS**，统一使用 CMSIS-V2 封装（`osThreadNew`、`osMessageQueuePut` 等）。
3. **编程语言为 C 语言（C99标准）**，不使用 C++。
4. **严禁重新初始化任何引脚或时钟**，所有 `MX_GPIO_Init()`、`SystemClock_Config()`、`MX_TIM2_Init()`
   等函数已在 `main.c` 中由 CubeMX 生成，直接调用即可，不得修改。
5. **每次输出必须是完整文件**，禁止使用省略号（`// ... existing code ...`），每个 `.c` 和 `.h`
   文件必须可以直接复制粘贴后编译通过。
6. **新功能必须写在独立源文件中**，`main.c` 中只暴露极少的初始化 API（如 `ws2812_task_init()`）。
7. **关键逻辑必须有详细的中文注释**，说明"为什么这样做"而不只是"做了什么"。

---

## 二、开发背景与项目描述

### 项目名称

AI 疲劳检测专注番茄钟（嵌入式竞赛作品）

### 双核架构说明

| 核心 | 职责 | 运行环境 |
|------|------|----------|
| Cortex-A7 | 摄像头采集、AI 疲劳检测（MediaPipe/SVM）、PyQt5 UI、数据库 | Linux (OpenSTLinux) |
| Cortex-M4 | 硬件外设驱动（LED/蜂鸣器/WS2812B）、接收 A7 指令并执行 | FreeRTOS |

**数据流向：**

```
A7(Linux) ──RPMsg──> M4(FreeRTOS) ──HAL──> WS2812B / 蜂鸣器 / 状态LED
```

A7 检测到用户疲劳后，通过 OpenAMP/RPMsg 发送字符串指令，M4 接收后控制对应外设给出反馈。

### 对接节点（4天后）

队员B（A7端）将提供：已能发送 RPMsg 字符串指令的 Python 脚本。
本端（M4）需提供：能正确解析并执行指令的完整固件 `.elf` 文件。

---

## 三、硬件资源分配（Ground Truth）

> ⚠️ **以下配置已在 STM32CubeMX 中完成，严禁重新初始化。直接调用 HAL API 即可。**

### 3.1 时钟配置（来自 CubeMX Clock Configuration 截图）

| 时钟域 | 频率 | 说明 |
|--------|------|------|
| M4 主时钟（HSI） | **64 MHz** | MCU Clock Mux 选 HSI，经 MCUDIV /1 |
| APB1 Timer Clock (PCLK1 × 1) | 64 MHz | TIM2 所在总线，无倍频 |
| APB2 Timer Clock | 64 MHz | 备用 |

**重要推导（WS2812B 时序参数，已验证）：**

```
TIM2 时钟 = 64 MHz
分频 PSC = 0（即不分频，计数频率 64 MHz）
ARR = 79（周期 = 80 计数 = 1.25 µs，对应 800 kHz）
CCR for "0码"（高电平 0.4 µs）= 26
CCR for "1码"（高电平 0.85 µs）= 52
```

> 这些值已硬编码进固件，**不得**依赖 `HAL_RCC_GetPCLK1Freq()` 动态查询（remoteproc
> 启动模式下 RCC 查询不可靠，会导致颜色显示异常）。

### 3.2 引脚分配

| 外设 | GPIO | 引脚 | 触发方式 | 备注 |
|------|------|------|----------|------|
| WS2812B RGB灯带 | GPIOB | Pin 10 (PB10) | TIM2_CH3 PWM + DMA | 5V供电，建议74AHCT125电平转换 |
| 蜂鸣器 | GPIOG | Pin 7 (PG7) | 低电平触发 | HAL_GPIO_WritePin 直接控制 |
| 状态指示灯 | GPIOD | Pin 6 (PD6) | 低电平点亮 | HAL_GPIO_WritePin 直接控制 |

### 3.3 核间通信

- 框架：RPMsg
- A7 端节点：/dev/ttyRPMSG0
  • 指令格式：ASCII 字符串，以 `\n` 结尾
- M4 端使用 `OPENAMP_Init()` 初始化，通过 `RPMSG_HDR` 虚拟串口或自定义 endpoint 接收消息
- A7 端写入 `/dev/ttyRPMSG0` 驱动发送字符串

## 四、双向指令协议（A7 与 M4 通信标准）

所有指令为 ASCII 字符串，必须以 `\n` 结尾，接收端需去除换行符再解析。

### 4.1 A7 发送，M4 接收与执行
| A7 发送指令字符串 | M4 执行动作                   | M4 执行后须立即回复给A7的确认(ACK)                    |
| ----------------- | ----------------------------- | ----------------------------------------------------- |
| `LED:R\n`         | WS2812B 全部亮红色 (疲劳警告) | `ACK:LED:R\n`                                         |
| `LED:G\n`         | WS2812B 全部亮绿色 (专注模式) | `ACK:LED:G\n`                                         |
| `LED:B\n`         | WS2812B 全部亮蓝色 (休息模式) | `ACK:LED:B\n`                                         |
| `LED:OFF\n`       | WS2812B 全部熄灭              | `ACK:LED:OFF\n`                                       |
| `BEEP:1\n`        | 蜂鸣器响 1 秒（持续警报）     | `ACK:BEEP:OK\n`                                       |
| `BEEP:3\n`        | 蜂鸣器响 3 次，每次 500ms     | `ACK:BEEP:OK\n`                                       |
| `STATUS:OK\n`     | 状态 LED 闪烁一次             | `ACK:STATUS:OK\n`                                     |
| *(未知指令)*      | 无动作或状态LED快闪报错       | `ERR:UNKNOWN_CMD\n`                                   |

| 触发条件                   | M4 发送给 A7 的字符串 | 含义                          |
| -------------------------- | --------------------- | ----------------------------- |
| 物理按键 (KEY0/PG3) 被按下 | `EVENT:BTN_PRESS\n`   | 通知A7端手动打断/强制解除警报 |
| 硬件初始化失败             | `EVENT:ERR_HAL\n`     | 严重错误，通知UI报错          |

## 五、目录结构

```
M4_Firmware/
├── Core/
│   ├── Inc/
│   │   ├── main.h                  # CubeMX 生成，不修改
│   │   ├── FreeRTOSConfig.h        # FreeRTOS 配置，不修改
│   │   ├── ws2812b.h               # WS2812B 驱动头文件
│   │   ├── task_led.h              # LED 控制任务头文件
│   │   ├── task_buzzer.h           # 蜂鸣器控制任务头文件
│   │   └── rpmsg_handler.h         # RPMsg 接收与指令解析头文件
│   └── Src/
│       ├── main.c                  # CubeMX 生成，仅添加任务启动调用
│       ├── ws2812b.c               # WS2812B 底层驱动（TIM2 PWM+DMA）
│       ├── task_led.c              # FreeRTOS LED 控制任务
│       ├── task_buzzer.c           # FreeRTOS 蜂鸣器控制任务
│       └── rpmsg_handler.c         # RPMsg 轮询任务 + 指令路由
├── Middlewares/
│   └── Third_Party/
│       ├── FreeRTOS/               # CubeMX 生成，不修改
│       └── OpenAMP/                # CubeMX 生成，不修改
└── README.md                       # 本文件
```

## 六、软件架构与 FreeRTOS 任务框架

### 6.1 任务总览

```
┌─────────────────────────────────────────────────────┐
│                   FreeRTOS Scheduler                │
├──────────────┬──────────────┬───────────────────────┤
│ Task: RPMsg  │ Task: LED    │ Task: Buzzer           │
│ 优先级: 3    │ 优先级: 2    │ 优先级: 1              │
│ 栈: 1024字   │ 栈: 512字    │ 栈: 256字              │
│              │              │                        │
│ 轮询RPMsg    │ 等待队列消息 │ 等待队列消息           │
│ 解析指令字符串│ 调用ws2812b │ 控制PG7 GPIO           │
│ 向两个队列   │ 设置颜色     │ 产生PWM/延时序列       │
│ 投递命令     │              │                        │
└──────┬───────┴──────┬───────┴────────┬───────────────┘
       │              │                │
       ▼              ▼                ▼
  xLedCmdQueue   ws2812b_set_color  HAL_GPIO_WritePin
  xBuzzerCmdQueue (DMA传输)          (PG7 蜂鸣器)
```

### 6.2 队列定义

```c
/* 在 rpmsg_handler.c 中定义，其他文件 extern 引用 */
QueueHandle_t xLedCmdQueue;     /* 容量: 4，元素类型: uint8_t (LED颜色枚举) */
QueueHandle_t xBuzzerCmdQueue;  /* 容量: 4，元素类型: uint8_t (蜂鸣次数) */
```

### 6.3 枚举定义（共用）

```c
/* 在 rpmsg_handler.h 中定义 */
typedef enum {
    LED_CMD_RED  = 0,
    LED_CMD_GREEN,
    LED_CMD_BLUE,
    LED_CMD_OFF
} LedCmd_t;
```

---

## 七、模块划分与接口规范

### 7.1 `ws2812b.c / ws2812b.h`

**职责：** WS2812B 单总线时序底层驱动，使用 TIM2_CH3 PWM + DMA 方式输出。

**关键参数（已硬编码，禁止动态计算）：**

```c
#define WS2812B_ARR          79U   /* 周期计数值，对应 800kHz */
#define WS2812B_BIT_0_CCR    26U   /* 0码高电平宽度：约 0.4µs */
#define WS2812B_BIT_1_CCR    52U   /* 1码高电平宽度：约 0.85µs */
#define WS2812B_NUM_LEDS     12U    /* 灯珠数量，按实际修改 */
#define WS2812B_RESET_BITS   50U   /* 复位码：低电平至少 50µs，填充 50 个 CCR=0 */
```

**DMA 缓冲区布局：**

```
[0 .. 24*N-1]    每颗灯 24 bit（GRB 顺序）各对应一个 CCR 值
[24*N .. 24*N+49] 50 个 0，产生 RESET 信号
```

**对外接口：**

```c
/* ws2812b.h */
void ws2812b_init(void);                                    /* 启动 DMA，只调用一次 */
void ws2812b_set_all(uint8_t r, uint8_t g, uint8_t b);     /* 设置全部灯珠颜色并发送 */
void ws2812b_set_one(uint8_t idx, uint8_t r, uint8_t g, uint8_t b); /* 设置单颗 */
void ws2812b_send(void);                                    /* 触发 DMA 传输 */
void ws2812b_clear(void);                                   /* 全部熄灭 */
```

**依赖：** 需要 `extern TIM_HandleTypeDef htim2;`（在 main.c 中由 CubeMX 定义）。

---

### 7.2 `task_led.c / task_led.h`

**职责：** FreeRTOS 任务，阻塞等待 `xLedCmdQueue`，根据命令调用 ws2812b 接口。

**对外接口：**

```c
/* task_led.h */
void task_led_init(void);   /* 创建任务和队列，在 main.c 的 StartDefaultTask 之前调用 */
```

**任务逻辑伪代码：**

```
loop:
    等待 xLedCmdQueue（阻塞，无超时）
    switch 命令:
        RED:   ws2812b_set_all(255, 0, 0)
        GREEN: ws2812b_set_all(0, 255, 0)
        BLUE:  ws2812b_set_all(0, 0, 255)
        OFF:   ws2812b_clear()
    ws2812b_send()
    状态LED翻转一次（PD6，表示收到指令）
```

---

### 7.3 `task_buzzer.c / task_buzzer.h`

**职责：** FreeRTOS 任务，等待 `xBuzzerCmdQueue`，执行蜂鸣序列。

**对外接口：**

```c
/* task_buzzer.h */
void task_buzzer_init(void);  /* 创建任务，在 main.c 中调用 */
```

**蜂鸣控制：**

```c
/* 低电平触发：置低=开，置高=关 */
HAL_GPIO_WritePin(GPIOG, GPIO_PIN_7, GPIO_PIN_RESET); /* 蜂鸣器开 */
osDelay(duration_ms);
HAL_GPIO_WritePin(GPIOG, GPIO_PIN_7, GPIO_PIN_SET);   /* 蜂鸣器关 */
```

**命令格式：** `xBuzzerCmdQueue` 接收蜂鸣次数（uint8_t），每次响 500ms，间隔 200ms。

---

### 7.4 `rpmsg_handler.c / rpmsg_handler.h`

**职责：** OpenAMP 初始化、RPMsg endpoint 创建、轮询接收消息、字符串解析、向队列投递命令。

**对外接口：**

```c
/* rpmsg_handler.h */
void rpmsg_handler_init(void);  /* 初始化 OpenAMP，创建轮询任务 */

/* 其他文件通过 extern 访问队列句柄 */
extern QueueHandle_t xLedCmdQueue;
extern QueueHandle_t xBuzzerCmdQueue;
```

**指令解析逻辑：**

```c
/* 接收到字符串后，去掉末尾 \r\n，再 strcmp/strncmp */
if      (strncmp(buf, "LED:R",   5) == 0) → 投递 LED_CMD_RED
else if (strncmp(buf, "LED:G",   5) == 0) → 投递 LED_CMD_GREEN
else if (strncmp(buf, "LED:B",   5) == 0) → 投递 LED_CMD_BLUE
else if (strncmp(buf, "LED:OFF", 7) == 0) → 投递 LED_CMD_OFF
else if (strncmp(buf, "BEEP:",   5) == 0) → 解析数字，投递到 xBuzzerCmdQueue
else if (strncmp(buf, "STATUS:", 7) == 0) → 翻转 PD6 状态LED
```

---

## 八、main.c 允许添加的内容

CubeMX 生成的 `main.c` 中，**只允许**在以下位置添加代码，其余部分不得修改：

```c
/* USER CODE BEGIN Includes */
#include "rpmsg_handler.h"
#include "task_led.h"
#include "task_buzzer.h"
/* USER CODE END Includes */

/* USER CODE BEGIN 2 */
/* 初始化各模块（在 MX 初始化之后，任务调度器启动之前） */
rpmsg_handler_init();
task_led_init();
task_buzzer_init();
/* USER CODE END 2 */
```

---

## 九、开发规范速查

| 类别 | 规范 |
|------|------|
| 头文件保护 | 必须使用 `#ifndef __MODULE_H` / `#define` / `#endif` |
| 全局变量 | 跨文件共享的用 `extern`，避免重复定义 |
| 中断/DMA回调 | 在对应 `.c` 文件中实现 `HAL_TIM_PWM_PulseFinishedCallback`，通过信号量通知任务 |
| 延时 | FreeRTOS 任务中一律用 `osDelay(ms)` 或 `vTaskDelay(pdMS_TO_TICKS(ms))`，禁止 `HAL_Delay` |
| 栈溢出检测 | `FreeRTOSConfig.h` 中开启 `configCHECK_FOR_STACK_OVERFLOW 2`，实现 `vApplicationStackOverflowHook` |
| 错误处理 | HAL 返回值非 `HAL_OK` 时，翻转 PD6 LED 进行报错指示 |
| DMA 缓冲区 | 必须声明为 `static uint32_t dma_buf[]`，且为全局/静态（不能是栈上变量） |

---

## 十、需要队员A提供给 Codex 的必要资料清单

每次向 Codex 提问时，请同时提供以下信息（根据具体问题选取）：

### 必须提供（每次）
- [ ] 本 README 全文（作为上下文）
- [ ] 当前报错信息（完整 Build Output 或 HardFault 调试信息）
- [ ] 你正在修改的文件完整内容

### 按需提供
- [ ] `main.c` 中 `MX_TIM2_Init()` 函数完整内容（调试 WS2812B 时序时需要）
- [ ] CubeMX 生成的 `stm32mp1xx_hal_conf.h`（调试 HAL 超时问题时需要）
- [ ] FreeRTOS 任务状态（如 `vTaskList()` 输出，调试任务挂死时需要）
- [ ] 逻辑分析仪截图（调试 WS2812B 时序时需要）
- [ ] A7 端发送的原始字符串（调试 RPMsg 解析时需要）

### 对接前需要确认（与队员B）
- [ ] A7 端使用的 RPMsg 设备节点名称（`/dev/ttyRPMSG0` ）
- [ ] A7 端发送指令带 `\n`
- [ ] WS2812B 灯珠实际数量12颗
- [ ] 需要 M4 回复确认消息给 A7

---

## 十一、已知坑与解决方案

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| WS2812B 颜色固定为"半黄半粉" | remoteproc 启动时 RCC 时钟查询不可靠 | **硬编码** ARR=79, CCR0=26, CCR1=52，不使用 `HAL_RCC_GetPCLK1Freq()` |
| DMA 传输后 LED 不更新 | DMA 缓冲区是局部变量被栈覆盖 | 声明为 `static` 或全局变量 |
| FreeRTOS 任务互相阻塞 | 队列满时投递方无超时 | `xQueueSend` 使用 `pdMS_TO_TICKS(10)` 超时而非 `portMAX_DELAY` |
| RPMsg 消息丢失 | 轮询间隔太长 | RPMsg 轮询任务延时不超过 10ms |
| 蜂鸣器一直响 | GPIO 初始状态为低 | 在 `task_buzzer_init()` 中先拉高 PG7 |
| `uint16_t` DMA 缓冲区颜色错误 | TIM2 CCR 为 32 位寄存器 | DMA 缓冲区必须是 `uint32_t[]` |

---

## 十二、编译与烧录流程

```bash
echo stop > /sys/class/remoteproc/remoteproc0/state
将 .elf 传到开发板利用你已经连上的 MobaXterm：
把你电脑上的那个 .elf 文件，直接拖拽到这个 /lib/firmware/ 目录里。
第四步：在 MobaXterm 的黑色终端窗口里，依次输入以下命令：
echo MP157_M4_FreeRTOS_CM4.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
 echo stop > /sys/class/remoteproc/remoteproc0/state

# 2. 传输到开发板 A7 端
scp Debug/M4_Firmware.elf root@192.168.1.xxx:/lib/firmware/

# 3. 在 A7 端加载 M4 固件
echo -n "M4_Firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# 4. 查看 M4 端日志
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

# 5. 停止 M4 固件
echo stop > /sys/class/remoteproc/remoteproc0/state
```

---

## 十三、给 AI 的 Prompt 模板（推荐每次使用）

当前任务：实现 ws2812b.c 中的 ws2812b_set_all 函数。

现有文件内容：

要求：
1. 严格遵守本 README 所有铁律（WS2812B_NUM_LEDS=12、硬编码时序等）
2. 输出完整可直接编译的文件
3. 添加详细中文注释
4. 错误处理：HAL失败时翻转 PD6 LED
