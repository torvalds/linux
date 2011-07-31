# system call counts, by pid
# (c) 2010, Tom Zanussi <tzanussi@gmail.com>
# Licensed under the terms of the GNU GPL License version 2
#
# Displays system-wide system call totals, broken down by syscall.
# If a [comm] arg is specified, only syscalls called by [comm] are displayed.

import os
import sys

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *

usage = "perf trace -s syscall-counts-by-pid.py [comm]\n";

for_comm = None

if len(sys.argv) > 2:
	sys.exit(usage)

if len(sys.argv) > 1:
	for_comm = sys.argv[1]

syscalls = autodict()

def trace_begin():
	pass

def trace_end():
	print_syscall_totals()

def raw_syscalls__sys_enter(event_name, context, common_cpu,
	common_secs, common_nsecs, common_pid, common_comm,
	id, args):
	if for_comm is not None:
		if common_comm != for_comm:
			return
	try:
		syscalls[common_comm][common_pid][id] += 1
	except TypeError:
		syscalls[common_comm][common_pid][id] = 1

def print_syscall_totals():
    if for_comm is not None:
	    print "\nsyscall events for %s:\n\n" % (for_comm),
    else:
	    print "\nsyscall events by comm/pid:\n\n",

    print "%-40s  %10s\n" % ("comm [pid]/syscalls", "count"),
    print "%-40s  %10s\n" % ("----------------------------------------", \
                                 "----------"),

    comm_keys = syscalls.keys()
    for comm in comm_keys:
	    pid_keys = syscalls[comm].keys()
	    for pid in pid_keys:
		    print "\n%s [%d]\n" % (comm, pid),
		    id_keys = syscalls[comm][pid].keys()
		    for id, val in sorted(syscalls[comm][pid].iteritems(), \
				  key = lambda(k, v): (v, k),  reverse = True):
			    print "  %-38d  %10d\n" % (id, val),
