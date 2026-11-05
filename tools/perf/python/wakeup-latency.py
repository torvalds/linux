#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Display avg/min/max wakeup latency."""
from __future__ import annotations

import argparse
from collections import defaultdict
import sys
from typing import Optional, Dict
import perf

class WakeupLatency:
    """Tracks and displays wakeup latency statistics."""
    def __init__(self) -> None:
        self.last_wakeup: Dict[int, int] = defaultdict(int)
        self.max_wakeup_latency: int = 0
        self.min_wakeup_latency: Optional[int] = None
        self.total_wakeup_latency = 0
        self.total_wakeups = 0
        self.unhandled: Dict[str, int] = defaultdict(int)
        self.session: Optional[perf.session] = None

    def process_event(self, sample: perf.sample_event) -> None:
        """Process events."""
        event_name = str(sample.evsel)
        if event_name.startswith("evsel(") and event_name.endswith(")"):
            event_name = event_name[6:-1]
        sample_time = sample.sample_time

        if "sched:sched_wakeup" in event_name:
            try:
                pid = sample.pid
                self.last_wakeup[pid] = sample_time
            except AttributeError:
                self.unhandled[event_name] += 1
        elif "sched:sched_switch" in event_name:
            try:
                next_pid = sample.next_pid
                wakeup_ts = self.last_wakeup.get(next_pid, 0)
                if wakeup_ts:
                    latency = sample_time - wakeup_ts
                    self.max_wakeup_latency = max(self.max_wakeup_latency, latency)
                    if self.min_wakeup_latency is None:
                        self.min_wakeup_latency = latency
                    else:
                        self.min_wakeup_latency = min(self.min_wakeup_latency, latency)
                    self.total_wakeup_latency += latency
                    self.total_wakeups += 1
                    del self.last_wakeup[next_pid]
            except AttributeError:
                self.unhandled[event_name] += 1
        else:
            self.unhandled[event_name] += 1

    def print_totals(self) -> None:
        """Print summary statistics."""
        print("wakeup_latency stats:\n")
        print(f"total_wakeups: {self.total_wakeups}")
        if self.total_wakeups:
            avg = self.total_wakeup_latency // self.total_wakeups
            print(f"avg_wakeup_latency (ns): {avg}")
            print(f"min_wakeup_latency (ns): {self.min_wakeup_latency}")
            print(f"max_wakeup_latency (ns): {self.max_wakeup_latency}")
        else:
            print("avg_wakeup_latency (ns): N/A")
            print("min_wakeup_latency (ns): N/A")
            print("max_wakeup_latency (ns): N/A")

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
        except KeyboardInterrupt:
            pass
        finally:
            # Break the reference cycle between self.session and the bound
            # self.process_event callback so the C perf.session object is freed.
            self.session = None
        self.print_totals()

def main() -> None:
    """Main function."""
    parser = argparse.ArgumentParser(description="Trace wakeup latency")
    parser.add_argument("-i", "--input", default="perf.data", help="Input file")
    args = parser.parse_args()

    analyzer = WakeupLatency()
    try:
        analyzer.run(args.input)
    except IOError as e:
        print(e, file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
