#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Run a perf script command multiple times in parallel, using perf script
# options --cpu and --time so that each job processes a different chunk
# of the data.
#
# Copyright (c) 2024, Intel Corporation.

import subprocess
import argparse
import pathlib
import shlex
import time
import copy
import sys
import os
import re

glb_prog_name = "parallel-perf.py"
glb_min_interval = 10.0
glb_min_samples = 64

class Verbosity():

	def __init__(self, quiet=False, verbose=False, debug=False):
		self.normal    = True
		self.verbose   = verbose
		self.debug     = debug
		self.self_test = True
		if self.debug:
			self.verbose = True
		if self.verbose:
			quiet = False
		if quiet:
			self.normal = False

# Manage work (Start/Wait/Kill), as represented by a subprocess.Popen command
class Work():

	def __init__(self, cmd, pipe_to, output_dir="."):
		self.popen = None
		self.consumer = None
		self.cmd = cmd
		self.pipe_to = pipe_to
		self.output_dir = output_dir
		self.cmdout_name = f"{output_dir}/cmd.txt"
		self.stdout_name = f"{output_dir}/out.txt"
		self.stderr_name = f"{output_dir}/err.txt"

	def Command(self):
		sh_cmd = [ shlex.quote(x) for x in self.cmd ]
		return " ".join(self.cmd)

	def Stdout(self):
		return open(self.stdout_name, "w")

	def Stderr(self):
		return open(self.stderr_name, "w")

	def CreateOutputDir(self):
		pathlib.Path(self.output_dir).mkdir(parents=True, exist_ok=True)

	def Start(self):
		if self.popen:
			return
		self.CreateOutputDir()
		with open(self.cmdout_name, "w") as f:
			f.write(self.Command())
			f.write("\n")
		stdout = self.Stdout()
		stderr = self.Stderr()
		if self.pipe_to:
			self.popen = subprocess.Popen(self.cmd, stdout=subprocess.PIPE, stderr=stderr)
			args = shlex.split(self.pipe_to)
			self.consumer = subprocess.Popen(args, stdin=self.popen.stdout, stdout=stdout, stderr=stderr)
		else:
			self.popen = subprocess.Popen(self.cmd, stdout=stdout, stderr=stderr)

	def RemoveEmptyErrFile(self):
		if os.path.exists(self.stderr_name):
			if os.path.getsize(self.stderr_name) == 0:
				os.unlink(self.stderr_name)

	def Errors(self):
		if os.path.exists(self.stderr_name):
			if os.path.getsize(self.stderr_name) != 0:
				return [ f"Non-empty error file {self.stderr_name}" ]
		return []

	def TidyUp(self):
		self.RemoveEmptyErrFile()

	def RawPollWait(self, p, wait):
		if wait:
			return p.wait()
		return p.poll()

	def Poll(self, wait=False):
		if not self.popen:
			return None
		result = self.RawPollWait(self.popen, wait)
		if self.consumer:
			res = result
			result = self.RawPollWait(self.consumer, wait)
			if result != None and res == None:
				self.popen.kill()
				result = None
			elif result == 0 and res != None and res != 0:
				result = res
		if result != None:
			self.TidyUp()
		return result

	def Wait(self):
		return self.Poll(wait=True)

	def Kill(self):
		if not self.popen:
			return
		self.popen.kill()
		if self.consumer:
			self.consumer.kill()

def KillWork(worklist, verbosity):
	for w in worklist:
		w.Kill()
	for w in worklist:
		w.Wait()

def NumberOfCPUs():
	return os.sysconf("SC_NPROCESSORS_ONLN")

def NanoSecsToSecsStr(x):
	if x == None:
		return ""
	x = str(x)
	if len(x) < 10:
		x = "0" * (10 - len(x)) + x
	return x[:len(x) - 9] + "." + x[-9:]

def InsertOptionAfter(cmd, option, after):
	try:
		pos = cmd.index(after)
		cmd.insert(pos + 1, option)
	except:
		cmd.append(option)

