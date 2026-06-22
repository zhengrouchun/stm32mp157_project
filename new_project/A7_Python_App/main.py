#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
A7 端业务状态机主程序。

本文件把 db_manager.py、rpmsg_client.py、pomodoro_ui.py 串联起来：
用户操作和 fatigue_detector.py 的检测结果驱动状态切换，状态切换再落库并发送
LED/BEEP 命令给 M4。
"""

import os
import queue
import sys
import threading

from db_manager import DBManager
from rpmsg_client import RPMsgClient
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QCursor
from PyQt5.QtCore import QObject, pyqtSignal
from pomodoro_ui import QApplication, QTimer, PomodoroWindow


class HardwareWorker(QObject):
    """Serializes RPMsg commands off the UI thread."""

    status_changed = pyqtSignal(str)
    data_changed = pyqtSignal()

    def __init__(self, rpmsg_client):
        super().__init__()
        self.rpmsg_client = rpmsg_client
        self.commands = queue.Queue()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def submit(self, action):
        self.commands.put(action)

    def _run(self):
        while True:
            action = self.commands.get()
            try:
                result = action()
                if isinstance(result, str):
                    self.status_changed.emit(result)
            finally:
                self.data_changed.emit()
                self.commands.task_done()


class PomodoroController:
    """业务逻辑控制器。"""

    STATE_IDLE = "idle"
    STATE_FOCUS = "focus"
    STATE_PAUSED = "paused"
    STATE_BREAK = "break"
    STATE_FATIGUE = "fatigue"

    def __init__(self, window, db_manager, rpmsg_client):
        self.window = window
        self.db_manager = db_manager
        self.rpmsg_client = rpmsg_client
        self.hardware_worker = HardwareWorker(rpmsg_client)
        self.hardware_worker.status_changed.connect(self.window.set_m4_status)
        self.hardware_worker.data_changed.connect(self._mark_data_dirty)
        self.data_dirty = False

        self.state = self.STATE_IDLE
        self.session_id = None
        self.total_seconds = self._get_int_setting("focus_minutes", 25) * 60
        self.remaining_seconds = self.total_seconds

        self.timer = QTimer()
        self.timer.timeout.connect(self._tick)
        self.timer.start(1000)

        self.refresh_timer = QTimer()
        self.refresh_timer.timeout.connect(self._refresh_ui_data_if_dirty)
        self.refresh_timer.start(3000)

        self._connect_signals()
        self._load_settings_to_ui()
        self._refresh_ui_data()
        self._try_ping_m4()

    def _connect_signals(self):
        self.window.start_requested.connect(self.start_focus)
        self.window.pause_requested.connect(self.pause_focus)
        self.window.resume_requested.connect(self.resume_focus)
        self.window.stop_requested.connect(lambda: self.stop_focus("interrupted"))
        self.window.exit_requested.connect(self.exit_app)
        self.window.led_test_requested.connect(self.test_led)
        self.window.beep_test_requested.connect(self.test_beep)
        self.window.settings_save_requested.connect(self.save_settings)

    def start_focus(self, task_name):
        if self.state in (self.STATE_FOCUS, self.STATE_PAUSED, self.STATE_FATIGUE):
            return

        self.total_seconds = self._get_int_setting("focus_minutes", 25) * 60
        self.remaining_seconds = self.total_seconds
        self.session_id = self.db_manager.start_focus_session(task_name)
        self.state = self.STATE_FOCUS

        # 进入专注时点亮绿色，和 README 第 4.5 节“正常专注”映射保持一致。
        self._send_hardware(self.rpmsg_client.send_led_green)
        self.window.set_status("专注中")
        self.window.set_paused(False)
        self._mark_data_dirty()

    def pause_focus(self):
        if self.state != self.STATE_FOCUS:
            return
        self.state = self.STATE_PAUSED
        self.window.set_status("已暂停")
        self.window.set_paused(True)

    def resume_focus(self):
        if self.state != self.STATE_PAUSED:
            return
        self.state = self.STATE_FOCUS
        self._send_hardware(self.rpmsg_client.send_led_green)
        self.window.set_status("专注中")
        self.window.set_paused(False)

    def stop_focus(self, status):
        if self.session_id is None:
            self.state = self.STATE_IDLE
            self._send_hardware(self.rpmsg_client.send_led_off)
            self.window.set_status("空闲")
            return

        self.db_manager.end_focus_session(self.session_id, status)
        self.session_id = None
        self.state = self.STATE_IDLE
        self.remaining_seconds = self.total_seconds

        # 结束或空闲时熄灭灯带，避免 M4 保持上一种颜色造成状态误解。
        self._send_hardware(self.rpmsg_client.send_led_off)
        self.window.set_status("空闲")
        self.window.set_paused(False)
        self._mark_data_dirty()

    def handle_fatigue_result(self, result):
        """接入队员 B 的 fatigue_detector.detect(frame) 返回结果。

        result 字段按 README 第 4.7 节约定透传入库；这里不重新定义算法结构，
        只负责业务状态机和硬件动作。
        """
        if self.session_id is None:
            return

        is_fatigue = bool(result.get("is_fatigue", False))
        fatigue_level = int(result.get("fatigue_level", 0))
        ear = float(result.get("ear", 0.0))
        mar = float(result.get("mar", 0.0))
        eye_closed_duration = float(result.get("eye_closed_duration", 0.0))
        yawn_count = int(result.get("yawn_count", 0))
        alarm_level = self._get_int_setting("fatigue_alarm_level", 2)
        beep_enabled = self.db_manager.get_setting("beep_enabled", "1") == "1"

        alarm_triggered = is_fatigue and fatigue_level >= alarm_level
        self.db_manager.add_fatigue_event(
            self.session_id,
            fatigue_level,
            ear,
            mar,
            eye_closed_duration,
            yawn_count,
            alarm_triggered,
        )

        if alarm_triggered:
            self.state = self.STATE_FATIGUE
            self.window.set_status("疲劳警告")
            self._send_hardware(self.rpmsg_client.send_led_red)
            if beep_enabled:
                self._send_hardware(self.rpmsg_client.send_beep_once)
        elif self.state != self.STATE_PAUSED:
            self.state = self.STATE_FOCUS
            self.window.set_status("专注中")
            self._send_hardware(self.rpmsg_client.send_led_green)

        self._mark_data_dirty()

    def test_led(self, color):
        if color == "R":
            self._send_hardware(self.rpmsg_client.send_led_red)
        elif color == "G":
            self._send_hardware(self.rpmsg_client.send_led_green)
        elif color == "B":
            self._send_hardware(self.rpmsg_client.send_led_blue)
        elif color == "OFF":
            self._send_hardware(self.rpmsg_client.send_led_off)
        self._mark_data_dirty()

    def test_beep(self):
        # 蜂鸣器只在疲劳报警时触发，避免测试按钮导致有源蜂鸣器长鸣。
        self._mark_data_dirty()

    def exit_app(self):
        self.rpmsg_client.send_led_off()
        self.rpmsg_client.close()
        QApplication.instance().exit(0)

    def save_settings(self, settings):
        for key, value in settings.items():
            self.db_manager.set_setting(key, value)
        self._load_settings_to_ui()

    def _tick(self):
        if self.state in (self.STATE_FOCUS, self.STATE_FATIGUE):
            self.remaining_seconds = max(0, self.remaining_seconds - 1)
            if self.remaining_seconds == 0:
                self.stop_focus("completed")

        self.window.set_timer(self.remaining_seconds, self.total_seconds)

    def _try_ping_m4(self):
        self._send_hardware(self._ping_m4)
        self._mark_data_dirty()

    def _ping_m4(self):
        if self.rpmsg_client.ping():
            return "已连接"
        return self.rpmsg_client.last_fail_reason or "未连接"

    def _send_hardware(self, action):
        self.hardware_worker.submit(action)

    def _mark_data_dirty(self):
        self.data_dirty = True

    def _refresh_ui_data_if_dirty(self):
        if self.data_dirty:
            self.data_dirty = False
            self._refresh_ui_data()

    def _refresh_ui_data(self):
        today = self.db_manager.get_daily_summary(1)
        if today:
            focus_minutes = today[0]["total_focus_duration"] // 60
            fatigue_count = today[0]["fatigue_count"]
        else:
            focus_minutes = 0
            fatigue_count = 0

        self.window.set_today_stats(focus_minutes, fatigue_count)
        self.window.set_sessions(self.db_manager.get_focus_sessions())
        self.window.set_alarm_events(self.db_manager.get_alarm_events())

    def _load_settings_to_ui(self):
        self.window.focus_minutes_input.setText(self.db_manager.get_setting("focus_minutes", "25"))
        self.window.break_minutes_input.setText(self.db_manager.get_setting("break_minutes", "5"))
        self.window.fatigue_level_input.setText(self.db_manager.get_setting("fatigue_alarm_level", "2"))
        self.window.beep_enabled_input.setText(self.db_manager.get_setting("beep_enabled", "1"))

    def _get_int_setting(self, key, default):
        value = self.db_manager.get_setting(key, str(default))
        try:
            return int(value)
        except (TypeError, ValueError):
            return default


def main():
    app = QApplication(sys.argv)
    app.setOverrideCursor(QCursor(Qt.BlankCursor))
    base_dir = os.path.dirname(os.path.abspath(__file__))
    db_path = os.path.join(base_dir, "tasks.db")

    db_manager = DBManager(db_path)
    rpmsg_client = RPMsgClient(db_manager=db_manager)
    window = PomodoroWindow()
    PomodoroController(window, db_manager, rpmsg_client)

    window.showFullScreen()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
