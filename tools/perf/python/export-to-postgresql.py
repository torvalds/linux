#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
r"""
Export perf data to a postgresql database.

This script has been ported to use the modern perf Python module and
libpq via ctypes. It no longer requires PySide2 or QtSql for exporting.

The script assumes postgresql is running on the local machine and that the
user has postgresql permissions to create databases.

An example of using this script with Intel PT:

	$ perf record -e intel_pt//u ls
	$ python export-to-postgresql.py -i perf.data -o pt_example

To browse the database, psql can be used e.g.

	$ psql pt_example
	pt_example=# select * from samples_view where id < 100;
	pt_example=# \d+
	pt_example=# \d+ samples_view
	pt_example=# \q

An example of using the database is provided by the script
exported-sql-viewer.py. Refer to that script for details.

Tables:

	The tables largely correspond to perf tools' data structures. They are
	largely self-explanatory.

	samples
		'samples' is the main table. It represents what instruction was
		executing at a point in time when something (a selected event)
		happened. The memory address is the instruction pointer or 'ip'.

	branch_types
		'branch_types' provides descriptions for each type of branch.

	comm_threads
		'comm_threads' shows how 'comms' relates to 'threads'.

	comms
		'comms' contains a record for each 'comm' - the name given to the
		executable that is running.

	dsos
		'dsos' contains a record for each executable file or library.

	machines
		'machines' can be used to distinguish virtual machines if
		virtualization is supported.

	selected_events
		'selected_events' contains a record for each kind of event that
		has been sampled.

	symbols
		'symbols' contains a record for each symbol. Only symbols that
		have samples are present.

	threads
		'threads' contains a record for each thread.

Views:

	Most of the tables have views for more friendly display. The views are:

		comm_threads_view
		dsos_view
		machines_view
		samples_view
		symbols_view
		threads_view

Ported from tools/perf/scripts/python/export-to-postgresql.py
"""

from __future__ import annotations
import typing
import argparse
from ctypes import CDLL, c_char_p, c_int, c_void_p, c_ubyte
import ctypes.util
import os
import shutil
import struct
import tempfile
import sys
from typing import Any, Dict, Optional
import perf

# Need to access PostgreSQL C library directly to use COPY FROM STDIN
libpq_name = ctypes.util.find_library("pq")
if not libpq_name:
    libpq_name = "libpq.so.5"

try:
    libpq = CDLL(libpq_name)
except OSError as e:
    print(f"Error loading {libpq_name}: {e}")
    print("Please ensure PostgreSQL client library is installed.")
    sys.exit(1)

PQconnectdb = libpq.PQconnectdb
PQconnectdb.restype = c_void_p
PQconnectdb.argtypes = [c_char_p]
PQfinish = libpq.PQfinish
PQfinish.argtypes = [c_void_p]
PQstatus = libpq.PQstatus
PQstatus.restype = c_int
PQstatus.argtypes = [c_void_p]
libpq.PQerrorMessage.restype = c_char_p
libpq.PQerrorMessage.argtypes = [c_void_p]
PQexec = libpq.PQexec
PQexec.restype = c_void_p
PQexec.argtypes = [c_void_p, c_char_p]
PQresultStatus = libpq.PQresultStatus
PQresultStatus.restype = c_int
PQresultStatus.argtypes = [c_void_p]
PQputCopyData = libpq.PQputCopyData
PQputCopyData.restype = c_int
PQputCopyData.argtypes = [c_void_p, c_void_p, c_int]
PQputCopyEnd = libpq.PQputCopyEnd
PQputCopyEnd.restype = c_int
PQputCopyEnd.argtypes = [c_void_p, c_void_p]
PQgetResult = libpq.PQgetResult
PQgetResult.restype = c_void_p
PQgetResult.argtypes = [c_void_p]
PQclear = libpq.PQclear
PQclear.argtypes = [c_void_p]


def toserverstr(s: str) -> bytes:
    """Convert string to server encoding (UTF-8)."""
    return bytes(s, "UTF_8")


def toclientstr(s: str) -> bytes:
    """Convert string to client encoding (UTF-8)."""
    return bytes(s, "UTF_8")

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

