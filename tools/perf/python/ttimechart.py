#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""ttimechart.py - interactive perf timechart written using textual.

Reads a perf.data file, typically created with 'perf timechart record', and
displays per-CPU and per-task timelines in the terminal. Scheduler
(sched:sched_switch, sched:sched_wakeup), power (power:cpu_idle,
power:cpu_frequency) and I/O syscall (perf timechart record -I) tracepoints
are understood. Unlike 'perf timechart', which writes an SVG file, the
timeline can be zoomed, panned and queried interactively.

Usage:
    perf timechart record -- <workload>
    perf timechart --tui
or:
    perf script ttimechart [-i perf.data]
"""
from __future__ import annotations

from abc import ABC, abstractmethod
import argparse
import bisect
from collections import defaultdict
from dataclasses import dataclass, replace
import math
import os
import sys
import threading
from time import monotonic
from typing import Any, Callable, Dict, List, Mapping, Optional, Sequence, Tuple

import perf
from rich.segment import Segment
from rich.style import Style
from textual import events, on, work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.color import Color
from textual.geometry import Size
from textual.message import Message
from textual.reactive import reactive
from textual.scroll_view import ScrollView
from textual.strip import Strip
from textual.widgets import DataTable, Footer, Header, Static, TabbedContent, TabPane
from textual.widgets.data_table import CellDoesNotExist, RowDoesNotExist

# Task states, SLEEPING and UNKNOWN aren't drawn.
STATE_UNKNOWN = -1
STATE_SLEEPING = 0
STATE_RUNNING = 1
STATE_WAITING = 2
STATE_BLOCKED = 3
NUM_STATES = 4
STATE_NAMES = {
    STATE_UNKNOWN: "unknown",
    STATE_SLEEPING: "sleeping",
    STATE_RUNNING: "running",
    STATE_WAITING: "runnable (waiting for a CPU)",
    STATE_BLOCKED: "blocked (uninterruptible)",
}

# I/O types, matching builtin-timechart.c.
IOTYPE_READ = 0
IOTYPE_WRITE = 1
IOTYPE_SYNC = 2
IOTYPE_TX = 3
IOTYPE_RX = 4
IOTYPE_POLL = 5
NUM_IOTYPES = 6
IOTYPE_NAMES = ["read", "write", "sync", "tx", "rx", "poll"]

IO_SYSCALLS = {
    "read": IOTYPE_READ, "pread64": IOTYPE_READ, "readv": IOTYPE_READ,
    "preadv": IOTYPE_READ,
    "write": IOTYPE_WRITE, "pwrite64": IOTYPE_WRITE, "writev": IOTYPE_WRITE,
    "pwritev": IOTYPE_WRITE,
    "sync": IOTYPE_SYNC, "sync_file_range": IOTYPE_SYNC, "fsync": IOTYPE_SYNC,
    "msync": IOTYPE_SYNC,
    "recvfrom": IOTYPE_RX, "recvmmsg": IOTYPE_RX, "recvmsg": IOTYPE_RX,
    "sendto": IOTYPE_TX, "sendmsg": IOTYPE_TX, "sendmmsg": IOTYPE_TX,
    "epoll_pwait": IOTYPE_POLL, "epoll_wait": IOTYPE_POLL, "poll": IOTYPE_POLL,
    "ppoll": IOTYPE_POLL, "pselect6": IOTYPE_POLL, "select": IOTYPE_POLL,
}

# Trace flags for interrupt context in the common_flags tracepoint field.
TRACE_FLAG_HARDIRQ = 0x08
TRACE_FLAG_SOFTIRQ = 0x10
# Value of state in power:cpu_idle when leaving idle.
PWR_EVENT_EXIT = 0xffffffff
# Width of the row label column.
LABEL_WIDTH = 28
# Characters for drawing fractional bars.
BARS = " ▁▂▃▄▅▆▇█"
NSEC_PER_SEC = 1_000_000_000


def fmt_duration(nsecs: float) -> str:
    """Format a duration in nanoseconds with an appropriate unit."""
    if nsecs >= NSEC_PER_SEC:
        return f"{nsecs / NSEC_PER_SEC:.3f}s"
    if nsecs >= 1_000_000:
        return f"{nsecs / 1_000_000:.3f}ms"
    if nsecs >= 1_000:
        return f"{nsecs / 1_000:.3f}us"
    return f"{nsecs:.0f}ns"


def escape(text: str) -> str:
    """Escape text, such as a task name, for use in textual markup.

    rich.markup.escape doesn't escape tags like "[1]", the name given to tasks
    with an unknown command, but textual fails to parse them.
    """
    return text.replace("[", "\\[")


def make_fixed_length_string(s: str, length: int, pad_char: str = ' ') -> str:
    """Truncate or right pad s so that it is length characters long."""
    return s[:length] if len(s) > length else s.ljust(length, pad_char)


def bar_char(frac: float) -> str:
    """A block character whose height represents frac, in the range [0, 1]."""
    if frac <= 0:
        return BARS[0]
    return BARS[max(1, min(8, round(frac * 8)))]


class SegmentList:
    """A time ordered list of non-overlapping [start, end) segments.

    Each segment has a small integer key, used for computing coverage, and
    arbitrary associated data.
    """
    def __init__(self) -> None:
        self.starts: List[int] = []
        self.ends: List[int] = []
        self.keys: List[int] = []
        self.data: List[Any] = []

    def __len__(self) -> int:
        return len(self.starts)

    def add(self, start: int, end: int, key: int, data: Any = None) -> None:
        """Append a segment, segments must be added in time order."""
        if end <= start:
            return
        if self.ends and start < self.ends[-1]:
            # Clip overlaps caused by inconsistent data.
            start = self.ends[-1]
            if end <= start:
                return
        self.starts.append(start)
        self.ends.append(end)
        self.keys.append(key)
        self.data.append(data)

    def find(self, time: float) -> int:
        """Index of the segment containing time or -1."""
        i = bisect.bisect_right(self.starts, time) - 1
        if i >= 0 and time < self.ends[i]:
            return i
        return -1

    def last_before(self, time: float) -> int:
        """Index of the last segment starting at or before time or -1."""
        return bisect.bisect_right(self.starts, time) - 1

    def next_change(self, time: float) -> Optional[int]:
        """The first segment start or end after time."""
        candidates = []
        i = bisect.bisect_right(self.starts, time)
        if i < len(self.starts):
            candidates.append(self.starts[i])
        i = bisect.bisect_right(self.ends, time)
        if i < len(self.ends):
            candidates.append(self.ends[i])
        return min(candidates) if candidates else None

    def prev_change(self, time: float) -> Optional[int]:
        """The last segment start or end before time."""
        candidates = []
        i = bisect.bisect_left(self.starts, time) - 1
        if i >= 0:
            candidates.append(self.starts[i])
        i = bisect.bisect_left(self.ends, time) - 1
        if i >= 0:
            candidates.append(self.ends[i])
        return max(candidates) if candidates else None

    def coverage(self, t0: float, dt: float, width: int, nkeys: int,
                 weight_fn=None) -> List[List[float]]:
        """Time covered by each key in each of width columns of size dt.

        If weight_fn is given then, rather than the time covered, the time
        covered multiplied by weight_fn(data) is accumulated.
        """
        cols = [[0.0] * nkeys for _ in range(width)]
        t1 = t0 + dt * width
        i = bisect.bisect_right(self.ends, t0)
        num = len(self.starts)
        while i < num and self.starts[i] < t1:
            start = max(self.starts[i], t0)
            end = min(self.ends[i], t1)
            key = self.keys[i]
            weight = weight_fn(self.data[i]) if weight_fn else 1.0
            x0 = min(int((start - t0) / dt), width - 1)
            x1 = min(int((end - t0) / dt), width - 1)
            if x0 == x1:
                cols[x0][key] += (end - start) * weight
            else:
                cols[x0][key] += (t0 + (x0 + 1) * dt - start) * weight
                for x in range(x0 + 1, x1):
                    cols[x][key] += dt * weight
                cols[x1][key] += (end - (t0 + x1 * dt)) * weight
            i += 1
        return cols


class Task:
    """Scheduling and I/O history of a single thread."""
    def __init__(self, tid: int, comm: str) -> None:
        self.tid = tid
        self.comm = comm
        self.comms: List[str] = [comm]
        self.state = STATE_UNKNOWN
        self.since = 0
        self.cpu = -1
        # Segments with a state key and the CPU as data.
        self.segs = SegmentList()
        # Segments with an I/O type key and (fd, ret) as data.
        self.io = SegmentList()
        self.io_pending: Optional[Tuple[int, int, int]] = None
        # Wakeups of this task as (time, waker tid) pairs.
        self.wakeups: List[Tuple[int, int]] = []
        self.totals = [0] * NUM_STATES
        self.switches = 0
        self.io_bytes = 0

    def name(self) -> str:
        """Name for the task used in labels."""
        return f"{self.comm} ({self.tid})"

    def set_comm(self, comm: Optional[str]) -> None:
        """Update the task's command name."""
        if not comm or comm == self.comm:
            return
        self.comm = comm
        if comm not in self.comms:
            self.comms.append(comm)

    def change_state(self, time: int, state: int, cpu: int = -1) -> None:
        """Record the current state as a segment and switch to a new state."""
        if self.state in (STATE_RUNNING, STATE_WAITING, STATE_BLOCKED) and time > self.since:
            self.segs.add(self.since, time, self.state, self.cpu)
            self.totals[self.state] += time - self.since
        self.state = state
        self.since = time
        if cpu >= 0:
            self.cpu = cpu

    def passes_filter(self, filters: Sequence[str]) -> bool:
        """Does the task match one of the process filters (pid or name)?"""
        if not filters:
            return True
        return any(f == str(self.tid) or f in self.comms for f in filters)


