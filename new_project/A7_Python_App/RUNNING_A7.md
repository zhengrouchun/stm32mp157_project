# A7 Python 应用运行说明

本文档说明如何从零配置环境并运行 A7 端 Python 程序。

## 1. 文件位置

当前 A7 应用目录：

```text
new_project/A7_Python_App/
├── db_manager.py
├── rpmsg_client.py
├── pomodoro_ui.py
├── main.py
└── requirements.txt
```

## 2. 在 Windows 电脑上先测试数据库和 UI

Windows 上没有 `/dev/ttyRPMSG0`，所以只能测试数据库和 PyQt5 界面，不能真实控制 M4。

### 2.1 进入目录

```powershell
cd D:\Stu15\IPC-Communication\RPMsg_UART_ADC\new_project\A7_Python_App
```

### 2.2 建议创建虚拟环境

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
```

如果 PowerShell 禁止激活脚本，可以临时执行：

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\.venv\Scripts\Activate.ps1
```

### 2.3 安装依赖

```powershell
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

### 2.4 测试数据库模块

```powershell
python db_manager.py
```

看到 `session_id`、`fatigue_events`、`daily_summary` 输出，说明 SQLite 部分正常。

### 2.5 单独测试 UI

```powershell
python pomodoro_ui.py
```

这一步只打开界面，不连接数据库和 M4。

### 2.6 运行主程序

```powershell
python main.py
```

在 Windows 上，M4 状态会显示未连接，这是正常的；等部署到 A7 Linux 后才会连接 `/dev/ttyRPMSG0`。

## 3. 在 STM32MP157 A7 Linux 上运行

### 3.1 确认 Python 和 PyQt5

```sh
python3 --version
python3 -c "import sqlite3; print('sqlite3 ok')"
python3 -c "import PyQt5; print('PyQt5 ok')"
```

`sqlite3` 是 Python 标准库，通常不需要额外安装。若 PyQt5 不存在，需要根据你的 OpenSTLinux 镜像安装对应包。

### 3.2 确认 M4 remoteproc 已启动

```sh
cat /sys/class/remoteproc/remoteproc0/state
```

期望输出：

```text
running
```

如果不是 running，先按你的板端流程加载并启动 M4 固件。

### 3.3 确认 RPMsg 设备节点

```sh
ls -l /dev/ttyRPMSG0
```

注意当前最终协议使用 `/dev/ttyRPMSG0`，不是 `/dev/rpmsg0`。

### 3.4 运行主程序

```sh
cd /path/to/A7_Python_App
python3 main.py
```

启动后程序会先发送 `PING`，如果 M4 回复 `ACK:PING`，界面会显示 M4 已连接。

## 4. 常见问题

### PyQt5 没装

现象：

```text
ModuleNotFoundError: No module named 'PyQt5'
```

Windows 上执行：

```powershell
python -m pip install -r requirements.txt
```

A7 Linux 上需要使用板端可用的软件源或提前交叉打包 PyQt5。

### Windows 上 M4 未连接

这是正常现象。Windows 没有 `/dev/ttyRPMSG0`，只能做 UI 和数据库验证。

### A7 Linux 上找不到 /dev/ttyRPMSG0

优先检查：

```sh
cat /sys/class/remoteproc/remoteproc0/state
dmesg | grep -i rpmsg
```

通常是 M4 固件未启动、OpenAMP 未建立，或设备节点名和固件/设备树不匹配。
