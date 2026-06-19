# STM32MP157 M4核固件开发指南（队员A专用）

> **本文件为 Codex/AI 编程助手的上下文注入文档。**
> 每次新建对话时，请将本文件全文粘贴至 System Prompt 或对话首条消息。

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
  • 指令格式：ASCII 字符串，以 \n 结尾
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
| *(未知指令)*      | 无动作或状态LED快闪报错       | `ERR:UNKNOWN_CMD\4.2 M4 主动上报，A7 接收（事件机制） |

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

**M4 发送数据到 A7 的接口规范：**
必须在 `rpmsg_handler.c` 中实现并暴露发送字符串的封装函数。

```c
/* rpmsg_handler.h */
/* M4 主动向 A7 发送字符串数据的接口（自动处理长度和末尾追加\n） */
void rpmsg_send_string(const char* str);
```

发送实现要求： AI 在实现 rpmsg_send_string 时，必须调用 OpenAMP 的 OPENAMP_send 或 rpmsg_send 底层 API，并确保数据成功压入 virtio 环形缓冲区。

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
- [ ] 队员B `fatigue_detector.py` 暴露的 `update()` / `get_alert()` 接口签名（编写 `main.py` 状态机前需要，见第十九节）
- [ ] `config.json` 的字段格式（Few-shot 个人阈值，UI 待机态读取需要，见第十七节）

### 编写 A7 端模块时按需提供（见第十四～二十三节）
- [ ] 当前正在修改的模块完整源码（`db_manager.py` / `pomodoro_ui.py` / `rpmsg_client.py` / `main.py`）
- [ ] 实际运行报错的完整堆栈（Python Traceback）
- [ ] 当前数据库表结构（`.schema` 输出，调试 SQL 报错时需要）
- [ ] 实测的 RPMsg 收发延时（调试整合阶段的状态切换卡顿时需要）

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
| 任务饿死导致灯光不响应 | 高优先级任务死循环或队列阻塞过长 | 如果出现饿死现象，允许微调任务阻塞时间（`osDelay`/`osWait` 超时参数）以让出 CPU |

---

## 十二、编译与烧录流程

```bash
# 1. 停止当前正在运行的 M4 固件
echo stop > /sys/class/remoteproc/remoteproc0/state

# 2. 传输新编译的 .elf 到开发板 /lib/firmware/ 目录
#    方式一：命令行 scp
scp Debug/M4_Firmware.elf root@192.168.1.xxx:/lib/firmware/
#    方式二：用 MobaXterm 左侧文件面板，直接把电脑上的 .elf 拖拽到该目录

# 3. 指定本次加载的固件文件名
echo -n "M4_Firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware

# 4. 启动 M4 固件
echo start > /sys/class/remoteproc/remoteproc0/state

# 5. 查看 M4 端日志（printf 调试输出）
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

