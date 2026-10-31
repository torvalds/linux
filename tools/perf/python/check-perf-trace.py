#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Basic test of Python scripting support for perf.
Ported from tools/perf/scripts/python/check-perf-trace.py
"""
from __future__ import annotations

import argparse
import collections
import perf

unhandled: collections.defaultdict[str, int] = collections.defaultdict(int)
session = None

softirq_vecs = {
    0: "HI_SOFTIRQ",
    1: "TIMER_SOFTIRQ",
    2: "NET_TX_SOFTIRQ",
    3: "NET_RX_SOFTIRQ",
    4: "BLOCK_SOFTIRQ",
    5: "IRQ_POLL_SOFTIRQ",
    6: "TASKLET_SOFTIRQ",
    7: "SCHED_SOFTIRQ",
    8: "HRTIMER_SOFTIRQ",
    9: "RCU_SOFTIRQ",
}

_GFP_DMA = 1 << 0
_GFP_HIGHMEM = 1 << 1
_GFP_DMA32 = 1 << 2
_GFP_MOVABLE = 1 << 3
_GFP_RECLAIMABLE = 1 << 4
_GFP_HIGH = 1 << 5
_GFP_IO = 1 << 6
_GFP_FS = 1 << 7
_GFP_ZERO = 1 << 8
_GFP_DIRECT_RECLAIM = 1 << 10
_GFP_KSWAPD_RECLAIM = 1 << 11
_GFP_WRITE = 1 << 12
_GFP_NOWARN = 1 << 13
_GFP_RETRY_MAYFAIL = 1 << 14
_GFP_NOFAIL = 1 << 15
_GFP_NORETRY = 1 << 16
_GFP_MEMALLOC = 1 << 17
_GFP_COMP = 1 << 18
_GFP_NOMEMALLOC = 1 << 19
_GFP_HARDWALL = 1 << 20
_GFP_THISNODE = 1 << 21
_GFP_ACCOUNT = 1 << 22
_GFP_ZEROTAGS = 1 << 23

_GFP_RECLAIM = _GFP_DIRECT_RECLAIM | _GFP_KSWAPD_RECLAIM
_GFP_KERNEL = _GFP_RECLAIM | _GFP_IO | _GFP_FS
_GFP_USER = _GFP_KERNEL | _GFP_HARDWALL
_GFP_HIGHUSER = _GFP_USER | _GFP_HIGHMEM
_GFP_HIGHUSER_MOVABLE = _GFP_HIGHUSER | _GFP_MOVABLE
_GFP_TRANSHUGE_LIGHT = (
    _GFP_HIGHUSER_MOVABLE | _GFP_COMP | _GFP_NOMEMALLOC | _GFP_NOWARN
) & ~_GFP_RECLAIM
_GFP_TRANSHUGE = _GFP_TRANSHUGE_LIGHT | _GFP_DIRECT_RECLAIM

GFP_FLAG_NAMES = [
    (_GFP_TRANSHUGE, "GFP_TRANSHUGE"),
    (_GFP_TRANSHUGE_LIGHT, "GFP_TRANSHUGE_LIGHT"),
    (_GFP_HIGHUSER_MOVABLE, "GFP_HIGHUSER_MOVABLE"),
    (_GFP_HIGHUSER, "GFP_HIGHUSER"),
    (_GFP_USER, "GFP_USER"),
    (_GFP_KERNEL | _GFP_ACCOUNT, "GFP_KERNEL_ACCOUNT"),
    (_GFP_KERNEL, "GFP_KERNEL"),
    (_GFP_RECLAIM | _GFP_IO, "GFP_NOFS"),
    (_GFP_HIGH | _GFP_KSWAPD_RECLAIM, "GFP_ATOMIC"),
    (_GFP_RECLAIM, "GFP_NOIO"),
    (_GFP_KSWAPD_RECLAIM | _GFP_NOWARN, "GFP_NOWAIT"),
    (_GFP_DMA, "GFP_DMA"),
    (_GFP_DMA32, "GFP_DMA32"),
    (_GFP_RECLAIM, "__GFP_RECLAIM"),
    (_GFP_DMA, "__GFP_DMA"),
    (_GFP_HIGHMEM, "__GFP_HIGHMEM"),
    (_GFP_DMA32, "__GFP_DMA32"),
    (_GFP_MOVABLE, "__GFP_MOVABLE"),
    (_GFP_RECLAIMABLE, "__GFP_RECLAIMABLE"),
    (_GFP_HIGH, "__GFP_HIGH"),
    (_GFP_IO, "__GFP_IO"),
    (_GFP_FS, "__GFP_FS"),
    (_GFP_ZERO, "__GFP_ZERO"),
    (_GFP_DIRECT_RECLAIM, "__GFP_DIRECT_RECLAIM"),
    (_GFP_KSWAPD_RECLAIM, "__GFP_KSWAPD_RECLAIM"),
    (_GFP_WRITE, "__GFP_WRITE"),
    (_GFP_NOWARN, "__GFP_NOWARN"),
    (_GFP_RETRY_MAYFAIL, "__GFP_RETRY_MAYFAIL"),
    (_GFP_NOFAIL, "__GFP_NOFAIL"),
    (_GFP_NORETRY, "__GFP_NORETRY"),
    (_GFP_MEMALLOC, "__GFP_MEMALLOC"),
    (_GFP_COMP, "__GFP_COMP"),
    (_GFP_NOMEMALLOC, "__GFP_NOMEMALLOC"),
    (_GFP_HARDWALL, "__GFP_HARDWALL"),
    (_GFP_THISNODE, "__GFP_THISNODE"),
    (_GFP_ACCOUNT, "__GFP_ACCOUNT"),
    (_GFP_ZEROTAGS, "__GFP_ZEROTAGS"),
]


def trace_begin() -> None:
    """Called at the start of trace processing."""
    print("in trace_begin")

def trace_end() -> None:
    """Called at the end of trace processing."""
    print_unhandled()
    print("in trace_end")

def symbol_str(event_name: str, field_name: str, value: int) -> str:
    """Resolves symbol values to strings."""
    # Note: The standalone Python API currently lacks dynamic libtraceevent
    # formatting (equivalent to _perf_trace_context.symbol_str())
    if event_name == "irq__softirq_entry" and field_name == "vec":
        return softirq_vecs.get(value, str(value))
    return str(value)

def flag_str(event_name: str, field_name: str, value: int) -> str:
    """Resolves flag values to strings."""
    # Note: The standalone Python API currently lacks dynamic libtraceevent
    # formatting (equivalent to _perf_trace_context.flag_str())
    if event_name == "kmem__kmalloc" and field_name == "gfp_flags":
        if value == 0:
            return "none"
        names = []
        rem = value
        for mask, name in GFP_FLAG_NAMES:
            if (rem & mask) == mask:
                names.append(name)
                rem &= ~mask
        if rem:
            names.append(f"0x{rem:x}")
        return "|".join(names)
    return str(value)

def print_header(event_name: str, sample: perf.sample_event) -> None:
    """Prints common header for events."""
    secs = sample.sample_time // 1000000000
    nsecs = sample.sample_time % 1000000000
    comm = "[unknown]"
    try:
        if session:
            thread = session.find_thread(sample.sample_pid, sample.sample_tid)
            if thread:
                comm = thread.comm() or "[unknown]"
    except (TypeError, AttributeError):
        pass
    print(f"{event_name:<20} {sample.sample_cpu:5} {secs:05}.{nsecs:09} "
          f"{sample.sample_tid:8} {comm:<20} ", end=' ')

def print_uncommon(sample: perf.sample_event) -> None:
    """Prints uncommon fields for tracepoints."""
    # Fallback to 0 if field not found (e.g. on older kernels or if not tracepoint)
    pc = getattr(sample, 'common_preempt_count', 0)
    flags = getattr(sample, 'common_flags', 0)
    lock_depth = getattr(sample, 'common_lock_depth', 0)

    print(f"common_preempt_count={pc}, common_flags={flags}, "
          f"common_lock_depth={lock_depth}, ", end='')

def irq__softirq_entry(sample: perf.sample_event) -> None:
    """Handles irq:softirq_entry events."""
    print_header("irq__softirq_entry", sample)
    print_uncommon(sample)
    print(f"vec={symbol_str('irq__softirq_entry', 'vec', getattr(sample, 'vec', 0))}")

def kmem__kmalloc(sample: perf.sample_event) -> None:
    """Handles kmem:kmalloc events."""
    print_header("kmem__kmalloc", sample)
    print_uncommon(sample)

    print(f"call_site={getattr(sample, 'call_site', 0):#x}, "
          f"ptr={getattr(sample, 'ptr', 0):#x}, "
          f"bytes_req={getattr(sample, 'bytes_req', 0):d}, "
          f"bytes_alloc={getattr(sample, 'bytes_alloc', 0):d}, "
          f"gfp_flags={flag_str('kmem__kmalloc', 'gfp_flags', getattr(sample, 'gfp_flags', 0))}")

def trace_unhandled(event_name: str) -> None:
    """Tracks unhandled events."""
    unhandled[event_name] += 1

def print_unhandled() -> None:
    """Prints summary of unhandled events."""
    if not unhandled:
        return
    print("\nunhandled events:\n")
    print(f"{'event':<40} {'count':>10}")
    print("---------------------------------------- -----------")
    for event_name, count in unhandled.items():
        print(f"{event_name:<40} {count:10}")

def process_event(sample: perf.sample_event) -> None:
    """Callback for processing events."""
    event_name = str(sample.evsel)
    if event_name.startswith("evsel(") and event_name.endswith(")"):
        event_name = event_name[6:-1]
    if event_name.startswith("irq:softirq_entry"):
        irq__softirq_entry(sample)
    elif event_name.startswith("kmem:kmalloc"):
        kmem__kmalloc(sample)
    else:
        trace_unhandled(event_name)

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    args = ap.parse_args()

    trace_begin()
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