def CreateWorkList(cmd, pipe_to, output_dir, cpus, time_ranges_by_cpu):
	max_len = len(str(cpus[-1]))
	cpu_dir_fmt = f"cpu-%.{max_len}u"
	worklist = []
	pos = 0
	for cpu in cpus:
		if cpu >= 0:
			cpu_dir = os.path.join(output_dir, cpu_dir_fmt % cpu)
			cpu_option = f"--cpu={cpu}"
		else:
			cpu_dir = output_dir
			cpu_option = None

		tr_dir_fmt = "time-range"

		if len(time_ranges_by_cpu) > 1:
			time_ranges = time_ranges_by_cpu[pos]
			tr_dir_fmt += f"-{pos}"
			pos += 1
		else:
			time_ranges = time_ranges_by_cpu[0]

		max_len = len(str(len(time_ranges)))
		tr_dir_fmt += f"-%.{max_len}u"

		i = 0
		for r in time_ranges:
			if r == [None, None]:
				time_option = None
				work_output_dir = cpu_dir
			else:
				time_option = "--time=" + NanoSecsToSecsStr(r[0]) + "," + NanoSecsToSecsStr(r[1])
				work_output_dir = os.path.join(cpu_dir, tr_dir_fmt % i)
				i += 1
			work_cmd = list(cmd)
			if time_option != None:
				InsertOptionAfter(work_cmd, time_option, "script")
			if cpu_option != None:
				InsertOptionAfter(work_cmd, cpu_option, "script")
			w = Work(work_cmd, pipe_to, work_output_dir)
			worklist.append(w)
	return worklist

def DoRunWork(worklist, nr_jobs, verbosity):
	nr_to_do = len(worklist)
	not_started = list(worklist)
	running = []
	done = []
	chg = False
	while True:
		nr_done = len(done)
		if chg and verbosity.normal:
			nr_run = len(running)
			print(f"\rThere are {nr_to_do} jobs: {nr_done} completed, {nr_run} running", flush=True, end=" ")
			if verbosity.verbose:
				print()
			chg = False
		if nr_done == nr_to_do:
			break
		while len(running) < nr_jobs and len(not_started):
			w = not_started.pop(0)
			running.append(w)
			if verbosity.verbose:
				print("Starting:", w.Command())
			w.Start()
			chg = True
		if len(running):
			time.sleep(0.1)
		finished = []
		not_finished = []
		while len(running):
			w = running.pop(0)
			r = w.Poll()
			if r == None:
				not_finished.append(w)
				continue
			if r == 0:
				if verbosity.verbose:
					print("Finished:", w.Command())
				finished.append(w)
				chg = True
				continue
			if verbosity.normal and not verbosity.verbose:
				print()
			print("Job failed!\n    return code:", r, "\n    command:    ", w.Command())
			if w.pipe_to:
				print("    piped to:   ", w.pipe_to)
			print("Killing outstanding jobs")
			KillWork(not_finished, verbosity)
			KillWork(running, verbosity)
			return False
		running = not_finished
		done += finished
	errorlist = []
	for w in worklist:
		errorlist += w.Errors()
	if len(errorlist):
		print("Errors:")
		for e in errorlist:
			print(e)
	elif verbosity.normal:
		print("\r"," "*50, "\rAll jobs finished successfully", flush=True)
	return True

def RunWork(worklist, nr_jobs=NumberOfCPUs(), verbosity=Verbosity()):
	try:
		return DoRunWork(worklist, nr_jobs, verbosity)
	except:
		for w in worklist:
			w.Kill()
		raise
	return True

def ReadHeader(perf, file_name):
	return subprocess.Popen([perf, "script", "--header-only", "--input", file_name], stdout=subprocess.PIPE).stdout.read().decode("utf-8")

def ParseHeader(hdr):
	result = {}
	lines = hdr.split("\n")
	for line in lines:
		if ":" in line and line[0] == "#":
			pos = line.index(":")
			name = line[1:pos-1].strip()
			value = line[pos+1:].strip()
			if name in result:
				orig_name = name
				nr = 2
				while True:
					name = f"{orig_name} {nr}"
					if name not in result:
						break
					nr += 1
			result[name] = value
	return result

def HeaderField(hdr_dict, hdr_fld):
	if hdr_fld not in hdr_dict:
		raise Exception(f"'{hdr_fld}' missing from header information")
	return hdr_dict[hdr_fld]

