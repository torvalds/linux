# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import logging
import inspect
from typing import Literal


MessageTemplate = str


class MessageLogger:
    """Logger that suppresses repeated messages and stores a summary of all logged messages."""

    _messages: dict[MessageTemplate, list[str]]
    _message_counts: dict[MessageTemplate, int]
    _repeated_logs_limit: int
    """Maximum number of repeated messages of the same type to log before suppressing further output."""

    def __init__(self, level: Literal["error", "warning"], repeated_logs_limit: int = 3) -> None:
        self._level = level
        self._messages = {}
        self._message_counts = {}
        self._repeated_logs_limit = repeated_logs_limit

    def log(self, template: MessageTemplate, /, **kwargs: str) -> None:
        """Log a message based on a template and optional variables. Example: `log("Missing {path}", path=str(p))`."""
        message = template
        for key, value in kwargs.items():
            message = message.replace("{" + key + "}", value)
        if template not in self._messages:
            self._messages[template] = []
            self._message_counts[template] = 0
        self._message_counts[template] += 1
        if self._message_counts[template] <= self._repeated_logs_limit:
            if self._level == "error":
                logging.error(message)
            elif self._level == "warning":
                logging.warning(message)
            self._messages[template].append(message)

    def get_summary(self) -> str:
        if len(self._messages) == 0:
            return ""
        summary: list[str] = [f"Summarize {self._level}s:"]
        for template, messages in self._messages.items():
            for message in messages:
                summary.append(message)
            n_suppressed_messages = self._message_counts[template] - self._repeated_logs_limit
            if n_suppressed_messages > 0:
                instances = "instance" if n_suppressed_messages == 1 else "instances"
                summary.append(f"... (Found {n_suppressed_messages} more {instances} of this {self._level})")
        return "\n".join(summary)

    def has_messages(self) -> bool:
        return len(self._message_counts) > 0


_warning_logger: MessageLogger
_error_logger: MessageLogger


def warning(msg_template: MessageTemplate, /, **kwargs: str) -> None:
    _warning_logger.log(msg_template, **kwargs)


def error(msg_template: MessageTemplate, /, **kwargs: str) -> None:
    frame = inspect.currentframe()
    caller_frame = frame.f_back if frame else None
    info = inspect.getframeinfo(caller_frame) if caller_frame else None
    if info:
        msg_template = f'File "{info.filename}", line {info.lineno}, in {info.function}\n{msg_template}'
    _error_logger.log(msg_template, **kwargs)


def summarize_warnings() -> str:
    return _warning_logger.get_summary()


def summarize_errors() -> str:
    return _error_logger.get_summary()


def has_errors() -> bool:
    return _error_logger.has_messages()


def init() -> None:
    global _warning_logger, _error_logger
    _warning_logger = MessageLogger("warning")
    _error_logger = MessageLogger("error")


init()
