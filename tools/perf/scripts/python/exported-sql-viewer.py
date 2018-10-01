#!/usr/bin/python2
# SPDX-License-Identifier: GPL-2.0
# exported-sql-viewer.py: view data from sql database
# Copyright (c) 2014-2018, Intel Corporation.

# To use this script you will need to have exported data using either the
# export-to-sqlite.py or the export-to-postgresql.py script.  Refer to those
# scripts for details.
#
# Following on from the example in the export scripts, a
# call-graph can be displayed for the pt_example database like this:
#
#	python tools/perf/scripts/python/exported-sql-viewer.py pt_example
#
# Note that for PostgreSQL, this script supports connecting to remote databases
# by setting hostname, port, username, password, and dbname e.g.
#
#	python tools/perf/scripts/python/exported-sql-viewer.py "hostname=myhost username=myuser password=mypassword dbname=pt_example"
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
import weakref
import threading
import string
import cPickle
import re
import os
from PySide.QtCore import *
from PySide.QtGui import *
from PySide.QtSql import *
from decimal import *
from ctypes import *
from multiprocessing import Process, Array, Value, Event

# Data formatting helpers

def dsoname(name):
	if name == "[kernel.kallsyms]":
		return "[kernel]"
	return name

# Percent to one decimal place

def PercentToOneDP(n, d):
	if not d:
		return "0.0"
	x = (n * Decimal(100)) / d
	return str(x.quantize(Decimal(".1"), rounding=ROUND_HALF_UP))

# Helper for queries that must not fail

def QueryExec(query, stmt):
	ret = query.exec_(stmt)
	if not ret:
		raise Exception("Query failed: " + query.lastError().text())

# Background thread

class Thread(QThread):

	done = Signal(object)

	def __init__(self, task, param=None, parent=None):
		super(Thread, self).__init__(parent)
		self.task = task
		self.param = param

	def run(self):
		while True:
			if self.param is None:
				done, result = self.task()
			else:
				done, result = self.task(self.param)
			self.done.emit(result)
			if done:
				break

# Tree data model

class TreeModel(QAbstractItemModel):

	def __init__(self, root, parent=None):
		super(TreeModel, self).__init__(parent)
		self.root = root
		self.last_row_read = 0

	def Item(self, parent):
		if parent.isValid():
			return parent.internalPointer()
		else:
			return self.root

	def rowCount(self, parent):
		result = self.Item(parent).childCount()
		if result < 0:
			result = 0
			self.dataChanged.emit(parent, parent)
		return result

	def hasChildren(self, parent):
		return self.Item(parent).hasChildren()

	def headerData(self, section, orientation, role):
		if role == Qt.TextAlignmentRole:
			return self.columnAlignment(section)
		if role != Qt.DisplayRole:
			return None
		if orientation != Qt.Horizontal:
			return None
		return self.columnHeader(section)

	def parent(self, child):
		child_item = child.internalPointer()
		if child_item is self.root:
			return QModelIndex()
		parent_item = child_item.getParentItem()
		return self.createIndex(parent_item.getRow(), 0, parent_item)

	def index(self, row, column, parent):
		child_item = self.Item(parent).getChildItem(row)
		return self.createIndex(row, column, child_item)

	def DisplayData(self, item, index):
		return item.getData(index.column())

	def FetchIfNeeded(self, row):
		if row > self.last_row_read:
			self.last_row_read = row
			if row + 10 >= self.root.child_count:
				self.fetcher.Fetch(glb_chunk_sz)

	def columnAlignment(self, column):
		return Qt.AlignLeft

	def columnFont(self, column):
		return None

	def data(self, index, role):
		if role == Qt.TextAlignmentRole:
			return self.columnAlignment(index.column())
		if role == Qt.FontRole:
			return self.columnFont(index.column())
		if role != Qt.DisplayRole:
			return None
		item = index.internalPointer()
		return self.DisplayData(item, index)

# Table data model

class TableModel(QAbstractTableModel):

	def __init__(self, parent=None):
		super(TableModel, self).__init__(parent)
		self.child_count = 0
		self.child_items = []
		self.last_row_read = 0

	def Item(self, parent):
		if parent.isValid():
			return parent.internalPointer()
		else:
			return self

	def rowCount(self, parent):
		return self.child_count

	def headerData(self, section, orientation, role):
		if role == Qt.TextAlignmentRole:
			return self.columnAlignment(section)
		if role != Qt.DisplayRole:
			return None
		if orientation != Qt.Horizontal:
			return None
		return self.columnHeader(section)

	def index(self, row, column, parent):
		return self.createIndex(row, column, self.child_items[row])

	def DisplayData(self, item, index):
		return item.getData(index.column())

	def FetchIfNeeded(self, row):
		if row > self.last_row_read:
			self.last_row_read = row
			if row + 10 >= self.child_count:
				self.fetcher.Fetch(glb_chunk_sz)

	def columnAlignment(self, column):
		return Qt.AlignLeft

	def columnFont(self, column):
		return None

	def data(self, index, role):
		if role == Qt.TextAlignmentRole:
			return self.columnAlignment(index.column())
		if role == Qt.FontRole:
			return self.columnFont(index.column())
		if role != Qt.DisplayRole:
			return None
		item = index.internalPointer()
		return self.DisplayData(item, index)

# Model cache

model_cache = weakref.WeakValueDictionary()
model_cache_lock = threading.Lock()

def LookupCreateModel(model_name, create_fn):
	model_cache_lock.acquire()
	try:
		model = model_cache[model_name]
	except:
		model = None
	if model is None:
		model = create_fn()
		model_cache[model_name] = model
	model_cache_lock.release()
	return model

# Find bar

