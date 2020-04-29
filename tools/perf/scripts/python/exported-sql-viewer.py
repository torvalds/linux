#!/usr/bin/env python
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

# There is also a "All branches" report, which displays branches and
# possibly disassembly.  However, presently, the only supported disassembler is
# Intel XED, and additionally the object code must be present in perf build ID
# cache. To use Intel XED, libxed.so must be present. To build and install
# libxed.so:
#            git clone https://github.com/intelxed/mbuild.git mbuild
#            git clone https://github.com/intelxed/xed
#            cd xed
#            ./mfile.py --share
#            sudo ./mfile.py --prefix=/usr/local install
#            sudo ldconfig
#
# Example report:
#
# Time           CPU  Command  PID    TID    Branch Type            In Tx  Branch
# 8107675239590  2    ls       22011  22011  return from interrupt  No     ffffffff86a00a67 native_irq_return_iret ([kernel]) -> 7fab593ea260 _start (ld-2.19.so)
#                                                                              7fab593ea260 48 89 e7                                        mov %rsp, %rdi
# 8107675239899  2    ls       22011  22011  hardware interrupt     No         7fab593ea260 _start (ld-2.19.so) -> ffffffff86a012e0 page_fault ([kernel])
# 8107675241900  2    ls       22011  22011  return from interrupt  No     ffffffff86a00a67 native_irq_return_iret ([kernel]) -> 7fab593ea260 _start (ld-2.19.so)
#                                                                              7fab593ea260 48 89 e7                                        mov %rsp, %rdi
#                                                                              7fab593ea263 e8 c8 06 00 00                                  callq  0x7fab593ea930
# 8107675241900  2    ls       22011  22011  call                   No         7fab593ea263 _start+0x3 (ld-2.19.so) -> 7fab593ea930 _dl_start (ld-2.19.so)
#                                                                              7fab593ea930 55                                              pushq  %rbp
#                                                                              7fab593ea931 48 89 e5                                        mov %rsp, %rbp
#                                                                              7fab593ea934 41 57                                           pushq  %r15
#                                                                              7fab593ea936 41 56                                           pushq  %r14
#                                                                              7fab593ea938 41 55                                           pushq  %r13
#                                                                              7fab593ea93a 41 54                                           pushq  %r12
#                                                                              7fab593ea93c 53                                              pushq  %rbx
#                                                                              7fab593ea93d 48 89 fb                                        mov %rdi, %rbx
#                                                                              7fab593ea940 48 83 ec 68                                     sub $0x68, %rsp
#                                                                              7fab593ea944 0f 31                                           rdtsc
#                                                                              7fab593ea946 48 c1 e2 20                                     shl $0x20, %rdx
#                                                                              7fab593ea94a 89 c0                                           mov %eax, %eax
#                                                                              7fab593ea94c 48 09 c2                                        or %rax, %rdx
#                                                                              7fab593ea94f 48 8b 05 1a 15 22 00                            movq  0x22151a(%rip), %rax
# 8107675242232  2    ls       22011  22011  hardware interrupt     No         7fab593ea94f _dl_start+0x1f (ld-2.19.so) -> ffffffff86a012e0 page_fault ([kernel])
# 8107675242900  2    ls       22011  22011  return from interrupt  No     ffffffff86a00a67 native_irq_return_iret ([kernel]) -> 7fab593ea94f _dl_start+0x1f (ld-2.19.so)
#                                                                              7fab593ea94f 48 8b 05 1a 15 22 00                            movq  0x22151a(%rip), %rax
#                                                                              7fab593ea956 48 89 15 3b 13 22 00                            movq  %rdx, 0x22133b(%rip)
# 8107675243232  2    ls       22011  22011  hardware interrupt     No         7fab593ea956 _dl_start+0x26 (ld-2.19.so) -> ffffffff86a012e0 page_fault ([kernel])

from __future__ import print_function

import sys
import argparse
import weakref
import threading
import string
try:
	# Python2
	import cPickle as pickle
	# size of pickled integer big enough for record size
	glb_nsz = 8
except ImportError:
	import pickle
	glb_nsz = 16
import re
import os
import random
import copy
import math

pyside_version_1 = True
if not "--pyside-version-1" in sys.argv:
	try:
		from PySide2.QtCore import *
		from PySide2.QtGui import *
		from PySide2.QtSql import *
		from PySide2.QtWidgets import *
		pyside_version_1 = False
	except:
		pass

if pyside_version_1:
	from PySide.QtCore import *
	from PySide.QtGui import *
	from PySide.QtSql import *

from decimal import *
from ctypes import *
from multiprocessing import Process, Array, Value, Event

# xrange is range in Python3
try:
	xrange
except NameError:
	xrange = range

def printerr(*args, **keyword_args):
	print(*args, file=sys.stderr, **keyword_args)

# Data formatting helpers

def tohex(ip):
	if ip < 0:
		ip += 1 << 64
	return "%x" % ip

def offstr(offset):
	if offset:
		return "+0x%x" % offset
	return ""

def dsoname(name):
	if name == "[kernel.kallsyms]":
		return "[kernel]"
	return name

def findnth(s, sub, n, offs=0):
	pos = s.find(sub)
	if pos < 0:
		return pos
	if n <= 1:
		return offs + pos
	return findnth(s[pos + 1:], sub, n - 1, offs + pos + 1)

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

	def __init__(self, glb, params, parent=None):
		super(TreeModel, self).__init__(parent)
		self.glb = glb
		self.params = params
		self.root = self.GetRoot()
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

