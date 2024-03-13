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

from __future__ import division, print_function

import os
import sys
import struct
import argparse

from libxed import LibXED
from ctypes import create_string_buffer, addressof

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import perf_set_itrace_options, \
	perf_sample_insn, perf_sample_srccode

try:
	broken_pipe_exception = BrokenPipeError
except:
	broken_pipe_exception = IOError

glb_switch_str		= {}
glb_insn		= False
glb_disassembler	= None
glb_src			= False
glb_source_file_name	= None
glb_line_number		= None
glb_dso			= None

def get_optional_null(perf_dict, field):
	if field in perf_dict:
		return perf_dict[field]
	return ""

def get_optional_zero(perf_dict, field):
	if field in perf_dict:
		return perf_dict[field]
	return 0

def get_optional_bytes(perf_dict, field):
	if field in perf_dict:
		return perf_dict[field]
	return bytes()

def get_optional(perf_dict, field):
	if field in perf_dict:
		return perf_dict[field]
	return "[unknown]"

def get_offset(perf_dict, field):
	if field in perf_dict:
		return "+%#x" % perf_dict[field]
	return ""

def trace_begin():
	ap = argparse.ArgumentParser(usage = "", add_help = False)
	ap.add_argument("--insn-trace", action='store_true')
	ap.add_argument("--src-trace", action='store_true')
	ap.add_argument("--all-switch-events", action='store_true')
	global glb_args
	global glb_insn
	global glb_src
	glb_args = ap.parse_args()
	if glb_args.insn_trace:
		print("Intel PT Instruction Trace")
		itrace = "i0nsepwxI"
		glb_insn = True
	elif glb_args.src_trace:
		print("Intel PT Source Trace")
		itrace = "i0nsepwxI"
		glb_insn = True
		glb_src = True
	else:
		print("Intel PT Branch Trace, Power Events, Event Trace and PTWRITE")
		itrace = "bepwxI"
	global glb_disassembler
	try:
		glb_disassembler = LibXED()
	except:
		glb_disassembler = None
	perf_set_itrace_options(perf_script_context, itrace)

def trace_end():
	print("End")

def trace_unhandled(event_name, context, event_fields_dict):
		print(' '.join(['%s=%s'%(k,str(v))for k,v in sorted(event_fields_dict.items())]))