class FindBar():

	def __init__(self, parent, finder, is_reg_expr=False):
		self.finder = finder
		self.context = []
		self.last_value = None
		self.last_pattern = None

		label = QLabel("Find:")
		label.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

		self.textbox = QComboBox()
		self.textbox.setEditable(True)
		self.textbox.currentIndexChanged.connect(self.ValueChanged)

		self.progress = QProgressBar()
		self.progress.setRange(0, 0)
		self.progress.hide()

		if is_reg_expr:
			self.pattern = QCheckBox("Regular Expression")
		else:
			self.pattern = QCheckBox("Pattern")
		self.pattern.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

		self.next_button = QToolButton()
		self.next_button.setIcon(parent.style().standardIcon(QStyle.SP_ArrowDown))
		self.next_button.released.connect(lambda: self.NextPrev(1))

		self.prev_button = QToolButton()
		self.prev_button.setIcon(parent.style().standardIcon(QStyle.SP_ArrowUp))
		self.prev_button.released.connect(lambda: self.NextPrev(-1))

		self.close_button = QToolButton()
		self.close_button.setIcon(parent.style().standardIcon(QStyle.SP_DockWidgetCloseButton))
		self.close_button.released.connect(self.Deactivate)

		self.hbox = QHBoxLayout()
		self.hbox.setContentsMargins(0, 0, 0, 0)

		self.hbox.addWidget(label)
		self.hbox.addWidget(self.textbox)
		self.hbox.addWidget(self.progress)
		self.hbox.addWidget(self.pattern)
		self.hbox.addWidget(self.next_button)
		self.hbox.addWidget(self.prev_button)
		self.hbox.addWidget(self.close_button)

		self.bar = QWidget()
		self.bar.setLayout(self.hbox);
		self.bar.hide()

	def Widget(self):
		return self.bar

	def Activate(self):
		self.bar.show()
		self.textbox.setFocus()

	def Deactivate(self):
		self.bar.hide()

	def Busy(self):
		self.textbox.setEnabled(False)
		self.pattern.hide()
		self.next_button.hide()
		self.prev_button.hide()
		self.progress.show()

	def Idle(self):
		self.textbox.setEnabled(True)
		self.progress.hide()
		self.pattern.show()
		self.next_button.show()
		self.prev_button.show()

	def Find(self, direction):
		value = self.textbox.currentText()
		pattern = self.pattern.isChecked()
		self.last_value = value
		self.last_pattern = pattern
		self.finder.Find(value, direction, pattern, self.context)

	def ValueChanged(self):
		value = self.textbox.currentText()
		pattern = self.pattern.isChecked()
		index = self.textbox.currentIndex()
		data = self.textbox.itemData(index)
		# Store the pattern in the combo box to keep it with the text value
		if data == None:
			self.textbox.setItemData(index, pattern)
		else:
			self.pattern.setChecked(data)
		self.Find(0)

	def NextPrev(self, direction):
		value = self.textbox.currentText()
		pattern = self.pattern.isChecked()
		if value != self.last_value:
			index = self.textbox.findText(value)
			# Allow for a button press before the value has been added to the combo box
			if index < 0:
				index = self.textbox.count()
				self.textbox.addItem(value, pattern)
				self.textbox.setCurrentIndex(index)
				return
			else:
				self.textbox.setItemData(index, pattern)
		elif pattern != self.last_pattern:
			# Keep the pattern recorded in the combo box up to date
			index = self.textbox.currentIndex()
			self.textbox.setItemData(index, pattern)
		self.Find(direction)

	def NotFound(self):
		QMessageBox.information(self.bar, "Find", "'" + self.textbox.currentText() + "' not found")

# Context-sensitive call graph data model item base

class CallGraphLevelItemBase(object):

	def __init__(self, glb, row, parent_item):
		self.glb = glb
		self.row = row
		self.parent_item = parent_item
		self.query_done = False;
		self.child_count = 0
		self.child_items = []

	def getChildItem(self, row):
		return self.child_items[row]

	def getParentItem(self):
		return self.parent_item

	def getRow(self):
		return self.row

	def childCount(self):
		if not self.query_done:
			self.Select()
			if not self.child_count:
				return -1
		return self.child_count

	def hasChildren(self):
		if not self.query_done:
			return True
		return self.child_count > 0

	def getData(self, column):
		return self.data[column]

# Context-sensitive call graph data model level 2+ item base

class CallGraphLevelTwoPlusItemBase(CallGraphLevelItemBase):

	def __init__(self, glb, row, comm_id, thread_id, call_path_id, time, branch_count, parent_item):
		super(CallGraphLevelTwoPlusItemBase, self).__init__(glb, row, parent_item)
		self.comm_id = comm_id
		self.thread_id = thread_id
		self.call_path_id = call_path_id
		self.branch_count = branch_count
		self.time = time

	def Select(self):
		self.query_done = True;
		query = QSqlQuery(self.glb.db)
		QueryExec(query, "SELECT call_path_id, name, short_name, COUNT(calls.id), SUM(return_time - call_time), SUM(branch_count)"
					" FROM calls"
					" INNER JOIN call_paths ON calls.call_path_id = call_paths.id"
					" INNER JOIN symbols ON call_paths.symbol_id = symbols.id"
					" INNER JOIN dsos ON symbols.dso_id = dsos.id"
					" WHERE parent_call_path_id = " + str(self.call_path_id) +
					" AND comm_id = " + str(self.comm_id) +
					" AND thread_id = " + str(self.thread_id) +
					" GROUP BY call_path_id, name, short_name"
					" ORDER BY call_path_id")
		while query.next():
			child_item = CallGraphLevelThreeItem(self.glb, self.child_count, self.comm_id, self.thread_id, query.value(0), query.value(1), query.value(2), query.value(3), int(query.value(4)), int(query.value(5)), self)
			self.child_items.append(child_item)
			self.child_count += 1

# Context-sensitive call graph data model level three item

class CallGraphLevelThreeItem(CallGraphLevelTwoPlusItemBase):

	def __init__(self, glb, row, comm_id, thread_id, call_path_id, name, dso, count, time, branch_count, parent_item):
		super(CallGraphLevelThreeItem, self).__init__(glb, row, comm_id, thread_id, call_path_id, time, branch_count, parent_item)
		dso = dsoname(dso)
		self.data = [ name, dso, str(count), str(time), PercentToOneDP(time, parent_item.time), str(branch_count), PercentToOneDP(branch_count, parent_item.branch_count) ]
		self.dbid = call_path_id

# Context-sensitive call graph data model level two item

