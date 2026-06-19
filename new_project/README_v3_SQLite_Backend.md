# STM32MP157 AI疲劳检测专注番茄钟 —— 项目现状与当前任务（v3）

> **本文件为 AI 编程助手（Codex/Claude 等）的上下文注入文档。**
> 每次新建对话时，请将本文件全文粘贴至对话首条消息。
>
> **版本说明：** 本文档整合 `README_M4.md` 与 `README_new.md` 的内容，并修正其中已过时的部分（时钟假设错误、已删除的引脚、已简化的协议），同时新增当前阶段的开发任务——**A7 端 SQLite 数据库与 Python 后端业务逻辑**。M4 端固件已完成并验证通过，**不应再被 AI 重新设计或修改**，仅作为既定事实背景提供。

---

## 一、角色设定与全局铁律

### 1.1 适用范围说明

本项目由两部分组成：

| 部分 | 状态 | AI 是否可修改 |
|------|------|----------------|
| M4 端固件（C / FreeRTOS） | **已完成、已验证、已锁定** | 仅在明确指出具体 bug 时才允许局部修改，禁止整体重构 |
| A7 端 Python 后端（SQLite / 业务逻辑 / PyQt5 UI） | **当前开发阶段** | 是，这是本文档的核心任务 |

### 1.2 M4 端铁律（背景知识，保留供 AI 理解既有代码，不要求重新生成）

1. 只使用 STM32 HAL 库，严禁标准库或直接操作寄存器。
2. 操作系统为 FreeRTOS，统一使用 CMSIS-V2 封装（`osThreadNew`、`osMessageQueuePut` 等）。
3. C 语言（C99），不使用 C++。
4. 严禁重新初始化引脚或时钟，`MX_GPIO_Init()`、`SystemClock_Config()`、`MX_TIM2_Init()` 等均由 CubeMX 生成，直接调用即可。
5. 关键时序参数已硬编码，**禁止**依赖 `HAL_RCC_GetPCLK1Freq()` 动态查询（remoteproc 启动模式下该查询不可靠）。

### 1.3 A7 端 Python 铁律（当前任务必须遵守）

1. **数据库操作统一封装**在 `db_manager.py` 中，UI 层和业务逻辑层不直接写 SQL 语句。
2. 统一使用 `with sqlite3.connect(self.db_name) as conn:` 上下文管理器，确保异常时自动回滚、连接自动关闭。
3. SQL 参数**必须**使用占位符 `?`，禁止用 f-string / `%` 拼接 SQL（防止注入、避免特殊字符导致崩溃）。
4. UI 层（`pomodoro_ui.py`）与数据逻辑严格解耦：UI 只调用 `db_manager.py` / `rpmsg_client.py` 暴露的方法，不直接操作数据库或串口设备。
5. 每个模块必须有独立的 `if __name__ == "__main__":` 自测试区，独立验证通过后才能被其他模块导入。
6. 变量名小写加下划线（如 `fatigue_count`），类名大驼峰（如 `DBManager`）。
7. 每次输出必须是完整文件，禁止省略号占位，关键逻辑需有中文注释说明"为什么这样做"。

---

## 二、项目背景与双核架构

### 2.1 项目名称

AI 疲劳检测专注番茄钟（嵌入式作品）

### 2.2 双核架构

| 核心 | 职责 | 运行环境 |
|------|------|----------|
| Cortex-A7 | 摄像头采集、AI 疲劳检测、Python 后端业务逻辑、SQLite 数据库、PyQt5 UI | Linux (OpenSTLinux) |
| Cortex-M4 | WS2812B 灯带、蜂鸣器等硬件实时控制，接收并执行 A7 指令 | FreeRTOS |

**数据流向：**

```
A7 Python/PyQt5 → /dev/ttyRPMSG0 → RPMsg/OpenAMP → M4 FreeRTOS → HAL 控制硬件
```

A7 负责"判断该做什么"，M4 负责"实时执行硬件动作"——接口边界清晰，A7 不需要、也不应该直接控制 GPIO / PWM / DMA。

### 2.3 团队分工