class Cpu:
    """Activity on a single CPU."""
    def __init__(self, cpu: int) -> None:
        self.cpu = cpu
        self.cur_tid = -1
        self.since = 0
        # Busy segments, key 1, with the running tid as data.
        self.run = SegmentList()
        # Idle state segments, key 1, with the C-state as data.
        self.cstate = SegmentList()
        self.cstate_cur: Optional[Tuple[int, int]] = None
        # Frequency segments, key 1, with the frequency in kHz as data.
        self.pstate = SegmentList()
        self.pstate_cur: Optional[Tuple[int, int]] = None


class LoadCancelled(Exception):
    """Raised from the sample callback to stop processing events early."""


class TimechartData:
    """Builds per-task and per-CPU timelines from perf events.

    The data may be displayed while it is loaded in another thread, lock must
    be held when modifying it or when reading it from another thread.
    """
    # Number of samples between checks for cancellation and progress.
    PROGRESS_INTERVAL = 1000
    # Minimum and maximum time between calls to the progress callback.
    PROGRESS_SECONDS = 1.0
    PROGRESS_MAX_SECONDS = 10.0
    # Updating the views costs more as more data is loaded, so the time between
    # progress calls grows as this fraction of the time spent loading. This
    # bounds the fraction of the load time spent updating the views.
    PROGRESS_FRACTION = 0.25

    def __init__(self) -> None:
        self.tasks: Dict[int, Task] = {}
        self.cpus: Dict[int, Cpu] = {}
        self.first_time = 0
        self.last_time = 0
        self.min_freq = 0
        self.max_freq = 0
        self.max_cstate = 0
        self.sched_events = 0
        self.power_events = 0
        self.io_events = 0
        self.nr_samples = 0
        self.unhandled: Dict[str, int] = defaultdict(int)
        self.session: Optional[perf.session] = None
        # Evsel name and event handler, keyed by sample ID.
        self._handlers: Dict[int, Tuple[str, Optional[Callable[[int, perf.sample_event],
                                                               None]]]] = {}
        self.lock = threading.Lock()
        # Set, possibly from another thread, to stop processing events.
        self.cancelled = False
        # Called periodically, see PROGRESS_FRACTION, while processing events.
        self.progress: Optional[Callable[[], None]] = None
        self.start_progress = monotonic()
        self.last_progress = self.start_progress

    def has_events(self) -> bool:
        """Were any events that can be displayed processed?"""
        return bool(self.sched_events or self.power_events or self.io_events)

    def task(self, tid: int, comm: Optional[str] = None) -> Task:
        """Find or create a task."""
        task = self.tasks.get(tid)
        if task is None:
            if not comm and self.session:
                try:
                    thread = self.session.find_thread(tid, tid)
                    comm = thread.comm() if thread else None
                except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError):
                    comm = None
            task = Task(tid, comm or f"[{tid}]")
            self.tasks[tid] = task
        else:
            task.set_comm(comm)
        return task

    def cpu(self, cpu: int) -> Cpu:
        """Find or create a CPU."""
        c = self.cpus.get(cpu)
        if c is None:
            c = Cpu(cpu)
            self.cpus[cpu] = c
        return c

    def sched_switch(self, time: int, cpu: int, prev_tid: int, prev_comm: Optional[str],
                     prev_state: int, next_tid: int, next_comm: Optional[str]) -> None:
        """Process a sched:sched_switch event."""
        self.sched_events += 1
        c = self.cpu(cpu)
        if c.cur_tid == -1 and prev_tid != 0:
            # Assume the task was running from the start of the trace.
            c.cur_tid = prev_tid
            c.since = self.first_time
        if c.cur_tid > 0:
            c.run.add(c.since, time, 1, c.cur_tid)
        c.cur_tid = next_tid
        c.since = time

        if prev_tid != 0:
            prev = self.task(prev_tid, prev_comm)
            if prev.state == STATE_UNKNOWN:
                prev.state = STATE_RUNNING
                prev.since = self.first_time
                prev.cpu = cpu
            # Ignore bits like TASK_REPORT_MAX used to report preemption.
            state = prev_state & 0xff
            if state == 0:
                new_state = STATE_WAITING
            elif state & 2:
                new_state = STATE_BLOCKED
            else:
                new_state = STATE_SLEEPING
            prev.change_state(time, new_state)
            prev.switches += 1

        if next_tid != 0:
            nxt = self.task(next_tid, next_comm)
            nxt.change_state(time, STATE_RUNNING, cpu)

    def sched_wakeup(self, time: int, wakee: int, comm: Optional[str], waker: int) -> None:
        """Process a sched:sched_wakeup or sched:sched_wakeup_new event."""
        self.sched_events += 1
        if wakee == 0:
            return
        task = self.task(wakee, comm)
        task.wakeups.append((time, waker))
        if task.state in (STATE_UNKNOWN, STATE_SLEEPING, STATE_BLOCKED):
            task.change_state(time, STATE_WAITING)

    def cstate_start(self, time: int, cpu: int, state: int) -> None:
        """Enter an idle state."""
        self.power_events += 1
        c = self.cpu(cpu)
        if c.cstate_cur:
            c.cstate.add(c.cstate_cur[0], time, 1, c.cstate_cur[1])
        c.cstate_cur = (time, state)
        self.max_cstate = max(self.max_cstate, state)

    def cstate_end(self, time: int, cpu: int) -> None:
        """Leave an idle state."""
        self.power_events += 1
        c = self.cpu(cpu)
        if c.cstate_cur:
            c.cstate.add(c.cstate_cur[0], time, 1, c.cstate_cur[1])
            c.cstate_cur = None

    def pstate_change(self, time: int, cpu: int, freq: int) -> None:
        """Change of CPU frequency, freq is in kHz."""
        if freq <= 0 or freq > 8000000:
            return
        self.power_events += 1
        c = self.cpu(cpu)
        if c.pstate_cur:
            c.pstate.add(c.pstate_cur[0], time, 1, c.pstate_cur[1])
        c.pstate_cur = (time, freq)
        self.max_freq = max(self.max_freq, freq)
        self.min_freq = freq if not self.min_freq else min(self.min_freq, freq)

    def io_enter(self, time: int, tid: int, iotype: int, fd: int) -> None:
        """Entry to an I/O syscall."""
        self.io_events += 1
        self.task(tid).io_pending = (time, iotype, fd)

    def io_exit(self, time: int, tid: int, iotype: int, ret: int) -> None:
        """Exit from an I/O syscall."""
        self.io_events += 1
        task = self.task(tid)
        pending = task.io_pending
        task.io_pending = None
        if not pending or pending[1] != iotype:
            return
        start = pending[0]
        task.io.add(start, max(time, start + 1), iotype, (pending[2], ret))
        if ret > 0 and iotype in (IOTYPE_READ, IOTYPE_WRITE, IOTYPE_TX, IOTYPE_RX):
            task.io_bytes += ret

    def _on_sched_switch(self, time: int, sample: perf.sample_event) -> None:
        self.sched_switch(time, sample.sample_cpu, sample.prev_pid,
                          getattr(sample, "prev_comm", None), sample.prev_state,
                          sample.next_pid, getattr(sample, "next_comm", None))

    def _on_sched_wakeup(self, time: int, sample: perf.sample_event) -> None:
        waker = getattr(sample, "common_pid", sample.sample_tid)
        flags = getattr(sample, "common_flags", 0)
        if flags & (TRACE_FLAG_HARDIRQ | TRACE_FLAG_SOFTIRQ):
            waker = -1
        self.sched_wakeup(time, sample.pid, getattr(sample, "comm", None), waker)

    def _cstate(self, time: int, cpu: int, state: int) -> None:
        if (state & 0xffffffff) == PWR_EVENT_EXIT:
            self.cstate_end(time, cpu)
        else:
            self.cstate_start(time, cpu, state)

    def _on_cpu_idle(self, time: int, sample: perf.sample_event) -> None:
        self._cstate(time, sample.cpu_id, sample.state)

    def _on_power_start(self, time: int, sample: perf.sample_event) -> None:
        self._cstate(time, sample.cpu_id, sample.value)

    def _on_power_end(self, time: int, sample: perf.sample_event) -> None:
        self.cstate_end(time, sample.sample_cpu)

    def _on_cpu_frequency(self, time: int, sample: perf.sample_event) -> None:
        self.pstate_change(time, sample.cpu_id, sample.state)

    def _on_power_frequency(self, time: int, sample: perf.sample_event) -> None:
        self.pstate_change(time, sample.cpu_id, sample.value)

    def _handler_for(self, name: str) -> Optional[Callable[[int, perf.sample_event], None]]:
        """Find the handler for events with the given evsel name."""
        handlers: Dict[str, Callable[[int, perf.sample_event], None]] = {
            "sched:sched_switch": self._on_sched_switch,
            "sched:sched_wakeup": self._on_sched_wakeup,
            "sched:sched_wakeup_new": self._on_sched_wakeup,
            "power:cpu_idle": self._on_cpu_idle,
            "power:power_start": self._on_power_start,
            "power:power_end": self._on_power_end,
            "power:cpu_frequency": self._on_cpu_frequency,
            "power:power_frequency": self._on_power_frequency,
        }
        if name in handlers:
            return handlers[name]
        if name.startswith("syscalls:sys_enter_") and name[19:] in IO_SYSCALLS:
            iotype = IO_SYSCALLS[name[19:]]
            return lambda time, sample: self.io_enter(time, sample.sample_tid, iotype,
                                                      getattr(sample, "fd", -1))
        if name.startswith("syscalls:sys_exit_") and name[18:] in IO_SYSCALLS:
            iotype = IO_SYSCALLS[name[18:]]
            return lambda time, sample: self.io_exit(time, sample.sample_tid, iotype,
                                                     sample.ret)
        return None

    def process_event(self, sample: perf.sample_event) -> None:
        """Callback from perf.session for each sample."""
        self.nr_samples += 1
        if self.nr_samples % self.PROGRESS_INTERVAL == 0:
            if self.cancelled:
                raise LoadCancelled()
            now = monotonic()
            interval = min(max(self.PROGRESS_SECONDS,
                               (now - self.start_progress) * self.PROGRESS_FRACTION),
                           self.PROGRESS_MAX_SECONDS)
            if self.progress and now - self.last_progress >= interval:
                with self.lock:
                    self.update_comms()
                # Must not hold the lock as the callback may read the data.
                self.progress()
                # Time from when the callback, that may block, returns.
                self.last_progress = monotonic()
        with self.lock:
            self._process_event(sample)

    def _process_event(self, sample: perf.sample_event) -> None:
        """Update the data from a sample, the lock must be held."""
        time = sample.sample_time
        if not self.first_time or time < self.first_time:
            self.first_time = time
        self.last_time = max(self.last_time, time)

        # Computing the evsel name and matching it is relatively expensive,
        # so cache the result by sample ID. Each ID belongs to a single evsel.
        sample_id = sample.sample_id
        cached = self._handlers.get(sample_id)
        if cached is None:
            name = str(sample.evsel)
            if name.startswith("evsel(") and name.endswith(")"):
                name = name[6:-1]
            cached = (name, self._handler_for(name))
            self._handlers[sample_id] = cached
        name, handler = cached
        if handler is None:
            self.unhandled[name] += 1
            return
        try:
            handler(time, sample)
        except AttributeError:
            self.unhandled[name] += 1

    def update_comms(self) -> None:
        """Refresh task command names from the session, the lock must be held."""
        if not self.session:
            return
        for tid, task in self.tasks.items():
            try:
                thread = self.session.find_thread(tid, tid)
                if thread:
                    task.set_comm(thread.comm())
            except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError):
                pass

    def finish(self) -> None:
        """Close open segments at the end of the trace."""
        with self.lock:
            self.update_comms()
            end = self.last_time
            for task in self.tasks.values():
                task.change_state(end, STATE_UNKNOWN)
            for c in self.cpus.values():
                if c.cur_tid > 0:
                    c.run.add(c.since, end, 1, c.cur_tid)
                if c.cstate_cur:
                    c.cstate.add(c.cstate_cur[0], end, 1, c.cstate_cur[1])
                    c.cstate_cur = None
                if c.pstate_cur:
                    c.pstate.add(c.pstate_cur[0], end, 1, c.pstate_cur[1])
                    c.pstate_cur = None

    def fmt_time(self, time: float) -> str:
        """Format an absolute timestamp relative to the trace start."""
        return f"{(time - self.first_time) / NSEC_PER_SEC:.6f}s"

    def task_name(self, tid: int) -> str:
        """Name of a task, or a description for special tids."""
        if tid == 0:
            return "idle"
        if tid < 0:
            return "interrupt"
        task = self.tasks.get(tid)
        return task.name() if task else f"[{tid}]"

    def sched_tasks(self, filters: Sequence[str]) -> List[Task]:
        """Tasks with scheduling history passing the filters, sorted by tid."""
        return sorted((t for t in self.tasks.values()
                       if len(t.segs) and t.passes_filter(filters)),
                      key=lambda t: t.tid)

    def io_tasks(self, filters: Sequence[str]) -> List[Task]:
        """Tasks with I/O passing the filters, most I/O first."""
        return sorted((t for t in self.tasks.values()
                       if len(t.io) and t.passes_filter(filters)),
                      key=lambda t: -len(t.io))

    def dump(self, power_only: bool, tasks_only: bool, filters: Sequence[str]) -> None:
        """Print a plain text summary, for use without a terminal UI."""
        duration = self.last_time - self.first_time
        span = max(duration, 1)
        tasks = self.sched_tasks(filters)
        io_tasks = self.io_tasks(filters)
        print(f"Duration: {fmt_duration(duration)}, CPUs: {len(self.cpus)}, "
              f"tasks: {len(tasks) or len(io_tasks)}, sched events: {self.sched_events}, "
              f"power events: {self.power_events}, I/O events: {self.io_events}")
        if not tasks_only:
            for c in sorted(self.cpus.values(), key=lambda c: c.cpu):
                busy = sum(e - s for s, e in zip(c.run.starts, c.run.ends))
                print(f"CPU {c.cpu}: busy {busy * 100 / span:.1f}%, "
                      f"{len(c.run)} runs, {len(c.cstate)} idle periods, "
                      f"{len(c.pstate)} frequency periods")
        if power_only:
            return
        print(f"{'Task':<24} {'TID':>8} {'Running':>12} {'Waiting':>12} {'Blocked':>12} "
              f"{'Switches':>9} {'Wakeups':>8}")
        for task in sorted(tasks, key=lambda t: -t.totals[STATE_RUNNING]):
            print(f"{task.comm[:24]:<24} {task.tid:>8} "
                  f"{fmt_duration(task.totals[STATE_RUNNING]):>12} "
                  f"{fmt_duration(task.totals[STATE_WAITING]):>12} "
                  f"{fmt_duration(task.totals[STATE_BLOCKED]):>12} "
                  f"{task.switches:>9} {len(task.wakeups):>8}")
        for task in io_tasks:
            print(f"I/O {task.name()}: {len(task.io)} syscalls, {task.io_bytes} bytes")