class CallGraphLevelTwoItem(CallGraphLevelTwoPlusItemBase):

	def __init__(self, glb, row, comm_id, thread_id, pid, tid, parent_item):
		super(CallGraphLevelTwoItem, self).__init__(glb, row, comm_id, thread_id, 1, 0, 0, parent_item)
		self.data = [str(pid) + ":" + str(tid), "", "", "", "", "", ""]
		self.dbid = thread_id

	def Select(self):
		super(CallGraphLevelTwoItem, self).Select()
		for child_item in self.child_items:
			self.time += child_item.time
			self.branch_count += child_item.branch_count
		for child_item in self.child_items:
			child_item.data[4] = PercentToOneDP(child_item.time, self.time)
			child_item.data[6] = PercentToOneDP(child_item.branch_count, self.branch_count)

# Context-sensitive call graph data model level one item

class CallGraphLevelOneItem(CallGraphLevelItemBase):

	def __init__(self, glb, row, comm_id, comm, parent_item):
		super(CallGraphLevelOneItem, self).__init__(glb, row, parent_item)
		self.data = [comm, "", "", "", "", "", ""]
		self.dbid = comm_id

	def Select(self):
		self.query_done = True;
		query = QSqlQuery(self.glb.db)
		QueryExec(query, "SELECT thread_id, pid, tid"
					" FROM comm_threads"
					" INNER JOIN threads ON thread_id = threads.id"
					" WHERE comm_id = " + str(self.dbid))
		while query.next():
			child_item = CallGraphLevelTwoItem(self.glb, self.child_count, self.dbid, query.value(0), query.value(1), query.value(2), self)
			self.child_items.append(child_item)
			self.child_count += 1

# Context-sensitive call graph data model root item

class CallGraphRootItem(CallGraphLevelItemBase):

	def __init__(self, glb):
		super(CallGraphRootItem, self).__init__(glb, 0, None)
		self.dbid = 0
		self.query_done = True;
		query = QSqlQuery(glb.db)
		QueryExec(query, "SELECT id, comm FROM comms")
		while query.next():
			if not query.value(0):
				continue
			child_item = CallGraphLevelOneItem(glb, self.child_count, query.value(0), query.value(1), self)
			self.child_items.append(child_item)
			self.child_count += 1

# Context-sensitive call graph data model

class CallGraphModel(TreeModel):

	def __init__(self, glb, parent=None):
		super(CallGraphModel, self).__init__(CallGraphRootItem(glb), parent)
		self.glb = glb

	def columnCount(self, parent=None):
		return 7

	def columnHeader(self, column):
		headers = ["Call Path", "Object", "Count ", "Time (ns) ", "Time (%) ", "Branch Count ", "Branch Count (%) "]
		return headers[column]

	def columnAlignment(self, column):
		alignment = [ Qt.AlignLeft, Qt.AlignLeft, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight ]
		return alignment[column]

	def FindSelect(self, value, pattern, query):
		if pattern:
			# postgresql and sqlite pattern patching differences:
			#   postgresql LIKE is case sensitive but sqlite LIKE is not
			#   postgresql LIKE allows % and _ to be escaped with \ but sqlite LIKE does not
			#   postgresql supports ILIKE which is case insensitive
			#   sqlite supports GLOB (text only) which uses * and ? and is case sensitive
			if not self.glb.dbref.is_sqlite3:
				# Escape % and _
				s = value.replace("%", "\%")
				s = s.replace("_", "\_")
				# Translate * and ? into SQL LIKE pattern characters % and _
				trans = string.maketrans("*?", "%_")
				match = " LIKE '" + str(s).translate(trans) + "'"
			else:
				match = " GLOB '" + str(value) + "'"
		else:
			match = " = '" + str(value) + "'"
		QueryExec(query, "SELECT call_path_id, comm_id, thread_id"
						" FROM calls"
						" INNER JOIN call_paths ON calls.call_path_id = call_paths.id"
						" INNER JOIN symbols ON call_paths.symbol_id = symbols.id"
						" WHERE symbols.name" + match +
						" GROUP BY comm_id, thread_id, call_path_id"
						" ORDER BY comm_id, thread_id, call_path_id")

	def FindPath(self, query):
		# Turn the query result into a list of ids that the tree view can walk
		# to open the tree at the right place.
		ids = []
		parent_id = query.value(0)
		while parent_id:
			ids.insert(0, parent_id)
			q2 = QSqlQuery(self.glb.db)
			QueryExec(q2, "SELECT parent_id"
					" FROM call_paths"
					" WHERE id = " + str(parent_id))
			if not q2.next():
				break
			parent_id = q2.value(0)
		# The call path root is not used
		if ids[0] == 1:
			del ids[0]
		ids.insert(0, query.value(2))
		ids.insert(0, query.value(1))
		return ids

	def Found(self, query, found):
		if found:
			return self.FindPath(query)
		return []

	def FindValue(self, value, pattern, query, last_value, last_pattern):
		if last_value == value and pattern == last_pattern:
			found = query.first()
		else:
			self.FindSelect(value, pattern, query)
			found = query.next()
		return self.Found(query, found)

	def FindNext(self, query):
		found = query.next()
		if not found:
			found = query.first()
		return self.Found(query, found)

	def FindPrev(self, query):
		found = query.previous()
		if not found:
			found = query.last()
		return self.Found(query, found)

	def FindThread(self, c):
		if c.direction == 0 or c.value != c.last_value or c.pattern != c.last_pattern:
			ids = self.FindValue(c.value, c.pattern, c.query, c.last_value, c.last_pattern)
		elif c.direction > 0:
			ids = self.FindNext(c.query)
		else:
			ids = self.FindPrev(c.query)
		return (True, ids)

	def Find(self, value, direction, pattern, context, callback):
		class Context():
			def __init__(self, *x):
				self.value, self.direction, self.pattern, self.query, self.last_value, self.last_pattern = x
			def Update(self, *x):
				self.value, self.direction, self.pattern, self.last_value, self.last_pattern = x + (self.value, self.pattern)
		if len(context):
			context[0].Update(value, direction, pattern)
		else:
			context.append(Context(value, direction, pattern, QSqlQuery(self.glb.db), None, None))
		# Use a thread so the UI is not blocked during the SELECT
		thread = Thread(self.FindThread, context[0])
		thread.done.connect(lambda ids, t=thread, c=callback: self.FindDone(t, c, ids), Qt.QueuedConnection)
		thread.start()

	def FindDone(self, thread, callback, ids):
		callback(ids)

