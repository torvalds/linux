#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Display r/w activity for files read/written to for a given program."""
from __future__ import annotations

import argparse
from collections import defaultdict
import sys
from typing import Optional, Dict
import perf

class RwByFile:
    """Tracks and displays read/write activity by file descriptor."""
    def __init__(self, comm: str) -> None:
        self.for_comm = comm
        self.reads: Dict[int, Dict[str, int]] = defaultdict(
            lambda: {"bytes_requested": 0, "total_reads": 0}
        )
        self.writes: Dict[int, Dict[str, int]] = defaultdict(
            lambda: {"bytes_written": 0, "total_writes": 0}
        )
        self.unhandled: Dict[str, int] = defaultdict(int)
        self.session: Optional[perf.session] = None

    def process_event(self, sample: perf.sample_event) -> None:
        """Process events."""
        event_name = str(sample.evsel)
        if event_name.startswith("evsel(") and event_name.endswith(")"):
            event_name = event_name[6:-1]
        event_name = "".join(c if c.isprintable() else "?" for c in event_name)

        pid = sample.sample_pid
        assert self.session is not None
        try:
            thread = self.session.find_thread(pid, sample.sample_tid)
            comm = (thread.comm() if thread else None) or "unknown"
        except (TypeError, AttributeError):
            comm = "unknown"

        if comm != self.for_comm:
            return

        if event_name == "syscalls:sys_enter_read":
            try:
                fd = sample.fd
                count = sample.count
                self.reads[fd]["bytes_requested"] += count
                self.reads[fd]["total_reads"] += 1
            except AttributeError:
                self.unhandled[event_name] += 1
        elif event_name == "syscalls:sys_enter_write":
            try:
                fd = sample.fd
                count = sample.count
                self.writes[fd]["bytes_written"] += count
                self.writes[fd]["total_writes"] += 1
            except AttributeError:
                self.unhandled[event_name] += 1
        else:
            self.unhandled[event_name] += 1

    def print_totals(self) -> None:
        """Print summary tables."""
        print(f"file read counts for {self.for_comm}:\n")
        print(f"{'fd':>6s}  {'# reads':>10s}  {'bytes_requested':>15s}")
        print(f"{'-'*6}  {'-'*10}  {'-'*15}")

        for fd, data in sorted(self.reads.items(),
                               key=lambda kv: kv[1]["bytes_requested"], reverse=True):
            print(f"{fd:6d}  {data['total_reads']:10d}  {data['bytes_requested']:15d}")

        print(f"\nfile write counts for {self.for_comm}:\n")
        print(f"{'fd':>6s}  {'# writes':>10s}  {'bytes_written':>15s}")
        print(f"{'-'*6}  {'-'*10}  {'-'*15}")

        for fd, data in sorted(self.writes.items(),
                               key=lambda kv: kv[1]["bytes_written"], reverse=True):
            print(f"{fd:6d}  {data['total_writes']:10d}  {data['bytes_written']:15d}")

        if self.unhandled:
            print("\nunhandled events:\n")
            print(f"{'event':<40s}  {'count':>10s}")
            print(f"{'-'*40}  {'-'*10}")
            for event_name, count in self.unhandled.items():
                print(f"{event_name:<40s}  {count:10d}")

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
    parser = argparse.ArgumentParser(description="Trace r/w activity by file")
    parser.add_argument("comm", help="Filter by command name")
    parser.add_argument("-i", "--input", default="perf.data", help="Input file")
    args = parser.parse_args()

    analyzer = RwByFile(args.comm)
    try:
        analyzer.run(args.input)
    except IOError as e:
        print(e, file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
