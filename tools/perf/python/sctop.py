#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
System call top

Periodically displays system-wide system call totals, broken down by
syscall.  If a [comm] arg is specified, only syscalls called by
[comm] are displayed. If an [interval] arg is specified, the display
will be refreshed every [interval] seconds.  The default interval is
3 seconds.

Ported from tools/perf/scripts/python/sctop.py
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import os
import sys
import threading
from typing import Optional
import perf
from perf_live import LiveSession


class SCTopAnalyzer:
    """Periodically displays system-wide system call totals."""

    def __init__(self, for_comm: Optional[str], interval: int, offline: bool = False):
        self.for_comm = for_comm
        self.interval = interval
        self.syscalls: dict[int, int] = defaultdict(int)
        self.comm_cache: dict[int, str] = {}
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self.print_syscall_totals)
        self.offline = offline
        self.own_pid = os.getpid()
        self.last_print_time: Optional[int] = None
        self.printed = False
        self.session: Optional[perf.session] = None
        self.e_machine: Optional[int] = None

    def syscall_name(self, syscall_id: int) -> str:
        """Lookup syscall name by ID."""
        # Mask out the x86_64 x32 ABI bit (__X32_SYSCALL_BIT = 0x40000000) before
        # resolving the syscall number in the architecture's syscall table.
        raw_sc_id = syscall_id & ~0x40000000
        try:
            e_machine = getattr(self.session, "e_machine", self.e_machine)
            if e_machine is not None:
                name = perf.syscall_name(raw_sc_id, e_machine)
            else:
                name = perf.syscall_name(raw_sc_id)
            if name is not None:
                return name
        except (TypeError, OverflowError):
            pass
        return str(syscall_id)

    def process_event(self, sample: perf.sample_event) -> None:
        """Collect syscall events."""
        if not self.offline and sample.sample_pid == self.own_pid:
            return

        name = str(sample.evsel)
        # Per-syscall syscalls:sys_enter_* tracepoints expose '__syscall_nr' (or 'nr')
        # and may have an unrelated syscall argument named 'id', whereas
        # raw_syscalls:sys_enter (and legacy pre-2.6.35 syscalls:sys_enter) expose 'id'.
        if name.startswith("evsel(syscalls:sys_enter_"):
            syscall_id = getattr(sample, "__syscall_nr", -1)
            if not (0 <= (syscall_id & ~0x40000000) <= 0xffff):
                syscall_id = getattr(sample, "nr", -1)
        elif name.startswith(("evsel(raw_syscalls:sys_enter", "evsel(syscalls:sys_enter")):
            syscall_id = getattr(sample, "id", -1)
            if not (0 <= (syscall_id & ~0x40000000) <= 0xffff):
                syscall_id = getattr(sample, "__syscall_nr", -1)
            if not (0 <= (syscall_id & ~0x40000000) <= 0xffff):
                syscall_id = getattr(sample, "nr", -1)
        else:
            syscall_id = -1

        skip = False
        with self.lock:
            if self.for_comm is not None:
                is_execve = (0 <= (syscall_id & ~0x40000000) <= 0xffff and
                             self.syscall_name(syscall_id) in ("execve", "execveat"))
                if is_execve:
                    self.comm_cache.pop(sample.sample_pid, None)

                comm = "Unknown"
                if hasattr(self, 'session') and self.session:
                    # In offline perf.data mode, query session.find_thread() directly
                    # so PERF_RECORD_COMM updates after execve (e.g. perf -> sleep)
                    # are reflected immediately rather than returning a stale cached comm.
                    try:
                        proc = self.session.find_thread(sample.sample_pid, sample.sample_tid)
                        if proc:
                            comm = proc.comm() or "Unknown"
                    except TypeError:
                        pass
                    if comm != "Unknown" and not is_execve:
                        self.comm_cache[sample.sample_pid] = comm
                    elif sample.sample_pid in self.comm_cache:
                        comm = self.comm_cache[sample.sample_pid]
                elif sample.sample_pid in self.comm_cache:
                    comm = self.comm_cache[sample.sample_pid]
                else:
                    try:
                        with open(f"/proc/{sample.sample_pid}/comm", "r",
                                  encoding="utf-8", errors="replace") as f:
                            comm = f.read().strip()
                    except OSError:
                        comm = "Unknown"
                    # Cache both matching and non-matching comms (including "Unknown"
                    # when a PID has exited or is inaccessible) so live system-wide
                    # tracing does not re-open /proc/<pid>/comm on every syscall.
                    # Do not cache during sys_enter(execve/execveat) since /proc/<pid>/comm
                    # still holds the pre-exec command name until the syscall completes.
                    if not is_execve:
                        self.comm_cache[sample.sample_pid] = comm

                if comm != self.for_comm:
                    skip = True

            is_enter = (name.startswith("evsel(raw_syscalls:sys_enter") or
                        name.startswith("evsel(syscalls:sys_enter"))
            if not skip and is_enter and 0 <= (syscall_id & ~0x40000000) <= 0xffff:
                self.syscalls[syscall_id] += 1

        if self.offline and hasattr(sample, "sample_time"):
            interval_ns = self.interval * (10 ** 9)
            if self.last_print_time is None:
                self.last_print_time = sample.sample_time
            elif sample.sample_time - self.last_print_time >= interval_ns:
                self.print_current_totals()
                self.last_print_time = sample.sample_time

    def print_current_totals(self):
        """Print current syscall totals."""
        self.printed = True
        # Clear terminal
        if not self.offline:
            print("\x1b[2J\x1b[H", end="")
        else:
            print()

        with self.lock:
            for_comm = self.for_comm
        if for_comm is not None:
            print(f"\nsyscall events for {for_comm}:\n")
        else:
            print("\nsyscall events:\n")

        print(f"{'event':40s}  {'count':10s}")
        print(f"{'-' * 40:40s}  {'-' * 10:10s}")

        with self.lock:
            current_syscalls = list(self.syscalls.items())
            self.syscalls.clear()
            self.comm_cache.clear()

        current_syscalls.sort(key=lambda kv: (-kv[1], kv[0]))

        for syscall_id, val in current_syscalls:
            print(f"{self.syscall_name(syscall_id):<40s}  {val:10d}")

    def print_syscall_totals(self):
        """Periodically print syscall totals."""
        while not self.stop_event.is_set():
            self.print_current_totals()
            self.stop_event.wait(self.interval)
        # Print final batch
        self.print_current_totals()

    def start(self):
        """Start the background thread."""
        self.thread.start()

    def stop(self):
        """Stop the background thread."""
        self.stop_event.set()
        self.thread.join()


