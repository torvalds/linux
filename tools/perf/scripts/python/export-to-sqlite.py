# export-to-sqlite.py: export perf data to a sqlite3 database
# Copyright (c) 2017, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.

import os
import sys
import struct
import datetime

# To use this script you will need to have installed package python-pyside which
# provides LGPL-licensed Python bindings for Qt.  You will also need the package
# libqt4-sql-sqlite for Qt sqlite3 support.
#
# An example of using this script with Intel PT:
#
#	$ perf record -e intel_pt//u ls
#	$ perf script -s ~/libexec/perf-core/scripts/python/export-to-sqlite.py pt_example branches calls
#	2017-07-31 14:26:07.326913 Creating database...
#	2017-07-31 14:26:07.538097 Writing records...
#	2017-07-31 14:26:09.889292 Adding indexes
#	2017-07-31 14:26:09.958746 Done
#
# To browse the database, sqlite3 can be used e.g.
#
#	$ sqlite3 pt_example
#	sqlite> .header on
#	sqlite> select * from samples_view where id < 10;
#	sqlite> .mode column
#	sqlite> select * from samples_view where id < 10;
#	sqlite> .tables
#	sqlite> .schema samples_view
#	sqlite> .quit
#
# An example of using the database is provided by the script
# exported-sql-viewer.py.  Refer to that script for details.
#
# The database structure is practically the same as created by the script
# export-to-postgresql.py. Refer to that script for details.  A notable
# difference is  the 'transaction' column of the 'samples' table which is
# renamed 'transaction_' in sqlite because 'transaction' is a reserved word.

from PySide.QtSql import *

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

# These perf imports are not used at present
#from perf_trace_context import *
#from Core import *

perf_db_export_mode = True
perf_db_export_calls = False
perf_db_export_callchains = False

def usage():
	print >> sys.stderr, "Usage is: export-to-sqlite.py <database name> [<columns>] [<calls>] [<callchains>]"
	print >> sys.stderr, "where:	columns		'all' or 'branches'"
	print >> sys.stderr, "		calls		'calls' => create calls and call_paths table"
	print >> sys.stderr, "		callchains	'callchains' => create call_paths table"
	raise Exception("Too few arguments")

if (len(sys.argv) < 2):
	usage()

dbname = sys.argv[1]

if (len(sys.argv) >= 3):
	columns = sys.argv[2]
else:
	columns = "all"

if columns not in ("all", "branches"):
	usage()

branches = (columns == "branches")

for i in range(3,len(sys.argv)):
	if (sys.argv[i] == "calls"):
		perf_db_export_calls = True
	elif (sys.argv[i] == "callchains"):
		perf_db_export_callchains = True
	else:
		usage()

def do_query(q, s):
	if (q.exec_(s)):
		return
	raise Exception("Query failed: " + q.lastError().text())

def do_query_(q):
	if (q.exec_()):
		return
	raise Exception("Query failed: " + q.lastError().text())

print datetime.datetime.today(), "Creating database..."

db_exists = False
try:
	f = open(dbname)
	f.close()
	db_exists = True
except:
	pass

if db_exists:
	raise Exception(dbname + " already exists")

db = QSqlDatabase.addDatabase('QSQLITE')
db.setDatabaseName(dbname)
db.open()

query = QSqlQuery(db)

do_query(query, 'PRAGMA journal_mode = OFF')
do_query(query, 'BEGIN TRANSACTION')

do_query(query, 'CREATE TABLE selected_events ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'name		varchar(80))')
do_query(query, 'CREATE TABLE machines ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'pid		integer,'
		'root_dir 	varchar(4096))')
do_query(query, 'CREATE TABLE threads ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'machine_id	bigint,'
		'process_id	bigint,'
		'pid		integer,'
		'tid		integer)')
do_query(query, 'CREATE TABLE comms ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'comm		varchar(16))')
do_query(query, 'CREATE TABLE comm_threads ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'comm_id	bigint,'
		'thread_id	bigint)')
