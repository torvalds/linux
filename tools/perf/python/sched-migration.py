#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Cpu task migration overview toy

Copyright (C) 2010 Frederic Weisbecker <fweisbec@gmail.com>
Ported to modern directory structure and refactored to use class.
"""
from __future__ import annotations

import argparse
from collections import defaultdict, UserList
import importlib
import sys
from typing import Any
import perf


# Global threads dictionary
threads = defaultdict(lambda: "unknown")
threads[0] = "idle"


def thread_name(pid: int) -> str:
    """Return thread name formatted with pid."""
    return f"{threads[pid]}:{pid}"


def task_state(state: int) -> str:
    """Map task state integer to string."""
    states = {
        0: "R",
        1: "S",
        2: "D",
        64: "DEAD"
    }
    return states.get(state, "Unknown")


class RunqueueEventUnknown:
    """Unknown runqueue event."""
    @staticmethod
    def color():
        """Return color for event."""
        return None

    def __repr__(self):
        return "unknown"


class RunqueueEventSleep:
    """Sleep runqueue event."""
    @staticmethod
    def color():
        """Return color for event."""
        return 0, 0, 0xff

    def __init__(self, sleeper: int):
        self.sleeper = sleeper

    def __repr__(self):
        return f"{thread_name(self.sleeper)} gone to sleep"


class RunqueueEventWakeup:
    """Wakeup runqueue event."""
    @staticmethod
    def color():
        """Return color for event."""
        return 0xff, 0xff, 0

    def __init__(self, wakee: int):
        self.wakee = wakee

    def __repr__(self):
        return f"{thread_name(self.wakee)} woke up"


class RunqueueEventFork:
    """Fork runqueue event."""
    @staticmethod
    def color():
        """Return color for event."""
        return 0, 0xff, 0

    def __init__(self, child: int):
        self.child = child

    def __repr__(self):
        return f"new forked task {thread_name(self.child)}"


class RunqueueMigrateIn:
    """Migrate in runqueue event."""
    @staticmethod
    def color():
        """Return color for event."""
        return 0, 0xf0, 0xff

    def __init__(self, new: int):
        self.new = new

    def __repr__(self):
        return f"task migrated in {thread_name(self.new)}"


class RunqueueMigrateOut:
    """Migrate out runqueue event."""
    @staticmethod
    def color():
        """Return color for event."""
        return 0xff, 0, 0xff

    def __init__(self, old: int):
        self.old = old

    def __repr__(self):
        return f"task migrated out {thread_name(self.old)}"


class RunqueueSnapshot:
    """Snapshot of runqueue state."""

    def __init__(self, tasks=None, event=None):
        if tasks is None:
            tasks = (0,)
        if event is None:
            event = RunqueueEventUnknown()
        self.tasks = tuple(tasks)
        self.event = event

    def sched_switch(self, prev: int, prev_state: int, next_pid: int):
        """Handle sched switch in snapshot."""
        if task_state(prev_state) == "R" and next_pid in self.tasks \
                and prev in self.tasks:
            return self

        event = (
            RunqueueEventSleep(prev)
            if task_state(prev_state) != "R"
            else RunqueueEventUnknown()
        )

        next_tasks = list(self.tasks[:])
        if prev in self.tasks:
            if task_state(prev_state) != "R":
                next_tasks.remove(prev)
        elif task_state(prev_state) == "R":
            next_tasks.append(prev)

        if next_pid not in next_tasks:
            next_tasks.append(next_pid)

        return RunqueueSnapshot(next_tasks, event)

    def migrate_out(self, old: int):
        """Handle task migrate out in snapshot."""
        if old not in self.tasks:
            return self
        next_tasks = [task for task in self.tasks if task != old]

        return RunqueueSnapshot(next_tasks, RunqueueMigrateOut(old))

    def __migrate_in(self, new: int, event):
        if new in self.tasks:
            return RunqueueSnapshot(self.tasks, event)
        next_tasks = self.tasks + tuple([new])

        return RunqueueSnapshot(next_tasks, event)

    def migrate_in(self, new: int):
        """Handle task migrate in snapshot."""
        return self.__migrate_in(new, RunqueueMigrateIn(new))

    def wake_up(self, new: int):
        """Handle task wakeup in snapshot."""
        return self.__migrate_in(new, RunqueueEventWakeup(new))

    def wake_up_new(self, new: int):
        """Handle task fork in snapshot."""
        return self.__migrate_in(new, RunqueueEventFork(new))

    def load(self) -> int:
        """Provide the number of tasks on the runqueue. Don't count idle"""
        return len(self.tasks) - 1

    def __repr__(self):
        return self.tasks.__repr__()


