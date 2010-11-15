# system call top
# (c) 2010, Tom Zanussi <tzanussi@gmail.com>
# Licensed under the terms of the GNU GPL License version 2
#
# Periodically displays system-wide system call totals, broken down by
# syscall.  If a [comm] arg is specified, only syscalls called by
# [comm] are displayed. If an [interval] arg is specified, the display
# will be refreshed every [interval] seconds.  The default interval is
# 3 seconds.

import os, sys, thread, time

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *
from Util import *

usage = "perf trace -s sctop.py [comm] [interval]\n";

for_comm = None
default_interval = 3
interval = default_interval

if len(sys.argv) > 3:
	sys.exit(usage)

if len(sys.argv) > 2:
	for_comm = sys.argv[1]
	interval = int(sys.argv[2])
elif len(sys.argv) > 1:
	try:
		interval = int(sys.argv[1])
	except ValueError:
		for_comm = sys.argv[1]
		interval = default_interval

syscalls = autodict()

def trace_begin():
	thread.start_new_thread(print_syscall_totals, (interval,))
	pass

def raw_syscalls__sys_enter(event_name, context, common_cpu,
	common_secs, common_nsecs, common_pid, common_comm,
	id, args):
	if for_comm is not None:
		if common_comm != for_comm:
			return
	try:
		syscalls[id] += 1
	except TypeError:
		syscalls[id] = 1

def print_syscall_totals(interval):
	while 1:
		clear_term()
		if for_comm is not None:
			print "\nsyscall events for %s:\n\n" % (for_comm),
		else:
			print "\nsyscall events:\n\n",

		print "%-40s  %10s\n" % ("event", "count"),
		print "%-40s  %10s\n" % ("----------------------------------------", \
						 "----------"),

		for id, val in sorted(syscalls.iteritems(), key = lambda(k, v): (v, k), \
					      reverse = True):
			try:
				print "%-40s  %10d\n" % (syscall_name(id), val),
			except TypeError:
				pass
		syscalls.clear()
		time.sleep(interval)