# Vertical widget layout

class VBox():

	def __init__(self, w1, w2, w3=None):
		self.vbox = QWidget()
		self.vbox.setLayout(QVBoxLayout());

		self.vbox.layout().setContentsMargins(0, 0, 0, 0)

		self.vbox.layout().addWidget(w1)
		self.vbox.layout().addWidget(w2)
		if w3:
			self.vbox.layout().addWidget(w3)

	def Widget(self):
		return self.vbox

# Context-sensitive call graph window

class CallGraphWindow(QMdiSubWindow):

	def __init__(self, glb, parent=None):
		super(CallGraphWindow, self).__init__(parent)

		self.model = LookupCreateModel("Context-Sensitive Call Graph", lambda x=glb: CallGraphModel(x))

		self.view = QTreeView()
		self.view.setModel(self.model)

		for c, w in ((0, 250), (1, 100), (2, 60), (3, 70), (4, 70), (5, 100)):
			self.view.setColumnWidth(c, w)

		self.find_bar = FindBar(self, self)

		self.vbox = VBox(self.view, self.find_bar.Widget())

		self.setWidget(self.vbox.Widget())

		AddSubWindow(glb.mainwindow.mdi_area, self, "Context-Sensitive Call Graph")

	def DisplayFound(self, ids):
		if not len(ids):
			return False
		parent = QModelIndex()
		for dbid in ids:
			found = False
			n = self.model.rowCount(parent)
			for row in xrange(n):
				child = self.model.index(row, 0, parent)
				if child.internalPointer().dbid == dbid:
					found = True
					self.view.setCurrentIndex(child)
					parent = child
					break
			if not found:
				break
		return found

	def Find(self, value, direction, pattern, context):
		self.view.setFocus()
		self.find_bar.Busy()
		self.model.Find(value, direction, pattern, context, self.FindDone)

	def FindDone(self, ids):
		found = True
		if not self.DisplayFound(ids):
			found = False
		self.find_bar.Idle()
		if not found:
			self.find_bar.NotFound()

# Child data item  finder

class ChildDataItemFinder():

	def __init__(self, root):
		self.root = root
		self.value, self.direction, self.pattern, self.last_value, self.last_pattern = (None,) * 5
		self.rows = []
		self.pos = 0

	def FindSelect(self):
		self.rows = []
		if self.pattern:
			pattern = re.compile(self.value)
			for child in self.root.child_items:
				for column_data in child.data:
					if re.search(pattern, str(column_data)) is not None:
						self.rows.append(child.row)
						break
		else:
			for child in self.root.child_items:
				for column_data in child.data:
					if self.value in str(column_data):
						self.rows.append(child.row)
						break

	def FindValue(self):
		self.pos = 0
		if self.last_value != self.value or self.pattern != self.last_pattern:
			self.FindSelect()
		if not len(self.rows):
			return -1
		return self.rows[self.pos]

	def FindThread(self):
		if self.direction == 0 or self.value != self.last_value or self.pattern != self.last_pattern:
			row = self.FindValue()
		elif len(self.rows):
			if self.direction > 0:
				self.pos += 1
				if self.pos >= len(self.rows):
					self.pos = 0
			else:
				self.pos -= 1
				if self.pos < 0:
					self.pos = len(self.rows) - 1
			row = self.rows[self.pos]
		else:
			row = -1
		return (True, row)

	def Find(self, value, direction, pattern, context, callback):
		self.value, self.direction, self.pattern, self.last_value, self.last_pattern = (value, direction,pattern, self.value, self.pattern)
		# Use a thread so the UI is not blocked
		thread = Thread(self.FindThread)
		thread.done.connect(lambda row, t=thread, c=callback: self.FindDone(t, c, row), Qt.QueuedConnection)
		thread.start()

	def FindDone(self, thread, callback, row):
		callback(row)

# Number of database records to fetch in one go

glb_chunk_sz = 10000

# size of pickled integer big enough for record size

glb_nsz = 8

# Background process for SQL data fetcher

class SQLFetcherProcess():

	def __init__(self, dbref, sql, buffer, head, tail, fetch_count, fetching_done, process_target, wait_event, fetched_event, prep):
		# Need a unique connection name
		conn_name = "SQLFetcher" + str(os.getpid())
		self.db, dbname = dbref.Open(conn_name)
		self.sql = sql
		self.buffer = buffer
		self.head = head
		self.tail = tail
		self.fetch_count = fetch_count
		self.fetching_done = fetching_done
		self.process_target = process_target
		self.wait_event = wait_event
		self.fetched_event = fetched_event
		self.prep = prep
		self.query = QSqlQuery(self.db)
		self.query_limit = 0 if "$$last_id$$" in sql else 2
		self.last_id = -1
		self.fetched = 0
		self.more = True
		self.local_head = self.head.value
		self.local_tail = self.tail.value

	def Select(self):
		if self.query_limit:
			if self.query_limit == 1:
				return
			self.query_limit -= 1
		stmt = self.sql.replace("$$last_id$$", str(self.last_id))
		QueryExec(self.query, stmt)

	def Next(self):
		if not self.query.next():
			self.Select()
			if not self.query.next():
				return None
		self.last_id = self.query.value(0)
		return self.prep(self.query)

	def WaitForTarget(self):
		while True:
			self.wait_event.clear()
			target = self.process_target.value
			if target > self.fetched or target < 0:
				break
			self.wait_event.wait()
		return target

	def HasSpace(self, sz):
		if self.local_tail <= self.local_head:
			space = len(self.buffer) - self.local_head
			if space > sz:
				return True
			if space >= glb_nsz:
				# Use 0 (or space < glb_nsz) to mean there is no more at the top of the buffer
				nd = cPickle.dumps(0, cPickle.HIGHEST_PROTOCOL)
				self.buffer[self.local_head : self.local_head + len(nd)] = nd
			self.local_head = 0
		if self.local_tail - self.local_head > sz:
			return True
		return False

	def WaitForSpace(self, sz):
		if self.HasSpace(sz):
			return
		while True:
			self.wait_event.clear()
			self.local_tail = self.tail.value
			if self.HasSpace(sz):
				return
			self.wait_event.wait()

	def AddToBuffer(self, obj):
		d = cPickle.dumps(obj, cPickle.HIGHEST_PROTOCOL)
		n = len(d)
		nd = cPickle.dumps(n, cPickle.HIGHEST_PROTOCOL)
		sz = n + glb_nsz
		self.WaitForSpace(sz)
		pos = self.local_head
		self.buffer[pos : pos + len(nd)] = nd
		self.buffer[pos + glb_nsz : pos + sz] = d
		self.local_head += sz

	def FetchBatch(self, batch_size):
		fetched = 0
		while batch_size > fetched:
			obj = self.Next()
			if obj is None:
				self.more = False
				break
			self.AddToBuffer(obj)
			fetched += 1
		if fetched:
			self.fetched += fetched
			with self.fetch_count.get_lock():
				self.fetch_count.value += fetched
			self.head.value = self.local_head
			self.fetched_event.set()

	def Run(self):
		while self.more:
			target = self.WaitForTarget()
			if target < 0:
				break
			batch_size = min(glb_chunk_sz, target - self.fetched)
			self.FetchBatch(batch_size)
		self.fetching_done.value = True
		self.fetched_event.set()

