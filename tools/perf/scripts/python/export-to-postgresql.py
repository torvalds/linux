# export-to-postgresql.py: export perf data to a postgresql database
# Copyright (c) 2014, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.

from __future__ import print_function

import os
import sys
import struct
import datetime

# To use this script you will need to have installed package python-pyside which
# provides LGPL-licensed Python bindings for Qt.  You will also need the package
# libqt4-sql-psql for Qt postgresql support.
#
# The script assumes postgresql is running on the local machine and that the
# user has postgresql permissions to create databases. Examples of installing
# postgresql and adding such a user are:
#
# fedora:
#
#	$ sudo yum install postgresql postgresql-server qt-postgresql
#	$ sudo su - postgres -c initdb
#	$ sudo service postgresql start
#	$ sudo su - postgres
#	$ createuser -s <your user id here>    # Older versions may not support -s, in which case answer the prompt below:
#	Shall the new role be a superuser? (y/n) y
#	$ sudo yum install python-pyside
#
#	Alternately, to use Python3 and/or pyside 2, one of the following:
#		$ sudo yum install python3-pyside
#		$ pip install --user PySide2
#		$ pip3 install --user PySide2
#
# ubuntu:
#
#	$ sudo apt-get install postgresql
#	$ sudo su - postgres
#	$ createuser -s <your user id here>
#	$ sudo apt-get install python-pyside.qtsql libqt4-sql-psql
#
#	Alternately, to use Python3 and/or pyside 2, one of the following:
#
#		$ sudo apt-get install python3-pyside.qtsql libqt4-sql-psql
#		$ sudo apt-get install python-pyside2.qtsql libqt5sql5-psql
#		$ sudo apt-get install python3-pyside2.qtsql libqt5sql5-psql
#
# An example of using this script with Intel PT:
#
#	$ perf record -e intel_pt//u ls
#	$ perf script -s ~/libexec/perf-core/scripts/python/export-to-postgresql.py pt_example branches calls
#	2015-05-29 12:49:23.464364 Creating database...
#	2015-05-29 12:49:26.281717 Writing to intermediate files...
#	2015-05-29 12:49:27.190383 Copying to database...
#	2015-05-29 12:49:28.140451 Removing intermediate files...
#	2015-05-29 12:49:28.147451 Adding primary keys
#	2015-05-29 12:49:28.655683 Adding foreign keys
#	2015-05-29 12:49:29.365350 Done
#
# To browse the database, psql can be used e.g.
#
#	$ psql pt_example
#	pt_example=# select * from samples_view where id < 100;
#	pt_example=# \d+
#	pt_example=# \d+ samples_view
#	pt_example=# \q
#
# An example of using the database is provided by the script
# exported-sql-viewer.py.  Refer to that script for details.
#
# Tables:
#
#	The tables largely correspond to perf tools' data structures.  They are largely self-explanatory.
#
#	samples
#
#		'samples' is the main table. It represents what instruction was executing at a point in time
#		when something (a selected event) happened.  The memory address is the instruction pointer or 'ip'.
#
#	calls
#
#		'calls' represents function calls and is related to 'samples' by 'call_id' and 'return_id'.
#		'calls' is only created when the 'calls' option to this script is specified.
#
#	call_paths
#
#		'call_paths' represents all the call stacks.  Each 'call' has an associated record in 'call_paths'.
#		'calls_paths' is only created when the 'calls' option to this script is specified.
#
#	branch_types
#
#		'branch_types' provides descriptions for each type of branch.
#
#	comm_threads
#
#		'comm_threads' shows how 'comms' relates to 'threads'.
#
#	comms
#
#		'comms' contains a record for each 'comm' - the name given to the executable that is running.
#
#	dsos
#
#		'dsos' contains a record for each executable file or library.
#
#	machines
#
#		'machines' can be used to distinguish virtual machines if virtualization is supported.
#
#	selected_events
#
#		'selected_events' contains a record for each kind of event that has been sampled.
#
#	symbols
#
#		'symbols' contains a record for each symbol.  Only symbols that have samples are present.
#
#	threads
#
#		'threads' contains a record for each thread.
#
# Views:
#
#	Most of the tables have views for more friendly display.  The views are:
#
#		calls_view
#		call_paths_view
#		comm_threads_view
#		dsos_view
#		machines_view
#		samples_view
#		symbols_view
#		threads_view
#
# More examples of browsing the database with psql:
#   Note that some of the examples are not the most optimal SQL query.
#   Note that call information is only available if the script's 'calls' option has been used.
#
#	Top 10 function calls (not aggregated by symbol):
#
#		SELECT * FROM calls_view ORDER BY elapsed_time DESC LIMIT 10;
#
#	Top 10 function calls (aggregated by symbol):
#
#		SELECT symbol_id,(SELECT name FROM symbols WHERE id = symbol_id) AS symbol,
#			SUM(elapsed_time) AS tot_elapsed_time,SUM(branch_count) AS tot_branch_count
#			FROM calls_view GROUP BY symbol_id ORDER BY tot_elapsed_time DESC LIMIT 10;
#
#		Note that the branch count gives a rough estimation of cpu usage, so functions
#		that took a long time but have a relatively low branch count must have spent time
#		waiting.
#
#	Find symbols by pattern matching on part of the name (e.g. names containing 'alloc'):
#
#		SELECT * FROM symbols_view WHERE name LIKE '%alloc%';
#
#	Top 10 function calls for a specific symbol (e.g. whose symbol_id is 187):
#
#		SELECT * FROM calls_view WHERE symbol_id = 187 ORDER BY elapsed_time DESC LIMIT 10;
#
#	Show function calls made by function in the same context (i.e. same call path) (e.g. one with call_path_id 254):
#
#		SELECT * FROM calls_view WHERE parent_call_path_id = 254;
#
#	Show branches made during a function call (e.g. where call_id is 29357 and return_id is 29370 and tid is 29670)
#
#		SELECT * FROM samples_view WHERE id >= 29357 AND id <= 29370 AND tid = 29670 AND event LIKE 'branches%';
#
#	Show transactions:
#
#		SELECT * FROM samples_view WHERE event = 'transactions';
#
#		Note transaction start has 'in_tx' true whereas, transaction end has 'in_tx' false.
#		Transaction aborts have branch_type_name 'transaction abort'
#
#	Show transaction aborts:
#
#		SELECT * FROM samples_view WHERE event = 'transactions' AND branch_type_name = 'transaction abort';
#
# To print a call stack requires walking the call_paths table.  For example this python script:
#   #!/usr/bin/python2
#
#   import sys
#   from PySide.QtSql import *
#
#   if __name__ == '__main__':
#           if (len(sys.argv) < 3):
#                   print >> sys.stderr, "Usage is: printcallstack.py <database name> <call_path_id>"
#                   raise Exception("Too few arguments")
#           dbname = sys.argv[1]
#           call_path_id = sys.argv[2]
#           db = QSqlDatabase.addDatabase('QPSQL')
#           db.setDatabaseName(dbname)
#           if not db.open():
#                   raise Exception("Failed to open database " + dbname + " error: " + db.lastError().text())
#           query = QSqlQuery(db)
#           print "    id          ip  symbol_id  symbol                          dso_id  dso_short_name"
#           while call_path_id != 0 and call_path_id != 1:
#                   ret = query.exec_('SELECT * FROM call_paths_view WHERE id = ' + str(call_path_id))
#                   if not ret:
#                           raise Exception("Query failed: " + query.lastError().text())
#                   if not query.next():
#                           raise Exception("Query failed")
#                   print "{0:>6}  {1:>10}  {2:>9}  {3:<30}  {4:>6}  {5:<30}".format(query.value(0), query.value(1), query.value(2), query.value(3), query.value(4), query.value(5))
#                   call_path_id = query.value(6)

