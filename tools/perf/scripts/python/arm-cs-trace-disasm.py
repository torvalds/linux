# SPDX-License-Identifier: GPL-2.0
# arm-cs-trace-disasm.py: ARM CoreSight Trace Dump With Disassember
#
# Author: Tor Jeremiassen <tor@ti.com>
#         Mathieu Poirier <mathieu.poirier@linaro.org>
#         Leo Yan <leo.yan@linaro.org>
#         Al Grant <Al.Grant@arm.com>

from __future__ import print_function
import os
from os import path
import re
from subprocess import *
import argparse
import platform

from perf_trace_context import perf_sample_srccode, perf_config_get

# Below are some example commands for using this script.
# Note a --kcore recording is required for accurate decode
# due to the alternatives patching mechanism. However this
# script only supports reading vmlinux for disassembly dump,
# meaning that any patched instructions will appear
# as unpatched, but the instruction ranges themselves will
# be correct. In addition to this, source line info comes
# from Perf, and when using kcore there is no debug info. The
# following lists the supported features in each mode:
#
# +-----------+-----------------+------------------+------------------+
# | Recording | Accurate decode | Source line dump | Disassembly dump |
# +-----------+-----------------+------------------+------------------+
# | --kcore   | yes             | no               | yes              |
# | normal    | no              | yes              | yes              |
# +-----------+-----------------+------------------+------------------+
#
# Output disassembly with objdump and auto detect vmlinux
# (when running on same machine.)
#  perf script -s scripts/python/arm-cs-trace-disasm.py -d
#
# Output disassembly with llvm-objdump:
#  perf script -s scripts/python/arm-cs-trace-disasm.py \
#		-- -d llvm-objdump-11 -k path/to/vmlinux
#
# Output only source line and symbols:
#  perf script -s scripts/python/arm-cs-trace-disasm.py

def default_objdump():
	config = perf_config_get("annotate.objdump")
	return config if config else "objdump"

# Command line parsing.
def int_arg(v):
	v = int(v)
	if v < 0:
		raise argparse.ArgumentTypeError("Argument must be a positive integer")
	return v

args = argparse.ArgumentParser()
args.add_argument("-k", "--vmlinux",
		  help="Set path to vmlinux file. Omit to autodetect if running on same machine")
args.add_argument("-d", "--objdump", nargs="?", const=default_objdump(),
		  help="Show disassembly. Can also be used to change the objdump path"),
args.add_argument("-v", "--verbose", action="store_true", help="Enable debugging log")
args.add_argument("--start-time", type=int_arg, help="Monotonic clock time of sample to start from. "
		  "See 'time' field on samples in -v mode.")
args.add_argument("--stop-time", type=int_arg, help="Monotonic clock time of sample to stop at. "
		  "See 'time' field on samples in -v mode.")
args.add_argument("--start-sample", type=int_arg, help="Index of sample to start from. "
		  "See 'index' field on samples in -v mode.")
args.add_argument("--stop-sample", type=int_arg, help="Index of sample to stop at. "
		  "See 'index' field on samples in -v mode.")

options = args.parse_args()
if (options.start_time and options.stop_time and
    options.start_time >= options.stop_time):
	print("--start-time must less than --stop-time")
	exit(2)
if (options.start_sample and options.stop_sample and
    options.start_sample >= options.stop_sample):
	print("--start-sample must less than --stop-sample")
	exit(2)

# Initialize global dicts and regular expression
disasm_cache = dict()
cpu_data = dict()
disasm_re = re.compile(r"^\s*([0-9a-fA-F]+):")
disasm_func_re = re.compile(r"^\s*([0-9a-fA-F]+)\s.*:")
cache_size = 64*1024
sample_idx = -1

glb_source_file_name	= None
glb_line_number		= None
glb_dso			= None

kver = platform.release()
vmlinux_paths = [
	f"/usr/lib/debug/boot/vmlinux-{kver}.debug",
	f"/usr/lib/debug/lib/modules/{kver}/vmlinux",
	f"/lib/modules/{kver}/build/vmlinux",
	f"/usr/lib/debug/boot/vmlinux-{kver}",
	f"/boot/vmlinux-{kver}",
	f"/boot/vmlinux",
	f"vmlinux"
]