# 6. 需要重新烧录时，先停止再重复步骤 2-4
echo stop > /sys/class/remoteproc/remoteproc0/state
```

---

## 十三、给 AI 的 Prompt 模板（推荐每次使用）

当前任务：实现 ws2812b.c 中的 ws2812b_set_all 函数。

现有文件内容：

要求：
1. 严格遵守本README所有铁律（WS2812B_NUM_LEDS=12、硬编码时序等）

2. 输出完整可直接编译的文件

3. 添加详细中文注释

4. 错误处理：HAL失败时翻转PD6 LED

## 十四、A7 端开发环境与运行前提

队员A 除了 M4 固件外，还负责为联调阶段输出三个 A7 端 Python 模块（数据库 / UI / 主程序），运行环境如下：

### 14.1 硬件与系统

- 屏幕：800×480 电容触摸屏（PyQt5 界面尺寸需按此分辨率设计，避免控件超出可视区域）
- 系统：OpenSTLinux（开发板），建议先在 PC（Windows/Mac/Linux）本地跑通逻辑，再迁移到板上

### 14.2 依赖安装

```bash
pip install PyQt5
# sqlite3 为 Python 内置库，无需手动安装
```

### 14.3 本地测试步骤

1. 新建文件夹 `A7_Project`
2. 按本 README 第十六、十七节模板分别保存 `db_manager.py`、`pomodoro_ui.py`
3. 分别独立运行 `python db_manager.py`、`python pomodoro_ui.py`，确认各自模块单测通过
4. 两个模块都验证无误后，再进行第十九节的 `main.py` 整合（先分后合，禁止一次性整体编写）

---

## 十五、A7 端目录结构与模块划分

```
A7_Python_App/
├── config.json            # 配置文件：个人疲劳阈值、Few-shot 个性化参数
├── tasks.db               # SQLite 数据库文件（首次运行自动生成）
├── db_manager.py          # 模块一：数据库操作封装（队员A负责，见第十六节）
├── pomodoro_ui.py         # 模块二：UI 界面与倒计时圆环（队员A负责，见第十七节）
├── rpmsg_client.py        # 模块三：A7→M4 RPMsg 通信封装（队员A负责，见第十八节）
└── main.py                # 模块四：全流程串联主程序（队员A负责，见第十九节）
```

> `fatigue_detector.py`（滑动窗口疲劳判定）由队员B提供，`main.py` 只需调用其 `update()` / `get_alert()` 接口，不需要队员A重新实现。

**编码风格（与 M4 端铁律保持一致）：**

1. 变量名小写加下划线（如 `time_left`），类名大驼峰（如 `DBManager`）
2. UI 与数据逻辑严格解耦：`pomodoro_ui.py` 不直接操作数据库，只通过 `db_manager.py` 暴露的方法读写
3. 每个模块必须有独立的 `if __name__ == "__main__":` 自测试区，验证通过后才能被其他模块导入

---

## 十六、`db_manager.py` —— 数据库模块规范

**职责：** 封装 SQLite 数据库操作，外层 UI 只调用类方法，不直接写 SQL 语句。

**数据表设计：**

| 表名 | 字段 | 说明 |
|------|------|------|
| `todos` | `id INTEGER PRIMARY KEY AUTOINCREMENT`, `title TEXT NOT NULL`, `done INTEGER DEFAULT 0` | 待办事项，`done`: 0=未完成，1=已完成 |
| `focus_records` | `id INTEGER PRIMARY KEY AUTOINCREMENT`, `date TEXT`, `duration INTEGER`, `fatigue_count INTEGER` | 每次专注结束写入：日期、时长（秒）、疲劳触发次数，供统计折线图使用 |

**对外接口：**

```python
class DBManager:
    def __init__(self, db_name="tasks.db"): ...
    def add_todo(self, title): ...                                   # 新增待办
    def get_all_todos(self): ...                                     # 查询所有未完成待办
    def mark_done(self, todo_id): ...                                # 标记完成（第十七节 UI 双击需要）
    def add_focus_record(self, date, duration, fatigue_count): ...   # 专注结束写入统计
    def get_focus_records(self, limit=30): ...                       # 供统计折线图读取
