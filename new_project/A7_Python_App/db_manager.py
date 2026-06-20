#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SQLite 数据库管理模块。

本模块是 A7 端所有数据库操作的唯一入口。UI 层和业务逻辑层只调用
DBManager 暴露的方法，不直接写 SQL，避免后续维护时出现字段和查询逻辑分散。
"""

import os
import sqlite3
from datetime import datetime, date


class DBManager:
    """封装 focus_sessions、fatigue_events、alarm_events、settings、daily_summary。"""

    def __init__(self, db_name="tasks.db"):
        self.db_name = db_name
        self._ensure_db_dir()
        self.init_database()

    def _ensure_db_dir(self):
        """如果数据库路径包含目录，先创建目录，保证首次运行可以自动生成数据库。"""
        db_dir = os.path.dirname(os.path.abspath(self.db_name))
        if db_dir and not os.path.exists(db_dir):
            os.makedirs(db_dir, exist_ok=True)

    def init_database(self):
        """创建项目需要的全部数据表。

        使用 IF NOT EXISTS 是为了让程序可以重复启动；已有数据库不会被覆盖，
        后续如果要升级字段，可以再单独增加迁移逻辑。
        """
        with sqlite3.connect(self.db_name) as conn:
            conn.execute("PRAGMA foreign_keys = ON")

            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS focus_sessions (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    task_name TEXT NOT NULL DEFAULT '',
                    start_time TEXT NOT NULL,
                    end_time TEXT,
                    duration INTEGER NOT NULL DEFAULT 0,
                    status TEXT NOT NULL DEFAULT 'running'
                )
                """
            )

            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS fatigue_events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    session_id INTEGER,
                    timestamp TEXT NOT NULL,
                    fatigue_level INTEGER NOT NULL,
                    ear REAL NOT NULL,
                    mar REAL NOT NULL,
                    eye_closed_duration REAL NOT NULL,
                    yawn_count INTEGER NOT NULL,
                    alarm_triggered INTEGER NOT NULL DEFAULT 0,
                    FOREIGN KEY (session_id) REFERENCES focus_sessions(id)
                )
                """
            )

            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS alarm_events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp TEXT NOT NULL,
                    command_sent TEXT NOT NULL,
                    ack_received TEXT NOT NULL DEFAULT '',
                    success INTEGER NOT NULL,
                    fail_reason TEXT NOT NULL DEFAULT ''
                )
                """
            )

            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS settings (
                    key TEXT PRIMARY KEY,
                    value TEXT NOT NULL
                )
                """
            )

            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS daily_summary (
                    date TEXT PRIMARY KEY,
                    total_focus_duration INTEGER NOT NULL DEFAULT 0,
                    fatigue_count INTEGER NOT NULL DEFAULT 0,
                    alarm_count INTEGER NOT NULL DEFAULT 0
                )
                """
            )

            conn.commit()

    def start_focus_session(self, task_name="") -> int:
        """开始一次专注会话，并返回新建 session 的 id。"""
        start_time = self._now_text()
        with sqlite3.connect(self.db_name) as conn:
            cursor = conn.execute(
                """
                INSERT INTO focus_sessions (task_name, start_time, status)
                VALUES (?, ?, ?)
                """,
                (task_name, start_time, "running"),
            )
            conn.commit()
            return cursor.lastrowid

    def end_focus_session(self, session_id, status):
        """结束一次专注会话，并自动更新当天 daily_summary。

        daily_summary 选择在专注结束时更新，是为了让统计页面读取更快，同时避免
        每次打开统计页都重复扫描全部历史表。
        """
        end_time = self._now_text()
        with sqlite3.connect(self.db_name) as conn:
            conn.execute("PRAGMA foreign_keys = ON")
            row = conn.execute(
                "SELECT start_time FROM focus_sessions WHERE id = ?",
                (session_id,),
            ).fetchone()
            if row is None:
                raise ValueError(f"focus session 不存在: {session_id}")

            duration = self._calc_duration_seconds(row[0], end_time)
            conn.execute(
                """
                UPDATE focus_sessions
                SET end_time = ?, duration = ?, status = ?
                WHERE id = ?
                """,
                (end_time, duration, status, session_id),
            )
            conn.commit()

        self.update_daily_summary(end_time[:10])

    def add_fatigue_event(
        self,
        session_id,
        fatigue_level,
        ear,
        mar,
        eye_closed_duration,
        yawn_count,
        alarm_triggered,
    ):
        """记录一次疲劳检测事件。

        EAR/MAR 等半处理数据一并入库，后续调整阈值或分析误报时才有依据。
        """
        with sqlite3.connect(self.db_name) as conn:
            conn.execute("PRAGMA foreign_keys = ON")
            conn.execute(
                """
                INSERT INTO fatigue_events (
                    session_id, timestamp, fatigue_level, ear, mar,
                    eye_closed_duration, yawn_count, alarm_triggered
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    session_id,
                    self._now_text(),
                    fatigue_level,
                    ear,
                    mar,
                    eye_closed_duration,
                    yawn_count,
                    int(bool(alarm_triggered)),
                ),
            )
            conn.commit()

    def get_fatigue_events(self, session_id):
        """读取某次专注会话下的疲劳事件，供 UI 历史页面展示。"""
        with sqlite3.connect(self.db_name) as conn:
            conn.row_factory = sqlite3.Row
            rows = conn.execute(
                """
                SELECT id, session_id, timestamp, fatigue_level, ear, mar,
                       eye_closed_duration, yawn_count, alarm_triggered
                FROM fatigue_events
                WHERE session_id = ?
                ORDER BY timestamp ASC
                """,
                (session_id,),
            ).fetchall()
            return [dict(row) for row in rows]

    def add_alarm_event(self, command_sent, ack_received, success, fail_reason=""):
        """记录一次发送给 M4 的硬件命令及结果。"""
        with sqlite3.connect(self.db_name) as conn:
            conn.execute(
                """
                INSERT INTO alarm_events (
                    timestamp, command_sent, ack_received, success, fail_reason
                )
                VALUES (?, ?, ?, ?, ?)
                """,
                (
                    self._now_text(),
                    command_sent,
                    ack_received or "",
                    int(bool(success)),
                    fail_reason or "",
                ),
            )
            conn.commit()

    def get_setting(self, key, default=None):
        """读取配置项；不存在时返回 default。"""
        with sqlite3.connect(self.db_name) as conn:
            row = conn.execute(
                "SELECT value FROM settings WHERE key = ?",
                (key,),
            ).fetchone()
            if row is None:
                return default
            return row[0]

    def set_setting(self, key, value):
        """写入配置项；同一个 key 重复写入时覆盖旧值。"""
        with sqlite3.connect(self.db_name) as conn:
            conn.execute(
                """
                INSERT INTO settings (key, value)
                VALUES (?, ?)
                ON CONFLICT(key) DO UPDATE SET value = excluded.value
                """,
                (key, str(value)),
            )
            conn.commit()

    def update_daily_summary(self, summary_date):
        """按指定日期重新汇总 daily_summary。

        这里采用“重新计算当天结果再覆盖”的方式，比单纯累加更稳：如果某次会话
        后续被修正为 interrupted 或补录事件，重新汇总不会产生重复计数。
        """
        if isinstance(summary_date, date):
            date_text = summary_date.isoformat()
        else:
            date_text = str(summary_date)

        with sqlite3.connect(self.db_name) as conn:
            total_focus_duration = conn.execute(
                """
                SELECT COALESCE(SUM(duration), 0)
                FROM focus_sessions
                WHERE date(start_time) = ? AND status = ?
                """,
                (date_text, "completed"),
            ).fetchone()[0]

            fatigue_count = conn.execute(
                """
                SELECT COUNT(*)
                FROM fatigue_events
                WHERE date(timestamp) = ?
                """,
                (date_text,),
            ).fetchone()[0]

            alarm_count = conn.execute(
                """
                SELECT COUNT(*)
                FROM alarm_events
                WHERE date(timestamp) = ? AND success = 1
                """,
                (date_text,),
            ).fetchone()[0]

            conn.execute(
                """
                INSERT INTO daily_summary (
                    date, total_focus_duration, fatigue_count, alarm_count
                )
                VALUES (?, ?, ?, ?)
                ON CONFLICT(date) DO UPDATE SET
                    total_focus_duration = excluded.total_focus_duration,
                    fatigue_count = excluded.fatigue_count,
                    alarm_count = excluded.alarm_count
                """,
                (date_text, total_focus_duration, fatigue_count, alarm_count),
            )
            conn.commit()

    def get_daily_summary(self, days=30):
        """读取最近 days 条每日统计缓存。"""
        with sqlite3.connect(self.db_name) as conn:
            conn.row_factory = sqlite3.Row
            rows = conn.execute(
                """
                SELECT date, total_focus_duration, fatigue_count, alarm_count
                FROM daily_summary
                ORDER BY date DESC
                LIMIT ?
                """,
                (days,),
            ).fetchall()
            return [dict(row) for row in rows]

    def get_focus_sessions(self, limit=50):
        """读取最近的专注记录，供 UI 历史页面展示。"""
        with sqlite3.connect(self.db_name) as conn:
            conn.row_factory = sqlite3.Row
            rows = conn.execute(
                """
                SELECT id, task_name, start_time, end_time, duration, status
                FROM focus_sessions
                ORDER BY id DESC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
            return [dict(row) for row in rows]

    def get_alarm_events(self, limit=100):
        """读取最近的硬件命令记录，便于排查 RPMsg 通信问题。"""
        with sqlite3.connect(self.db_name) as conn:
            conn.row_factory = sqlite3.Row
            rows = conn.execute(
                """
                SELECT id, timestamp, command_sent, ack_received, success, fail_reason
                FROM alarm_events
                ORDER BY id DESC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
            return [dict(row) for row in rows]

    @staticmethod
    def _now_text():
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    @staticmethod
    def _calc_duration_seconds(start_time, end_time):
        start_dt = datetime.strptime(start_time, "%Y-%m-%d %H:%M:%S")
        end_dt = datetime.strptime(end_time, "%Y-%m-%d %H:%M:%S")
        return max(0, int((end_dt - start_dt).total_seconds()))


if __name__ == "__main__":
    test_db = DBManager("test_tasks.db")
    session_id = test_db.start_focus_session("数据库自测")
    test_db.add_fatigue_event(session_id, 2, 0.21, 0.65, 1.8, 1, True)
    test_db.add_alarm_event("LED:R", "ACK:LED:R", True)
    test_db.set_setting("focus_minutes", 25)
    test_db.end_focus_session(session_id, "completed")

    print("session_id:", session_id)
    print("setting focus_minutes:", test_db.get_setting("focus_minutes"))
    print("fatigue_events:", test_db.get_fatigue_events(session_id))
    print("daily_summary:", test_db.get_daily_summary())
