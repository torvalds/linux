#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
General event handler in Python, using SQLite to analyze events.

The 2 database related functions in this script just show how to gather
the basic information, and users can modify and write their own functions
according to their specific requirement.

The first function "show_general_events" just does a basic grouping for all
generic events with the help of sqlite, and the 2nd one "show_pebs_ll" is
for a x86 HW PMU event: PEBS with load latency data.

Ported from tools/perf/scripts/python/event_analyzing_sample.py
"""
from __future__ import annotations

import argparse
import math
import sqlite3
import struct
from typing import Any
import perf

# Event types, user could add more here
EVTYPE_GENERIC  = 0
EVTYPE_PEBS     = 1     # Basic PEBS event
EVTYPE_PEBS_LL  = 2     # PEBS event with load latency info
EVTYPE_IBS      = 3

#
# Currently we don't have good way to tell the event type, but by
# the size of raw buffer, raw PEBS event with load latency data's
# size is 176 bytes, while the pure PEBS event's size is 144 bytes.
#
def create_event(name, comm, dso, symbol, raw_buf):
    """Create an event object based on raw buffer size."""
    if len(raw_buf) == 144:
        event = PebsEvent(name, comm, dso, symbol, raw_buf)
    elif len(raw_buf) == 176:
        event = PebsNHM(name, comm, dso, symbol, raw_buf)
    else:
        event = PerfEvent(name, comm, dso, symbol, raw_buf)

    return event

class PerfEvent:
    """Base class for all perf event samples."""
    event_num = 0
    def __init__(self, name, comm, dso, symbol, raw_buf, ev_type=EVTYPE_GENERIC):
        self.name       = name
        self.comm       = comm
        self.dso        = dso
        self.symbol     = symbol
        self.raw_buf    = raw_buf
        self.ev_type    = ev_type
        PerfEvent.event_num += 1

    def show(self):
        """Display PMU event info."""
        print(f"PMU event: name={self.name:12s}, symbol={self.symbol:24s}, "
              f"comm={self.comm:8s}, dso={self.dso:12s}")

#
# Basic Intel PEBS (Precise Event-based Sampling) event, whose raw buffer
# contains the context info when that event happened: the EFLAGS and
# linear IP info, as well as all the registers.
#
class PebsEvent(PerfEvent):
    """Intel PEBS event."""
    pebs_num = 0
    def __init__(self, name, comm, dso, symbol, raw_buf, ev_type=EVTYPE_PEBS):
        tmp_buf = raw_buf[0:80]
        flags, ip, ax, bx, cx, dx, si, di, bp, sp = struct.unpack('<QQQQQQQQQQ', tmp_buf)
        self.flags = flags
        self.ip    = ip
        self.ax    = ax
        self.bx    = bx
        self.cx    = cx
        self.dx    = dx
        self.si    = si
        self.di    = di
        self.bp    = bp
        self.sp    = sp

        super().__init__(name, comm, dso, symbol, raw_buf, ev_type)
        PebsEvent.pebs_num += 1
        del tmp_buf

#
# Intel Nehalem and Westmere support PEBS plus Load Latency info which lie
# in the four 64 bit words write after the PEBS data:
#       Status: records the IA32_PERF_GLOBAL_STATUS register value
#       DLA:    Data Linear Address (EIP)
#       DSE:    Data Source Encoding, where the latency happens, hit or miss
#               in L1/L2/L3 or IO operations
#       LAT:    the actual latency in cycles
#
class PebsNHM(PebsEvent):
    """Intel Nehalem/Westmere PEBS event with load latency."""
    pebs_nhm_num = 0
    def __init__(self, name, comm, dso, symbol, raw_buf, ev_type=EVTYPE_PEBS_LL):
        tmp_buf = raw_buf[144:176]
        status, dla, dse, lat = struct.unpack('<QQQQ', tmp_buf)
        self.status = status
        self.dla = dla
        self.dse = dse
        self.lat = lat

        super().__init__(name, comm, dso, symbol, raw_buf, ev_type)
        PebsNHM.pebs_nhm_num += 1
        del tmp_buf

session: Any = None

class _DB:
    con: sqlite3.Connection | None = None

def trace_begin(db_path: str | None = None) -> None:
    """Initialize database tables."""
    print("In trace_begin:\n")
    con = sqlite3.connect(db_path or ":memory:")
    try:
        # Drop any pre-existing tables so repeated runs do not accumulate duplicate events.
        con.execute("drop table if exists gen_events;")
        con.execute("drop table if exists pebs_ll;")

        # Will create several tables at the start, pebs_ll is for PEBS data with
        # load latency info, while gen_events is for general event.
        con.execute("""
            create table if not exists gen_events (
                    name text,
                    symbol text,
                    comm text,
                    dso text
            );""")
        con.execute("""
            create table if not exists pebs_ll (
                    name text,
                    symbol text,
                    comm text,
                    dso text,
                    flags integer,
                    ip integer,
                    status integer,
                    dse integer,
                    dla integer,
                    lat integer
            );""")
    except BaseException:
        con.close()
        raise
    _DB.con = con

def insert_db(event: Any) -> None:
    """Insert event into database."""
    con = _DB.con
    assert con is not None
    if event.ev_type == EVTYPE_GENERIC:
        con.execute("insert into gen_events values(?, ?, ?, ?)",
                    (event.name, event.symbol, event.comm, event.dso))
    elif event.ev_type == EVTYPE_PEBS_LL:
        # Python's sqlite3 module only accepts signed 64-bit integers (-2^63 to 2^63 - 1)
        # and raises OverflowError for unsigned 64-bit kernel pointers (> 0x7fffffffffffffff).
        # Convert unsigned 64-bit addresses to signed 64-bit two's-complement integers.
        ip = event.ip - 0x10000000000000000 if event.ip > 0x7fffffffffffffff else event.ip
        status = (event.status - 0x10000000000000000
                  if event.status > 0x7fffffffffffffff else event.status)
        dla = event.dla - 0x10000000000000000 if event.dla > 0x7fffffffffffffff else event.dla
        con.execute("insert into pebs_ll values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (event.name, event.symbol, event.comm, event.dso, event.flags,
                     ip, status, event.dse, dla, event.lat))

def process_event(sample: perf.sample_event) -> None:
    """Callback for processing events."""
    # Create and insert event object to a database so that user could
    # do more analysis with simple database commands.

    # Resolve comm, symbol, dso
    comm = "Unknown_comm"
    try:
        if session is not None:
            proc = session.find_thread(sample.sample_pid, sample.sample_tid)
            if proc:
                comm = proc.comm() or "Unknown_comm"
    except TypeError:
        pass

    # Symbol and dso info are not always resolved
    dso = sample.dso if hasattr(sample, 'dso') and sample.dso else "Unknown_dso"
    symbol = sample.symbol if hasattr(sample, 'symbol') and sample.symbol else "Unknown_symbol"
    name = str(sample.evsel)
    if name.startswith("evsel("):
        name = name[6:-1]

    # Create the event object and insert it to the right table in database
    try:
        event = create_event(name, comm, dso, symbol, sample.raw_buf)
        insert_db(event)
    except (sqlite3.Error, ValueError, TypeError, OverflowError) as e:
        print(f"Error creating/inserting event: {e}")

def num2sym(num: int) -> str:
    """Convert number to a histogram symbol (log2)."""
    # As the event number may be very big, so we can't use linear way
    # to show the histogram in real number, but use a log2 algorithm.
    if num <= 0:
        return ""
    snum = '#' * (int(math.log(num, 2)) + 1)
    return snum

def show_general_events() -> None:
    """Display statistics for general events."""
    con = _DB.con
    assert con is not None
    count = con.execute("select count(*) from gen_events")
    for t in count:
        print(f"There is {t[0]} records in gen_events table")
        if t[0] == 0:
            return

    print("Statistics about the general events grouped by thread/symbol/dso: \n")

    # Group by thread
    commq = con.execute("""
        select comm, count(comm) from gen_events
        group by comm order by -count(comm)
    """)
    print(f"\n{ 'comm':>16} {'number':>8} {'histogram':>16}\n{'='*42}")
    for row in commq:
        print(f"{row[0]:>16} {row[1]:>8}     {num2sym(row[1])}")

    # Group by symbol
    print(f"\n{'symbol':>32} {'number':>8} {'histogram':>16}\n{'='*58}")
    symbolq = con.execute("""
        select symbol, count(symbol) from gen_events
        group by symbol order by -count(symbol)
    """)
    for row in symbolq:
        print(f"{row[0]:>32} {row[1]:>8}     {num2sym(row[1])}")

    # Group by dso
    print(f"\n{'dso':>40} {'number':>8} {'histogram':>16}\n{'='*74}")
    dsoq = con.execute("select dso, count(dso) from gen_events group by dso order by -count(dso)")
    for row in dsoq:
        print(f"{row[0]:>40} {row[1]:>8}     {num2sym(row[1])}")

def show_pebs_ll() -> None:
    """Display statistics for PEBS load latency events."""
    con = _DB.con
    assert con is not None
    # This function just shows the basic info, and we could do more with the
    # data in the tables, like checking the function parameters when some
    # big latency events happen.
    count = con.execute("select count(*) from pebs_ll")
    for t in count:
        print(f"There is {t[0]} records in pebs_ll table")
        if t[0] == 0:
            return

    print("Statistics about the PEBS Load Latency events grouped by thread/symbol/dse/latency: \n")

    # Group by thread
    commq = con.execute("select comm, count(comm) from pebs_ll group by comm order by -count(comm)")
    print(f"\n{'comm':>16} {'number':>8} {'histogram':>16}\n{'='*42}")
    for row in commq:
        print(f"{row[0]:>16} {row[1]:>8}     {num2sym(row[1])}")

    # Group by symbol
    print(f"\n{'symbol':>32} {'number':>8} {'histogram':>16}\n{'='*58}")
    symbolq = con.execute("""
        select symbol, count(symbol) from pebs_ll
        group by symbol order by -count(symbol)
    """)
    for row in symbolq:
        print(f"{row[0]:>32} {row[1]:>8}     {num2sym(row[1])}")

    # Group by dse
    dseq = con.execute("select dse, count(dse) from pebs_ll group by dse order by -count(dse)")
    print(f"\n{'dse':>32} {'number':>8} {'histogram':>16}\n{'='*58}")
    for row in dseq:
        print(f"{row[0]:>32} {row[1]:>8}     {num2sym(row[1])}")

    # Group by latency
    latq = con.execute("select lat, count(lat) from pebs_ll group by lat order by lat")
    print(f"\n{'latency':>32} {'number':>8} {'histogram':>16}\n{'='*58}")
    for row in latq:
        print(f"{str(row[0]):>32} {row[1]:>8}     {num2sym(row[1])}")

def trace_end() -> None:
    """Called at the end of trace processing."""
    print("In trace_end:\n")
    if _DB.con:
        try:
            _DB.con.commit()
            show_general_events()
            show_pebs_ll()
        finally:
            _DB.con.close()
            _DB.con = None

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Analyze events with SQLite")
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    ap.add_argument("-d", "--db", "--database", dest="database", default=None,
                    help="Database file name (defaults to an in-memory database)")
    args = ap.parse_args()

    try:
        trace_begin(args.database)
        session = perf.session(perf.data(args.input), sample=process_event)
        session.process_events()
    finally:
        # Break the reference cycle between session and process_event (whose module
        # globals reference session) so the C session object is deterministically freed.
        session = None
        trace_end()