- **队员 A（本文档主要使用者）**：负责 M4 端 RPMsg 通信、FreeRTOS 多任务、WS2812B、蜂鸣器控制（已完成）；当前负责 A7 端 SQLite 数据库、Python 后端业务逻辑、PyQt5 UI 基础框架（本文档第四节）。
- **队员 B**：负责 AI 疲劳检测算法，提供 `fatigue_detector.py` 模块，向队员 A 的业务逻辑层输出检测结果（接口约定见第 4.7 节）。

> 因此本文档第四节中除 AI 检测算法本身外的所有 A7 端模块（数据库、RPMsg 客户端、UI、状态机整合）均由队员 A（你）负责实现，AI 助手协助的范围也是这一部分。

---

## 三、M4 端现状（已完成，背景事实，不在当前任务范围内）

### 3.1 核间通信

- 框架：OpenAMP / RPMsg，已双向打通。
- A7 端设备节点：`/dev/ttyRPMSG0`（**注意不是** `/dev/rpmsg0`，这是早期的误解，已修正）。
- 指令为 ASCII 字符串，以 `\n` 结尾，接收端去除换行符后再解析。

### 3.2 FreeRTOS 任务架构

已实现三任务模型，均使用 CMSIS-RTOS V2 API：

| 任务 | 职责 |
|------|------|
| RPMsg 接收/解析任务 | 轮询 RPMsg，解析指令字符串，向各队列投递命令并回 ACK |
| WS2812B LED 控制任务 | 等待 LED 命令队列，调用底层驱动设置灯带颜色 |
| 蜂鸣器控制任务（PG7） | 等待蜂鸣命令队列，执行响声序列 |

### 3.3 硬件引脚与时钟（重要更新）

| 外设 | GPIO | 触发方式 | 状态 |
|------|------|----------|------|
| WS2812B RGB 灯带（12 颗） | PB10 | TIM2_CH3 PWM + DMA | 已能正常变色 |
| 蜂鸣器 | PG7 | 低电平触发，`HAL_GPIO_WritePin` | 正常 |
| ~~状态指示灯（PD6）~~ | ~~PD6~~ | — | **已移除**，避免后续硬件冲突 |
| ~~按键（PG3/KEY0）~~ | ~~PG3~~ | — | **已移除**，避免后续硬件冲突 |

> ⚠️ **AI 助手注意：** 旧版文档中关于 PD6 状态灯、PG3 按键的所有描述（包括状态翻转、`EVENT:BTN_PRESS` 上报）均已废弃，今后协助调试时不应再引用这些引脚或事件。

**WS2812B 时序参数（已修正，实测验证通过）：**

旧版文档曾假设 TIM2 时钟为 64 MHz 并据此硬编码 `ARR=79, CCR0=26, CCR1=52`。经进一步排查（"半黄半粉"显示异常，PWM 实际频率跑到约 2.6MHz），发现根源是 `.ioc` 中 `TIM2OutputFreq_Value=209000000`，**实际 TIM2 时钟为 209 MHz**，而非 64 MHz，原硬编码值因此失效。

**最终确认的硬编码参数：**

```c
#define WS2812B_PSC          0U     /* 不分频 */
#define WS2812B_ARR          260U   /* 周期计数值 */
#define WS2812B_BIT_0_CCR    84U    /* 0码高电平宽度 */
#define WS2812B_BIT_1_CCR    178U   /* 1码高电平宽度 */
#define WS2812B_NUM_LEDS     12U
#define WS2812B_RESET_BITS   50U    /* 复位码：50 个 CCR=0，提供前后低电平 reset */
```

**对应的时序换算：**

```
PWM 频率 = 209MHz / (260 + 1) ≈ 800.77kHz（周期 ≈ 1.249µs，符合 WS2812B 800kHz 规格）
0码高电平 = 84 / 209MHz ≈ 0.402µs
1码高电平 = 178 / 209MHz ≈ 0.852µs
reset 低电平 ≈ 50 × 1.249µs ≈ 62.45µs
```

> 已实测验证：A7 发送 `LED:R` / `LED:G` / `LED:B` / `LED:OFF` 后，12 颗 WS2812B 均可正常变色与熄灭。**这是当前固件中实际使用的最终值，今后调试 WS2812B 相关问题时以此为准，不得再退回 64MHz 假设下的旧数值。**

