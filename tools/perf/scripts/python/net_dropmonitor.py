# Monitor the system for dropped packets and proudce a report of drop locations and counts
# SPDX-License-Identifier: GPL-2.0

from __future__ import print_function

import os
import sys

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *
from Util import *

drop_log = {}
kallsyms = []

def get_kallsyms_table():
	global kallsyms

	try:
		f = open("/proc/kallsyms", "r")
	except:
		return

	for line in f:
		loc = int(line.split()[0], 16)
		name = line.split()[2]
		kallsyms.append((loc, name))
	kallsyms.sort()

def get_sym(sloc):
	loc = int(sloc)

	# Invariant: kallsyms[i][0] <= loc for all 0 <= i <= start
	#            kallsyms[i][0] > loc for all end <= i < len(kallsyms)
	start, end = -1, len(kallsyms)
	while end != start + 1:
		pivot = (start + end) // 2
		if loc < kallsyms[pivot][0]:
			end = pivot
		else:
			start = pivot

	# Now (start == -1 or kallsyms[start][0] <= loc)
	# and (start == len(kallsyms) - 1 or loc < kallsyms[start + 1][0])
	if start >= 0:
		symloc, name = kallsyms[start]
		return (name, loc - symloc)
	else:
		return (None, 0)

def print_drop_table():
	print("%25s %25s %25s" % ("LOCATION", "OFFSET", "COUNT"))
	for i in drop_log.keys():
		(sym, off) = get_sym(i)
		if sym == None:
			sym = i
		print("%25s %25s %25s" % (sym, off, drop_log[i]))


def trace_begin():
	print("Starting trace (Ctrl-C to dump results)")

def trace_end():
	print("Gathering kallsyms data")
	get_kallsyms_table()
	print_drop_table()

# called from perf, when it finds a corresponding event
def skb__kfree_skb(name, context, cpu, sec, nsec, pid, comm, callchain,
		   skbaddr, location, protocol, reason):
	slocation = str(location)
	try:
		drop_log[slocation] = drop_log[slocation] + 1
	except:
		drop_log[slocation] = 1