def LookupModel(model_name):
	model_cache_lock.acquire()
	try:
		model = model_cache[model_name]
	except:
		model = None
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
		self.bar.setLayout(self.hbox)
		self.bar.hide()

	def Widget(self):
		return self.bar

	def Activate(self):
		self.bar.show()
		self.textbox.lineEdit().selectAll()
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

	def __init__(self, glb, params, row, parent_item):
		self.glb = glb
		self.params = params
		self.row = row
		self.parent_item = parent_item
		self.query_done = False
		self.child_count = 0
		self.child_items = []
		if parent_item:
			self.level = parent_item.level + 1
		else:
			self.level = 0

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

	def __init__(self, glb, params, row, comm_id, thread_id, call_path_id, time, insn_cnt, cyc_cnt, branch_count, parent_item):
		super(CallGraphLevelTwoPlusItemBase, self).__init__(glb, params, row, parent_item)
		self.comm_id = comm_id
		self.thread_id = thread_id
		self.call_path_id = call_path_id
		self.insn_cnt = insn_cnt
		self.cyc_cnt = cyc_cnt
		self.branch_count = branch_count
		self.time = time

	def Select(self):
		self.query_done = True
		query = QSqlQuery(self.glb.db)
		if self.params.have_ipc:
			ipc_str = ", SUM(insn_count), SUM(cyc_count)"
		else:
			ipc_str = ""
		QueryExec(query, "SELECT call_path_id, name, short_name, COUNT(calls.id), SUM(return_time - call_time)" + ipc_str + ", SUM(branch_count)"
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
			if self.params.have_ipc:
				insn_cnt = int(query.value(5))
				cyc_cnt = int(query.value(6))
				branch_count = int(query.value(7))
			else:
				insn_cnt = 0
				cyc_cnt = 0
				branch_count = int(query.value(5))
			child_item = CallGraphLevelThreeItem(self.glb, self.params, self.child_count, self.comm_id, self.thread_id, query.value(0), query.value(1), query.value(2), query.value(3), int(query.value(4)), insn_cnt, cyc_cnt, branch_count, self)
			self.child_items.append(child_item)
			self.child_count += 1

# Context-sensitive call graph data model level three item

class CallGraphLevelThreeItem(CallGraphLevelTwoPlusItemBase):

	def __init__(self, glb, params, row, comm_id, thread_id, call_path_id, name, dso, count, time, insn_cnt, cyc_cnt, branch_count, parent_item):
		super(CallGraphLevelThreeItem, self).__init__(glb, params, row, comm_id, thread_id, call_path_id, time, insn_cnt, cyc_cnt, branch_count, parent_item)
		dso = dsoname(dso)
		if self.params.have_ipc:
			insn_pcnt = PercentToOneDP(insn_cnt, parent_item.insn_cnt)
			cyc_pcnt = PercentToOneDP(cyc_cnt, parent_item.cyc_cnt)
			br_pcnt = PercentToOneDP(branch_count, parent_item.branch_count)
			ipc = CalcIPC(cyc_cnt, insn_cnt)
			self.data = [ name, dso, str(count), str(time), PercentToOneDP(time, parent_item.time), str(insn_cnt), insn_pcnt, str(cyc_cnt), cyc_pcnt, ipc, str(branch_count), br_pcnt ]
		else:
			self.data = [ name, dso, str(count), str(time), PercentToOneDP(time, parent_item.time), str(branch_count), PercentToOneDP(branch_count, parent_item.branch_count) ]
		self.dbid = call_path_id

# Context-sensitive call graph data model level two item

class CallGraphLevelTwoItem(CallGraphLevelTwoPlusItemBase):

	def __init__(self, glb, params, row, comm_id, thread_id, pid, tid, parent_item):
		super(CallGraphLevelTwoItem, self).__init__(glb, params, row, comm_id, thread_id, 1, 0, 0, 0, 0, parent_item)
		if self.params.have_ipc:
			self.data = [str(pid) + ":" + str(tid), "", "", "", "", "", "", "", "", "", "", ""]
		else:
			self.data = [str(pid) + ":" + str(tid), "", "", "", "", "", ""]
		self.dbid = thread_id

	def Select(self):
		super(CallGraphLevelTwoItem, self).Select()
		for child_item in self.child_items:
			self.time += child_item.time
			self.insn_cnt += child_item.insn_cnt
			self.cyc_cnt += child_item.cyc_cnt
			self.branch_count += child_item.branch_count
		for child_item in self.child_items:
			child_item.data[4] = PercentToOneDP(child_item.time, self.time)
			if self.params.have_ipc:
				child_item.data[6] = PercentToOneDP(child_item.insn_cnt, self.insn_cnt)
				child_item.data[8] = PercentToOneDP(child_item.cyc_cnt, self.cyc_cnt)
				child_item.data[11] = PercentToOneDP(child_item.branch_count, self.branch_count)
			else:
				child_item.data[6] = PercentToOneDP(child_item.branch_count, self.branch_count)

# Context-sensitive call graph data model level one item

class CallGraphLevelOneItem(CallGraphLevelItemBase):

	def __init__(self, glb, params, row, comm_id, comm, parent_item):
		super(CallGraphLevelOneItem, self).__init__(glb, params, row, parent_item)
		if self.params.have_ipc:
			self.data = [comm, "", "", "", "", "", "", "", "", "", "", ""]
		else:
			self.data = [comm, "", "", "", "", "", ""]
		self.dbid = comm_id

	def Select(self):
		self.query_done = True
		query = QSqlQuery(self.glb.db)
		QueryExec(query, "SELECT thread_id, pid, tid"
					" FROM comm_threads"
					" INNER JOIN threads ON thread_id = threads.id"
					" WHERE comm_id = " + str(self.dbid))
		while query.next():
			child_item = CallGraphLevelTwoItem(self.glb, self.params, self.child_count, self.dbid, query.value(0), query.value(1), query.value(2), self)
			self.child_items.append(child_item)
			self.child_count += 1

# Context-sensitive call graph data model root item

class CallGraphRootItem(CallGraphLevelItemBase):

	def __init__(self, glb, params):
		super(CallGraphRootItem, self).__init__(glb, params, 0, None)
		self.dbid = 0
		self.query_done = True
		if_has_calls = ""
		if IsSelectable(glb.db, "comms", columns = "has_calls"):
			if_has_calls = " WHERE has_calls = " + glb.dbref.TRUE
		query = QSqlQuery(glb.db)
		QueryExec(query, "SELECT id, comm FROM comms" + if_has_calls)
		while query.next():
			if not query.value(0):
				continue
			child_item = CallGraphLevelOneItem(glb, params, self.child_count, query.value(0), query.value(1), self)
			self.child_items.append(child_item)
			self.child_count += 1

# Call graph model parameters

class CallGraphModelParams():

	def __init__(self, glb, parent=None):
		self.have_ipc = IsSelectable(glb.db, "calls", columns = "insn_count, cyc_count")

# Context-sensitive call graph data model base

class CallGraphModelBase(TreeModel):

	def __init__(self, glb, parent=None):
		super(CallGraphModelBase, self).__init__(glb, CallGraphModelParams(glb), parent)

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
		self.DoFindSelect(query, match)

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

# Context-sensitive call graph data model

class CallGraphModel(CallGraphModelBase):

	def __init__(self, glb, parent=None):
		super(CallGraphModel, self).__init__(glb, parent)

	def GetRoot(self):
		return CallGraphRootItem(self.glb, self.params)

	def columnCount(self, parent=None):
		if self.params.have_ipc:
			return 12
		else:
			return 7

	def columnHeader(self, column):
		if self.params.have_ipc:
			headers = ["Call Path", "Object", "Count ", "Time (ns) ", "Time (%) ", "Insn Cnt", "Insn Cnt (%)", "Cyc Cnt", "Cyc Cnt (%)", "IPC", "Branch Count ", "Branch Count (%) "]
		else:
			headers = ["Call Path", "Object", "Count ", "Time (ns) ", "Time (%) ", "Branch Count ", "Branch Count (%) "]
		return headers[column]

	def columnAlignment(self, column):
		if self.params.have_ipc:
			alignment = [ Qt.AlignLeft, Qt.AlignLeft, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight ]
		else:
			alignment = [ Qt.AlignLeft, Qt.AlignLeft, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight ]
		return alignment[column]

	def DoFindSelect(self, query, match):
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

# Call tree data model level 2+ item base

class CallTreeLevelTwoPlusItemBase(CallGraphLevelItemBase):

	def __init__(self, glb, params, row, comm_id, thread_id, calls_id, call_time, time, insn_cnt, cyc_cnt, branch_count, parent_item):
		super(CallTreeLevelTwoPlusItemBase, self).__init__(glb, params, row, parent_item)
		self.comm_id = comm_id
		self.thread_id = thread_id
		self.calls_id = calls_id
		self.call_time = call_time
		self.time = time
		self.insn_cnt = insn_cnt
		self.cyc_cnt = cyc_cnt
		self.branch_count = branch_count

	def Select(self):
		self.query_done = True
		if self.calls_id == 0:
			comm_thread = " AND comm_id = " + str(self.comm_id) + " AND thread_id = " + str(self.thread_id)
		else:
			comm_thread = ""
		if self.params.have_ipc:
			ipc_str = ", insn_count, cyc_count"
		else:
			ipc_str = ""
		query = QSqlQuery(self.glb.db)
		QueryExec(query, "SELECT calls.id, name, short_name, call_time, return_time - call_time" + ipc_str + ", branch_count"
					" FROM calls"
					" INNER JOIN call_paths ON calls.call_path_id = call_paths.id"
					" INNER JOIN symbols ON call_paths.symbol_id = symbols.id"
					" INNER JOIN dsos ON symbols.dso_id = dsos.id"
					" WHERE calls.parent_id = " + str(self.calls_id) + comm_thread +
					" ORDER BY call_time, calls.id")
		while query.next():
			if self.params.have_ipc:
				insn_cnt = int(query.value(5))
				cyc_cnt = int(query.value(6))
				branch_count = int(query.value(7))
			else:
				insn_cnt = 0
				cyc_cnt = 0
				branch_count = int(query.value(5))
			child_item = CallTreeLevelThreeItem(self.glb, self.params, self.child_count, self.comm_id, self.thread_id, query.value(0), query.value(1), query.value(2), query.value(3), int(query.value(4)), insn_cnt, cyc_cnt, branch_count, self)
			self.child_items.append(child_item)
			self.child_count += 1

# Call tree data model level three item

class CallTreeLevelThreeItem(CallTreeLevelTwoPlusItemBase):

	def __init__(self, glb, params, row, comm_id, thread_id, calls_id, name, dso, call_time, time, insn_cnt, cyc_cnt, branch_count, parent_item):
		super(CallTreeLevelThreeItem, self).__init__(glb, params, row, comm_id, thread_id, calls_id, call_time, time, insn_cnt, cyc_cnt, branch_count, parent_item)
		dso = dsoname(dso)
		if self.params.have_ipc:
			insn_pcnt = PercentToOneDP(insn_cnt, parent_item.insn_cnt)
			cyc_pcnt = PercentToOneDP(cyc_cnt, parent_item.cyc_cnt)
			br_pcnt = PercentToOneDP(branch_count, parent_item.branch_count)
			ipc = CalcIPC(cyc_cnt, insn_cnt)
			self.data = [ name, dso, str(call_time), str(time), PercentToOneDP(time, parent_item.time), str(insn_cnt), insn_pcnt, str(cyc_cnt), cyc_pcnt, ipc, str(branch_count), br_pcnt ]
		else:
			self.data = [ name, dso, str(call_time), str(time), PercentToOneDP(time, parent_item.time), str(branch_count), PercentToOneDP(branch_count, parent_item.branch_count) ]
		self.dbid = calls_id

# Call tree data model level two item

class CallTreeLevelTwoItem(CallTreeLevelTwoPlusItemBase):

	def __init__(self, glb, params, row, comm_id, thread_id, pid, tid, parent_item):
		super(CallTreeLevelTwoItem, self).__init__(glb, params, row, comm_id, thread_id, 0, 0, 0, 0, 0, 0, parent_item)
		if self.params.have_ipc:
			self.data = [str(pid) + ":" + str(tid), "", "", "", "", "", "", "", "", "", "", ""]
		else:
			self.data = [str(pid) + ":" + str(tid), "", "", "", "", "", ""]
		self.dbid = thread_id

	def Select(self):
		super(CallTreeLevelTwoItem, self).Select()
		for child_item in self.child_items:
			self.time += child_item.time
			self.insn_cnt += child_item.insn_cnt
			self.cyc_cnt += child_item.cyc_cnt
			self.branch_count += child_item.branch_count
		for child_item in self.child_items:
			child_item.data[4] = PercentToOneDP(child_item.time, self.time)
			if self.params.have_ipc:
				child_item.data[6] = PercentToOneDP(child_item.insn_cnt, self.insn_cnt)
				child_item.data[8] = PercentToOneDP(child_item.cyc_cnt, self.cyc_cnt)
				child_item.data[11] = PercentToOneDP(child_item.branch_count, self.branch_count)
			else:
				child_item.data[6] = PercentToOneDP(child_item.branch_count, self.branch_count)

# Call tree data model level one item

class CallTreeLevelOneItem(CallGraphLevelItemBase):

	def __init__(self, glb, params, row, comm_id, comm, parent_item):
		super(CallTreeLevelOneItem, self).__init__(glb, params, row, parent_item)
		if self.params.have_ipc:
			self.data = [comm, "", "", "", "", "", "", "", "", "", "", ""]
		else:
			self.data = [comm, "", "", "", "", "", ""]
		self.dbid = comm_id

	def Select(self):
		self.query_done = True
		query = QSqlQuery(self.glb.db)
		QueryExec(query, "SELECT thread_id, pid, tid"
					" FROM comm_threads"
					" INNER JOIN threads ON thread_id = threads.id"
					" WHERE comm_id = " + str(self.dbid))
		while query.next():
			child_item = CallTreeLevelTwoItem(self.glb, self.params, self.child_count, self.dbid, query.value(0), query.value(1), query.value(2), self)
			self.child_items.append(child_item)
			self.child_count += 1

# Call tree data model root item

class CallTreeRootItem(CallGraphLevelItemBase):

	def __init__(self, glb, params):
		super(CallTreeRootItem, self).__init__(glb, params, 0, None)
		self.dbid = 0
		self.query_done = True
		if_has_calls = ""
		if IsSelectable(glb.db, "comms", columns = "has_calls"):
			if_has_calls = " WHERE has_calls = " + glb.dbref.TRUE
		query = QSqlQuery(glb.db)
		QueryExec(query, "SELECT id, comm FROM comms" + if_has_calls)
		while query.next():
			if not query.value(0):
				continue
			child_item = CallTreeLevelOneItem(glb, params, self.child_count, query.value(0), query.value(1), self)
			self.child_items.append(child_item)
			self.child_count += 1

# Call Tree data model

class CallTreeModel(CallGraphModelBase):

	def __init__(self, glb, parent=None):
		super(CallTreeModel, self).__init__(glb, parent)

	def GetRoot(self):
		return CallTreeRootItem(self.glb, self.params)

	def columnCount(self, parent=None):
		if self.params.have_ipc:
			return 12
		else:
			return 7

	def columnHeader(self, column):
		if self.params.have_ipc:
			headers = ["Call Path", "Object", "Call Time", "Time (ns) ", "Time (%) ", "Insn Cnt", "Insn Cnt (%)", "Cyc Cnt", "Cyc Cnt (%)", "IPC", "Branch Count ", "Branch Count (%) "]
		else:
			headers = ["Call Path", "Object", "Call Time", "Time (ns) ", "Time (%) ", "Branch Count ", "Branch Count (%) "]
		return headers[column]

	def columnAlignment(self, column):
		if self.params.have_ipc:
			alignment = [ Qt.AlignLeft, Qt.AlignLeft, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight ]
		else:
			alignment = [ Qt.AlignLeft, Qt.AlignLeft, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight, Qt.AlignRight ]
		return alignment[column]

	def DoFindSelect(self, query, match):
		QueryExec(query, "SELECT calls.id, comm_id, thread_id"
						" FROM calls"
						" INNER JOIN call_paths ON calls.call_path_id = call_paths.id"
						" INNER JOIN symbols ON call_paths.symbol_id = symbols.id"
						" WHERE symbols.name" + match +
						" ORDER BY comm_id, thread_id, call_time, calls.id")

	def FindPath(self, query):
		# Turn the query result into a list of ids that the tree view can walk
		# to open the tree at the right place.
		ids = []
		parent_id = query.value(0)
		while parent_id:
			ids.insert(0, parent_id)
			q2 = QSqlQuery(self.glb.db)
			QueryExec(q2, "SELECT parent_id"
					" FROM calls"
					" WHERE id = " + str(parent_id))
			if not q2.next():
				break
			parent_id = q2.value(0)
		ids.insert(0, query.value(2))
		ids.insert(0, query.value(1))
		return ids

# Vertical layout

class HBoxLayout(QHBoxLayout):

	def __init__(self, *children):
		super(HBoxLayout, self).__init__()

		self.layout().setContentsMargins(0, 0, 0, 0)
		for child in children:
			if child.isWidgetType():
				self.layout().addWidget(child)
			else:
				self.layout().addLayout(child)

# Horizontal layout

class VBoxLayout(QVBoxLayout):

	def __init__(self, *children):
		super(VBoxLayout, self).__init__()

		self.layout().setContentsMargins(0, 0, 0, 0)
		for child in children:
			if child.isWidgetType():
				self.layout().addWidget(child)
			else:
				self.layout().addLayout(child)

# Vertical layout widget

class VBox():

	def __init__(self, *children):
		self.vbox = QWidget()
		self.vbox.setLayout(VBoxLayout(*children))

	def Widget(self):
		return self.vbox

# Tree window base

class TreeWindowBase(QMdiSubWindow):

	def __init__(self, parent=None):
		super(TreeWindowBase, self).__init__(parent)

		self.model = None
		self.find_bar = None

		self.view = QTreeView()
		self.view.setSelectionMode(QAbstractItemView.ContiguousSelection)
		self.view.CopyCellsToClipboard = CopyTreeCellsToClipboard

		self.context_menu = TreeContextMenu(self.view)

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


# Context-sensitive call graph window

class CallGraphWindow(TreeWindowBase):

	def __init__(self, glb, parent=None):
		super(CallGraphWindow, self).__init__(parent)

		self.model = LookupCreateModel("Context-Sensitive Call Graph", lambda x=glb: CallGraphModel(x))

		self.view.setModel(self.model)

		for c, w in ((0, 250), (1, 100), (2, 60), (3, 70), (4, 70), (5, 100)):
			self.view.setColumnWidth(c, w)

		self.find_bar = FindBar(self, self)

		self.vbox = VBox(self.view, self.find_bar.Widget())

		self.setWidget(self.vbox.Widget())

		AddSubWindow(glb.mainwindow.mdi_area, self, "Context-Sensitive Call Graph")

# Call tree window

class CallTreeWindow(TreeWindowBase):

	def __init__(self, glb, parent=None, thread_at_time=None):
		super(CallTreeWindow, self).__init__(parent)

		self.model = LookupCreateModel("Call Tree", lambda x=glb: CallTreeModel(x))

		self.view.setModel(self.model)

		for c, w in ((0, 230), (1, 100), (2, 100), (3, 70), (4, 70), (5, 100)):
			self.view.setColumnWidth(c, w)

		self.find_bar = FindBar(self, self)

		self.vbox = VBox(self.view, self.find_bar.Widget())

		self.setWidget(self.vbox.Widget())

		AddSubWindow(glb.mainwindow.mdi_area, self, "Call Tree")

		if thread_at_time:
			self.DisplayThreadAtTime(*thread_at_time)

	def DisplayThreadAtTime(self, comm_id, thread_id, time):
		parent = QModelIndex()
		for dbid in (comm_id, thread_id):
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
				return
		found = False
		while True:
			n = self.model.rowCount(parent)
			if not n:
				return
			last_child = None
			for row in xrange(n):
				child = self.model.index(row, 0, parent)
				child_call_time = child.internalPointer().call_time
				if child_call_time < time:
					last_child = child
				elif child_call_time == time:
					self.view.setCurrentIndex(child)
					return
				elif child_call_time > time:
					break
			if not last_child:
				if not found:
					child = self.model.index(0, 0, parent)
					self.view.setCurrentIndex(child)
				return
			found = True
			self.view.setCurrentIndex(last_child)
			parent = last_child

# ExecComm() gets the comm_id of the command string that was set when the process exec'd i.e. the program name

def ExecComm(db, thread_id, time):
	query = QSqlQuery(db)
	QueryExec(query, "SELECT comm_threads.comm_id, comms.c_time, comms.exec_flag"
				" FROM comm_threads"
				" INNER JOIN comms ON comms.id = comm_threads.comm_id"
				" WHERE comm_threads.thread_id = " + str(thread_id) +
				" ORDER BY comms.c_time, comms.id")
	first = None
	last = None
	while query.next():
		if first is None:
			first = query.value(0)
		if query.value(2) and Decimal(query.value(1)) <= Decimal(time):
			last = query.value(0)
	if not(last is None):
		return last
	return first

# Container for (x, y) data

class XY():
	def __init__(self, x=0, y=0):
		self.x = x
		self.y = y

	def __str__(self):
		return "XY({}, {})".format(str(self.x), str(self.y))

# Container for sub-range data

class Subrange():
	def __init__(self, lo=0, hi=0):
		self.lo = lo
		self.hi = hi

	def __str__(self):
		return "Subrange({}, {})".format(str(self.lo), str(self.hi))

# Graph data region base class

class GraphDataRegion(object):

	def __init__(self, key, title = "", ordinal = ""):
		self.key = key
		self.title = title
		self.ordinal = ordinal

# Function to sort GraphDataRegion

def GraphDataRegionOrdinal(data_region):
	return data_region.ordinal

# Attributes for a graph region

class GraphRegionAttribute():

	def __init__(self, colour):
		self.colour = colour

# Switch graph data region represents a task

class SwitchGraphDataRegion(GraphDataRegion):

	def __init__(self, key, exec_comm_id, pid, tid, comm, thread_id, comm_id):
		super(SwitchGraphDataRegion, self).__init__(key)

		self.title = str(pid) + " / " + str(tid) + " " + comm
		# Order graph legend within exec comm by pid / tid / time
		self.ordinal = str(pid).rjust(16) + str(exec_comm_id).rjust(8) + str(tid).rjust(16)
		self.exec_comm_id = exec_comm_id
		self.pid = pid
		self.tid = tid
		self.comm = comm
		self.thread_id = thread_id
		self.comm_id = comm_id

# Graph data point

class GraphDataPoint():

	def __init__(self, data, index, x, y, altx=None, alty=None, hregion=None, vregion=None):
		self.data = data
		self.index = index
		self.x = x
		self.y = y
		self.altx = altx
		self.alty = alty
		self.hregion = hregion
		self.vregion = vregion

# Graph data (single graph) base class

class GraphData(object):

	def __init__(self, collection, xbase=Decimal(0), ybase=Decimal(0)):
		self.collection = collection
		self.points = []
		self.xbase = xbase
		self.ybase = ybase
		self.title = ""

	def AddPoint(self, x, y, altx=None, alty=None, hregion=None, vregion=None):
		index = len(self.points)

		x = float(Decimal(x) - self.xbase)
		y = float(Decimal(y) - self.ybase)

		self.points.append(GraphDataPoint(self, index, x, y, altx, alty, hregion, vregion))

	def XToData(self, x):
		return Decimal(x) + self.xbase

	def YToData(self, y):
		return Decimal(y) + self.ybase

# Switch graph data (for one CPU)

class SwitchGraphData(GraphData):

	def __init__(self, db, collection, cpu, xbase):
		super(SwitchGraphData, self).__init__(collection, xbase)

		self.cpu = cpu
		self.title = "CPU " + str(cpu)
		self.SelectSwitches(db)

	def SelectComms(self, db, thread_id, last_comm_id, start_time, end_time):
		query = QSqlQuery(db)
		QueryExec(query, "SELECT id, c_time"
					" FROM comms"
					" WHERE c_thread_id = " + str(thread_id) +
					"   AND exec_flag = " + self.collection.glb.dbref.TRUE +
					"   AND c_time >= " + str(start_time) +
					"   AND c_time <= " + str(end_time) +
					" ORDER BY c_time, id")
		while query.next():
			comm_id = query.value(0)
			if comm_id == last_comm_id:
				continue
			time = query.value(1)
			hregion = self.HRegion(db, thread_id, comm_id, time)
			self.AddPoint(time, 1000, None, None, hregion)

	def SelectSwitches(self, db):
		last_time = None
		last_comm_id = None
		last_thread_id = None
		query = QSqlQuery(db)
		QueryExec(query, "SELECT time, thread_out_id, thread_in_id, comm_out_id, comm_in_id, flags"
					" FROM context_switches"
					" WHERE machine_id = " + str(self.collection.machine_id) +
					"   AND cpu = " + str(self.cpu) +
					" ORDER BY time, id")
		while query.next():
			flags = int(query.value(5))
			if flags & 1:
				# Schedule-out: detect and add exec's
				if last_thread_id == query.value(1) and last_comm_id is not None and last_comm_id != query.value(3):
					self.SelectComms(db, last_thread_id, last_comm_id, last_time, query.value(0))
				continue
			# Schedule-in: add data point
			if len(self.points) == 0:
				start_time = self.collection.glb.StartTime(self.collection.machine_id)
				hregion = self.HRegion(db, query.value(1), query.value(3), start_time)
				self.AddPoint(start_time, 1000, None, None, hregion)
			time = query.value(0)
			comm_id = query.value(4)
			thread_id = query.value(2)
			hregion = self.HRegion(db, thread_id, comm_id, time)
			self.AddPoint(time, 1000, None, None, hregion)
			last_time = time
			last_comm_id = comm_id
			last_thread_id = thread_id

	def NewHRegion(self, db, key, thread_id, comm_id, time):
		exec_comm_id = ExecComm(db, thread_id, time)
		query = QSqlQuery(db)
		QueryExec(query, "SELECT pid, tid FROM threads WHERE id = " + str(thread_id))
		if query.next():
			pid = query.value(0)
			tid = query.value(1)
		else:
			pid = -1
			tid = -1
		query = QSqlQuery(db)
		QueryExec(query, "SELECT comm FROM comms WHERE id = " + str(comm_id))
		if query.next():
			comm = query.value(0)
		else:
			comm = ""
		return SwitchGraphDataRegion(key, exec_comm_id, pid, tid, comm, thread_id, comm_id)

	def HRegion(self, db, thread_id, comm_id, time):
		key = str(thread_id) + ":" + str(comm_id)
		hregion = self.collection.LookupHRegion(key)
		if hregion is None:
			hregion = self.NewHRegion(db, key, thread_id, comm_id, time)
			self.collection.AddHRegion(key, hregion)
		return hregion

# Graph data collection (multiple related graphs) base class

class GraphDataCollection(object):

	def __init__(self, glb):
		self.glb = glb
		self.data = []
		self.hregions = {}
		self.xrangelo = None
		self.xrangehi = None
		self.yrangelo = None
		self.yrangehi = None
		self.dp = XY(0, 0)

	def AddGraphData(self, data):
		self.data.append(data)

	def LookupHRegion(self, key):
		if key in self.hregions:
			return self.hregions[key]
		return None

	def AddHRegion(self, key, hregion):
		self.hregions[key] = hregion

# Switch graph data collection (SwitchGraphData for each CPU)

class SwitchGraphDataCollection(GraphDataCollection):

	def __init__(self, glb, db, machine_id):
		super(SwitchGraphDataCollection, self).__init__(glb)

		self.machine_id = machine_id
		self.cpus = self.SelectCPUs(db)

		self.xrangelo = glb.StartTime(machine_id)
		self.xrangehi = glb.FinishTime(machine_id)

		self.yrangelo = Decimal(0)
		self.yrangehi = Decimal(1000)

		for cpu in self.cpus:
			self.AddGraphData(SwitchGraphData(db, self, cpu, self.xrangelo))

	def SelectCPUs(self, db):
		cpus = []
		query = QSqlQuery(db)
		QueryExec(query, "SELECT DISTINCT cpu"
					" FROM context_switches"
					" WHERE machine_id = " + str(self.machine_id))
		while query.next():
			cpus.append(int(query.value(0)))
		return sorted(cpus)

# Switch graph data graphics item displays the graphed data

class SwitchGraphDataGraphicsItem(QGraphicsItem):

	def __init__(self, data, graph_width, graph_height, attrs, event_handler, parent=None):
		super(SwitchGraphDataGraphicsItem, self).__init__(parent)

		self.data = data
		self.graph_width = graph_width
		self.graph_height = graph_height
		self.attrs = attrs
		self.event_handler = event_handler
		self.setAcceptHoverEvents(True)

	def boundingRect(self):
		return QRectF(0, 0, self.graph_width, self.graph_height)

	def PaintPoint(self, painter, last, x):
		if not(last is None or last.hregion.pid == 0 or x < self.attrs.subrange.x.lo):
			if last.x < self.attrs.subrange.x.lo:
				x0 = self.attrs.subrange.x.lo
			else:
				x0 = last.x
			if x > self.attrs.subrange.x.hi:
				x1 = self.attrs.subrange.x.hi
			else:
				x1 = x - 1
			x0 = self.attrs.XToPixel(x0)
			x1 = self.attrs.XToPixel(x1)

			y0 = self.attrs.YToPixel(last.y)

			colour = self.attrs.region_attributes[last.hregion.key].colour

			width = x1 - x0 + 1
			if width < 2:
				painter.setPen(colour)
				painter.drawLine(x0, self.graph_height - y0, x0, self.graph_height)
			else:
				painter.fillRect(x0, self.graph_height - y0, width, self.graph_height - 1, colour)

	def paint(self, painter, option, widget):
		last = None
		for point in self.data.points:
			self.PaintPoint(painter, last, point.x)
			if point.x > self.attrs.subrange.x.hi:
				break;
			last = point
		self.PaintPoint(painter, last, self.attrs.subrange.x.hi + 1)

	def BinarySearchPoint(self, target):
		lower_pos = 0
		higher_pos = len(self.data.points)
		while True:
			pos = int((lower_pos + higher_pos) / 2)
			val = self.data.points[pos].x
			if target >= val:
				lower_pos = pos
			else:
				higher_pos = pos
			if higher_pos <= lower_pos + 1:
				return lower_pos

	def XPixelToData(self, x):
		x = self.attrs.PixelToX(x)
		if x < self.data.points[0].x:
			x = 0
			pos = 0
			low = True
		else:
			pos = self.BinarySearchPoint(x)
			low = False
		return (low, pos, self.data.XToData(x))

	def EventToData(self, event):
		no_data = (None,) * 4
		if len(self.data.points) < 1:
			return no_data
		x = event.pos().x()
		if x < 0:
			return no_data
		low0, pos0, time_from = self.XPixelToData(x)
		low1, pos1, time_to = self.XPixelToData(x + 1)
		hregions = set()
		hregion_times = []
		if not low1:
			for i in xrange(pos0, pos1 + 1):
				hregion = self.data.points[i].hregion
				hregions.add(hregion)
				if i == pos0:
					time = time_from
				else:
					time = self.data.XToData(self.data.points[i].x)
				hregion_times.append((hregion, time))
		return (time_from, time_to, hregions, hregion_times)

	def hoverMoveEvent(self, event):
		time_from, time_to, hregions, hregion_times = self.EventToData(event)
		if time_from is not None:
			self.event_handler.PointEvent(self.data.cpu, time_from, time_to, hregions)

	def hoverLeaveEvent(self, event):
		self.event_handler.NoPointEvent()

	def mousePressEvent(self, event):
		if event.button() != Qt.RightButton:
			super(SwitchGraphDataGraphicsItem, self).mousePressEvent(event)
			return
		time_from, time_to, hregions, hregion_times = self.EventToData(event)
		if hregion_times:
			self.event_handler.RightClickEvent(self.data.cpu, hregion_times, event.screenPos())

# X-axis graphics item

class XAxisGraphicsItem(QGraphicsItem):

	def __init__(self, width, parent=None):
		super(XAxisGraphicsItem, self).__init__(parent)

		self.width = width
		self.max_mark_sz = 4
		self.height = self.max_mark_sz + 1

	def boundingRect(self):
		return QRectF(0, 0, self.width, self.height)

	def Step(self):
		attrs = self.parentItem().attrs
		subrange = attrs.subrange.x
		t = subrange.hi - subrange.lo
		s = (3.0 * t) / self.width
		n = 1.0
		while s > n:
			n = n * 10.0
		return n

	def PaintMarks(self, painter, at_y, lo, hi, step, i):
		attrs = self.parentItem().attrs
		x = lo
		while x <= hi:
			xp = attrs.XToPixel(x)
			if i % 10:
				if i % 5:
					sz = 1
				else:
					sz = 2
			else:
				sz = self.max_mark_sz
				i = 0
			painter.drawLine(xp, at_y, xp, at_y + sz)
			x += step
			i += 1

	def paint(self, painter, option, widget):
		# Using QPainter::drawLine(int x1, int y1, int x2, int y2) so x2 = width -1
		painter.drawLine(0, 0, self.width - 1, 0)
		n = self.Step()
		attrs = self.parentItem().attrs
		subrange = attrs.subrange.x
		if subrange.lo:
			x_offset = n - (subrange.lo % n)
		else:
			x_offset = 0.0
		x = subrange.lo + x_offset
		i = (x / n) % 10
		self.PaintMarks(painter, 0, x, subrange.hi, n, i)

	def ScaleDimensions(self):
		n = self.Step()
		attrs = self.parentItem().attrs
		lo = attrs.subrange.x.lo
		hi = (n * 10.0) + lo
		width = attrs.XToPixel(hi)
		if width > 500:
			width = 0
		return (n, lo, hi, width)

	def PaintScale(self, painter, at_x, at_y):
		n, lo, hi, width = self.ScaleDimensions()
		if not width:
			return
		painter.drawLine(at_x, at_y, at_x + width, at_y)
		self.PaintMarks(painter, at_y, lo, hi, n, 0)

	def ScaleWidth(self):
		n, lo, hi, width = self.ScaleDimensions()
		return width

	def ScaleHeight(self):
		return self.height

	def ScaleUnit(self):
		return self.Step() * 10

# Scale graphics item base class

class ScaleGraphicsItem(QGraphicsItem):

	def __init__(self, axis, parent=None):
		super(ScaleGraphicsItem, self).__init__(parent)
		self.axis = axis

	def boundingRect(self):
		scale_width = self.axis.ScaleWidth()
		if not scale_width:
			return QRectF()
		return QRectF(0, 0, self.axis.ScaleWidth() + 100, self.axis.ScaleHeight())

	def paint(self, painter, option, widget):
		scale_width = self.axis.ScaleWidth()
		if not scale_width:
			return
		self.axis.PaintScale(painter, 0, 5)
		x = scale_width + 4
		painter.drawText(QPointF(x, 10), self.Text())

	def Unit(self):
		return self.axis.ScaleUnit()

	def Text(self):
		return ""

# Switch graph scale graphics item

class SwitchScaleGraphicsItem(ScaleGraphicsItem):

	def __init__(self, axis, parent=None):
		super(SwitchScaleGraphicsItem, self).__init__(axis, parent)

	def Text(self):
		unit = self.Unit()
		if unit >= 1000000000:
			unit = int(unit / 1000000000)
			us = "s"
		elif unit >= 1000000:
			unit = int(unit / 1000000)
			us = "ms"
		elif unit >= 1000:
			unit = int(unit / 1000)
			us = "us"
		else:
			unit = int(unit)
			us = "ns"
		return " = " + str(unit) + " " + us

# Switch graph graphics item contains graph title, scale, x/y-axis, and the graphed data

class SwitchGraphGraphicsItem(QGraphicsItem):

	def __init__(self, collection, data, attrs, event_handler, first, parent=None):
		super(SwitchGraphGraphicsItem, self).__init__(parent)
		self.collection = collection
		self.data = data
		self.attrs = attrs
		self.event_handler = event_handler

		margin = 20
		title_width = 50

		self.title_graphics = QGraphicsSimpleTextItem(data.title, self)

		self.title_graphics.setPos(margin, margin)
		graph_width = attrs.XToPixel(attrs.subrange.x.hi) + 1
		graph_height = attrs.YToPixel(attrs.subrange.y.hi) + 1

		self.graph_origin_x = margin + title_width + margin
		self.graph_origin_y = graph_height + margin

		x_axis_size = 1
		y_axis_size = 1
		self.yline = QGraphicsLineItem(0, 0, 0, graph_height, self)

		self.x_axis = XAxisGraphicsItem(graph_width, self)
		self.x_axis.setPos(self.graph_origin_x, self.graph_origin_y + 1)

		if first:
			self.scale_item = SwitchScaleGraphicsItem(self.x_axis, self)
			self.scale_item.setPos(self.graph_origin_x, self.graph_origin_y + 10)

		self.yline.setPos(self.graph_origin_x - y_axis_size, self.graph_origin_y - graph_height)

		self.axis_point = QGraphicsLineItem(0, 0, 0, 0, self)
		self.axis_point.setPos(self.graph_origin_x - 1, self.graph_origin_y +1)

		self.width = self.graph_origin_x + graph_width + margin
		self.height = self.graph_origin_y + margin

		self.graph = SwitchGraphDataGraphicsItem(data, graph_width, graph_height, attrs, event_handler, self)
		self.graph.setPos(self.graph_origin_x, self.graph_origin_y - graph_height)

		if parent and 'EnableRubberBand' in dir(parent):
			parent.EnableRubberBand(self.graph_origin_x, self.graph_origin_x + graph_width - 1, self)

	def boundingRect(self):
		return QRectF(0, 0, self.width, self.height)

	def paint(self, painter, option, widget):
		pass

	def RBXToPixel(self, x):
		return self.attrs.PixelToX(x - self.graph_origin_x)

	def RBXRangeToPixel(self, x0, x1):
		return (self.RBXToPixel(x0), self.RBXToPixel(x1 + 1))

	def RBPixelToTime(self, x):
		if x < self.data.points[0].x:
			return self.data.XToData(0)
		return self.data.XToData(x)

	def RBEventTimes(self, x0, x1):
		x0, x1 = self.RBXRangeToPixel(x0, x1)
		time_from = self.RBPixelToTime(x0)
		time_to = self.RBPixelToTime(x1)
		return (time_from, time_to)

	def RBEvent(self, x0, x1):
		time_from, time_to = self.RBEventTimes(x0, x1)
		self.event_handler.RangeEvent(time_from, time_to)

	def RBMoveEvent(self, x0, x1):
		if x1 < x0:
			x0, x1 = x1, x0
		self.RBEvent(x0, x1)

	def RBReleaseEvent(self, x0, x1, selection_state):
		if x1 < x0:
			x0, x1 = x1, x0
		x0, x1 = self.RBXRangeToPixel(x0, x1)
		self.event_handler.SelectEvent(x0, x1, selection_state)

# Graphics item to draw a vertical bracket (used to highlight "forward" sub-range)

class VerticalBracketGraphicsItem(QGraphicsItem):

	def __init__(self, parent=None):
		super(VerticalBracketGraphicsItem, self).__init__(parent)

		self.width = 0
		self.height = 0
		self.hide()

	def SetSize(self, width, height):
		self.width = width + 1
		self.height = height + 1

	def boundingRect(self):
		return QRectF(0, 0, self.width, self.height)

	def paint(self, painter, option, widget):
		colour = QColor(255, 255, 0, 32)
		painter.fillRect(0, 0, self.width, self.height, colour)
		x1 = self.width - 1
		y1 = self.height - 1
		painter.drawLine(0, 0, x1, 0)
		painter.drawLine(0, 0, 0, 3)
		painter.drawLine(x1, 0, x1, 3)
		painter.drawLine(0, y1, x1, y1)
		painter.drawLine(0, y1, 0, y1 - 3)
		painter.drawLine(x1, y1, x1, y1 - 3)

# Graphics item to contain graphs arranged vertically

class VertcalGraphSetGraphicsItem(QGraphicsItem):

	def __init__(self, collection, attrs, event_handler, child_class, parent=None):
		super(VertcalGraphSetGraphicsItem, self).__init__(parent)

		self.collection = collection

		self.top = 10

		self.width = 0
		self.height = self.top

		self.rubber_band = None
		self.rb_enabled = False

		first = True
		for data in collection.data:
			child = child_class(collection, data, attrs, event_handler, first, self)
			child.setPos(0, self.height + 1)
			rect = child.boundingRect()
			if rect.right() > self.width:
				self.width = rect.right()
			self.height = self.height + rect.bottom() + 1
			first = False

		self.bracket = VerticalBracketGraphicsItem(self)

	def EnableRubberBand(self, xlo, xhi, rb_event_handler):
		if self.rb_enabled:
			return
		self.rb_enabled = True
		self.rb_in_view = False
		self.setAcceptedMouseButtons(Qt.LeftButton)
		self.rb_xlo = xlo
		self.rb_xhi = xhi
		self.rb_event_handler = rb_event_handler
		self.mousePressEvent = self.MousePressEvent
		self.mouseMoveEvent = self.MouseMoveEvent
		self.mouseReleaseEvent = self.MouseReleaseEvent

	def boundingRect(self):
		return QRectF(0, 0, self.width, self.height)

	def paint(self, painter, option, widget):
		pass

	def RubberBandParent(self):
		scene = self.scene()
		view = scene.views()[0]
		viewport = view.viewport()
		return viewport

	def RubberBandSetGeometry(self, rect):
		scene_rectf = self.mapRectToScene(QRectF(rect))
		scene = self.scene()
		view = scene.views()[0]
		poly = view.mapFromScene(scene_rectf)
		self.rubber_band.setGeometry(poly.boundingRect())

	def SetSelection(self, selection_state):
		if self.rubber_band:
			if selection_state:
				self.RubberBandSetGeometry(selection_state)
				self.rubber_band.show()
			else:
				self.rubber_band.hide()

	def SetBracket(self, rect):
		if rect:
			x, y, width, height = rect.x(), rect.y(), rect.width(), rect.height()
			self.bracket.setPos(x, y)
			self.bracket.SetSize(width, height)
			self.bracket.show()
		else:
			self.bracket.hide()

	def RubberBandX(self, event):
		x = event.pos().toPoint().x()
		if x < self.rb_xlo:
			x = self.rb_xlo
		elif x > self.rb_xhi:
			x = self.rb_xhi
		else:
			self.rb_in_view = True
		return x

	def RubberBandRect(self, x):
		if self.rb_origin.x() <= x:
			width = x - self.rb_origin.x()
			rect = QRect(self.rb_origin, QSize(width, self.height))
		else:
			width = self.rb_origin.x() - x
			top_left = QPoint(self.rb_origin.x() - width, self.rb_origin.y())
			rect = QRect(top_left, QSize(width, self.height))
		return rect

	def MousePressEvent(self, event):
		self.rb_in_view = False
		x = self.RubberBandX(event)
		self.rb_origin = QPoint(x, self.top)
		if self.rubber_band is None:
			self.rubber_band = QRubberBand(QRubberBand.Rectangle, self.RubberBandParent())
		self.RubberBandSetGeometry(QRect(self.rb_origin, QSize(0, self.height)))
		if self.rb_in_view:
			self.rubber_band.show()
			self.rb_event_handler.RBMoveEvent(x, x)
		else:
			self.rubber_band.hide()

	def MouseMoveEvent(self, event):
		x = self.RubberBandX(event)
		rect = self.RubberBandRect(x)
		self.RubberBandSetGeometry(rect)
		if self.rb_in_view:
			self.rubber_band.show()
			self.rb_event_handler.RBMoveEvent(self.rb_origin.x(), x)

	def MouseReleaseEvent(self, event):
		x = self.RubberBandX(event)
		if self.rb_in_view:
			selection_state = self.RubberBandRect(x)
		else:
			selection_state = None
		self.rb_event_handler.RBReleaseEvent(self.rb_origin.x(), x, selection_state)

# Switch graph legend data model

class SwitchGraphLegendModel(QAbstractTableModel):

	def __init__(self, collection, region_attributes, parent=None):
		super(SwitchGraphLegendModel, self).__init__(parent)

		self.region_attributes = region_attributes

		self.child_items = sorted(collection.hregions.values(), key=GraphDataRegionOrdinal)
		self.child_count = len(self.child_items)

		self.highlight_set = set()

		self.column_headers = ("pid", "tid", "comm")

	def rowCount(self, parent):
		return self.child_count

	def headerData(self, section, orientation, role):
		if role != Qt.DisplayRole:
			return None
		if orientation != Qt.Horizontal:
			return None
		return self.columnHeader(section)

	def index(self, row, column, parent):
		return self.createIndex(row, column, self.child_items[row])

	def columnCount(self, parent=None):
		return len(self.column_headers)

	def columnHeader(self, column):
		return self.column_headers[column]

	def data(self, index, role):
		if role == Qt.BackgroundRole:
			child = self.child_items[index.row()]
			if child in self.highlight_set:
				return self.region_attributes[child.key].colour
			return None
		if role == Qt.ForegroundRole:
			child = self.child_items[index.row()]
			if child in self.highlight_set:
				return QColor(255, 255, 255)
			return self.region_attributes[child.key].colour
		if role != Qt.DisplayRole:
			return None
		hregion = self.child_items[index.row()]
		col = index.column()
		if col == 0:
			return hregion.pid
		if col == 1:
			return hregion.tid
		if col == 2:
			return hregion.comm
		return None

	def SetHighlight(self, row, set_highlight):
		child = self.child_items[row]
		top_left = self.createIndex(row, 0, child)
		bottom_right = self.createIndex(row, len(self.column_headers) - 1, child)
		self.dataChanged.emit(top_left, bottom_right)

	def Highlight(self, highlight_set):
		for row in xrange(self.child_count):
			child = self.child_items[row]
			if child in self.highlight_set:
				if child not in highlight_set:
					self.SetHighlight(row, False)
			elif child in highlight_set:
				self.SetHighlight(row, True)
		self.highlight_set = highlight_set

# Switch graph legend is a table

class SwitchGraphLegend(QWidget):

	def __init__(self, collection, region_attributes, parent=None):
		super(SwitchGraphLegend, self).__init__(parent)

		self.data_model = SwitchGraphLegendModel(collection, region_attributes)

		self.model = QSortFilterProxyModel()
		self.model.setSourceModel(self.data_model)

		self.view = QTableView()
		self.view.setModel(self.model)
		self.view.setEditTriggers(QAbstractItemView.NoEditTriggers)
		self.view.verticalHeader().setVisible(False)
		self.view.sortByColumn(-1, Qt.AscendingOrder)
		self.view.setSortingEnabled(True)
		self.view.resizeColumnsToContents()
		self.view.resizeRowsToContents()

		self.vbox = VBoxLayout(self.view)
		self.setLayout(self.vbox)

		sz1 = self.view.columnWidth(0) + self.view.columnWidth(1) + self.view.columnWidth(2) + 2
		sz1 = sz1 + self.view.verticalScrollBar().sizeHint().width()
		self.saved_size = sz1

	def resizeEvent(self, event):
		self.saved_size = self.size().width()
		super(SwitchGraphLegend, self).resizeEvent(event)

	def Highlight(self, highlight_set):
		self.data_model.Highlight(highlight_set)
		self.update()

	def changeEvent(self, event):
		if event.type() == QEvent.FontChange:
			self.view.resizeRowsToContents()
			self.view.resizeColumnsToContents()
			# Need to resize rows again after column resize
			self.view.resizeRowsToContents()
		super(SwitchGraphLegend, self).changeEvent(event)

# Random colour generation

def RGBColourTooLight(r, g, b):
	if g > 230:
		return True
	if g <= 160:
		return False
	if r <= 180 and g <= 180:
		return False
	if r < 60:
		return False
	return True

def GenerateColours(x):
	cs = [0]
	for i in xrange(1, x):
		cs.append(int((255.0 / i) + 0.5))
	colours = []
	for r in cs:
		for g in cs:
			for b in cs:
				# Exclude black and colours that look too light against a white background
				if (r, g, b) == (0, 0, 0) or RGBColourTooLight(r, g, b):
					continue
				colours.append(QColor(r, g, b))
	return colours

def GenerateNColours(n):
	for x in xrange(2, n + 2):
		colours = GenerateColours(x)
		if len(colours) >= n:
			return colours
	return []

def GenerateNRandomColours(n, seed):
	colours = GenerateNColours(n)
	random.seed(seed)
	random.shuffle(colours)
	return colours

# Graph attributes, in particular the scale and subrange that change when zooming

class GraphAttributes():

	def __init__(self, scale, subrange, region_attributes, dp):
		self.scale = scale
		self.subrange = subrange
		self.region_attributes = region_attributes
		# Rounding avoids errors due to finite floating point precision
		self.dp = dp	# data decimal places
		self.Update()

	def XToPixel(self, x):
		return int(round((x - self.subrange.x.lo) * self.scale.x, self.pdp.x))

	def YToPixel(self, y):
		return int(round((y - self.subrange.y.lo) * self.scale.y, self.pdp.y))

	def PixelToXRounded(self, px):
		return round((round(px, 0) / self.scale.x), self.dp.x) + self.subrange.x.lo

	def PixelToYRounded(self, py):
		return round((round(py, 0) / self.scale.y), self.dp.y) + self.subrange.y.lo

	def PixelToX(self, px):
		x = self.PixelToXRounded(px)
		if self.pdp.x == 0:
			rt = self.XToPixel(x)
			if rt > px:
				return x - 1
		return x

	def PixelToY(self, py):
		y = self.PixelToYRounded(py)
		if self.pdp.y == 0:
			rt = self.YToPixel(y)
			if rt > py:
				return y - 1
		return y

	def ToPDP(self, dp, scale):
		# Calculate pixel decimal places:
		#    (10 ** dp) is the minimum delta in the data
		#    scale it to get the minimum delta in pixels
		#    log10 gives the number of decimals places negatively
		#    subtrace 1 to divide by 10
		#    round to the lower negative number
		#    change the sign to get the number of decimals positively
		x = math.log10((10 ** dp) * scale)
		if x < 0:
			x -= 1
			x = -int(math.floor(x) - 0.1)
		else:
			x = 0
		return x

	def Update(self):
		x = self.ToPDP(self.dp.x, self.scale.x)
		y = self.ToPDP(self.dp.y, self.scale.y)
		self.pdp = XY(x, y) # pixel decimal places

# Switch graph splitter which divides the CPU graphs from the legend

class SwitchGraphSplitter(QSplitter):

	def __init__(self, parent=None):
		super(SwitchGraphSplitter, self).__init__(parent)

		self.first_time = False

	def resizeEvent(self, ev):
		if self.first_time:
			self.first_time = False
			sz1 = self.widget(1).view.columnWidth(0) + self.widget(1).view.columnWidth(1) + self.widget(1).view.columnWidth(2) + 2
			sz1 = sz1 + self.widget(1).view.verticalScrollBar().sizeHint().width()
			sz0 = self.size().width() - self.handleWidth() - sz1
			self.setSizes([sz0, sz1])
		elif not(self.widget(1).saved_size is None):
			sz1 = self.widget(1).saved_size
			sz0 = self.size().width() - self.handleWidth() - sz1
			self.setSizes([sz0, sz1])
		super(SwitchGraphSplitter, self).resizeEvent(ev)

# Graph widget base class

class GraphWidget(QWidget):

	graph_title_changed = Signal(object)

	def __init__(self, parent=None):
		super(GraphWidget, self).__init__(parent)

	def GraphTitleChanged(self, title):
		self.graph_title_changed.emit(title)

	def Title(self):
		return ""

# Display time in s, ms, us or ns

def ToTimeStr(val):
	val = Decimal(val)
	if val >= 1000000000:
		return "{} s".format((val / 1000000000).quantize(Decimal("0.000000001")))
	if val >= 1000000:
		return "{} ms".format((val / 1000000).quantize(Decimal("0.000001")))
	if val >= 1000:
		return "{} us".format((val / 1000).quantize(Decimal("0.001")))
	return "{} ns".format(val.quantize(Decimal("1")))

# Switch (i.e. context switch i.e. Time Chart by CPU) graph widget which contains the CPU graphs and the legend and control buttons

class SwitchGraphWidget(GraphWidget):

	def __init__(self, glb, collection, parent=None):
		super(SwitchGraphWidget, self).__init__(parent)

		self.glb = glb
		self.collection = collection

		self.back_state = []
		self.forward_state = []
		self.selection_state = (None, None)
		self.fwd_rect = None
		self.start_time = self.glb.StartTime(collection.machine_id)

		i = 0
		hregions = collection.hregions.values()
		colours = GenerateNRandomColours(len(hregions), 1013)
		region_attributes = {}
		for hregion in hregions:
			if hregion.pid == 0 and hregion.tid == 0:
				region_attributes[hregion.key] = GraphRegionAttribute(QColor(0, 0, 0))
			else:
				region_attributes[hregion.key] = GraphRegionAttribute(colours[i])
				i = i + 1

		# Default to entire range
		xsubrange = Subrange(0.0, float(collection.xrangehi - collection.xrangelo) + 1.0)
		ysubrange = Subrange(0.0, float(collection.yrangehi - collection.yrangelo) + 1.0)
		subrange = XY(xsubrange, ysubrange)

		scale = self.GetScaleForRange(subrange)

		self.attrs = GraphAttributes(scale, subrange, region_attributes, collection.dp)

		self.item = VertcalGraphSetGraphicsItem(collection, self.attrs, self, SwitchGraphGraphicsItem)

		self.scene = QGraphicsScene()
		self.scene.addItem(self.item)

		self.view = QGraphicsView(self.scene)
		self.view.centerOn(0, 0)
		self.view.setAlignment(Qt.AlignLeft | Qt.AlignTop)

		self.legend = SwitchGraphLegend(collection, region_attributes)

		self.splitter = SwitchGraphSplitter()
		self.splitter.addWidget(self.view)
		self.splitter.addWidget(self.legend)

		self.point_label = QLabel("")
		self.point_label.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)

		self.back_button = QToolButton()
		self.back_button.setIcon(self.style().standardIcon(QStyle.SP_ArrowLeft))
		self.back_button.setDisabled(True)
		self.back_button.released.connect(lambda: self.Back())

		self.forward_button = QToolButton()
		self.forward_button.setIcon(self.style().standardIcon(QStyle.SP_ArrowRight))
		self.forward_button.setDisabled(True)
		self.forward_button.released.connect(lambda: self.Forward())

		self.zoom_button = QToolButton()
		self.zoom_button.setText("Zoom")
		self.zoom_button.setDisabled(True)
		self.zoom_button.released.connect(lambda: self.Zoom())

		self.hbox = HBoxLayout(self.back_button, self.forward_button, self.zoom_button, self.point_label)

		self.vbox = VBoxLayout(self.splitter, self.hbox)

		self.setLayout(self.vbox)

	def GetScaleForRangeX(self, xsubrange):
		# Default graph 1000 pixels wide
		dflt = 1000.0
		r = xsubrange.hi - xsubrange.lo
		return dflt / r

	def GetScaleForRangeY(self, ysubrange):
		# Default graph 50 pixels high
		dflt = 50.0
		r = ysubrange.hi - ysubrange.lo
		return dflt / r

	def GetScaleForRange(self, subrange):
		# Default graph 1000 pixels wide, 50 pixels high
		xscale = self.GetScaleForRangeX(subrange.x)
		yscale = self.GetScaleForRangeY(subrange.y)
		return XY(xscale, yscale)

	def PointEvent(self, cpu, time_from, time_to, hregions):
		text = "CPU: " + str(cpu)
		time_from = time_from.quantize(Decimal(1))
		rel_time_from = time_from - self.glb.StartTime(self.collection.machine_id)
		text = text + " Time: " + str(time_from) + " (+" + ToTimeStr(rel_time_from) + ")"
		self.point_label.setText(text)
		self.legend.Highlight(hregions)

	def RightClickEvent(self, cpu, hregion_times, pos):
		if not IsSelectable(self.glb.db, "calls", "WHERE parent_id >= 0"):
			return
		menu = QMenu(self.view)
		for hregion, time in hregion_times:
			thread_at_time = (hregion.exec_comm_id, hregion.thread_id, time)
			menu_text = "Show Call Tree for {} {}:{} at {}".format(hregion.comm, hregion.pid, hregion.tid, time)
			menu.addAction(CreateAction(menu_text, "Show Call Tree", lambda a=None, args=thread_at_time: self.RightClickSelect(args), self.view))
		menu.exec_(pos)

	def RightClickSelect(self, args):
		CallTreeWindow(self.glb, self.glb.mainwindow, thread_at_time=args)

	def NoPointEvent(self):
		self.point_label.setText("")
		self.legend.Highlight({})

	def RangeEvent(self, time_from, time_to):
		time_from = time_from.quantize(Decimal(1))
		time_to = time_to.quantize(Decimal(1))
		if time_to <= time_from:
			self.point_label.setText("")
			return
		rel_time_from = time_from - self.start_time
		rel_time_to = time_to - self.start_time
		text = " Time: " + str(time_from) + " (+" + ToTimeStr(rel_time_from) + ") to: " + str(time_to) + " (+" + ToTimeStr(rel_time_to) + ")"
		text = text + " duration: " + ToTimeStr(time_to - time_from)
		self.point_label.setText(text)

	def BackState(self):
		return (self.attrs.subrange, self.attrs.scale, self.selection_state, self.fwd_rect)

	def PushBackState(self):
		state = copy.deepcopy(self.BackState())
		self.back_state.append(state)
		self.back_button.setEnabled(True)

	def PopBackState(self):
		self.attrs.subrange, self.attrs.scale, self.selection_state, self.fwd_rect = self.back_state.pop()
		self.attrs.Update()
		if not self.back_state:
			self.back_button.setDisabled(True)

	def PushForwardState(self):
		state = copy.deepcopy(self.BackState())
		self.forward_state.append(state)
		self.forward_button.setEnabled(True)

	def PopForwardState(self):
		self.attrs.subrange, self.attrs.scale, self.selection_state, self.fwd_rect = self.forward_state.pop()
		self.attrs.Update()
		if not self.forward_state:
			self.forward_button.setDisabled(True)

	def Title(self):
		time_from = self.collection.xrangelo + Decimal(self.attrs.subrange.x.lo)
		time_to = self.collection.xrangelo + Decimal(self.attrs.subrange.x.hi)
		rel_time_from = time_from - self.start_time
		rel_time_to = time_to - self.start_time
		title = "+" + ToTimeStr(rel_time_from) + " to +" + ToTimeStr(rel_time_to)
		title = title + " (" + ToTimeStr(time_to - time_from) + ")"
		return title

	def Update(self):
		selected_subrange, selection_state = self.selection_state
		self.item.SetSelection(selection_state)
		self.item.SetBracket(self.fwd_rect)
		self.zoom_button.setDisabled(selected_subrange is None)
		self.GraphTitleChanged(self.Title())
		self.item.update(self.item.boundingRect())

	def Back(self):
		if not self.back_state:
			return
		self.PushForwardState()
		self.PopBackState()
		self.Update()

	def Forward(self):
		if not self.forward_state:
			return
		self.PushBackState()
		self.PopForwardState()
		self.Update()

	def SelectEvent(self, x0, x1, selection_state):
		if selection_state is None:
			selected_subrange = None
		else:
			if x1 - x0 < 1.0:
				x1 += 1.0
			selected_subrange = Subrange(x0, x1)
		self.selection_state = (selected_subrange, selection_state)
		self.zoom_button.setDisabled(selected_subrange is None)

	def Zoom(self):
		selected_subrange, selection_state = self.selection_state
		if selected_subrange is None:
			return
		self.fwd_rect = selection_state
		self.item.SetSelection(None)
		self.PushBackState()
		self.attrs.subrange.x = selected_subrange
		self.forward_state = []
		self.forward_button.setDisabled(True)
		self.selection_state = (None, None)
		self.fwd_rect = None
		self.attrs.scale.x = self.GetScaleForRangeX(self.attrs.subrange.x)
		self.attrs.Update()
		self.Update()