pyside_version_1 = True
if not "pyside-version-1" in sys.argv:
	try:
		from PySide2.QtSql import *
		pyside_version_1 = False
	except:
		pass

if pyside_version_1:
	from PySide.QtSql import *

if sys.version_info < (3, 0):
	def toserverstr(str):
		return str
	def toclientstr(str):
		return str
else:
	# Assume UTF-8 server_encoding and client_encoding
	def toserverstr(str):
		return bytes(str, "UTF_8")
	def toclientstr(str):
		return bytes(str, "UTF_8")

# Need to access PostgreSQL C library directly to use COPY FROM STDIN
from ctypes import *
libpq = CDLL("libpq.so.5")
PQconnectdb = libpq.PQconnectdb
PQconnectdb.restype = c_void_p
PQconnectdb.argtypes = [ c_char_p ]
PQfinish = libpq.PQfinish
PQfinish.argtypes = [ c_void_p ]
PQstatus = libpq.PQstatus
PQstatus.restype = c_int
PQstatus.argtypes = [ c_void_p ]
PQexec = libpq.PQexec
PQexec.restype = c_void_p
PQexec.argtypes = [ c_void_p, c_char_p ]
PQresultStatus = libpq.PQresultStatus
PQresultStatus.restype = c_int
PQresultStatus.argtypes = [ c_void_p ]
PQputCopyData = libpq.PQputCopyData
PQputCopyData.restype = c_int
PQputCopyData.argtypes = [ c_void_p, c_void_p, c_int ]
PQputCopyEnd = libpq.PQputCopyEnd
PQputCopyEnd.restype = c_int
PQputCopyEnd.argtypes = [ c_void_p, c_void_p ]

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

# These perf imports are not used at present
#from perf_trace_context import *
#from Core import *

perf_db_export_mode = True
perf_db_export_calls = False
perf_db_export_callchains = False

def printerr(*args, **kw_args):
	print(*args, file=sys.stderr, **kw_args)

def printdate(*args, **kw_args):
        print(datetime.datetime.today(), *args, sep=' ', **kw_args)

def usage():
	printerr("Usage is: export-to-postgresql.py <database name> [<columns>] [<calls>] [<callchains>] [<pyside-version-1>]");
	printerr("where:  columns            'all' or 'branches'");
	printerr("        calls              'calls' => create calls and call_paths table");
	printerr("        callchains         'callchains' => create call_paths table");
	printerr("        pyside-version-1   'pyside-version-1' => use pyside version 1");
	raise Exception("Too few or bad arguments")

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
	elif (sys.argv[i] == "pyside-version-1"):
		pass
	else:
		usage()