### 3.4 通信协议（最终版，已简化）

**A7 → M4：**

| 指令 | M4 执行动作 | M4 回复 ACK |
|------|-------------|-------------|
| `LED:R\n` | 灯带全部红色（疲劳警告） | `ACK:LED:R\n` |
| `LED:G\n` | 灯带全部绿色（专注模式） | `ACK:LED:G\n` |
| `LED:B\n` | 灯带全部蓝色（休息模式） | `ACK:LED:B\n` |
| `LED:OFF\n` | 灯带全部熄灭 | `ACK:LED:OFF\n` |
| `BEEP:1\n` | 蜂鸣器响 1 次 | `ACK:BEEP:OK\n` |
| `BEEP:3\n` | 蜂鸣器响 3 次 | `ACK:BEEP:OK\n` |
| `PING\n` | 通信测试 | `ACK:PING\n` |
| *(未知指令)* | 无动作 | `ERR:UNKNOWN_CMD\n` |

**M4 → A7（主动上报）：**

| 触发条件 | M4 发送 | 含义 |
|----------|---------|------|
| 硬件初始化失败 | `EVENT:ERR_HAL\n` | 严重错误，通知 UI 报错 |

> 旧版文档中的 `STATUS:OK` 指令与 `EVENT:BTN_PRESS` 上报机制（依赖已移除的 PG3 按键）已不再使用。

### 3.5 已知坑（M4 端，供参考，不需要重新解决）

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| WS2812B 颜色固定为"半黄半粉" | TIM2 实际时钟为 209MHz（`.ioc` 中 `TIM2OutputFreq_Value=209000000`），代码误按 64MHz 计算时序，导致 PWM 实际跑到约 2.6MHz | 改用 `ARR=260, CCR0=84, CCR1=178`（按 209MHz 重新计算），见 3.3 节 |
| DMA 传输后 LED 不更新 | DMA 缓冲区为局部变量被栈覆盖 | 声明为 `static` 或全局变量 |
| `uint16_t` DMA 缓冲区颜色错误 | TIM2 CCR 为 32 位寄存器 | DMA 缓冲区必须是 `uint32_t[]` |
| FreeRTOS 任务互相阻塞 | 队列满时投递方无超时 | `xQueueSend` 使用有限超时而非 `portMAX_DELAY` |
| RPMsg 消息丢失 | 轮询间隔太长 | 轮询任务延时不超过 10ms |

---

## 四、当前任务：SQLite 数据库 + Python 后端业务逻辑

> 这是本文档的核心，也是接下来需要 AI 助手协助实现的部分。

### 4.1 三层架构

| 层 | 职责 | 对应文件 |
|----|------|----------|
| 硬件通信层 | 负责 `/dev/ttyRPMSG0` 的读写、ACK 等待与超时处理 | `rpmsg_client.py` |
| 业务逻辑层 | 根据疲劳检测结果、番茄钟状态、用户操作生成控制命令并落库 | 整合于 `main.py` |
| UI 展示层 | PyQt5 显示状态、统计数据、控制按钮 | `pomodoro_ui.py` |

### 4.2 数据库表设计

数据库文件：`tasks.db`（首次运行自动创建）。字段设计如下：

| 表名 | 字段 | 说明 |
|------|------|------|
| `focus_sessions` | `id, task_name, start_time, end_time, duration, status` | 每次专注的任务名称（便于 UI 展示与统计）、开始/结束时间、持续时长（秒）、结束状态（完成/中断） |
| `fatigue_events` | `id, session_id, timestamp, fatigue_level, ear, mar, eye_closed_duration, yawn_count, alarm_triggered` | `session_id` 关联 `focus_sessions`；除疲劳等级外，保留 EAR（眼睛纵横比）、MAR（嘴部纵横比）、闭眼时长、打哈欠次数等半处理数据，便于后续调阈值、分析误报 |
| `alarm_events` | `id, timestamp, command_sent, ack_received, success, fail_reason` | 发送给 M4 的 `LED`/`BEEP` 命令、收到的 ACK、是否成功；`fail_reason` 记录超时/串口异常等失败原因，便于排查通信问题 |
| `settings` | `key, value`（或固定单行表） | 疲劳阈值、番茄钟时长、休息时长、蜂鸣器开关等配置 |
| `daily_summary` | `date, total_focus_duration, fatigue_count, alarm_count` | 按日缓存的专注总时长、疲劳次数、报警次数（可选，用于统计页加速查询） |

