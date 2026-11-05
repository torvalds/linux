#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Displays system-wide system call totals, broken down by syscall.
If a [comm] arg is specified, only syscalls called by [comm] are displayed.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from typing import (Dict, Tuple)
import perf

syscalls: Dict[Tuple[str, int, int], int] = defaultdict(int)
for_comm = None
for_pid = None
session = None


def print_syscall_totals():
    """Print aggregated statistics."""
    if for_comm is not None:
        print(f"\nsyscall events for {for_comm}:\n")
    elif for_pid is not None:
        print(f"\nsyscall events for PID {for_pid}:\n")
    else:
        print("\nsyscall events:\n")

    print(f"{'comm [pid]/syscalls':<40} {'count':>10}")
    print("---------------------------------------- -----------")

    sorted_keys = sorted(syscalls.keys(), key=lambda k: (k[0], k[1], -syscalls[k], k[2]))
    current_comm_pid = None
    for comm, pid, sc_id in sorted_keys:
        if current_comm_pid != (comm, pid):
            print(f"\n{comm} [{pid}]")
            current_comm_pid = (comm, pid)
        e_machine = getattr(session, "e_machine", 0) or 0
        # Mask out the x86_64 x32 ABI bit (__X32_SYSCALL_BIT = 0x40000000) before
        # resolving the syscall number in the architecture's syscall table.
        raw_sc_id = sc_id & ~0x40000000
        if e_machine:
            name = perf.syscall_name(raw_sc_id, e_machine) or str(sc_id)
        else:
            name = perf.syscall_name(raw_sc_id) or str(sc_id)
        print(f"  {name:<38} {syscalls[(comm, pid, sc_id)]:>10}")


def process_event(sample):
    """Process a single sample event."""
    event_name = str(sample.evsel)
    # Per-syscall syscalls:sys_enter_* tracepoints expose '__syscall_nr' (or 'nr')
    # and may have an unrelated syscall argument named 'id', whereas
    # raw_syscalls:sys_enter (and legacy pre-2.6.35 syscalls:sys_enter) expose 'id'.
    if event_name.startswith("evsel(syscalls:sys_enter_"):
        sc_id = getattr(sample, "__syscall_nr", None)
        if sc_id is not None and not (0 <= (sc_id & ~0x40000000) <= 0xffff):
            sc_id = None
        if sc_id is None:
            sc_id = getattr(sample, "nr", -1)
    elif event_name.startswith(("evsel(raw_syscalls:sys_enter", "evsel(syscalls:sys_enter")):
        sc_id = getattr(sample, "id", -1)
        if not (0 <= (sc_id & ~0x40000000) <= 0xffff):
            sc_id = getattr(sample, "__syscall_nr", -1)
        if not (0 <= (sc_id & ~0x40000000) <= 0xffff):
            sc_id = getattr(sample, "nr", -1)
    else:
        return

    # Mask out __X32_SYSCALL_BIT (0x40000000) when validating the syscall ID range.
    if not (0 <= (sc_id & ~0x40000000) <= 0xffff):
        return

    pid = sample.sample_pid

    if for_pid is not None and pid != for_pid:
        return

    comm = "unknown"
    try:
        if session:
            proc = session.find_thread(sample.sample_pid, sample.sample_tid)
            if proc:
                comm = proc.comm() or "unknown"
    except (TypeError, AttributeError):
        pass

    if for_comm and comm != for_comm:
        return
    syscalls[(comm, pid, sc_id)] += 1


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("filter", nargs="?", help="COMM or PID to filter by")
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    args = ap.parse_args()

    if args.filter:
        try:
            for_pid = int(args.filter)
        except ValueError:
            for_comm = args.filter

    try:
        session = perf.session(perf.data(args.input), sample=process_event)
        session.process_events()
        print_syscall_totals()
    finally:
        # Break the reference cycle between session and process_event (whose module
        # globals reference session) since perf.session lacks cyclic GC (tp_traverse).
        session = None