output_dir_name = os.getcwd() + "/" + dbname + "-perf-data"
os.mkdir(output_dir_name)

def do_query(q, s):
	if (q.exec_(s)):
		return
	raise Exception("Query failed: " + q.lastError().text())

printdate("Creating database...")

db = QSqlDatabase.addDatabase('QPSQL')
query = QSqlQuery(db)
db.setDatabaseName('postgres')
db.open()
try:
	do_query(query, 'CREATE DATABASE ' + dbname)
except:
	os.rmdir(output_dir_name)
	raise
query.finish()
query.clear()
db.close()

db.setDatabaseName(dbname)
db.open()

query = QSqlQuery(db)
do_query(query, 'SET client_min_messages TO WARNING')

do_query(query, 'CREATE TABLE selected_events ('
		'id		bigint		NOT NULL,'
		'name		varchar(80))')
do_query(query, 'CREATE TABLE machines ('
		'id		bigint		NOT NULL,'
		'pid		integer,'
		'root_dir 	varchar(4096))')
do_query(query, 'CREATE TABLE threads ('
		'id		bigint		NOT NULL,'
		'machine_id	bigint,'
		'process_id	bigint,'
		'pid		integer,'
		'tid		integer)')
do_query(query, 'CREATE TABLE comms ('
		'id		bigint		NOT NULL,'
		'comm		varchar(16))')
do_query(query, 'CREATE TABLE comm_threads ('
		'id		bigint		NOT NULL,'
		'comm_id	bigint,'
		'thread_id	bigint)')
do_query(query, 'CREATE TABLE dsos ('
		'id		bigint		NOT NULL,'
		'machine_id	bigint,'
		'short_name	varchar(256),'
		'long_name	varchar(4096),'
		'build_id	varchar(64))')
do_query(query, 'CREATE TABLE symbols ('
		'id		bigint		NOT NULL,'
		'dso_id		bigint,'
		'sym_start	bigint,'
		'sym_end	bigint,'
		'binding	integer,'
		'name		varchar(2048))')
do_query(query, 'CREATE TABLE branch_types ('
		'id		integer		NOT NULL,'
		'name		varchar(80))')

if branches:
	do_query(query, 'CREATE TABLE samples ('
		'id		bigint		NOT NULL,'
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
		'call_path_id	bigint,'
		'insn_count	bigint,'
		'cyc_count	bigint)')
else:
	do_query(query, 'CREATE TABLE samples ('
		'id		bigint		NOT NULL,'
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
		'transaction	bigint,'
		'data_src	bigint,'
		'branch_type	integer,'
		'in_tx		boolean,'
		'call_path_id	bigint,'
		'insn_count	bigint,'
		'cyc_count	bigint)')

if perf_db_export_calls or perf_db_export_callchains:
	do_query(query, 'CREATE TABLE call_paths ('
		'id		bigint		NOT NULL,'
		'parent_id	bigint,'
		'symbol_id	bigint,'
		'ip		bigint)')
if perf_db_export_calls:
	do_query(query, 'CREATE TABLE calls ('
		'id		bigint		NOT NULL,'
		'thread_id	bigint,'
		'comm_id	bigint,'
		'call_path_id	bigint,'
		'call_time	bigint,'
		'return_time	bigint,'
		'branch_count	bigint,'
		'call_id	bigint,'
		'return_id	bigint,'
		'parent_call_path_id	bigint,'
		'flags		integer,'
		'parent_id	bigint,'
		'insn_count	bigint,'
		'cyc_count	bigint)')

do_query(query, 'CREATE TABLE ptwrite ('
	'id		bigint		NOT NULL,'
	'payload	bigint,'
	'exact_ip	boolean)')

do_query(query, 'CREATE TABLE cbr ('
	'id		bigint		NOT NULL,'
	'cbr		integer,'
	'mhz		integer,'
	'percent	integer)')

do_query(query, 'CREATE TABLE mwait ('
	'id		bigint		NOT NULL,'
	'hints		integer,'
	'extensions	integer)')

do_query(query, 'CREATE TABLE pwre ('
	'id		bigint		NOT NULL,'
	'cstate		integer,'
	'subcstate	integer,'
	'hw		boolean)')

do_query(query, 'CREATE TABLE exstop ('
	'id		bigint		NOT NULL,'
	'exact_ip	boolean)')

