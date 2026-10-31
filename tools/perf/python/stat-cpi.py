#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Calculate CPI from perf stat data or live."""
from __future__ import annotations

import argparse
import os
import signal
import sys
import time
from typing import Any, Optional
import perf

class StatCpiAnalyzer:
    """Accumulates cycles and instructions and calculates CPI."""

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.data: dict[str, float] = {}
        self.prev_data: dict[str, tuple[int, int, int]] = {}
        self.recorded_pairs: set[tuple[int, int]] = set()

    def get_key(self, event: str, cpu: int, thread: int) -> str:
        """Get key for data dictionary."""
        return f"{event}-{cpu}-{thread}"

    def store_key(self, cpu: int, thread: int) -> None:
        """Store CPU and thread IDs."""
        self.recorded_pairs.add((cpu, thread))

    def store(self, event: str, cpu: int, thread: int,
              counts: tuple[int, int, int], is_delta: bool = False,
              raw_name: Optional[str] = None) -> None:
        """Store counter values, computing difference from previous
        absolute values if not already deltas."""
        self.store_key(cpu, thread)
        key = self.get_key(event, cpu, thread)
        prev_key = self.get_key(raw_name or event, cpu, thread)

        val, ena, run = counts
        if is_delta:
            # counts are already deltas
            cur_val = val
            cur_ena = ena
            cur_run = run
        else:
            if prev_key in self.prev_data:
                prev_val, prev_ena, prev_run = self.prev_data[prev_key]
                cur_val = val - prev_val
                cur_ena = ena - prev_ena
                cur_run = run - prev_run
            else:
                cur_val = val
                cur_ena = ena
                cur_run = run
            self.prev_data[prev_key] = counts  # Store absolute value for next time

        # Scale each raw event's delta by its own multiplexing ratio before
        # summing across PMUs (e.g. cpu_core and cpu_atom on hybrid systems) so
        # enabled time from an idle PMU does not inflate the multiplier of an
        # active PMU.
        scaled_val = cur_val * (cur_ena / float(cur_run)) if cur_run > 0 else float(cur_val)
        self.data[key] = self.data.get(key, 0.0) + scaled_val

    def get(self, event: str, cpu: int, thread: int) -> float:
        """Get scaled counter value."""
        key = self.get_key(event, cpu, thread)
        return self.data.get(key, 0.0)

    @staticmethod
    def _classify_event(name: str) -> Optional[str]:
        """Classify an event name as 'cycles' or 'instructions'."""
        ev = name[6:-1] if name.startswith("evsel(") and name.endswith(")") else name
        ev = ev.split(":", 1)[0]
        if "/" in ev:
            parts = [p for p in ev.split("/") if p]
            if len(parts) >= 2:
                ev = parts[1]
        if ev in ("cycles", "cpu-cycles"):
            return "cycles"
        if ev == "instructions":
            return "instructions"
        return None

    def process_stat_event(self, event: Any, name: Optional[str] = None) -> None:
        """Process PERF_RECORD_STAT and PERF_RECORD_STAT_ROUND events."""
        if event.type == perf.RECORD_STAT:
            if name:
                event_name = self._classify_event(name)
                if not event_name:
                    return
                self.store(event_name, event.cpu, event.thread,
                           (event.val, event.ena, event.run), raw_name=name)
        elif event.type == perf.RECORD_STAT_ROUND:
            timestamp = getattr(event, "time", 0)
            self.print_interval(timestamp)
            self.data.clear()
            self.recorded_pairs.clear()

    def print_interval(self, timestamp: int) -> None:
        """Print CPI for the current interval."""
        for cpu, thread in sorted(self.recorded_pairs):
            cyc = self.get("cycles", cpu, thread)
            ins = self.get("instructions", cpu, thread)
            cpi = 0.0
            if ins != 0:
                cpi = cyc / float(ins)
            t_sec = timestamp / 1000000000.0
            print(f"{t_sec:15f}: cpu {cpu}, thread {thread} -> cpi {cpi:f} ({cyc:.0f}/{ins:.0f})",
                  flush=True)

    def read_counters(self, evlist: Any) -> None:
        """Read counters live."""
        for evsel in evlist:
            name = str(evsel)
            event_name = self._classify_event(name)
            if not event_name:
                continue

            for cpu in evsel.cpus():
                for thread in evsel.threads():
                    try:
                        counts = evsel.read(cpu, thread)
                        self.store(event_name, cpu, thread,
                                   (counts.val, counts.ena, counts.run),
                                   is_delta=True, raw_name=name)
                    except OSError:
                        pass

    def run_file(self) -> None:
        """Process events from file."""
        session: Optional[perf.session] = perf.session(
            perf.data(self.args.input), stat=self.process_stat_event
        )
        try:
            assert session is not None
            session.process_events()
        finally:
            session = None

    def _open_live_evlist(self) -> Any:
        """Open evlist for live mode, falling back to user-space or process scope on EACCES."""
        threads = perf.thread_map(self.args.pid) if self.args.pid else None
        candidates = [
            ("cycles,instructions", threads),
            ("cycles:u,instructions:u", threads),
        ]
        if threads is None:
            self_threads = perf.thread_map(os.getpid())
            candidates.append(("cycles,instructions", self_threads))
            candidates.append(("cycles:u,instructions:u", self_threads))

        last_err: Optional[OSError] = None
        for events, tmap in candidates:
            evlist = None
            try:
                evlist = perf.parse_events(events, None, tmap)
                for evsel in evlist:
                    evsel.read_format |= (
                        perf.FORMAT_TOTAL_TIME_ENABLED | perf.FORMAT_TOTAL_TIME_RUNNING
                    )
                evlist.open()
                evlist.enable()
                return evlist
            except PermissionError as e:
                if evlist is not None:
                    evlist.close()
                last_err = e
            except OSError as e:
                if evlist is not None:
                    evlist.close()
                if e.errno == 13:
                    last_err = e
                else:
                    raise
            except BaseException:
                if evlist is not None:
                    evlist.close()
                raise
        if last_err is not None:
            raise last_err
        raise RuntimeError("Failed to open events")

    def run_live(self) -> None:
        """Read counters live."""
        def handle_signal(_signum: int, _frame: Any) -> None:
            raise KeyboardInterrupt

        signal.signal(signal.SIGINT, signal.default_int_handler)
        signal.signal(signal.SIGTERM, handle_signal)

        evlist = None
        try:
            try:
                evlist = self._open_live_evlist()
            except OSError as e:
                print(f"Failed to open events: {e}", file=sys.stderr)
                sys.exit(1)

            print("Live mode started. Press Ctrl+C to stop.", flush=True)
            while True:
                time.sleep(self.args.interval)
                timestamp = time.time_ns()
                self.read_counters(evlist)
                self.print_interval(timestamp)
                self.data.clear()
                self.recorded_pairs.clear()
        except KeyboardInterrupt:
            print("\nStopped.", flush=True)
        finally:
            if evlist is not None:
                evlist.close()

def main() -> None:
    """Main function."""
    ap = argparse.ArgumentParser(description="Calculate CPI from perf stat data or live")
    ap.add_argument("-i", "--input", help="Input file name (enables file mode)")
    ap.add_argument("-I", "--interval", type=float, default=1.0,
                    help="Interval in seconds for live mode")
    ap.add_argument("-p", "--pid", type=int,
                    help="Monitor specific process ID in live mode")
    args = ap.parse_args()

    analyzer = StatCpiAnalyzer(args)
    if args.input:
        analyzer.run_file()
    else:
        analyzer.run_live()

if __name__ == "__main__":
    main()
