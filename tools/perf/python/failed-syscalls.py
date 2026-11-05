#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Failed system call counts."""
from __future__ import annotations

import argparse
from collections import defaultdict
import sys
from typing import Optional
import perf

class FailedSyscalls:
    """Tracks and displays failed system call totals."""
    def __init__(self, comm: Optional[str] = None) -> None:
        self.failed_syscalls: dict[str, int] = defaultdict(int)
        self.for_comm = comm
        self.session: Optional[perf.session] = None

    def process_event(self, sample: perf.sample_event) -> None:
        """Process sys_exit events."""
        event_name = str(sample.evsel)
        if not event_name.startswith("evsel(syscalls:sys_exit") and \
           not event_name.startswith("evsel(raw_syscalls:sys_exit"):
            return

        try:
            ret = sample.ret
        except AttributeError:
            print("ERROR: tracepoint fields missed", file=sys.stderr)
            sys.exit(1)

        if ret > 0:
            if ret >= 0xfffffffffffff000:  # 64-bit negative errors
                ret -= 0x10000000000000000
            elif 0xfffff000 <= ret <= 0xffffffff:  # 32-bit negative errors
                assert self.session is not None
                if not self.session.is_64_bit:
                    ret -= 0x100000000

        # Linux kernel syscalls only return error codes in the [-4095, -1] (-MAX_ERRNO)
        # range; other negative signed values are valid unsigned user-space addresses.
        if not -4095 <= ret < 0:
            return

        assert self.session is not None
        try:
            thread = self.session.find_thread(sample.sample_pid, sample.sample_tid)
            comm = (thread.comm() if thread else None) or "unknown"
        except (TypeError, AttributeError):
            # find_thread returns None when the thread isn't known.
            comm = "unknown"

        if self.for_comm and comm != self.for_comm:
            return

        self.failed_syscalls[comm] += 1

    def print_totals(self) -> None:
        """Print summary table."""
        print("\nfailed syscalls by comm:\n")
        print(f"{'comm':<20s}  {'# errors':>10s}")
        print(f"{'-'*20}  {'-'*10}")

        for comm, val in sorted(self.failed_syscalls.items(),
                                key=lambda kv: (-kv[1], kv[0])):
            print(f"{comm:<20s}  {val:10d}")

    def run(self, input_file: str) -> None:
        """Run the session."""
        self.session = perf.session(perf.data(input_file), sample=self.process_event)
        try:
            self.session.process_events()
        finally:
            # Break the reference cycle between perf.session and self.process_event
            # because perf.session lacks cyclic GC support (tp_traverse).
            self.session = None
        self.print_totals()

def main() -> None:
    """Main function."""
    parser = argparse.ArgumentParser(description="Trace failed syscalls")
    parser.add_argument("comm", nargs="?", help="Filter by command name")
    parser.add_argument("-i", "--input", default="perf.data", help="Input file")
    args = parser.parse_args()

    analyzer = FailedSyscalls(args.comm)
    try:
        analyzer.run(args.input)
    except IOError as e:
        print(e, file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