# Represent the position of an option within a command string
# and provide the option value and/or remove the option
class OptPos():

	def Init(self, opt_element=-1, value_element=-1, opt_pos=-1, value_pos=-1, error=None):
		self.opt_element = opt_element		# list element that contains option
		self.value_element = value_element	# list element that contains option value
		self.opt_pos = opt_pos			# string position of option
		self.value_pos = value_pos		# string position of value
		self.error = error			# error message string

	def __init__(self, args, short_name, long_name, default=None):
		self.args = list(args)
		self.default = default
		n = 2 + len(long_name)
		m = len(short_name)
		pos = -1
		for opt in args:
			pos += 1
			if m and opt[:2] == f"-{short_name}":
				if len(opt) == 2:
					if pos + 1 < len(args):
						self.Init(pos, pos + 1, 0, 0)
					else:
						self.Init(error = f"-{short_name} option missing value")
				else:
					self.Init(pos, pos, 0, 2)
				return
			if opt[:n] == f"--{long_name}":
				if len(opt) == n:
					if pos + 1 < len(args):
						self.Init(pos, pos + 1, 0, 0)
					else:
						self.Init(error = f"--{long_name} option missing value")
				elif opt[n] == "=":
					self.Init(pos, pos, 0, n + 1)
				else:
					self.Init(error = f"--{long_name} option expected '='")
				return
			if m and opt[:1] == "-" and opt[:2] != "--" and short_name in opt:
				ipos = opt.index(short_name)
				if "-" in opt[1:]:
					hpos = opt[1:].index("-")
					if hpos < ipos:
						continue
				if ipos + 1 == len(opt):
					if pos + 1 < len(args):
						self.Init(pos, pos + 1, ipos, 0)
					else:
						self.Init(error = f"-{short_name} option missing value")
				else:
					self.Init(pos, pos, ipos, ipos + 1)
				return
		self.Init()

	def Value(self):
		if self.opt_element >= 0:
			if self.opt_element != self.value_element:
				return self.args[self.value_element]
			else:
				return self.args[self.value_element][self.value_pos:]
		return self.default

	def Remove(self, args):
		if self.opt_element == -1:
			return
		if self.opt_element != self.value_element:
			del args[self.value_element]
		if self.opt_pos:
			args[self.opt_element] = args[self.opt_element][:self.opt_pos]
		else:
			del args[self.opt_element]

def DetermineInputFileName(cmd):
	p = OptPos(cmd, "i", "input", "perf.data")
	if p.error:
		raise Exception(f"perf command {p.error}")
	file_name = p.Value()
	if not os.path.exists(file_name):
		raise Exception(f"perf command input file '{file_name}' not found")
	return file_name

def ReadOption(args, short_name, long_name, err_prefix, remove=False):
	p = OptPos(args, short_name, long_name)
	if p.error:
		raise Exception(f"{err_prefix}{p.error}")
	value = p.Value()
	if remove:
		p.Remove(args)
	return value

def ExtractOption(args, short_name, long_name, err_prefix):
	return ReadOption(args, short_name, long_name, err_prefix, True)

def ReadPerfOption(args, short_name, long_name):
	return ReadOption(args, short_name, long_name, "perf command ")

def ExtractPerfOption(args, short_name, long_name):
	return ExtractOption(args, short_name, long_name, "perf command ")

def PerfDoubleQuickCommands(cmd, file_name):
	cpu_str = ReadPerfOption(cmd, "C", "cpu")
	time_str = ReadPerfOption(cmd, "", "time")
	# Use double-quick sampling to determine trace data density
	times_cmd = ["perf", "script", "--ns", "--input", file_name, "--itrace=qqi"]
	if cpu_str != None and cpu_str != "":
		times_cmd.append(f"--cpu={cpu_str}")
	if time_str != None and time_str != "":
		times_cmd.append(f"--time={time_str}")
	cnts_cmd = list(times_cmd)
	cnts_cmd.append("-Fcpu")
	times_cmd.append("-Fcpu,time")
	return cnts_cmd, times_cmd

class CPUTimeRange():
	def __init__(self, cpu):
		self.cpu = cpu
		self.sample_cnt = 0
		self.time_ranges = None
		self.interval = 0
		self.interval_remaining = 0
		self.remaining = 0
		self.tr_pos = 0

def CalcTimeRangesByCPU(line, cpu, cpu_time_ranges, max_time):
	cpu_time_range = cpu_time_ranges[cpu]
	cpu_time_range.remaining -= 1
	cpu_time_range.interval_remaining -= 1
	if cpu_time_range.remaining == 0:
		cpu_time_range.time_ranges[cpu_time_range.tr_pos][1] = max_time
		return
	if cpu_time_range.interval_remaining == 0:
		time = TimeVal(line[1][:-1], 0)
		time_ranges = cpu_time_range.time_ranges
		time_ranges[cpu_time_range.tr_pos][1] = time - 1
		time_ranges.append([time, max_time])
		cpu_time_range.tr_pos += 1
		cpu_time_range.interval_remaining = cpu_time_range.interval