do_query(query, 'CREATE TABLE pwrx ('
	'id		bigint		NOT NULL,'
	'deepest_cstate	integer,'
	'last_cstate	integer,'
	'wake_reason	integer)')

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
			'to_hex(c.ip) AS ip,'
			'c.symbol_id,'
			'(SELECT name FROM symbols WHERE id = c.symbol_id) AS symbol,'
			'(SELECT dso_id FROM symbols WHERE id = c.symbol_id) AS dso_id,'
			'(SELECT dso FROM symbols_view  WHERE id = c.symbol_id) AS dso_short_name,'
			'c.parent_id,'
			'to_hex(p.ip) AS parent_ip,'
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
			'to_hex(ip) AS ip,'
			'symbol_id,'
			'(SELECT name FROM symbols WHERE id = symbol_id) AS symbol,'
			'call_time,'
			'return_time,'
			'return_time - call_time AS elapsed_time,'
			'branch_count,'
			'insn_count,'
			'cyc_count,'
			'CASE WHEN cyc_count=0 THEN CAST(0 AS NUMERIC(20, 2)) ELSE CAST((CAST(insn_count AS FLOAT) / cyc_count) AS NUMERIC(20, 2)) END AS IPC,'
			'call_id,'
			'return_id,'
			'CASE WHEN flags=0 THEN \'\' WHEN flags=1 THEN \'no call\' WHEN flags=2 THEN \'no return\' WHEN flags=3 THEN \'no call/return\' WHEN flags=6 THEN \'jump\' ELSE CAST ( flags AS VARCHAR(6) ) END AS flags,'
			'parent_call_path_id,'
			'calls.parent_id'
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
		'to_hex(ip) AS ip_hex,'
		'(SELECT name FROM symbols WHERE id = symbol_id) AS symbol,'
		'sym_offset,'
		'(SELECT short_name FROM dsos WHERE id = dso_id) AS dso_short_name,'
		'to_hex(to_ip) AS to_ip_hex,'
		'(SELECT name FROM symbols WHERE id = to_symbol_id) AS to_symbol,'
		'to_sym_offset,'
		'(SELECT short_name FROM dsos WHERE id = to_dso_id) AS to_dso_short_name,'
		'(SELECT name FROM branch_types WHERE id = branch_type) AS branch_type_name,'
		'in_tx,'
		'insn_count,'
		'cyc_count,'
		'CASE WHEN cyc_count=0 THEN CAST(0 AS NUMERIC(20, 2)) ELSE CAST((CAST(insn_count AS FLOAT) / cyc_count) AS NUMERIC(20, 2)) END AS IPC'
	' FROM samples')

do_query(query, 'CREATE VIEW ptwrite_view AS '
	'SELECT '
		'ptwrite.id,'
		'time,'
		'cpu,'
		'to_hex(payload) AS payload_hex,'
		'CASE WHEN exact_ip=FALSE THEN \'False\' ELSE \'True\' END AS exact_ip'
	' FROM ptwrite'
	' INNER JOIN samples ON samples.id = ptwrite.id')

do_query(query, 'CREATE VIEW cbr_view AS '
	'SELECT '
		'cbr.id,'
		'time,'
		'cpu,'
		'cbr,'
		'mhz,'
		'percent'
	' FROM cbr'
	' INNER JOIN samples ON samples.id = cbr.id')

do_query(query, 'CREATE VIEW mwait_view AS '
	'SELECT '
		'mwait.id,'
		'time,'
		'cpu,'
		'to_hex(hints) AS hints_hex,'
		'to_hex(extensions) AS extensions_hex'
	' FROM mwait'
	' INNER JOIN samples ON samples.id = mwait.id')

do_query(query, 'CREATE VIEW pwre_view AS '
	'SELECT '
		'pwre.id,'
		'time,'
		'cpu,'
		'cstate,'
		'subcstate,'
		'CASE WHEN hw=FALSE THEN \'False\' ELSE \'True\' END AS hw'
	' FROM pwre'
	' INNER JOIN samples ON samples.id = pwre.id')

do_query(query, 'CREATE VIEW exstop_view AS '
	'SELECT '
		'exstop.id,'
		'time,'
		'cpu,'
		'CASE WHEN exact_ip=FALSE THEN \'False\' ELSE \'True\' END AS exact_ip'
	' FROM exstop'
	' INNER JOIN samples ON samples.id = exstop.id')

do_query(query, 'CREATE VIEW pwrx_view AS '
	'SELECT '
		'pwrx.id,'
		'time,'
		'cpu,'
		'deepest_cstate,'
		'last_cstate,'
		'CASE     WHEN wake_reason=1 THEN \'Interrupt\''
			' WHEN wake_reason=2 THEN \'Timer Deadline\''
			' WHEN wake_reason=4 THEN \'Monitored Address\''
			' WHEN wake_reason=8 THEN \'HW\''
			' ELSE CAST ( wake_reason AS VARCHAR(2) )'
		'END AS wake_reason'
	' FROM pwrx'
	' INNER JOIN samples ON samples.id = pwrx.id')

