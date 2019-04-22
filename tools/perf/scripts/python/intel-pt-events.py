# intel-pt-events.py: Print Intel PT Power Events and PTWRITE
# Copyright (c) 2017, Intel Corporation.
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

def trace_begin():
	print("Intel PT Power Events and PTWRITE")

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

def print_common_start(comm, sample, name):
	ts = sample["time"]
	cpu = sample["cpu"]
	pid = sample["pid"]
	tid = sample["tid"]
	print("%16s %5u/%-5u [%03u] %9u.%09u %7s:" %
		(comm, pid, tid, cpu, ts / 1000000000, ts %1000000000, name),
		end=' ')

def print_common_ip(sample, symbol, dso):
	ip = sample["ip"]
	print("%16x %s (%s)" % (ip, symbol, dso))

def process_event(param_dict):
	event_attr = param_dict["attr"]
	sample	 = param_dict["sample"]
	raw_buf	= param_dict["raw_buf"]
	comm	   = param_dict["comm"]
	name	   = param_dict["ev_name"]

	# Symbol and dso info are not always resolved
	if "dso" in param_dict:
		dso = param_dict["dso"]
	else:
		dso = "[unknown]"

	if "symbol" in param_dict:
		symbol = param_dict["symbol"]
	else:
		symbol = "[unknown]"

	if name == "ptwrite":
		print_common_start(comm, sample, name)
		print_ptwrite(raw_buf)
		print_common_ip(sample, symbol, dso)
	elif name == "cbr":
		print_common_start(comm, sample, name)
		print_cbr(raw_buf)
		print_common_ip(sample, symbol, dso)
	elif name == "mwait":
		print_common_start(comm, sample, name)
		print_mwait(raw_buf)
		print_common_ip(sample, symbol, dso)
	elif name == "pwre":
		print_common_start(comm, sample, name)
		print_pwre(raw_buf)
		print_common_ip(sample, symbol, dso)
	elif name == "exstop":
		print_common_start(comm, sample, name)
		print_exstop(raw_buf)
		print_common_ip(sample, symbol, dso)
	elif name == "pwrx":
		print_common_start(comm, sample, name)
		print_pwrx(raw_buf)
		print_common_ip(sample, symbol, dso)