def CountSamplesByCPU(line, cpu, cpu_time_ranges):
	try:
		cpu_time_ranges[cpu].sample_cnt += 1
	except:
		print("exception")
		print("cpu", cpu)
		print("len(cpu_time_ranges)", len(cpu_time_ranges))
		raise

def ProcessCommandOutputLines(cmd, per_cpu, fn, *x):
	# Assume CPU number is at beginning of line and enclosed by []
	pat = re.compile(r"\s*\[[0-9]+\]")
	p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
	while True:
		if line := p.stdout.readline():
			line = line.decode("utf-8")
			if pat.match(line):
				line = line.split()
				if per_cpu:
					# Assumes CPU number is enclosed by []
					cpu = int(line[0][1:-1])
				else:
					cpu = 0
				fn(line, cpu, *x)
		else:
			break
	p.wait()

def IntersectTimeRanges(new_time_ranges, time_ranges):
	pos = 0
	new_pos = 0
	# Can assume len(time_ranges) != 0 and len(new_time_ranges) != 0
	# Note also, there *must* be at least one intersection.
	while pos < len(time_ranges) and new_pos < len(new_time_ranges):
		# new end < old start => no intersection, remove new
		if new_time_ranges[new_pos][1] < time_ranges[pos][0]:
			del new_time_ranges[new_pos]
			continue
		# new start > old end => no intersection, check next
		if new_time_ranges[new_pos][0] > time_ranges[pos][1]:
			pos += 1
			if pos < len(time_ranges):
				continue
			# no next, so remove remaining
			while new_pos < len(new_time_ranges):
				del new_time_ranges[new_pos]
			return
		# Found an intersection
		# new start < old start => adjust new start = old start
		if new_time_ranges[new_pos][0] < time_ranges[pos][0]:
			new_time_ranges[new_pos][0] = time_ranges[pos][0]
		# new end > old end => keep the overlap, insert the remainder
		if new_time_ranges[new_pos][1] > time_ranges[pos][1]:
			r = [ time_ranges[pos][1] + 1, new_time_ranges[new_pos][1] ]
			new_time_ranges[new_pos][1] = time_ranges[pos][1]
			new_pos += 1
			new_time_ranges.insert(new_pos, r)
			continue
		# new [start, end] is within old [start, end]
		new_pos += 1

def SplitTimeRangesByTraceDataDensity(time_ranges, cpus, nr, cmd, file_name, per_cpu, min_size, min_interval, verbosity):
	if verbosity.normal:
		print("\rAnalyzing...", flush=True, end=" ")
		if verbosity.verbose:
			print()
	cnts_cmd, times_cmd = PerfDoubleQuickCommands(cmd, file_name)

	nr_cpus = cpus[-1] + 1 if per_cpu else 1
	if per_cpu:
		nr_cpus = cpus[-1] + 1
		cpu_time_ranges = [ CPUTimeRange(cpu) for cpu in range(nr_cpus) ]
	else:
		nr_cpus = 1
		cpu_time_ranges = [ CPUTimeRange(-1) ]

	if verbosity.debug:
		print("nr_cpus", nr_cpus)
		print("cnts_cmd", cnts_cmd)
		print("times_cmd", times_cmd)

	# Count the number of "double quick" samples per CPU
	ProcessCommandOutputLines(cnts_cmd, per_cpu, CountSamplesByCPU, cpu_time_ranges)

	tot = 0
	mx = 0
	for cpu_time_range in cpu_time_ranges:
		cnt = cpu_time_range.sample_cnt
		tot += cnt
		if cnt > mx:
			mx = cnt
		if verbosity.debug:
			print("cpu:", cpu_time_range.cpu, "sample_cnt", cnt)

	if min_size < 1:
		min_size = 1

	if mx < min_size:
		# Too little data to be worth splitting
		if verbosity.debug:
			print("Too little data to split by time")
		if nr == 0:
			nr = 1
		return [ SplitTimeRangesIntoN(time_ranges, nr, min_interval) ]

	if nr:
		divisor = nr
		min_size = 1
	else:
		divisor = NumberOfCPUs()

	interval = int(round(tot / divisor, 0))
	if interval < min_size:
		interval = min_size

	if verbosity.debug:
		print("divisor", divisor)
		print("min_size", min_size)
		print("interval", interval)

	min_time = time_ranges[0][0]
	max_time = time_ranges[-1][1]

	for cpu_time_range in cpu_time_ranges:
		cnt = cpu_time_range.sample_cnt
		if cnt == 0:
			cpu_time_range.time_ranges = copy.deepcopy(time_ranges)
			continue
		# Adjust target interval for CPU to give approximately equal interval sizes
		# Determine number of intervals, rounding to nearest integer
		n = int(round(cnt / interval, 0))
		if n < 1:
			n = 1
		# Determine interval size, rounding up
		d, m = divmod(cnt, n)
		if m:
			d += 1
		cpu_time_range.interval = d
		cpu_time_range.interval_remaining = d
		cpu_time_range.remaining = cnt
		# Init. time ranges for each CPU with the start time
		cpu_time_range.time_ranges = [ [min_time, max_time] ]

	# Set time ranges so that the same number of "double quick" samples
	# will fall into each time range.
	ProcessCommandOutputLines(times_cmd, per_cpu, CalcTimeRangesByCPU, cpu_time_ranges, max_time)

	for cpu_time_range in cpu_time_ranges:
		if cpu_time_range.sample_cnt:
			IntersectTimeRanges(cpu_time_range.time_ranges, time_ranges)

	return [cpu_time_ranges[cpu].time_ranges for cpu in cpus]