def print_ptwrite(raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	flags = data[0]
	payload = data[1]
	exact_ip = flags & 1
	try:
		s = payload.to_bytes(8, "little").decode("ascii").rstrip("\x00")
		if not s.isprintable():
			s = ""
	except:
		s = ""
	print("IP: %u payload: %#x" % (exact_ip, payload), s, end=' ')

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

glb_cfe = ["", "INTR", "IRET", "SMI", "RSM", "SIPI", "INIT", "VMENTRY", "VMEXIT",
		"VMEXIT_INTR", "SHUTDOWN", "", "UINT", "UIRET"] + [""] * 18
glb_evd = ["", "PFA", "VMXQ", "VMXR"] + [""] * 60

def print_evt(raw_buf):
	data = struct.unpack_from("<BBH", raw_buf)
	typ = data[0] & 0x1f
	ip_flag = (data[0] & 0x80) >> 7
	vector = data[1]
	evd_cnt = data[2]
	s = glb_cfe[typ]
	if s:
		print(" cfe: %s IP: %u vector: %u" % (s, ip_flag, vector), end=' ')
	else:
		print(" cfe: %u IP: %u vector: %u" % (typ, ip_flag, vector), end=' ')
	pos = 4
	for i in range(evd_cnt):
		data = struct.unpack_from("<QQ", raw_buf)
		et = data[0] & 0x3f
		s = glb_evd[et]
		if s:
			print("%s: %#x" % (s, data[1]), end=' ')
		else:
			print("EVD_%u: %#x" % (et, data[1]), end=' ')

def print_iflag(raw_buf):
	data = struct.unpack_from("<IQ", raw_buf)
	iflag = data[0] & 1
	old_iflag = iflag ^ 1
	via_branch = data[0] & 2
	branch_ip = data[1]
	if via_branch:
		s = "via"
	else:
		s = "non"
	print("IFLAG: %u->%u %s branch" % (old_iflag, iflag, s), end=' ')

def common_start_str(comm, sample):
	ts = sample["time"]
	cpu = sample["cpu"]
	pid = sample["pid"]
	tid = sample["tid"]
	if "machine_pid" in sample:
		machine_pid = sample["machine_pid"]
		vcpu = sample["vcpu"]
		return "VM:%5d VCPU:%03d %16s %5u/%-5u [%03u] %9u.%09u  " % (machine_pid, vcpu, comm, pid, tid, cpu, ts / 1000000000, ts %1000000000)
	else:
		return "%16s %5u/%-5u [%03u] %9u.%09u  " % (comm, pid, tid, cpu, ts / 1000000000, ts %1000000000)

def print_common_start(comm, sample, name):
	flags_disp = get_optional_null(sample, "flags_disp")
	# Unused fields:
	# period      = sample["period"]
	# phys_addr   = sample["phys_addr"]
	# weight      = sample["weight"]
	# transaction = sample["transaction"]
	# cpumode     = get_optional_zero(sample, "cpumode")
	print(common_start_str(comm, sample) + "%8s  %21s" % (name, flags_disp), end=' ')

def print_instructions_start(comm, sample):
	if "x" in get_optional_null(sample, "flags"):
		print(common_start_str(comm, sample) + "x", end=' ')
	else:
		print(common_start_str(comm, sample), end='  ')

def disassem(insn, ip):
	inst = glb_disassembler.Instruction()
	glb_disassembler.SetMode(inst, 0) # Assume 64-bit
	buf = create_string_buffer(64)
	buf.value = insn
	return glb_disassembler.DisassembleOne(inst, addressof(buf), len(insn), ip)

def print_common_ip(param_dict, sample, symbol, dso):
	ip   = sample["ip"]
	offs = get_offset(param_dict, "symoff")
	if "cyc_cnt" in sample:
		cyc_cnt = sample["cyc_cnt"]
		insn_cnt = get_optional_zero(sample, "insn_cnt")
		ipc_str = "  IPC: %#.2f (%u/%u)" % (insn_cnt / cyc_cnt, insn_cnt, cyc_cnt)
	else:
		ipc_str = ""
	if glb_insn and glb_disassembler is not None:
		insn = perf_sample_insn(perf_script_context)
		if insn and len(insn):
			cnt, text = disassem(insn, ip)
			byte_str = ("%x" % ip).rjust(16)
			if sys.version_info.major >= 3:
				for k in range(cnt):
					byte_str += " %02x" % insn[k]
			else:
				for k in xrange(cnt):
					byte_str += " %02x" % ord(insn[k])
			print("%-40s  %-30s" % (byte_str, text), end=' ')
		print("%s%s (%s)" % (symbol, offs, dso), end=' ')
	else:
		print("%16x %s%s (%s)" % (ip, symbol, offs, dso), end=' ')
	if "addr_correlates_sym" in sample:
		addr   = sample["addr"]
		dso    = get_optional(sample, "addr_dso")
		symbol = get_optional(sample, "addr_symbol")
		offs   = get_offset(sample, "addr_symoff")
		print("=> %x %s%s (%s)%s" % (addr, symbol, offs, dso, ipc_str))
	else:
		print(ipc_str)

def print_srccode(comm, param_dict, sample, symbol, dso, with_insn):
	ip = sample["ip"]
	if symbol == "[unknown]":
		start_str = common_start_str(comm, sample) + ("%x" % ip).rjust(16).ljust(40)
	else:
		offs = get_offset(param_dict, "symoff")
		start_str = common_start_str(comm, sample) + (symbol + offs).ljust(40)

	if with_insn and glb_insn and glb_disassembler is not None:
		insn = perf_sample_insn(perf_script_context)
		if insn and len(insn):
			cnt, text = disassem(insn, ip)
		start_str += text.ljust(30)

	global glb_source_file_name
	global glb_line_number
	global glb_dso

	source_file_name, line_number, source_line = perf_sample_srccode(perf_script_context)
	if source_file_name:
		if glb_line_number == line_number and glb_source_file_name == source_file_name:
			src_str = ""
		else:
			if len(source_file_name) > 40:
				src_file = ("..." + source_file_name[-37:]) + " "
			else:
				src_file = source_file_name.ljust(41)
			if source_line is None:
				src_str = src_file + str(line_number).rjust(4) + " <source not found>"
			else:
				src_str = src_file + str(line_number).rjust(4) + " " + source_line
		glb_dso = None
	elif dso == glb_dso:
		src_str = ""
	else:
		src_str = dso
		glb_dso = dso

	glb_line_number = line_number
	glb_source_file_name = source_file_name

	print(start_str, src_str)

def do_process_event(param_dict):
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

	cpu = sample["cpu"]
	if cpu in glb_switch_str:
		print(glb_switch_str[cpu])
		del glb_switch_str[cpu]

	if name[0:12] == "instructions":
		if glb_src:
			print_srccode(comm, param_dict, sample, symbol, dso, True)
		else:
			print_instructions_start(comm, sample)
			print_common_ip(param_dict, sample, symbol, dso)
	elif name[0:8] == "branches":
		if glb_src:
			print_srccode(comm, param_dict, sample, symbol, dso, False)
		else:
			print_common_start(comm, sample, name)
			print_common_ip(param_dict, sample, symbol, dso)
	elif name == "ptwrite":
		print_common_start(comm, sample, name)
		print_ptwrite(raw_buf)
		print_common_ip(param_dict, sample, symbol, dso)
	elif name == "cbr":
		print_common_start(comm, sample, name)
		print_cbr(raw_buf)
		print_common_ip(param_dict, sample, symbol, dso)
	elif name == "mwait":
		print_common_start(comm, sample, name)
		print_mwait(raw_buf)
		print_common_ip(param_dict, sample, symbol, dso)
	elif name == "pwre":
		print_common_start(comm, sample, name)
		print_pwre(raw_buf)
		print_common_ip(param_dict, sample, symbol, dso)
	elif name == "exstop":
		print_common_start(comm, sample, name)
		print_exstop(raw_buf)
		print_common_ip(param_dict, sample, symbol, dso)
	elif name == "pwrx":
		print_common_start(comm, sample, name)
		print_pwrx(raw_buf)
		print_common_ip(param_dict, sample, symbol, dso)
	elif name == "psb":
		print_common_start(comm, sample, name)
		print_psb(raw_buf)
		print_common_ip(param_dict, sample, symbol, dso)
	elif name == "evt":
		print_common_start(comm, sample, name)
		print_evt(raw_buf)
		print_common_ip(param_dict, sample, symbol, dso)
	elif name == "iflag":
		print_common_start(comm, sample, name)
		print_iflag(raw_buf)
		print_common_ip(param_dict, sample, symbol, dso)
	else:
		print_common_start(comm, sample, name)
		print_common_ip(param_dict, sample, symbol, dso)

def process_event(param_dict):
	try:
		do_process_event(param_dict)
	except broken_pipe_exception:
		# Stop python printing broken pipe errors and traceback
		sys.stdout = open(os.devnull, 'w')
		sys.exit(1)

def auxtrace_error(typ, code, cpu, pid, tid, ip, ts, msg, cpumode, *x):
	if len(x) >= 2 and x[0]:
		machine_pid = x[0]
		vcpu = x[1]
	else:
		machine_pid = 0
		vcpu = -1
	try:
		if machine_pid:
			print("VM:%5d VCPU:%03d %16s %5u/%-5u [%03u] %9u.%09u  error type %u code %u: %s ip 0x%16x" %
				(machine_pid, vcpu, "Trace error", pid, tid, cpu, ts / 1000000000, ts %1000000000, typ, code, msg, ip))
		else:
			print("%16s %5u/%-5u [%03u] %9u.%09u  error type %u code %u: %s ip 0x%16x" %
				("Trace error", pid, tid, cpu, ts / 1000000000, ts %1000000000, typ, code, msg, ip))
	except broken_pipe_exception:
		# Stop python printing broken pipe errors and traceback
		sys.stdout = open(os.devnull, 'w')
		sys.exit(1)

def context_switch(ts, cpu, pid, tid, np_pid, np_tid, machine_pid, out, out_preempt, *x):
	if out:
		out_str = "Switch out "
	else:
		out_str = "Switch In  "
	if out_preempt:
		preempt_str = "preempt"
	else:
		preempt_str = ""
	if len(x) >= 2 and x[0]:
		machine_pid = x[0]
		vcpu = x[1]
	else:
		vcpu = None;
	if machine_pid == -1:
		machine_str = ""
	elif vcpu is None:
		machine_str = "machine PID %d" % machine_pid
	else:
		machine_str = "machine PID %d VCPU %d" % (machine_pid, vcpu)
	switch_str = "%16s %5d/%-5d [%03u] %9u.%09u %5d/%-5d %s %s" % \
		(out_str, pid, tid, cpu, ts / 1000000000, ts %1000000000, np_pid, np_tid, machine_str, preempt_str)
	if glb_args.all_switch_events:
		print(switch_str)
	else:
		global glb_switch_str
		glb_switch_str[cpu] = switch_str