# Slow initialization - perform non-GUI initialization in a separate thread and put up a modal message box while waiting

class SlowInitClass():

	def __init__(self, glb, title, init_fn):
		self.init_fn = init_fn
		self.done = False
		self.result = None

		self.msg_box = QMessageBox(glb.mainwindow)
		self.msg_box.setText("Initializing " + title + ". Please wait.")
		self.msg_box.setWindowTitle("Initializing " + title)
		self.msg_box.setWindowIcon(glb.mainwindow.style().standardIcon(QStyle.SP_MessageBoxInformation))

		self.init_thread = Thread(self.ThreadFn, glb)
		self.init_thread.done.connect(lambda: self.Done(), Qt.QueuedConnection)

		self.init_thread.start()

	def Done(self):
		self.msg_box.done(0)

	def ThreadFn(self, glb):
		conn_name = "SlowInitClass" + str(os.getpid())
		db, dbname = glb.dbref.Open(conn_name)
		self.result = self.init_fn(db)
		self.done = True
		return (True, 0)

	def Result(self):
		while not self.done:
			self.msg_box.exec_()
		self.init_thread.wait()
		return self.result

def SlowInit(glb, title, init_fn):
	init = SlowInitClass(glb, title, init_fn)
	return init.Result()

# Time chart by CPU window

