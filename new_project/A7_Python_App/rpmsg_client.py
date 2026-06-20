#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RPMsg 客户端模块。

负责通过 /dev/ttyRPMSG0 向 M4 发送最终版 ASCII 协议命令，并等待 ACK。
如传入 DBManager，每次 send() 都会自动记录 alarm_events，方便后续排查通信问题。
"""

import os
import select
import time

try:
    import termios
    import tty
except ImportError:
    termios = None
    tty = None


class RPMsgClient:
    """A7 到 M4 的 RPMsg 通信封装。"""

    EXPECTED_ACK = {
        "LED:R": "ACK:LED:R",
        "LED:G": "ACK:LED:G",
        "LED:B": "ACK:LED:B",
        "LED:OFF": "ACK:LED:OFF",
        "BEEP:1": "ACK:BEEP:OK",
        "BEEP:3": "ACK:BEEP:OK",
        "PING": "ACK:PING",
    }

    def __init__(self, dev_path="/dev/ttyRPMSG0", timeout=0.5, db_manager=None):
        self.dev_path = dev_path
        self.timeout = timeout
        self.db_manager = db_manager
        self.fd = None
        self.last_ack = ""
        self.last_fail_reason = ""

    def open(self):
        """打开 RPMsg 设备并设置 RAW 模式。"""
        if termios is None or tty is None:
            raise OSError("当前系统不支持 termios/tty，RPMsg 实机通信请在 A7 Linux 上运行")

        if not os.path.exists(self.dev_path):
            raise FileNotFoundError(f"RPMsg 设备不存在: {self.dev_path}")

        self.fd = os.open(self.dev_path, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
        # RAW 模式可以关闭终端行缓冲和特殊字符处理，保证 M4 收到的是干净协议帧。
        tty.setraw(self.fd, termios.TCSANOW)
        time.sleep(0.05)
        self._flush_read_buffer()
        return self

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self.open()

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def send(self, cmd: str) -> bool:
        """发送指令并等待 ACK，成功返回 True，超时或 ACK 不匹配返回 False。"""
        clean_cmd = cmd.strip()
        self.last_ack = ""
        self.last_fail_reason = ""

        try:
            self._ensure_open()
            os.write(self.fd, f"{clean_cmd}\n".encode("ascii"))
            ack = self._read_line(self.timeout)
            self.last_ack = ack

            expected_ack = self.EXPECTED_ACK.get(clean_cmd)
            if not ack:
                return self._record_and_return(clean_cmd, ack, False, "等待 ACK 超时")
            if ack.startswith("ERR:"):
                return self._record_and_return(clean_cmd, ack, False, "M4 返回错误")
            if expected_ack and ack != expected_ack:
                return self._record_and_return(clean_cmd, ack, False, "ACK 与命令不匹配")

            return self._record_and_return(clean_cmd, ack, True, "")
        except OSError as exc:
            self.last_fail_reason = f"串口异常: {exc}"
            self._record_alarm(clean_cmd, "", False, self.last_fail_reason)
            return False

    def send_led_red(self):
        return self.send("LED:R")

    def send_led_green(self):
        return self.send("LED:G")

    def send_led_blue(self):
        return self.send("LED:B")

    def send_led_off(self):
        return self.send("LED:OFF")

    def send_beep_once(self):
        return self.send("BEEP:1")

    def send_beep_three_times(self):
        return self.send("BEEP:3")

    def ping(self):
        return self.send("PING")

    def read_event(self, timeout=0.1):
        """读取 M4 主动上报的 EVENT:* 字符串；没有事件时返回空字符串。"""
        if self.fd is None:
            return ""

        line = self._read_line(timeout)
        if line.startswith("EVENT:"):
            return line
        return ""

    def _ensure_open(self):
        if self.fd is None:
            self.open()

    def _flush_read_buffer(self):
        try:
            while True:
                os.read(self.fd, 256)
        except BlockingIOError:
            return

    def _read_line(self, timeout):
        """在超时时间内读取一行回复。"""
        if self.fd is None:
            return ""

        end_time = time.monotonic() + timeout
        buffer = bytearray()

        while time.monotonic() < end_time:
            remain = max(0.0, end_time - time.monotonic())
            ready, _, _ = select.select([self.fd], [], [], remain)
            if not ready:
                break

            try:
                chunk = os.read(self.fd, 256)
            except BlockingIOError:
                continue

            if not chunk:
                continue

            buffer.extend(chunk)
            if b"\n" in chunk or b"\r" in chunk:
                break

        return buffer.decode("ascii", errors="ignore").strip()

    def _record_and_return(self, cmd, ack, success, fail_reason):
        self.last_fail_reason = fail_reason
        self._record_alarm(cmd, ack, success, fail_reason)
        return success

    def _record_alarm(self, cmd, ack, success, fail_reason):
        if self.db_manager is not None:
            self.db_manager.add_alarm_event(cmd, ack, success, fail_reason)


if __name__ == "__main__":
    client = RPMsgClient()
    try:
        with client:
            print("PING:", client.ping(), client.last_ack or client.last_fail_reason)
            print("LED:G:", client.send_led_green(), client.last_ack or client.last_fail_reason)
            time.sleep(0.5)
            print("LED:OFF:", client.send_led_off(), client.last_ack or client.last_fail_reason)
    except FileNotFoundError as exc:
        print(exc)