Cell = Tuple[str, Style]


class ThemeColors:
    """Colors and Rich styles derived from the active Textual theme."""
    def __init__(self, theme_variables: Mapping[str, str]) -> None:
        self.running = theme_variables["primary"]
        self.waiting = theme_variables["error"]
        self.blocked = theme_variables["warning"]
        self.idle_light = theme_variables["secondary-lighten-2"]
        self.idle_dark = theme_variables["secondary-darken-2"]
        self.freq_low = theme_variables["success"]
        self.freq_high = theme_variables["error"]
        self.io = [
            theme_variables["success"],
            theme_variables["error"],
            theme_variables["warning"],
            theme_variables["accent"],
            theme_variables["primary"],
            theme_variables["secondary-lighten-2"],
        ]
        run_color = Color.parse(self.running).rich_color
        wait_color = Color.parse(self.waiting).rich_color
        block_color = Color.parse(self.blocked).rich_color
        self.run_style = Style(color=run_color)
        self.wait_style = Style(color=run_color, bgcolor=wait_color)
        self.block_style = Style(color=run_color, bgcolor=block_color)
        low = Color.parse(self.freq_low)
        high = Color.parse(self.freq_high)
        self.freq_styles = [Style(color=low.blend(high, x / 8).rich_color) for x in range(9)]
        light = Color.parse(self.idle_light)
        dark = Color.parse(self.idle_dark)
        self.idle_styles = [Style(color=light.blend(dark, x / 8).rich_color) for x in range(9)]
        self.io_styles = [Style(color=Color.parse(c).rich_color) for c in self.io]
        self.io_err_styles = [Style(color=Color.parse(c).rich_color, underline=True)
                              for c in self.io]
        accent = Color.parse(theme_variables["accent"]).rich_color
        accent_muted = Color.parse(theme_variables["accent-muted"]).rich_color
        self.selected_style = Style(color=accent, bgcolor=accent_muted, bold=True)