> 设计原则：数据库不只是"记录结果"，更要支持后续调试与统计——粗粒度的"疲劳/不疲劳"标记不足以支撑阈值调整或误报分析，因此 `fatigue_events` 保留了原始/半处理的检测数值；`alarm_events` 保留失败原因，便于排查 RPMsg 通信问题。

### 4.3 `db_manager.py` 接口规范

```python
class DBManager:
    def __init__(self, db_name="tasks.db"): ...

    # focus_sessions
    def start_focus_session(self, task_name="") -> int: ...   # 返回新建 session 的 id
    def end_focus_session(self, session_id, status): ...

    # fatigue_events
    def add_fatigue_event(self, session_id, fatigue_level, ear, mar,
                           eye_closed_duration, yawn_count, alarm_triggered): ...
    def get_fatigue_events(self, session_id): ...

    # alarm_events
    def add_alarm_event(self, command_sent, ack_received, success, fail_reason=""): ...

    # settings
    def get_setting(self, key, default=None): ...
    def set_setting(self, key, value): ...

    # daily_summary / 统计
    def update_daily_summary(self, date): ...           # 汇总当日数据
    def get_daily_summary(self, days=30): ...            # 供统计图表读取
```

实现时遵循第 1.3 节铁律：上下文管理器、`?` 占位符、详细中文注释。

### 4.4 RPMsg 客户端接口规范（`rpmsg_client.py`）

```python
class RPMsgClient:
    def __init__(self, dev_path="/dev/ttyRPMSG0"): ...

    def send(self, cmd: str) -> bool:
        """发送指令（自动追加 \\n），等待 M4 的 ACK，超时返回 False"""

    def send_led_red(self): self.send("LED:R")
    def send_led_green(self): self.send("LED:G")
    def send_led_blue(self): self.send("LED:B")
    def send_led_off(self): self.send("LED:OFF")
    def send_beep_once(self): self.send("BEEP:1")
    def send_beep_three_times(self): self.send("BEEP:3")

    def read_event(self, timeout=0.1):
        """非阻塞读取 M4 主动上报的 EVENT:* 字符串"""
```

- 超时建议 200ms～500ms。
- 每次 `send()` 调用后，无论成功失败都应记录到 `alarm_events` 表（命令、ACK 内容、是否成功、失败原因），便于排查通信问题。

### 4.5 疲劳检测结果 → 命令映射规则

| 状态 | LED 命令 | 蜂鸣器 |
|------|----------|--------|
| 正常专注 | `LED:G` | 不响 |
| 休息提醒 | `LED:B` | 不响 |
| 疲劳报警 | `LED:R` | `BEEP:1` 或 `BEEP:3` |
| 结束/空闲 | `LED:OFF` | 不响 |

### 4.6 PyQt5 UI 模块建议

**主界面：**
- 当前状态（专注中 / 休息中 / 疲劳警告 / 空闲）
- 番茄钟倒计时（`QPainter` 圆弧动画）
- 今日专注时长、今日疲劳次数
- M4 通信状态指示

**控制按钮：** 开始专注 / 暂停继续 / 结束专注 / 手动测试红绿蓝灯及熄灭 / 手动测试蜂鸣器

**数据页面：** 历史专注记录、疲劳事件记录、报警事件记录（均从 `db_manager.py` 读取）

**设置页面：** 专注时长、休息时长、疲劳报警阈值、是否启用蜂鸣器（读写 `settings` 表）

> UI 层不直接操作数据库或串口，所有数据读写通过 `db_manager.py`，所有硬件指令通过 `rpmsg_client.py`。

### 4.7 与队员 B 的接口约定（`fatigue_detector.py`）

队员 B 提供的疲劳检测模块只需关注算法本身，向业务逻辑层输出统一格式的检测结果，由队员 A 的 `main.py` 负责状态机判断与硬件/数据库联动。

