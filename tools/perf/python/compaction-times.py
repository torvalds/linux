#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Report time spent in memory compaction.

Memory compaction is a feature in the Linux kernel that defragments memory
by moving used pages to create larger contiguous blocks of free memory. This
is particularly useful for allocating huge pages.

This script processes trace events related to memory compaction and reports:
- Total time spent in compaction (stall time).
- Statistics for page migration (moved vs. failed).
- Statistics for the free scanner (scanned vs. isolated pages).
- Statistics for the migration scanner (scanned vs. isolated pages).

Definitions:
- **Compaction**: Defragmenting memory by moving allocated pages.
- **Migration**: Moving pages from their current location to free pages found by the free scanner.
- **Free Scanner**: Scans memory (typically from the end of a zone) to find free pages.
- **Migration Scanner**: Scans memory (typically from the beginning of a zone)
  to find pages to move.
- **Isolated Pages**: Pages that have been temporarily removed from the buddy
  system for migration or as migration targets.

Ported from tools/perf/scripts/python/compaction-times.py to the modern perf Python module.
"""
from __future__ import annotations

import argparse
import enum
import re
import signal
import sys
from typing import Callable, Dict, List, Optional, Any
import perf

if hasattr(signal, "SIGPIPE"):
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)

class Popt(enum.IntEnum):
    """Process display options."""
    DISP_DFL = 0
    DISP_PROC = 1
    DISP_PROC_VERBOSE = 2

class Topt(enum.IntFlag):
    """Trace display options."""
    DISP_TIME = 0
    DISP_MIG = 1
    DISP_ISOLFREE = 2
    DISP_ISOLMIG = 4
    DISP_ALL = DISP_MIG | DISP_ISOLFREE | DISP_ISOLMIG

# Globals to satisfy pylint when accessed in functions before assignment in main.
OPT_NS = True
opt_disp = Topt.DISP_ALL
opt_proc = Popt.DISP_DFL
session = None

def get_comm_filter(regex: re.Pattern) -> Callable[[int, str], bool]:
    """Returns a filter function based on command regex."""
    def filter_func(_pid: int, comm: str) -> bool:
        regex_match = regex.search(comm)
        return regex_match is None or regex_match.group() == ""
    return filter_func

def get_pid_filter(low_str: str, high_str: str) -> Callable[[int, str], bool]:
    """Returns a filter function based on PID range."""
    low = 0 if low_str == "" else int(low_str)
    high = None if high_str == "" else int(high_str)

    def filter_func(pid: int, _comm: str) -> bool:
        return not (pid >= low and (high is None or pid <= high))
    return filter_func

def ns_to_time(ns: int) -> str:
    """Format nanoseconds to string based on options."""
    return f"{ns}ns" if OPT_NS else f"{round(ns, -3) // 1000}us"

class Pair:
    """Represents a pair of related counters (e.g., scanned vs isolated, moved vs failed)."""
    def __init__(self, aval: int, bval: int,
                 alabel: Optional[str] = None, blabel: Optional[str] = None):
        self.alabel = alabel
        self.blabel = blabel
        self.aval = aval
        self.bval = bval

    def __add__(self, rhs: 'Pair') -> 'Pair':
        return Pair(self.aval + rhs.aval, self.bval + rhs.bval, self.alabel, self.blabel)

    def __iadd__(self, rhs: 'Pair') -> 'Pair':
        self.aval += rhs.aval
        self.bval += rhs.bval
        return self

    def __str__(self) -> str:
        return f"{self.alabel}={self.aval} {self.blabel}={self.bval}"

class Cnode:
    """Holds statistics for a single compaction event or an aggregated set of events."""
    def __init__(self, ns: int):
        self.ns = ns
        self.migrated = Pair(0, 0, "moved", "failed")
        self.fscan = Pair(0, 0, "scanned", "isolated")
        self.mscan = Pair(0, 0, "scanned", "isolated")

    def __add__(self, rhs: 'Cnode') -> 'Cnode':
        res = Cnode(self.ns + rhs.ns)
        res.migrated = self.migrated + rhs.migrated
        res.fscan = self.fscan + rhs.fscan
        res.mscan = self.mscan + rhs.mscan
        return res

    def __iadd__(self, rhs: 'Cnode') -> 'Cnode':
        self.ns += rhs.ns
        self.migrated += rhs.migrated
        self.fscan += rhs.fscan
        self.mscan += rhs.mscan
        return self

    def __str__(self) -> str:
        prev = False
        s = f"{ns_to_time(self.ns)} "
        if opt_disp & Topt.DISP_MIG:
            s += f"migration: {self.migrated}"
            prev = True
        if opt_disp & Topt.DISP_ISOLFREE:
            s += f"{' ' if prev else ''}free_scanner: {self.fscan}"
            prev = True
        if opt_disp & Topt.DISP_ISOLMIG:
            s += f"{' ' if prev else ''}migration_scanner: {self.mscan}"
        return s

    def complete(self, secs: int, nsecs: int) -> None:
        """Complete the node with duration."""
        self.ns = (secs * 1000000000 + nsecs) - self.ns

    def increment(self, migrated: Optional[Pair], fscan: Optional[Pair],
                  mscan: Optional[Pair]) -> None:
        """Increment statistics."""
        if migrated is not None:
            self.migrated += migrated
        if fscan is not None:
            self.fscan += fscan
        if mscan is not None:
            self.mscan += mscan

class Chead:
    """Aggregates compaction statistics per process (PID) and maintains total statistics."""
    heads: Dict[int, 'Chead'] = {}
    val = Cnode(0)
    fobj: Optional[Any] = None

    @classmethod
    def add_filter(cls, fobj: Any) -> None:
        """Add a filter object."""
        cls.fobj = fobj

    @classmethod
    def create_pending(cls, pid: int, comm: str, start_secs: int, start_nsecs: int) -> None:
        """Create a pending node for a process."""
        filtered = False
        try:
            head = cls.heads[pid]
            filtered = head.is_filtered()
        except KeyError:
            if cls.fobj is not None:
                filtered = cls.fobj(pid, comm)
            head = cls.heads[pid] = Chead(comm, pid, filtered)

        if not filtered:
            head.mark_pending(start_secs, start_nsecs)

    @classmethod
    def increment_pending(cls, pid: int, migrated: Optional[Pair],
                          fscan: Optional[Pair], mscan: Optional[Pair]) -> None:
        """Increment pending stats for a process."""
        if pid not in cls.heads:
            return
        head = cls.heads[pid]
        if not head.is_filtered():
            if head.is_pending():
                head.do_increment(migrated, fscan, mscan)
            else:
                sys.stderr.write(f"missing start compaction event for pid {pid}\n")

    @classmethod
    def complete_pending(cls, pid: int, secs: int, nsecs: int) -> None:
        """Complete pending stats for a process."""
        if pid not in cls.heads:
            return
        head = cls.heads[pid]
        if not head.is_filtered():
            if head.is_pending():
                head.make_complete(secs, nsecs)
            else:
                sys.stderr.write(f"missing start compaction event for pid {pid}\n")

    @classmethod
    def gen(cls):
        """Generate heads for display."""
        if opt_proc != Popt.DISP_DFL:
            yield from cls.heads.values()

    @classmethod
    def get_total(cls) -> Cnode:
        """Get total statistics."""
        return cls.val

    def __init__(self, comm: str, pid: int, filtered: bool):
        self.comm = comm
        self.pid = pid
        self.val = Cnode(0)
        self.pending: Optional[Cnode] = None
        self.filtered = filtered
        self.list: List[Cnode] = []

    def mark_pending(self, secs: int, nsecs: int) -> None:
        """Mark node as pending."""
        self.pending = Cnode(secs * 1000000000 + nsecs)

    def do_increment(self, migrated: Optional[Pair], fscan: Optional[Pair],
                     mscan: Optional[Pair]) -> None:
        """Increment pending stats."""
        if self.pending is not None:
            self.pending.increment(migrated, fscan, mscan)

    def make_complete(self, secs: int, nsecs: int) -> None:
        """Make pending stats complete."""
        if self.pending is not None:
            self.pending.complete(secs, nsecs)
            Chead.val += self.pending

            if opt_proc != Popt.DISP_DFL:
                self.val += self.pending

                if opt_proc == Popt.DISP_PROC_VERBOSE:
                    self.list.append(self.pending)
            self.pending = None

    def enumerate(self) -> None:
        """Enumerate verbose stats."""
        if opt_proc == Popt.DISP_PROC_VERBOSE and not self.is_filtered():
            for i, pelem in enumerate(self.list):
                sys.stdout.write(f"{self.pid}[{self.comm}].{i+1}: {pelem}\n")

    def is_pending(self) -> bool:
        """Check if node is pending."""
        return self.pending is not None

    def is_filtered(self) -> bool:
        """Check if node is filtered."""
        return self.filtered

    def display(self) -> None:
        """Display stats."""
        if not self.is_filtered():
            sys.stdout.write(f"{self.pid}[{self.comm}]: {self.val}\n")

def trace_end() -> None:
    """Called at the end of trace processing."""
    sys.stdout.write(f"total: {Chead.get_total()}\n")
    for i in Chead.gen():
        i.display()
        i.enumerate()

def process_event(sample: perf.sample_event) -> None:
    """Callback for processing events."""
    event_name = str(sample.evsel)
    pid = sample.sample_tid
    comm = "[unknown]"
    try:
        if session:
            thread = session.find_thread(sample.sample_pid, pid)
            if thread:
                comm = thread.comm() or "[unknown]"
    except (TypeError, AttributeError):
        pass
    secs = sample.sample_time // 1000000000
    nsecs = sample.sample_time % 1000000000

    if "evsel(compaction:mm_compaction_begin)" in event_name:
        Chead.create_pending(pid, comm, secs, nsecs)
    elif "evsel(compaction:mm_compaction_end)" in event_name:
        Chead.complete_pending(pid, secs, nsecs)
    elif "evsel(compaction:mm_compaction_migratepages)" in event_name:
        nr_migrated = getattr(sample, "nr_migrated", 0)
        nr_failed = getattr(sample, "nr_failed", 0)
        Chead.increment_pending(pid, Pair(nr_migrated, nr_failed), None, None)
    elif "evsel(compaction:mm_compaction_isolate_freepages)" in event_name:
        nr_scanned = getattr(sample, "nr_scanned", 0)
        nr_taken = getattr(sample, "nr_taken", 0)
        Chead.increment_pending(pid, None, Pair(nr_scanned, nr_taken), None)
    elif "evsel(compaction:mm_compaction_isolate_migratepages)" in event_name:
        nr_scanned = getattr(sample, "nr_scanned", 0)
        nr_taken = getattr(sample, "nr_taken", 0)
        Chead.increment_pending(pid, None, None, Pair(nr_scanned, nr_taken))

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Report time spent in compaction")
    ap.add_argument("-p", action="store_true", help="display by process")
    ap.add_argument("-pv", action="store_true", help="display by process (verbose)")
    ap.add_argument("-u", action="store_true", help="display results in microseconds")
    ap.add_argument("-t", action="store_true", help="display stall times only")
    ap.add_argument("-m", action="store_true", help="display stats for migration")
    ap.add_argument("-fs", action="store_true", help="display stats for free scanner")
    ap.add_argument("-ms", action="store_true", help="display stats for migration scanner")
    ap.add_argument("filter", nargs="?", help="pid|pid-range|comm-regex")
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    args = ap.parse_args()

    opt_proc = Popt.DISP_DFL
    if args.pv:
        opt_proc = Popt.DISP_PROC_VERBOSE
    elif args.p:
        opt_proc = Popt.DISP_PROC

    OPT_NS = not args.u

    opt_disp = Topt.DISP_ALL
    if args.t or args.m or args.fs or args.ms:
        opt_disp = Topt(0)
        if args.t:
            opt_disp |= Topt.DISP_TIME
        if args.m:
            opt_disp |= Topt.DISP_MIG
        if args.fs:
            opt_disp |= Topt.DISP_ISOLFREE
        if args.ms:
            opt_disp |= Topt.DISP_ISOLMIG

    if args.filter:
        PID_PATTERN = r"^(\d*)-(\d*)$|^(\d*)$"
        pid_re = re.compile(PID_PATTERN)
        match = pid_re.search(args.filter)
        filter_obj: Any = None
        if match is not None and match.group() != "":
            if match.group(3) is not None:
                filter_obj = get_pid_filter(match.group(3), match.group(3))
            else:
                filter_obj = get_pid_filter(match.group(1), match.group(2))
        else:
            try:
                comm_re = re.compile(args.filter)
            except re.error:
                sys.stderr.write(f"invalid regex '{args.filter}'\n")
                sys.exit(1)
            filter_obj = get_comm_filter(comm_re)
        Chead.add_filter(filter_obj)

    try:
        session = perf.session(perf.data(args.input), sample=process_event)
        try:
            session.process_events()
        except KeyboardInterrupt:
            pass
        trace_end()
    finally:
        # Break the reference cycle between the global session and the
        # process_event callback so the C perf.session object is freed.
        session = None
