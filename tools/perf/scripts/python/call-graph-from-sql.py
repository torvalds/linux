#!/usr/bin/python2
# call-graph-from-sql.py: create call-graph from sql database
# Copyright (c) 2014-2017, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.

# To use this script you will need to have exported data using either the
# export-to-sqlite.py or the export-to-postgresql.py script.  Refer to those
# scripts for details.
#
# Following on from the example in the export scripts, a
# call-graph can be displayed for the pt_example database like this:
#
#	python tools/perf/scripts/python/call-graph-from-sql.py pt_example
#
# Note that for PostgreSQL, this script supports connecting to remote databases
# by setting hostname, port, username, password, and dbname e.g.
#
#	python tools/perf/scripts/python/call-graph-from-sql.py "hostname=myhost username=myuser password=mypassword dbname=pt_example"
#
# The result is a GUI window with a tree representing a context-sensitive
# call-graph.  Expanding a couple of levels of the tree and adjusting column
# widths to suit will display something like:
#
#                                         Call Graph: pt_example
# Call Path                          Object      Count   Time(ns)  Time(%)  Branch Count   Branch Count(%)
# v- ls
#     v- 2638:2638
#         v- _start                  ld-2.19.so    1     10074071   100.0         211135            100.0
#           |- unknown               unknown       1        13198     0.1              1              0.0
#           >- _dl_start             ld-2.19.so    1      1400980    13.9          19637              9.3
#           >- _d_linit_internal     ld-2.19.so    1       448152     4.4          11094              5.3
#           v-__libc_start_main@plt  ls            1      8211741    81.5         180397             85.4
#              >- _dl_fixup          ld-2.19.so    1         7607     0.1            108              0.1
#              >- __cxa_atexit       libc-2.19.so  1        11737     0.1             10              0.0
#              >- __libc_csu_init    ls            1        10354     0.1             10              0.0
#              |- _setjmp            libc-2.19.so  1            0     0.0              4              0.0
#              v- main               ls            1      8182043    99.6         180254             99.9
#
# Points to note:
#	The top level is a command name (comm)
#	The next level is a thread (pid:tid)
#	Subsequent levels are functions
#	'Count' is the number of calls
#	'Time' is the elapsed time until the function returns
#	Percentages are relative to the level above
#	'Branch Count' is the total number of branches for that function and all
#       functions that it calls

import sys
from PySide.QtCore import *
from PySide.QtGui import *
from PySide.QtSql import *
from decimal import *

class TreeItem():

	def __init__(self, db, row, parent_item):
		self.db = db
		self.row = row
		self.parent_item = parent_item
		self.query_done = False;
		self.child_count = 0
		self.child_items = []
		self.data = ["", "", "", "", "", "", ""]
		self.comm_id = 0
		self.thread_id = 0
		self.call_path_id = 1
		self.branch_count = 0
		self.time = 0
		if not parent_item:
			self.setUpRoot()

	def setUpRoot(self):
		self.query_done = True
		query = QSqlQuery(self.db)
		ret = query.exec_('SELECT id, comm FROM comms')
		if not ret:
			raise Exception("Query failed: " + query.lastError().text())
		while query.next():
			if not query.value(0):
				continue
			child_item = TreeItem(self.db, self.child_count, self)
			self.child_items.append(child_item)
			self.child_count += 1
			child_item.setUpLevel1(query.value(0), query.value(1))

	def setUpLevel1(self, comm_id, comm):
		self.query_done = True;
		self.comm_id = comm_id
		self.data[0] = comm
		self.child_items = []
		self.child_count = 0
		query = QSqlQuery(self.db)
		ret = query.exec_('SELECT thread_id, ( SELECT pid FROM threads WHERE id = thread_id ), ( SELECT tid FROM threads WHERE id = thread_id ) FROM comm_threads WHERE comm_id = ' + str(comm_id))
		if not ret:
			raise Exception("Query failed: " + query.lastError().text())
		while query.next():
			child_item = TreeItem(self.db, self.child_count, self)
			self.child_items.append(child_item)
			self.child_count += 1
			child_item.setUpLevel2(comm_id, query.value(0), query.value(1), query.value(2))

	def setUpLevel2(self, comm_id, thread_id, pid, tid):
		self.comm_id = comm_id
		self.thread_id = thread_id
		self.data[0] = str(pid) + ":" + str(tid)

	def getChildItem(self, row):
		return self.child_items[row]

	def getParentItem(self):
		return self.parent_item

	def getRow(self):
		return self.row

	def timePercent(self, b):
		if not self.time:
			return "0.0"
		x = (b * Decimal(100)) / self.time
		return str(x.quantize(Decimal('.1'), rounding=ROUND_HALF_UP))

	def branchPercent(self, b):
		if not self.branch_count:
			return "0.0"
		x = (b * Decimal(100)) / self.branch_count
		return str(x.quantize(Decimal('.1'), rounding=ROUND_HALF_UP))

	def addChild(self, call_path_id, name, dso, count, time, branch_count):
		child_item = TreeItem(self.db, self.child_count, self)
		child_item.comm_id = self.comm_id
		child_item.thread_id = self.thread_id
		child_item.call_path_id = call_path_id
		child_item.branch_count = branch_count
		child_item.time = time
		child_item.data[0] = name
		if dso == "[kernel.kallsyms]":
			dso = "[kernel]"
		child_item.data[1] = dso
		child_item.data[2] = str(count)
		child_item.data[3] = str(time)
		child_item.data[4] = self.timePercent(time)
		child_item.data[5] = str(branch_count)
		child_item.data[6] = self.branchPercent(branch_count)
		self.child_items.append(child_item)
		self.child_count += 1

	def selectCalls(self):
		self.query_done = True;
		query = QSqlQuery(self.db)
		ret = query.exec_('SELECT id, call_path_id, branch_count, call_time, return_time, '
				  '( SELECT name FROM symbols WHERE id = ( SELECT symbol_id FROM call_paths WHERE id = call_path_id ) ), '
				  '( SELECT short_name FROM dsos WHERE id = ( SELECT dso_id FROM symbols WHERE id = ( SELECT symbol_id FROM call_paths WHERE id = call_path_id ) ) ), '
				  '( SELECT ip FROM call_paths where id = call_path_id ) '
				  'FROM calls WHERE parent_call_path_id = ' + str(self.call_path_id) + ' AND comm_id = ' + str(self.comm_id) + ' AND thread_id = ' + str(self.thread_id) +
				  ' ORDER BY call_path_id')
		if not ret:
			raise Exception("Query failed: " + query.lastError().text())
		last_call_path_id = 0
		name = ""
		dso = ""
		count = 0
		branch_count = 0
		total_branch_count = 0
		time = 0
		total_time = 0
		while query.next():
			if query.value(1) == last_call_path_id:
				count += 1
				branch_count += query.value(2)
				time += query.value(4) - query.value(3)
			else:
				if count:
					self.addChild(last_call_path_id, name, dso, count, time, branch_count)
				last_call_path_id = query.value(1)
				name = query.value(5)
				dso = query.value(6)
				count = 1
				total_branch_count += branch_count
				total_time += time
				branch_count = query.value(2)
				time = query.value(4) - query.value(3)
		if count:
			self.addChild(last_call_path_id, name, dso, count, time, branch_count)
		total_branch_count += branch_count
		total_time += time
		# Top level does not have time or branch count, so fix that here
		if total_branch_count > self.branch_count:
			self.branch_count = total_branch_count
			if self.branch_count:
				for child_item in self.child_items:
					child_item.data[6] = self.branchPercent(child_item.branch_count)
		if total_time > self.time:
			self.time = total_time
			if self.time:
				for child_item in self.child_items:
					child_item.data[4] = self.timePercent(child_item.time)

	def childCount(self):
		if not self.query_done:
			self.selectCalls()
		return self.child_count

	def columnCount(self):
		return 7

	def columnHeader(self, column):
		headers = ["Call Path", "Object", "Count ", "Time (ns) ", "Time (%) ", "Branch Count ", "Branch Count (%) "]
		return headers[column]

	def getData(self, column):
		return self.data[column]