class Row(ABC):
    """A row within a timeline view."""
    def __init__(self, label: str) -> None:
        self._label = label

    def label(self) -> str:
        """Label shown to the left of the row."""
        return self._label

    @abstractmethod
    def segments(self) -> SegmentList:
        """Segments used for navigating between changes."""

    @abstractmethod
    def cells(self, t0: float, dt: float, width: int, colors: ThemeColors) -> List[Cell]:
        """Cells for the columns starting at time t0 and dt wide."""

    @abstractmethod
    def describe(self, time: float, t0: float, t1: float) -> str:
        """Rich markup describing the row at time within the window [t0, t1)."""


class TaskRow(Row):
    """Row showing the running, waiting and blocked states of a task.

    The height of the bar shows the fraction of the time running, the
    background shows waiting for a CPU or being blocked.
    """
    def __init__(self, data: TimechartData, task: Task) -> None:
        super().__init__(task.name())
        self.data = data
        self.task = task

    def label(self) -> str:
        return self.task.name()

    def segments(self) -> SegmentList:
        return self.task.segs

    def cells(self, t0: float, dt: float, width: int, colors: ThemeColors) -> List[Cell]:
        result = []
        for col in self.task.segs.coverage(t0, dt, width, NUM_STATES):
            run = col[STATE_RUNNING] / dt
            wait = col[STATE_WAITING]
            block = col[STATE_BLOCKED]
            if wait > 0 and wait >= block:
                style = colors.wait_style
            elif block > 0:
                style = colors.block_style
            else:
                style = colors.run_style
            result.append((bar_char(run), style))
        return result

    def describe(self, time: float, t0: float, t1: float) -> str:
        task = self.task
        i = task.segs.find(time)
        if i >= 0:
            state = task.segs.keys[i]
            desc = STATE_NAMES[state]
            if state == STATE_RUNNING:
                desc += f" on CPU {task.segs.data[i]}"
            elif state == STATE_WAITING and task.segs.data[i] >= 0:
                desc += f", last ran on CPU {task.segs.data[i]}"
            start = task.segs.starts[i]
            end = task.segs.ends[i]
            desc += (f" from {self.data.fmt_time(start)} for "
                     f"{fmt_duration(end - start)}")
        else:
            desc = "sleeping or not traced"
        window = task.segs.coverage(t0, max(t1 - t0, 1), 1, NUM_STATES)[0]
        span = max(t1 - t0, 1)
        lines = [
            f"[b]{escape(task.name())}[/b]: {desc}",
            f"In view: running {window[STATE_RUNNING] * 100 / span:.1f}%, "
            f"waiting {window[STATE_WAITING] * 100 / span:.1f}%, "
            f"blocked {window[STATE_BLOCKED] * 100 / span:.1f}%",
            f"Total: running {fmt_duration(task.totals[STATE_RUNNING])}, "
            f"waiting {fmt_duration(task.totals[STATE_WAITING])}, "
            f"blocked {fmt_duration(task.totals[STATE_BLOCKED])}, "
            f"{task.switches} switches, {len(task.wakeups)} wakeups",
        ]
        idx = bisect.bisect_right(task.wakeups, (time, sys.maxsize)) - 1
        if idx >= 0:
            wake_time, waker = task.wakeups[idx]
            lines.append(f"Last woken at {self.data.fmt_time(wake_time)} by "
                         f"{escape(self.data.task_name(waker))} (press 'w' to go to waker)")
        if len(task.comms) > 1:
            lines.append(f"Names: {escape(', '.join(task.comms))}")
        return "\n".join(lines)


class CpuRow(Row):
    """Row showing how busy a CPU is, colored by frequency if known."""
    def __init__(self, data: TimechartData, cpu: Cpu) -> None:
        super().__init__(f"CPU {cpu.cpu}")
        self.data = data
        self.cpu = cpu

    def segments(self) -> SegmentList:
        return self.cpu.run

    def cells(self, t0: float, dt: float, width: int, colors: ThemeColors) -> List[Cell]:
        busy = self.cpu.run.coverage(t0, dt, width, 2)
        freqs: Optional[List[List[float]]] = None
        weighted: Optional[List[List[float]]] = None
        if len(self.cpu.pstate) and self.data.max_freq > self.data.min_freq:
            freqs = self.cpu.pstate.coverage(t0, dt, width, 2)
            weighted = self.cpu.pstate.coverage(t0, dt, width, 2, weight_fn=float)
        result = []
        for x in range(width):
            style = colors.run_style
            if freqs and weighted and freqs[x][1] > 0:
                freq = weighted[x][1] / freqs[x][1]
                frac = (freq - self.data.min_freq) / (self.data.max_freq - self.data.min_freq)
                style = colors.freq_styles[max(0, min(8, round(frac * 8)))]
            result.append((bar_char(busy[x][1] / dt), style))
        return result

    def describe(self, time: float, t0: float, t1: float) -> str:
        c = self.cpu
        i = c.run.find(time)
        if i >= 0:
            desc = (f"running {escape(self.data.task_name(c.run.data[i]))} from "
                    f"{self.data.fmt_time(c.run.starts[i])} for "
                    f"{fmt_duration(c.run.ends[i] - c.run.starts[i])}")
        else:
            desc = "idle"
        i = c.cstate.find(time)
        if i >= 0:
            desc += f", C-state C{c.cstate.data[i]}"
        i = c.pstate.find(time)
        if i >= 0:
            desc += f", {c.pstate.data[i] / 1000:.0f} MHz"
        span = max(t1 - t0, 1)
        window = c.run.coverage(t0, span, 1, 2)[0]
        return "\n".join([
            f"[b]CPU {c.cpu}[/b]: {desc}",
            f"In view: busy {window[1] * 100 / span:.1f}%",
            "Bar height is the fraction of time busy" +
            (", color is the frequency from low to high"
             if len(c.pstate) else ""),
        ])


class CStateRow(Row):
    """Row showing the idle states of a CPU."""
    def __init__(self, data: TimechartData, cpu: Cpu) -> None:
        super().__init__(f"  CPU {cpu.cpu} idle")
        self.data = data
        self.cpu = cpu

    def segments(self) -> SegmentList:
        return self.cpu.cstate

    def cells(self, t0: float, dt: float, width: int, colors: ThemeColors) -> List[Cell]:
        cov = self.cpu.cstate.coverage(t0, dt, width, 2)
        depth = self.cpu.cstate.coverage(t0, dt, width, 2, weight_fn=float)
        max_cstate = max(self.data.max_cstate, 1)
        result = []
        for x in range(width):
            frac = cov[x][1] / dt
            style = colors.idle_styles[0]
            if cov[x][1] > 0:
                avg = depth[x][1] / cov[x][1]
                style = colors.idle_styles[max(0, min(8, round(avg * 8 / max_cstate)))]
            result.append((bar_char(frac), style))
        return result

    def describe(self, time: float, t0: float, t1: float) -> str:
        c = self.cpu
        i = c.cstate.find(time)
        if i >= 0:
            desc = (f"C{c.cstate.data[i]} from {self.data.fmt_time(c.cstate.starts[i])} for "
                    f"{fmt_duration(c.cstate.ends[i] - c.cstate.starts[i])}")
        else:
            desc = "not idle"
        span = max(t1 - t0, 1)
        window = c.cstate.coverage(t0, span, 1, 2)[0]
        return "\n".join([
            f"[b]CPU {c.cpu} idle state[/b]: {desc}",
            f"In view: idle {window[1] * 100 / span:.1f}%",
            "Bar height is the fraction of time idle, darker colors are deeper C-states",
        ])


