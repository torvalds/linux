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
from PySide.QtCore import *
from PySide.QtGui import *
from PySide.QtSql import *
from decimal import *

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
		edit_menu.addAction(CreateAction("&Shrink Font", "Make text smaller", self.ShrinkFont, self, [QKeySequence("Ctrl+-")]))
		edit_menu.addAction(CreateAction("&Enlarge Font", "Make text bigger", self.EnlargeFont, self, [QKeySequence("Ctrl++")]))

		reports_menu = menu.addMenu("&Reports")
		reports_menu.addAction(CreateAction("Context-Sensitive Call &Graph", "Create a new window containing a context-sensitive call graph", self.NewCallGraph, self))

		self.window_menu = WindowMenu(self.mdi_area, menu)

	def Find(self):
		win = self.mdi_area.activeSubWindow()
		if win:
			try:
				win.find_bar.Activate()
			except:
				pass

	def ShrinkFont(self):
		win = self.mdi_area.activeSubWindow()
		ShrinkFont(win.view)

	def EnlargeFont(self):
		win = self.mdi_area.activeSubWindow()
		EnlargeFont(win.view)

	def NewCallGraph(self):
		CallGraphWindow(self.glb, self)

# Global data

class Glb():

	def __init__(self, dbref, db, dbname):
		self.dbref = dbref
		self.db = db
		self.dbname = dbname
		self.app = None
		self.mainwindow = None

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
	db.close()
	sys.exit(err)

if __name__ == "__main__":
	Main()