do_query(query, 'CREATE TABLE dsos ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'machine_id	bigint,'
		'short_name	varchar(256),'
		'long_name	varchar(4096),'
		'build_id	varchar(64))')
do_query(query, 'CREATE TABLE symbols ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'dso_id		bigint,'
		'sym_start	bigint,'
		'sym_end	bigint,'
		'binding	integer,'
		'name		varchar(2048))')
do_query(query, 'CREATE TABLE branch_types ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'name		varchar(80))')

if branches:
	do_query(query, 'CREATE TABLE samples ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'evsel_id	bigint,'
		'machine_id	bigint,'
		'thread_id	bigint,'
		'comm_id	bigint,'
		'dso_id		bigint,'
		'symbol_id	bigint,'
		'sym_offset	bigint,'
		'ip		bigint,'
		'time		bigint,'
		'cpu		integer,'
		'to_dso_id	bigint,'
		'to_symbol_id	bigint,'
		'to_sym_offset	bigint,'
		'to_ip		bigint,'
		'branch_type	integer,'
		'in_tx		boolean,'
		'call_path_id	bigint)')
else:
	do_query(query, 'CREATE TABLE samples ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'evsel_id	bigint,'
		'machine_id	bigint,'
		'thread_id	bigint,'
		'comm_id	bigint,'
		'dso_id		bigint,'
		'symbol_id	bigint,'
		'sym_offset	bigint,'
		'ip		bigint,'
		'time		bigint,'
		'cpu		integer,'
		'to_dso_id	bigint,'
		'to_symbol_id	bigint,'
		'to_sym_offset	bigint,'
		'to_ip		bigint,'
		'period		bigint,'
		'weight		bigint,'
		'transaction_	bigint,'
		'data_src	bigint,'
		'branch_type	integer,'
		'in_tx		boolean,'
		'call_path_id	bigint)')

if perf_db_export_calls or perf_db_export_callchains:
	do_query(query, 'CREATE TABLE call_paths ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'parent_id	bigint,'
		'symbol_id	bigint,'
		'ip		bigint)')
if perf_db_export_calls:
	do_query(query, 'CREATE TABLE calls ('
		'id		integer		NOT NULL	PRIMARY KEY,'
		'thread_id	bigint,'
		'comm_id	bigint,'
		'call_path_id	bigint,'
		'call_time	bigint,'
		'return_time	bigint,'
		'branch_count	bigint,'
		'call_id	bigint,'
		'return_id	bigint,'
		'parent_call_path_id	bigint,'
		'flags		integer)')

# printf was added to sqlite in version 3.8.3
sqlite_has_printf = False
try:
	do_query(query, 'SELECT printf("") FROM machines')
	sqlite_has_printf = True
except:
	pass

def emit_to_hex(x):
	if sqlite_has_printf:
		return 'printf("%x", ' + x + ')'
	else:
		return x

do_query(query, 'CREATE VIEW machines_view AS '
	'SELECT '
		'id,'
		'pid,'
		'root_dir,'
		'CASE WHEN id=0 THEN \'unknown\' WHEN pid=-1 THEN \'host\' ELSE \'guest\' END AS host_or_guest'
	' FROM machines')

do_query(query, 'CREATE VIEW dsos_view AS '
	'SELECT '
		'id,'
		'machine_id,'
		'(SELECT host_or_guest FROM machines_view WHERE id = machine_id) AS host_or_guest,'
		'short_name,'
		'long_name,'
		'build_id'
	' FROM dsos')

do_query(query, 'CREATE VIEW symbols_view AS '
	'SELECT '
		'id,'
		'name,'
		'(SELECT short_name FROM dsos WHERE id=dso_id) AS dso,'
		'dso_id,'
		'sym_start,'
		'sym_end,'
		'CASE WHEN binding=0 THEN \'local\' WHEN binding=1 THEN \'global\' ELSE \'weak\' END AS binding'
	' FROM symbols')