class FreqRow(Row):
    """Row showing the frequency of a CPU."""
    def __init__(self, data: TimechartData, cpu: Cpu) -> None:
        super().__init__(f"  CPU {cpu.cpu} freq")
        self.data = data
        self.cpu = cpu

    def segments(self) -> SegmentList:
        return self.cpu.pstate

    def cells(self, t0: float, dt: float, width: int, colors: ThemeColors) -> List[Cell]:
        cov = self.cpu.pstate.coverage(t0, dt, width, 2)
        weighted = self.cpu.pstate.coverage(t0, dt, width, 2, weight_fn=float)
        max_freq = max(self.data.max_freq, 1)
        min_freq = self.data.min_freq
        result = []
        for x in range(width):
            if cov[x][1] <= 0:
                result.append((" ", colors.freq_styles[0]))
                continue
            freq = weighted[x][1] / cov[x][1]
            frac = (freq - min_freq) / (max_freq - min_freq) if max_freq > min_freq else 1.0
            result.append((bar_char(max(freq / max_freq, 1 / 8)),
                           colors.freq_styles[max(0, min(8, round(frac * 8)))]))
        return result

    def describe(self, time: float, t0: float, t1: float) -> str:
        c = self.cpu
        i = c.pstate.find(time)
        if i >= 0:
            desc = (f"{c.pstate.data[i] / 1000:.0f} MHz from "
                    f"{self.data.fmt_time(c.pstate.starts[i])} for "
                    f"{fmt_duration(c.pstate.ends[i] - c.pstate.starts[i])}")
        else:
            desc = "unknown"
        return "\n".join([
            f"[b]CPU {c.cpu} frequency[/b]: {desc}",
            f"Range: {self.data.min_freq / 1000:.0f} - {self.data.max_freq / 1000:.0f} MHz",
            "Bar height is the frequency relative to the maximum",
        ])


class IoRow(Row):
    """Row showing the I/O syscalls of a task."""
    def __init__(self, data: TimechartData, task: Task) -> None:
        super().__init__(task.name())
        self.data = data
        self.task = task

    def label(self) -> str:
        return self.task.name()

    def segments(self) -> SegmentList:
        return self.task.io

    def cells(self, t0: float, dt: float, width: int, colors: ThemeColors) -> List[Cell]:
        errs = self.task.io.coverage(t0, dt, width, NUM_IOTYPES,
                                     weight_fn=lambda d: 1.0 if d[1] < 0 else 0.0)
        result = []
        for x, col in enumerate(self.task.io.coverage(t0, dt, width, NUM_IOTYPES)):
            total = sum(col)
            if total <= 0:
                result.append((" ", colors.io_styles[0]))
                continue
            iotype = col.index(max(col))
            styles = colors.io_err_styles if sum(errs[x]) > 0 else colors.io_styles
            result.append((bar_char(max(total / dt, 1 / 8)), styles[iotype]))
        return result

    def describe(self, time: float, t0: float, t1: float) -> str:
        task = self.task
        i = task.io.find(time)
        if i < 0:
            i = task.io.last_before(time)
            prefix = "last I/O"
        else:
            prefix = "in"
        if i >= 0:
            fd, ret = task.io.data[i]
            result = f"returned {ret}" if ret >= 0 else f"failed with error {-ret}"
            desc = (f"{prefix} {IOTYPE_NAMES[task.io.keys[i]]} fd={fd} at "
                    f"{self.data.fmt_time(task.io.starts[i])} for "
                    f"{fmt_duration(task.io.ends[i] - task.io.starts[i])}, {result}")
        else:
            desc = "no I/O"
        span = max(t1 - t0, 1)
        window = task.io.coverage(t0, span, 1, NUM_IOTYPES)[0]
        in_view = ", ".join(f"{IOTYPE_NAMES[t]} {window[t] * 100 / span:.1f}%"
                            for t in range(NUM_IOTYPES) if window[t] > 0)
        return "\n".join([
            f"[b]{escape(task.name())}[/b]: {desc}",
            f"In view: {in_view or 'no I/O'}",
            f"Total: {len(task.io)} I/O syscalls, {task.io_bytes} bytes",
        ])


@dataclass(frozen=True)
class TimeWindow:
    """The visible time range and the cursor, shared between views.

    Immutable so that it can be a reactive value, the methods return new windows.
    """
    first: int
    last: int
    start: float
    end: float
    cursor: float

    @staticmethod
    def whole(first: int, last: int) -> "TimeWindow":
        """A window showing the whole trace with the cursor at the start."""
        last = max(last, first + 1)
        return TimeWindow(first, last, first, last, first)

    def span(self) -> float:
        """Length of the visible time range."""
        return self.end - self.start

    def reset(self) -> "TimeWindow":
        """Show the whole trace."""
        return replace(self, start=self.first, end=self.last)

    def with_range(self, start: float, span: float) -> "TimeWindow":
        """Set the visible range clamped to the trace."""
        span = min(max(span, 100.0), self.last - self.first)
        start = min(max(start, self.first), self.last - span)
        return replace(self, start=start, end=start + span)

    def zoom(self, factor: float) -> "TimeWindow":
        """Zoom by factor keeping the cursor at the same position."""
        cursor = self.cursor
        if not self.start <= cursor <= self.end:
            cursor = (self.start + self.end) / 2
        rel = (cursor - self.start) / self.span()
        span = self.span() * factor
        return replace(self, cursor=cursor).with_range(cursor - rel * span, span)

    def pan(self, frac: float) -> "TimeWindow":
        """Pan the view by the fraction of the visible span."""
        delta = self.span() * frac
        win = self.with_range(self.start + delta, self.span())
        return replace(win, cursor=min(max(self.cursor + delta, win.start), win.end))

    def with_cursor(self, time: float) -> "TimeWindow":
        """Move the cursor, scrolling to keep it visible."""
        win = replace(self, cursor=min(max(time, self.first), self.last))
        if win.cursor < win.start:
            return win.with_range(win.cursor, win.span())
        if win.cursor >= win.end:
            return win.with_range(win.cursor - win.span() * 0.9, win.span())
        return win

    def extend(self, first: int, last: int) -> "TimeWindow":
        """Change the bounds of the trace as more of it is loaded.

        If the whole trace was visible then it still is, otherwise the visible
        range is kept.
        """
        last = max(last, first + 1)
        whole = self.start == self.first and self.end == self.last
        win = replace(self, first=first, last=last, cursor=min(max(self.cursor, first), last))
        return win.reset() if whole else win.with_range(self.start, self.span())