```python
result = fatigue_detector.detect(frame)

# 约定返回格式
{
    "is_fatigue": True,
    "fatigue_level": 2,
    "ear": 0.21,
    "mar": 0.65,
    "eye_closed_duration": 1.8,
    "yawn_count": 1,
    "timestamp": "2026-06-19 12:00:00"
}
```

- `main.py` 拿到 `result` 后，直接将 `ear`、`mar`、`eye_closed_duration`、`yawn_count` 等字段透传给 `db_manager.add_fatigue_event()` 落库（见 4.2、4.3 节），字段名保持一致，避免两边模块各自重新定义结构。
- `is_fatigue` / `fatigue_level` 用于驱动第 4.5 节的命令映射规则（决定发送 `LED:R` + `BEEP` 还是 `LED:G`）。
- 若队员 B 后续调整返回字段，应同步更新本节约定，而不是各自在代码里临时兼容。

---

## 五、A7 端目录结构建议

```
A7_Python_App/
├── config.json          # 个人化配置（如有，与 settings 表二选一或互为缓存）
├── tasks.db              # SQLite 数据库（首次运行自动生成）
├── db_manager.py          # 数据库操作封装（当前任务核心）
├── rpmsg_client.py        # A7→M4 RPMsg 通信封装（当前任务核心）
├── pomodoro_ui.py         # PyQt5 界面
└── main.py                # 状态机整合主程序
```

**整合顺序（建议严格按此顺序，每步验证通过才进入下一步）：**

1. `db_manager.py` 独立跑通（单测：建表、增删改查）
2. `rpmsg_client.py` 独立跑通（手动发送第 3.4 节协议指令，确认 M4 收到并回 ACK）
3. `pomodoro_ui.py` 独立跑通（先不接数据库、不接 RPMsg，纯界面动画）
4. 三者在 `main.py` 中串联：状态切换 → 调用 `rpmsg_client.send()` → 调用 `db_manager` 写库
5. 接入队员 B 提供的 `fatigue_detector.py`（接口格式见 4.7 节），驱动状态机切换到"报警"

---

## 六、给 AI 助手的资料清单与 Prompt 模板

### 6.1 每次提问时建议提供

- 本 README 全文（作为上下文）
- 当前报错信息（完整 Traceback）
- 正在修改的文件完整内容
- 若调试通信问题：实测的 RPMsg 收发延时、M4 端 `/sys/kernel/debug/remoteproc/remoteproc0/trace0` 日志

### 6.2 单模块开发 Prompt 模板

```
当前任务：实现 db_manager.py 中的 xxx 方法。

现有文件内容：
[粘贴当前文件全文]

要求：
1. 严格遵守本 README 第 1.3 节 Python 铁律（with sqlite3.connect() 上下文管理器，? 占位符）
2. 输出完整可直接运行的文件，禁止省略号
3. 每个方法添加详细中文注释，说明设计原因
4. 保持与已有方法一致的命名风格
```

### 6.3 模块整合 Prompt 模板

```
我已经独立跑通了 db_manager.py、rpmsg_client.py、pomodoro_ui.py（均在下方提供）。
请按本 README 第 4.5 节的状态机与命令映射规则，编写 main.py 将三者串联。

要求：禁止省略号，给出完整可运行源码，用中文注释说明状态切换的触发条件。

[依次粘贴三个模块的完整源码]
```

---

## 七、参考资料

- Python `sqlite3` 官方文档：https://docs.python.org/3/library/sqlite3.html
- PyQt5 官方文档（中文）：https://doc.qt.io/qtforpython-5/
- B 站搜索关键词：`Python sqlite3 增删改查`、`PyQt5 入门教程 番茄钟`、`PyQt5 QPainter 圆弧进度条`

---

## 八、本版本已确认事项

以下三项原为待确认的开放问题，现已根据你的反馈写入正文，今后无需再向 AI 助手重复说明：

1. WS2812B 时序参数（`ARR=260, CCR0=84, CCR1=178`，基于实测 209MHz）——见第 3.3 节。
2. 数据库字段（`task_name`、EAR/MAR 等检测数值、`fail_reason`）——见第 4.2、4.3 节。
3. 团队分工与 `fatigue_detector.py` 接口约定——见第 2.3、4.7 节。
