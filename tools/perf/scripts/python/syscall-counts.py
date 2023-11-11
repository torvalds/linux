# system call counts
# (c) 2010, Tom Zanussi <tzanussi@gmail.com>
# Licensed under the terms of the GNU GPL License version 2
#
# Displays system-wide system call totals, broken down by syscall.
# If a [comm] arg is specified, only syscalls called by [comm] are displayed.

from __future__ import print_function

import os
import sys

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *
from Util import syscall_name

usage = "perf script -s syscall-counts.py [comm]\n";

for_comm = None

if len(sys.argv) > 2:
	sys.exit(usage)

if len(sys.argv) > 1:
	for_comm = sys.argv[1]

syscalls = autodict()

def trace_begin():
	print("Press control+C to stop and show the summary")

def trace_end():
	print_syscall_totals()

def raw_syscalls__sys_enter(event_name, context, common_cpu,
		common_secs, common_nsecs, common_pid, common_comm,
		common_callchain, id, args):
	if for_comm is not None:
		if common_comm != for_comm:
			return
	try:
		syscalls[id] += 1
	except TypeError:
		syscalls[id] = 1

def syscalls__sys_enter(event_name, context, common_cpu,
		common_secs, common_nsecs, common_pid, common_comm, id, args):
	raw_syscalls__sys_enter(**locals())

def print_syscall_totals():
	if for_comm is not None:
		print("\nsyscall events for %s:\n" % (for_comm))
	else:
		print("\nsyscall events:\n")

	print("%-40s  %10s" % ("event", "count"))
	print("%-40s  %10s" % ("----------------------------------------",
				"-----------"))

	for id, val in sorted(syscalls.items(),
			key = lambda kv: (kv[1], kv[0]), reverse = True):
		print("%-40s  %10d" % (syscall_name(id), val))
