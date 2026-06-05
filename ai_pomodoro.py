#!/usr/bin/env python3
# ============================================================
# a7_controller.py  —  A7端控制脚本
# 功能：通过 /dev/ttyRPMSG0 向M4发送命令，实现双向通信
#
# 使用方法：
#   python3 a7_controller.py          # 交互模式
#   python3 a7_controller.py --demo   # 自动演示模式
#   python3 a7_controller.py --test   # 连通性测试（发hello）
# ============================================================

import os
import sys
import time
import select
import tty
import termios
import argparse

RPMSG_NODE = "/dev/ttyRPMSG0"
TIMEOUT_SEC = 2.0

class RPMsgController:
    def __init__(self, node=RPMSG_NODE):
        self.node = node
        self.fd = None

    def open(self):
        """打开RPMsg设备，设置为RAW模式"""
        if not os.path.exists(self.node):
            raise FileNotFoundError(
                f"设备节点 {self.node} 不存在！\n"
                f"请确认：\n"
                f"  1. M4固件已通过 remoteproc 加载\n"
                f"  2. 运行: cat /sys/class/remoteproc/remoteproc0/state\n"
                f"  3. 应显示 'running'，否则运行: "
                f"echo start > /sys/class/remoteproc/remoteproc0/state"
            )
        self.fd = os.open(self.node, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
        # 设置RAW模式：关闭所有Linux终端处理（行缓冲、特殊字符等）
        tty.setraw(self.fd)
        # 清空启动时可能积压的数据
        time.sleep(0.1)
        self._flush_read()
        print(f"[A7] 已连接 M4 节点: {self.node}")
        return self

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self.open()

    def __exit__(self, *args):
        self.close()

    def _flush_read(self):
        """清空读缓冲区"""
        try:
            while True:
                os.read(self.fd, 256)
        except BlockingIOError:
            pass

    def send(self, cmd: str):
        """发送命令字符串（自动添加换行与结束符）"""
        # 在原命令字符串末尾加上 \r\n (回车换行)
        # 大部分 M4 端的串口或 RPMSG 接收逻辑，都是靠检测到 \r 或 \n 来判断一帧数据接收完毕的
        cmd_with_terminator = cmd + '\r\n' 
        
        # 将拼接好的字符串编码为 utf-8 字节流
        data = cmd_with_terminator.encode('utf-8')
        
        # 如果 M4 端的 C 代码极其依赖纯正的 C 字符串结束符 '\0' 进行 strcmp 判断，
        # 也可以在 encode 后追加一个 0x00 字节，例如：
        # data = cmd_with_terminator.encode('utf-8') + b'\x00'
        
        os.write(self.fd, data)

    def recv(self, timeout=TIMEOUT_SEC) -> str:
        """等待M4回复，超时返回空字符串"""
        ready = select.select([self.fd], [], [], timeout)
        if ready[0]:
            try:
                raw = os.read(self.fd, 256)
                return raw.decode('utf-8', errors='ignore').strip()
            except BlockingIOError:
                return ""
        return ""

    def send_recv(self, cmd: str, timeout=TIMEOUT_SEC) -> str:
        """发送命令并等待回复"""
        print(f"[A7→M4] {cmd!r}")
        self.send(cmd)
        reply = self.recv(timeout)
        if reply:
            print(f"[M4→A7] {reply!r}")
        else:
            print(f"[M4→A7] <超时，未收到回复>")
        return reply

# ============================================================
# 各功能封装
# ============================================================

def test_connection(ctrl: RPMsgController):
    """连通性测试：发送hello，期望收到world"""
    print("\n=== 连通性测试 ===")
    reply = ctrl.send_recv("hello")
    if "world" in reply:
        print("✓ 通信正常！M4已就绪。")
        return True
    else:
        print("✗ 通信异常，请检查M4固件是否已加载。")
        return False

def set_led_color(ctrl: RPMsgController, color: str):
    """
    设置WS2812B颜色
    color: 'R'（红）/ 'G'（绿）/ 'B'（蓝）/ 'OFF'（关闭）
    """
    color = color.upper()
    if color in ('R', 'G', 'B', 'OFF'):
        ctrl.send_recv(f"LED:{color}")
    else:
        print(f"[错误] 不支持的颜色: {color}，可用: R / G / B / OFF")

# 蜂鸣器功能
def beep(ctrl: RPMsgController, seconds: int):
    """控制蜂鸣器响N秒（1~9）"""
    if 1 <= seconds <= 9:
       ctrl.send_recv(f"BEEP:{seconds}")
    else:
       print(f"[错误] 秒数必须在1~9之间，当前: {seconds}")

def demo_sequence(ctrl: RPMsgController):
    """自动演示：按顺序测试所有功能"""
    print("\n=== 自动演示模式 ===")
    # 1. 连通性测试
    if not test_connection(ctrl):
        return

    time.sleep(0.5)

    # 2. WS2812B颜色循环
    print("\n--- WS2812B 颜色演示 ---")
    for color, name in [('R', '红色'), ('G', '绿色'), ('B', '蓝色')]:
        print(f"→ 设置{name}...")
        set_led_color(ctrl, color)
        time.sleep(2)

    print("→ 关闭LED...")
    set_led_color(ctrl, 'OFF')
    time.sleep(0.5)

    # 3. 蜂鸣器测试 (已注释)
    # print("\n--- 蜂鸣器演示 ---")
    # print("→ 蜂鸣器响2秒...")
    # beep(ctrl, 2)
    # time.sleep(3)  # 等待蜂鸣器完成

    print("\n演示完成！")

def interactive_mode(ctrl: RPMsgController):
    """交互模式：手动输入命令"""
    print("\n=== 交互模式 ===")
    print("可用命令：")
    print("  LED:R     → WS2812B全红")
    print("  LED:G     → WS2812B全绿")
    print("  LED:B     → WS2812B全蓝")
    print("  LED:OFF   → 关闭LED")
    # print("  BEEP:N    → 蜂鸣器响N秒 (N=1~9)") # 已注释
    print("  hello     → 连通性测试")
    print("  quit/exit → 退出")
    print()

    while True:
        try:
            cmd = input("输入命令> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n退出。")
            break

        if not cmd:
            continue
        if cmd.lower() in ('quit', 'exit', 'q'):
            print("退出。")
            break

        ctrl.send_recv(cmd)

# ============================================================
# AI疲劳检测集成（原始功能保留）
# ============================================================

def run_ai_fatigue_detection():
    """模拟AI疲劳检测（替换为真实模型）"""
    time.sleep(1)
    current_time = int(time.time())
    if current_time % 5 == 0:
        return "fatigue"
    return "normal"

def fatigue_monitor_mode(ctrl: RPMsgController):
    """AI疲劳监测模式：检测疲劳时控制M4硬件报警"""
    print("\n=== AI 番茄钟疲劳监测模式 ===")
    ctrl.send_recv("CMD:START_POMO")

    while True:
        status = run_ai_fatigue_detection()
        if status == "fatigue":
            print("\n[!] 检测到疲劳！触发M4报警...")
            # 红灯 + 蜂鸣器（蜂鸣器已注释）
            set_led_color(ctrl, 'R')
            # beep(ctrl, 2) 
            time.sleep(3)
            set_led_color(ctrl, 'OFF')
        else:
            print("[A7] 用户状态良好，专注中...")

# ============================================================
# 主入口
# ============================================================

def main():
    parser = argparse.ArgumentParser(description='STM32MP1 M4 RPMsg控制器')
    parser.add_argument('--demo',    action='store_true', help='自动演示模式')
    parser.add_argument('--test',    action='store_true', help='连通性测试')
    parser.add_argument('--fatigue', action='store_true', help='AI疲劳监测模式')
    parser.add_argument('--device',  default=RPMSG_NODE,  help=f'设备节点（默认：{RPMSG_NODE}）')
    args = parser.parse_args()

    try:
        with RPMsgController(args.device) as ctrl:
            if args.test:
                test_connection(ctrl)
            elif args.demo:
                demo_sequence(ctrl)
            elif args.fatigue:
                fatigue_monitor_mode(ctrl)
            else:
                interactive_mode(ctrl)

    except FileNotFoundError as e:
        print(f"[错误] {e}")
        sys.exit(1)
    except Exception as e:
        print(f"[错误] 程序异常: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()