do_query(query, 'CREATE VIEW threads_view AS '
	'SELECT '
		'id,'
		'machine_id,'
		'(SELECT host_or_guest FROM machines_view WHERE id = machine_id) AS host_or_guest,'
		'process_id,'
		'pid,'
		'tid'
	' FROM threads')

do_query(query, 'CREATE VIEW comm_threads_view AS '
	'SELECT '
		'comm_id,'
		'(SELECT comm FROM comms WHERE id = comm_id) AS command,'
		'thread_id,'
		'(SELECT pid FROM threads WHERE id = thread_id) AS pid,'
		'(SELECT tid FROM threads WHERE id = thread_id) AS tid'
	' FROM comm_threads')

if perf_db_export_calls or perf_db_export_callchains:
	do_query(query, 'CREATE VIEW call_paths_view AS '
		'SELECT '
			'c.id,'
			+ emit_to_hex('c.ip') + ' AS ip,'
			'c.symbol_id,'
			'(SELECT name FROM symbols WHERE id = c.symbol_id) AS symbol,'
			'(SELECT dso_id FROM symbols WHERE id = c.symbol_id) AS dso_id,'
			'(SELECT dso FROM symbols_view  WHERE id = c.symbol_id) AS dso_short_name,'
			'c.parent_id,'
			+ emit_to_hex('p.ip') + ' AS parent_ip,'
			'p.symbol_id AS parent_symbol_id,'
			'(SELECT name FROM symbols WHERE id = p.symbol_id) AS parent_symbol,'
			'(SELECT dso_id FROM symbols WHERE id = p.symbol_id) AS parent_dso_id,'
			'(SELECT dso FROM symbols_view  WHERE id = p.symbol_id) AS parent_dso_short_name'
		' FROM call_paths c INNER JOIN call_paths p ON p.id = c.parent_id')
if perf_db_export_calls:
	do_query(query, 'CREATE VIEW calls_view AS '
		'SELECT '
			'calls.id,'
			'thread_id,'
			'(SELECT pid FROM threads WHERE id = thread_id) AS pid,'
			'(SELECT tid FROM threads WHERE id = thread_id) AS tid,'
			'(SELECT comm FROM comms WHERE id = comm_id) AS command,'
			'call_path_id,'
			+ emit_to_hex('ip') + ' AS ip,'
			'symbol_id,'
			'(SELECT name FROM symbols WHERE id = symbol_id) AS symbol,'
			'call_time,'
			'return_time,'
			'return_time - call_time AS elapsed_time,'
			'branch_count,'
			'call_id,'
			'return_id,'
			'CASE WHEN flags=1 THEN \'no call\' WHEN flags=2 THEN \'no return\' WHEN flags=3 THEN \'no call/return\' ELSE \'\' END AS flags,'
			'parent_call_path_id'
		' FROM calls INNER JOIN call_paths ON call_paths.id = call_path_id')

do_query(query, 'CREATE VIEW samples_view AS '
	'SELECT '
		'id,'
		'time,'
		'cpu,'
		'(SELECT pid FROM threads WHERE id = thread_id) AS pid,'
		'(SELECT tid FROM threads WHERE id = thread_id) AS tid,'
		'(SELECT comm FROM comms WHERE id = comm_id) AS command,'
		'(SELECT name FROM selected_events WHERE id = evsel_id) AS event,'
		+ emit_to_hex('ip') + ' AS ip_hex,'
		'(SELECT name FROM symbols WHERE id = symbol_id) AS symbol,'
		'sym_offset,'
		'(SELECT short_name FROM dsos WHERE id = dso_id) AS dso_short_name,'
		+ emit_to_hex('to_ip') + ' AS to_ip_hex,'
		'(SELECT name FROM symbols WHERE id = to_symbol_id) AS to_symbol,'
		'to_sym_offset,'
		'(SELECT short_name FROM dsos WHERE id = to_dso_id) AS to_dso_short_name,'
		'(SELECT name FROM branch_types WHERE id = branch_type) AS branch_type_name,'
		'in_tx'
	' FROM samples')

do_query(query, 'END TRANSACTION')