class TimeChartByCPUWindow(QMdiSubWindow):

	def __init__(self, glb, parent=None):
		super(TimeChartByCPUWindow, self).__init__(parent)

		self.glb = glb
		self.machine_id = glb.HostMachineId()
		self.collection_name = "SwitchGraphDataCollection " + str(self.machine_id)

		collection = LookupModel(self.collection_name)
		if collection is None:
			collection = SlowInit(glb, "Time Chart", self.Init)

		self.widget = SwitchGraphWidget(glb, collection, self)
		self.view = self.widget

		self.base_title = "Time Chart by CPU"
		self.setWindowTitle(self.base_title + self.widget.Title())
		self.widget.graph_title_changed.connect(self.GraphTitleChanged)

		self.setWidget(self.widget)

		AddSubWindow(glb.mainwindow.mdi_area, self, self.windowTitle())

	def Init(self, db):
		return LookupCreateModel(self.collection_name, lambda : SwitchGraphDataCollection(self.glb, db, self.machine_id))

	def GraphTitleChanged(self, title):
		self.setWindowTitle(self.base_title + " : " + title)

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
				nd = pickle.dumps(0, pickle.HIGHEST_PROTOCOL)
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
		d = pickle.dumps(obj, pickle.HIGHEST_PROTOCOL)
		n = len(d)
		nd = pickle.dumps(n, pickle.HIGHEST_PROTOCOL)
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
		n = pickle.loads(self.buffer[pos : pos + glb_nsz])
		if n == 0:
			pos = 0
			n = pickle.loads(self.buffer[0 : glb_nsz])
		pos += glb_nsz
		obj = pickle.loads(self.buffer[pos : pos + n])
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
		self.bar.setLayout(self.hbox)
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

