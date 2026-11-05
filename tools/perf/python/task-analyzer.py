#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# task-analyzer.py - comprehensive perf tasks analysis
# Copyright (c) 2022, Hagen Paul Pfeifer <hagen@jauu.net>
# Licensed under the terms of the GNU GPL License version 2
#
# Usage:
#
#     perf record -e sched:sched_switch -a -- sleep 10
#     ./task-analyzer.py
#
"""Comprehensive perf tasks analysis."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import decimal

from typing import List, Dict, Union

def _median(numbers: List[decimal.Decimal]) -> decimal.Decimal:
    """phython3 hat statistics module - we have nothing"""
    n = len(numbers)
    index = n // 2
    if n % 2:
        return sorted(numbers)[index]
    return sum(sorted(numbers)[index - 1 : index + 1]) / decimal.Decimal(2)

def _mean(numbers: List[decimal.Decimal]) -> decimal.Decimal:
    return sum(numbers) / decimal.Decimal(len(numbers))

import os
import string
import sys
from typing import Any, Optional
import perf


# Columns will have a static size to align everything properly
# Support of 116 days of active update with nano precision
LEN_SWITCHED_IN = len("9999999.999999999")
LEN_SWITCHED_OUT = len("9999999.999999999")
LEN_CPU = len("000")
LEN_PID = len("maxvalue")
LEN_TID = len("maxvalue")
LEN_COMM = len("max-comms-length")
LEN_RUNTIME = len("999999.999")
# Support of 3.45 hours of timespans
LEN_OUT_IN = len("99999999999.999")
LEN_OUT_OUT = len("99999999999.999")
LEN_IN_IN = len("99999999999.999")
LEN_IN_OUT = len("99999999999.999")

class Timespans:
    """Tracks elapsed time between occurrences of the same task."""
    def __init__(self, args: argparse.Namespace, time_unit: str) -> None:
        self.args = args
        self.time_unit = time_unit
        self._last_start: Optional[decimal.Decimal] = None
        self._last_finish: Optional[decimal.Decimal] = None
        self.current = {
            'out_out': decimal.Decimal(-1),
            'in_out': decimal.Decimal(-1),
            'out_in': decimal.Decimal(-1),
            'in_in': decimal.Decimal(-1)
        }
        if args.summary_extended:
            self._time_in: decimal.Decimal = decimal.Decimal(-1)
            self.max_vals = {
                'out_in': decimal.Decimal(-1),
                'at': decimal.Decimal(-1),
                'in_out': decimal.Decimal(-1),
                'in_in': decimal.Decimal(-1),
                'out_out': decimal.Decimal(-1)
            }

    def feed(self, task: 'Task') -> None:
        """Calculate timespans from chronological task occurrences."""
        if not self._last_finish:
            self._last_start = task.time_in(self.time_unit)
            self._last_finish = task.time_out(self.time_unit)
            return
        assert self._last_start is not None
        assert self._last_finish is not None
        self._time_in = task.time_in()
        time_in = task.time_in(self.time_unit)
        time_out = task.time_out(self.time_unit)
        self.current['in_in'] = time_in - self._last_start
        self.current['out_in'] = time_in - self._last_finish
        self.current['in_out'] = time_out - self._last_start
        self.current['out_out'] = time_out - self._last_finish
        if self.args.summary_extended:
            self.update_max_entries()
        self._last_finish = task.time_out(self.time_unit)
        self._last_start = task.time_in(self.time_unit)

    def update_max_entries(self) -> None:
        """Update maximum timespans."""
        self.max_vals['in_in'] = max(self.max_vals['in_in'], self.current['in_in'])
        self.max_vals['out_out'] = max(self.max_vals['out_out'], self.current['out_out'])
        self.max_vals['in_out'] = max(self.max_vals['in_out'], self.current['in_out'])
        if self.current['out_in'] > self.max_vals['out_in']:
            self.max_vals['out_in'] = self.current['out_in']
            self.max_vals['at'] = self._time_in

class Task:
    """Handles information of a given task."""
    def __init__(self, task_id: str, tid: int, cpu: int, comm: str) -> None:
        self.id = task_id
        self.tid = tid
        self.cpu = cpu
        self.comm = comm
        self.pid: Optional[int] = None
        self._time_in: Optional[decimal.Decimal] = None
        self._time_out: Optional[decimal.Decimal] = None

    def schedule_in_at(self, time_ns: int) -> None:
        """Set schedule in time."""
        self._time_in = decimal.Decimal(time_ns) / decimal.Decimal(1e9)

    def schedule_out_at(self, time_ns: int) -> None:
        """Set schedule out time."""
        self._time_out = decimal.Decimal(time_ns) / decimal.Decimal(1e9)

    def time_out(self, unit: str = "s") -> decimal.Decimal:
        """Return schedule out time."""
        factor = TaskAnalyzer.time_uniter(unit)
        return self._time_out * decimal.Decimal(factor) if self._time_out else decimal.Decimal(0)

    def time_in(self, unit: str = "s") -> decimal.Decimal:
        """Return schedule in time."""
        factor = TaskAnalyzer.time_uniter(unit)
        return self._time_in * decimal.Decimal(factor) if self._time_in else decimal.Decimal(0)

    def runtime(self, unit: str = "us") -> decimal.Decimal:
        """Return runtime."""
        factor = TaskAnalyzer.time_uniter(unit)
        if self._time_out is not None and self._time_in is not None:
            return (self._time_out - self._time_in) * decimal.Decimal(factor)
        return decimal.Decimal(0)

    def update_pid(self, pid: int) -> None:
        """Update PID."""
        self.pid = pid

class Summary:
    """
    Primary instance for calculating the summary output. Processes the whole trace to
    find and memorize relevant data such as mean, max et cetera. This instance handles
    dynamic alignment aspects for summary output.
    """

    def __init__(self, analyzer):
        self.analyzer = analyzer
        self.args = analyzer.args
        self.db = analyzer.db
        self.time_unit = analyzer.time_unit
        self.fd_sum = analyzer.fd_sum
        self._body = []

    class AlignmentHelper:
        """
        Used to calculated the alignment for the output of the summary.
        """
        def __init__(self, pid, tid, comm, runs, acc, mean,
                    median, min_val, max_val, max_at):
            self.pid = pid
            self.tid = tid
            self.comm = comm
            self.runs = runs
            self.acc = acc
            self.mean = mean
            self.median = median
            self.min = min_val
            self.max = max_val
            self.max_at = max_at
            self.out_in = None
            self.inter_at = None
            self.out_out = None
            self.in_in = None
            self.in_out = None

    def _print_header(self):
        '''
        Output is aligned using the formatted column widths in self.db.
        '''
        len_tasks = max(
            len("Task Information"),
            sum(self.db["task_info"].values()) + len(self.db["task_info"]) - 1,
        )
        fmt = "{{:^{}}}".format(len_tasks)
        fmt += " {{:^{}}}".format(
            sum(self.db["runtime_info"].values()) + len(self.db["runtime_info"]) - 1
        )
        _header = ("Task Information", "Runtime Information")

        if self.args.summary_extended:
            fmt += " {{:^{}}}".format(
                sum(self.db["inter_times"].values()) + len(self.db["inter_times"]) - 1
            )
            _header += ("Max Inter Task Times",)
        self.fd_sum.write(fmt.format(*_header) + "\n")

    def _column_titles(self):
        """
        Cells are being processed and displayed in different way so an alignment adjust
        is implemented depeding on the choice of the timeunit. The positions of the max
        values are being displayed in grey. Thus in their format two additional {},
        are placed for color set and reset.
        """
        separator, fix_csv_align = self.analyzer.prepare_fmt_sep(is_summary=True)
        fmt = "{{:>{}}}".format(self.db["task_info"]["pid"] * fix_csv_align)
        fmt += "{}{{:>{}}}".format(separator, self.db["task_info"]["tid"] * fix_csv_align)
        fmt += "{}{{:>{}}}".format(separator, self.db["task_info"]["comm"] * fix_csv_align)
        fmt += "{}{{:>{}}}".format(separator, self.db["runtime_info"]["runs"] * fix_csv_align)
        fmt += "{}{{:>{}}}".format(separator, self.db["runtime_info"]["acc"] * fix_csv_align)
        fmt += "{}{{:>{}}}".format(separator, self.db["runtime_info"]["mean"] * fix_csv_align)
        fmt += "{}{{:>{}}}".format(
            separator, self.db["runtime_info"]["median"] * fix_csv_align
        )
        fmt += "{}{{:>{}}}".format(
            separator, self.db["runtime_info"]["min"] * fix_csv_align
        )
        fmt += "{}{{:>{}}}".format(
            separator, self.db["runtime_info"]["max"] * fix_csv_align
        )
        fmt += "{}{{}}{{:>{}}}{{}}".format(
            separator, self.db["runtime_info"]["max_at"] * fix_csv_align
        )

        grey = "" if self.args.csv_summary else TaskAnalyzer.COLORS["grey"]
        reset = "" if self.args.csv_summary else TaskAnalyzer.COLORS["reset"]
        column_titles = ("PID", "TID", "Comm")
        column_titles += ("Runs", "Accumulated", "Mean", "Median", "Min", "Max")
        column_titles += (grey, "Max At", reset)

        if self.args.summary_extended:
            fmt += "{}{{:>{}}}".format(
                separator,
                self.db["inter_times"]["out_in"] * fix_csv_align
            )
            fmt += "{}{{}}{{:>{}}}{{}}".format(
                separator,
                self.db["inter_times"]["inter_at"] * fix_csv_align
            )
            fmt += "{}{{:>{}}}".format(
                separator,
                self.db["inter_times"]["out_out"] * fix_csv_align
            )
            fmt += "{}{{:>{}}}".format(
                separator,
                self.db["inter_times"]["in_in"] * fix_csv_align
            )
            fmt += "{}{{:>{}}}".format(
                separator,
                self.db["inter_times"]["in_out"] * fix_csv_align
            )

            column_titles += (
                "Out-In", grey, "Max At",
                reset, "Out-Out", "In-In", "In-Out"
            )

        self.fd_sum.write(fmt.format(*column_titles) + "\n")

    def _task_stats(self):
        """calculates the stats of every task and constructs the printable summary"""
        grey = "" if self.args.csv_summary else TaskAnalyzer.COLORS["grey"]
        reset = "" if self.args.csv_summary else TaskAnalyzer.COLORS["reset"]
        for tid in sorted(self.db["tid"]):
            color_one_sample = grey
            color_reset = reset
            no_executed = 0
            runtimes = []
            time_in = []
            timespans = Timespans(self.args, self.time_unit)
            for task in self.db["tid"][tid]:
                pid = task.pid
                comm = task.comm
                no_executed += 1
                runtimes.append(task.runtime(self.time_unit))
                time_in.append(task.time_in())
                timespans.feed(task)
            if len(runtimes) > 1:
                color_one_sample = ""
                color_reset = ""
            time_max = max(runtimes)
            time_min = min(runtimes)
            max_at = time_in[runtimes.index(max(runtimes))]

            # The size of the decimal after sum,mean and median varies, thus we cut
            # the decimal number, by rounding it. It has no impact on the output,
            # because we have a precision of the decimal points at the output.
            time_sum = round(sum(runtimes), 3)
            time_mean = round(_mean(runtimes), 3)
            time_median = round(_median(runtimes), 3)

            align_helper = self.AlignmentHelper(pid, tid, comm, no_executed, time_sum,
                                    time_mean, time_median, time_min, time_max, max_at)
            self._body.append([
                pid, tid, comm, no_executed, time_sum, color_one_sample,
                time_mean, time_median, time_min, time_max,
                grey, max_at,
                reset, color_reset
            ])
            if self.args.summary_extended:
                self._body[-1].extend([timespans.max_vals['out_in'],
                                grey, timespans.max_vals['at'],
                                reset, timespans.max_vals['out_out'],
                                timespans.max_vals['in_in'],
                                timespans.max_vals['in_out']])
                align_helper.out_in = timespans.max_vals['out_in']
                align_helper.inter_at = timespans.max_vals['at']
                align_helper.out_out = timespans.max_vals['out_out']
                align_helper.in_in = timespans.max_vals['in_in']
                align_helper.in_out = timespans.max_vals['in_out']
            self._calc_alignments_summary(align_helper)

    def _format_stats(self):
        separator, fix_csv_align = self.analyzer.prepare_fmt_sep(is_summary=True)
        decimal_precision, time_precision = self.analyzer.prepare_fmt_precision()
        len_pid = self.db["task_info"]["pid"] * fix_csv_align
        len_tid = self.db["task_info"]["tid"] * fix_csv_align
        len_comm = self.db["task_info"]["comm"] * fix_csv_align
        len_runs = self.db["runtime_info"]["runs"] * fix_csv_align
        len_acc = self.db["runtime_info"]["acc"] * fix_csv_align
        len_mean = self.db["runtime_info"]["mean"] * fix_csv_align
        len_median = self.db["runtime_info"]["median"] * fix_csv_align
        len_min = self.db["runtime_info"]["min"] * fix_csv_align
        len_max = self.db["runtime_info"]["max"] * fix_csv_align
        len_max_at = self.db["runtime_info"]["max_at"] * fix_csv_align
        if self.args.summary_extended:
            len_out_in = self.db["inter_times"]["out_in"] * fix_csv_align
            len_inter_at = self.db["inter_times"]["inter_at"] * fix_csv_align
            len_out_out = self.db["inter_times"]["out_out"] * fix_csv_align
            len_in_in = self.db["inter_times"]["in_in"] * fix_csv_align
            len_in_out = self.db["inter_times"]["in_out"] * fix_csv_align

        fmt = "{{:{}d}}".format(len_pid)
        fmt += "{}{{:{}d}}".format(separator, len_tid)
        fmt += "{}{{:>{}}}".format(separator, len_comm)
        fmt += "{}{{:{}d}}".format(separator, len_runs)
        fmt += "{}{{:{}.{}f}}".format(separator, len_acc, time_precision)
        fmt += "{}{{}}{{:{}.{}f}}".format(separator, len_mean, time_precision)
        fmt += "{}{{:{}.{}f}}".format(separator, len_median, time_precision)
        fmt += "{}{{:{}.{}f}}".format(separator, len_min, time_precision)
        fmt += "{}{{:{}.{}f}}".format(separator, len_max, time_precision)
        fmt += "{}{{}}{{:{}.{}f}}{{}}{{}}".format(
            separator, len_max_at, decimal_precision
        )
        if self.args.summary_extended:
            fmt += "{}{{:{}.{}f}}".format(separator, len_out_in, time_precision)
            fmt += "{}{{}}{{:{}.{}f}}{{}}".format(
                separator, len_inter_at, decimal_precision
            )
            fmt += "{}{{:{}.{}f}}".format(separator, len_out_out, time_precision)
            fmt += "{}{{:{}.{}f}}".format(separator, len_in_in, time_precision)
            fmt += "{}{{:{}.{}f}}".format(separator, len_in_out, time_precision)
        return fmt

    def _calc_alignments_summary(self, align_helper):
        # Measure the formatted string widths (e.g. f"{val:.{decimal_precision}f}"
        # and f"{val:.{time_precision}f}") rather than str(val), because Decimal's
        # default string representation may include unrounded fractional digits or
        # omit trailing zeros that _format_stats() renders with fixed precision.
        decimal_precision, time_precision = self.analyzer.prepare_fmt_precision()
        for key in self.db["task_info"]:
            val_len = len(str(getattr(align_helper, key)))
            if val_len > self.db["task_info"][key]:
                self.db["task_info"][key] = val_len
        for key in self.db["runtime_info"]:
            val = getattr(align_helper, key)
            if key == "runs":
                val_len = len(str(val))
            elif key == "max_at":
                val_len = len(f"{val:.{decimal_precision}f}")
            else:
                val_len = len(f"{val:.{time_precision}f}")
            if val_len > self.db["runtime_info"][key]:
                self.db["runtime_info"][key] = val_len
        if self.args.summary_extended:
            for key in self.db["inter_times"]:
                val = getattr(align_helper, key)
                if key == "inter_at":
                    val_len = len(f"{val:.{decimal_precision}f}")
                else:
                    val_len = len(f"{val:.{time_precision}f}")
                if val_len > self.db["inter_times"][key]:
                    self.db["inter_times"][key] = val_len

    def print(self):
        self._task_stats()
        fmt = self._format_stats()

        if not self.args.csv_summary:
            print("\nSummary", file=self.fd_sum)
            self._print_header()
        self._column_titles()
        for i in range(len(self._body)):
            self.fd_sum.write(fmt.format(*tuple(self._body[i])) + "\n")


class TaskAnalyzer:

    """Main class for task analysis."""

    COLORS = {
        "grey": "\033[90m",
        "red": "\033[91m",
        "green": "\033[92m",
        "yellow": "\033[93m",
        "blue": "\033[94m",
        "violet": "\033[95m",
        "reset": "\033[0m",
    }

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.db: Dict[str, Any] = {}
        self.session: Optional[perf.session] = None
        self._tgid_cache: Dict[int, int] = {}
        self.time_unit = "us"
        if args.ns:
            self.time_unit = "ns"
        elif args.ms:
            self.time_unit = "ms"
        self._init_db()
        self._check_color()
        self.fd_task = sys.stdout
        self.fd_sum = sys.stdout

    @contextmanager
    def open_output(self, filename: str, default: Any):
        """Context manager for file or stdout."""
        if filename:
            with open(filename, "w", encoding="utf-8") as f:
                yield f
        else:
            yield default

    def _init_db(self) -> None:
        self.db["running"] = {}
        self.db["tid"] = {}
        self.db["global"] = []
        if (self.args.summary or self.args.summary_extended or
                self.args.summary_only or self.args.csv_summary):
            self.db["task_info"] = {}
            self.db["runtime_info"] = {}
            self.db["task_info"]["pid"] = len("PID")
            self.db["task_info"]["tid"] = len("TID")
            self.db["task_info"]["comm"] = len("Comm")
            self.db["runtime_info"]["runs"] = len("Runs")
            self.db["runtime_info"]["acc"] = len("Accumulated")
            self.db["runtime_info"]["max"] = len("Max")
            self.db["runtime_info"]["max_at"] = len("Max At")
            self.db["runtime_info"]["min"] = len("Min")
            self.db["runtime_info"]["mean"] = len("Mean")
            self.db["runtime_info"]["median"] = len("Median")
            if self.args.summary_extended:
                self.db["inter_times"] = {}
                self.db["inter_times"]["out_in"] = len("Out-In")
                self.db["inter_times"]["inter_at"] = len("Max At")
                self.db["inter_times"]["out_out"] = len("Out-Out")
                self.db["inter_times"]["in_in"] = len("In-In")
                self.db["inter_times"]["in_out"] = len("In-Out")

    def _check_color(self) -> None:
        """Check if color should be enabled."""
        if self.args.csv:
            TaskAnalyzer.COLORS = {k: "" for k in TaskAnalyzer.COLORS}
            return
        if sys.stdout.isatty() and self.args.stdio_color != "never":
            return
        if self.args.stdio_color == "always":
            return
        TaskAnalyzer.COLORS = {k: "" for k in TaskAnalyzer.COLORS}

    @staticmethod
    def time_uniter(unit: str) -> float:
        """Return time unit factor."""
        picker = {"s": 1, "ms": 1e3, "us": 1e6, "ns": 1e9}
        return picker[unit]

    def _task_id(self, pid: int, cpu: int) -> str:
        return f"{pid}-{cpu}"

    def _filter_non_printable(self, unfiltered: Union[str, bytearray]) -> str:
        # Strip non-printable characters, whitespace control characters
        # (\r\n\t\x0b\x0c), quotes (", '), and ';' because --csv and
        # --csv-summary emit unquoted ';'-delimited columns. Also replace a
        # leading '=', '+', '-', or '@' with '_' to mitigate CSV formula / DDE
        # injection attacks if an attacker-controlled process comm is opened in
        # a spreadsheet program.
        if isinstance(unfiltered, (bytearray, bytes)):
            unfiltered = unfiltered.decode('utf-8', 'ignore')
        filtered = ""
        for char in unfiltered:
            if char in string.printable and char not in "\r\n\t\x0b\x0c;\"'":
                filtered += char
        stripped = filtered.lstrip()
        if stripped and stripped[0] in "=+-@":
            filtered = "_" + stripped[1:]
        return filtered

    def prepare_fmt_precision(self) -> tuple[int, int]:
        if self.args.ns:
            return 9, 0
        return 6, 3

    def prepare_fmt_sep(self, is_summary: bool = False) -> tuple[str, int]:
        # fix_csv_align multiplies format field widths: 0 in CSV mode collapses
        # {:>N} padding to {:>0} (no extra spaces around ';'), while 1 in
        # standard text mode preserves fixed-width column alignment.
        if (is_summary and self.args.csv_summary) or (not is_summary and self.args.csv):
            return ";", 0
        return " ", 1

    def _fmt_header(self) -> str:
        separator, fix_csv_align = self.prepare_fmt_sep()
        fmt = f"{{:>{LEN_SWITCHED_IN*fix_csv_align}}}"
        fmt += f"{separator}{{:>{LEN_SWITCHED_OUT*fix_csv_align}}}"
        fmt += f"{separator}{{:>{LEN_CPU*fix_csv_align}}}"
        fmt += f"{separator}{{:>{LEN_PID*fix_csv_align}}}"
        fmt += f"{separator}{{:>{LEN_TID*fix_csv_align}}}"
        fmt += f"{separator}{{:>{LEN_COMM*fix_csv_align}}}"
        fmt += f"{separator}{{:>{LEN_RUNTIME*fix_csv_align}}}"
        fmt += f"{separator}{{:>{LEN_OUT_IN*fix_csv_align}}}"
        if self.args.extended_times:
            fmt += f"{separator}{{:>{LEN_OUT_OUT*fix_csv_align}}}"
            fmt += f"{separator}{{:>{LEN_IN_IN*fix_csv_align}}}"
            fmt += f"{separator}{{:>{LEN_IN_OUT*fix_csv_align}}}"
        return fmt

    def _fmt_body(self) -> str:
        separator, fix_csv_align = self.prepare_fmt_sep()
        decimal_precision, time_precision = self.prepare_fmt_precision()
        fmt = f"{{}}{{:{LEN_SWITCHED_IN*fix_csv_align}.{decimal_precision}f}}"
        fmt += f"{separator}{{:{LEN_SWITCHED_OUT*fix_csv_align}.{decimal_precision}f}}"
        fmt += f"{separator}{{:{LEN_CPU*fix_csv_align}d}}"
        fmt += f"{separator}{{:{LEN_PID*fix_csv_align}d}}"
        fmt += f"{separator}{{}}{{:{LEN_TID*fix_csv_align}d}}{{}}"
        fmt += f"{separator}{{}}{{:>{LEN_COMM*fix_csv_align}}}"
        fmt += f"{separator}{{:{LEN_RUNTIME*fix_csv_align}.{time_precision}f}}"
        if self.args.extended_times:
            fmt += f"{separator}{{:{LEN_OUT_IN*fix_csv_align}.{time_precision}f}}"
            fmt += f"{separator}{{:{LEN_OUT_OUT*fix_csv_align}.{time_precision}f}}"
            fmt += f"{separator}{{:{LEN_IN_IN*fix_csv_align}.{time_precision}f}}"
            fmt += f"{separator}{{:{LEN_IN_OUT*fix_csv_align}.{time_precision}f}}{{}}"
        else:
            fmt += f"{separator}{{:{LEN_OUT_IN*fix_csv_align}.{time_precision}f}}{{}}"
        return fmt

    def _print_header(self) -> None:
        fmt = self._fmt_header()
        header = ["Switched-In", "Switched-Out", "CPU", "PID", "TID", "Comm",
                  "Runtime", "Time Out-In"]
        if self.args.extended_times:
            header += ["Time Out-Out", "Time In-In", "Time In-Out"]
        self.fd_task.write(fmt.format(*header) + "\n")

    def _print_task_finish(self, task: Task) -> None:
        c_row_set = ""
        c_row_reset = ""
        out_in: Any = -1
        out_out: Any = -1
        in_in: Any = -1
        in_out: Any = -1
        fmt = self._fmt_body()

        if str(task.tid) in self.args.highlight_tasks_map:
            c_row_set = TaskAnalyzer.COLORS.get(self.args.highlight_tasks_map[str(task.tid)], '')
            c_row_reset = TaskAnalyzer.COLORS["reset"]
        if task.comm in self.args.highlight_tasks_map:
            c_row_set = TaskAnalyzer.COLORS.get(self.args.highlight_tasks_map[task.comm], '')
            c_row_reset = TaskAnalyzer.COLORS["reset"]

        c_tid_set = ""
        c_tid_reset = ""
        if task.pid == task.tid:
            c_tid_set = TaskAnalyzer.COLORS["grey"]
            c_tid_reset = TaskAnalyzer.COLORS["reset"]

        if task.tid in self.db["tid"]:
            last_tid_task = self.db["tid"][task.tid][-1]
            timespan_gap_tid = Timespans(self.args, self.time_unit)
            timespan_gap_tid.feed(last_tid_task)
            timespan_gap_tid.feed(task)
            out_in = timespan_gap_tid.current['out_in']
            out_out = timespan_gap_tid.current['out_out']
            in_in = timespan_gap_tid.current['in_in']
            in_out = timespan_gap_tid.current['in_out']

        if self.args.extended_times:
            line_out = fmt.format(c_row_set, task.time_in(), task.time_out(), task.cpu,
                            task.pid, c_tid_set, task.tid, c_tid_reset, c_row_set, task.comm,
                            task.runtime(self.time_unit), out_in, out_out, in_in, in_out,
                            c_row_reset) + "\n"
        else:
            line_out = fmt.format(c_row_set, task.time_in(), task.time_out(), task.cpu,
                            task.pid, c_tid_set, task.tid, c_tid_reset, c_row_set, task.comm,
                            task.runtime(self.time_unit), out_in, c_row_reset) + "\n"
        self.fd_task.write(line_out)

    def _record_cleanup(self, _list: list[Any]) -> list[Any]:
        need_summary = (self.args.summary or self.args.summary_extended or
                        self.args.summary_only or self.args.csv_summary)
        if not need_summary and len(_list) > 1:
            return _list[len(_list) - 1:]
        return _list

    def _record_by_tid(self, task: Task) -> None:
        tid = task.tid
        if tid not in self.db["tid"]:
            self.db["tid"][tid] = []
        self.db["tid"][tid].append(task)
        self.db["tid"][tid] = self._record_cleanup(self.db["tid"][tid])

    def _record_global(self, task: Task) -> None:
        self.db["global"].append(task)
        self.db["global"] = self._record_cleanup(self.db["global"])

    def _handle_task_finish(self, tid: int, cpu: int, time_ns: int, pid: int) -> None:
        if tid == 0 and (not self.args.tid_renames or 0 not in self.args.tid_renames):
            return
        _id = self._task_id(tid, cpu)
        if _id not in self.db["running"]:
            return
        task = self.db["running"][_id]
        task.schedule_out_at(time_ns)
        task.update_pid(pid)
        del self.db["running"][_id]

        if not self._limit_filtered(tid, pid, task.comm):
            if not self.args.summary_only:
                self._print_task_finish(task)
            self._record_by_tid(task)
            self._record_global(task)

    def _handle_task_start(self, tid: int, cpu: int, comm: str, time_ns: int) -> None:
        if tid == 0 and (not self.args.tid_renames or 0 not in self.args.tid_renames):
            return
        if tid in self.args.tid_renames:
            comm = self._filter_non_printable(self.args.tid_renames[tid])
        _id = self._task_id(tid, cpu)
        if _id in self.db["running"]:
            return
        task = Task(_id, tid, cpu, comm)
        task.schedule_in_at(time_ns)
        self.db["running"][_id] = task

    def _limit_filtered(self, tid: int, pid: int, comm: str) -> bool:
        """Filter tasks based on CLI arguments."""
        match_filter = False
        if self.args.filter_tasks:
            if (str(tid) in self.args.filter_tasks or
                str(pid) in self.args.filter_tasks or
                comm in self.args.filter_tasks):
                match_filter = True

        match_limit = False
        if self.args.limit_to_tasks:
            if (str(tid) in self.args.limit_to_tasks or
                str(pid) in self.args.limit_to_tasks or
                comm in self.args.limit_to_tasks):
                match_limit = True

        if self.args.filter_tasks and match_filter:
            return True
        if self.args.limit_to_tasks and not match_limit:
            return True
        return False

    def _is_within_timelimit(self, time_ns: int) -> bool:
        if not self.args.time_limit:
            return True
        time_s = decimal.Decimal(time_ns) / decimal.Decimal(1e9)
        bounds = self.args.time_limit.split(":")
        lower_bound = bounds[0] if len(bounds) > 0 else ""
        upper_bound = bounds[1] if len(bounds) > 1 else ""
        if lower_bound and time_s < decimal.Decimal(lower_bound):
            return False
        if upper_bound and time_s > decimal.Decimal(upper_bound):
            return False
        return True

    def process_event(self, sample: perf.sample_event) -> None:
        """Process sched:sched_switch events."""
        if "sched:sched_switch" not in str(sample.evsel):
            return

        time_ns = sample.sample_time
        if not self._is_within_timelimit(time_ns):
            return

        # Access tracepoint fields directly from sample object
        try:
            prev_pid = sample.prev_pid
            next_pid = sample.next_pid
            next_comm = sample.next_comm
            common_cpu = sample.sample_cpu
        except AttributeError:
            if not self.db.get("warned_missing_fields"):
                self.db["warned_missing_fields"] = True
                print("Warning: sched:sched_switch sample missing tracepoint fields "
                      "(is libtraceevent support enabled?).", file=sys.stderr)
            return

        next_comm = self._filter_non_printable(next_comm)

        # Task finish for previous task. Resolve TGID (process ID) from TID:
        # in file mode we query perf.session.find_thread() (checking thread.pid > 0
        # so an unresolved -1 does not overwrite the prev_pid fallback), whereas in
        # live mode (self.session is None) we read /proc/<tid>/status and cache
        # the lookup in _tgid_cache.
        prev_tgid = prev_pid  # Fallback
        if prev_pid != 0:
            if self.session:
                try:
                    thread = self.session.find_thread(-1, prev_pid)
                    if thread and thread.pid > 0:
                        prev_tgid = thread.pid
                except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError):
                    pass
            elif prev_pid in self._tgid_cache:
                prev_tgid = self._tgid_cache[prev_pid]
            else:
                if len(self._tgid_cache) >= 4096:
                    self._tgid_cache.pop(next(iter(self._tgid_cache)))
                self._tgid_cache[prev_pid] = prev_tgid
                try:
                    with open(f"/proc/{prev_pid}/status", encoding="utf-8") as f:
                        for line in f:
                            if line.startswith("Tgid:"):
                                prev_tgid = int(line.split()[1])
                                self._tgid_cache[prev_pid] = prev_tgid
                                break
                except (OSError, ValueError, KeyError, RuntimeError):
                    pass
        self._handle_task_finish(prev_pid, common_cpu, time_ns, prev_tgid)
        # Task start for next task
        self._handle_task_start(next_pid, common_cpu, next_comm, time_ns)

    def print_summary(self) -> None:
        """Calculate and print summary."""
        need_summary = (self.args.summary or self.args.summary_extended or
                        self.args.summary_only or self.args.csv_summary)
        if not need_summary:
            return

        Summary(self).print()

    def _run_file(self) -> None:
        if not self.args.summary_only:
            self._print_header()

        session = perf.session(perf.data(self.args.input), sample=self.process_event)
        self.session = session
        try:
            session.process_events()
        except KeyboardInterrupt:
            pass
        finally:
            # Break the reference cycle between self.session and the bound
            # self.process_event callback so the C perf.session object is freed.
            self.session = None

        if not self.db["global"]:
            print(f"Warning: No sched:sched_switch trace events found in '{self.args.input}'.",
                  file=sys.stderr)

        self.print_summary()

    def _run_live(self) -> None:
        if not self.args.summary_only:
            self._print_header()

        cpus = perf.cpu_map()
        threads = perf.thread_map(-1)
        evlist = perf.parse_events("sched:sched_switch", cpus, threads)
        evlist.config()

        evlist.open()
        evlist.mmap()
        evlist.enable()

        # Per-CPU perf ring buffers are drained sequentially in a loop, so events
        # read from CPU N may have timestamps slightly earlier than events just
        # read from CPU 0. Buffer events in pending_events and only dispatch those
        # older than a 50 ms watermark behind max_ts so cross-CPU migrations and
        # switches are processed in global timestamp order.
        pending_events: list[perf.sample_event] = []
        print("Live mode started. Press Ctrl+C to stop.", file=sys.stderr)
        try:
            while True:
                try:
                    evlist.poll(timeout=100)
                except InterruptedError:
                    continue
                for cpu in cpus:
                    while True:
                        event = evlist.read_on_cpu(cpu)
                        if not event:
                            break
                        if not isinstance(event, perf.sample_event):
                            continue
                        pending_events.append(event)
                if pending_events:
                    pending_events.sort(
                        key=lambda e: getattr(e, 'sample_time', getattr(e, 'time', 0))
                    )
                    max_ts = getattr(
                        pending_events[-1], 'sample_time', getattr(pending_events[-1], 'time', 0)
                    )
                    cutoff_ts = max_ts - 50_000_000
                    ready_idx = 0
                    for event in pending_events:
                        ev_ts = getattr(event, 'sample_time', getattr(event, 'time', 0))
                        if ev_ts <= cutoff_ts:
                            self.process_event(event)
                            ready_idx += 1
                        else:
                            break
                    del pending_events[:ready_idx]
        except KeyboardInterrupt:
            print("\nStopping live mode...", file=sys.stderr)
        finally:
            pending_events.sort(key=lambda e: getattr(e, 'sample_time', getattr(e, 'time', 0)))
            for event in pending_events:
                self.process_event(event)
            evlist.close()
            self.print_summary()

    def run(self) -> None:
        """Run the session."""
        is_live = (self.args.live or
                   (not os.path.exists(self.args.input) and self.args.input == "perf.data"))
        with self.open_output(self.args.csv, sys.stdout) as fd_task:
            if self.args.csv and self.args.csv == self.args.csv_summary:
                self.fd_task = fd_task
                self.fd_sum = fd_task
                if is_live:
                    self._run_live()
                else:
                    self._run_file()
            else:
                with self.open_output(self.args.csv_summary, sys.stdout) as fd_sum:
                    self.fd_task = fd_task
                    self.fd_sum = fd_sum
                    if is_live:
                        self._run_live()
                    else:
                        self._run_file()

def main() -> None:
    """Main function."""
    parser = argparse.ArgumentParser(description="Analyze tasks behavior")
    parser.add_argument("-i", "--input", default="perf.data", help="Input file name")
    parser.add_argument("--time-limit", default="", help="print tasks only in time window")
    parser.add_argument("--summary", action="store_true",
                        help="print additional runtime information")
    parser.add_argument("--summary-only", action="store_true",
                        help="print only summary without traces")
    parser.add_argument("--summary-extended", action="store_true",
                        help="print extended summary")
    parser.add_argument("--ns", action="store_true", help="show timestamps in nanoseconds")
    parser.add_argument("--ms", action="store_true", help="show timestamps in milliseconds")
    parser.add_argument("--live", action="store_true",
                        help="force live mode (ignores -i/perf.data)")
    parser.add_argument("--extended-times", action="store_true",
                        help="Show elapsed times between schedule in/out")
    parser.add_argument("--filter-tasks", default="", help="filter tasks by tid, pid or comm")
    parser.add_argument("--limit-to-tasks", default="", help="limit output to selected tasks")
    parser.add_argument("--highlight-tasks", default="", help="colorize special tasks")
    parser.add_argument("--rename-comms-by-tids", default="", help="rename task names by using tid")
    parser.add_argument("--stdio-color", default="auto", choices=["always", "never", "auto"],
                        help="configure color output")
    parser.add_argument("--csv", default="", help="Write trace to file")
    parser.add_argument("--csv-summary", default="", help="Write summary to file")

    args = parser.parse_args()
    args.tid_renames = {}
    args.highlight_tasks_map = {}
    args.filter_tasks = args.filter_tasks.split(",") if args.filter_tasks else []
    args.limit_to_tasks = args.limit_to_tasks.split(",") if args.limit_to_tasks else []

    if args.rename_comms_by_tids:
        for item in args.rename_comms_by_tids.split(","):
            try:
                tid, name = item.split(":", 1)
                args.tid_renames[int(tid)] = name
            except ValueError:
                print(f"Error: Invalid format for --rename-comms-by-tids '{item}', "
                      "expected tid:name", file=sys.stderr)
                sys.exit(1)

    if args.highlight_tasks:
        for item in args.highlight_tasks.split(","):
            parts = item.split(":")
            if len(parts) == 1:
                parts.append("red")
            key, color = parts[0], parts[1]
            args.highlight_tasks_map[key] = color

    analyzer = TaskAnalyzer(args)
    analyzer.run()

if __name__ == "__main__":
    main()
