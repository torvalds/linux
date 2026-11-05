#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Measures futex contention."""
from __future__ import annotations

import argparse
from collections import defaultdict
from typing import Dict, Tuple
import perf

class LockStats:
    """Aggregate lock contention information."""
    def __init__(self) -> None:
        self.count = 0
        self.total_time = 0
        self.min_time = 0
        self.max_time = 0

    def add(self, duration: int) -> None:
        """Add a new duration measurement."""
        self.count += 1
        self.total_time += duration
        if self.count == 1:
            self.min_time = duration
            self.max_time = duration
        else:
            self.min_time = min(self.min_time, duration)
            self.max_time = max(self.max_time, duration)

    def avg(self) -> float:
        """Return average duration."""
        return self.total_time / self.count if self.count > 0 else 0.0

process_names: Dict[int, str] = {}
start_times: Dict[int, Tuple[int, int]] = {}
session = None
durations: Dict[Tuple[int, int], LockStats] = defaultdict(LockStats)

FUTEX_WAIT = 0
FUTEX_PRIVATE_FLAG = 128
FUTEX_CLOCK_REALTIME = 256
# Mask out FUTEX_PRIVATE_FLAG and FUTEX_CLOCK_REALTIME so variants such as
# FUTEX_WAIT_PRIVATE (0 | 128) match the base FUTEX_WAIT command.
FUTEX_CMD_MASK = ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)


def handle_start(pid: int, tid: int, uaddr: int, op: int, start_time: int) -> None:
    """Handle a futex sys_enter event."""
    if (op & FUTEX_CMD_MASK) != FUTEX_WAIT:
        return
    try:
        if session:
            process = session.find_thread(pid, tid)
            if process:
                process_names[tid] = process.comm() or "unknown"
    except (TypeError, AttributeError):
        pass
    if tid not in process_names:
        process_names[tid] = "unknown"

    start_times[tid] = (uaddr, start_time)

def handle_end(tid: int, end_time: int) -> None:
    """Handle a futex sys_exit event."""
    if tid not in start_times:
        return
    (uaddr, start_time) = start_times[tid]
    del start_times[tid]
    durations[(tid, uaddr)].add(end_time - start_time)

def process_event(sample: perf.sample_event) -> None:
    """Process a single sample event."""
    event_name = str(sample.evsel)
    if event_name.startswith("evsel(") and event_name.endswith(")"):
        event_name = event_name[6:-1]
    if event_name.startswith("syscalls:sys_enter_futex"):
        uaddr = getattr(sample, "uaddr", None)
        op = getattr(sample, "op", None)
        if uaddr is None or op is None:
            return  # Tracepoint fields missing, skip silent attribution to 0
        handle_start(getattr(sample, "sample_pid", sample.sample_tid),
                     sample.sample_tid, uaddr, op, sample.sample_time)
    elif event_name.startswith("syscalls:sys_exit_futex"):
        handle_end(sample.sample_tid, sample.sample_time)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Measure futex contention")
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    args = ap.parse_args()

    try:
        session = perf.session(perf.data(args.input), sample=process_event)
        try:
            session.process_events()
        except KeyboardInterrupt:
            pass

        for ((t, u), stats) in sorted(durations.items()):
            avg_ns = stats.avg()
            print(f"{process_names.get(t, 'unknown')}[{t}] lock {u:x} contended "
                  f"{stats.count} times, {avg_ns:.0f} avg ns "
                  f"[max: {stats.max_time} ns, min {stats.min_time} ns]")
    finally:
        # Break the reference cycle between the global session and the
        # process_event callback so the C perf.session object is freed.
        session = None