```

> `mark_done` / `add_focus_record` / `get_focus_records` 是基础模板中缺失、但 UI 整合阶段（第十七、十九节）必须用到的接口，编写时需一并实现。

**关键规范：**

- 统一使用 `with sqlite3.connect(self.db_name) as conn:` 上下文管理器，确保异常时自动回滚、连接自动关闭
- SQL 参数必须使用占位符 `?`，禁止用 f-string 拼接 SQL（防止注入、避免特殊字符崩溃）
- 每个方法添加详细中文注释，说明"为什么这样做"而不仅是"做了什么"

---

## 十七、`pomodoro_ui.py` —— UI 模块规范

**职责：** PyQt5 主界面，包含倒计时圆环（`QPainter` 绘制）与 Todo 列表，并驱动四个界面状态切换。

**界面四态（对应分工文档第三阶段集成需求）：**

| 状态 | 触发条件 | 界面表现 | 关联动作 |
|------|----------|----------|----------|
| 待机 | 启动 / 番茄钟结束 | `QListWidget` 展示 Todo，双击编辑，底部"开始专注"按钮 | 调用 `rpmsg_client.send("LED:OFF")` |
| 专注 | 点击"开始专注" | `QPainter` 圆弧倒计时 + 右上角 AI 监测小窗（`QLabel` 显示队员B处理后画面） | 调用 `rpmsg_client.send("LED:G")` |
| 报警 | 队员B `fatigue_detector.get_alert()` 返回 True | 全屏红色闪烁 | 调用 `rpmsg_client.send("LED:R")` 与 `rpmsg_client.send("BEEP:3")` |
| 统计 | 番茄钟自然结束 | `pyqtgraph` 折线图展示专注得分时序 | 调用 `db_manager.add_focus_record(...)` 写库 |

**对外接口：**

```python
class PomodoroArc(QWidget):
    def __init__(self): ...
    def start_timer(self): ...
    def update_timer(self): ...       # QTimer 每秒触发，剩余时间减1并调用 self.update()
    def paintEvent(self, event): ...  # 绘制背景环 + 动态进度环

class MainWindow(QWidget):
    def __init__(self): ...
    def show_idle_view(self): ...     # 待机态
    def show_focus_view(self): ...    # 专注态
    def show_alert_view(self): ...    # 报警态
    def show_stats_view(self): ...    # 统计态
```

> `MainWindow` 及四个 `show_*_view` 方法是基础模板未覆盖、但必须在 `main.py` 整合前完成的部分（详见第十九节）。

**关键技术点：**

- `paintEvent` 中必须开启 `painter.setRenderHint(QPainter.Antialiasing)`（抗锯齿），否则圆环边缘会有锯齿
- 角度换算：PyQt 规定 1 度 = 16 计数单位，画整圆需要 `16 * 360`
- 倒计时方向：`span_angle = int(-16 * 360 * progress_ratio)`，负号表示顺时针递减
- Todo 列表双击编辑：`QListWidget` 的 `itemDoubleClicked` 信号连接到弹窗编辑，编辑完成后调用 `db_manager` 对应方法更新

---

## 十八、A7→M4 RPMsg 通信封装规范（Python 侧）

**职责：** 将第四节"双向指令协议"在 A7 端封装为可被 `pomodoro_ui.py` / `main.py` 直接调用的简单函数，并处理 ACK 等待与超时。

**对外接口：**

```python
class RPMsgClient:
    def __init__(self, dev_path="/dev/ttyRPMSG0"): ...
    def send(self, cmd: str) -> bool:
        """发送指令（自动追加 \n），等待 M4 的 ACK，超时返回 False"""
    def read_event(self, timeout=0.1):
        """非阻塞读取 M4 主动上报的 EVENT:* 字符串（如按键中断）"""
```

**实现要点：**

- 必须以 `\n` 结尾发送，接收 ACK 时要先去除换行符再比较
- 超时建议设置为 200ms～500ms（M4 端 RPMsg 轮询任务延时不超过 10ms，正常情况下应能很快收到 ACK）
- 与队员B约定：调试初期可先用 USB 转串口模拟通信，避免一开始就卡在 RPMsg 驱动问题上；核间通信验证通过后再切换为正式的 `/dev/ttyRPMSG0`（分工文档中明确列出的风险预案）

---

## 十九、`main.py` —— 主程序整合规范

**职责：** 串联 `db_manager.py`（任务读取）、`pomodoro_ui.py`（界面与计时）、`rpmsg_client.py`（硬件反馈）与队员B提供的 `fatigue_detector.py`（AI 判定），实现完整闭环。

**状态机与颜色映射（汇总自第四、十七节，供整合时对照）：**

```
待机 ──点击开始专注──> 专注 ──疲劳判定为真──> 报警 ──手动/超时解除──> 待机
                          │
                          └──倒计时结束──────> 统计 ──查看后────────> 待机
