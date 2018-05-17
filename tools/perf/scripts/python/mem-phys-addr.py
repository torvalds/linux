# mem-phys-addr.py: Resolve physical address samples
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018, Intel Corporation.

from __future__ import division
import os
import sys
import struct
import re
import bisect
import collections

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

#physical address ranges for System RAM
system_ram = []
#physical address ranges for Persistent Memory
pmem = []
#file object for proc iomem
f = None
#Count for each type of memory
load_mem_type_cnt = collections.Counter()
#perf event name
event_name = None

def parse_iomem():
	global f
	f = open('/proc/iomem', 'r')
	for i, j in enumerate(f):
		m = re.split('-|:',j,2)
		if m[2].strip() == 'System RAM':
			system_ram.append(long(m[0], 16))
			system_ram.append(long(m[1], 16))
		if m[2].strip() == 'Persistent Memory':
			pmem.append(long(m[0], 16))
			pmem.append(long(m[1], 16))

def print_memory_type():
	print "Event: %s" % (event_name)
	print "%-40s  %10s  %10s\n" % ("Memory type", "count", "percentage"),
	print "%-40s  %10s  %10s\n" % ("----------------------------------------", \
					"-----------", "-----------"),
	total = sum(load_mem_type_cnt.values())
	for mem_type, count in sorted(load_mem_type_cnt.most_common(), \
					key = lambda(k, v): (v, k), reverse = True):
		print "%-40s  %10d  %10.1f%%\n" % (mem_type, count, 100 * count / total),

def trace_begin():
	parse_iomem()

def trace_end():
	print_memory_type()
	f.close()

def is_system_ram(phys_addr):
	#/proc/iomem is sorted
	position = bisect.bisect(system_ram, phys_addr)
	if position % 2 == 0:
		return False
	return True

def is_persistent_mem(phys_addr):
	position = bisect.bisect(pmem, phys_addr)
	if position % 2 == 0:
		return False
	return True

def find_memory_type(phys_addr):
	if phys_addr == 0:
		return "N/A"
	if is_system_ram(phys_addr):
		return "System RAM"

	if is_persistent_mem(phys_addr):
		return "Persistent Memory"

	#slow path, search all
	f.seek(0, 0)
	for j in f:
		m = re.split('-|:',j,2)
		if long(m[0], 16) <= phys_addr <= long(m[1], 16):
			return m[2]
	return "N/A"

def process_event(param_dict):
	name       = param_dict["ev_name"]
	sample     = param_dict["sample"]
	phys_addr  = sample["phys_addr"]

	global event_name
	if event_name == None:
		event_name = name
	load_mem_type_cnt[find_memory_type(phys_addr)] += 1