class TimelineView(ScrollView):
    """A scrollable view of rows against a time axis.

    Line 0 is a time ruler that stays at the top, the other lines are rows.
    """
    BINDINGS = [
        Binding("up,k", "row_up", "Up", show=False),
        Binding("down,j", "row_down", "Down", show=False),
        Binding("left,h", "cursor_left", "Cursor ←", key_display="←"),
        Binding("right,l", "cursor_right", "Cursor →", key_display="→"),
        Binding("shift+left,H", "pan_left", "Pan left", show=False),
        Binding("shift+right,L", "pan_right", "Pan right", show=False),
        Binding("plus,equals_sign", "zoom_in", "Zoom in", key_display="+"),
        Binding("minus", "zoom_out", "Zoom out", key_display="-"),
        Binding("0,escape", "zoom_reset", "Reset zoom", key_display="0"),
        Binding("n", "next_change", "Next change"),
        Binding("p", "prev_change", "Prev change"),
        Binding("pageup", "page_up", "Page up", show=False),
        Binding("pagedown", "page_down", "Page down", show=False),
    ]

    DEFAULT_CSS = """
    TimelineView {
        width: 100%;
        height: 1fr;
    }
    """

    class SelectionChanged(Message):
        """Posted when the selected row changes."""

    # Bound to TimechartApp.window, changing it repaints the view.
    window: reactive[TimeWindow] = reactive(TimeWindow.whole(0, 1))
    # Index of the selected row.
    selected: reactive[int] = reactive(0)

    def __init__(self, data: TimechartData, rows: List[Row], *pos_args, **kwargs) -> None:
        super().__init__(*pos_args, **kwargs)
        self.can_focus = True
        self.data = data
        self.rows = rows
        self.cache: Dict[int, List[Cell]] = {}
        self.cache_key: Tuple[float, float, int, Optional[ThemeColors]] = (0.0, 0.0, 0, None)
        self.label_style = Style()
        self.ruler_style = Style(dim=True)
        self.cursor_style = Style(reverse=True)

    def selected_row(self) -> Optional[Row]:
        """The selected row, if any."""
        if 0 <= self.selected < len(self.rows):
            return self.rows[self.selected]
        return None

    def timeline_width(self) -> int:
        """Number of columns used for the timeline."""
        return max(self.scrollable_content_region.width - LABEL_WIDTH, 1)

    def update_size(self) -> None:
        """Update the virtual size after rows change or the widget resizes."""
        self.virtual_size = Size(self.scrollable_content_region.width, len(self.rows) + 1)

    def on_mount(self) -> None:
        """Size the view when mounted."""
        self.update_size()

    def on_resize(self) -> None:
        """Size the view when resized."""
        self.update_size()
        self.refresh()

    def set_rows(self, rows: List[Row]) -> None:
        """Replace the rows, after sorting or as more data is loaded.

        The selected row is kept, if it moves watch_selected scrolls to it.
        """
        selected = self.selected_row()
        self.rows = rows
        self.cache.clear()
        self.update_size()
        self.selected = rows.index(selected) if selected in rows else 0
        self.refresh()

    def validate_selected(self, idx: int) -> int:
        """Keep the selection within the rows."""
        return max(0, min(idx, len(self.rows) - 1))

    def watch_selected(self) -> None:
        """Keep the selection visible and tell the app."""
        self.scroll_to_selected()
        self.post_message(self.SelectionChanged())

    def scroll_to_selected(self) -> None:
        """Scroll so that the selected row is visible below the ruler."""
        _, scroll_y = self.scroll_offset
        visible = max(self.scrollable_content_region.height - 1, 1)
        if self.selected < scroll_y:
            self.scroll_to(y=self.selected, animate=False)
        elif self.selected >= scroll_y + visible:
            self.scroll_to(y=self.selected - visible + 1, animate=False)

    def set_window(self, window: TimeWindow) -> None:
        """Change the window shared by all views, owned by the app."""
        app = self.app
        if isinstance(app, TimechartApp):
            app.window = window

    def ruler(self, width: int) -> Strip:
        """The time axis, labeled in seconds relative to the trace start."""
        win = self.window
        dt = win.span() / width
        tick = 14
        decimals = max(0, min(9, 1 - math.floor(math.log10(max(dt * tick, 1) / NSEC_PER_SEC))))
        chars = [" "] * width
        x = 0
        while x < width:
            label = f"|{(win.start + x * dt - self.data.first_time) / NSEC_PER_SEC:.{decimals}f}"
            if x + len(label) > width:
                break
            chars[x:x + len(label)] = list(label)
            x += max(tick, len(label) + 2)
        segments = [Segment(make_fixed_length_string("Time (s)", LABEL_WIDTH), self.ruler_style)]
        cursor_x = self.cursor_column(width)
        if 0 <= cursor_x < width:
            segments.append(Segment("".join(chars[:cursor_x]), self.ruler_style))
            segments.append(Segment("▼"))
            segments.append(Segment("".join(chars[cursor_x + 1:]), self.ruler_style))
        else:
            segments.append(Segment("".join(chars), self.ruler_style))
        return Strip(segments)

    def cursor_column(self, width: int) -> int:
        """Column of the cursor or -1 if not visible."""
        win = self.window
        if not win.start <= win.cursor < win.end:
            return -1
        return min(int((win.cursor - win.start) * width / win.span()), width - 1)

    def render_line(self, y: int) -> Strip:
        """Render the ruler or a row."""
        width = self.timeline_width()
        if y == 0:
            return self.ruler(width)
        _, scroll_y = self.scroll_offset
        idx = scroll_y + y - 1
        if idx >= len(self.rows):
            return Strip.blank(self.scrollable_content_region.width)

        colors = self.app.theme_colors if isinstance(self.app, TimechartApp) else \
            ThemeColors(self.app.theme_variables)
        win = self.window
        key = (win.start, win.end, width, colors)
        if key != self.cache_key:
            self.cache.clear()
            self.cache_key = key
        row = self.rows[idx]
        with self.data.lock:
            cells = self.cache.get(idx)
            if cells is None:
                cells = row.cells(win.start, win.span() / width, width, colors)
                self.cache[idx] = cells
            label = row.label()

        label_style = colors.selected_style if idx == self.selected else self.label_style
        segments = [Segment(make_fixed_length_string(label, LABEL_WIDTH - 1) + " ",
                            label_style)]
        cursor_x = self.cursor_column(width)
        # Merge runs of cells with the same style into one segment.
        text = ""
        style: Optional[Style] = None
        for x, (char, cell_style) in enumerate(cells):
            if x == cursor_x:
                cell_style = cell_style + self.cursor_style
            if cell_style is not style and text:
                segments.append(Segment(text, style))
                text = ""
            text += char
            style = cell_style
        if text:
            segments.append(Segment(text, style))
        return Strip(segments)

    def column_time(self, x: int) -> float:
        """Time at the center of the column containing screen offset x."""
        width = self.timeline_width()
        return self.window.start + (x - LABEL_WIDTH + 0.5) * self.window.span() / width

    def on_click(self, click: events.Click) -> None:
        """Select the clicked row and move the cursor to the clicked time."""
        if click.x >= LABEL_WIDTH:
            self.set_window(self.window.with_cursor(self.column_time(click.x)))
        if click.y > 0:
            _, scroll_y = self.scroll_offset
            self.selected = scroll_y + click.y - 1

    def action_row_up(self) -> None:
        """Select the previous row."""
        self.selected -= 1

    def action_row_down(self) -> None:
        """Select the next row."""
        self.selected += 1

    def action_page_up(self) -> None:
        """Select a row a page up."""
        self.selected -= max(self.scrollable_content_region.height - 2, 1)

    def action_page_down(self) -> None:
        """Select a row a page down."""
        self.selected += max(self.scrollable_content_region.height - 2, 1)

    def action_cursor_left(self) -> None:
        """Move the cursor one column left."""
        win = self.window
        self.set_window(win.with_cursor(win.cursor - win.span() / self.timeline_width()))

    def action_cursor_right(self) -> None:
        """Move the cursor one column right."""
        win = self.window
        self.set_window(win.with_cursor(win.cursor + win.span() / self.timeline_width()))

    def action_pan_left(self) -> None:
        """Pan a quarter of the view left."""
        self.set_window(self.window.pan(-0.25))

    def action_pan_right(self) -> None:
        """Pan a quarter of the view right."""
        self.set_window(self.window.pan(0.25))

    def action_zoom_in(self) -> None:
        """Halve the visible time range around the cursor."""
        self.set_window(self.window.zoom(0.5))

    def action_zoom_out(self) -> None:
        """Double the visible time range around the cursor."""
        self.set_window(self.window.zoom(2))

    def action_zoom_reset(self) -> None:
        """Show the whole trace."""
        self.set_window(self.window.reset())

    def action_next_change(self) -> None:
        """Move the cursor to the next change in the selected row."""
        row = self.selected_row()
        if row:
            # Skip changes within the cursor's column.
            win = self.window
            with self.data.lock:
                time = row.segments().next_change(
                    win.cursor + win.span() / self.timeline_width() / 2)
            if time is not None:
                self.set_window(win.with_cursor(time))

    def action_prev_change(self) -> None:
        """Move the cursor to the previous change in the selected row."""
        row = self.selected_row()
        if row:
            win = self.window
            with self.data.lock:
                time = row.segments().prev_change(
                    win.cursor - win.span() / self.timeline_width() / 2)
            if time is not None:
                self.set_window(win.with_cursor(time))