```

| UI 状态 | WS2812B 颜色 | 蜂鸣器 |
|---------|--------------|--------|
| 待机 | OFF | 不响 |
| 专注 | 绿色 | 不响 |
| 报警 | 红色 | 响 3 次 |
| 统计 | OFF（或保留专注前颜色） | 不响 |

**整合顺序（必须严格按此顺序，每步验证通过才能进入下一步）：**

1. `db_manager.py` 独立跑通
2. `pomodoro_ui.py` 独立跑通（先不接 AI、不接 RPMsg，纯界面动画）
3. `rpmsg_client.py` 独立跑通（先用第四节协议表手动发送指令，确认 M4 能收到并回 ACK）
4. 三者在 `main.py` 中串联：UI 状态切换 → 调用 `rpmsg_client.send()` → 调用 `db_manager` 写库
5. 最后接入队员B的 `fatigue_detector.py`，由其 `get_alert()` 结果驱动状态机切换到"报警"

---

## 二十、硬件清单与 PCB 设计（BOM）

分工文档将 PCB 列为"可选"任务（用杜邦线飞线替代也完全可以），但技术报告需要"硬件选型"章节，建议至少整理出元件清单：

| 类别 | 元件 | 数量 | 选型理由（实际型号、价格需自行补充） |
|------|------|------|----------------------------------------|
| 主控 | STM32MP157 开发板 | 1 | 双核异构：A7 跑 Linux+AI，M4 跑 FreeRTOS 硬件控制 |
| LED | WS2812B 灯带 | 12 颗 | 单线时序控制，可独立寻址显示三态颜色 |
| 电平转换 | 74AHCT125 | 1 | WS2812B 为 5V 逻辑，MP157 GPIO 为 3.3V，需电平转换保证时序可靠 |
| 声音反馈 | 蜂鸣器（低电平触发） | 1 | 报警提示，PG7 直接驱动 |
| 输入 | 按键 KEY0 | 1 | PG3，用于手动打断报警（对应第四节 `EVENT:BTN_PRESS`） |
| 连接 | 杜邦线 / 转接板 | 若干 | 若不画 PCB，用杜邦线飞线连接即可 |

> 若决定画 PCB：用嘉立创 EDA，重点是把 WS2812B、蜂鸣器、按键的接口统一引到 MP157 扩展口上，2 天内可完成（分工文档安排）。若时间紧张，飞线方案不影响评分。

---

## 二十一、系统集成与联调计划（与队员B协作）

**第三阶段分工边界：**

| 负责人 | 内容 |
|--------|------|
| 队员A | RPMsg 调用、数据库读写、UI 的 QPainter 圆弧、WS2812B 颜色状态映射 |
| 队员B | AI 推理线程与 UI 信号槽对接、统计图表、Few-shot 向导界面 |

**每日联调测试场景（按此顺序逐项验证，不要跳步）：**

| 场景 | 测试内容 | 队员A需确认的点 |
|------|----------|------------------|
| 1 | 摄像头开启→AI推理→EAR/MAR数值实时显示在UI | UI 小窗能正常接收队员B的画面/数据 |
| 2 | EAR低于阈值→疲劳判定→RPMsg发指令→M4亮红灯+蜂鸣器响 | `rpmsg_client.send()` 到 M4 执行的全链路延时是否可接受 |
| 3 | 番茄钟结束→统计图表弹出→数据写入SQLite | `db_manager.add_focus_record()` 是否正确落库 |
| 4 | 首次开机Few-shot向导→个人配置写入JSON→第二次开机自动加载 | UI 待机态能否正确读取 `config.json` |
| 5 | 全流程压力测试，连续运行3个番茄钟 | 状态机是否会卡死在某一态，RPMsg 是否丢包 |

**界面美化要求：** 深色主题，背景 `#1A1A2E`，强调色 `#00D4FF`，圆弧加渐变色，Todo 列表完成时加动画。