class TreeModel(QAbstractItemModel):

	def __init__(self, db, parent=None):
		super(TreeModel, self).__init__(parent)
		self.db = db
		self.root = TreeItem(db, 0, None)

	def columnCount(self, parent):
		return self.root.columnCount()

	def rowCount(self, parent):
		if parent.isValid():
			parent_item = parent.internalPointer()
		else:
			parent_item = self.root
		return parent_item.childCount()

	def headerData(self, section, orientation, role):
		if role == Qt.TextAlignmentRole:
			if section > 1:
				return Qt.AlignRight
		if role != Qt.DisplayRole:
			return None
		if orientation != Qt.Horizontal:
			return None
		return self.root.columnHeader(section)

	def parent(self, child):
		child_item = child.internalPointer()
		if child_item is self.root:
			return QModelIndex()
		parent_item = child_item.getParentItem()
		return self.createIndex(parent_item.getRow(), 0, parent_item)

	def index(self, row, column, parent):
		if parent.isValid():
			parent_item = parent.internalPointer()
		else:
			parent_item = self.root
		child_item = parent_item.getChildItem(row)
		return self.createIndex(row, column, child_item)

	def data(self, index, role):
		if role == Qt.TextAlignmentRole:
			if index.column() > 1:
				return Qt.AlignRight
		if role != Qt.DisplayRole:
			return None
		index_item = index.internalPointer()
		return index_item.getData(index.column())

class MainWindow(QMainWindow):

	def __init__(self, db, dbname, parent=None):
		super(MainWindow, self).__init__(parent)

		self.setObjectName("MainWindow")
		self.setWindowTitle("Call Graph: " + dbname)
		self.move(100, 100)
		self.resize(800, 600)
		style = self.style()
		icon = style.standardIcon(QStyle.SP_MessageBoxInformation)
		self.setWindowIcon(icon);

		self.model = TreeModel(db)

		self.view = QTreeView()
		self.view.setModel(self.model)

		self.setCentralWidget(self.view)

if __name__ == '__main__':
	if (len(sys.argv) < 2):
		print >> sys.stderr, "Usage is: call-graph-from-sql.py <database name>"
		raise Exception("Too few arguments")

	dbname = sys.argv[1]

	is_sqlite3 = False
	try:
		f = open(dbname)
		if f.read(15) == "SQLite format 3":
			is_sqlite3 = True
		f.close()
	except:
		pass

	if is_sqlite3:
		db = QSqlDatabase.addDatabase('QSQLITE')
	else:
		db = QSqlDatabase.addDatabase('QPSQL')
		opts = dbname.split()
		for opt in opts:
			if '=' in opt:
				opt = opt.split('=')
				if opt[0] == 'hostname':
					db.setHostName(opt[1])
				elif opt[0] == 'port':
					db.setPort(int(opt[1]))
				elif opt[0] == 'username':
					db.setUserName(opt[1])
				elif opt[0] == 'password':
					db.setPassword(opt[1])
				elif opt[0] == 'dbname':
					dbname = opt[1]
			else:
				dbname = opt

	db.setDatabaseName(dbname)
	if not db.open():
		raise Exception("Failed to open database " + dbname + " error: " + db.lastError().text())

	app = QApplication(sys.argv)
	window = MainWindow(db, dbname)
	window.show()
	err = app.exec_()
	db.close()
	sys.exit(err)
