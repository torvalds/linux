# system call counts, by pid
# (c) 2010, Tom Zanussi <tzanussi@gmail.com>
# Licensed under the terms of the GNU GPL License version 2
#
# Displays system-wide system call totals, broken down by syscall.
# If a [comm] arg is specified, only syscalls called by [comm] are displayed.

from __future__ import print_function

import os, sys

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *
from Util import syscall_name

usage = "perf script -s syscall-counts-by-pid.py [comm]\n";

for_comm = None
for_pid = None

if len(sys.argv) > 2:
	sys.exit(usage)

if len(sys.argv) > 1:
	try:
		for_pid = int(sys.argv[1])
	except:
		for_comm = sys.argv[1]

syscalls = autodict()

def trace_begin():
	print("Press control+C to stop and show the summary")

def trace_end():
	print_syscall_totals()

def raw_syscalls__sys_enter(event_name, context, common_cpu,
		common_secs, common_nsecs, common_pid, common_comm,
		common_callchain, id, args):
	if (for_comm and common_comm != for_comm) or \
		(for_pid and common_pid != for_pid ):
		return
	try:
		syscalls[common_comm][common_pid][id] += 1
	except TypeError:
		syscalls[common_comm][common_pid][id] = 1

def syscalls__sys_enter(event_name, context, common_cpu,
		common_secs, common_nsecs, common_pid, common_comm,
		id, args):
	raw_syscalls__sys_enter(**locals())

def print_syscall_totals():
	if for_comm is not None:
		print("\nsyscall events for %s:\n" % (for_comm))
	else:
		print("\nsyscall events by comm/pid:\n")

	print("%-40s  %10s" % ("comm [pid]/syscalls", "count"))
	print("%-40s  %10s" % ("----------------------------------------",
				"----------"))

	comm_keys = syscalls.keys()
	for comm in comm_keys:
		pid_keys = syscalls[comm].keys()
		for pid in pid_keys:
			print("\n%s [%d]" % (comm, pid))
			id_keys = syscalls[comm][pid].keys()
			for id, val in sorted(syscalls[comm][pid].items(),
				key = lambda kv: (kv[1], kv[0]), reverse = True):
				print("  %-38s  %10d" % (syscall_name(id), val))