def SplitSingleTimeRangeIntoN(time_range, n):
	if n <= 1:
		return [time_range]
	start = time_range[0]
	end   = time_range[1]
	duration = int((end - start + 1) / n)
	if duration < 1:
		return [time_range]
	time_ranges = []
	for i in range(n):
		time_ranges.append([start, start + duration - 1])
		start += duration
	time_ranges[-1][1] = end
	return time_ranges

def TimeRangeDuration(r):
	return r[1] - r[0] + 1

def TotalDuration(time_ranges):
	duration = 0
	for r in time_ranges:
		duration += TimeRangeDuration(r)
	return duration

def SplitTimeRangesByInterval(time_ranges, interval):
	new_ranges = []
	for r in time_ranges:
		duration = TimeRangeDuration(r)
		n = duration / interval
		n = int(round(n, 0))
		new_ranges += SplitSingleTimeRangeIntoN(r, n)
	return new_ranges

def SplitTimeRangesIntoN(time_ranges, n, min_interval):
	if n <= len(time_ranges):
		return time_ranges
	duration = TotalDuration(time_ranges)
	interval = duration / n
	if interval < min_interval:
		interval = min_interval
	return SplitTimeRangesByInterval(time_ranges, interval)

def RecombineTimeRanges(tr):
	new_tr = copy.deepcopy(tr)
	n = len(new_tr)
	i = 1
	while i < len(new_tr):
		# if prev end + 1 == cur start, combine them
		if new_tr[i - 1][1] + 1 == new_tr[i][0]:
			new_tr[i][0] = new_tr[i - 1][0]
			del new_tr[i - 1]
		else:
			i += 1
	return new_tr

def OpenTimeRangeEnds(time_ranges, min_time, max_time):
	if time_ranges[0][0] <= min_time:
		time_ranges[0][0] = None
	if time_ranges[-1][1] >= max_time:
		time_ranges[-1][1] = None

def BadTimeStr(time_str):
	raise Exception(f"perf command bad time option: '{time_str}'\nCheck also 'time of first sample' and 'time of last sample' in perf script --header-only")

def ValidateTimeRanges(time_ranges, time_str):
	n = len(time_ranges)
	for i in range(n):
		start = time_ranges[i][0]
		end   = time_ranges[i][1]
		if i != 0 and start <= time_ranges[i - 1][1]:
			BadTimeStr(time_str)
		if start > end:
			BadTimeStr(time_str)

def TimeVal(s, dflt):
	s = s.strip()
	if s == "":
		return dflt
	a = s.split(".")
	if len(a) > 2:
		raise Exception(f"Bad time value'{s}'")
	x = int(a[0])
	if x < 0:
		raise Exception("Negative time not allowed")
	x *= 1000000000
	if len(a) > 1:
		x += int((a[1] + "000000000")[:9])
	return x

def BadCPUStr(cpu_str):
	raise Exception(f"perf command bad cpu option: '{cpu_str}'\nCheck also 'nrcpus avail' in perf script --header-only")