def SQLFetcherFn(*x):
	process = SQLFetcherProcess(*x)
	process.Run()

# SQL data fetcher

class SQLFetcher(QObject):

	done = Signal(object)

	def __init__(self, glb, sql, prep, process_data, parent=None):
		super(SQLFetcher, self).__init__(parent)
		self.process_data = process_data
		self.more = True
		self.target = 0
		self.last_target = 0
		self.fetched = 0
		self.buffer_size = 16 * 1024 * 1024
		self.buffer = Array(c_char, self.buffer_size, lock=False)
		self.head = Value(c_longlong)
		self.tail = Value(c_longlong)
		self.local_tail = 0
		self.fetch_count = Value(c_longlong)
		self.fetching_done = Value(c_bool)
		self.last_count = 0
		self.process_target = Value(c_longlong)
		self.wait_event = Event()
		self.fetched_event = Event()
		glb.AddInstanceToShutdownOnExit(self)
		self.process = Process(target=SQLFetcherFn, args=(glb.dbref, sql, self.buffer, self.head, self.tail, self.fetch_count, self.fetching_done, self.process_target, self.wait_event, self.fetched_event, prep))
		self.process.start()
		self.thread = Thread(self.Thread)
		self.thread.done.connect(self.ProcessData, Qt.QueuedConnection)
		self.thread.start()

	def Shutdown(self):
		# Tell the thread and process to exit
		self.process_target.value = -1
		self.wait_event.set()
		self.more = False
		self.fetching_done.value = True
		self.fetched_event.set()

	def Thread(self):
		if not self.more:
			return True, 0
		while True:
			self.fetched_event.clear()
			fetch_count = self.fetch_count.value
			if fetch_count != self.last_count:
				break
			if self.fetching_done.value:
				self.more = False
				return True, 0
			self.fetched_event.wait()
		count = fetch_count - self.last_count
		self.last_count = fetch_count
		self.fetched += count
		return False, count

	def Fetch(self, nr):
		if not self.more:
			# -1 inidcates there are no more
			return -1
		result = self.fetched
		extra = result + nr - self.target
		if extra > 0:
			self.target += extra
			# process_target < 0 indicates shutting down
			if self.process_target.value >= 0:
				self.process_target.value = self.target
			self.wait_event.set()
		return result

	def RemoveFromBuffer(self):
		pos = self.local_tail
		if len(self.buffer) - pos < glb_nsz:
			pos = 0
		n = cPickle.loads(self.buffer[pos : pos + glb_nsz])
		if n == 0:
			pos = 0
			n = cPickle.loads(self.buffer[0 : glb_nsz])
		pos += glb_nsz
		obj = cPickle.loads(self.buffer[pos : pos + n])
		self.local_tail = pos + n
		return obj

	def ProcessData(self, count):
		for i in xrange(count):
			obj = self.RemoveFromBuffer()
			self.process_data(obj)
		self.tail.value = self.local_tail
		self.wait_event.set()
		self.done.emit(count)

# Fetch more records bar

class FetchMoreRecordsBar():

	def __init__(self, model, parent):
		self.model = model

		self.label = QLabel("Number of records (x " + "{:,}".format(glb_chunk_sz) + ") to fetch:")
		self.label.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

		self.fetch_count = QSpinBox()
		self.fetch_count.setRange(1, 1000000)
		self.fetch_count.setValue(10)
		self.fetch_count.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

		self.fetch = QPushButton("Go!")
		self.fetch.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
		self.fetch.released.connect(self.FetchMoreRecords)

		self.progress = QProgressBar()
		self.progress.setRange(0, 100)
		self.progress.hide()

		self.done_label = QLabel("All records fetched")
		self.done_label.hide()

		self.spacer = QLabel("")

		self.close_button = QToolButton()
		self.close_button.setIcon(parent.style().standardIcon(QStyle.SP_DockWidgetCloseButton))
		self.close_button.released.connect(self.Deactivate)

		self.hbox = QHBoxLayout()
		self.hbox.setContentsMargins(0, 0, 0, 0)

		self.hbox.addWidget(self.label)
		self.hbox.addWidget(self.fetch_count)
		self.hbox.addWidget(self.fetch)
		self.hbox.addWidget(self.spacer)
		self.hbox.addWidget(self.progress)
		self.hbox.addWidget(self.done_label)
		self.hbox.addWidget(self.close_button)

		self.bar = QWidget()
		self.bar.setLayout(self.hbox);
		self.bar.show()

		self.in_progress = False
		self.model.progress.connect(self.Progress)

		self.done = False

		if not model.HasMoreRecords():
			self.Done()

	def Widget(self):
		return self.bar

	def Activate(self):
		self.bar.show()
		self.fetch.setFocus()

	def Deactivate(self):
		self.bar.hide()

	def Enable(self, enable):
		self.fetch.setEnabled(enable)
		self.fetch_count.setEnabled(enable)

	def Busy(self):
		self.Enable(False)
		self.fetch.hide()
		self.spacer.hide()
		self.progress.show()

	def Idle(self):
		self.in_progress = False
		self.Enable(True)
		self.progress.hide()
		self.fetch.show()
		self.spacer.show()

	def Target(self):
		return self.fetch_count.value() * glb_chunk_sz

	def Done(self):
		self.done = True
		self.Idle()
		self.label.hide()
		self.fetch_count.hide()
		self.fetch.hide()
		self.spacer.hide()
		self.done_label.show()

	def Progress(self, count):
		if self.in_progress:
			if count:
				percent = ((count - self.start) * 100) / self.Target()
				if percent >= 100:
					self.Idle()
				else:
					self.progress.setValue(percent)
		if not count:
			# Count value of zero means no more records
			self.Done()

	def FetchMoreRecords(self):
		if self.done:
			return
		self.progress.setValue(0)
		self.Busy()
		self.in_progress = True
		self.start = self.model.FetchMoreRecords(self.Target())