class PostgresExporter:
    """Handles PostgreSQL connection and exporting of perf events."""

    def __init__(self, dbname: str):
        self.dbname = dbname
        self.conn = None
        self.session: Optional[perf.session] = None
        self.output_dir_name = tempfile.mkdtemp(prefix=dbname + "-perf-data-")
        self.created_output_dir = True

        self.file_header = struct.pack("!11sii", b"PGCOPY\n\377\r\n\0", 0, 0)
        self.file_trailer = b"\377\377"

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
        self.symbol_rows: Dict[int, tuple[int, int, int, int, str]] = {}

        self.next_id = {
            'machine': 1,
            'thread': 1,
            'comm': 1,
            'dso': 1,
            'symbol': 1,
            'event': 1,
            'branch_type': 1,
            'comm_thread': 1,
            'call_path': 1,
            'sample': 1,
            'call': 1,
            'context_switch': 1
        }

        self.files: Dict[str, Any] = {}
        self.unhandled_count = 0

    def connect(self, db_to_use: str) -> None:
        """Connect to database.

        Reject URI schemes ('://') and key=value pairs ('=') to prevent
        connection parameter injection, and escape backslashes and single
        quotes per libpq single-quoted conninfo string rules.
        """
        if "://" in db_to_use or "=" in db_to_use:
            raise ValueError(f"Invalid database name: {db_to_use}")
        safe_db = db_to_use.replace('\\', '\\\\').replace("'", "\\'")
        conn_str = toclientstr(f"dbname='{safe_db}'")
        self.conn = PQconnectdb(conn_str)
        if PQstatus(self.conn) != 0:
            PQfinish(self.conn)
            self.conn = None
            raise RuntimeError(f"PQconnectdb failed for {db_to_use}")

    def disconnect(self) -> None:
        """Disconnect from database."""
        if self.conn:
            PQfinish(self.conn)
            self.conn = None

    def do_query(self, sql: str) -> None:
        """Execute a query and check status."""
        res = PQexec(self.conn, toserverstr(sql))
        status = PQresultStatus(res)
        PQclear(res)
        if status not in (1, 2):  # PGRES_COMMAND_OK, PGRES_TUPLES_OK
            error_msg = libpq.PQerrorMessage(self.conn).decode('utf-8')
            raise RuntimeError(f"Query failed: {sql}. Error: {error_msg}")

    def open_output_file(self, file_name: str):
        """Open intermediate binary file."""
        path_name = self.output_dir_name + "/" + file_name
        f = open(path_name, "wb+")
        f.write(self.file_header)
        return f

    def close_output_file(self, f):
        """Close intermediate binary file."""
        f.write(self.file_trailer)
        f.close()

    def copy_output_file(self, path_name: str, table_name: str):
        """Copy intermediate file to database."""
        sql = f"COPY {table_name} FROM STDIN (FORMAT 'binary')"
        res = PQexec(self.conn, toserverstr(sql))
        if PQresultStatus(res) != 4:  # PGRES_COPY_IN
            PQclear(res)
            err = libpq.PQerrorMessage(self.conn).decode()
            raise RuntimeError(f"COPY FROM STDIN PQexec failed for {table_name}: {err}")
        PQclear(res)

        with open(path_name, "rb") as f:
            data = f.read(65536)
            while len(data) > 0:
                c_data = (c_ubyte * len(data)).from_buffer_copy(data)
                ret = PQputCopyData(self.conn, c_data, len(data))
                if ret != 1:
                    raise RuntimeError(f"PQputCopyData failed for {table_name}")
                data = f.read(65536)

        ret = PQputCopyEnd(self.conn, None)
        if ret != 1:
            err = libpq.PQerrorMessage(self.conn).decode()
            raise RuntimeError(f"PQputCopyEnd failed for {table_name}: {err}")

        res = PQgetResult(self.conn)
        while res:
            status = PQresultStatus(res)
            if status != 1: # PGRES_COMMAND_OK
                error_msg = libpq.PQerrorMessage(self.conn).decode('utf-8')
                PQclear(res)
                raise RuntimeError(
                    f"COPY completion failed for {table_name}. Status {status}: {error_msg}"
                )
            PQclear(res)
            res = PQgetResult(self.conn)

    def setup_db(self) -> None:
        """Create database and tables. MUST be called after init."""
        if "://" in self.dbname or "=" in self.dbname:
            raise ValueError(f"Invalid database name: {self.dbname}")
        self.created_output_dir = True

        self.connect('postgres')
        try:
            # SQL identifiers are double-quoted; escape embedded double quotes by doubling them.
            db_name = self.dbname.replace('"', '""')
            self.do_query(f'CREATE DATABASE "{db_name}"')
        except Exception as db_error:
            shutil.rmtree(self.output_dir_name, ignore_errors=True)
            raise db_error
        self.disconnect()

        self.connect(self.dbname)
        self.do_query("SET client_min_messages TO WARNING")

        self.do_query("""
            CREATE TABLE selected_events (
                    id              bigint          NOT NULL,
                    name            varchar(80))
        """)
        self.do_query("""
            CREATE TABLE machines (
                    id              bigint          NOT NULL,
                    pid             integer,
                    root_dir        varchar(4096))
        """)
        self.do_query("""
            CREATE TABLE threads (
                    id              bigint          NOT NULL,
                    machine_id      bigint,
                    process_id      bigint,
                    pid             integer,
                    tid             integer)
        """)
        self.do_query("""
            CREATE TABLE comms (
                    id              bigint          NOT NULL,
                    comm            varchar(16),
                    c_thread_id     bigint,
                    c_time          bigint,
                    exec_flag       boolean)
        """)
        self.do_query("""
            CREATE TABLE comm_threads (
                    id              bigint          NOT NULL,
                    comm_id         bigint,
                    thread_id       bigint)
        """)
        self.do_query("""
            CREATE TABLE dsos (
                    id              bigint          NOT NULL,
                    machine_id      bigint,
                    short_name      varchar(256),
                    long_name       varchar(4096),
                    build_id        varchar(64))
        """)
        self.do_query("""
            CREATE TABLE symbols (
                    id              bigint          NOT NULL,
                    dso_id          bigint,
                    sym_start       bigint,
                    sym_end         bigint,
                    binding         integer,
                    name            varchar(2048))
        """)
        self.do_query("""
            CREATE TABLE branch_types (
                    id              integer         NOT NULL,
                    name            varchar(80))
        """)
        self.do_query("""
            CREATE TABLE samples (
                    id              bigint          NOT NULL,
                    evsel_id        bigint,
                    machine_id      bigint,
                    thread_id       bigint,
                    comm_id         bigint,
                    dso_id          bigint,
                    symbol_id       bigint,
                    sym_offset      bigint,
                    ip              bigint,
                    time            bigint,
                    cpu             integer,
                    to_dso_id       bigint,
                    to_symbol_id    bigint,
                    to_sym_offset   bigint,
                    to_ip           bigint,
                    period          bigint,
                    weight          bigint,
                    transaction_    bigint,
                    data_src        bigint,
                    branch_type     integer,
                    in_tx           boolean,
                    call_path_id    bigint,
                    insn_count      bigint,
                    cyc_count       bigint,
                    flags           integer)
        """)
        self.do_query('''
            CREATE TABLE context_switches (
                id bigint,
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
        self.do_query('''
            CREATE TABLE call_paths (
                    id              bigint          NOT NULL,
                    parent_id       bigint,
                    symbol_id       bigint,
                    ip              bigint)
        ''')
        self.do_query('''
            CREATE TABLE calls (
                id bigint NOT NULL,
                thread_id bigint,
                comm_id bigint,
                call_path_id bigint,
                call_time bigint,
                return_time bigint,
                branch_count bigint,
                call_id bigint,
                return_id bigint,
                parent_call_path_id bigint,
                flags integer,
                parent_id bigint,
                insn_count bigint,
                cyc_count bigint
            )
        ''')
        self.do_query('''
            CREATE TABLE ptwrite (
                id bigint NOT NULL,
                payload bigint,
                exact_ip boolean
            )
        ''')
        self.do_query('''
            CREATE TABLE cbr (
                id bigint NOT NULL,
                cbr integer,
                mhz integer,
                percent integer
            )
        ''')
        self.do_query('''
            CREATE TABLE mwait (
                id bigint NOT NULL,
                hints integer,
                extensions integer
            )
        ''')
        self.do_query('''
            CREATE TABLE pwre (
                id bigint NOT NULL,
                cstate integer,
                subcstate integer,
                hw boolean
            )
        ''')
        self.do_query('''
            CREATE TABLE exstop (
                id bigint NOT NULL,
                exact_ip boolean
            )
        ''')
        self.do_query('''
            CREATE TABLE pwrx (
                id bigint NOT NULL,
                deepest_cstate integer,
                last_cstate integer,
                wake_reason integer
            )
        ''')

        self.files['evsel'] = self.open_output_file("evsel_table.bin")
        self.files['machine'] = self.open_output_file("machine_table.bin")
        self.files['thread'] = self.open_output_file("thread_table.bin")
        self.files['comm'] = self.open_output_file("comm_table.bin")
        self.files['comm_thread'] = self.open_output_file("comm_thread_table.bin")
        self.files['dso'] = self.open_output_file("dso_table.bin")
        self.files['symbol'] = self.open_output_file("symbol_table.bin")
        self.files['branch_type'] = self.open_output_file("branch_type_table.bin")
        self.files['sample'] = self.open_output_file("sample_table.bin")
        self.files['context_switches'] = self.open_output_file("context_switches_table.bin")
        self.files['call'] = self.open_output_file("call_table.bin")
        self.files['call_path'] = self.open_output_file("call_path_table.bin")
        self.files['ptwrite'] = self.open_output_file("ptwrite_table.bin")
        self.files['cbr'] = self.open_output_file("cbr_table.bin")
        self.files['mwait'] = self.open_output_file("mwait_table.bin")
        self.files['pwre'] = self.open_output_file("pwre_table.bin")
        self.files['exstop'] = self.open_output_file("exstop_table.bin")
        self.files['pwrx'] = self.open_output_file("pwrx_table.bin")

        self.write_evsel(0, "unknown")
        self.write_machine(0, 0, "unknown")
        self.write_thread(0, 0, 0, -1, -1)
        self.write_comm(0, "unknown", 0, 0, 0)
        self.write_dso(0, 0, "unknown", "unknown", "")
        self.write_symbol(0, 0, 0, 0, 0, "unknown")
        self.write_call_path(0, 0, 0, 0)
        # Sentinel id = 0 rows in sample_table.bin and call_table.bin satisfy
        # PostgreSQL FOREIGN KEY constraints (call_id/return_id -> samples.id
        # and parent references) when a call or return event has no matching
        # sample reference (0).
        self.files['sample'].write(struct.pack(
            "!hiqiqiqiqiqiqiqiQiQiQiIiqiqiQiQiQiQiQiQiiiBiqiQiQii",
            25, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 4, 0,
            8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 4, 0, 1, 0, 8, 0, 8, 0, 8, 0, 4, 0
        ))
        self.files['call'].write(struct.pack(
            "!hiqiqiqiqiqiqiqiqiqiqiiiqiqiq",
            14, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 8, 0, 4, 0, 8, 0, 8, 0, 8, 0
        ))
        for b_type, b_name in BRANCH_TYPES:
            self.write_branch_type(b_type, b_name)

    def write_branch_type(self, branch_type: int, name: str) -> None:
        name_bytes = toserverstr(name)
        n = len(name_bytes)
        fmt = "!hiii" + str(n) + "s"
        value = struct.pack(fmt, 2, 4, branch_type, n, name_bytes)
        self.files['branch_type'].write(value)

    def write_ptwrite(self, id_: int, raw_buf: bytes) -> None:
        data = struct.unpack_from("<IQ", raw_buf)
        flags, payload = data[0], data[1]
        value = struct.pack("!hiqiQiB", 3, 8, id_, 8, payload, 1, flags & 1)
        self.files['ptwrite'].write(value)

    def write_cbr(self, id_: int, raw_buf: bytes) -> None:
        data = struct.unpack_from("<BBBBII", raw_buf)
        cbr_val, max_nonturbo, freq = data[0], data[2], data[4]
        mhz = (freq + 500) // 1000
        percent = ((cbr_val * 1000 // max_nonturbo) + 5) // 10 if max_nonturbo else 0
        value = struct.pack("!hiqiiiiii", 4, 8, id_, 4, cbr_val, 4, int(mhz), 4, int(percent))
        self.files['cbr'].write(value)

    def write_mwait(self, id_: int, raw_buf: bytes) -> None:
        data = struct.unpack_from("<IQ", raw_buf)
        payload = data[1]
        hints = payload & 0xff
        extensions = (payload >> 32) & 0x3
        value = struct.pack("!hiqiiii", 3, 8, id_, 4, hints, 4, extensions)
        self.files['mwait'].write(value)

    def write_pwre(self, id_: int, raw_buf: bytes) -> None:
        data = struct.unpack_from("<IQ", raw_buf)
        payload = data[1]
        hw = (payload >> 7) & 1
        cstate = (payload >> 12) & 0xf
        subcstate = (payload >> 8) & 0xf
        value = struct.pack("!hiqiiiiiB", 4, 8, id_, 4, cstate, 4, subcstate, 1, hw)
        self.files['pwre'].write(value)

    def write_exstop(self, id_: int, raw_buf: bytes) -> None:
        data = struct.unpack_from("<I", raw_buf)
        flags = data[0]
        value = struct.pack("!hiqiB", 2, 8, id_, 1, flags & 1)
        self.files['exstop'].write(value)

    def write_pwrx(self, id_: int, raw_buf: bytes) -> None:
        data = struct.unpack_from("<IQ", raw_buf)
        payload = data[1]
        deepest = payload & 0xf
        last = (payload >> 4) & 0xf
        wake = (payload >> 8) & 0xf
        value = struct.pack("!hiqiiiiii", 4, 8, id_, 4, deepest, 4, last, 4, wake)
        self.files['pwrx'].write(value)

    def write_evsel(self, evsel_id: int, name: str) -> None:
        """Write event to binary file."""
        name_bytes = toserverstr(name)
        n = len(name_bytes)
        fmt = "!hiqi" + str(n) + "s"
        value = struct.pack(fmt, 2, 8, evsel_id, n, name_bytes)
        self.files['evsel'].write(value)

    def write_machine(self, machine_id: int, pid: int, root_dir: str) -> None:
        """Write machine to binary file."""
        rd_bytes = toserverstr(root_dir)
        n = len(rd_bytes)
        fmt = "!hiqiii" + str(n) + "s"
        value = struct.pack(fmt, 3, 8, machine_id, 4, pid, n, rd_bytes)
        self.files['machine'].write(value)

    def write_thread(self, thread_id: int, machine_id: int, process_id: int,
                     pid: int, tid: int) -> None:
        """Write thread to binary file."""
        value = struct.pack("!hiqiqiqiIiI", 5, 8, thread_id, 8, machine_id,
                            8, process_id, 4, pid & 0xffffffff, 4, tid & 0xffffffff)
        self.files['thread'].write(value)

    def write_comm(self, comm_id: int, comm_str: str, thread_id: int,
                   time: int, exec_flag: int) -> None:
        """Write comm to binary file."""
        comm_bytes = toserverstr(comm_str)
        n = len(comm_bytes)
        fmt = "!hiqi" + str(n) + "s" + "iqiqiB"
        value = struct.pack(fmt, 5, 8, comm_id, n, comm_bytes, 8,
                            thread_id, 8, time, 1, exec_flag)
        self.files['comm'].write(value)

    def write_comm_thread(self, comm_thread_id: int, comm_id: int,
                          thread_id: int) -> None:
        """Write comm_thread to binary file."""
        fmt = "!hiqiqiq"
        value = struct.pack(fmt, 3, 8, comm_thread_id, 8, comm_id, 8, thread_id)
        self.files['comm_thread'].write(value)

    def write_dso(self, dso_id: int, machine_id: int, short_name: str,
                  long_name: str, build_id: str) -> None:
        """Write DSO to binary file."""
        sn_bytes = toserverstr(short_name)
        ln_bytes = toserverstr(long_name)
        bi_bytes = toserverstr(build_id)
        n1, n2, n3 = len(sn_bytes), len(ln_bytes), len(bi_bytes)
        fmt = "!hiqiqi" + str(n1) + "si" + str(n2) + "si" + str(n3) + "s"
        value = struct.pack(fmt, 5, 8, dso_id, 8, machine_id, n1,
                            sn_bytes, n2, ln_bytes, n3, bi_bytes)
        self.files['dso'].write(value)

    def write_symbol(self, symbol_id: int, dso_id: int, sym_start: int,
                      sym_end: int, binding: int, symbol_name: str) -> None:
        """Write symbol to binary file."""
        name_bytes = toserverstr(symbol_name)
        n = len(name_bytes)
        fmt = "!hiqiqiQiQiii" + str(n) + "s"
        value = struct.pack(fmt, 6, 8, symbol_id, 8, dso_id, 8,
                            sym_start, 8, sym_end, 4, binding, n, name_bytes)
        self.files['symbol'].write(value)

    def write_call_path(self, cp_id: int, parent_id: int, symbol_id: int,
                         ip: int) -> None:
        """Write call path to binary file."""
        fmt = "!hiqiqiqiQ"
        value = struct.pack(fmt, 4, 8, cp_id, 8, parent_id, 8, symbol_id, 8, ip)
        self.files['call_path'].write(value)

    def write_sample(self, sample_id: int, evsel_id: int, machine_id: int, thread_id: int,
                      comm_id: int, dso_id: int, symbol_id: int,
                      sample: perf.sample_event, call_path_id: int) -> None:
        """Write sample to binary file."""
        addr_dso_id = self.get_dso_id(
            sample.addr_dso or "Unknown_dso", sample.addr_dso or "Unknown_dso_long", "",
            machine_id
        )
        addr_symbol_id = self.get_symbol_id(
            addr_dso_id, sample.addr_symbol or "Unknown_symbol", 0, 0
        )
        value = struct.pack(
            "!hiqiqiqiqiqiqiqiQiQiQiIiqiqiQiQiQiQiQiQiiiBiqiQiQii",
            25, 8, sample_id, 8, evsel_id, 8, machine_id, 8, thread_id, 8, comm_id,
            8, dso_id, 8, symbol_id, 8, getattr(sample, 'sym_offset', 0) or 0,
            8, getattr(sample, 'sample_ip', 0) or 0,
            8, getattr(sample, 'sample_time', 0) or 0,
            4, getattr(sample, 'sample_cpu', 0) & 0xffffffff,
            8, addr_dso_id,
            8, addr_symbol_id,
            8, getattr(sample, 'addr_sym_offset', 0) or 0,
            8, getattr(sample, 'sample_addr', 0) or 0,
            8, getattr(sample, 'sample_period', 0) or 0,
            8, getattr(sample, 'sample_weight', 0) or 0,
            8, getattr(sample, 'transaction', 0) or 0,
            8, getattr(sample, 'sample_data_src', 0) or 0 or 0,
            4, getattr(sample, 'branch_type', 0) or 0,
            1, getattr(sample, 'in_tx', 0) or 0,
            8, call_path_id,
            8, getattr(sample, 'sample_insn_count', 0) or 0 or 0,
            8, getattr(sample, 'sample_cyc_count', 0) or 0 or 0,
            4, getattr(sample, 'flags', 0) or 0
        )
        self.files['sample'].write(value)

    def get_machine_id(self, machine_pid: Optional[int]) -> int:
        """Get or create machine ID."""
        if machine_pid is None or machine_pid <= 0 or machine_pid > 0x7fffffff:
            machine_pid = -1
        if machine_pid in self.caches['machines']:
            return self.caches['machines'][machine_pid]
        machine_id = self.next_id['machine']
        self.next_id['machine'] += 1
        self.write_machine(machine_id, machine_pid, "")
        self.caches['machines'][machine_pid] = machine_id
        return machine_id

    def get_event_id(self, name: str) -> int:
        """Get or create event ID."""
        if name in self.caches['events']:
            return self.caches['events'][name]
        event_id = self.next_id['event']
        self.write_evsel(event_id, name)
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
        process_id = self.get_thread_id(machine_id, pid, pid) if tid != pid else -1
        thread_id = self.next_id['thread']
        if process_id == -1: process_id = thread_id
        self.write_thread(thread_id, machine_id, process_id, pid, tid)
        self.caches['threads'][key] = thread_id
        self.next_id['thread'] += 1
        return thread_id

    def get_comm_id(self, comm: str, thread_id: int) -> int:
        """Get or create comm ID."""
        c_key = (comm, thread_id)
        if c_key in self.caches['comms']:
            comm_id = self.caches['comms'][c_key]
        else:
            comm_id = self.next_id['comm']
            self.write_comm(comm_id, comm, thread_id, 0, 0)
            self.caches['comms'][c_key] = comm_id
            self.next_id['comm'] += 1

        key = (comm_id, thread_id)
        if 'comm_threads' not in self.caches:
            self.caches['comm_threads'] = {}
        if key not in self.caches['comm_threads']:
            comm_thread_id = self.next_id['comm_thread']
            self.write_comm_thread(comm_thread_id, comm_id, thread_id)
            self.caches['comm_threads'][key] = True
            self.next_id['comm_thread'] += 1

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
        self.write_dso(dso_id, machine_id, short_name, long_name, build_id)
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
                self.caches['symbols'][key] = (symbol_id, start, end)
                self.symbol_rows[symbol_id] = (dso_id, start, end, 0, name)
            return symbol_id
        symbol_id = self.next_id['symbol']
        self.caches['symbols'][key] = (symbol_id, start, end)
        self.symbol_rows[symbol_id] = (dso_id, start, end, 0, name)
        self.next_id['symbol'] += 1
        return symbol_id

    def get_call_path_id(self, parent_id: int, symbol_id: int,
                         ip: int) -> int:
        """Get or create call path ID."""
        key = (parent_id, symbol_id, ip)
        if key in self.caches['call_paths']:
            return self.caches['call_paths'][key]
        call_path_id = self.next_id['call_path']
        self.write_call_path(call_path_id, parent_id, symbol_id, ip)
        self.caches['call_paths'][key] = call_path_id
        self.next_id['call_path'] += 1
        return call_path_id

    def process_context_switch(self, event: typing.Any) -> None:
        """Callback for processing context switch events."""
        misc = getattr(event, 'misc', 0)
        out = bool(misc & (1 << 13))
        out_preempt = bool(misc & (1 << 14))
        flags = (1 if out else 0) | ((1 if out_preempt else 0) << 1)
        machine_id = self.get_machine_id(getattr(event, 'machine_pid', None))

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
                    comm_a_name = proc.comm() or "Unknown_comm"
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
                    comm_b_name = proc.comm() or "Unknown_comm"
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

        cs_id = self.next_id['context_switch']
        self.next_id['context_switch'] += 1
        time = getattr(event, "time", getattr(event, "sample_time", 0))
        cpu = getattr(event, "sample_cpu", 0) or 0
        if cpu > 0x7fffffff:
            cpu -= 0x100000000

        # 9 columns total:
        fmt = "!hiQiqiQiiiqiqiqiqii"
        value = struct.pack(
            fmt, 9, 8, cs_id, 8, machine_id, 8, time, 4, cpu,
            8, th_out_id, 8, comm_out_id, 8, th_in_id, 8, comm_in_id, 4, flags
        )
        self.files["context_switches"].write(value)

    def get_call_path_ids(self, call_path: typing.Any, machine_id: int = 0) -> tuple[int, int]:
        """Add a perf.callchain as call_path rows, return its id and parent's."""
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
        machine_id = self.get_machine_id(getattr(cr, "machine_pid", 0) or 0)
        thread_id = self.get_thread_id(machine_id,
                                       cr.pid if cr.pid is not None else -1,
                                       cr.tid if cr.tid is not None else -1)
        comm_id = self.get_comm_id(cr.comm or "Unknown_comm", thread_id)
        call_path_id, parent_call_path_id = self.get_call_path_ids(cr.call_path or [], machine_id)

        fmt = "!hiqiqiqiqiqiqiqiqiqiqiiiqiqiq"
        # call.id and call.parent_id use the db_id of the perf module, as a call
        # is given an id before it returns its parent cannot be numbered here.
        value = struct.pack(
            fmt, 14, 8, cr.db_id, 8, thread_id, 8, comm_id, 8, call_path_id,
            8, cr.call_time, 8, cr.return_time, 8, cr.branch_count,
            8, cr.call_ref, 8, cr.return_ref,
            8, parent_call_path_id, 4, cr.flags, 8, cr.parent_id,
            8, cr.insn_count, 8, cr.cyc_count
        )
        self.files['call'].write(value)

    def process_event(self, sample: typing.Any) -> None:
        """Callback for processing events."""

        machine_db_id = getattr(sample, 'machine_pid', 0)
        machine_id = self.get_machine_id(machine_db_id)
        thread_id = self.get_thread_id(machine_id, sample.sample_pid, sample.sample_tid)

        comm = "Unknown_comm"
        try:
            if self.session is not None:
                proc = self.session.find_thread(sample.sample_pid, sample.sample_tid)
                if proc:
                    comm = proc.comm() or "Unknown_comm"
        except TypeError:
            pass
        comm_id = self.get_comm_id(comm, thread_id)

        dso_id = self.get_dso_id(
            sample.dso or "Unknown_dso",
            sample.dso_long_name or "Unknown_dso_long",
            sample.dso_bid or "",
            machine_id
        )

        symbol_id = self.get_symbol_id(
            dso_id,
            sample.symbol or "Unknown_symbol",
            sample.sym_start or 0,
            sample.sym_end or 0
        )

        call_path_id = 0
        if hasattr(sample, 'callchain') and sample.callchain:
            parent_id = 0
            for node in reversed(sample.callchain):
                node_dso = getattr(node, 'dso', None) or getattr(node, 'map', None)
                node_symbol = getattr(node, 'symbol', None) or getattr(node, 'sym', None)

                dso_name = "Unknown_dso"
                if node_dso:
                    dso_name = (node_dso if isinstance(node_dso, str)
                                else getattr(node_dso, 'name', "Unknown_dso") or "Unknown_dso")

                symbol_name = "Unknown_symbol"
                if node_symbol:
                    symbol_name = (node_symbol if isinstance(node_symbol, str)
                                   else getattr(node_symbol, 'name', "Unknown_symbol")
                                   or "Unknown_symbol")

                node_dso_id = self.get_dso_id(dso_name, dso_name, "", machine_id)
                node_symbol_id = self.get_symbol_id(node_dso_id, symbol_name, 0, 0)

                parent_id = self.get_call_path_id(parent_id, node_symbol_id, node.ip)
            call_path_id = parent_id

        sample_id = self.next_id['sample']
        evsel_name = str(sample.evsel)
        if evsel_name.startswith("evsel(") and evsel_name.endswith(")"):
            evsel_name = evsel_name[6:-1]
        self.write_sample(sample_id,
                          self.get_event_id(evsel_name),
                          machine_id, thread_id, comm_id, dso_id, symbol_id, sample,
                          call_path_id)

        event_name_str = evsel_name
        if hasattr(sample, "raw_buf"):
            if event_name_str == "ptwrite":
                self.write_ptwrite(sample_id, sample.raw_buf)
            elif event_name_str == "cbr":
                self.write_cbr(sample_id, sample.raw_buf)
            elif event_name_str == "mwait":
                self.write_mwait(sample_id, sample.raw_buf)
            elif event_name_str == "pwre":
                self.write_pwre(sample_id, sample.raw_buf)
            elif event_name_str == "exstop":
                self.write_exstop(sample_id, sample.raw_buf)
            elif event_name_str == "pwrx":
                self.write_pwrx(sample_id, sample.raw_buf)

        self.next_id['sample'] += 1

    def finalize(self) -> None:
        """Copy files to database and add keys/views."""
        print("Copying to database...")
        for symbol_id, (dso_id, sym_start, sym_end, binding, name) in self.symbol_rows.items():
            self.write_symbol(symbol_id, dso_id, sym_start, sym_end, binding, name)
        for name, f in self.files.items():
            self.close_output_file(f)

        table_mapping = {
            'evsel': 'selected_events',
            'machine': 'machines',
            'thread': 'threads',
            'comm': 'comms',
            'comm_thread': 'comm_threads',
            'dso': 'dsos',
            'symbol': 'symbols',
            'branch_type': 'branch_types',
            'sample': 'samples',
            'call': 'calls',
            'call_path': 'call_paths',
            'context_switches': 'context_switches',
            'ptwrite': 'ptwrite',
            'cbr': 'cbr',
            'mwait': 'mwait',
            'pwre': 'pwre',
            'exstop': 'exstop',
            'pwrx': 'pwrx'
        }
        for name, f in self.files.items():
            table_name = table_mapping.get(name, name + "s")
            self.copy_output_file(f.name, table_name)

        print("Removing intermediate files...")
        for name, f in self.files.items():
            os.unlink(f.name)
        shutil.rmtree(self.output_dir_name, ignore_errors=True)

        print("Adding primary keys")
        self.do_query("ALTER TABLE selected_events ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE machines        ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE threads         ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE comms           ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE comm_threads    ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE dsos            ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE symbols         ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE branch_types    ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE samples         ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE call_paths      ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE calls           ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE ptwrite         ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE cbr             ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE mwait           ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE pwre            ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE exstop          ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE pwrx            ADD PRIMARY KEY (id)")
        self.do_query("ALTER TABLE context_switches ADD PRIMARY KEY (id)")

        print("Adding foreign keys")
        self.do_query('ALTER TABLE threads '
                      'ADD CONSTRAINT machinefk FOREIGN KEY (machine_id) REFERENCES machines (id),'
                      'ADD CONSTRAINT processfk FOREIGN KEY (process_id) REFERENCES threads (id)')
        self.do_query('ALTER TABLE comms '
                      'ADD CONSTRAINT threadfk FOREIGN KEY (c_thread_id) REFERENCES threads (id)')
        self.do_query('ALTER TABLE comm_threads '
                      'ADD CONSTRAINT commfk FOREIGN KEY (comm_id) REFERENCES comms (id),'
                      'ADD CONSTRAINT threadfk FOREIGN KEY (thread_id) REFERENCES threads (id)')
        self.do_query('ALTER TABLE dsos '
                      'ADD CONSTRAINT machinefk FOREIGN KEY (machine_id) REFERENCES machines (id)')
        self.do_query('ALTER TABLE symbols '
                      'ADD CONSTRAINT dsofk FOREIGN KEY (dso_id) REFERENCES dsos (id)')
        self.do_query('ALTER TABLE samples '
                      'ADD CONSTRAINT evselfk FOREIGN KEY (evsel_id) '
                      'REFERENCES selected_events (id),'
                      'ADD CONSTRAINT machinefk FOREIGN KEY (machine_id) REFERENCES machines (id),'
                      'ADD CONSTRAINT threadfk FOREIGN KEY (thread_id) REFERENCES threads (id),'
                      'ADD CONSTRAINT commfk FOREIGN KEY (comm_id) REFERENCES comms (id),'
                      'ADD CONSTRAINT dsofk FOREIGN KEY (dso_id) REFERENCES dsos (id),'
                      'ADD CONSTRAINT symbolfk FOREIGN KEY (symbol_id) REFERENCES symbols (id),'
                      'ADD CONSTRAINT todsofk FOREIGN KEY (to_dso_id) REFERENCES dsos (id),'
                      'ADD CONSTRAINT tosymbolfk FOREIGN KEY (to_symbol_id) '
                      'REFERENCES symbols (id)')
        self.do_query('ALTER TABLE call_paths '
                      'ADD CONSTRAINT parentfk FOREIGN KEY (parent_id) REFERENCES call_paths (id),'
                      'ADD CONSTRAINT symbolfk FOREIGN KEY (symbol_id) REFERENCES symbols (id)')
        self.do_query('ALTER TABLE calls '
                      'ADD CONSTRAINT threadfk FOREIGN KEY (thread_id) REFERENCES threads (id),'
                      'ADD CONSTRAINT commfk FOREIGN KEY (comm_id) REFERENCES comms (id),'
                      'ADD CONSTRAINT call_pathfk FOREIGN KEY (call_path_id) '
                      'REFERENCES call_paths (id),'
                      'ADD CONSTRAINT callfk FOREIGN KEY (call_id) REFERENCES samples (id),'
                      'ADD CONSTRAINT returnfk FOREIGN KEY (return_id) REFERENCES samples (id),'
                      'ADD CONSTRAINT parent_call_pathfk FOREIGN KEY (parent_call_path_id) '
                      'REFERENCES call_paths (id)')
        # pcpid_idx, pid_idx, and comms.has_calls are required by
        # exported-sql-viewer.py to filter the command dropdown to threads with
        # call graphs and to expand parent/child call trees efficiently.
        self.do_query('CREATE INDEX pcpid_idx ON calls (parent_call_path_id)')
        self.do_query('CREATE INDEX pid_idx ON calls (parent_id)')
        self.do_query('ALTER TABLE comms ADD has_calls boolean')
        self.do_query('UPDATE comms SET has_calls = TRUE WHERE comms.id IN '
                      '(SELECT DISTINCT comm_id FROM calls)')
        self.do_query('ALTER TABLE context_switches '
                      'ADD CONSTRAINT machinefk FOREIGN KEY (machine_id) REFERENCES machines (id),'
                      'ADD CONSTRAINT toutfk FOREIGN KEY (thread_out_id) REFERENCES threads (id),'
                      'ADD CONSTRAINT tinfk FOREIGN KEY (thread_in_id) REFERENCES threads (id),'
                      'ADD CONSTRAINT coutfk FOREIGN KEY (comm_out_id) REFERENCES comms (id),'
                      'ADD CONSTRAINT cinfk FOREIGN KEY (comm_in_id) REFERENCES comms (id)')
        self.do_query('ALTER TABLE ptwrite '
                      'ADD CONSTRAINT idfk FOREIGN KEY (id) REFERENCES samples (id)')
        self.do_query('ALTER TABLE cbr '
                      'ADD CONSTRAINT idfk FOREIGN KEY (id) REFERENCES samples (id)')
        self.do_query('ALTER TABLE mwait '
                      'ADD CONSTRAINT idfk FOREIGN KEY (id) REFERENCES samples (id)')
        self.do_query('ALTER TABLE pwre '
                      'ADD CONSTRAINT idfk FOREIGN KEY (id) REFERENCES samples (id)')
        self.do_query('ALTER TABLE exstop '
                      'ADD CONSTRAINT idfk FOREIGN KEY (id) REFERENCES samples (id)')
        self.do_query('ALTER TABLE pwrx '
                      'ADD CONSTRAINT idfk FOREIGN KEY (id) REFERENCES samples (id)')

        print("Creating views...")
        self.do_query(
            "CREATE VIEW machines_view AS "
            "SELECT id, pid, root_dir, "
            "CASE WHEN id=0 THEN 'unknown' WHEN pid=-1 THEN 'host' ELSE 'guest' END "
            "AS host_or_guest FROM machines"
        )
        self.do_query(
            "CREATE VIEW dsos_view AS "
            "SELECT id, machine_id, "
            "(SELECT host_or_guest FROM machines_view WHERE id = machine_id) AS host_or_guest, "
            "short_name, long_name, build_id FROM dsos"
        )
        self.do_query(
            "CREATE VIEW symbols_view AS "
            "SELECT id, name, (SELECT short_name FROM dsos WHERE id=dso_id) AS dso, "
            "dso_id, sym_start, sym_end, "
            "CASE WHEN binding=0 THEN 'local' WHEN binding=1 THEN 'global' ELSE 'weak' END "
            "AS binding FROM symbols"
        )
        self.do_query(
            "CREATE VIEW threads_view AS "
            "SELECT id, machine_id, "
            "(SELECT host_or_guest FROM machines_view WHERE id = machine_id) AS host_or_guest, "
            "process_id, pid, tid FROM threads"
        )
        self.do_query(
            "CREATE VIEW comm_threads_view AS "
            "SELECT comm_id, (SELECT comm FROM comms WHERE id = comm_id) AS command, "
            "thread_id, (SELECT pid FROM threads WHERE id = thread_id) AS pid, "
            "(SELECT tid FROM threads WHERE id = thread_id) AS tid FROM comm_threads"
        )
        self.do_query(
            "CREATE VIEW call_paths_view AS "
            "SELECT c.id, to_hex(c.ip) AS ip, c.symbol_id, "
            "(SELECT name FROM symbols WHERE id = c.symbol_id) AS symbol, "
            "(SELECT dso_id FROM symbols WHERE id = c.symbol_id) AS dso_id, "
            "(SELECT dso FROM symbols_view WHERE id = c.symbol_id) AS dso_short_name, "
            "c.parent_id, to_hex(p.ip) AS parent_ip, p.symbol_id AS parent_symbol_id, "
            "(SELECT name FROM symbols WHERE id = p.symbol_id) AS parent_symbol, "
            "(SELECT dso_id FROM symbols WHERE id = p.symbol_id) AS parent_dso_id, "
            "(SELECT dso FROM symbols_view WHERE id = p.symbol_id) AS parent_dso_short_name "
            "FROM call_paths c LEFT JOIN call_paths p ON p.id = c.parent_id"
        )
        self.do_query(
            "CREATE VIEW calls_view AS "
            "SELECT calls.id, thread_id, "
            "(SELECT pid FROM threads WHERE id = thread_id) AS pid, "
            "(SELECT tid FROM threads WHERE id = thread_id) AS tid, "
            "(SELECT comm FROM comms WHERE id = comm_id) AS command, "
            "call_path_id, to_hex(ip) AS ip, symbol_id, "
            "(SELECT name FROM symbols WHERE id = symbol_id) AS symbol, "
            "call_time, return_time, return_time - call_time AS elapsed_time, "
            "branch_count, insn_count, cyc_count, "
            "CASE WHEN cyc_count=0 THEN CAST(0 AS FLOAT) "
            "ELSE CAST(insn_count AS FLOAT) / cyc_count END AS IPC, "
            "call_id, return_id, "
            "CASE WHEN flags=0 THEN '' WHEN flags=1 THEN 'no call' "
            "WHEN flags=2 THEN 'no return' WHEN flags=3 THEN 'no call/return' "
            "WHEN flags IN (4, 6) THEN 'jmp' ELSE CAST(flags AS VARCHAR(6)) END AS flags, "
            "parent_call_path_id, calls.parent_id "
            "FROM calls INNER JOIN call_paths ON call_paths.id = call_path_id"
        )
        self.do_query(
            "CREATE VIEW samples_view AS "
            "SELECT id, time, cpu, "
            "(SELECT pid FROM threads WHERE id = thread_id) AS pid, "
            "(SELECT tid FROM threads WHERE id = thread_id) AS tid, "
            "(SELECT comm FROM comms WHERE id = comm_id) AS command, "
            "(SELECT name FROM selected_events WHERE id = evsel_id) AS event, "
            "to_hex(ip) AS ip_hex, "
            "(SELECT name FROM symbols WHERE id = symbol_id) AS symbol, sym_offset, "
            "(SELECT short_name FROM dsos WHERE id = dso_id) AS dso_short_name, "
            "to_hex(to_ip) AS to_ip_hex, "
            "(SELECT name FROM symbols WHERE id = to_symbol_id) AS to_symbol, to_sym_offset, "
            "(SELECT short_name FROM dsos WHERE id = to_dso_id) AS to_dso_short_name, "
            "(SELECT name FROM branch_types WHERE id = branch_type) AS branch_type_name, "
            "in_tx, call_path_id, insn_count, cyc_count, "
            "CASE WHEN cyc_count=0 THEN CAST(0 AS FLOAT) "
            "ELSE CAST(insn_count AS FLOAT) / cyc_count END AS IPC, flags FROM samples"
        )
        self.do_query(
            "CREATE VIEW context_switches_view AS "
            "SELECT context_switches.id, context_switches.machine_id, "
            "context_switches.time, context_switches.cpu, "
            "th_out.pid AS pid_out, th_out.tid AS tid_out, comm_out.comm AS comm_out, "
            "th_in.pid AS pid_in, th_in.tid AS tid_in, comm_in.comm AS comm_in, "
            "CASE WHEN flags=0 THEN 'in' WHEN flags=1 THEN 'out' "
            "WHEN flags=3 THEN 'out preempt' ELSE CAST(flags AS VARCHAR(6)) END AS flags "
            "FROM context_switches "
            "INNER JOIN threads AS th_out ON th_out.id   = context_switches.thread_out_id "
            "INNER JOIN threads AS th_in  ON th_in.id    = context_switches.thread_in_id "
            "INNER JOIN comms AS comm_out ON comm_out.id = context_switches.comm_out_id "
            "INNER JOIN comms AS comm_in  ON comm_in.id  = context_switches.comm_in_id"
        )
        self.do_query(
            "CREATE VIEW ptwrite_view AS "
            "SELECT ptwrite.id, time, cpu, to_hex(payload) AS payload_hex, "
            "CASE WHEN exact_ip=FALSE THEN 'False' ELSE 'True' END AS exact_ip "
            "FROM ptwrite INNER JOIN samples ON samples.id = ptwrite.id"
        )
        self.do_query(
            "CREATE VIEW cbr_view AS "
            "SELECT cbr.id, time, cpu, cbr, mhz, percent "
            "FROM cbr INNER JOIN samples ON samples.id = cbr.id"
        )
        self.do_query(
            "CREATE VIEW mwait_view AS "
            "SELECT mwait.id, time, cpu, to_hex(hints) AS hints_hex, "
            "to_hex(extensions) AS extensions_hex "
            "FROM mwait INNER JOIN samples ON samples.id = mwait.id"
        )
        self.do_query(
            "CREATE VIEW pwre_view AS "
            "SELECT pwre.id, time, cpu, cstate, subcstate, "
            "CASE WHEN hw=FALSE THEN 'False' ELSE 'True' END AS hw "
            "FROM pwre INNER JOIN samples ON samples.id = pwre.id"
        )
        self.do_query(
            "CREATE VIEW exstop_view AS "
            "SELECT exstop.id, time, cpu, "
            "CASE WHEN exact_ip=FALSE THEN 'False' ELSE 'True' END AS exact_ip "
            "FROM exstop INNER JOIN samples ON samples.id = exstop.id"
        )
        self.do_query(
            "CREATE VIEW pwrx_view AS "
            "SELECT pwrx.id, time, cpu, deepest_cstate, last_cstate, "
            "CASE WHEN wake_reason=1 THEN 'Interrupt' "
            "WHEN wake_reason=2 THEN 'Timer Deadline' "
            "WHEN wake_reason=4 THEN 'Monitored Address' "
            "WHEN wake_reason=8 THEN 'HW' WHEN wake_reason=16 THEN 'Other' "
            "ELSE CAST ( wake_reason AS VARCHAR(2) ) END AS wake_reason "
            "FROM pwrx INNER JOIN samples ON samples.id = pwrx.id"
        )
        self.do_query(
            "CREATE VIEW power_events_view AS "
            "SELECT samples.id, samples.time, samples.cpu, selected_events.name AS event, "
            "FORMAT('%6s', cbr.cbr) AS cbr, FORMAT('%6s', cbr.mhz) AS MHz, "
            "FORMAT('%5s', cbr.percent) AS percent, to_hex(mwait.hints) AS hints_hex, "
            "to_hex(mwait.extensions) AS extensions_hex, FORMAT('%3s', pwre.cstate) AS cstate, "
            "FORMAT('%3s', pwre.subcstate) AS subcstate, "
            "CASE WHEN pwre.hw=FALSE THEN 'False' WHEN pwre.hw=TRUE THEN 'True' "
            "ELSE NULL END AS hw, "
            "CASE WHEN exstop.exact_ip=FALSE THEN 'False' WHEN exstop.exact_ip=TRUE THEN 'True' "
            "ELSE NULL END AS exact_ip, FORMAT('%3s', pwrx.deepest_cstate) AS deepest_cstate, "
            "FORMAT('%3s', pwrx.last_cstate) AS last_cstate, "
            "CASE WHEN pwrx.wake_reason=1 THEN 'Interrupt' "
            "WHEN wake_reason=2 THEN 'Timer Deadline' "
            "WHEN wake_reason=4 THEN 'Monitored Address' "
            "WHEN wake_reason=8 THEN 'HW' WHEN wake_reason=16 THEN 'Other' "
            "ELSE FORMAT('%2s', pwrx.wake_reason) END AS wake_reason "
            "FROM cbr FULL JOIN mwait ON mwait.id = cbr.id "
            "FULL JOIN pwre ON pwre.id = cbr.id FULL JOIN exstop ON exstop.id = cbr.id "
            "FULL JOIN pwrx ON pwrx.id = cbr.id INNER JOIN samples ON samples.id = "
            "coalesce(cbr.id, mwait.id, pwre.id, exstop.id, pwrx.id) "
            "INNER JOIN selected_events ON selected_events.id = samples.evsel_id "
            "ORDER BY samples.id"
        )


if __name__ == "__main__":
    ap = argparse.ArgumentParser(
        description="Export perf data to a postgresql database")
    ap.add_argument("-i", "--input", default="perf.data",
                    help="Input file name")
    ap.add_argument("-o", "--output", required=True,
                    help="Output database name")
    ap.add_argument("--itrace", default=None,
                    help="itrace options, e.g. cr")
    args = ap.parse_args()

    exporter = PostgresExporter(args.output)

    session = None
    succeeded = False
    caught_error = False
    try:
        exporter.setup_db()
        session = perf.session(perf.data(args.input),
                               context_switch=exporter.process_context_switch,
                               sample=exporter.process_event,
                               call_return=exporter.process_call_return,
                               itrace=args.itrace)
        exporter.session = session
        try:
            session.process_events()
        finally:
            # Break the C perf.session -> bound method -> exporter reference cycle.
            exporter.session = None
        exporter.finalize()
        print(f"Successfully exported to {args.output}")
        succeeded = True
    except (OSError, RuntimeError, ValueError, KeyboardInterrupt) as e:
        if not isinstance(e, (KeyboardInterrupt, SystemExit)):
            import traceback
            traceback.print_exc()
        caught_error = True
    finally:
        exporter.session = None
        for out_file in exporter.files.values():
            try:
                if not out_file.closed:
                    out_file.close()
            except OSError:
                pass
        exporter.disconnect()
        if not succeeded:
            if (getattr(exporter, 'created_output_dir', False) and
                    os.path.exists(exporter.output_dir_name)):
                shutil.rmtree(exporter.output_dir_name, ignore_errors=True)
            if caught_error:
                sys.exit(1)