**风险预案（队员A相关）：**

| 风险 | 应对方案 |
|------|----------|
| RPMsg 通信调试困难 | 先用串口模拟通信，联调期再切换为正式 RPMsg（见第十八节） |
| PyQt5 在板上卡顿 | 降低 AI 小窗刷新率到 5fps，主界面保持 30fps |
| 时间来不及 | 优先保证"检测→报警→番茄钟闭环"，统计图表功能可降级或删除 |

---

## 二十二、技术报告撰写清单（队员A负责部分）

| 章节 | 必须包含的内容 |
|------|------------------|
| 硬件选型 | BOM 清单（见第二十节）、各元件选型理由 |
| M4端 FreeRTOS 设计 | 任务架构图（见第六节）、RPMsg 协议表（见第四节） |
| PCB 设计 | 若画板：Gerber 图截图 + 走线说明；若飞线：连接示意图 |
| 系统集成与测试 | 第二十一节联调表 + 实测中遇到的问题与解决方案（可直接引用第十一节"已知坑"） |
| 摘要、结论与展望 | 与队员B共写，队员A负责审阅其AI算法相关描述是否与硬件实现一致 |

---

## 二十三、给 AI 的 Prompt 模板（A7 端模块，续第十三节）

**单模块开发（推荐每次使用，与第十三节 M4 模板同一套规范）：**

```
当前任务：实现 db_manager.py 中的 add_focus_record 和 get_focus_records 方法。

现有文件内容：
[粘贴当前 db_manager.py 全文]

要求：
1. 严格遵守 with sqlite3.connect() 上下文管理器写法，参数用 ? 占位符
2. 输出完整可直接运行的文件，禁止省略号
3. 每个方法添加详细中文注释，说明设计原因
4. 保持与已有方法一致的命名风格（小写加下划线）
```

**模块整合（仅当 db_manager.py / pomodoro_ui.py / rpmsg_client.py 均已独立测试通过后才使用）：**

```
当前我已经独立跑通了 db_manager.py、pomodoro_ui.py、rpmsg_client.py 三个模块（均在下方提供）。
现在请按本 README 第十九节的状态机设计，编写 main.py，将三者串联：
1. 待机/专注/报警/统计四态切换逻辑
2. 状态切换时正确调用 rpmsg_client.send() 对应指令
3. 报警时长结束/手动解除后正确写库并回到待机

要求：禁止使用任何省略号，给出完整可运行的 main.py 源码，并用中文注释说明状态切换的触发条件。

[依次粘贴三个模块的完整源码]
```

---

## 二十四、0基础学习资源（队员A参考）

**M4 / FreeRTOS / WS2812B / RPMsg：**

- FreeRTOS：正点原子 MP157 FreeRTOS 例程 + B站搜索"FreeRTOS CMSIS-V2 入门"
- WS2812B：B站搜索"STM32 TIM PWM DMA WS2812"
- RPMsg：ST Wiki "How to exchange message with Linux using RPMsg"
- HAL 库：STM32CubeMP1 例程 + 正点原子开发手册

**A7 / PyQt5 / SQLite：**

- PyQt5：官方中文文档（doc.qt.io/qtforpython-5），B站搜索"PyQt5 入门教程 番茄钟"
- SQLite：Python 官方文档（docs.python.org/3/library/sqlite3.html），B站搜索"Python sqlite3 增删改查"
- 圆弧进度条画法：B站搜索"PyQt5 QPainter 圆弧进度条"

**学习建议：** 先跑官方例程 → 让AI生成单个模块 → 测试 → Git commit。每个模块独立验证通过后才进入下一个，最终在 `main.py` 中拼装（详见第十九节）。