evsel_query = QSqlQuery(db)
evsel_query.prepare("INSERT INTO selected_events VALUES (?, ?)")
machine_query = QSqlQuery(db)
machine_query.prepare("INSERT INTO machines VALUES (?, ?, ?)")
thread_query = QSqlQuery(db)
thread_query.prepare("INSERT INTO threads VALUES (?, ?, ?, ?, ?)")
comm_query = QSqlQuery(db)
comm_query.prepare("INSERT INTO comms VALUES (?, ?)")
comm_thread_query = QSqlQuery(db)
comm_thread_query.prepare("INSERT INTO comm_threads VALUES (?, ?, ?)")
dso_query = QSqlQuery(db)
dso_query.prepare("INSERT INTO dsos VALUES (?, ?, ?, ?, ?)")
symbol_query = QSqlQuery(db)
symbol_query.prepare("INSERT INTO symbols VALUES (?, ?, ?, ?, ?, ?)")
branch_type_query = QSqlQuery(db)
branch_type_query.prepare("INSERT INTO branch_types VALUES (?, ?)")
sample_query = QSqlQuery(db)
if branches:
	sample_query.prepare("INSERT INTO samples VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
else:
	sample_query.prepare("INSERT INTO samples VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
if perf_db_export_calls or perf_db_export_callchains:
	call_path_query = QSqlQuery(db)
	call_path_query.prepare("INSERT INTO call_paths VALUES (?, ?, ?, ?)")
if perf_db_export_calls:
	call_query = QSqlQuery(db)
	call_query.prepare("INSERT INTO calls VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")

def trace_begin():
	print datetime.datetime.today(), "Writing records..."
	do_query(query, 'BEGIN TRANSACTION')
	# id == 0 means unknown.  It is easier to create records for them than replace the zeroes with NULLs
	evsel_table(0, "unknown")
	machine_table(0, 0, "unknown")
	thread_table(0, 0, 0, -1, -1)
	comm_table(0, "unknown")
	dso_table(0, 0, "unknown", "unknown", "")
	symbol_table(0, 0, 0, 0, 0, "unknown")
	sample_table(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
	if perf_db_export_calls or perf_db_export_callchains:
		call_path_table(0, 0, 0, 0)

unhandled_count = 0

def trace_end():
	do_query(query, 'END TRANSACTION')

	print datetime.datetime.today(), "Adding indexes"
	if perf_db_export_calls:
		do_query(query, 'CREATE INDEX pcpid_idx ON calls (parent_call_path_id)')

	if (unhandled_count):
		print datetime.datetime.today(), "Warning: ", unhandled_count, " unhandled events"
	print datetime.datetime.today(), "Done"

def trace_unhandled(event_name, context, event_fields_dict):
	global unhandled_count
	unhandled_count += 1

def sched__sched_switch(*x):
	pass

def bind_exec(q, n, x):
	for xx in x[0:n]:
		q.addBindValue(str(xx))
	do_query_(q)

def evsel_table(*x):
	bind_exec(evsel_query, 2, x)

def machine_table(*x):
	bind_exec(machine_query, 3, x)

def thread_table(*x):
	bind_exec(thread_query, 5, x)

def comm_table(*x):
	bind_exec(comm_query, 2, x)

def comm_thread_table(*x):
	bind_exec(comm_thread_query, 3, x)

def dso_table(*x):
	bind_exec(dso_query, 5, x)

def symbol_table(*x):
	bind_exec(symbol_query, 6, x)

def branch_type_table(*x):
	bind_exec(branch_type_query, 2, x)

def sample_table(*x):
	if branches:
		for xx in x[0:15]:
			sample_query.addBindValue(str(xx))
		for xx in x[19:22]:
			sample_query.addBindValue(str(xx))
		do_query_(sample_query)
	else:
		bind_exec(sample_query, 22, x)

def call_path_table(*x):
	bind_exec(call_path_query, 4, x)

def call_return_table(*x):
	bind_exec(call_query, 11, x)