def ParseTimeStr(time_str, min_time, max_time):
	if time_str == None or time_str == "":
		return [[min_time, max_time]]
	time_ranges = []
	for r in time_str.split():
		a = r.split(",")
		if len(a) != 2:
			BadTimeStr(time_str)
		try:
			start = TimeVal(a[0], min_time)
			end   = TimeVal(a[1], max_time)
		except:
			BadTimeStr(time_str)
		time_ranges.append([start, end])
	ValidateTimeRanges(time_ranges, time_str)
	return time_ranges

def ParseCPUStr(cpu_str, nr_cpus):
	if cpu_str == None or cpu_str == "":
		return [-1]
	cpus = []
	for r in cpu_str.split(","):
		a = r.split("-")
		if len(a) < 1 or len(a) > 2:
			BadCPUStr(cpu_str)
		try:
			start = int(a[0].strip())
			if len(a) > 1:
				end = int(a[1].strip())
			else:
				end = start
		except:
			BadCPUStr(cpu_str)
		if start < 0 or end < 0 or end < start or end >= nr_cpus:
			BadCPUStr(cpu_str)
		cpus.extend(range(start, end + 1))
	cpus = list(set(cpus)) # Remove duplicates
	cpus.sort()
	return cpus

class ParallelPerf():

	def __init__(self, a):
		for arg_name in vars(a):
			setattr(self, arg_name, getattr(a, arg_name))
		self.orig_nr = self.nr
		self.orig_cmd = list(self.cmd)
		self.perf = self.cmd[0]
		if os.path.exists(self.output_dir):
			raise Exception(f"Output '{self.output_dir}' already exists")
		if self.jobs < 0 or self.nr < 0 or self.interval < 0:
			raise Exception("Bad options (negative values): try -h option for help")
		if self.nr != 0 and self.interval != 0:
			raise Exception("Cannot specify number of time subdivisions and time interval")
		if self.jobs == 0:
			self.jobs = NumberOfCPUs()
		if self.nr == 0 and self.interval == 0:
			if self.per_cpu:
				self.nr = 1
			else:
				self.nr = self.jobs

	def Init(self):
		if self.verbosity.debug:
			print("cmd", self.cmd)
		self.file_name = DetermineInputFileName(self.cmd)
		self.hdr = ReadHeader(self.perf, self.file_name)
		self.hdr_dict = ParseHeader(self.hdr)
		self.cmd_line = HeaderField(self.hdr_dict, "cmdline")

	def ExtractTimeInfo(self):
		self.min_time = TimeVal(HeaderField(self.hdr_dict, "time of first sample"), 0)
		self.max_time = TimeVal(HeaderField(self.hdr_dict, "time of last sample"), 0)
		self.time_str = ExtractPerfOption(self.cmd, "", "time")
		self.time_ranges = ParseTimeStr(self.time_str, self.min_time, self.max_time)
		if self.verbosity.debug:
			print("time_ranges", self.time_ranges)

	def ExtractCPUInfo(self):
		if self.per_cpu:
			nr_cpus = int(HeaderField(self.hdr_dict, "nrcpus avail"))
			self.cpu_str = ExtractPerfOption(self.cmd, "C", "cpu")
			if self.cpu_str == None or self.cpu_str == "":
				self.cpus = [ x for x in range(nr_cpus) ]
			else:
				self.cpus = ParseCPUStr(self.cpu_str, nr_cpus)
		else:
			self.cpu_str = None
			self.cpus = [-1]
		if self.verbosity.debug:
			print("cpus", self.cpus)

	def IsIntelPT(self):
		return self.cmd_line.find("intel_pt") >= 0

	def SplitTimeRanges(self):
		if self.IsIntelPT() and self.interval == 0:
			self.split_time_ranges_for_each_cpu = \
				SplitTimeRangesByTraceDataDensity(self.time_ranges, self.cpus, self.orig_nr,
								  self.orig_cmd, self.file_name, self.per_cpu,
								  self.min_size, self.min_interval, self.verbosity)
		elif self.nr:
			self.split_time_ranges_for_each_cpu = [ SplitTimeRangesIntoN(self.time_ranges, self.nr, self.min_interval) ]
		else:
			self.split_time_ranges_for_each_cpu = [ SplitTimeRangesByInterval(self.time_ranges, self.interval) ]

	def CheckTimeRanges(self):
		for tr in self.split_time_ranges_for_each_cpu:
			# Re-combined time ranges should be the same
			new_tr = RecombineTimeRanges(tr)
			if new_tr != self.time_ranges:
				if self.verbosity.debug:
					print("tr", tr)
					print("new_tr", new_tr)
				raise Exception("Self test failed!")

	def OpenTimeRangeEnds(self):
		for time_ranges in self.split_time_ranges_for_each_cpu:
			OpenTimeRangeEnds(time_ranges, self.min_time, self.max_time)

	def CreateWorkList(self):
		self.worklist = CreateWorkList(self.cmd, self.pipe_to, self.output_dir, self.cpus, self.split_time_ranges_for_each_cpu)

	def PerfDataRecordedPerCPU(self):
		if "--per-thread" in self.cmd_line.split():
			return False
		return True

	def DefaultToPerCPU(self):
		# --no-per-cpu option takes precedence
		if self.no_per_cpu:
			return False
		if not self.PerfDataRecordedPerCPU():
			return False
		# Default to per-cpu for Intel PT data that was recorded per-cpu,
		# because decoding can be done for each CPU separately.
		if self.IsIntelPT():
			return True
		return False

	def Config(self):
		self.Init()
		self.ExtractTimeInfo()
		if not self.per_cpu:
			self.per_cpu = self.DefaultToPerCPU()
		if self.verbosity.debug:
			print("per_cpu", self.per_cpu)
		self.ExtractCPUInfo()
		self.SplitTimeRanges()
		if self.verbosity.self_test:
			self.CheckTimeRanges()
		# Prefer open-ended time range to starting / ending with min_time / max_time resp.
		self.OpenTimeRangeEnds()
		self.CreateWorkList()

	def Run(self):
		if self.dry_run:
			print(len(self.worklist),"jobs:")
			for w in self.worklist:
				print(w.Command())
			return True
		result = RunWork(self.worklist, self.jobs, verbosity=self.verbosity)
		if self.verbosity.verbose:
			print(glb_prog_name, "done")
		return result