# Brance data model level two item

class BranchLevelTwoItem():

	def __init__(self, row, col, text, parent_item):
		self.row = row
		self.parent_item = parent_item
		self.data = [""] * (col + 1)
		self.data[col] = text
		self.level = 2

	def getParentItem(self):
		return self.parent_item

	def getRow(self):
		return self.row

	def childCount(self):
		return 0

	def hasChildren(self):
		return False

	def getData(self, column):
		return self.data[column]

# Brance data model level one item

class BranchLevelOneItem():

	def __init__(self, glb, row, data, parent_item):
		self.glb = glb
		self.row = row
		self.parent_item = parent_item
		self.child_count = 0
		self.child_items = []
		self.data = data[1:]
		self.dbid = data[0]
		self.level = 1
		self.query_done = False
		self.br_col = len(self.data) - 1

	def getChildItem(self, row):
		return self.child_items[row]

	def getParentItem(self):
		return self.parent_item

	def getRow(self):
		return self.row

	def Select(self):
		self.query_done = True

		if not self.glb.have_disassembler:
			return

		query = QSqlQuery(self.glb.db)

		QueryExec(query, "SELECT cpu, to_dso_id, to_symbol_id, to_sym_offset, short_name, long_name, build_id, sym_start, to_ip"
				  " FROM samples"
				  " INNER JOIN dsos ON samples.to_dso_id = dsos.id"
				  " INNER JOIN symbols ON samples.to_symbol_id = symbols.id"
				  " WHERE samples.id = " + str(self.dbid))
		if not query.next():
			return
		cpu = query.value(0)
		dso = query.value(1)
		sym = query.value(2)
		if dso == 0 or sym == 0:
			return
		off = query.value(3)
		short_name = query.value(4)
		long_name = query.value(5)
		build_id = query.value(6)
		sym_start = query.value(7)
		ip = query.value(8)

		QueryExec(query, "SELECT samples.dso_id, symbol_id, sym_offset, sym_start"
				  " FROM samples"
				  " INNER JOIN symbols ON samples.symbol_id = symbols.id"
				  " WHERE samples.id > " + str(self.dbid) + " AND cpu = " + str(cpu) +
				  " ORDER BY samples.id"
				  " LIMIT 1")
		if not query.next():
			return
		if query.value(0) != dso:
			# Cannot disassemble from one dso to another
			return
		bsym = query.value(1)
		boff = query.value(2)
		bsym_start = query.value(3)
		if bsym == 0:
			return
		tot = bsym_start + boff + 1 - sym_start - off
		if tot <= 0 or tot > 16384:
			return

		inst = self.glb.disassembler.Instruction()
		f = self.glb.FileFromNamesAndBuildId(short_name, long_name, build_id)
		if not f:
			return
		mode = 0 if Is64Bit(f) else 1
		self.glb.disassembler.SetMode(inst, mode)

		buf_sz = tot + 16
		buf = create_string_buffer(tot + 16)
		f.seek(sym_start + off)
		buf.value = f.read(buf_sz)
		buf_ptr = addressof(buf)
		i = 0
		while tot > 0:
			cnt, text = self.glb.disassembler.DisassembleOne(inst, buf_ptr, buf_sz, ip)
			if cnt:
				byte_str = tohex(ip).rjust(16)
				for k in xrange(cnt):
					byte_str += " %02x" % ord(buf[i])
					i += 1
				while k < 15:
					byte_str += "   "
					k += 1
				self.child_items.append(BranchLevelTwoItem(0, self.br_col, byte_str + " " + text, self))
				self.child_count += 1
			else:
				return
			buf_ptr += cnt
			tot -= cnt
			buf_sz -= cnt
			ip += cnt

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

# Brance data model root item

class BranchRootItem():

	def __init__(self):
		self.child_count = 0
		self.child_items = []
		self.level = 0

	def getChildItem(self, row):
		return self.child_items[row]

	def getParentItem(self):
		return None

	def getRow(self):
		return 0

	def childCount(self):
		return self.child_count

	def hasChildren(self):
		return self.child_count > 0

	def getData(self, column):
		return ""

# Calculate instructions per cycle

def CalcIPC(cyc_cnt, insn_cnt):
	if cyc_cnt and insn_cnt:
		ipc = Decimal(float(insn_cnt) / cyc_cnt)
		ipc = str(ipc.quantize(Decimal(".01"), rounding=ROUND_HALF_UP))
	else:
		ipc = "0"
	return ipc

# Branch data preparation

def BranchDataPrepBr(query, data):
	data.append(tohex(query.value(8)).rjust(16) + " " + query.value(9) + offstr(query.value(10)) +
			" (" + dsoname(query.value(11)) + ")" + " -> " +
			tohex(query.value(12)) + " " + query.value(13) + offstr(query.value(14)) +
			" (" + dsoname(query.value(15)) + ")")

def BranchDataPrepIPC(query, data):
	insn_cnt = query.value(16)
	cyc_cnt = query.value(17)
	ipc = CalcIPC(cyc_cnt, insn_cnt)
	data.append(insn_cnt)
	data.append(cyc_cnt)
	data.append(ipc)

def BranchDataPrep(query):
	data = []
	for i in xrange(0, 8):
		data.append(query.value(i))
	BranchDataPrepBr(query, data)
	return data

def BranchDataPrepWA(query):
	data = []
	data.append(query.value(0))
	# Workaround pyside failing to handle large integers (i.e. time) in python3 by converting to a string
	data.append("{:>19}".format(query.value(1)))
	for i in xrange(2, 8):
		data.append(query.value(i))
	BranchDataPrepBr(query, data)
	return data

def BranchDataWithIPCPrep(query):
	data = []
	for i in xrange(0, 8):
		data.append(query.value(i))
	BranchDataPrepIPC(query, data)
	BranchDataPrepBr(query, data)
	return data

def BranchDataWithIPCPrepWA(query):
	data = []
	data.append(query.value(0))
	# Workaround pyside failing to handle large integers (i.e. time) in python3 by converting to a string
	data.append("{:>19}".format(query.value(1)))
	for i in xrange(2, 8):
		data.append(query.value(i))
	BranchDataPrepIPC(query, data)
	BranchDataPrepBr(query, data)
	return data

# Branch data model

class BranchModel(TreeModel):

	progress = Signal(object)

	def __init__(self, glb, event_id, where_clause, parent=None):
		super(BranchModel, self).__init__(glb, None, parent)
		self.event_id = event_id
		self.more = True
		self.populated = 0
		self.have_ipc = IsSelectable(glb.db, "samples", columns = "insn_count, cyc_count")
		if self.have_ipc:
			select_ipc = ", insn_count, cyc_count"
			prep_fn = BranchDataWithIPCPrep
			prep_wa_fn = BranchDataWithIPCPrepWA
		else:
			select_ipc = ""
			prep_fn = BranchDataPrep
			prep_wa_fn = BranchDataPrepWA
		sql = ("SELECT samples.id, time, cpu, comm, pid, tid, branch_types.name,"
			" CASE WHEN in_tx = '0' THEN 'No' ELSE 'Yes' END,"
			" ip, symbols.name, sym_offset, dsos.short_name,"
			" to_ip, to_symbols.name, to_sym_offset, to_dsos.short_name"
			+ select_ipc +
			" FROM samples"
			" INNER JOIN comms ON comm_id = comms.id"
			" INNER JOIN threads ON thread_id = threads.id"
			" INNER JOIN branch_types ON branch_type = branch_types.id"
			" INNER JOIN symbols ON symbol_id = symbols.id"
			" INNER JOIN symbols to_symbols ON to_symbol_id = to_symbols.id"
			" INNER JOIN dsos ON samples.dso_id = dsos.id"
			" INNER JOIN dsos AS to_dsos ON samples.to_dso_id = to_dsos.id"
			" WHERE samples.id > $$last_id$$" + where_clause +
			" AND evsel_id = " + str(self.event_id) +
			" ORDER BY samples.id"
			" LIMIT " + str(glb_chunk_sz))
		if pyside_version_1 and sys.version_info[0] == 3:
			prep = prep_fn
		else:
			prep = prep_wa_fn
		self.fetcher = SQLFetcher(glb, sql, prep, self.AddSample)
		self.fetcher.done.connect(self.Update)
		self.fetcher.Fetch(glb_chunk_sz)

	def GetRoot(self):
		return BranchRootItem()

	def columnCount(self, parent=None):
		if self.have_ipc:
			return 11
		else:
			return 8

	def columnHeader(self, column):
		if self.have_ipc:
			return ("Time", "CPU", "Command", "PID", "TID", "Branch Type", "In Tx", "Insn Cnt", "Cyc Cnt", "IPC", "Branch")[column]
		else:
			return ("Time", "CPU", "Command", "PID", "TID", "Branch Type", "In Tx", "Branch")[column]

	def columnFont(self, column):
		if self.have_ipc:
			br_col = 10
		else:
			br_col = 7
		if column != br_col:
			return None
		return QFont("Monospace")

	def DisplayData(self, item, index):
		if item.level == 1:
			self.FetchIfNeeded(item.row)
		return item.getData(index.column())

	def AddSample(self, data):
		child = BranchLevelOneItem(self.glb, self.populated, data, self.root)
		self.root.child_items.append(child)
		self.populated += 1

	def Update(self, fetched):
		if not fetched:
			self.more = False
			self.progress.emit(0)
		child_count = self.root.child_count
		count = self.populated - child_count
		if count > 0:
			parent = QModelIndex()
			self.beginInsertRows(parent, child_count, child_count + count - 1)
			self.insertRows(child_count, count, parent)
			self.root.child_count += count
			self.endInsertRows()
			self.progress.emit(self.root.child_count)

	def FetchMoreRecords(self, count):
		current = self.root.child_count
		if self.more:
			self.fetcher.Fetch(count)
		else:
			self.progress.emit(0)
		return current

	def HasMoreRecords(self):
		return self.more

# Report Variables

class ReportVars():

	def __init__(self, name = "", where_clause = "", limit = ""):
		self.name = name
		self.where_clause = where_clause
		self.limit = limit

	def UniqueId(self):
		return str(self.where_clause + ";" + self.limit)

# Branch window

class BranchWindow(QMdiSubWindow):

	def __init__(self, glb, event_id, report_vars, parent=None):
		super(BranchWindow, self).__init__(parent)

		model_name = "Branch Events " + str(event_id) +  " " + report_vars.UniqueId()

		self.model = LookupCreateModel(model_name, lambda: BranchModel(glb, event_id, report_vars.where_clause))

		self.view = QTreeView()
		self.view.setUniformRowHeights(True)
		self.view.setSelectionMode(QAbstractItemView.ContiguousSelection)
		self.view.CopyCellsToClipboard = CopyTreeCellsToClipboard
		self.view.setModel(self.model)

		self.ResizeColumnsToContents()

		self.context_menu = TreeContextMenu(self.view)

		self.find_bar = FindBar(self, self, True)

		self.finder = ChildDataItemFinder(self.model.root)

		self.fetch_bar = FetchMoreRecordsBar(self.model, self)

		self.vbox = VBox(self.view, self.find_bar.Widget(), self.fetch_bar.Widget())

		self.setWidget(self.vbox.Widget())

		AddSubWindow(glb.mainwindow.mdi_area, self, report_vars.name + " Branch Events")

	def ResizeColumnToContents(self, column, n):
		# Using the view's resizeColumnToContents() here is extrememly slow
		# so implement a crude alternative
		mm = "MM" if column else "MMMM"
		font = self.view.font()
		metrics = QFontMetrics(font)
		max = 0
		for row in xrange(n):
			val = self.model.root.child_items[row].data[column]
			len = metrics.width(str(val) + mm)
			max = len if len > max else max
		val = self.model.columnHeader(column)
		len = metrics.width(str(val) + mm)
		max = len if len > max else max
		self.view.setColumnWidth(column, max)

	def ResizeColumnsToContents(self):
		n = min(self.model.root.child_count, 100)
		if n < 1:
			# No data yet, so connect a signal to notify when there is
			self.model.rowsInserted.connect(self.UpdateColumnWidths)
			return
		columns = self.model.columnCount()
		for i in xrange(columns):
			self.ResizeColumnToContents(i, n)

	def UpdateColumnWidths(self, *x):
		# This only needs to be done once, so disconnect the signal now
		self.model.rowsInserted.disconnect(self.UpdateColumnWidths)
		self.ResizeColumnsToContents()

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

# Line edit data item

class LineEditDataItem(object):

	def __init__(self, glb, label, placeholder_text, parent, id = "", default = ""):
		self.glb = glb
		self.label = label
		self.placeholder_text = placeholder_text
		self.parent = parent
		self.id = id

		self.value = default

		self.widget = QLineEdit(default)
		self.widget.editingFinished.connect(self.Validate)
		self.widget.textChanged.connect(self.Invalidate)
		self.red = False
		self.error = ""
		self.validated = True

		if placeholder_text:
			self.widget.setPlaceholderText(placeholder_text)

	def TurnTextRed(self):
		if not self.red:
			palette = QPalette()
			palette.setColor(QPalette.Text,Qt.red)
			self.widget.setPalette(palette)
			self.red = True

	def TurnTextNormal(self):
		if self.red:
			palette = QPalette()
			self.widget.setPalette(palette)
			self.red = False

	def InvalidValue(self, value):
		self.value = ""
		self.TurnTextRed()
		self.error = self.label + " invalid value '" + value + "'"
		self.parent.ShowMessage(self.error)

	def Invalidate(self):
		self.validated = False

	def DoValidate(self, input_string):
		self.value = input_string.strip()

	def Validate(self):
		self.validated = True
		self.error = ""
		self.TurnTextNormal()
		self.parent.ClearMessage()
		input_string = self.widget.text()
		if not len(input_string.strip()):
			self.value = ""
			return
		self.DoValidate(input_string)

	def IsValid(self):
		if not self.validated:
			self.Validate()
		if len(self.error):
			self.parent.ShowMessage(self.error)
			return False
		return True

	def IsNumber(self, value):
		try:
			x = int(value)
		except:
			x = 0
		return str(x) == value

# Non-negative integer ranges dialog data item

class NonNegativeIntegerRangesDataItem(LineEditDataItem):

	def __init__(self, glb, label, placeholder_text, column_name, parent):
		super(NonNegativeIntegerRangesDataItem, self).__init__(glb, label, placeholder_text, parent)

		self.column_name = column_name

	def DoValidate(self, input_string):
		singles = []
		ranges = []
		for value in [x.strip() for x in input_string.split(",")]:
			if "-" in value:
				vrange = value.split("-")
				if len(vrange) != 2 or not self.IsNumber(vrange[0]) or not self.IsNumber(vrange[1]):
					return self.InvalidValue(value)
				ranges.append(vrange)
			else:
				if not self.IsNumber(value):
					return self.InvalidValue(value)
				singles.append(value)
		ranges = [("(" + self.column_name + " >= " + r[0] + " AND " + self.column_name + " <= " + r[1] + ")") for r in ranges]
		if len(singles):
			ranges.append(self.column_name + " IN (" + ",".join(singles) + ")")
		self.value = " OR ".join(ranges)

# Positive integer dialog data item