# SQL data preparation

def SQLTableDataPrep(query, count):
	data = []
	for i in xrange(count):
		data.append(query.value(i))
	return data

# SQL table data model item

class SQLTableItem():

	def __init__(self, row, data):
		self.row = row
		self.data = data

	def getData(self, column):
		return self.data[column]

# SQL table data model

class SQLTableModel(TableModel):

	progress = Signal(object)

	def __init__(self, glb, sql, column_count, parent=None):
		super(SQLTableModel, self).__init__(parent)
		self.glb = glb
		self.more = True
		self.populated = 0
		self.fetcher = SQLFetcher(glb, sql, lambda x, y=column_count: SQLTableDataPrep(x, y), self.AddSample)
		self.fetcher.done.connect(self.Update)
		self.fetcher.Fetch(glb_chunk_sz)

	def DisplayData(self, item, index):
		self.FetchIfNeeded(item.row)
		return item.getData(index.column())

	def AddSample(self, data):
		child = SQLTableItem(self.populated, data)
		self.child_items.append(child)
		self.populated += 1

	def Update(self, fetched):
		if not fetched:
			self.more = False
			self.progress.emit(0)
		child_count = self.child_count
		count = self.populated - child_count
		if count > 0:
			parent = QModelIndex()
			self.beginInsertRows(parent, child_count, child_count + count - 1)
			self.insertRows(child_count, count, parent)
			self.child_count += count
			self.endInsertRows()
			self.progress.emit(self.child_count)

	def FetchMoreRecords(self, count):
		current = self.child_count
		if self.more:
			self.fetcher.Fetch(count)
		else:
			self.progress.emit(0)
		return current

	def HasMoreRecords(self):
		return self.more

# SQL automatic table data model

class SQLAutoTableModel(SQLTableModel):

	def __init__(self, glb, table_name, parent=None):
		sql = "SELECT * FROM " + table_name + " WHERE id > $$last_id$$ ORDER BY id LIMIT " + str(glb_chunk_sz)
		if table_name == "comm_threads_view":
			# For now, comm_threads_view has no id column
			sql = "SELECT * FROM " + table_name + " WHERE comm_id > $$last_id$$ ORDER BY comm_id LIMIT " + str(glb_chunk_sz)
		self.column_headers = []
		query = QSqlQuery(glb.db)
		if glb.dbref.is_sqlite3:
			QueryExec(query, "PRAGMA table_info(" + table_name + ")")
			while query.next():
				self.column_headers.append(query.value(1))
			if table_name == "sqlite_master":
				sql = "SELECT * FROM " + table_name
		else:
			if table_name[:19] == "information_schema.":
				sql = "SELECT * FROM " + table_name
				select_table_name = table_name[19:]
				schema = "information_schema"
			else:
				select_table_name = table_name
				schema = "public"
			QueryExec(query, "SELECT column_name FROM information_schema.columns WHERE table_schema = '" + schema + "' and table_name = '" + select_table_name + "'")
			while query.next():
				self.column_headers.append(query.value(0))
		super(SQLAutoTableModel, self).__init__(glb, sql, len(self.column_headers), parent)

	def columnCount(self, parent=None):
		return len(self.column_headers)

	def columnHeader(self, column):
		return self.column_headers[column]

# Base class for custom ResizeColumnsToContents

class ResizeColumnsToContentsBase(QObject):

	def __init__(self, parent=None):
		super(ResizeColumnsToContentsBase, self).__init__(parent)

	def ResizeColumnToContents(self, column, n):
		# Using the view's resizeColumnToContents() here is extrememly slow
		# so implement a crude alternative
		font = self.view.font()
		metrics = QFontMetrics(font)
		max = 0
		for row in xrange(n):
			val = self.data_model.child_items[row].data[column]
			len = metrics.width(str(val) + "MM")
			max = len if len > max else max
		val = self.data_model.columnHeader(column)
		len = metrics.width(str(val) + "MM")
		max = len if len > max else max
		self.view.setColumnWidth(column, max)

	def ResizeColumnsToContents(self):
		n = min(self.data_model.child_count, 100)
		if n < 1:
			# No data yet, so connect a signal to notify when there is
			self.data_model.rowsInserted.connect(self.UpdateColumnWidths)
			return
		columns = self.data_model.columnCount()
		for i in xrange(columns):
			self.ResizeColumnToContents(i, n)

	def UpdateColumnWidths(self, *x):
		# This only needs to be done once, so disconnect the signal now
		self.data_model.rowsInserted.disconnect(self.UpdateColumnWidths)
		self.ResizeColumnsToContents()

# Table window