def RunParallelPerf(a):
	pp = ParallelPerf(a)
	pp.Config()
	return pp.Run()

def Main(args):
	ap = argparse.ArgumentParser(
		prog=glb_prog_name, formatter_class = argparse.RawDescriptionHelpFormatter,
		description =
"""
Run a perf script command multiple times in parallel, using perf script options
--cpu and --time so that each job processes a different chunk of the data.
""",
		epilog =
"""
Follow the options by '--' and then the perf script command e.g.

	$ perf record -a -- sleep 10
	$ parallel-perf.py --nr=4 -- perf script --ns
	All jobs finished successfully
	$ tree parallel-perf-output/
	parallel-perf-output/
	├── time-range-0
	│   ├── cmd.txt
	│   └── out.txt
	├── time-range-1
	│   ├── cmd.txt
	│   └── out.txt
	├── time-range-2
	│   ├── cmd.txt
	│   └── out.txt
	└── time-range-3
	    ├── cmd.txt
	    └── out.txt
	$ find parallel-perf-output -name cmd.txt | sort | xargs grep -H .
	parallel-perf-output/time-range-0/cmd.txt:perf script --time=,9466.504461499 --ns
	parallel-perf-output/time-range-1/cmd.txt:perf script --time=9466.504461500,9469.005396999 --ns
	parallel-perf-output/time-range-2/cmd.txt:perf script --time=9469.005397000,9471.506332499 --ns
	parallel-perf-output/time-range-3/cmd.txt:perf script --time=9471.506332500, --ns

Any perf script command can be used, including the use of perf script options
--dlfilter and --script, so that the benefit of running parallel jobs
naturally extends to them also.

If option --pipe-to is used, standard output is first piped through that
command. Beware, if the command fails (e.g. grep with no matches), it will be
considered a fatal error.

Final standard output is redirected to files named out.txt in separate
subdirectories under the output directory. Similarly, standard error is
written to files named err.txt. In addition, files named cmd.txt contain the
corresponding perf script command. After processing, err.txt files are removed
if they are empty.

If any job exits with a non-zero exit code, then all jobs are killed and no
more are started. A message is printed if any job results in a non-empty
err.txt file.

There is a separate output subdirectory for each time range. If the --per-cpu
option is used, these are further grouped under cpu-n subdirectories, e.g.

	$ parallel-perf.py --per-cpu --nr=2 -- perf script --ns --cpu=0,1
	All jobs finished successfully
	$ tree parallel-perf-output
	parallel-perf-output/
	├── cpu-0
	│   ├── time-range-0
	│   │   ├── cmd.txt
	│   │   └── out.txt
	│   └── time-range-1
	│       ├── cmd.txt
	│       └── out.txt
	└── cpu-1
	    ├── time-range-0
	    │   ├── cmd.txt
	    │   └── out.txt
	    └── time-range-1
	        ├── cmd.txt
	        └── out.txt
	$ find parallel-perf-output -name cmd.txt | sort | xargs grep -H .
	parallel-perf-output/cpu-0/time-range-0/cmd.txt:perf script --cpu=0 --time=,9469.005396999 --ns
	parallel-perf-output/cpu-0/time-range-1/cmd.txt:perf script --cpu=0 --time=9469.005397000, --ns
	parallel-perf-output/cpu-1/time-range-0/cmd.txt:perf script --cpu=1 --time=,9469.005396999 --ns
	parallel-perf-output/cpu-1/time-range-1/cmd.txt:perf script --cpu=1 --time=9469.005397000, --ns

Subdivisions of time range, and cpus if the --per-cpu option is used, are
expressed by the --time and --cpu perf script options respectively. If the
supplied perf script command has a --time option, then that time range is
subdivided, otherwise the time range given by 'time of first sample' to
'time of last sample' is used (refer perf script --header-only). Similarly, the
supplied perf script command may provide a --cpu option, and only those CPUs
will be processed.

To prevent time intervals becoming too small, the --min-interval option can
be used.

Note there is special handling for processing Intel PT traces. If an interval is
not specified and the perf record command contained the intel_pt event, then the
time range will be subdivided in order to produce subdivisions that contain
approximately the same amount of trace data. That is accomplished by counting
double-quick (--itrace=qqi) samples, and choosing time ranges that encompass
approximately the same number of samples. In that case, time ranges may not be
the same for each CPU processed. For Intel PT, --per-cpu is the default, but
that can be overridden by --no-per-cpu. Note, for Intel PT, double-quick
decoding produces 1 sample for each PSB synchronization packet, which in turn
come after a certain number of bytes output, determined by psb_period (refer
perf Intel PT documentation). The minimum number of double-quick samples that
will define a time range can be set by the --min_size option, which defaults to
64.
""")
	ap.add_argument("-o", "--output-dir", default="parallel-perf-output", help="output directory (default 'parallel-perf-output')")
	ap.add_argument("-j", "--jobs", type=int, default=0, help="maximum number of jobs to run in parallel at one time (default is the number of CPUs)")
	ap.add_argument("-n", "--nr", type=int, default=0, help="number of time subdivisions (default is the number of jobs)")
	ap.add_argument("-i", "--interval", type=float, default=0, help="subdivide the time range using this time interval (in seconds e.g. 0.1 for a tenth of a second)")
	ap.add_argument("-c", "--per-cpu", action="store_true", help="process data for each CPU in parallel")
	ap.add_argument("-m", "--min-interval", type=float, default=glb_min_interval, help=f"minimum interval (default {glb_min_interval} seconds)")
	ap.add_argument("-p", "--pipe-to", help="command to pipe output to (optional)")
	ap.add_argument("-N", "--no-per-cpu", action="store_true", help="do not process data for each CPU in parallel")
	ap.add_argument("-b", "--min_size", type=int, default=glb_min_samples, help="minimum data size (for Intel PT in PSBs)")
	ap.add_argument("-D", "--dry-run", action="store_true", help="do not run any jobs, just show the perf script commands")
	ap.add_argument("-q", "--quiet", action="store_true", help="do not print any messages except errors")
	ap.add_argument("-v", "--verbose", action="store_true", help="print more messages")
	ap.add_argument("-d", "--debug", action="store_true", help="print debugging messages")
	cmd_line = list(args)
	try:
		split_pos = cmd_line.index("--")
		cmd = cmd_line[split_pos + 1:]
		args = cmd_line[:split_pos]
	except:
		cmd = None
		args = cmd_line
	a = ap.parse_args(args=args[1:])
	a.cmd = cmd
	a.verbosity = Verbosity(a.quiet, a.verbose, a.debug)
	try:
		if a.cmd == None:
			if len(args) <= 1:
				ap.print_help()
				return True
			raise Exception("Command line must contain '--' before perf command")
		return RunParallelPerf(a)
	except Exception as e:
		print("Fatal error: ", str(e))
		if a.debug:
			raise
		return False

if __name__ == "__main__":
	if not Main(sys.argv):
		sys.exit(1)