class PositiveIntegerDataItem(LineEditDataItem):

	def __init__(self, glb, label, placeholder_text, parent, id = "", default = ""):
		super(PositiveIntegerDataItem, self).__init__(glb, label, placeholder_text, parent, id, default)

	def DoValidate(self, input_string):
		if not self.IsNumber(input_string.strip()):
			return self.InvalidValue(input_string)
		value = int(input_string.strip())
		if value <= 0:
			return self.InvalidValue(input_string)
		self.value = str(value)

# Dialog data item converted and validated using a SQL table

class SQLTableDataItem(LineEditDataItem):

	def __init__(self, glb, label, placeholder_text, table_name, match_column, column_name1, column_name2, parent):
		super(SQLTableDataItem, self).__init__(glb, label, placeholder_text, parent)

		self.table_name = table_name
		self.match_column = match_column
		self.column_name1 = column_name1
		self.column_name2 = column_name2

	def ValueToIds(self, value):
		ids = []
		query = QSqlQuery(self.glb.db)
		stmt = "SELECT id FROM " + self.table_name + " WHERE " + self.match_column + " = '" + value + "'"
		ret = query.exec_(stmt)
		if ret:
			while query.next():
				ids.append(str(query.value(0)))
		return ids

	def DoValidate(self, input_string):
		all_ids = []
		for value in [x.strip() for x in input_string.split(",")]:
			ids = self.ValueToIds(value)
			if len(ids):
				all_ids.extend(ids)
			else:
				return self.InvalidValue(value)
		self.value = self.column_name1 + " IN (" + ",".join(all_ids) + ")"
		if self.column_name2:
			self.value = "( " + self.value + " OR " + self.column_name2 + " IN (" + ",".join(all_ids) + ") )"

# Sample time ranges dialog data item converted and validated using 'samples' SQL table

class SampleTimeRangesDataItem(LineEditDataItem):

	def __init__(self, glb, label, placeholder_text, column_name, parent):
		self.column_name = column_name

		self.last_id = 0
		self.first_time = 0
		self.last_time = 2 ** 64

		query = QSqlQuery(glb.db)
		QueryExec(query, "SELECT id, time FROM samples ORDER BY id DESC LIMIT 1")
		if query.next():
			self.last_id = int(query.value(0))
		self.first_time = int(glb.HostStartTime())
		self.last_time = int(glb.HostFinishTime())
		if placeholder_text:
			placeholder_text += ", between " + str(self.first_time) + " and " + str(self.last_time)

		super(SampleTimeRangesDataItem, self).__init__(glb, label, placeholder_text, parent)

	def IdBetween(self, query, lower_id, higher_id, order):
		QueryExec(query, "SELECT id FROM samples WHERE id > " + str(lower_id) + " AND id < " + str(higher_id) + " ORDER BY id " + order + " LIMIT 1")
		if query.next():
			return True, int(query.value(0))
		else:
			return False, 0

	def BinarySearchTime(self, lower_id, higher_id, target_time, get_floor):
		query = QSqlQuery(self.glb.db)
		while True:
			next_id = int((lower_id + higher_id) / 2)
			QueryExec(query, "SELECT time FROM samples WHERE id = " + str(next_id))
			if not query.next():
				ok, dbid = self.IdBetween(query, lower_id, next_id, "DESC")
				if not ok:
					ok, dbid = self.IdBetween(query, next_id, higher_id, "")
					if not ok:
						return str(higher_id)
				next_id = dbid
				QueryExec(query, "SELECT time FROM samples WHERE id = " + str(next_id))
			next_time = int(query.value(0))
			if get_floor:
				if target_time > next_time:
					lower_id = next_id
				else:
					higher_id = next_id
				if higher_id <= lower_id + 1:
					return str(higher_id)
			else:
				if target_time >= next_time:
					lower_id = next_id
				else:
					higher_id = next_id
				if higher_id <= lower_id + 1:
					return str(lower_id)

	def ConvertRelativeTime(self, val):
		mult = 1
		suffix = val[-2:]
		if suffix == "ms":
			mult = 1000000
		elif suffix == "us":
			mult = 1000
		elif suffix == "ns":
			mult = 1
		else:
			return val
		val = val[:-2].strip()
		if not self.IsNumber(val):
			return val
		val = int(val) * mult
		if val >= 0:
			val += self.first_time
		else:
			val += self.last_time
		return str(val)

	def ConvertTimeRange(self, vrange):
		if vrange[0] == "":
			vrange[0] = str(self.first_time)
		if vrange[1] == "":
			vrange[1] = str(self.last_time)
		vrange[0] = self.ConvertRelativeTime(vrange[0])
		vrange[1] = self.ConvertRelativeTime(vrange[1])
		if not self.IsNumber(vrange[0]) or not self.IsNumber(vrange[1]):
			return False
		beg_range = max(int(vrange[0]), self.first_time)
		end_range = min(int(vrange[1]), self.last_time)
		if beg_range > self.last_time or end_range < self.first_time:
			return False
		vrange[0] = self.BinarySearchTime(0, self.last_id, beg_range, True)
		vrange[1] = self.BinarySearchTime(1, self.last_id + 1, end_range, False)
		return True

	def AddTimeRange(self, value, ranges):
		n = value.count("-")
		if n == 1:
			pass
		elif n == 2:
			if value.split("-")[1].strip() == "":
				n = 1
		elif n == 3:
			n = 2
		else:
			return False
		pos = findnth(value, "-", n)
		vrange = [value[:pos].strip() ,value[pos+1:].strip()]
		if self.ConvertTimeRange(vrange):
			ranges.append(vrange)
			return True
		return False

	def DoValidate(self, input_string):
		ranges = []
		for value in [x.strip() for x in input_string.split(",")]:
			if not self.AddTimeRange(value, ranges):
				return self.InvalidValue(value)
		ranges = [("(" + self.column_name + " >= " + r[0] + " AND " + self.column_name + " <= " + r[1] + ")") for r in ranges]
		self.value = " OR ".join(ranges)

# Report Dialog Base

class ReportDialogBase(QDialog):

	def __init__(self, glb, title, items, partial, parent=None):
		super(ReportDialogBase, self).__init__(parent)

		self.glb = glb

		self.report_vars = ReportVars()

		self.setWindowTitle(title)
		self.setMinimumWidth(600)

		self.data_items = [x(glb, self) for x in items]

		self.partial = partial

		self.grid = QGridLayout()

		for row in xrange(len(self.data_items)):
			self.grid.addWidget(QLabel(self.data_items[row].label), row, 0)
			self.grid.addWidget(self.data_items[row].widget, row, 1)

		self.status = QLabel()

		self.ok_button = QPushButton("Ok", self)
		self.ok_button.setDefault(True)
		self.ok_button.released.connect(self.Ok)
		self.ok_button.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

		self.cancel_button = QPushButton("Cancel", self)
		self.cancel_button.released.connect(self.reject)
		self.cancel_button.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

		self.hbox = QHBoxLayout()
		#self.hbox.addStretch()
		self.hbox.addWidget(self.status)
		self.hbox.addWidget(self.ok_button)
		self.hbox.addWidget(self.cancel_button)

		self.vbox = QVBoxLayout()
		self.vbox.addLayout(self.grid)
		self.vbox.addLayout(self.hbox)

		self.setLayout(self.vbox)

	def Ok(self):
		vars = self.report_vars
		for d in self.data_items:
			if d.id == "REPORTNAME":
				vars.name = d.value
		if not vars.name:
			self.ShowMessage("Report name is required")
			return
		for d in self.data_items:
			if not d.IsValid():
				return
		for d in self.data_items[1:]:
			if d.id == "LIMIT":
				vars.limit = d.value
			elif len(d.value):
				if len(vars.where_clause):
					vars.where_clause += " AND "
				vars.where_clause += d.value
		if len(vars.where_clause):
			if self.partial:
				vars.where_clause = " AND ( " + vars.where_clause + " ) "
			else:
				vars.where_clause = " WHERE " + vars.where_clause + " "
		self.accept()

	def ShowMessage(self, msg):
		self.status.setText("<font color=#FF0000>" + msg)

	def ClearMessage(self):
		self.status.setText("")

# Selected branch report creation dialog

class SelectedBranchDialog(ReportDialogBase):

	def __init__(self, glb, parent=None):
		title = "Selected Branches"
		items = (lambda g, p: LineEditDataItem(g, "Report name:", "Enter a name to appear in the window title bar", p, "REPORTNAME"),
			 lambda g, p: SampleTimeRangesDataItem(g, "Time ranges:", "Enter time ranges", "samples.id", p),
			 lambda g, p: NonNegativeIntegerRangesDataItem(g, "CPUs:", "Enter CPUs or ranges e.g. 0,5-6", "cpu", p),
			 lambda g, p: SQLTableDataItem(g, "Commands:", "Only branches with these commands will be included", "comms", "comm", "comm_id", "", p),
			 lambda g, p: SQLTableDataItem(g, "PIDs:", "Only branches with these process IDs will be included", "threads", "pid", "thread_id", "", p),
			 lambda g, p: SQLTableDataItem(g, "TIDs:", "Only branches with these thread IDs will be included", "threads", "tid", "thread_id", "", p),
			 lambda g, p: SQLTableDataItem(g, "DSOs:", "Only branches with these DSOs will be included", "dsos", "short_name", "samples.dso_id", "to_dso_id", p),
			 lambda g, p: SQLTableDataItem(g, "Symbols:", "Only branches with these symbols will be included", "symbols", "name", "symbol_id", "to_symbol_id", p),
			 lambda g, p: LineEditDataItem(g, "Raw SQL clause: ", "Enter a raw SQL WHERE clause", p))
		super(SelectedBranchDialog, self).__init__(glb, title, items, True, parent)

# Event list

def GetEventList(db):
	events = []
	query = QSqlQuery(db)
	QueryExec(query, "SELECT name FROM selected_events WHERE id > 0 ORDER BY id")
	while query.next():
		events.append(query.value(0))
	return events

# Is a table selectable

def IsSelectable(db, table, sql = "", columns = "*"):
	query = QSqlQuery(db)
	try:
		QueryExec(query, "SELECT " + columns + " FROM " + table + " " + sql + " LIMIT 1")
	except:
		return False
	return True

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

	def __init__(self, glb, sql, column_headers, parent=None):
		super(SQLTableModel, self).__init__(parent)
		self.glb = glb
		self.more = True
		self.populated = 0
		self.column_headers = column_headers
		self.fetcher = SQLFetcher(glb, sql, lambda x, y=len(column_headers): self.SQLTableDataPrep(x, y), self.AddSample)
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

	def columnCount(self, parent=None):
		return len(self.column_headers)

	def columnHeader(self, column):
		return self.column_headers[column]

	def SQLTableDataPrep(self, query, count):
		data = []
		for i in xrange(count):
			data.append(query.value(i))
		return data

# SQL automatic table data model

class SQLAutoTableModel(SQLTableModel):

	def __init__(self, glb, table_name, parent=None):
		sql = "SELECT * FROM " + table_name + " WHERE id > $$last_id$$ ORDER BY id LIMIT " + str(glb_chunk_sz)
		if table_name == "comm_threads_view":
			# For now, comm_threads_view has no id column
			sql = "SELECT * FROM " + table_name + " WHERE comm_id > $$last_id$$ ORDER BY comm_id LIMIT " + str(glb_chunk_sz)
		column_headers = []
		query = QSqlQuery(glb.db)
		if glb.dbref.is_sqlite3:
			QueryExec(query, "PRAGMA table_info(" + table_name + ")")
			while query.next():
				column_headers.append(query.value(1))
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
				column_headers.append(query.value(0))
		if pyside_version_1 and sys.version_info[0] == 3:
			if table_name == "samples_view":
				self.SQLTableDataPrep = self.samples_view_DataPrep
			if table_name == "samples":
				self.SQLTableDataPrep = self.samples_DataPrep
		super(SQLAutoTableModel, self).__init__(glb, sql, column_headers, parent)

	def samples_view_DataPrep(self, query, count):
		data = []
		data.append(query.value(0))
		# Workaround pyside failing to handle large integers (i.e. time) in python3 by converting to a string
		data.append("{:>19}".format(query.value(1)))
		for i in xrange(2, count):
			data.append(query.value(i))
		return data

	def samples_DataPrep(self, query, count):
		data = []
		for i in xrange(9):
			data.append(query.value(i))
		# Workaround pyside failing to handle large integers (i.e. time) in python3 by converting to a string
		data.append("{:>19}".format(query.value(9)))
		for i in xrange(10, count):
			data.append(query.value(i))
		return data

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

# Convert value to CSV

def ToCSValue(val):
	if '"' in val:
		val = val.replace('"', '""')
	if "," in val or '"' in val:
		val = '"' + val + '"'
	return val

# Key to sort table model indexes by row / column, assuming fewer than 1000 columns

glb_max_cols = 1000

def RowColumnKey(a):
	return a.row() * glb_max_cols + a.column()

# Copy selected table cells to clipboard

def CopyTableCellsToClipboard(view, as_csv=False, with_hdr=False):
	indexes = sorted(view.selectedIndexes(), key=RowColumnKey)
	idx_cnt = len(indexes)
	if not idx_cnt:
		return
	if idx_cnt == 1:
		with_hdr=False
	min_row = indexes[0].row()
	max_row = indexes[0].row()
	min_col = indexes[0].column()
	max_col = indexes[0].column()
	for i in indexes:
		min_row = min(min_row, i.row())
		max_row = max(max_row, i.row())
		min_col = min(min_col, i.column())
		max_col = max(max_col, i.column())
	if max_col > glb_max_cols:
		raise RuntimeError("glb_max_cols is too low")
	max_width = [0] * (1 + max_col - min_col)
	for i in indexes:
		c = i.column() - min_col
		max_width[c] = max(max_width[c], len(str(i.data())))
	text = ""
	pad = ""
	sep = ""
	if with_hdr:
		model = indexes[0].model()
		for col in range(min_col, max_col + 1):
			val = model.headerData(col, Qt.Horizontal)
			if as_csv:
				text += sep + ToCSValue(val)
				sep = ","
			else:
				c = col - min_col
				max_width[c] = max(max_width[c], len(val))
				width = max_width[c]
				align = model.headerData(col, Qt.Horizontal, Qt.TextAlignmentRole)
				if align & Qt.AlignRight:
					val = val.rjust(width)
				text += pad + sep + val
				pad = " " * (width - len(val))
				sep = "  "
		text += "\n"
		pad = ""
		sep = ""
	last_row = min_row
	for i in indexes:
		if i.row() > last_row:
			last_row = i.row()
			text += "\n"
			pad = ""
			sep = ""
		if as_csv:
			text += sep + ToCSValue(str(i.data()))
			sep = ","
		else:
			width = max_width[i.column() - min_col]
			if i.data(Qt.TextAlignmentRole) & Qt.AlignRight:
				val = str(i.data()).rjust(width)
			else:
				val = str(i.data())
			text += pad + sep + val
			pad = " " * (width - len(val))
			sep = "  "
	QApplication.clipboard().setText(text)

def CopyTreeCellsToClipboard(view, as_csv=False, with_hdr=False):
	indexes = view.selectedIndexes()
	if not len(indexes):
		return

	selection = view.selectionModel()

	first = None
	for i in indexes:
		above = view.indexAbove(i)
		if not selection.isSelected(above):
			first = i
			break

	if first is None:
		raise RuntimeError("CopyTreeCellsToClipboard internal error")

	model = first.model()
	row_cnt = 0
	col_cnt = model.columnCount(first)
	max_width = [0] * col_cnt

	indent_sz = 2
	indent_str = " " * indent_sz

	expanded_mark_sz = 2
	if sys.version_info[0] == 3:
		expanded_mark = "\u25BC "
		not_expanded_mark = "\u25B6 "
	else:
		expanded_mark = unicode(chr(0xE2) + chr(0x96) + chr(0xBC) + " ", "utf-8")
		not_expanded_mark =  unicode(chr(0xE2) + chr(0x96) + chr(0xB6) + " ", "utf-8")
	leaf_mark = "  "

	if not as_csv:
		pos = first
		while True:
			row_cnt += 1
			row = pos.row()
			for c in range(col_cnt):
				i = pos.sibling(row, c)
				if c:
					n = len(str(i.data()))
				else:
					n = len(str(i.data()).strip())
					n += (i.internalPointer().level - 1) * indent_sz
					n += expanded_mark_sz
				max_width[c] = max(max_width[c], n)
			pos = view.indexBelow(pos)
			if not selection.isSelected(pos):
				break

	text = ""
	pad = ""
	sep = ""
	if with_hdr:
		for c in range(col_cnt):
			val = model.headerData(c, Qt.Horizontal, Qt.DisplayRole).strip()
			if as_csv:
				text += sep + ToCSValue(val)
				sep = ","
			else:
				max_width[c] = max(max_width[c], len(val))
				width = max_width[c]
				align = model.headerData(c, Qt.Horizontal, Qt.TextAlignmentRole)
				if align & Qt.AlignRight:
					val = val.rjust(width)
				text += pad + sep + val
				pad = " " * (width - len(val))
				sep = "   "
		text += "\n"
		pad = ""
		sep = ""

	pos = first
	while True:
		row = pos.row()
		for c in range(col_cnt):
			i = pos.sibling(row, c)
			val = str(i.data())
			if not c:
				if model.hasChildren(i):
					if view.isExpanded(i):
						mark = expanded_mark
					else:
						mark = not_expanded_mark
				else:
					mark = leaf_mark
				val = indent_str * (i.internalPointer().level - 1) + mark + val.strip()
			if as_csv:
				text += sep + ToCSValue(val)
				sep = ","
			else:
				width = max_width[c]
				if c and i.data(Qt.TextAlignmentRole) & Qt.AlignRight:
					val = val.rjust(width)
				text += pad + sep + val
				pad = " " * (width - len(val))
				sep = "   "
		pos = view.indexBelow(pos)
		if not selection.isSelected(pos):
			break
		text = text.rstrip() + "\n"
		pad = ""
		sep = ""

	QApplication.clipboard().setText(text)