class TableWindow(QMdiSubWindow, ResizeColumnsToContentsBase):

	def __init__(self, glb, table_name, parent=None):
		super(TableWindow, self).__init__(parent)

		self.data_model = LookupCreateModel(table_name + " Table", lambda: SQLAutoTableModel(glb, table_name))

		self.model = QSortFilterProxyModel()
		self.model.setSourceModel(self.data_model)

		self.view = QTableView()
		self.view.setModel(self.model)
		self.view.setEditTriggers(QAbstractItemView.NoEditTriggers)
		self.view.verticalHeader().setVisible(False)
		self.view.sortByColumn(-1, Qt.AscendingOrder)
		self.view.setSortingEnabled(True)

		self.ResizeColumnsToContents()

		self.find_bar = FindBar(self, self, True)

		self.finder = ChildDataItemFinder(self.data_model)

		self.fetch_bar = FetchMoreRecordsBar(self.data_model, self)

		self.vbox = VBox(self.view, self.find_bar.Widget(), self.fetch_bar.Widget())

		self.setWidget(self.vbox.Widget())

		AddSubWindow(glb.mainwindow.mdi_area, self, table_name + " Table")

	def Find(self, value, direction, pattern, context):
		self.view.setFocus()
		self.find_bar.Busy()
		self.finder.Find(value, direction, pattern, context, self.FindDone)

	def FindDone(self, row):
		self.find_bar.Idle()
		if row >= 0:
			self.view.setCurrentIndex(self.model.index(row, 0, QModelIndex()))
		else:
			self.find_bar.NotFound()

# Table list

def GetTableList(glb):
	tables = []
	query = QSqlQuery(glb.db)
	if glb.dbref.is_sqlite3:
		QueryExec(query, "SELECT name FROM sqlite_master WHERE type IN ( 'table' , 'view' ) ORDER BY name")
	else:
		QueryExec(query, "SELECT table_name FROM information_schema.tables WHERE table_schema = 'public' AND table_type IN ( 'BASE TABLE' , 'VIEW' ) ORDER BY table_name")
	while query.next():
		tables.append(query.value(0))
	if glb.dbref.is_sqlite3:
		tables.append("sqlite_master")
	else:
		tables.append("information_schema.tables")
		tables.append("information_schema.views")
		tables.append("information_schema.columns")
	return tables

# Action Definition

def CreateAction(label, tip, callback, parent=None, shortcut=None):
	action = QAction(label, parent)
	if shortcut != None:
		action.setShortcuts(shortcut)
	action.setStatusTip(tip)
	action.triggered.connect(callback)
	return action

# Typical application actions

def CreateExitAction(app, parent=None):
	return CreateAction("&Quit", "Exit the application", app.closeAllWindows, parent, QKeySequence.Quit)

# Typical MDI actions

def CreateCloseActiveWindowAction(mdi_area):
	return CreateAction("Cl&ose", "Close the active window", mdi_area.closeActiveSubWindow, mdi_area)

def CreateCloseAllWindowsAction(mdi_area):
	return CreateAction("Close &All", "Close all the windows", mdi_area.closeAllSubWindows, mdi_area)

def CreateTileWindowsAction(mdi_area):
	return CreateAction("&Tile", "Tile the windows", mdi_area.tileSubWindows, mdi_area)

def CreateCascadeWindowsAction(mdi_area):
	return CreateAction("&Cascade", "Cascade the windows", mdi_area.cascadeSubWindows, mdi_area)

def CreateNextWindowAction(mdi_area):
	return CreateAction("Ne&xt", "Move the focus to the next window", mdi_area.activateNextSubWindow, mdi_area, QKeySequence.NextChild)

def CreatePreviousWindowAction(mdi_area):
	return CreateAction("Pre&vious", "Move the focus to the previous window", mdi_area.activatePreviousSubWindow, mdi_area, QKeySequence.PreviousChild)

# Typical MDI window menu

class WindowMenu():

	def __init__(self, mdi_area, menu):
		self.mdi_area = mdi_area
		self.window_menu = menu.addMenu("&Windows")
		self.close_active_window = CreateCloseActiveWindowAction(mdi_area)
		self.close_all_windows = CreateCloseAllWindowsAction(mdi_area)
		self.tile_windows = CreateTileWindowsAction(mdi_area)
		self.cascade_windows = CreateCascadeWindowsAction(mdi_area)
		self.next_window = CreateNextWindowAction(mdi_area)
		self.previous_window = CreatePreviousWindowAction(mdi_area)
		self.window_menu.aboutToShow.connect(self.Update)

	def Update(self):
		self.window_menu.clear()
		sub_window_count = len(self.mdi_area.subWindowList())
		have_sub_windows = sub_window_count != 0
		self.close_active_window.setEnabled(have_sub_windows)
		self.close_all_windows.setEnabled(have_sub_windows)
		self.tile_windows.setEnabled(have_sub_windows)
		self.cascade_windows.setEnabled(have_sub_windows)
		self.next_window.setEnabled(have_sub_windows)
		self.previous_window.setEnabled(have_sub_windows)
		self.window_menu.addAction(self.close_active_window)
		self.window_menu.addAction(self.close_all_windows)
		self.window_menu.addSeparator()
		self.window_menu.addAction(self.tile_windows)
		self.window_menu.addAction(self.cascade_windows)
		self.window_menu.addSeparator()
		self.window_menu.addAction(self.next_window)
		self.window_menu.addAction(self.previous_window)
		if sub_window_count == 0:
			return
		self.window_menu.addSeparator()
		nr = 1
		for sub_window in self.mdi_area.subWindowList():
			label = str(nr) + " " + sub_window.name
			if nr < 10:
				label = "&" + label
			action = self.window_menu.addAction(label)
			action.setCheckable(True)
			action.setChecked(sub_window == self.mdi_area.activeSubWindow())
			action.triggered.connect(lambda x=nr: self.setActiveSubWindow(x))
			self.window_menu.addAction(action)
			nr += 1

	def setActiveSubWindow(self, nr):
		self.mdi_area.setActiveSubWindow(self.mdi_area.subWindowList()[nr - 1])

# Font resize

