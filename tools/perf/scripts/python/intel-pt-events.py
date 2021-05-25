# SPDX-License-Identifier: GPL-2.0
# intel-pt-events.py: Print Intel PT Events including Power Events and PTWRITE
# Copyright (c) 2017-2021, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.

from __future__ import print_function

import os
import sys
import struct

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

# These perf imports are not used at present
#from perf_trace_context import *
#from Core import *

try:
	broken_pipe_exception = BrokenPipeError
except:
	broken_pipe_exception = IOError

glb_switch_str = None
glb_switch_printed = True

def get_optional_null(perf_dict, field):
	if field in perf_dict:
		return perf_dict[field]
	return ""

def get_optional_zero(perf_dict, field):
	if field in perf_dict:
		return perf_dict[field]
	return 0

def get_optional(perf_dict, field):
	if field in perf_dict:
		return perf_dict[field]
	return "[unknown]"

def get_offset(perf_dict, field):
	if field in perf_dict:
		return "+%#x" % perf_dict[field]
	return ""

def trace_begin():
	print("Intel PT Branch Trace, Power Events and PTWRITE")

def trace_end():
	print("End")

def trace_unhandled(event_name, context, event_fields_dict):
		print(' '.join(['%s=%s'%(k,str(v))for k,v in sorted(event_fields_dict.items())]))

def print_ptwrite(raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	flags = data[0]
	payload = data[1]
	exact_ip = flags & 1
	print("IP: %u payload: %#x" % (exact_ip, payload), end=' ')

def print_cbr(raw_buf):
	data = struct.unpack_from("<BBBBII", raw_buf)
	cbr = data[0]
	f = (data[4] + 500) / 1000
	p = ((cbr * 1000 / data[2]) + 5) / 10
	print("%3u  freq: %4u MHz  (%3u%%)" % (cbr, f, p), end=' ')

def print_mwait(raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	payload = data[1]
	hints = payload & 0xff
	extensions = (payload >> 32) & 0x3
	print("hints: %#x extensions: %#x" % (hints, extensions), end=' ')

def print_pwre(raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	payload = data[1]
	hw = (payload >> 7) & 1
	cstate = (payload >> 12) & 0xf
	subcstate = (payload >> 8) & 0xf
	print("hw: %u cstate: %u sub-cstate: %u" % (hw, cstate, subcstate),
		end=' ')

def print_exstop(raw_buf):
	data = struct.unpack_from("<I", raw_buf)
	flags = data[0]
	exact_ip = flags & 1
	print("IP: %u" % (exact_ip), end=' ')

def print_pwrx(raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	payload = data[1]
	deepest_cstate = payload & 0xf
	last_cstate = (payload >> 4) & 0xf
	wake_reason = (payload >> 8) & 0xf
	print("deepest cstate: %u last cstate: %u wake reason: %#x" %
		(deepest_cstate, last_cstate, wake_reason), end=' ')

def print_psb(raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	offset = data[1]
	print("offset: %#x" % (offset), end=' ')

def print_common_start(comm, sample, name):
	ts = sample["time"]
	cpu = sample["cpu"]
	pid = sample["pid"]
	tid = sample["tid"]
	flags_disp = get_optional_null(sample, "flags_disp")
	# Unused fields:
	# period      = sample["period"]
	# phys_addr   = sample["phys_addr"]
	# weight      = sample["weight"]
	# transaction = sample["transaction"]
	# cpumode     = get_optional_zero(sample, "cpumode")
	print("%16s %5u/%-5u [%03u] %9u.%09u  %7s  %19s" %
		(comm, pid, tid, cpu, ts / 1000000000, ts %1000000000, name, flags_disp),
		end=' ')

def print_common_ip(param_dict, sample, symbol, dso):
	ip   = sample["ip"]
	offs = get_offset(param_dict, "symoff")
	print("%16x %s%s (%s)" % (ip, symbol, offs, dso), end=' ')
	if "addr_correlates_sym" in sample:
		addr   = sample["addr"]
		dso    = get_optional(sample, "addr_dso")
		symbol = get_optional(sample, "addr_symbol")
		offs   = get_offset(sample, "addr_symoff")
		print("=> %x %s%s (%s)" % (addr, symbol, offs, dso))
	else:
		print()

def do_process_event(param_dict):
	global glb_switch_printed
	if not glb_switch_printed:
		print(glb_switch_str)
		glb_switch_printed = True
	event_attr = param_dict["attr"]
	sample	   = param_dict["sample"]
	raw_buf	   = param_dict["raw_buf"]
	comm	   = param_dict["comm"]
	name	   = param_dict["ev_name"]
	# Unused fields:
	# callchain  = param_dict["callchain"]
	# brstack    = param_dict["brstack"]
	# brstacksym = param_dict["brstacksym"]

	# Symbol and dso info are not always resolved
	dso    = get_optional(param_dict, "dso")
	symbol = get_optional(param_dict, "symbol")

	print_common_start(comm, sample, name)

	if name == "ptwrite":
		print_ptwrite(raw_buf)
	elif name == "cbr":
		print_cbr(raw_buf)
	elif name == "mwait":
		print_mwait(raw_buf)
	elif name == "pwre":
		print_pwre(raw_buf)
	elif name == "exstop":
		print_exstop(raw_buf)
	elif name == "pwrx":
		print_pwrx(raw_buf)
	elif name == "psb":
		print_psb(raw_buf)

	print_common_ip(param_dict, sample, symbol, dso)

def process_event(param_dict):
	try:
		do_process_event(param_dict)
	except broken_pipe_exception:
		# Stop python printing broken pipe errors and traceback
		sys.stdout = open(os.devnull, 'w')
		sys.exit(1)

def auxtrace_error(typ, code, cpu, pid, tid, ip, ts, msg, cpumode, *x):
	try:
		print("%16s %5u/%-5u [%03u] %9u.%09u  error type %u code %u: %s ip 0x%16x" %
			("Trace error", pid, tid, cpu, ts / 1000000000, ts %1000000000, typ, code, msg, ip))
	except broken_pipe_exception:
		# Stop python printing broken pipe errors and traceback
		sys.stdout = open(os.devnull, 'w')
		sys.exit(1)

def context_switch(ts, cpu, pid, tid, np_pid, np_tid, machine_pid, out, out_preempt, *x):
	global glb_switch_printed
	global glb_switch_str
	if out:
		out_str = "Switch out "
	else:
		out_str = "Switch In  "
	if out_preempt:
		preempt_str = "preempt"
	else:
		preempt_str = ""
	if machine_pid == -1:
		machine_str = ""
	else:
		machine_str = "machine PID %d" % machine_pid
	glb_switch_str = "%16s %5d/%-5d [%03u] %9u.%09u %5d/%-5d %s %s" % \
		(out_str, pid, tid, cpu, ts / 1000000000, ts %1000000000, np_pid, np_tid, machine_str, preempt_str)
	glb_switch_printed = False