class TimechartApp(App):
    """A Textual application to display a timechart."""
    TITLE = "perf timechart"

    BINDINGS = [
        Binding("s", "sort", "Sort tasks", tooltip="Cycle task sort order"),
        Binding("w", "goto_waker", "Go to waker",
                tooltip="Select the task that last woke the selected task"),
        Binding(key="^q", action="quit", description="Quit", tooltip="Quit the app"),
    ]

    CSS = """
    TabbedContent, TabbedContent > ContentSwitcher, TabPane {
        height: 1fr;
    }
    TabPane {
        padding: 0;
    }
    .legend {
        height: 1;
        padding: 0 1;
    }
    #details {
        height: 8;
        border: round $primary;
        padding: 0 1;
    }
    """

    SORT_ORDERS = ["run time", "tid", "name", "first run"]

    # The visible time range and cursor, bound to each TimelineView's window.
    window: reactive[TimeWindow] = reactive(TimeWindow.whole(0, 1), init=False)
    # Index into SORT_ORDERS for the tasks view.
    sort_order: reactive[int] = reactive(0, init=False)

    def __init__(self, input_name: str, power_only: bool, tasks_only: bool,
                 filters: Sequence[str], data: Optional[TimechartData] = None) -> None:
        """Create the app, if data isn't given it is loaded from input_name.

        While the data is loading the views show what has been read so far.
        """
        super().__init__()
        self.input_name = input_name
        self.power_only = power_only
        self.tasks_only = tasks_only
        self.filters = filters
        # The data being displayed, possibly still being loaded.
        self.data = data if data else TimechartData()
        self.loaded = data is not None
        # The data being loaded in a background thread.
        self.loading: Optional[TimechartData] = None
        self.tasks: List[Task] = []
        self.io_tasks: List[Task] = []
        # Rows keyed by type and CPU or tid. They are reused as the data loads
        # so that the views can keep the selected row.
        self.row_cache: Dict[Tuple[str, int], Row] = {}
        self.io_tab_shown = True
        # Does the summary table need updating before it is next shown?
        self.summary_stale = True
        self.theme_colors = ThemeColors(self.theme_variables)

    def cached_row(self, cls: Callable[[TimechartData, Any], Row], num: int, obj: Any) -> Row:
        """Find or create the row of type cls for the CPU or task obj numbered num."""
        key = (getattr(cls, "__name__", ""), num)
        row = self.row_cache.get(key)
        if row is None:
            row = cls(self.data, obj)
            self.row_cache[key] = row
        return row

    def cpu_rows(self) -> List[Row]:
        """Rows for the CPUs tab, the data's lock must be held."""
        rows: List[Row] = []
        for cpu in sorted(self.data.cpus.values(), key=lambda c: c.cpu):
            if len(cpu.run) or len(cpu.cstate) or len(cpu.pstate):
                rows.append(self.cached_row(CpuRow, cpu.cpu, cpu))
            if len(cpu.cstate):
                rows.append(self.cached_row(CStateRow, cpu.cpu, cpu))
            if len(cpu.pstate):
                rows.append(self.cached_row(FreqRow, cpu.cpu, cpu))
        return rows

    def task_rows(self) -> List[Row]:
        """Rows for the tasks tab in the current sort order, the data's lock must be held."""
        order = self.SORT_ORDERS[self.sort_order]
        if order == "run time":
            tasks = sorted(self.tasks, key=lambda t: -t.totals[STATE_RUNNING])
        elif order == "name":
            tasks = sorted(self.tasks, key=lambda t: (t.comm, t.tid))
        elif order == "first run":
            tasks = sorted(self.tasks, key=lambda t: t.segs.starts[0])
        else:
            tasks = self.tasks
        return [self.cached_row(TaskRow, t.tid, t) for t in tasks]

    def compose(self) -> ComposeResult:
        """Composes the user interface of the application."""
        yield Header()
        with TabbedContent():
            if not self.tasks_only:
                with TabPane("CPUs", id="cpus"):
                    yield Static(id="cpus_legend", classes="legend")
                    yield TimelineView(self.data, [],
                                       id="cpus_view").data_bind(TimechartApp.window)
            if not self.power_only:
                with TabPane("Tasks", id="tasks"):
                    yield Static(id="tasks_legend", classes="legend")
                    yield TimelineView(self.data, [],
                                       id="tasks_view").data_bind(TimechartApp.window)
                # Hidden until there are tasks doing I/O.
                with TabPane("I/O", id="io"):
                    yield Static(id="io_legend", classes="legend")
                    yield TimelineView(self.data, [],
                                       id="io_view").data_bind(TimechartApp.window)
                with TabPane("Summary", id="summary"):
                    yield DataTable(id="summary_table", cursor_type="row")
        yield Static(id="details")
        yield Footer()

    def on_mount(self) -> None:
        """Show the data, loading it in the background if it wasn't given."""
        self.theme_changed_signal.subscribe(self, self.on_theme_changed)
        self.update_legends()
        self.update_views()
        view = self.active_view()
        if view:
            view.focus()
        if self.loaded:
            self.finish_loading()
        else:
            self.sub_title = f"Loading {self.input_name}"
            self.loading = self.data
            self.load_events()

    def on_theme_changed(self, _theme: Any) -> None:
        """Update colors, legends and timeline views when the theme changes."""
        self.theme_colors = ThemeColors(self.theme_variables)
        self.update_legends()
        for view in self.query(TimelineView):
            view.refresh()

    def update_legends(self) -> None:
        """Update the legend strings using the active theme's colors."""
        c = self.theme_colors
        with self.data.lock:
            cpus_legend = self.cpu_legend()
        for static in self.query("#cpus_legend").results(Static):
            static.update(cpus_legend)
        tasks_legend = (f"[{c.running}]█[/] running   "
                        f"[on {c.waiting}] [/] runnable   "
                        f"[on {c.blocked}] [/] blocked   "
                        "(bar height is the fraction of time running)")
        for static in self.query("#tasks_legend").results(Static):
            static.update(tasks_legend)
        io_legend = ("   ".join(f"[{c.io[t]}]█[/] {IOTYPE_NAMES[t]}"
                                for t in range(NUM_IOTYPES)) +
                     "   (underline is an error)")
        for static in self.query("#io_legend").results(Static):
            static.update(io_legend)

    @work(thread=True, exclusive=True)
    def load_events(self) -> None:
        """Read the perf.data file in a thread so the UI stays responsive."""
        data = self.loading or self.data
        self.loading = data

        def progress() -> None:
            try:
                # Blocks until the UI has shown the data read so far.
                self.call_from_thread(self.update_progress, data)
            except RuntimeError:
                # The app is no longer running.
                data.cancelled = True

        data.progress = progress
        data.start_progress = data.last_progress = monotonic()
        try:
            read_events(data, self.input_name)
        except LoadCancelled:
            return
        except (OSError, ValueError, RuntimeError) as e:
            self.call_from_thread(self.exit, None, 1,
                                  f"Error processing {self.input_name}: {e}")
            return
        finally:
            data.progress = None
            self.loading = None
        if data.cancelled:
            return
        if not data.has_events():
            self.call_from_thread(self.exit, None, 1,
                                  f"Error: no scheduler, power or I/O events found in "
                                  f"{self.input_name}.\n"
                                  "Record them with 'perf timechart record'.")
            return
        self.call_from_thread(self.data_loaded)

    def data_loaded(self) -> None:
        """Called on the UI thread when loading completes."""
        self.loaded = True
        self.finish_loading()

    def update_progress(self, data: TimechartData) -> None:
        """Show how much of the file has been processed and the data so far."""
        self.sub_title = (f"Loading {self.input_name}: {data.nr_samples:,} samples, "
                          f"{fmt_duration(data.last_time - data.first_time)} of trace")
        self.update_views()

    def finish_loading(self) -> None:
        """Show all of the data and a summary of it."""
        self.update_views()
        views = self.query("#cpus_view")
        if views and not views.first(TimelineView).rows:
            self.query_one(TabbedContent).hide_tab("cpus")
        task_views = self.query("#tasks_view")
        if task_views and not task_views.first(TimelineView).rows and self.io_tasks:
            self.query_one(TabbedContent).hide_tab("tasks")
        nr_tasks = len(self.tasks) or len(self.io_tasks)
        self.sub_title = (f"{self.input_name}: "
                          f"{fmt_duration(self.data.last_time - self.data.first_time)}, "
                          f"{len(self.data.cpus)} CPUs, {nr_tasks} tasks")

    def cancel_loading(self) -> None:
        """Stop a background load, the worker notices at the next progress interval."""
        loading = self.loading
        if loading is not None:
            loading.cancelled = True

    async def action_quit(self) -> None:
        """Quit, stopping any background load."""
        self.cancel_loading()
        await super().action_quit()

    def on_unmount(self) -> None:
        """Stop any background load when the app exits."""
        self.cancel_loading()

    def cpu_legend(self) -> str:
        """Legend for the CPUs tab, saying which power events are missing.

        The data's lock must be held.
        """
        c = self.theme_colors
        data = self.data
        has_pstate = any(len(cpu.pstate) for cpu in data.cpus.values())
        has_cstate = any(len(cpu.cstate) for cpu in data.cpus.values())
        parts = [f"[{c.running}]█[/] busy"]
        if has_pstate and data.max_freq > data.min_freq:
            parts.append(f"[{c.freq_low}]█[/]→[{c.freq_high}]█[/] frequency")
        elif has_pstate:
            parts.append(f"frequency constant at {data.max_freq / 1000:.0f} MHz")
        if has_cstate:
            parts.append(f"[{c.idle_light}]█[/]→[{c.idle_dark}]█[/] idle state")
        missing = []
        if not has_pstate:
            missing.append("power:cpu_frequency")
        if not has_cstate:
            missing.append("power:cpu_idle")
        if missing:
            parts.append(f"[dim](no {' or '.join(missing)} events)[/dim]")
        return "   ".join(parts)

    def update_views(self) -> None:
        """Update the views from the data, which may still be loading."""
        data = self.data
        with data.lock:
            first, last = data.first_time, data.last_time
            self.tasks = data.sched_tasks(self.filters)
            self.io_tasks = data.io_tasks(self.filters)
            all_rows = {
                "cpus_view": self.cpu_rows(),
                "tasks_view": self.task_rows(),
                "io_view": [self.cached_row(IoRow, t.tid, t) for t in self.io_tasks],
            }
            legend = self.cpu_legend()
        for view_id, rows in all_rows.items():
            for view in self.query(f"#{view_id}").results(TimelineView):
                view.set_rows(rows)
        for static in self.query("#cpus_legend").results(Static):
            static.update(legend)
        if self.query("#io") and bool(self.io_tasks) != self.io_tab_shown:
            self.io_tab_shown = bool(self.io_tasks)
            tabbed = self.query_one(TabbedContent)
            if self.io_tab_shown:
                tabbed.show_tab("io")
            else:
                tabbed.hide_tab("io")
        if first or last:
            self.window = self.window.extend(first, last)
        self.summary_stale = True
        tabs = self.query(TabbedContent)
        if tabs and tabs.first(TabbedContent).active == "summary":
            self.update_summary()
        self.update_details()

    def update_summary(self) -> None:
        """Fill in the summary table keeping the selected task."""
        tables = self.query("#summary_table")
        if not tables:
            return
        self.summary_stale = False
        table = tables.first(DataTable)
        if not table.columns:
            table.add_columns("Task", "TID", "Running", "Waiting", "Blocked",
                              "Switches", "Wakeups", "I/O bytes")
        selected = None
        if table.row_count:
            try:
                selected = table.coordinate_to_cell_key(table.cursor_coordinate).row_key
            except CellDoesNotExist:
                pass
        summary_tasks = self.tasks if self.tasks else self.io_tasks
        with self.data.lock:
            rows = [(task.comm, task.tid,
                     fmt_duration(task.totals[STATE_RUNNING]),
                     fmt_duration(task.totals[STATE_WAITING]),
                     fmt_duration(task.totals[STATE_BLOCKED]),
                     task.switches, len(task.wakeups), task.io_bytes)
                    for task in sorted(summary_tasks,
                                       key=lambda t: (-t.totals[STATE_RUNNING], -t.io_bytes))]
        table.clear()
        for row in rows:
            table.add_row(*row, key=str(row[1]))
        if selected is not None:
            try:
                table.move_cursor(row=table.get_row_index(selected), animate=False)
            except RowDoesNotExist:
                pass

    def active_view(self) -> Optional[TimelineView]:
        """The timeline view in the active tab, if any."""
        tabs = self.query(TabbedContent)
        if not tabs:
            return None
        active = tabs.first(TabbedContent).active
        views = self.query(f"#{active}_view") if active else None
        return views.first(TimelineView) if views else None

    def watch_window(self) -> None:
        """The bound views repaint themselves, update the details."""
        self.update_details()

    @on(TimelineView.SelectionChanged)
    def on_selection_changed(self) -> None:
        """Describe the newly selected row."""
        self.update_details()

    def update_details(self) -> None:
        """Describe the selected row at the cursor."""
        details = self.query("#details")
        if not details:
            return
        win = self.window
        text = (f"Cursor [b]{self.data.fmt_time(win.cursor)}[/b]   "
                f"View {self.data.fmt_time(win.start)} - {self.data.fmt_time(win.end)} "
                f"({fmt_duration(win.span())})\n")
        view = self.active_view()
        row = view.selected_row() if view else None
        if row:
            with self.data.lock:
                text += row.describe(win.cursor, win.start, win.end)
        elif view is None:
            text += "Select a task and press enter to show it in the Tasks timeline"
        details.first(Static).update(text)

    @on(TabbedContent.TabActivated)
    def on_tab_activated(self) -> None:
        """Focus the view in the newly active tab."""
        view = self.active_view()
        if view:
            view.focus()
        else:
            if self.summary_stale:
                self.update_summary()
            tables = self.query("#summary_table")
            if tables:
                tables.first(DataTable).focus()
        self.update_details()

    @on(DataTable.RowSelected)
    def on_row_selected(self, event: DataTable.RowSelected) -> None:
        """Show the selected summary task in the tasks timeline."""
        if event.row_key.value is not None:
            self.goto_task(int(event.row_key.value), None)

    def goto_task(self, tid: int, time: Optional[float]) -> None:
        """Select the task in the tasks or I/O view, optionally moving the cursor."""
        for tab_id, view_id in (("tasks", "#tasks_view"), ("io", "#io_view")):
            views = self.query(view_id)
            if not views:
                continue
            view = views.first(TimelineView)
            for idx, row in enumerate(view.rows):
                if isinstance(row, (TaskRow, IoRow)) and row.task.tid == tid:
                    self.query_one(TabbedContent).active = tab_id
                    if time is not None:
                        self.window = self.window.with_cursor(time)
                    view.selected = idx
                    view.focus()
                    return
        self.notify(f"Task {tid} isn't shown in the tasks view", severity="warning")

    def action_sort(self) -> None:
        """Cycle the sort order of the tasks view."""
        self.sort_order = (self.sort_order + 1) % len(self.SORT_ORDERS)

    def watch_sort_order(self) -> None:
        """Re-sort the tasks view."""
        views = self.query("#tasks_view")
        if not views:
            return
        with self.data.lock:
            rows = self.task_rows()
        views.first(TimelineView).set_rows(rows)
        self.notify(f"Tasks sorted by {self.SORT_ORDERS[self.sort_order]}")
        self.update_details()

    def action_goto_waker(self) -> None:
        """Select the task that woke the selected task before the cursor."""
        view = self.active_view()
        row = view.selected_row() if view else None
        if not isinstance(row, (TaskRow, IoRow)):
            return
        with self.data.lock:
            wakeups = row.task.wakeups
            idx = bisect.bisect_right(wakeups, (self.window.cursor, sys.maxsize)) - 1
            wake_time, waker = wakeups[idx] if idx >= 0 else (0, 0)
        if idx < 0:
            self.notify("No wakeup before the cursor", severity="warning")
            return
        if waker <= 0:
            self.notify(f"Woken by {self.data.task_name(waker)} at "
                        f"{self.data.fmt_time(wake_time)}")
            return
        self.goto_task(waker, wake_time)