def main():
    """Main function."""
    ap = argparse.ArgumentParser(description="System call top")
    ap.add_argument("args", nargs="*", help="[comm] [interval] or [interval]")
    ap.add_argument("-i", "--input", help="Input file name")
    args = ap.parse_args()

    for_comm = None
    default_interval = 3
    interval = default_interval

    if len(args.args) > 2:
        print("Usage: python sctop.py [comm] [interval]")
        sys.exit(1)

    if len(args.args) > 1:
        for_comm = args.args[0]
        try:
            interval = int(args.args[1])
        except ValueError:
            print(f"Invalid interval: {args.args[1]}")
            sys.exit(1)
    elif len(args.args) > 0:
        try:
            interval = int(args.args[0])
        except ValueError:
            for_comm = args.args[0]
            interval = default_interval

    analyzer = SCTopAnalyzer(for_comm, interval, offline=bool(args.input))
    session = None

    try:
        if args.input:
            session = perf.session(perf.data(args.input), sample=analyzer.process_event)
            analyzer.session = session
            analyzer.e_machine = getattr(session, "e_machine", None)
            session.process_events()
        else:
            try:
                live_session = LiveSession(
                    "raw_syscalls:sys_enter", sample_callback=analyzer.process_event
                )
            except OSError:
                live_session = LiveSession(
                    "syscalls:sys_enter_*", sample_callback=analyzer.process_event
                )
            analyzer.start()
            live_session.run()
    except KeyboardInterrupt:
        pass
    except (OSError, IOError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        if args.input:
            if not analyzer.printed or analyzer.syscalls:
                analyzer.print_current_totals()
            # Break the reference cycle between perf.session and analyzer.process_event
            # because perf.session lacks cyclic GC support (tp_traverse).
            analyzer.session = None
            session = None
        elif analyzer.thread.is_alive():
            analyzer.stop()


if __name__ == "__main__":
    main()