def CopyCellsToClipboard(view, as_csv=False, with_hdr=False):
	view.CopyCellsToClipboard(view, as_csv, with_hdr)

def CopyCellsToClipboardHdr(view):
	CopyCellsToClipboard(view, False, True)

def CopyCellsToClipboardCSV(view):
	CopyCellsToClipboard(view, True, True)

# Context menu

class ContextMenu(object):

	def __init__(self, view):
		self.view = view
		self.view.setContextMenuPolicy(Qt.CustomContextMenu)
		self.view.customContextMenuRequested.connect(self.ShowContextMenu)

	def ShowContextMenu(self, pos):
		menu = QMenu(self.view)
		self.AddActions(menu)
		menu.exec_(self.view.mapToGlobal(pos))

	def AddCopy(self, menu):
		menu.addAction(CreateAction("&Copy selection", "Copy to clipboard", lambda: CopyCellsToClipboardHdr(self.view), self.view))
		menu.addAction(CreateAction("Copy selection as CS&V", "Copy to clipboard as CSV", lambda: CopyCellsToClipboardCSV(self.view), self.view))

	def AddActions(self, menu):
		self.AddCopy(menu)

class TreeContextMenu(ContextMenu):

	def __init__(self, view):
		super(TreeContextMenu, self).__init__(view)

	def AddActions(self, menu):
		i = self.view.currentIndex()
		text = str(i.data()).strip()
		if len(text):
			menu.addAction(CreateAction('Copy "' + text + '"', "Copy to clipboard", lambda: QApplication.clipboard().setText(text), self.view))
		self.AddCopy(menu)

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
		self.view.setSelectionMode(QAbstractItemView.ContiguousSelection)
		self.view.CopyCellsToClipboard = CopyTableCellsToClipboard

		self.ResizeColumnsToContents()

		self.context_menu = ContextMenu(self.view)

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
			self.view.setCurrentIndex(self.model.mapFromSource(self.data_model.index(row, 0, QModelIndex())))
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

# Top Calls data model

class TopCallsModel(SQLTableModel):

	def __init__(self, glb, report_vars, parent=None):
		text = ""
		if not glb.dbref.is_sqlite3:
			text = "::text"
		limit = ""
		if len(report_vars.limit):
			limit = " LIMIT " + report_vars.limit
		sql = ("SELECT comm, pid, tid, name,"
			" CASE"
			" WHEN (short_name = '[kernel.kallsyms]') THEN '[kernel]'" + text +
			" ELSE short_name"
			" END AS dso,"
			" call_time, return_time, (return_time - call_time) AS elapsed_time, branch_count, "
			" CASE"
			" WHEN (calls.flags = 1) THEN 'no call'" + text +
			" WHEN (calls.flags = 2) THEN 'no return'" + text +
			" WHEN (calls.flags = 3) THEN 'no call/return'" + text +
			" ELSE ''" + text +
			" END AS flags"
			" FROM calls"
			" INNER JOIN call_paths ON calls.call_path_id = call_paths.id"
			" INNER JOIN symbols ON call_paths.symbol_id = symbols.id"
			" INNER JOIN dsos ON symbols.dso_id = dsos.id"
			" INNER JOIN comms ON calls.comm_id = comms.id"
			" INNER JOIN threads ON calls.thread_id = threads.id" +
			report_vars.where_clause +
			" ORDER BY elapsed_time DESC" +
			limit
			)
		column_headers = ("Command", "PID", "TID", "Symbol", "Object", "Call Time", "Return Time", "Elapsed Time (ns)", "Branch Count", "Flags")
		self.alignment = (Qt.AlignLeft, Qt.AlignLeft, Qt.AlignLeft, Qt.AlignLeft, Qt.AlignLeft, Qt.AlignLeft, Qt.AlignLeft, Qt.AlignRight, Qt.AlignRight, Qt.AlignLeft)
		super(TopCallsModel, self).__init__(glb, sql, column_headers, parent)

	def columnAlignment(self, column):
		return self.alignment[column]

# Top Calls report creation dialog

class TopCallsDialog(ReportDialogBase):

	def __init__(self, glb, parent=None):
		title = "Top Calls by Elapsed Time"
		items = (lambda g, p: LineEditDataItem(g, "Report name:", "Enter a name to appear in the window title bar", p, "REPORTNAME"),
			 lambda g, p: SQLTableDataItem(g, "Commands:", "Only calls with these commands will be included", "comms", "comm", "comm_id", "", p),
			 lambda g, p: SQLTableDataItem(g, "PIDs:", "Only calls with these process IDs will be included", "threads", "pid", "thread_id", "", p),
			 lambda g, p: SQLTableDataItem(g, "TIDs:", "Only calls with these thread IDs will be included", "threads", "tid", "thread_id", "", p),
			 lambda g, p: SQLTableDataItem(g, "DSOs:", "Only calls with these DSOs will be included", "dsos", "short_name", "dso_id", "", p),
			 lambda g, p: SQLTableDataItem(g, "Symbols:", "Only calls with these symbols will be included", "symbols", "name", "symbol_id", "", p),
			 lambda g, p: LineEditDataItem(g, "Raw SQL clause: ", "Enter a raw SQL WHERE clause", p),
			 lambda g, p: PositiveIntegerDataItem(g, "Record limit:", "Limit selection to this number of records", p, "LIMIT", "100"))
		super(TopCallsDialog, self).__init__(glb, title, items, False, parent)

# Top Calls window

class TopCallsWindow(QMdiSubWindow, ResizeColumnsToContentsBase):

	def __init__(self, glb, report_vars, parent=None):
		super(TopCallsWindow, self).__init__(parent)

		self.data_model = LookupCreateModel("Top Calls " + report_vars.UniqueId(), lambda: TopCallsModel(glb, report_vars))
		self.model = self.data_model

		self.view = QTableView()
		self.view.setModel(self.model)
		self.view.setEditTriggers(QAbstractItemView.NoEditTriggers)
		self.view.verticalHeader().setVisible(False)
		self.view.setSelectionMode(QAbstractItemView.ContiguousSelection)
		self.view.CopyCellsToClipboard = CopyTableCellsToClipboard

		self.context_menu = ContextMenu(self.view)

		self.ResizeColumnsToContents()

		self.find_bar = FindBar(self, self, True)

		self.finder = ChildDataItemFinder(self.model)

		self.fetch_bar = FetchMoreRecordsBar(self.data_model, self)

		self.vbox = VBox(self.view, self.find_bar.Widget(), self.fetch_bar.Widget())

		self.setWidget(self.vbox.Widget())

		AddSubWindow(glb.mainwindow.mdi_area, self, report_vars.name)

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
			action.triggered.connect(lambda a=None,x=nr: self.setActiveSubWindow(x))
			self.window_menu.addAction(action)
			nr += 1

	def setActiveSubWindow(self, nr):
		self.mdi_area.setActiveSubWindow(self.mdi_area.subWindowList()[nr - 1])

# Help text

glb_help_text = """
<h1>Contents</h1>
<style>
p.c1 {
    text-indent: 40px;
}
p.c2 {
    text-indent: 80px;
}
}
</style>
<p class=c1><a href=#reports>1. Reports</a></p>
<p class=c2><a href=#callgraph>1.1 Context-Sensitive Call Graph</a></p>
<p class=c2><a href=#calltree>1.2 Call Tree</a></p>
<p class=c2><a href=#allbranches>1.3 All branches</a></p>
<p class=c2><a href=#selectedbranches>1.4 Selected branches</a></p>
<p class=c2><a href=#topcallsbyelapsedtime>1.5 Top calls by elapsed time</a></p>
<p class=c1><a href=#charts>2. Charts</a></p>
<p class=c2><a href=#timechartbycpu>2.1 Time chart by CPU</a></p>
<p class=c1><a href=#tables>3. Tables</a></p>
<h1 id=reports>1. Reports</h1>
<h2 id=callgraph>1.1 Context-Sensitive Call Graph</h2>
The result is a GUI window with a tree representing a context-sensitive
call-graph. Expanding a couple of levels of the tree and adjusting column
widths to suit will display something like:
<pre>
                                         Call Graph: pt_example
Call Path                          Object      Count   Time(ns)  Time(%)  Branch Count   Branch Count(%)
v- ls
    v- 2638:2638
        v- _start                  ld-2.19.so    1     10074071   100.0         211135            100.0
          |- unknown               unknown       1        13198     0.1              1              0.0
          >- _dl_start             ld-2.19.so    1      1400980    13.9          19637              9.3
          >- _d_linit_internal     ld-2.19.so    1       448152     4.4          11094              5.3
          v-__libc_start_main@plt  ls            1      8211741    81.5         180397             85.4
             >- _dl_fixup          ld-2.19.so    1         7607     0.1            108              0.1
             >- __cxa_atexit       libc-2.19.so  1        11737     0.1             10              0.0
             >- __libc_csu_init    ls            1        10354     0.1             10              0.0
             |- _setjmp            libc-2.19.so  1            0     0.0              4              0.0
             v- main               ls            1      8182043    99.6         180254             99.9
</pre>
<h3>Points to note:</h3>
<ul>
<li>The top level is a command name (comm)</li>
<li>The next level is a thread (pid:tid)</li>
<li>Subsequent levels are functions</li>
<li>'Count' is the number of calls</li>
<li>'Time' is the elapsed time until the function returns</li>
<li>Percentages are relative to the level above</li>
<li>'Branch Count' is the total number of branches for that function and all functions that it calls
</ul>
<h3>Find</h3>
Ctrl-F displays a Find bar which finds function names by either an exact match or a pattern match.
The pattern matching symbols are ? for any character and * for zero or more characters.
<h2 id=calltree>1.2 Call Tree</h2>
The Call Tree report is very similar to the Context-Sensitive Call Graph, but the data is not aggregated.
Also the 'Count' column, which would be always 1, is replaced by the 'Call Time'.
<h2 id=allbranches>1.3 All branches</h2>
The All branches report displays all branches in chronological order.
Not all data is fetched immediately. More records can be fetched using the Fetch bar provided.
<h3>Disassembly</h3>
Open a branch to display disassembly. This only works if:
<ol>
<li>The disassembler is available. Currently, only Intel XED is supported - see <a href=#xed>Intel XED Setup</a></li>
<li>The object code is available. Currently, only the perf build ID cache is searched for object code.
The default directory ~/.debug can be overridden by setting environment variable PERF_BUILDID_DIR.
One exception is kcore where the DSO long name is used (refer dsos_view on the Tables menu),
or alternatively, set environment variable PERF_KCORE to the kcore file name.</li>
</ol>
<h4 id=xed>Intel XED Setup</h4>
To use Intel XED, libxed.so must be present.  To build and install libxed.so:
<pre>
git clone https://github.com/intelxed/mbuild.git mbuild
git clone https://github.com/intelxed/xed
cd xed
./mfile.py --share
sudo ./mfile.py --prefix=/usr/local install
sudo ldconfig
</pre>
<h3>Instructions per Cycle (IPC)</h3>
If available, IPC information is displayed in columns 'insn_cnt', 'cyc_cnt' and 'IPC'.
<p><b>Intel PT note:</b> The information applies to the blocks of code ending with, and including, that branch.
Due to the granularity of timing information, the number of cycles for some code blocks will not be known.
In that case, 'insn_cnt', 'cyc_cnt' and 'IPC' are zero, but when 'IPC' is displayed it covers the period
since the previous displayed 'IPC'.
<h3>Find</h3>
Ctrl-F displays a Find bar which finds substrings by either an exact match or a regular expression match.
Refer to Python documentation for the regular expression syntax.
All columns are searched, but only currently fetched rows are searched.
<h2 id=selectedbranches>1.4 Selected branches</h2>
This is the same as the <a href=#allbranches>All branches</a> report but with the data reduced
by various selection criteria. A dialog box displays available criteria which are AND'ed together.
<h3>1.4.1 Time ranges</h3>
The time ranges hint text shows the total time range. Relative time ranges can also be entered in
ms, us or ns. Also, negative values are relative to the end of trace.  Examples:
<pre>
	81073085947329-81073085958238	From 81073085947329 to 81073085958238
	100us-200us		From 100us to 200us
	10ms-			From 10ms to the end
	-100ns			The first 100ns
	-10ms-			The last 10ms
</pre>
N.B. Due to the granularity of timestamps, there could be no branches in any given time range.
<h2 id=topcallsbyelapsedtime>1.5 Top calls by elapsed time</h2>
The Top calls by elapsed time report displays calls in descending order of time elapsed between when the function was called and when it returned.
The data is reduced by various selection criteria. A dialog box displays available criteria which are AND'ed together.
If not all data is fetched, a Fetch bar is provided. Ctrl-F displays a Find bar.
<h1 id=charts>2. Charts</h1>
<h2 id=timechartbycpu>2.1 Time chart by CPU</h2>
This chart displays context switch information when that data is available. Refer to context_switches_view on the Tables menu.
<h3>Features</h3>
<ol>
<li>Mouse over to highight the task and show the time</li>
<li>Drag the mouse to select a region and zoom by pushing the Zoom button</li>
<li>Go back and forward by pressing the arrow buttons</li>
<li>If call information is available, right-click to show a call tree opened to that task and time.
Note, the call tree may take some time to appear, and there may not be call information for the task or time selected.
</li>
</ol>
<h3>Important</h3>
The graph can be misleading in the following respects:
<ol>
<li>The graph shows the first task on each CPU as running from the beginning of the time range.
Because tracing might start on different CPUs at different times, that is not necessarily the case.
Refer to context_switches_view on the Tables menu to understand what data the graph is based upon.</li>
<li>Similarly, the last task on each CPU can be showing running longer than it really was.
Again, refer to context_switches_view on the Tables menu to understand what data the graph is based upon.</li>
<li>When the mouse is over a task, the highlighted task might not be visible on the legend without scrolling if the legend does not fit fully in the window</li>
</ol>
<h1 id=tables>3. Tables</h1>
The Tables menu shows all tables and views in the database. Most tables have an associated view
which displays the information in a more friendly way. Not all data for large tables is fetched
immediately. More records can be fetched using the Fetch bar provided. Columns can be sorted,
but that can be slow for large tables.
<p>There are also tables of database meta-information.
For SQLite3 databases, the sqlite_master table is included.
For PostgreSQL databases, information_schema.tables/views/columns are included.
<h3>Find</h3>
Ctrl-F displays a Find bar which finds substrings by either an exact match or a regular expression match.
Refer to Python documentation for the regular expression syntax.
All columns are searched, but only currently fetched rows are searched.
<p>N.B. Results are found in id order, so if the table is re-ordered, find-next and find-previous
will go to the next/previous result in id order, instead of display order.
"""

# Help window

class HelpWindow(QMdiSubWindow):

	def __init__(self, glb, parent=None):
		super(HelpWindow, self).__init__(parent)

		self.text = QTextBrowser()
		self.text.setHtml(glb_help_text)
		self.text.setReadOnly(True)
		self.text.setOpenExternalLinks(True)

		self.setWidget(self.text)

		AddSubWindow(glb.mainwindow.mdi_area, self, "Exported SQL Viewer Help")

# Main window that only displays the help text

class HelpOnlyWindow(QMainWindow):

	def __init__(self, parent=None):
		super(HelpOnlyWindow, self).__init__(parent)

		self.setMinimumSize(200, 100)
		self.resize(800, 600)
		self.setWindowTitle("Exported SQL Viewer Help")
		self.setWindowIcon(self.style().standardIcon(QStyle.SP_MessageBoxInformation))

		self.text = QTextBrowser()
		self.text.setHtml(glb_help_text)
		self.text.setReadOnly(True)
		self.text.setOpenExternalLinks(True)

		self.setCentralWidget(self.text)

# PostqreSQL server version

