#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PyQt5 基础界面模块。

UI 只发出用户意图信号，不直接操作数据库或 RPMsg 设备；真正的数据和硬件动作由
main.py 中的控制器接收信号后完成。
"""

import sys

try:
    from PyQt5.QtCore import Qt, QTimer, pyqtSignal
    from PyQt5.QtGui import QColor, QCursor, QFont, QPainter, QPen
    from PyQt5.QtWidgets import (
        QApplication,
        QGridLayout,
        QHBoxLayout,
        QLabel,
        QLineEdit,
        QMainWindow,
        QPushButton,
        QTableWidget,
        QTableWidgetItem,
        QTabWidget,
        QVBoxLayout,
        QWidget,
    )
except ImportError as exc:
    raise ImportError("请先安装 PyQt5: pip install PyQt5") from exc


class CircleTimer(QWidget):
    """番茄钟圆环进度组件。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.total_seconds = 25 * 60
        self.remaining_seconds = self.total_seconds
        self.setMinimumSize(220, 220)

    def set_time(self, remaining_seconds, total_seconds):
        self.remaining_seconds = max(0, int(remaining_seconds))
        self.total_seconds = max(1, int(total_seconds))
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.fillRect(self.rect(), QColor("#f4f7fb"))

        size = min(self.width(), self.height()) - 24
        left = (self.width() - size) / 2
        top = (self.height() - size) / 2

        painter.setPen(QPen(QColor("#d7dce2"), 12, Qt.SolidLine, Qt.RoundCap))
        painter.drawEllipse(int(left), int(top), int(size), int(size))

        progress = self.remaining_seconds / self.total_seconds
        painter.setPen(QPen(QColor("#2f80ed"), 12, Qt.SolidLine, Qt.RoundCap))
        painter.drawArc(
            int(left),
            int(top),
            int(size),
            int(size),
            90 * 16,
            int(-360 * 16 * progress),
        )

        minutes = self.remaining_seconds // 60
        seconds = self.remaining_seconds % 60
        painter.setPen(QColor("#1f2933"))
        painter.setFont(QFont("Microsoft YaHei", 28, QFont.Bold))
        painter.drawText(self.rect(), Qt.AlignCenter, f"{minutes:02d}:{seconds:02d}")