do_query(query, 'CREATE VIEW power_events_view AS '
	'SELECT '
		'samples.id,'
		'samples.time,'
		'samples.cpu,'
		'selected_events.name AS event,'
		'FORMAT(\'%6s\', cbr.cbr) AS cbr,'
		'FORMAT(\'%6s\', cbr.mhz) AS MHz,'
		'FORMAT(\'%5s\', cbr.percent) AS percent,'
		'to_hex(mwait.hints) AS hints_hex,'
		'to_hex(mwait.extensions) AS extensions_hex,'
		'FORMAT(\'%3s\', pwre.cstate) AS cstate,'
		'FORMAT(\'%3s\', pwre.subcstate) AS subcstate,'
		'CASE WHEN pwre.hw=FALSE THEN \'False\' WHEN pwre.hw=TRUE THEN \'True\' ELSE NULL END AS hw,'
		'CASE WHEN exstop.exact_ip=FALSE THEN \'False\' WHEN exstop.exact_ip=TRUE THEN \'True\' ELSE NULL END AS exact_ip,'
		'FORMAT(\'%3s\', pwrx.deepest_cstate) AS deepest_cstate,'
		'FORMAT(\'%3s\', pwrx.last_cstate) AS last_cstate,'
		'CASE     WHEN pwrx.wake_reason=1 THEN \'Interrupt\''
			' WHEN pwrx.wake_reason=2 THEN \'Timer Deadline\''
			' WHEN pwrx.wake_reason=4 THEN \'Monitored Address\''
			' WHEN pwrx.wake_reason=8 THEN \'HW\''
			' ELSE FORMAT(\'%2s\', pwrx.wake_reason)'
		'END AS wake_reason'
	' FROM cbr'
	' FULL JOIN mwait ON mwait.id = cbr.id'
	' FULL JOIN pwre ON pwre.id = cbr.id'
	' FULL JOIN exstop ON exstop.id = cbr.id'
	' FULL JOIN pwrx ON pwrx.id = cbr.id'
	' INNER JOIN samples ON samples.id = coalesce(cbr.id, mwait.id, pwre.id, exstop.id, pwrx.id)'
	' INNER JOIN selected_events ON selected_events.id = samples.evsel_id'
	' ORDER BY samples.id')

file_header = struct.pack("!11sii", b"PGCOPY\n\377\r\n\0", 0, 0)
file_trailer = b"\377\377"

def open_output_file(file_name):
	path_name = output_dir_name + "/" + file_name
	file = open(path_name, "wb+")
	file.write(file_header)
	return file

def close_output_file(file):
	file.write(file_trailer)
	file.close()

def copy_output_file_direct(file, table_name):
	close_output_file(file)
	sql = "COPY " + table_name + " FROM '" + file.name + "' (FORMAT 'binary')"
	do_query(query, sql)

# Use COPY FROM STDIN because security may prevent postgres from accessing the files directly
def copy_output_file(file, table_name):
	conn = PQconnectdb(toclientstr("dbname = " + dbname))
	if (PQstatus(conn)):
		raise Exception("COPY FROM STDIN PQconnectdb failed")
	file.write(file_trailer)
	file.seek(0)
	sql = "COPY " + table_name + " FROM STDIN (FORMAT 'binary')"
	res = PQexec(conn, toclientstr(sql))
	if (PQresultStatus(res) != 4):
		raise Exception("COPY FROM STDIN PQexec failed")
	data = file.read(65536)
	while (len(data)):
		ret = PQputCopyData(conn, data, len(data))
		if (ret != 1):
			raise Exception("COPY FROM STDIN PQputCopyData failed, error " + str(ret))
		data = file.read(65536)
	ret = PQputCopyEnd(conn, None)
	if (ret != 1):
		raise Exception("COPY FROM STDIN PQputCopyEnd failed, error " + str(ret))
	PQfinish(conn)

def remove_output_file(file):
	name = file.name
	file.close()
	os.unlink(name)

evsel_file		= open_output_file("evsel_table.bin")
machine_file		= open_output_file("machine_table.bin")
thread_file		= open_output_file("thread_table.bin")
comm_file		= open_output_file("comm_table.bin")
comm_thread_file	= open_output_file("comm_thread_table.bin")
dso_file		= open_output_file("dso_table.bin")
symbol_file		= open_output_file("symbol_table.bin")
branch_type_file	= open_output_file("branch_type_table.bin")
sample_file		= open_output_file("sample_table.bin")
if perf_db_export_calls or perf_db_export_callchains:
	call_path_file		= open_output_file("call_path_table.bin")
if perf_db_export_calls:
	call_file		= open_output_file("call_table.bin")
ptwrite_file		= open_output_file("ptwrite_table.bin")
cbr_file		= open_output_file("cbr_table.bin")
mwait_file		= open_output_file("mwait_table.bin")
pwre_file		= open_output_file("pwre_table.bin")
exstop_file		= open_output_file("exstop_table.bin")
pwrx_file		= open_output_file("pwrx_table.bin")