def PostqreSQLServerVersion(db):
	query = QSqlQuery(db)
	QueryExec(query, "SELECT VERSION()")
	if query.next():
		v_str = query.value(0)
		v_list = v_str.strip().split(" ")
		if v_list[0] == "PostgreSQL" and v_list[2] == "on":
			return v_list[1]
		return v_str
	return "Unknown"

# SQLite version

def SQLiteVersion(db):
	query = QSqlQuery(db)
	QueryExec(query, "SELECT sqlite_version()")
	if query.next():
		return query.value(0)
	return "Unknown"

# About dialog

class AboutDialog(QDialog):

	def __init__(self, glb, parent=None):
		super(AboutDialog, self).__init__(parent)

		self.setWindowTitle("About Exported SQL Viewer")
		self.setMinimumWidth(300)

		pyside_version = "1" if pyside_version_1 else "2"

		text = "<pre>"
		text += "Python version:     " + sys.version.split(" ")[0] + "\n"
		text += "PySide version:     " + pyside_version + "\n"
		text += "Qt version:         " + qVersion() + "\n"
		if glb.dbref.is_sqlite3:
			text += "SQLite version:     " + SQLiteVersion(glb.db) + "\n"
		else:
			text += "PostqreSQL version: " + PostqreSQLServerVersion(glb.db) + "\n"
		text += "</pre>"

		self.text = QTextBrowser()
		self.text.setHtml(text)
		self.text.setReadOnly(True)
		self.text.setOpenExternalLinks(True)

		self.vbox = QVBoxLayout()
		self.vbox.addWidget(self.text)

		self.setLayout(self.vbox)

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
		edit_menu.addAction(CreateAction("&Copy", "Copy to clipboard", self.CopyToClipboard, self, QKeySequence.Copy))
		edit_menu.addAction(CreateAction("Copy as CS&V", "Copy to clipboard as CSV", self.CopyToClipboardCSV, self))
		edit_menu.addAction(CreateAction("&Find...", "Find items", self.Find, self, QKeySequence.Find))
		edit_menu.addAction(CreateAction("Fetch &more records...", "Fetch more records", self.FetchMoreRecords, self, [QKeySequence(Qt.Key_F8)]))
		edit_menu.addAction(CreateAction("&Shrink Font", "Make text smaller", self.ShrinkFont, self, [QKeySequence("Ctrl+-")]))
		edit_menu.addAction(CreateAction("&Enlarge Font", "Make text bigger", self.EnlargeFont, self, [QKeySequence("Ctrl++")]))

		reports_menu = menu.addMenu("&Reports")
		if IsSelectable(glb.db, "calls"):
			reports_menu.addAction(CreateAction("Context-Sensitive Call &Graph", "Create a new window containing a context-sensitive call graph", self.NewCallGraph, self))

		if IsSelectable(glb.db, "calls", "WHERE parent_id >= 0"):
			reports_menu.addAction(CreateAction("Call &Tree", "Create a new window containing a call tree", self.NewCallTree, self))

		self.EventMenu(GetEventList(glb.db), reports_menu)

		if IsSelectable(glb.db, "calls"):
			reports_menu.addAction(CreateAction("&Top calls by elapsed time", "Create a new window displaying top calls by elapsed time", self.NewTopCalls, self))

		if IsSelectable(glb.db, "context_switches"):
			charts_menu = menu.addMenu("&Charts")
			charts_menu.addAction(CreateAction("&Time chart by CPU", "Create a new window displaying time charts by CPU", self.TimeChartByCPU, self))

		self.TableMenu(GetTableList(glb), menu)

		self.window_menu = WindowMenu(self.mdi_area, menu)

		help_menu = menu.addMenu("&Help")
		help_menu.addAction(CreateAction("&Exported SQL Viewer Help", "Helpful information", self.Help, self, QKeySequence.HelpContents))
		help_menu.addAction(CreateAction("&About Exported SQL Viewer", "About this application", self.About, self))

	def Try(self, fn):
		win = self.mdi_area.activeSubWindow()
		if win:
			try:
				fn(win.view)
			except:
				pass

	def CopyToClipboard(self):
		self.Try(CopyCellsToClipboardHdr)

	def CopyToClipboardCSV(self):
		self.Try(CopyCellsToClipboardCSV)

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
		self.Try(ShrinkFont)

	def EnlargeFont(self):
		self.Try(EnlargeFont)

	def EventMenu(self, events, reports_menu):
		branches_events = 0
		for event in events:
			event = event.split(":")[0]
			if event == "branches":
				branches_events += 1
		dbid = 0
		for event in events:
			dbid += 1
			event = event.split(":")[0]
			if event == "branches":
				label = "All branches" if branches_events == 1 else "All branches " + "(id=" + dbid + ")"
				reports_menu.addAction(CreateAction(label, "Create a new window displaying branch events", lambda a=None,x=dbid: self.NewBranchView(x), self))
				label = "Selected branches" if branches_events == 1 else "Selected branches " + "(id=" + dbid + ")"
				reports_menu.addAction(CreateAction(label, "Create a new window displaying branch events", lambda a=None,x=dbid: self.NewSelectedBranchView(x), self))

	def TimeChartByCPU(self):
		TimeChartByCPUWindow(self.glb, self)

	def TableMenu(self, tables, menu):
		table_menu = menu.addMenu("&Tables")
		for table in tables:
			table_menu.addAction(CreateAction(table, "Create a new window containing a table view", lambda a=None,t=table: self.NewTableView(t), self))

	def NewCallGraph(self):
		CallGraphWindow(self.glb, self)

	def NewCallTree(self):
		CallTreeWindow(self.glb, self)

	def NewTopCalls(self):
		dialog = TopCallsDialog(self.glb, self)
		ret = dialog.exec_()
		if ret:
			TopCallsWindow(self.glb, dialog.report_vars, self)

	def NewBranchView(self, event_id):
		BranchWindow(self.glb, event_id, ReportVars(), self)

	def NewSelectedBranchView(self, event_id):
		dialog = SelectedBranchDialog(self.glb, self)
		ret = dialog.exec_()
		if ret:
			BranchWindow(self.glb, event_id, dialog.report_vars, self)

	def NewTableView(self, table_name):
		TableWindow(self.glb, table_name, self)

	def Help(self):
		HelpWindow(self.glb, self)

	def About(self):
		dialog = AboutDialog(self.glb, self)
		dialog.exec_()

# XED Disassembler

class xed_state_t(Structure):

	_fields_ = [
		("mode", c_int),
		("width", c_int)
	]

class XEDInstruction():

	def __init__(self, libxed):
		# Current xed_decoded_inst_t structure is 192 bytes. Use 512 to allow for future expansion
		xedd_t = c_byte * 512
		self.xedd = xedd_t()
		self.xedp = addressof(self.xedd)
		libxed.xed_decoded_inst_zero(self.xedp)
		self.state = xed_state_t()
		self.statep = addressof(self.state)
		# Buffer for disassembled instruction text
		self.buffer = create_string_buffer(256)
		self.bufferp = addressof(self.buffer)

class LibXED():

	def __init__(self):
		try:
			self.libxed = CDLL("libxed.so")
		except:
			self.libxed = None
		if not self.libxed:
			self.libxed = CDLL("/usr/local/lib/libxed.so")

		self.xed_tables_init = self.libxed.xed_tables_init
		self.xed_tables_init.restype = None
		self.xed_tables_init.argtypes = []

		self.xed_decoded_inst_zero = self.libxed.xed_decoded_inst_zero
		self.xed_decoded_inst_zero.restype = None
		self.xed_decoded_inst_zero.argtypes = [ c_void_p ]

		self.xed_operand_values_set_mode = self.libxed.xed_operand_values_set_mode
		self.xed_operand_values_set_mode.restype = None
		self.xed_operand_values_set_mode.argtypes = [ c_void_p, c_void_p ]

		self.xed_decoded_inst_zero_keep_mode = self.libxed.xed_decoded_inst_zero_keep_mode
		self.xed_decoded_inst_zero_keep_mode.restype = None
		self.xed_decoded_inst_zero_keep_mode.argtypes = [ c_void_p ]

		self.xed_decode = self.libxed.xed_decode
		self.xed_decode.restype = c_int
		self.xed_decode.argtypes = [ c_void_p, c_void_p, c_uint ]

		self.xed_format_context = self.libxed.xed_format_context
		self.xed_format_context.restype = c_uint
		self.xed_format_context.argtypes = [ c_int, c_void_p, c_void_p, c_int, c_ulonglong, c_void_p, c_void_p ]

		self.xed_tables_init()

	def Instruction(self):
		return XEDInstruction(self)

	def SetMode(self, inst, mode):
		if mode:
			inst.state.mode = 4 # 32-bit
			inst.state.width = 4 # 4 bytes
		else:
			inst.state.mode = 1 # 64-bit
			inst.state.width = 8 # 8 bytes
		self.xed_operand_values_set_mode(inst.xedp, inst.statep)

	def DisassembleOne(self, inst, bytes_ptr, bytes_cnt, ip):
		self.xed_decoded_inst_zero_keep_mode(inst.xedp)
		err = self.xed_decode(inst.xedp, bytes_ptr, bytes_cnt)
		if err:
			return 0, ""
		# Use AT&T mode (2), alternative is Intel (3)
		ok = self.xed_format_context(2, inst.xedp, inst.bufferp, sizeof(inst.buffer), ip, 0, 0)
		if not ok:
			return 0, ""
		if sys.version_info[0] == 2:
			result = inst.buffer.value
		else:
			result = inst.buffer.value.decode()
		# Return instruction length and the disassembled instruction text
		# For now, assume the length is in byte 166
		return inst.xedd[166], result

def TryOpen(file_name):
	try:
		return open(file_name, "rb")
	except:
		return None

def Is64Bit(f):
	result = sizeof(c_void_p)
	# ELF support only
	pos = f.tell()
	f.seek(0)
	header = f.read(7)
	f.seek(pos)
	magic = header[0:4]
	if sys.version_info[0] == 2:
		eclass = ord(header[4])
		encoding = ord(header[5])
		version = ord(header[6])
	else:
		eclass = header[4]
		encoding = header[5]
		version = header[6]
	if magic == chr(127) + "ELF" and eclass > 0 and eclass < 3 and encoding > 0 and encoding < 3 and version == 1:
		result = True if eclass == 2 else False
	return result

# Global data

class Glb():

	def __init__(self, dbref, db, dbname):
		self.dbref = dbref
		self.db = db
		self.dbname = dbname
		self.home_dir = os.path.expanduser("~")
		self.buildid_dir = os.getenv("PERF_BUILDID_DIR")
		if self.buildid_dir:
			self.buildid_dir += "/.build-id/"
		else:
			self.buildid_dir = self.home_dir + "/.debug/.build-id/"
		self.app = None
		self.mainwindow = None
		self.instances_to_shutdown_on_exit = weakref.WeakSet()
		try:
			self.disassembler = LibXED()
			self.have_disassembler = True
		except:
			self.have_disassembler = False
		self.host_machine_id = 0
		self.host_start_time = 0
		self.host_finish_time = 0

	def FileFromBuildId(self, build_id):
		file_name = self.buildid_dir + build_id[0:2] + "/" + build_id[2:] + "/elf"
		return TryOpen(file_name)

	def FileFromNamesAndBuildId(self, short_name, long_name, build_id):
		# Assume current machine i.e. no support for virtualization
		if short_name[0:7] == "[kernel" and os.path.basename(long_name) == "kcore":
			file_name = os.getenv("PERF_KCORE")
			f = TryOpen(file_name) if file_name else None
			if f:
				return f
			# For now, no special handling if long_name is /proc/kcore
			f = TryOpen(long_name)
			if f:
				return f
		f = self.FileFromBuildId(build_id)
		if f:
			return f
		return None

	def AddInstanceToShutdownOnExit(self, instance):
		self.instances_to_shutdown_on_exit.add(instance)

	# Shutdown any background processes or threads
	def ShutdownInstances(self):
		for x in self.instances_to_shutdown_on_exit:
			try:
				x.Shutdown()
			except:
				pass

	def GetHostMachineId(self):
		query = QSqlQuery(self.db)
		QueryExec(query, "SELECT id FROM machines WHERE pid = -1")
		if query.next():
			self.host_machine_id = query.value(0)
		else:
			self.host_machine_id = 0
		return self.host_machine_id

	def HostMachineId(self):
		if self.host_machine_id:
			return self.host_machine_id
		return self.GetHostMachineId()

	def SelectValue(self, sql):
		query = QSqlQuery(self.db)
		try:
			QueryExec(query, sql)
		except:
			return None
		if query.next():
			return Decimal(query.value(0))
		return None

	def SwitchesMinTime(self, machine_id):
		return self.SelectValue("SELECT time"
					" FROM context_switches"
					" WHERE time != 0 AND machine_id = " + str(machine_id) +
					" ORDER BY id LIMIT 1")

	def SwitchesMaxTime(self, machine_id):
		return self.SelectValue("SELECT time"
					" FROM context_switches"
					" WHERE time != 0 AND machine_id = " + str(machine_id) +
					" ORDER BY id DESC LIMIT 1")

	def SamplesMinTime(self, machine_id):
		return self.SelectValue("SELECT time"
					" FROM samples"
					" WHERE time != 0 AND machine_id = " + str(machine_id) +
					" ORDER BY id LIMIT 1")

	def SamplesMaxTime(self, machine_id):
		return self.SelectValue("SELECT time"
					" FROM samples"
					" WHERE time != 0 AND machine_id = " + str(machine_id) +
					" ORDER BY id DESC LIMIT 1")

	def CallsMinTime(self, machine_id):
		return self.SelectValue("SELECT calls.call_time"
					" FROM calls"
					" INNER JOIN threads ON threads.thread_id = calls.thread_id"
					" WHERE calls.call_time != 0 AND threads.machine_id = " + str(machine_id) +
					" ORDER BY calls.id LIMIT 1")

	def CallsMaxTime(self, machine_id):
		return self.SelectValue("SELECT calls.return_time"
					" FROM calls"
					" INNER JOIN threads ON threads.thread_id = calls.thread_id"
					" WHERE calls.return_time != 0 AND threads.machine_id = " + str(machine_id) +
					" ORDER BY calls.return_time DESC LIMIT 1")

	def GetStartTime(self, machine_id):
		t0 = self.SwitchesMinTime(machine_id)
		t1 = self.SamplesMinTime(machine_id)
		t2 = self.CallsMinTime(machine_id)
		if t0 is None or (not(t1 is None) and t1 < t0):
			t0 = t1
		if t0 is None or (not(t2 is None) and t2 < t0):
			t0 = t2
		return t0

	def GetFinishTime(self, machine_id):
		t0 = self.SwitchesMaxTime(machine_id)
		t1 = self.SamplesMaxTime(machine_id)
		t2 = self.CallsMaxTime(machine_id)
		if t0 is None or (not(t1 is None) and t1 > t0):
			t0 = t1
		if t0 is None or (not(t2 is None) and t2 > t0):
			t0 = t2
		return t0

	def HostStartTime(self):
		if self.host_start_time:
			return self.host_start_time
		self.host_start_time = self.GetStartTime(self.HostMachineId())
		return self.host_start_time

	def HostFinishTime(self):
		if self.host_finish_time:
			return self.host_finish_time
		self.host_finish_time = self.GetFinishTime(self.HostMachineId())
		return self.host_finish_time

	def StartTime(self, machine_id):
		if machine_id == self.HostMachineId():
			return self.HostStartTime()
		return self.GetStartTime(machine_id)

	def FinishTime(self, machine_id):
		if machine_id == self.HostMachineId():
			return self.HostFinishTime()
		return self.GetFinishTime(machine_id)

# Database reference

class DBRef():

	def __init__(self, is_sqlite3, dbname):
		self.is_sqlite3 = is_sqlite3
		self.dbname = dbname
		self.TRUE = "TRUE"
		self.FALSE = "FALSE"
		# SQLite prior to version 3.23 does not support TRUE and FALSE
		if self.is_sqlite3:
			self.TRUE = "1"
			self.FALSE = "0"

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
	usage_str =	"exported-sql-viewer.py [--pyside-version-1] <database name>\n" \
			"   or: exported-sql-viewer.py --help-only"
	ap = argparse.ArgumentParser(usage = usage_str, add_help = False)
	ap.add_argument("--pyside-version-1", action='store_true')
	ap.add_argument("dbname", nargs="?")
	ap.add_argument("--help-only", action='store_true')
	args = ap.parse_args()

	if args.help_only:
		app = QApplication(sys.argv)
		mainwindow = HelpOnlyWindow()
		mainwindow.show()
		err = app.exec_()
		sys.exit(err)

	dbname = args.dbname
	if dbname is None:
		ap.print_usage()
		print("Too few arguments")
		sys.exit(1)

	is_sqlite3 = False
	try:
		f = open(dbname, "rb")
		if f.read(15) == b'SQLite format 3':
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