class PomodoroWindow(QMainWindow):
    """番茄钟主界面。"""

    start_requested = pyqtSignal(str)
    pause_requested = pyqtSignal()
    resume_requested = pyqtSignal()
    stop_requested = pyqtSignal()
    exit_requested = pyqtSignal()
    led_test_requested = pyqtSignal(str)
    beep_test_requested = pyqtSignal()
    settings_save_requested = pyqtSignal(dict)

    def __init__(self):
        super().__init__()
        self.is_paused = False
        self.setWindowTitle("AI 疲劳检测专注番茄钟")
        self.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
        self.setAttribute(Qt.WA_AcceptTouchEvents, True)
        self.setAttribute(Qt.WA_NoSystemBackground, False)
        self.setAutoFillBackground(True)
        self.resize(900, 620)
        self._build_ui()
        self.setStyleSheet(
            """
            QWidget {
                background: #f4f7fb;
                color: #101828;
                font-family: Microsoft YaHei, WenQuanYi Micro Hei, sans-serif;
                font-size: 18px;
            }
            QTabWidget::pane {
                border: 2px solid #344054;
                background: #f4f7fb;
            }
            QTabBar::tab {
                min-width: 96px;
                min-height: 44px;
                padding: 6px 12px;
                border: 1px solid #667085;
                background: #e4e7ec;
            }
            QTabBar::tab:selected {
                background: #ffffff;
                font-weight: bold;
            }
            QPushButton {
                min-height: 54px;
                border: 2px solid #344054;
                background: #ffffff;
                font-weight: bold;
            }
            QPushButton:pressed {
                background: #d0e4ff;
            }
            QLineEdit {
                min-height: 44px;
                border: 2px solid #667085;
                background: #ffffff;
                padding: 4px 8px;
            }
            """
        )
        self.keep_front_timer = QTimer(self)
        self.keep_front_timer.timeout.connect(self.keep_on_top)
        self.keep_front_timer.start(1000)

    def _build_ui(self):
        tabs = QTabWidget()
        tabs.addTab(self._build_main_tab(), "专注")
        tabs.addTab(self._build_history_tab(), "数据")
        tabs.addTab(self._build_settings_tab(), "设置")
        self.setCentralWidget(tabs)

    def _build_main_tab(self):
        page = QWidget()
        layout = QVBoxLayout(page)

        self.status_label = QLabel("空闲")
        self.status_label.setAlignment(Qt.AlignCenter)
        self.status_label.setFont(QFont("Microsoft YaHei", 24, QFont.Bold))

        self.timer_widget = CircleTimer()

        self.task_input = QLineEdit()
        self.task_input.setPlaceholderText("任务名称")

        stats_layout = QHBoxLayout()
        self.today_focus_label = QLabel("今日专注：0 分钟")
        self.today_fatigue_label = QLabel("今日疲劳：0 次")
        self.m4_status_label = QLabel("M4：未连接")
        stats_layout.addWidget(self.today_focus_label)
        stats_layout.addWidget(self.today_fatigue_label)
        stats_layout.addWidget(self.m4_status_label)

        button_layout = QHBoxLayout()
        self.start_button = QPushButton("开始专注")
        self.pause_button = QPushButton("暂停")
        self.stop_button = QPushButton("结束专注")
        self.exit_button = QPushButton("退出")
        for button in (self.start_button, self.pause_button, self.stop_button, self.exit_button):
            button.setAutoRepeat(False)
        button_layout.addWidget(self.start_button)
        button_layout.addWidget(self.pause_button)
        button_layout.addWidget(self.stop_button)
        button_layout.addWidget(self.exit_button)

        test_layout = QHBoxLayout()
        for color, text in (("R", "红灯"), ("G", "绿灯"), ("B", "蓝灯"), ("OFF", "熄灭")):
            button = QPushButton(text)
            button.setAutoRepeat(False)
            button.pressed.connect(lambda value=color: self.led_test_requested.emit(value))
            test_layout.addWidget(button)
        beep_button = QPushButton("蜂鸣器")
        beep_button.setEnabled(False)
        beep_button.setText("蜂鸣器(疲劳时响)")
        test_layout.addWidget(beep_button)

        layout.addWidget(self.status_label)
        layout.addWidget(self.timer_widget, alignment=Qt.AlignCenter)
        layout.addWidget(self.task_input)
        layout.addLayout(stats_layout)
        layout.addLayout(button_layout)
        layout.addLayout(test_layout)

        self.start_button.pressed.connect(self._emit_start)
        self.pause_button.pressed.connect(self._emit_pause_or_resume)
        self.stop_button.pressed.connect(self.stop_requested.emit)
        self.exit_button.pressed.connect(self.exit_requested.emit)
        return page

    def _build_history_tab(self):
        page = QWidget()
        layout = QVBoxLayout(page)

        self.sessions_table = QTableWidget(0, 6)
        self.sessions_table.setHorizontalHeaderLabels(["ID", "任务", "开始", "结束", "时长(s)", "状态"])

        self.alarm_table = QTableWidget(0, 6)
        self.alarm_table.setHorizontalHeaderLabels(["ID", "时间", "命令", "ACK", "成功", "失败原因"])

        layout.addWidget(QLabel("专注记录"))
        layout.addWidget(self.sessions_table)
        layout.addWidget(QLabel("硬件命令记录"))
        layout.addWidget(self.alarm_table)
        return page

    def _build_settings_tab(self):
        page = QWidget()
        layout = QGridLayout(page)

        self.focus_minutes_input = QLineEdit("25")
        self.break_minutes_input = QLineEdit("5")
        self.fatigue_level_input = QLineEdit("2")
        self.beep_enabled_input = QLineEdit("1")

        layout.addWidget(QLabel("专注时长(分钟)"), 0, 0)
        layout.addWidget(self.focus_minutes_input, 0, 1)
        layout.addWidget(QLabel("休息时长(分钟)"), 1, 0)
        layout.addWidget(self.break_minutes_input, 1, 1)
        layout.addWidget(QLabel("疲劳报警等级"), 2, 0)
        layout.addWidget(self.fatigue_level_input, 2, 1)
        layout.addWidget(QLabel("启用蜂鸣器(1/0)"), 3, 0)
        layout.addWidget(self.beep_enabled_input, 3, 1)

        save_button = QPushButton("保存设置")
        save_button.clicked.connect(self._emit_settings)
        layout.addWidget(save_button, 4, 0, 1, 2)
        return page

    def _emit_start(self):
        self.start_requested.emit(self.task_input.text().strip())

    def _emit_pause_or_resume(self):
        if self.is_paused:
            self.resume_requested.emit()
        else:
            self.pause_requested.emit()

    def _emit_settings(self):
        settings = {
            "focus_minutes": self.focus_minutes_input.text().strip(),
            "break_minutes": self.break_minutes_input.text().strip(),
            "fatigue_alarm_level": self.fatigue_level_input.text().strip(),
            "beep_enabled": self.beep_enabled_input.text().strip(),
        }
        self.settings_save_requested.emit(settings)

    def set_status(self, text):
        self.status_label.setText(text)

    def set_m4_status(self, text):
        self.m4_status_label.setText(f"M4：{text}")

    def set_timer(self, remaining_seconds, total_seconds):
        self.timer_widget.set_time(remaining_seconds, total_seconds)

    def set_paused(self, paused):
        self.is_paused = paused
        self.pause_button.setText("继续" if paused else "暂停")

    def set_today_stats(self, focus_minutes, fatigue_count):
        self.today_focus_label.setText(f"今日专注：{focus_minutes} 分钟")
        self.today_fatigue_label.setText(f"今日疲劳：{fatigue_count} 次")

    def set_sessions(self, sessions):
        self._fill_table(
            self.sessions_table,
            sessions,
            ["id", "task_name", "start_time", "end_time", "duration", "status"],
        )

    def set_alarm_events(self, events):
        self._fill_table(
            self.alarm_table,
            events,
            ["id", "timestamp", "command_sent", "ack_received", "success", "fail_reason"],
        )

    def showEvent(self, event):
        super().showEvent(event)
        self.keep_on_top()

    def keep_on_top(self):
        self.raise_()
        self.activateWindow()
        self.setFocus(Qt.ActiveWindowFocusReason)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Escape:
            QApplication.instance().quit()
        else:
            super().keyPressEvent(event)

    @staticmethod
    def _fill_table(table, rows, keys):
        table.setRowCount(len(rows))
        for row_index, row in enumerate(rows):
            for col_index, key in enumerate(keys):
                item = QTableWidgetItem(str(row.get(key, "")))
                table.setItem(row_index, col_index, item)
        table.resizeColumnsToContents()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setOverrideCursor(QCursor(Qt.BlankCursor))
    window = PomodoroWindow()

    demo_remaining = {"value": 25 * 60}

    def tick_demo():
        demo_remaining["value"] = max(0, demo_remaining["value"] - 1)
        window.set_timer(demo_remaining["value"], 25 * 60)

    timer = QTimer()
    timer.timeout.connect(tick_demo)
    timer.start(1000)

    window.showFullScreen()
    sys.exit(app.exec_())