def get_optional(perf_dict, field):
       if field in perf_dict:
               return perf_dict[field]
       return "[unknown]"

def get_offset(perf_dict, field):
	if field in perf_dict:
		return "+%#x" % perf_dict[field]
	return ""

def find_vmlinux():
	if hasattr(find_vmlinux, "path"):
		return find_vmlinux.path

	for v in vmlinux_paths:
		if os.access(v, os.R_OK):
			find_vmlinux.path = v
			break
	else:
		find_vmlinux.path = None

	return find_vmlinux.path

def get_dso_file_path(dso_name, dso_build_id):
	if (dso_name == "[kernel.kallsyms]" or dso_name == "vmlinux"):
		if (options.vmlinux):
			return options.vmlinux;
		else:
			return find_vmlinux() if find_vmlinux() else dso_name

	if (dso_name == "[vdso]") :
		append = "/vdso"
	else:
		append = "/elf"

	dso_path = os.environ['PERF_BUILDID_DIR'] + "/" + dso_name + "/" + dso_build_id + append;
	# Replace duplicate slash chars to single slash char
	dso_path = dso_path.replace('//', '/', 1)
	return dso_path

def read_disam(dso_fname, dso_start, start_addr, stop_addr):
	addr_range = str(start_addr) + ":" + str(stop_addr) + ":" + dso_fname

	# Don't let the cache get too big, clear it when it hits max size
	if (len(disasm_cache) > cache_size):
		disasm_cache.clear();

	if addr_range in disasm_cache:
		disasm_output = disasm_cache[addr_range];
	else:
		start_addr = start_addr - dso_start;
		stop_addr = stop_addr - dso_start;
		disasm = [ options.objdump, "-d", "-z",
			   "--start-address="+format(start_addr,"#x"),
			   "--stop-address="+format(stop_addr,"#x") ]
		disasm += [ dso_fname ]
		disasm_output = check_output(disasm).decode('utf-8').split('\n')
		disasm_cache[addr_range] = disasm_output

	return disasm_output

def print_disam(dso_fname, dso_start, start_addr, stop_addr):
	for line in read_disam(dso_fname, dso_start, start_addr, stop_addr):
		m = disasm_func_re.search(line)
		if m is None:
			m = disasm_re.search(line)
			if m is None:
				continue
		print("\t" + line)

def print_sample(sample):
	print("Sample = { cpu: %04d addr: 0x%016x phys_addr: 0x%016x ip: 0x%016x " \
	      "pid: %d tid: %d period: %d time: %d index: %d}" % \
	      (sample['cpu'], sample['addr'], sample['phys_addr'], \
	       sample['ip'], sample['pid'], sample['tid'], \
	       sample['period'], sample['time'], sample_idx))

def trace_begin():
	print('ARM CoreSight Trace Data Assembler Dump')

def trace_end():
	print('End')

def trace_unhandled(event_name, context, event_fields_dict):
	print(' '.join(['%s=%s'%(k,str(v))for k,v in sorted(event_fields_dict.items())]))

def common_start_str(comm, sample):
	sec = int(sample["time"] / 1000000000)
	ns = sample["time"] % 1000000000
	cpu = sample["cpu"]
	pid = sample["pid"]
	tid = sample["tid"]
	return "%16s %5u/%-5u [%04u] %9u.%09u  " % (comm, pid, tid, cpu, sec, ns)

# This code is copied from intel-pt-events.py for printing source code
# line and symbols.
def print_srccode(comm, param_dict, sample, symbol, dso):
	ip = sample["ip"]
	if symbol == "[unknown]":
		start_str = common_start_str(comm, sample) + ("%x" % ip).rjust(16).ljust(40)
	else:
		offs = get_offset(param_dict, "symoff")
		start_str = common_start_str(comm, sample) + (symbol + offs).ljust(40)

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