def trace_begin():
	printdate("Writing to intermediate files...")
	# id == 0 means unknown.  It is easier to create records for them than replace the zeroes with NULLs
	evsel_table(0, "unknown")
	machine_table(0, 0, "unknown")
	thread_table(0, 0, 0, -1, -1)
	comm_table(0, "unknown")
	dso_table(0, 0, "unknown", "unknown", "")
	symbol_table(0, 0, 0, 0, 0, "unknown")
	sample_table(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
	if perf_db_export_calls or perf_db_export_callchains:
		call_path_table(0, 0, 0, 0)
		call_return_table(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

unhandled_count = 0

def is_table_empty(table_name):
	do_query(query, 'SELECT * FROM ' + table_name + ' LIMIT 1');
	if query.next():
		return False
	return True

def drop(table_name):
	do_query(query, 'DROP VIEW ' + table_name + '_view');
	do_query(query, 'DROP TABLE ' + table_name);

def trace_end():
	printdate("Copying to database...")
	copy_output_file(evsel_file,		"selected_events")
	copy_output_file(machine_file,		"machines")
	copy_output_file(thread_file,		"threads")
	copy_output_file(comm_file,		"comms")
	copy_output_file(comm_thread_file,	"comm_threads")
	copy_output_file(dso_file,		"dsos")
	copy_output_file(symbol_file,		"symbols")
	copy_output_file(branch_type_file,	"branch_types")
	copy_output_file(sample_file,		"samples")
	if perf_db_export_calls or perf_db_export_callchains:
		copy_output_file(call_path_file,	"call_paths")
	if perf_db_export_calls:
		copy_output_file(call_file,		"calls")
	copy_output_file(ptwrite_file,		"ptwrite")
	copy_output_file(cbr_file,		"cbr")
	copy_output_file(mwait_file,		"mwait")
	copy_output_file(pwre_file,		"pwre")
	copy_output_file(exstop_file,		"exstop")
	copy_output_file(pwrx_file,		"pwrx")

	printdate("Removing intermediate files...")
	remove_output_file(evsel_file)
	remove_output_file(machine_file)
	remove_output_file(thread_file)
	remove_output_file(comm_file)
	remove_output_file(comm_thread_file)
	remove_output_file(dso_file)
	remove_output_file(symbol_file)
	remove_output_file(branch_type_file)
	remove_output_file(sample_file)
	if perf_db_export_calls or perf_db_export_callchains:
		remove_output_file(call_path_file)
	if perf_db_export_calls:
		remove_output_file(call_file)
	remove_output_file(ptwrite_file)
	remove_output_file(cbr_file)
	remove_output_file(mwait_file)
	remove_output_file(pwre_file)
	remove_output_file(exstop_file)
	remove_output_file(pwrx_file)
	os.rmdir(output_dir_name)
	printdate("Adding primary keys")
	do_query(query, 'ALTER TABLE selected_events ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE machines        ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE threads         ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE comms           ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE comm_threads    ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE dsos            ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE symbols         ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE branch_types    ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE samples         ADD PRIMARY KEY (id)')
	if perf_db_export_calls or perf_db_export_callchains:
		do_query(query, 'ALTER TABLE call_paths      ADD PRIMARY KEY (id)')
	if perf_db_export_calls:
		do_query(query, 'ALTER TABLE calls           ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE ptwrite         ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE cbr             ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE mwait           ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE pwre            ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE exstop          ADD PRIMARY KEY (id)')
	do_query(query, 'ALTER TABLE pwrx            ADD PRIMARY KEY (id)')

	printdate("Adding foreign keys")
	do_query(query, 'ALTER TABLE threads '
					'ADD CONSTRAINT machinefk  FOREIGN KEY (machine_id)   REFERENCES machines   (id),'
					'ADD CONSTRAINT processfk  FOREIGN KEY (process_id)   REFERENCES threads    (id)')
	do_query(query, 'ALTER TABLE comm_threads '
					'ADD CONSTRAINT commfk     FOREIGN KEY (comm_id)      REFERENCES comms      (id),'
					'ADD CONSTRAINT threadfk   FOREIGN KEY (thread_id)    REFERENCES threads    (id)')
	do_query(query, 'ALTER TABLE dsos '
					'ADD CONSTRAINT machinefk  FOREIGN KEY (machine_id)   REFERENCES machines   (id)')
	do_query(query, 'ALTER TABLE symbols '
					'ADD CONSTRAINT dsofk      FOREIGN KEY (dso_id)       REFERENCES dsos       (id)')
	do_query(query, 'ALTER TABLE samples '
					'ADD CONSTRAINT evselfk    FOREIGN KEY (evsel_id)     REFERENCES selected_events (id),'
					'ADD CONSTRAINT machinefk  FOREIGN KEY (machine_id)   REFERENCES machines   (id),'
					'ADD CONSTRAINT threadfk   FOREIGN KEY (thread_id)    REFERENCES threads    (id),'
					'ADD CONSTRAINT commfk     FOREIGN KEY (comm_id)      REFERENCES comms      (id),'
					'ADD CONSTRAINT dsofk      FOREIGN KEY (dso_id)       REFERENCES dsos       (id),'
					'ADD CONSTRAINT symbolfk   FOREIGN KEY (symbol_id)    REFERENCES symbols    (id),'
					'ADD CONSTRAINT todsofk    FOREIGN KEY (to_dso_id)    REFERENCES dsos       (id),'
					'ADD CONSTRAINT tosymbolfk FOREIGN KEY (to_symbol_id) REFERENCES symbols    (id)')
	if perf_db_export_calls or perf_db_export_callchains:
		do_query(query, 'ALTER TABLE call_paths '
					'ADD CONSTRAINT parentfk    FOREIGN KEY (parent_id)    REFERENCES call_paths (id),'
					'ADD CONSTRAINT symbolfk    FOREIGN KEY (symbol_id)    REFERENCES symbols    (id)')
	if perf_db_export_calls:
		do_query(query, 'ALTER TABLE calls '
					'ADD CONSTRAINT threadfk    FOREIGN KEY (thread_id)    REFERENCES threads    (id),'
					'ADD CONSTRAINT commfk      FOREIGN KEY (comm_id)      REFERENCES comms      (id),'
					'ADD CONSTRAINT call_pathfk FOREIGN KEY (call_path_id) REFERENCES call_paths (id),'
					'ADD CONSTRAINT callfk      FOREIGN KEY (call_id)      REFERENCES samples    (id),'
					'ADD CONSTRAINT returnfk    FOREIGN KEY (return_id)    REFERENCES samples    (id),'
					'ADD CONSTRAINT parent_call_pathfk FOREIGN KEY (parent_call_path_id) REFERENCES call_paths (id)')
		do_query(query, 'CREATE INDEX pcpid_idx ON calls (parent_call_path_id)')
		do_query(query, 'CREATE INDEX pid_idx ON calls (parent_id)')
	do_query(query, 'ALTER TABLE ptwrite '
					'ADD CONSTRAINT idfk        FOREIGN KEY (id)           REFERENCES samples   (id)')
	do_query(query, 'ALTER TABLE  cbr '
					'ADD CONSTRAINT idfk        FOREIGN KEY (id)           REFERENCES samples   (id)')
	do_query(query, 'ALTER TABLE  mwait '
					'ADD CONSTRAINT idfk        FOREIGN KEY (id)           REFERENCES samples   (id)')
	do_query(query, 'ALTER TABLE  pwre '
					'ADD CONSTRAINT idfk        FOREIGN KEY (id)           REFERENCES samples   (id)')
	do_query(query, 'ALTER TABLE  exstop '
					'ADD CONSTRAINT idfk        FOREIGN KEY (id)           REFERENCES samples   (id)')
	do_query(query, 'ALTER TABLE  pwrx '
					'ADD CONSTRAINT idfk        FOREIGN KEY (id)           REFERENCES samples   (id)')

	printdate("Dropping unused tables")
	if is_table_empty("ptwrite"):
		drop("ptwrite")
	if is_table_empty("mwait") and is_table_empty("pwre") and is_table_empty("exstop") and is_table_empty("pwrx"):
		do_query(query, 'DROP VIEW power_events_view');
		drop("mwait")
		drop("pwre")
		drop("exstop")
		drop("pwrx")
		if is_table_empty("cbr"):
			drop("cbr")

	if (unhandled_count):
		printdate("Warning: ", unhandled_count, " unhandled events")
	printdate("Done")

def trace_unhandled(event_name, context, event_fields_dict):
	global unhandled_count
	unhandled_count += 1

def sched__sched_switch(*x):
	pass

def evsel_table(evsel_id, evsel_name, *x):
	evsel_name = toserverstr(evsel_name)
	n = len(evsel_name)
	fmt = "!hiqi" + str(n) + "s"
	value = struct.pack(fmt, 2, 8, evsel_id, n, evsel_name)
	evsel_file.write(value)

def machine_table(machine_id, pid, root_dir, *x):
	root_dir = toserverstr(root_dir)
	n = len(root_dir)
	fmt = "!hiqiii" + str(n) + "s"
	value = struct.pack(fmt, 3, 8, machine_id, 4, pid, n, root_dir)
	machine_file.write(value)

def thread_table(thread_id, machine_id, process_id, pid, tid, *x):
	value = struct.pack("!hiqiqiqiiii", 5, 8, thread_id, 8, machine_id, 8, process_id, 4, pid, 4, tid)
	thread_file.write(value)

def comm_table(comm_id, comm_str, *x):
	comm_str = toserverstr(comm_str)
	n = len(comm_str)
	fmt = "!hiqi" + str(n) + "s"
	value = struct.pack(fmt, 2, 8, comm_id, n, comm_str)
	comm_file.write(value)

def comm_thread_table(comm_thread_id, comm_id, thread_id, *x):
	fmt = "!hiqiqiq"
	value = struct.pack(fmt, 3, 8, comm_thread_id, 8, comm_id, 8, thread_id)
	comm_thread_file.write(value)

def dso_table(dso_id, machine_id, short_name, long_name, build_id, *x):
	short_name = toserverstr(short_name)
	long_name = toserverstr(long_name)
	build_id = toserverstr(build_id)
	n1 = len(short_name)
	n2 = len(long_name)
	n3 = len(build_id)
	fmt = "!hiqiqi" + str(n1) + "si"  + str(n2) + "si" + str(n3) + "s"
	value = struct.pack(fmt, 5, 8, dso_id, 8, machine_id, n1, short_name, n2, long_name, n3, build_id)
	dso_file.write(value)

def symbol_table(symbol_id, dso_id, sym_start, sym_end, binding, symbol_name, *x):
	symbol_name = toserverstr(symbol_name)
	n = len(symbol_name)
	fmt = "!hiqiqiqiqiii" + str(n) + "s"
	value = struct.pack(fmt, 6, 8, symbol_id, 8, dso_id, 8, sym_start, 8, sym_end, 4, binding, n, symbol_name)
	symbol_file.write(value)

def branch_type_table(branch_type, name, *x):
	name = toserverstr(name)
	n = len(name)
	fmt = "!hiii" + str(n) + "s"
	value = struct.pack(fmt, 2, 4, branch_type, n, name)
	branch_type_file.write(value)

def sample_table(sample_id, evsel_id, machine_id, thread_id, comm_id, dso_id, symbol_id, sym_offset, ip, time, cpu, to_dso_id, to_symbol_id, to_sym_offset, to_ip, period, weight, transaction, data_src, branch_type, in_tx, call_path_id, insn_cnt, cyc_cnt, *x):
	if branches:
		value = struct.pack("!hiqiqiqiqiqiqiqiqiqiqiiiqiqiqiqiiiBiqiqiq", 20, 8, sample_id, 8, evsel_id, 8, machine_id, 8, thread_id, 8, comm_id, 8, dso_id, 8, symbol_id, 8, sym_offset, 8, ip, 8, time, 4, cpu, 8, to_dso_id, 8, to_symbol_id, 8, to_sym_offset, 8, to_ip, 4, branch_type, 1, in_tx, 8, call_path_id, 8, insn_cnt, 8, cyc_cnt)
	else:
		value = struct.pack("!hiqiqiqiqiqiqiqiqiqiqiiiqiqiqiqiqiqiqiqiiiBiqiqiq", 24, 8, sample_id, 8, evsel_id, 8, machine_id, 8, thread_id, 8, comm_id, 8, dso_id, 8, symbol_id, 8, sym_offset, 8, ip, 8, time, 4, cpu, 8, to_dso_id, 8, to_symbol_id, 8, to_sym_offset, 8, to_ip, 8, period, 8, weight, 8, transaction, 8, data_src, 4, branch_type, 1, in_tx, 8, call_path_id, 8, insn_cnt, 8, cyc_cnt)
	sample_file.write(value)

def call_path_table(cp_id, parent_id, symbol_id, ip, *x):
	fmt = "!hiqiqiqiq"
	value = struct.pack(fmt, 4, 8, cp_id, 8, parent_id, 8, symbol_id, 8, ip)
	call_path_file.write(value)

def call_return_table(cr_id, thread_id, comm_id, call_path_id, call_time, return_time, branch_count, call_id, return_id, parent_call_path_id, flags, parent_id, insn_cnt, cyc_cnt, *x):
	fmt = "!hiqiqiqiqiqiqiqiqiqiqiiiqiqiq"
	value = struct.pack(fmt, 14, 8, cr_id, 8, thread_id, 8, comm_id, 8, call_path_id, 8, call_time, 8, return_time, 8, branch_count, 8, call_id, 8, return_id, 8, parent_call_path_id, 4, flags, 8, parent_id, 8, insn_cnt, 8, cyc_cnt)
	call_file.write(value)

def ptwrite(id, raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	flags = data[0]
	payload = data[1]
	exact_ip = flags & 1
	value = struct.pack("!hiqiqiB", 3, 8, id, 8, payload, 1, exact_ip)
	ptwrite_file.write(value)

def cbr(id, raw_buf):
	data = struct.unpack_from("<BBBBII", raw_buf)
	cbr = data[0]
	MHz = (data[4] + 500) / 1000
	percent = ((cbr * 1000 / data[2]) + 5) / 10
	value = struct.pack("!hiqiiiiii", 4, 8, id, 4, cbr, 4, MHz, 4, percent)
	cbr_file.write(value)

def mwait(id, raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	payload = data[1]
	hints = payload & 0xff
	extensions = (payload >> 32) & 0x3
	value = struct.pack("!hiqiiii", 3, 8, id, 4, hints, 4, extensions)
	mwait_file.write(value)

def pwre(id, raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	payload = data[1]
	hw = (payload >> 7) & 1
	cstate = (payload >> 12) & 0xf
	subcstate = (payload >> 8) & 0xf
	value = struct.pack("!hiqiiiiiB", 4, 8, id, 4, cstate, 4, subcstate, 1, hw)
	pwre_file.write(value)

def exstop(id, raw_buf):
	data = struct.unpack_from("<I", raw_buf)
	flags = data[0]
	exact_ip = flags & 1
	value = struct.pack("!hiqiB", 2, 8, id, 1, exact_ip)
	exstop_file.write(value)

def pwrx(id, raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	payload = data[1]
	deepest_cstate = payload & 0xf
	last_cstate = (payload >> 4) & 0xf
	wake_reason = (payload >> 8) & 0xf
	value = struct.pack("!hiqiiiiii", 4, 8, id, 4, deepest_cstate, 4, last_cstate, 4, wake_reason)
	pwrx_file.write(value)

def synth_data(id, config, raw_buf, *x):
	if config == 0:
		ptwrite(id, raw_buf)
	elif config == 1:
		mwait(id, raw_buf)
	elif config == 2:
		pwre(id, raw_buf)
	elif config == 3:
		exstop(id, raw_buf)
	elif config == 4:
		pwrx(id, raw_buf)
	elif config == 5:
		cbr(id, raw_buf)