class TimeSlice:
    """Represents a time slice of execution."""

    def __init__(self, start: int, prev):
        self.start = start
        self.prev = prev
        self.end = start
        # cpus that triggered the event
        self.event_cpus: list[int] = []
        if prev is not None:
            self.total_load = prev.total_load
            self.rqs = prev.rqs.copy()
        else:
            self.rqs = defaultdict(RunqueueSnapshot)
            self.total_load = 0

    def __update_total_load(self, old_rq: RunqueueSnapshot, new_rq: RunqueueSnapshot):
        diff = new_rq.load() - old_rq.load()
        self.total_load += diff

    def sched_switch(self, ts_list, prev: int, prev_state: int, next_pid: int, cpu: int):
        """Process sched_switch in time slice."""
        old_rq = self.prev.rqs[cpu]
        new_rq = old_rq.sched_switch(prev, prev_state, next_pid)

        if old_rq is new_rq:
            return

        self.rqs[cpu] = new_rq
        self.__update_total_load(old_rq, new_rq)
        ts_list.append(self)
        self.event_cpus = [cpu]

    def migrate(self, ts_list, new: int, old_cpu: int, new_cpu: int):
        """Process task migration in time slice."""
        if old_cpu == new_cpu:
            return
        old_rq = self.prev.rqs[old_cpu]
        out_rq = old_rq.migrate_out(new)
        self.rqs[old_cpu] = out_rq
        self.__update_total_load(old_rq, out_rq)

        new_rq = self.prev.rqs[new_cpu]
        in_rq = new_rq.migrate_in(new)
        self.rqs[new_cpu] = in_rq
        self.__update_total_load(new_rq, in_rq)

        ts_list.append(self)

        if old_rq is not out_rq:
            self.event_cpus.append(old_cpu)
        self.event_cpus.append(new_cpu)

    def wake_up(self, ts_list, pid: int, cpu: int, fork: bool):
        """Process wakeup in time slice."""
        old_rq = self.prev.rqs[cpu]
        if fork:
            new_rq = old_rq.wake_up_new(pid)
        else:
            new_rq = old_rq.wake_up(pid)

        if new_rq is old_rq:
            return
        self.rqs[cpu] = new_rq
        self.__update_total_load(old_rq, new_rq)
        ts_list.append(self)
        self.event_cpus = [cpu]

    def next(self, t: int):
        """Create next time slice."""
        self.end = t
        return TimeSlice(t, self)


class TimeSliceList(UserList):
    """List of time slices with search capabilities."""

    def __init__(self, arg=None):
        super().__init__(arg if arg is not None else [])
        self.root_win = None

    def get_time_slice(self, ts: int) -> TimeSlice:
        """Get or create time slice for timestamp."""
        if len(self.data) == 0:
            ts_slice = TimeSlice(ts, TimeSlice(-1, None))
        else:
            ts_slice = self.data[-1].next(ts)
        return ts_slice

    def find_time_slice(self, ts: int) -> int:
        """Binary search for time slice containing timestamp."""
        if not self.data:
            return -1
        start = 0
        end = len(self.data)
        found = -1
        searching = True
        while searching:
            if start in (end, end - 1):
                searching = False

            i = (end + start) // 2
            if self.data[i].start <= ts <= self.data[i].end:
                found = i
                break

            if self.data[i].end < ts:
                start = i
            elif self.data[i].start > ts:
                end = i

        return found

    def set_root_win(self, win):
        """Set root window for GUI."""
        self.root_win = win

    def mouse_down(self, cpu: int, t: int):
        """Handle mouse down event from GUI."""
        idx = self.find_time_slice(t)
        if idx == -1:
            return

        ts = self[idx]
        rq = ts.rqs[cpu]
        raw = f"CPU: {cpu}\n"
        raw += f"Last event : {repr(rq.event)}\n"
        raw += f"Timestamp : {ts.start // (10 ** 9)}.{ts.start % (10 ** 9) // 1000:06d}\n"
        raw += f"Duration : {(ts.end - ts.start) // (10 ** 3):6d} us\n"
        raw += f"Load = {rq.load()}\n"
        for task in rq.tasks:
            raw += f"{thread_name(task)} \n"

        if self.root_win:
            self.root_win.update_summary(raw)

    def update_rectangle_cpu(self, slice_obj: TimeSlice, cpu: int):
        """Update rectangle for CPU in GUI."""
        rq = slice_obj.rqs[cpu]

        if slice_obj.total_load != 0:
            load_rate = rq.load() / float(slice_obj.total_load)
        else:
            load_rate = 0

        red_power = int(0xff - (0xff * load_rate))
        color = (0xff, red_power, red_power)

        top_color = None
        if cpu in slice_obj.event_cpus:
            top_color = rq.event.color()

        if self.root_win:
            self.root_win.paint_rectangle_zone(cpu, color, top_color,
                                               slice_obj.start, slice_obj.end)

    def fill_zone(self, start: int, end: int):
        """Fill zone in GUI."""
        i = self.find_time_slice(start)
        if i == -1:
            return

        for idx in range(i, len(self.data)):
            timeslice = self.data[idx]
            if timeslice.start > end:
                return

            for cpu in timeslice.rqs:
                self.update_rectangle_cpu(timeslice, cpu)

    def interval(self) -> tuple[int, int]:
        """Return start and end timestamps."""
        if len(self.data) == 0:
            return 0, 0
        return self.data[0].start, self.data[-1].end

    def nr_rectangles(self) -> int:
        """Return maximum CPU number."""
        if not self.data:
            return 0
        last_ts = self.data[-1]
        max_cpu = 0
        for cpu in last_ts.rqs:
            max_cpu = max(max_cpu, cpu)
        return max_cpu