def process_event(param_dict):
	global cache_size
	global options
	global sample_idx

	sample = param_dict["sample"]
	comm = param_dict["comm"]

	name = param_dict["ev_name"]
	dso = get_optional(param_dict, "dso")
	dso_bid = get_optional(param_dict, "dso_bid")
	dso_start = get_optional(param_dict, "dso_map_start")
	dso_end = get_optional(param_dict, "dso_map_end")
	symbol = get_optional(param_dict, "symbol")
	map_pgoff = get_optional(param_dict, "map_pgoff")
	# check for valid map offset
	if (str(map_pgoff) == '[unknown]'):
		map_pgoff = 0

	cpu = sample["cpu"]
	ip = sample["ip"]
	addr = sample["addr"]

	sample_idx += 1

	if (options.start_time and sample["time"] < options.start_time):
		return
	if (options.stop_time and sample["time"] > options.stop_time):
		exit(0)
	if (options.start_sample and sample_idx < options.start_sample):
		return
	if (options.stop_sample and sample_idx > options.stop_sample):
		exit(0)

	if (options.verbose == True):
		print("Event type: %s" % name)
		print_sample(sample)

	# Initialize CPU data if it's empty, and directly return back
	# if this is the first tracing event for this CPU.
	if (cpu_data.get(str(cpu) + 'addr') == None):
		cpu_data[str(cpu) + 'addr'] = addr
		return

	# If cannot find dso so cannot dump assembler, bail out
	if (dso == '[unknown]'):
		return

	# Validate dso start and end addresses
	if ((dso_start == '[unknown]') or (dso_end == '[unknown]')):
		print("Failed to find valid dso map for dso %s" % dso)
		return

	if (name[0:12] == "instructions"):
		print_srccode(comm, param_dict, sample, symbol, dso)
		return

	# Don't proceed if this event is not a branch sample, .
	if (name[0:8] != "branches"):
		return

	# The format for packet is:
	#
	#		  +------------+------------+------------+
	#  sample_prev:   |    addr    |    ip	    |	 cpu	 |
	#		  +------------+------------+------------+
	#  sample_next:   |    addr    |    ip	    |	 cpu	 |
	#		  +------------+------------+------------+
	#
	# We need to combine the two continuous packets to get the instruction
	# range for sample_prev::cpu:
	#
	#     [ sample_prev::addr .. sample_next::ip ]
	#
	# For this purose, sample_prev::addr is stored into cpu_data structure
	# and read back for 'start_addr' when the new packet comes, and we need
	# to use sample_next::ip to calculate 'stop_addr', plusing extra 4 for
	# 'stop_addr' is for the sake of objdump so the final assembler dump can
	# include last instruction for sample_next::ip.
	start_addr = cpu_data[str(cpu) + 'addr']
	stop_addr  = ip + 4

	# Record for previous sample packet
	cpu_data[str(cpu) + 'addr'] = addr

	# Filter out zero start_address. Optionally identify CS_ETM_TRACE_ON packet
	if (start_addr == 0):
		if ((stop_addr == 4) and (options.verbose == True)):
			print("CPU%d: CS_ETM_TRACE_ON packet is inserted" % cpu)
		return

	if (start_addr < int(dso_start) or start_addr > int(dso_end)):
		print("Start address 0x%x is out of range [ 0x%x .. 0x%x ] for dso %s" % (start_addr, int(dso_start), int(dso_end), dso))
		return

	if (stop_addr < int(dso_start) or stop_addr > int(dso_end)):
		print("Stop address 0x%x is out of range [ 0x%x .. 0x%x ] for dso %s" % (stop_addr, int(dso_start), int(dso_end), dso))
		return

	if (options.objdump != None):
		# It doesn't need to decrease virtual memory offset for disassembly
		# for kernel dso and executable file dso, so in this case we set
		# vm_start to zero.
		if (dso == "[kernel.kallsyms]" or dso_start == 0x400000):
			dso_vm_start = 0
			map_pgoff = 0
		else:
			dso_vm_start = int(dso_start)

		dso_fname = get_dso_file_path(dso, dso_bid)
		if path.exists(dso_fname):
			print_disam(dso_fname, dso_vm_start, start_addr + map_pgoff, stop_addr + map_pgoff)
		else:
			print("Failed to find dso %s for address range [ 0x%x .. 0x%x ]" % (dso, start_addr + map_pgoff, stop_addr + map_pgoff))

	print_srccode(comm, param_dict, sample, symbol, dso)