def ResizeFont(widget, diff):
	font = widget.font()
	sz = font.pointSize()
	font.setPointSize(sz + diff)
	widget.setFont(font)

def ShrinkFont(widget):
	ResizeFont(widget, -1)

def EnlargeFont(widget):
	ResizeFont(widget, 1)

# Unique name for sub-windows

def NumberedWindowName(name, nr):
	if nr > 1:
		name += " <" + str(nr) + ">"
	return name

def UniqueSubWindowName(mdi_area, name):
	nr = 1
	while True:
		unique_name = NumberedWindowName(name, nr)
		ok = True
		for sub_window in mdi_area.subWindowList():
			if sub_window.name == unique_name:
				ok = False
				break
		if ok:
			return unique_name
		nr += 1

# Add a sub-window

def AddSubWindow(mdi_area, sub_window, name):
	unique_name = UniqueSubWindowName(mdi_area, name)
	sub_window.setMinimumSize(200, 100)
	sub_window.resize(800, 600)
	sub_window.setWindowTitle(unique_name)
	sub_window.setAttribute(Qt.WA_DeleteOnClose)
	sub_window.setWindowIcon(sub_window.style().standardIcon(QStyle.SP_FileIcon))
	sub_window.name = unique_name
	mdi_area.addSubWindow(sub_window)
	sub_window.show()

# Main window

class MainWindow(QMainWindow):

	def __init__(self, glb, parent=None):
		super(MainWindow, self).__init__(parent)

		self.glb = glb

		self.setWindowTitle("Exported SQL Viewer: " + glb.dbname)
		self.setWindowIcon(self.style().standardIcon(QStyle.SP_ComputerIcon))
		self.setMinimumSize(200, 100)

		self.mdi_area = QMdiArea()
		self.mdi_area.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
		self.mdi_area.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)

		self.setCentralWidget(self.mdi_area)

		menu = self.menuBar()

		file_menu = menu.addMenu("&File")
		file_menu.addAction(CreateExitAction(glb.app, self))

		edit_menu = menu.addMenu("&Edit")
		edit_menu.addAction(CreateAction("&Find...", "Find items", self.Find, self, QKeySequence.Find))
		edit_menu.addAction(CreateAction("Fetch &more records...", "Fetch more records", self.FetchMoreRecords, self, [QKeySequence(Qt.Key_F8)]))
		edit_menu.addAction(CreateAction("&Shrink Font", "Make text smaller", self.ShrinkFont, self, [QKeySequence("Ctrl+-")]))
		edit_menu.addAction(CreateAction("&Enlarge Font", "Make text bigger", self.EnlargeFont, self, [QKeySequence("Ctrl++")]))

		reports_menu = menu.addMenu("&Reports")
		reports_menu.addAction(CreateAction("Context-Sensitive Call &Graph", "Create a new window containing a context-sensitive call graph", self.NewCallGraph, self))

		self.TableMenu(GetTableList(glb), menu)

		self.window_menu = WindowMenu(self.mdi_area, menu)

	def Find(self):
		win = self.mdi_area.activeSubWindow()
		if win:
			try:
				win.find_bar.Activate()
			except:
				pass

	def FetchMoreRecords(self):
		win = self.mdi_area.activeSubWindow()
		if win:
			try:
				win.fetch_bar.Activate()
			except:
				pass

	def ShrinkFont(self):
		win = self.mdi_area.activeSubWindow()
		ShrinkFont(win.view)

	def EnlargeFont(self):
		win = self.mdi_area.activeSubWindow()
		EnlargeFont(win.view)

	def TableMenu(self, tables, menu):
		table_menu = menu.addMenu("&Tables")
		for table in tables:
			table_menu.addAction(CreateAction(table, "Create a new window containing a table view", lambda t=table: self.NewTableView(t), self))

	def NewCallGraph(self):
		CallGraphWindow(self.glb, self)

	def NewTableView(self, table_name):
		TableWindow(self.glb, table_name, self)

# Global data

class Glb():

	def __init__(self, dbref, db, dbname):
		self.dbref = dbref
		self.db = db
		self.dbname = dbname
		self.app = None
		self.mainwindow = None
		self.instances_to_shutdown_on_exit = weakref.WeakSet()

	def AddInstanceToShutdownOnExit(self, instance):
		self.instances_to_shutdown_on_exit.add(instance)

	# Shutdown any background processes or threads
	def ShutdownInstances(self):
		for x in self.instances_to_shutdown_on_exit:
			try:
				x.Shutdown()
			except:
				pass

# Database reference

class DBRef():

	def __init__(self, is_sqlite3, dbname):
		self.is_sqlite3 = is_sqlite3
		self.dbname = dbname

	def Open(self, connection_name):
		dbname = self.dbname
		if self.is_sqlite3:
			db = QSqlDatabase.addDatabase("QSQLITE", connection_name)
		else:
			db = QSqlDatabase.addDatabase("QPSQL", connection_name)
			opts = dbname.split()
			for opt in opts:
				if "=" in opt:
					opt = opt.split("=")
					if opt[0] == "hostname":
						db.setHostName(opt[1])
					elif opt[0] == "port":
						db.setPort(int(opt[1]))
					elif opt[0] == "username":
						db.setUserName(opt[1])
					elif opt[0] == "password":
						db.setPassword(opt[1])
					elif opt[0] == "dbname":
						dbname = opt[1]
				else:
					dbname = opt

		db.setDatabaseName(dbname)
		if not db.open():
			raise Exception("Failed to open database " + dbname + " error: " + db.lastError().text())
		return db, dbname

# Main

def Main():
	if (len(sys.argv) < 2):
		print >> sys.stderr, "Usage is: exported-sql-viewer.py <database name>"
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

	dbref = DBRef(is_sqlite3, dbname)
	db, dbname = dbref.Open("main")
	glb = Glb(dbref, db, dbname)
	app = QApplication(sys.argv)
	glb.app = app
	mainwindow = MainWindow(glb)
	glb.mainwindow = mainwindow
	mainwindow.show()
	err = app.exec_()
	glb.ShutdownInstances()
	db.close()
	sys.exit(err)

if __name__ == "__main__":
	Main()