def main() -> None:
    """Parse arguments, read the perf.data file and run the app."""
    parser = argparse.ArgumentParser(
        description="Interactive timechart of CPU, task and I/O activity.")
    parser.add_argument("-i", "--input", default="perf.data", help="input perf.data file")
    parser.add_argument("-P", "--power-only", action="store_true",
                        help="only show CPU power information")
    parser.add_argument("-T", "--tasks-only", action="store_true",
                        help="only show task information")
    parser.add_argument("-p", "--process", action="append", default=[],
                        help="only show processes with the given name or PID, may be repeated")
    parser.add_argument("--dump", action="store_true",
                        help="print a text summary rather than running the interactive UI")
    args = parser.parse_args()

    if args.power_only and args.tasks_only:
        print("Error: -P and -T options cannot be used at the same time.", file=sys.stderr)
        sys.exit(1)

    if args.input == "-":
        if not args.dump:
            # The interactive UI reads the keyboard from stdin.
            print("Error: reading perf.data from stdin requires --dump.", file=sys.stderr)
            sys.exit(1)
    elif not os.path.exists(args.input):
        print(f"Error: {args.input} not found. (try 'perf timechart record' first)",
              file=sys.stderr)
        sys.exit(1)

    if args.dump:
        data = load_data(args.input)
        data.dump(args.power_only, args.tasks_only, args.process)
        return

    # The app starts immediately and loads the data in the background.
    app = TimechartApp(args.input, args.power_only, args.tasks_only, args.process)
    app.run()
    sys.exit(app.return_code or 0)


def read_events(data: TimechartData, input_name: str) -> None:
    """Process the events in input_name into data, raising on errors."""
    if data.cancelled:
        raise LoadCancelled()
    try:
        data.session = perf.session(perf.data(input_name), sample=data.process_event)
        data.session.process_events()
        data.finish()
    finally:
        # Break the reference cycle between the session and the callback.
        data.session = None


def load_data(input_name: str) -> TimechartData:
    """Read the perf.data file exiting on errors or if there's nothing to show."""
    if input_name != "-" and not os.path.exists(input_name):
        print(f"Error: {input_name} not found. (try 'perf timechart record' first)",
              file=sys.stderr)
        sys.exit(1)

    data = TimechartData()
    try:
        read_events(data, input_name)
    except (OSError, ValueError, RuntimeError) as e:
        print(f"Error processing {input_name}: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        data.finish()

    if not data.has_events():
        print(f"Error: no scheduler, power or I/O events found in {input_name}.\n"
              "Record them with 'perf timechart record'.", file=sys.stderr)
        sys.exit(1)
    return data


if __name__ == "__main__":
    main()