class SchedMigrationAnalyzer:
    """Analyzes task migrations and manages time slices."""

    def __init__(self):
        self.current_tsk = defaultdict(lambda: -1)
        self.timeslices = TimeSliceList()

    def sched_switch(self, time: int, cpu: int, prev_comm: str, prev_pid: int, prev_state: int,
                     next_comm: str, next_pid: int):
        """Handle sched_switch event."""
        on_cpu_task = self.current_tsk[cpu]

        if on_cpu_task not in (-1, prev_pid):
            print(f"Sched switch event rejected ts: {time} cpu: {cpu} "
                  f"prev: {prev_comm}({prev_pid}) next: {next_comm}({next_pid})")

        threads[prev_pid] = prev_comm
        threads[next_pid] = next_comm
        self.current_tsk[cpu] = next_pid

        ts = self.timeslices.get_time_slice(time)
        ts.sched_switch(self.timeslices, prev_pid, prev_state, next_pid, cpu)

    def migrate(self, time: int, pid: int, orig_cpu: int, dest_cpu: int):
        """Handle sched_migrate_task event."""
        ts = self.timeslices.get_time_slice(time)
        ts.migrate(self.timeslices, pid, orig_cpu, dest_cpu)

    def wake_up(self, time: int, pid: int, success: int, target_cpu: int, fork: bool):
        """Handle wakeup event."""
        if success == 0:
            return
        ts = self.timeslices.get_time_slice(time)
        ts.wake_up(self.timeslices, pid, target_cpu, fork)

    def process_event(self, sample: perf.sample_event) -> None:
        """Collect events and pass to analyzer."""
        name = str(sample.evsel)
        if name.startswith("evsel(") and name.endswith(")"):
            name = name[6:-1]
        if name.count(":") >= 2:
            name = ":".join(name.split(":")[:2])
        time = sample.sample_time
        cpu = sample.sample_cpu
        _pid = sample.sample_pid
        _comm = "Unknown"

        if name.startswith("sched:sched_switch"):
            prev_comm = getattr(sample, "prev_comm", "Unknown")
            prev_pid = getattr(sample, "prev_pid", -1)
            prev_state = getattr(sample, "prev_state", 0)
            next_comm = getattr(sample, "next_comm", "Unknown")
            next_pid = getattr(sample, "next_pid", -1)
            self.sched_switch(time, cpu, prev_comm, prev_pid, prev_state, next_comm, next_pid)
        elif name.startswith("sched:sched_migrate_task"):
            task_pid = getattr(sample, "pid", -1)
            orig_cpu = getattr(sample, "orig_cpu", -1)
            dest_cpu = getattr(sample, "dest_cpu", -1)
            self.migrate(time, task_pid, orig_cpu, dest_cpu)
        elif name.startswith("sched:sched_wakeup_new"):
            task_pid = getattr(sample, "pid", -1)
            success = getattr(sample, "success", 1)
            target_cpu = getattr(sample, "target_cpu", -1)
            self.wake_up(time, task_pid, success, target_cpu, True)
        elif name.startswith("sched:sched_wakeup"):
            task_pid = getattr(sample, "pid", -1)
            success = getattr(sample, "success", 1)
            target_cpu = getattr(sample, "target_cpu", -1)
            self.wake_up(time, task_pid, success, target_cpu, False)

    def run_gui(self):
        """Start wxPython GUI."""
        try:
            wx_mod: Any = importlib.import_module("wx")
            sched_gui: Any = importlib.import_module("SchedGui")
        except ImportError:
            print("wxPython is not available. Cannot start GUI.")
            return
        app = wx_mod.App(False)
        _frame = sched_gui.RootFrame(self.timeslices, "Migration")
        app.MainLoop()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Cpu task migration overview toy")
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="Print parsed migration summary")
    ap.add_argument("--gui", action="store_true", default=True,
                    help="Start the GUI (default)")
    ap.add_argument("--no-gui", action="store_true", help="Do not start the GUI")
    args = ap.parse_args()

    analyzer = SchedMigrationAnalyzer()
    session = None

    try:
        try:
            session = perf.session(perf.data(args.input), sample=analyzer.process_event)
            try:
                session.process_events()
            except KeyboardInterrupt:
                pass
        finally:
            # Release the perf.session reference before launching the long-running
            # wxPython GUI event loop so the underlying C session and callback
            # reference cycle are freed immediately.
            session = None
        if args.verbose:
            start_ts, end_ts = analyzer.timeslices.interval()
            print(f"Timeslices: {len(analyzer.timeslices.data)} "
                  f"(interval: {start_ts}..{end_ts})")
        if args.gui and not args.no_gui:
            analyzer.run_gui()
    except KeyboardInterrupt:
        pass
    except OSError as e:
        print(f"Error processing events: {e}")
        sys.exit(1)
