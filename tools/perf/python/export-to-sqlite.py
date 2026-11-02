#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Export perf data to a sqlite3 database.

This script has been ported to use the modern perf Python module and the
standard library sqlite3 module. It no longer requires PySide2 or QtSql
for exporting.

Examples of using this script with Intel PT:

	$ perf record -e intel_pt//u ls
	$ python export-to-sqlite.py -i perf.data -o pt_example

To browse the database, sqlite3 can be used e.g.

	$ sqlite3 pt_example
	sqlite> .header on
	sqlite> select * from samples_view where id < 10;
	sqlite> .mode column
	sqlite> select * from samples_view where id < 10;
	sqlite> .tables
	sqlite> .schema samples_view
	sqlite> .quit

An example of using the database is provided by the script
exported-sql-viewer.py. Refer to that script for details.

Ported from tools/perf/scripts/python/export-to-sqlite.py
"""

from __future__ import annotations
import typing
import argparse
import os
import shutil
import sqlite3
import struct
import sys
import tempfile
from typing import Dict, Optional
import perf


PERF_IP_FLAG_BRANCH = 1 << 0
PERF_IP_FLAG_CALL = 1 << 1
PERF_IP_FLAG_RETURN = 1 << 2
PERF_IP_FLAG_CONDITIONAL = 1 << 3
PERF_IP_FLAG_SYSCALLRET = 1 << 4
PERF_IP_FLAG_ASYNC = 1 << 5
PERF_IP_FLAG_INTERRUPT = 1 << 6
PERF_IP_FLAG_TX_ABORT = 1 << 7
PERF_IP_FLAG_TRACE_BEGIN = 1 << 8
PERF_IP_FLAG_TRACE_END = 1 << 9
PERF_IP_FLAG_IN_TX = 1 << 10
PERF_IP_FLAG_VMENTRY = 1 << 11
PERF_IP_FLAG_VMEXIT = 1 << 12

BRANCH_TYPES = [
    (0, "no branch"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL, "call"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN, "return"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CONDITIONAL, "conditional jump"),
    (PERF_IP_FLAG_BRANCH, "unconditional jump"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_INTERRUPT, "software interrupt"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN | PERF_IP_FLAG_INTERRUPT, "return from interrupt"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_SYSCALLRET, "system call"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN | PERF_IP_FLAG_SYSCALLRET,
     "return from system call"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_ASYNC, "asynchronous branch"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_ASYNC | PERF_IP_FLAG_INTERRUPT,
     "hardware interrupt"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TX_ABORT, "transaction abort"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TRACE_BEGIN, "trace begin"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TRACE_END, "trace end"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_VMENTRY, "vm entry"),
    (PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_VMEXIT, "vm exit"),
]
for _b_type, _b_name in list(BRANCH_TYPES):
    if (_b_type == PERF_IP_FLAG_BRANCH or
            (_b_type & (PERF_IP_FLAG_TRACE_BEGIN | PERF_IP_FLAG_TRACE_END))):
        continue
    BRANCH_TYPES.append((_b_type | PERF_IP_FLAG_TRACE_BEGIN, f"trace begin / {_b_name}"))
    BRANCH_TYPES.append((_b_type | PERF_IP_FLAG_TRACE_END, f"{_b_name} / trace end"))


class DatabaseExporter:
    """Handles database connection and exporting of perf events."""

    def __init__(self, db_path: str):
        self.con = sqlite3.connect(db_path)
        self.con.execute("PRAGMA journal_mode = MEMORY")
        self.session: Optional[perf.session] = None
        self.sample_count = 0

        # Caches and counters grouped to reduce instance attributes
        self.caches: Dict[str, dict] = {
            'machines': {},
            'threads': {},
            'comms': {},
            'dsos': {},
            'symbols': {},
            'events': {},
            'branch_types': {},
            'call_paths': {}
        }

        self.next_id = {
            'machine': 1,
            'thread': 1,
            'comm': 1,
            'dso': 1,
            'symbol': 1,
            'event': 1,
            'branch_type': 1,
            'call_path': 1
        }

        self.create_tables()

    def create_tables(self) -> None:
        """Create database tables."""
        self.con.execute("""
            CREATE TABLE IF NOT EXISTS selected_events (
                    id      INTEGER         NOT NULL        PRIMARY KEY,
                    name    VARCHAR(80))
        """)
        self.con.execute("""
            CREATE TABLE IF NOT EXISTS machines (
                    id      INTEGER         NOT NULL        PRIMARY KEY,
                    pid     INTEGER,
                    root_dir VARCHAR(4096))
        """)
        self.con.execute("""
            CREATE TABLE IF NOT EXISTS threads (
                    id      INTEGER         NOT NULL        PRIMARY KEY,
                    machine_id BIGINT,
                    process_id BIGINT,
                    pid     INTEGER,
                    tid     INTEGER)
        """)
        self.con.execute("""
            CREATE TABLE IF NOT EXISTS comms (
                    id      INTEGER         NOT NULL        PRIMARY KEY,
                    comm    VARCHAR(16),
                    c_thread_id BIGINT,
                    c_time  BIGINT,
                    exec_flag BOOLEAN)
        """)
        self.con.execute("""
            CREATE TABLE IF NOT EXISTS comm_threads (
                    id      INTEGER         NOT NULL        PRIMARY KEY,
                    comm_id BIGINT,
                    thread_id BIGINT)
        """)
        self.con.execute("""
            CREATE TABLE IF NOT EXISTS dsos (
                    id      INTEGER         NOT NULL        PRIMARY KEY,
                    machine_id BIGINT,
                    short_name VARCHAR(256),
                    long_name VARCHAR(4096),
                    build_id VARCHAR(64))
        """)
        self.con.execute("""
            CREATE TABLE IF NOT EXISTS symbols (
                    id      INTEGER         NOT NULL        PRIMARY KEY,
                    dso_id  BIGINT,
                    sym_start BIGINT,
                    sym_end BIGINT,
                    binding INTEGER,
                    name    VARCHAR(2048))
        """)
        self.con.execute("""
            CREATE TABLE IF NOT EXISTS branch_types (
                    id      INTEGER         NOT NULL        PRIMARY KEY,
                    name    VARCHAR(80))
        """)
        self.con.execute("""
            CREATE TABLE IF NOT EXISTS samples (
                    id              INTEGER         NOT NULL        PRIMARY KEY,
                    evsel_id        BIGINT,
                    machine_id      BIGINT,
                    thread_id       BIGINT,
                    comm_id         BIGINT,
                    dso_id          BIGINT,
                    symbol_id       BIGINT,
                    sym_offset      BIGINT,
                    ip              BIGINT,
                    time            BIGINT,
                    cpu             INTEGER,
                    to_dso_id       BIGINT,
                    to_symbol_id    BIGINT,
                    to_sym_offset   BIGINT,
                    to_ip           BIGINT,
                    period          BIGINT,
                    weight          BIGINT,
                    transaction_    BIGINT,
                    data_src        BIGINT,
                    branch_type     INTEGER,
                    in_tx           BOOLEAN,
                    call_path_id    BIGINT,
                    insn_count      BIGINT,
                    cyc_count       BIGINT,
                    flags           INTEGER)
        """)
        self.con.execute('''
            CREATE TABLE IF NOT EXISTS calls (
                id          INTEGER     NOT NULL    PRIMARY KEY,
                thread_id   BIGINT,
                comm_id     BIGINT,
                call_path_id BIGINT,
                call_time   BIGINT,
                return_time BIGINT,
                branch_count BIGINT,
                call_id     BIGINT,
                return_id   BIGINT,
                parent_call_path_id BIGINT,
                flags       INTEGER,
                parent_id   BIGINT,
                insn_count  BIGINT,
                cyc_count   BIGINT
            )
        ''')
        self.con.execute('''
            CREATE TABLE IF NOT EXISTS call_paths (
                id          INTEGER     NOT NULL    PRIMARY KEY,
                parent_id   BIGINT,
                symbol_id   BIGINT,
                ip          BIGINT
            )
        ''')

        self.con.execute('''
            CREATE TABLE IF NOT EXISTS cbr (
                id integer primary key,
                cbr integer,
                mhz integer,
                percent integer
            )
        ''')
        self.con.execute('''
            CREATE TABLE IF NOT EXISTS mwait (
                id integer primary key,
                hints integer,
                extensions integer
            )
        ''')

        self.con.execute('''
            CREATE TABLE IF NOT EXISTS context_switches (
                id integer primary key,
                machine_id bigint,
                time bigint,
                cpu integer,
                thread_out_id bigint,
                comm_out_id bigint,
                thread_in_id bigint,
                comm_in_id bigint,
                flags integer
            )
        ''')
        self.con.execute(
            "CREATE TABLE IF NOT EXISTS ptwrite ("
            "id integer primary key, payload integer, exact_ip integer"
            ")"
        )
        self.con.execute(
            "CREATE TABLE IF NOT EXISTS pwre ("
            "id integer primary key, hw integer, cstate integer, subcstate integer, "
            "hw_name text, cstate_name text"
            ")"
        )
        self.con.execute(
            "CREATE TABLE IF NOT EXISTS exstop ("
            "id integer primary key, exact_ip integer"
            ")"
        )
        self.con.execute(
            "CREATE TABLE IF NOT EXISTS pwrx ("
            "id integer primary key, deepest_cstate integer, last_cstate integer, "
            "wake_reason integer"
            ")"
        )

        self.con.execute(
            "CREATE VIEW IF NOT EXISTS machines_view AS "
            "SELECT id, pid, root_dir, "
            "CASE WHEN id=0 THEN 'unknown' WHEN pid=-1 THEN 'host' ELSE 'guest' END "
            "AS host_or_guest FROM machines"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS dsos_view AS "
            "SELECT id, machine_id, "
            "(SELECT host_or_guest FROM machines_view WHERE id = machine_id) AS host_or_guest, "
            "short_name, long_name, build_id FROM dsos"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS symbols_view AS "
            "SELECT id, name, (SELECT short_name FROM dsos WHERE id=dso_id) AS dso, "
            "dso_id, sym_start, sym_end, "
            "CASE WHEN binding=0 THEN 'local' WHEN binding=1 THEN 'global' ELSE 'weak' END "
            "AS binding FROM symbols"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS threads_view AS "
            "SELECT id, machine_id, "
            "(SELECT host_or_guest FROM machines_view WHERE id = machine_id) AS host_or_guest, "
            "process_id, pid, tid FROM threads"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS comm_threads_view AS "
            "SELECT comm_id, (SELECT comm FROM comms WHERE id = comm_id) AS command, "
            "thread_id, (SELECT pid FROM threads WHERE id = thread_id) AS pid, "
            "(SELECT tid FROM threads WHERE id = thread_id) AS tid FROM comm_threads"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS call_paths_view AS "
            "SELECT c.id, printf('%x', c.ip) AS ip, c.symbol_id, "
            "(SELECT name FROM symbols WHERE id = c.symbol_id) AS symbol, "
            "(SELECT dso_id FROM symbols WHERE id = c.symbol_id) AS dso_id, "
            "(SELECT dso FROM symbols_view WHERE id = c.symbol_id) AS dso_short_name, "
            "c.parent_id, printf('%x', p.ip) AS parent_ip, p.symbol_id AS parent_symbol_id, "
            "(SELECT name FROM symbols WHERE id = p.symbol_id) AS parent_symbol, "
            "(SELECT dso_id FROM symbols WHERE id = p.symbol_id) AS parent_dso_id, "
            "(SELECT dso FROM symbols_view WHERE id = p.symbol_id) AS parent_dso_short_name "
            "FROM call_paths c LEFT JOIN call_paths p ON p.id = c.parent_id"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS calls_view AS "
            "SELECT calls.id, thread_id, "
            "(SELECT pid FROM threads WHERE id = thread_id) AS pid, "
            "(SELECT tid FROM threads WHERE id = thread_id) AS tid, "
            "(SELECT comm FROM comms WHERE id = comm_id) AS command, "
            "call_path_id, printf('%x', ip) AS ip, symbol_id, "
            "(SELECT name FROM symbols WHERE id = symbol_id) AS symbol, "
            "call_time, return_time, return_time - call_time AS elapsed_time, "
            "branch_count, insn_count, cyc_count, "
            "CASE WHEN cyc_count=0 THEN CAST(0 AS FLOAT) "
            "ELSE CAST(insn_count AS FLOAT) / cyc_count END AS IPC, "
            "call_id, return_id, "
            "CASE WHEN flags=0 THEN '' WHEN flags=1 THEN 'no call' "
            "WHEN flags=2 THEN 'no return' WHEN flags=3 THEN 'no call/return' "
            "WHEN flags IN (4, 6) THEN 'jmp' ELSE flags END AS flags, "
            "parent_call_path_id, calls.parent_id "
            "FROM calls INNER JOIN call_paths ON call_paths.id = call_path_id"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS samples_view AS "
            "SELECT id, time, cpu, "
            "(SELECT pid FROM threads WHERE id = thread_id) AS pid, "
            "(SELECT tid FROM threads WHERE id = thread_id) AS tid, "
            "(SELECT comm FROM comms WHERE id = comm_id) AS command, "
            "(SELECT name FROM selected_events WHERE id = evsel_id) AS event, "
            "printf('%x', ip) AS ip_hex, "
            "(SELECT name FROM symbols WHERE id = symbol_id) AS symbol, sym_offset, "
            "(SELECT short_name FROM dsos WHERE id = dso_id) AS dso_short_name, "
            "printf('%x', to_ip) AS to_ip_hex, "
            "(SELECT name FROM symbols WHERE id = to_symbol_id) AS to_symbol, to_sym_offset, "
            "(SELECT short_name FROM dsos WHERE id = to_dso_id) AS to_dso_short_name, "
            "(SELECT name FROM branch_types WHERE id = branch_type) AS branch_type_name, "
            "in_tx, call_path_id, insn_count, cyc_count, "
            "CASE WHEN cyc_count=0 THEN CAST(0 AS FLOAT) "
            "ELSE CAST(insn_count AS FLOAT) / cyc_count END AS IPC, flags FROM samples"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS ptwrite_view AS "
            "SELECT ptwrite.id, time, cpu, printf('%x', payload) AS payload_hex, "
            "CASE WHEN exact_ip=0 THEN 'False' ELSE 'True' END AS exact_ip "
            "FROM ptwrite INNER JOIN samples ON samples.id = ptwrite.id"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS cbr_view AS "
            "SELECT cbr.id, time, cpu, cbr, mhz, percent "
            "FROM cbr INNER JOIN samples ON samples.id = cbr.id"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS mwait_view AS "
            "SELECT mwait.id, time, cpu, printf('%x', hints) AS hints_hex, "
            "printf('%x', extensions) AS extensions_hex "
            "FROM mwait INNER JOIN samples ON samples.id = mwait.id"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS pwre_view AS "
            "SELECT pwre.id, time, cpu, cstate, subcstate, "
            "CASE WHEN hw=0 THEN 'False' ELSE 'True' END AS hw "
            "FROM pwre INNER JOIN samples ON samples.id = pwre.id"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS exstop_view AS "
            "SELECT exstop.id, time, cpu, "
            "CASE WHEN exact_ip=0 THEN 'False' ELSE 'True' END AS exact_ip "
            "FROM exstop INNER JOIN samples ON samples.id = exstop.id"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS pwrx_view AS "
            "SELECT pwrx.id, time, cpu, deepest_cstate, last_cstate, "
            "CASE WHEN wake_reason=1 THEN 'Interrupt' "
            "WHEN wake_reason=2 THEN 'Timer Deadline' "
            "WHEN wake_reason=4 THEN 'Monitored Address' "
            "WHEN wake_reason=8 THEN 'HW' WHEN wake_reason=16 THEN 'Other' "
            "ELSE wake_reason END AS wake_reason "
            "FROM pwrx INNER JOIN samples ON samples.id = pwrx.id"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS power_events_view AS "
            "SELECT samples.id, time, cpu, selected_events.name AS event, "
            "CASE WHEN selected_events.name='cbr' THEN "
            "(SELECT cbr FROM cbr WHERE cbr.id = samples.id) ELSE \"\" END AS cbr, "
            "CASE WHEN selected_events.name='cbr' THEN "
            "(SELECT mhz FROM cbr WHERE cbr.id = samples.id) ELSE \"\" END AS mhz, "
            "CASE WHEN selected_events.name='cbr' THEN "
            "(SELECT percent FROM cbr WHERE cbr.id = samples.id) ELSE \"\" END AS percent, "
            "CASE WHEN selected_events.name='mwait' THEN "
            "(SELECT printf('%x', hints) FROM mwait WHERE mwait.id = samples.id) "
            "ELSE \"\" END AS hints_hex, "
            "CASE WHEN selected_events.name='mwait' THEN "
            "(SELECT printf('%x', extensions) FROM mwait WHERE mwait.id = samples.id) "
            "ELSE \"\" END AS extensions_hex, "
            "CASE WHEN selected_events.name='pwre' THEN "
            "(SELECT cstate FROM pwre WHERE pwre.id = samples.id) ELSE \"\" END AS cstate, "
            "CASE WHEN selected_events.name='pwre' THEN "
            "(SELECT subcstate FROM pwre WHERE pwre.id = samples.id) ELSE \"\" END AS subcstate, "
            "CASE WHEN selected_events.name='pwre' THEN "
            "(SELECT hw FROM pwre WHERE pwre.id = samples.id) ELSE \"\" END AS hw, "
            "CASE WHEN selected_events.name='exstop' THEN "
            "(SELECT exact_ip FROM exstop WHERE exstop.id = samples.id) "
            "ELSE \"\" END AS exact_ip, "
            "CASE WHEN selected_events.name='pwrx' THEN "
            "(SELECT deepest_cstate FROM pwrx WHERE pwrx.id = samples.id) "
            "ELSE \"\" END AS deepest_cstate, "
            "CASE WHEN selected_events.name='pwrx' THEN "
            "(SELECT last_cstate FROM pwrx WHERE pwrx.id = samples.id) "
            "ELSE \"\" END AS last_cstate, "
            "CASE WHEN selected_events.name='pwrx' THEN "
            "(SELECT wake_reason FROM pwrx WHERE pwrx.id = samples.id) "
            "ELSE \"\" END AS wake_reason "
            "FROM samples INNER JOIN selected_events ON selected_events.id = evsel_id "
            "WHERE selected_events.name IN ('cbr','mwait','pwre','exstop','pwrx')"
        )
        self.con.execute(
            "CREATE VIEW IF NOT EXISTS context_switches_view AS "
            "SELECT context_switches.id, context_switches.machine_id, "
            "context_switches.time, context_switches.cpu, "
            "th_out.pid AS pid_out, th_out.tid AS tid_out, comm_out.comm AS comm_out, "
            "th_in.pid AS pid_in, th_in.tid AS tid_in, comm_in.comm AS comm_in, "
            "CASE WHEN flags=0 THEN 'in' WHEN flags=1 THEN 'out' "
            "WHEN flags=3 THEN 'out preempt' ELSE flags END AS flags "
            "FROM context_switches "
            "INNER JOIN threads AS th_out ON th_out.id = context_switches.thread_out_id "
            "INNER JOIN threads AS th_in  ON th_in.id  = context_switches.thread_in_id "
            "INNER JOIN comms AS comm_out ON comm_out.id = context_switches.comm_out_id "
            "INNER JOIN comms AS comm_in  ON comm_in.id  = context_switches.comm_in_id"
        )

        # id == 0 means unknown. It is easier to create records for them than
        # replace the zeroes with NULLs
        self.con.execute("INSERT OR IGNORE INTO selected_events VALUES (0, 'unknown')")
        self.con.execute("INSERT OR IGNORE INTO machines VALUES (0, 0, 'unknown')")
        self.con.execute("INSERT OR IGNORE INTO threads VALUES (0, 0, 0, -1, -1)")
        self.con.execute("INSERT OR IGNORE INTO comms VALUES (0, 'unknown', 0, 0, 0)")
        self.con.execute("INSERT OR IGNORE INTO dsos VALUES (0, 0, 'unknown', 'unknown', '')")
        self.con.execute("INSERT OR IGNORE INTO call_paths VALUES (0, 0, 0, 0)")
        self.con.execute("INSERT OR IGNORE INTO symbols VALUES (0, 0, 0, 0, 0, 'unknown')")
        for b_type, b_name in BRANCH_TYPES:
            self.con.execute("INSERT OR IGNORE INTO branch_types VALUES (?, ?)", (b_type, b_name))
            self.caches['branch_types'][b_type] = b_name

        self.caches['events']['unknown'] = 0
        self.caches['threads'][(0, -1, -1)] = 0
        self.caches['threads'][(0, 0, 0)] = 0
        self.caches['comms'][('unknown', 0)] = 0
        self.caches['dsos'][(0, 'unknown', 'unknown', '')] = 0
        self.caches['symbols'][(0, 'unknown')] = (0, 0, 0)
        # Initialize comm_threads mapping
        self.comm_threads_cache: set[tuple[int, int]] = set()
        self.next_comm_thread_id = 1

    def _exec(self, sql: str, params: tuple[typing.Any, ...] = ()) -> sqlite3.Cursor:
        """Execute SQL statement converting unsigned 64-bit ints to signed 64-bit.

        Python's sqlite3 module only binds signed 64-bit integers (int64_t) and
        raises OverflowError for unsigned 64-bit kernel addresses (>= 2^63).
        Convert values in [2^63, 2^64) into two's-complement signed 64-bit ints.
        """
        conv_params = tuple(
            p - 0x10000000000000000 if isinstance(p, int) and p >= 0x8000000000000000 else p
            for p in params
        )
        return self.con.execute(sql, conv_params)

    def get_machine_id(self, machine_pid: Optional[int]) -> int:
        """Get or create machine ID."""
        if machine_pid is None or machine_pid <= 0 or machine_pid > 0x7fffffff:
            machine_pid = -1
        if machine_pid in self.caches['machines']:
            return self.caches['machines'][machine_pid]
        machine_id = self.next_id['machine']
        self.next_id['machine'] += 1
        self._exec("INSERT INTO machines VALUES (?, ?, ?)",
                   (machine_id, machine_pid, ""))
        self.caches['machines'][machine_pid] = machine_id
        return machine_id

    def get_event_id(self, name: str) -> int:
        """Get or create event ID."""
        if name in self.caches['events']:
            return self.caches['events'][name]
        event_id = self.next_id['event']
        self._exec("INSERT INTO selected_events VALUES (?, ?)",
                   (event_id, name))
        self.caches['events'][name] = event_id
        self.next_id['event'] += 1
        return event_id

    def get_thread_id(self, machine_id: int, pid: Optional[int], tid: Optional[int]) -> int:
        """Get or create thread ID."""
        if pid is None:
            pid = -1
        elif pid > 0x7fffffff:
            pid -= 0x100000000
        if tid is None:
            tid = -1
        elif tid > 0x7fffffff:
            tid -= 0x100000000
        key = (machine_id, pid, tid)
        if key in self.caches['threads']:
            return self.caches['threads'][key]
        thread_id = self.next_id['thread']
        self.next_id['thread'] += 1
        self.caches['threads'][key] = thread_id
        process_id = thread_id if pid == tid else self.get_thread_id(machine_id, pid, pid)
        self._exec("INSERT INTO threads VALUES (?, ?, ?, ?, ?)",
                   (thread_id, machine_id, process_id, pid, tid))
        return thread_id

    def get_comm_id(self, comm: str, thread_id: int) -> int:
        """Get or create comm ID."""
        key = (comm, thread_id)
        if key not in self.caches['comms']:
            comm_id = self.next_id['comm']
            self._exec("INSERT INTO comms VALUES (?, ?, ?, ?, ?)",
                       (comm_id, comm, thread_id, 0, 0))
            self.caches['comms'][key] = comm_id
            self.next_id['comm'] += 1
        comm_id = self.caches['comms'][key]
        mapping_key = (comm_id, thread_id)
        if mapping_key not in self.comm_threads_cache:
            comm_thread_id = self.next_comm_thread_id
            self._exec("INSERT INTO comm_threads VALUES (?, ?, ?)",
                       (comm_thread_id, comm_id, thread_id))
            self.comm_threads_cache.add(mapping_key)
            self.next_comm_thread_id += 1
        return comm_id

    def get_dso_id(self, short_name: str, long_name: str,
                   build_id: str, machine_id: int = 0) -> int:
        """Get or create DSO ID."""
        key = (machine_id, short_name, long_name, build_id)
        if key in self.caches['dsos']:
            return self.caches['dsos'][key]
        short_key = (machine_id, short_name)
        if not build_id and short_key in self.caches['dsos']:
            return self.caches['dsos'][short_key]
        dso_id = self.next_id['dso']
        self._exec("INSERT INTO dsos VALUES (?, ?, ?, ?, ?)",
                   (dso_id, machine_id, short_name, long_name, build_id))
        self.caches['dsos'][key] = dso_id
        self.caches['dsos'][short_key] = dso_id
        self.next_id['dso'] += 1
        return dso_id

    def get_symbol_id(self, dso_id: int, name: str, start: int,
                      end: int) -> int:
        """Get or create symbol ID."""
        key = (dso_id, name)
        if key in self.caches['symbols']:
            symbol_id, cached_start, cached_end = self.caches['symbols'][key]
            if cached_start == 0 and cached_end == 0 and (start != 0 or end != 0):
                self._exec("UPDATE symbols SET sym_start = ?, sym_end = ? WHERE id = ?",
                           (start, end, symbol_id))
                self.caches['symbols'][key] = (symbol_id, start, end)
            return symbol_id
        symbol_id = self.next_id['symbol']
        self._exec("INSERT INTO symbols VALUES (?, ?, ?, ?, ?, ?)",
                   (symbol_id, dso_id, start, end, 0, name))
        self.caches['symbols'][key] = (symbol_id, start, end)
        self.next_id['symbol'] += 1
        return symbol_id

    def get_call_path_id(self, parent_id: int, symbol_id: int,
                         ip: int) -> int:
        """Get or create call path ID."""
        key = (parent_id, symbol_id, ip)
        if key in self.caches['call_paths']:
            return self.caches['call_paths'][key]
        call_path_id = self.next_id['call_path']
        self._exec("INSERT INTO call_paths VALUES (?, ?, ?, ?)",
                   (call_path_id, parent_id, symbol_id, ip))
        self.caches['call_paths'][key] = call_path_id
        self.next_id['call_path'] += 1
        return call_path_id

    def process_event(self, sample: perf.sample_event) -> None:
        """Callback for processing events."""

        machine_pid = getattr(sample, 'machine_pid', 0)
        machine_id = self.get_machine_id(machine_pid)
        thread_id = self.get_thread_id(machine_id, sample.sample_pid, sample.sample_tid)

        comm = "Unknown_comm"
        try:
            if self.session is not None:
                proc = self.session.find_thread(sample.sample_pid, sample.sample_tid)
                if proc:
                    comm_name = proc.comm()
                    if comm_name:
                        comm = comm_name
        except TypeError:
            pass
        comm_id = self.get_comm_id(comm, thread_id)

        build_id = sample.dso_bid or ""
        dso_id = self.get_dso_id(
            sample.dso or "Unknown_dso",
            sample.dso_long_name or "Unknown_dso_long",
            build_id,
            machine_id
        )

        symbol_id = self.get_symbol_id(
            dso_id,
            sample.symbol or "Unknown_symbol",
            sample.sym_start or 0,
            sample.sym_end or 0
        )

        # Handle callchain
        call_path_id = 0
        if hasattr(sample, 'callchain') and sample.callchain:
            parent_id = 0
            for node in reversed(sample.callchain):
                dso_name = node.dso or "Unknown_dso"
                symbol_name = node.symbol or "Unknown_symbol"

                node_dso_id = self.get_dso_id(dso_name, dso_name, "", machine_id)
                node_symbol_id = self.get_symbol_id(node_dso_id, symbol_name, 0, 0)

                parent_id = self.get_call_path_id(parent_id, node_symbol_id, node.ip)
            call_path_id = parent_id
        else:
            call_path_id = 0

        # Insert sample
        event_name_str = str(sample.evsel)
        if event_name_str.startswith("evsel(") and event_name_str.endswith(")"):
            event_name_str = event_name_str[6:-1]
        cursor = self._exec("""
            INSERT INTO samples VALUES (
                NULL, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?
            )
        """, (
            self.get_event_id(event_name_str),
            machine_id, thread_id, comm_id,
            dso_id, symbol_id,
            getattr(sample, 'sym_offset', 0) or 0,
            sample.sample_ip, sample.sample_time, sample.sample_cpu,
            self.get_dso_id(sample.addr_dso or "Unknown_dso",
                            sample.addr_dso or "Unknown_dso_long", "", machine_id),
            self.get_symbol_id(
                self.get_dso_id(sample.addr_dso or "Unknown_dso",
                                sample.addr_dso or "Unknown_dso_long", "", machine_id),
                sample.addr_symbol or "Unknown_symbol", 0, 0
            ),
            sample.addr_sym_offset or 0,
            sample.sample_addr or 0,
            sample.sample_period or 0,
            sample.sample_weight or 0,
            sample.transaction or 0,
            sample.sample_data_src,
            sample.branch_type or 0,
            sample.in_tx or 0,
            call_path_id,
            sample.sample_insn_count,
            sample.sample_cyc_count,
            getattr(sample, "flags", 0)  # flags
        ))
        sample_id = cursor.lastrowid

        # Handle Intel PT specific raw payloads mathematically equivalent to
        # legacy synth_data unpacking

        if event_name_str == "ptwrite" and hasattr(sample, "raw_buf"):
            try:
                flags, payload = struct.unpack_from("<IQ", sample.raw_buf)
                self._exec("INSERT INTO ptwrite VALUES (?, ?, ?)",
                           (sample_id, payload, flags & 1))
            except struct.error:
                pass

        elif event_name_str == "cbr" and hasattr(sample, "raw_buf"):
            try:
                data = struct.unpack_from("<BBBBII", sample.raw_buf)
                cbr_val, max_nonturbo, freq = data[0], data[2], data[4]
                MHz = (freq + 500) // 1000
                percent = ((cbr_val * 1000 // max_nonturbo) + 5) // 10 if max_nonturbo else 0
                self._exec("INSERT INTO cbr VALUES (?, ?, ?, ?)",
                           (sample_id, cbr_val, MHz, percent))
            except struct.error:
                pass

        elif event_name_str == "mwait" and hasattr(sample, "raw_buf"):
            try:
                flags, payload = struct.unpack_from("<IQ", sample.raw_buf)
                hints = payload & 0xff
                extensions = (payload >> 32) & 0x3
                self._exec("INSERT INTO mwait VALUES (?, ?, ?)",
                           (sample_id, hints, extensions))
            except struct.error:
                pass

        elif event_name_str == "pwre" and hasattr(sample, "raw_buf"):
            try:
                flags, payload = struct.unpack_from("<IQ", sample.raw_buf)
                hw = (payload >> 7) & 1
                cstate = (payload >> 12) & 0xf
                subcstate = (payload >> 8) & 0xf
                self._exec("INSERT INTO pwre (id, cstate, subcstate, hw) VALUES (?, ?, ?, ?)",
                           (sample_id, cstate, subcstate, hw))
            except struct.error:
                pass

        elif event_name_str == "exstop" and hasattr(sample, "raw_buf"):
            try:
                flags = struct.unpack_from("<I", sample.raw_buf)[0]
                self._exec("INSERT INTO exstop VALUES (?, ?)",
                           (sample_id, flags & 1))
            except struct.error:
                pass

        elif event_name_str == "pwrx" and hasattr(sample, "raw_buf"):
            try:
                flags, payload = struct.unpack_from("<IQ", sample.raw_buf)
                deepest_cstate = payload & 0xf
                last_cstate = (payload >> 4) & 0xf
                wake_reason = (payload >> 8) & 0xf
                self._exec("INSERT INTO pwrx VALUES (?, ?, ?, ?)",
                           (sample_id, deepest_cstate, last_cstate, wake_reason))
            except struct.error:
                pass

        self.sample_count += 1
        if self.sample_count % 10000 == 0:
            self.commit()

    def get_call_path_ids(self, call_path: typing.Any, machine_id: int = 0) -> tuple[int, int]:
        """Add a perf.callchain as call_paths rows, return its id and parent's."""
        parent_id = 0
        call_path_id = 0
        for node in call_path:
            dso_name = node.dso or "Unknown_dso"
            symbol_name = node.symbol or "Unknown_symbol"
            dso_id = self.get_dso_id(dso_name, dso_name, "", machine_id)
            symbol_id = self.get_symbol_id(dso_id, symbol_name, 0, 0)
            parent_id = call_path_id
            call_path_id = self.get_call_path_id(parent_id, symbol_id, node.ip)
        return call_path_id, parent_id

    def process_call_return(self, cr: perf.call_return) -> None:
        """Callback for processing call_return events."""
        machine_id = self.get_machine_id(getattr(cr, "machine_pid", None))
        thread_id = self.get_thread_id(machine_id, cr.pid, cr.tid)
        comm_id = self.get_comm_id(cr.comm or "Unknown_comm", thread_id)
        call_path_id, parent_call_path_id = self.get_call_path_ids(cr.call_path or [], machine_id)
        # calls.id and calls.parent_id use the db_id of the perf module, as a
        # call is given an id before it returns its parent cannot be numbered
        # here.
        self._exec(
            "INSERT INTO calls VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (cr.db_id, thread_id, comm_id, call_path_id,
             cr.call_time, cr.return_time,
             cr.branch_count, cr.call_ref,
             cr.return_ref, parent_call_path_id,
             cr.flags, cr.parent_id,
             cr.insn_count, cr.cyc_count)
        )

    def process_context_switch(self, event: typing.Any) -> None:
        """Callback for processing context switch events."""
        misc = getattr(event, 'misc', 0)
        out = bool(misc & (1 << 13)) # PERF_RECORD_MISC_SWITCH_OUT
        out_preempt = bool(misc & (1 << 14)) # PERF_RECORD_MISC_SWITCH_OUT_PREEMPT
        flags = (1 if out else 0) | ((1 if out_preempt else 0) << 1)
        machine_pid = getattr(event, 'machine_pid', None)
        machine_id = self.get_machine_id(machine_pid)

        sample_pid = getattr(event, 'sample_pid', None)
        if sample_pid is None:
            sample_pid = -1
        elif sample_pid > 0x7fffffff:
            sample_pid -= 0x100000000
        sample_tid = getattr(event, 'sample_tid', None)
        if sample_tid is None:
            sample_tid = -1
        elif sample_tid > 0x7fffffff:
            sample_tid -= 0x100000000
        next_prev_pid = getattr(event, 'next_prev_pid', None)
        if next_prev_pid is None:
            next_prev_pid = -1
        elif next_prev_pid > 0x7fffffff:
            next_prev_pid -= 0x100000000
        next_prev_tid = getattr(event, 'next_prev_tid', None)
        if next_prev_tid is None:
            next_prev_tid = -1
        elif next_prev_tid > 0x7fffffff:
            next_prev_tid -= 0x100000000

        th_a_id = self.get_thread_id(machine_id, sample_pid, sample_tid)
        comm_a = "Unknown_comm"
        if self.session:
            try:
                proc = self.session.find_thread(sample_pid, sample_tid)
                if proc:
                    comm_a_name = proc.comm()
                    if comm_a_name:
                        comm_a = comm_a_name
            except TypeError:
                pass
        comm_a_id = self.get_comm_id(comm_a, th_a_id)

        th_b_id = self.get_thread_id(machine_id, next_prev_pid, next_prev_tid)
        comm_b = "Unknown_comm"
        if self.session:
            try:
                proc = self.session.find_thread(next_prev_pid, next_prev_tid)
                if proc:
                    comm_b_name = proc.comm()
                    if comm_b_name:
                        comm_b = comm_b_name
            except TypeError:
                pass
        comm_b_id = self.get_comm_id(comm_b, th_b_id)

        if out:
            th_out_id = th_a_id
            comm_out_id = comm_a_id
            th_in_id = th_b_id
            comm_in_id = comm_b_id
        else:
            th_out_id = th_b_id
            comm_out_id = comm_b_id
            th_in_id = th_a_id
            comm_in_id = comm_a_id

        self._exec(
            "INSERT INTO context_switches VALUES (NULL, ?, ?, ?, ?, ?, ?, ?, ?)",
            (machine_id, getattr(event, 'sample_time', 0), getattr(event, 'sample_cpu', 0) or 0,
             th_out_id, comm_out_id, th_in_id, comm_in_id, flags)
        )

    def finish(self) -> None:
        """Create indexes and update summary metadata after processing events.

        pcpid_idx, pid_idx, and comms.has_calls are required by
        exported-sql-viewer.py to filter the command dropdown to threads with
        call graphs and to expand parent/child call trees efficiently.
        """
        self.con.execute("CREATE INDEX IF NOT EXISTS pcpid_idx ON calls (parent_call_path_id)")
        self.con.execute("CREATE INDEX IF NOT EXISTS pid_idx ON calls (parent_id)")
        self.con.execute("ALTER TABLE comms ADD has_calls boolean")
        self.con.execute("UPDATE comms SET has_calls = 1 WHERE comms.id IN "
                         "(SELECT DISTINCT comm_id FROM calls)")

    def commit(self) -> None:
        """Commit transaction."""
        self.con.commit()

    def close(self) -> None:
        """Close connection."""
        self.con.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(
        description="Export perf data to a sqlite3 database")
    ap.add_argument("-i", "--input", default="perf.data",
                    help="Input file name")
    ap.add_argument("-o", "--output", default="perf.db",
                    help="Output database name")
    ap.add_argument("--itrace", help="Instruction Tracing options (e.g. crpt)")
    args = ap.parse_args()

    try:
        fd = os.open(args.output, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        print(f"Error: {args.output} already exists")
        sys.exit(1)

    tmp_dir = None
    exporter = None
    succeeded = False
    try:
        tmp_dir = tempfile.mkdtemp(prefix="perf-export-sqlite-")
        tmp_db = os.path.join(tmp_dir, "perf.db")
        exporter = DatabaseExporter(tmp_db)
        session = perf.session(perf.data(args.input),
                               sample=exporter.process_event,
                               context_switch=exporter.process_context_switch,
                               call_return=exporter.process_call_return,
                               itrace=args.itrace)
        exporter.session = session
        try:
            session.process_events()
        finally:
            # Break the C perf.session -> bound method -> exporter reference cycle.
            exporter.session = None
        exporter.finish()
        exporter.commit()
        exporter.close()
        exporter = None
        with open(tmp_db, "rb") as src, os.fdopen(fd, "wb", closefd=False) as dst:
            shutil.copyfileobj(src, dst)
        succeeded = True
        print(f"Successfully exported to {args.output}")
    except (OSError, RuntimeError, ValueError, sqlite3.Error):
        import traceback
        traceback.print_exc()
    finally:
        if exporter is not None:
            exporter.session = None
            try:
                exporter.close()
            except (OSError, sqlite3.Error):
                pass
        try:
            os.close(fd)
        except OSError:
            pass
        if tmp_dir is not None:
            shutil.rmtree(tmp_dir, ignore_errors=True)
        if not succeeded:
            try:
                os.remove(args.output)
            except OSError:
                pass
    if not succeeded:
        sys.exit(1)
