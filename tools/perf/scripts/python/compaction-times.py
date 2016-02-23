# report time spent in compaction
# Licensed under the terms of the GNU GPL License version 2

# testing:
# 'echo 1 > /proc/sys/vm/compact_memory' to force compaction of all zones

import os
import sys
import re

import signal
signal.signal(signal.SIGPIPE, signal.SIG_DFL)

usage = "usage: perf script report compaction-times.py -- [-h] [-u] [-p|-pv] [-t | [-m] [-fs] [-ms]] [pid|pid-range|comm-regex]\n"

class popt:
	DISP_DFL = 0
	DISP_PROC = 1
	DISP_PROC_VERBOSE=2

class topt:
	DISP_TIME = 0
	DISP_MIG = 1
	DISP_ISOLFREE = 2
	DISP_ISOLMIG = 4
	DISP_ALL = 7

class comm_filter:
	def __init__(self, re):
		self.re = re

	def filter(self, pid, comm):
		m = self.re.search(comm)
		return m == None or m.group() == ""

class pid_filter:
	def __init__(self, low, high):
		self.low = (0 if low == "" else int(low))
		self.high = (0 if high == "" else int(high))

	def filter(self, pid, comm):
		return not (pid >= self.low and (self.high == 0 or pid <= self.high))

def set_type(t):
	global opt_disp
	opt_disp = (t if opt_disp == topt.DISP_ALL else opt_disp|t)

def ns(sec, nsec):
	return (sec * 1000000000) + nsec

def time(ns):
	return "%dns" % ns if opt_ns else "%dus" % (round(ns, -3) / 1000)

class pair:
	def __init__(self, aval, bval, alabel = None, blabel = None):
		self.alabel = alabel
		self.blabel = blabel
		self.aval = aval
		self.bval = bval

	def __add__(self, rhs):
		self.aval += rhs.aval
		self.bval += rhs.bval
		return self

	def __str__(self):
		return "%s=%d %s=%d" % (self.alabel, self.aval, self.blabel, self.bval)

class cnode:
	def __init__(self, ns):
		self.ns = ns
		self.migrated = pair(0, 0, "moved", "failed")
		self.fscan = pair(0,0, "scanned", "isolated")
		self.mscan = pair(0,0, "scanned", "isolated")

	def __add__(self, rhs):
		self.ns += rhs.ns
		self.migrated += rhs.migrated
		self.fscan += rhs.fscan
		self.mscan += rhs.mscan
		return self

	def __str__(self):
		prev = 0
		s = "%s " % time(self.ns)
		if (opt_disp & topt.DISP_MIG):
			s += "migration: %s" % self.migrated
			prev = 1
		if (opt_disp & topt.DISP_ISOLFREE):
			s += "%sfree_scanner: %s" % (" " if prev else "", self.fscan)
			prev = 1
		if (opt_disp & topt.DISP_ISOLMIG):
			s += "%smigration_scanner: %s" % (" " if prev else "", self.mscan)
		return s

	def complete(self, secs, nsecs):
		self.ns = ns(secs, nsecs) - self.ns

	def increment(self, migrated, fscan, mscan):
		if (migrated != None):
			self.migrated += migrated
		if (fscan != None):
			self.fscan += fscan
		if (mscan != None):
			self.mscan += mscan


class chead:
	heads = {}
	val = cnode(0);
	fobj = None

	@classmethod
	def add_filter(cls, filter):
		cls.fobj = filter

	@classmethod
	def create_pending(cls, pid, comm, start_secs, start_nsecs):
		filtered = 0
		try:
			head = cls.heads[pid]
			filtered = head.is_filtered()
		except KeyError:
			if cls.fobj != None:
				filtered = cls.fobj.filter(pid, comm)
			head = cls.heads[pid] = chead(comm, pid, filtered)

		if not filtered:
			head.mark_pending(start_secs, start_nsecs)

	@classmethod
	def increment_pending(cls, pid, migrated, fscan, mscan):
		head = cls.heads[pid]
		if not head.is_filtered():
			if head.is_pending():
				head.do_increment(migrated, fscan, mscan)
			else:
				sys.stderr.write("missing start compaction event for pid %d\n" % pid)

	@classmethod
	def complete_pending(cls, pid, secs, nsecs):
		head = cls.heads[pid]
		if not head.is_filtered():
			if head.is_pending():
				head.make_complete(secs, nsecs)
			else:
				sys.stderr.write("missing start compaction event for pid %d\n" % pid)

	@classmethod
	def gen(cls):
		if opt_proc != popt.DISP_DFL:
			for i in cls.heads:
				yield cls.heads[i]

	@classmethod
	def str(cls):
		return cls.val

	def __init__(self, comm, pid, filtered):
		self.comm = comm
		self.pid = pid
		self.val = cnode(0)
		self.pending = None
		self.filtered = filtered
		self.list = []

	def __add__(self, rhs):
		self.ns += rhs.ns
		self.val += rhs.val
		return self

	def mark_pending(self, secs, nsecs):
		self.pending = cnode(ns(secs, nsecs))

	def do_increment(self, migrated, fscan, mscan):
		self.pending.increment(migrated, fscan, mscan)

	def make_complete(self, secs, nsecs):
		self.pending.complete(secs, nsecs)
		chead.val += self.pending

		if opt_proc != popt.DISP_DFL:
			self.val += self.pending

			if opt_proc == popt.DISP_PROC_VERBOSE:
				self.list.append(self.pending)
		self.pending = None

	def enumerate(self):
		if opt_proc == popt.DISP_PROC_VERBOSE and not self.is_filtered():
			for i, pelem in enumerate(self.list):
				sys.stdout.write("%d[%s].%d: %s\n" % (self.pid, self.comm, i+1, pelem))

	def is_pending(self):
		return self.pending != None

	def is_filtered(self):
		return self.filtered

	def display(self):
		if not self.is_filtered():
			sys.stdout.write("%d[%s]: %s\n" % (self.pid, self.comm, self.val))


def trace_end():
	sys.stdout.write("total: %s\n" % chead.str())
	for i in chead.gen():
		i.display(),
		i.enumerate()

def compaction__mm_compaction_migratepages(event_name, context, common_cpu,
	common_secs, common_nsecs, common_pid, common_comm,
	common_callchain, nr_migrated, nr_failed):

	chead.increment_pending(common_pid,
		pair(nr_migrated, nr_failed), None, None)

def compaction__mm_compaction_isolate_freepages(event_name, context, common_cpu,
        common_secs, common_nsecs, common_pid, common_comm,
        common_callchain, start_pfn, end_pfn, nr_scanned, nr_taken):

	chead.increment_pending(common_pid,
		None, pair(nr_scanned, nr_taken), None)

def compaction__mm_compaction_isolate_migratepages(event_name, context, common_cpu,
        common_secs, common_nsecs, common_pid, common_comm,
        common_callchain, start_pfn, end_pfn, nr_scanned, nr_taken):

	chead.increment_pending(common_pid,
		None, None, pair(nr_scanned, nr_taken))

def compaction__mm_compaction_end(event_name, context, common_cpu,
	common_secs, common_nsecs, common_pid, common_comm,
	common_callchain, zone_start, migrate_start, free_start, zone_end,
	sync, status):

	chead.complete_pending(common_pid, common_secs, common_nsecs)

def compaction__mm_compaction_begin(event_name, context, common_cpu,
	common_secs, common_nsecs, common_pid, common_comm,
	common_callchain, zone_start, migrate_start, free_start, zone_end,
	sync):

	chead.create_pending(common_pid, common_comm, common_secs, common_nsecs)

def pr_help():
	global usage

	sys.stdout.write(usage)
	sys.stdout.write("\n")
	sys.stdout.write("-h	display this help\n")
	sys.stdout.write("-p	display by process\n")
	sys.stdout.write("-pv	display by process (verbose)\n")
	sys.stdout.write("-t	display stall times only\n")
	sys.stdout.write("-m	display stats for migration\n")
	sys.stdout.write("-fs	display stats for free scanner\n")
	sys.stdout.write("-ms	display stats for migration scanner\n")
	sys.stdout.write("-u	display results in microseconds (default nanoseconds)\n")


comm_re = None
pid_re = None
pid_regex = "^(\d*)-(\d*)$|^(\d*)$"

opt_proc = popt.DISP_DFL
opt_disp = topt.DISP_ALL

opt_ns = True

argc = len(sys.argv) - 1
if argc >= 1:
	pid_re = re.compile(pid_regex)

	for i, opt in enumerate(sys.argv[1:]):
		if opt[0] == "-":
			if opt == "-h":
				pr_help()
				exit(0);
			elif opt == "-p":
				opt_proc = popt.DISP_PROC
			elif opt == "-pv":
				opt_proc = popt.DISP_PROC_VERBOSE
			elif opt == '-u':
				opt_ns = False
			elif opt == "-t":
				set_type(topt.DISP_TIME)
			elif opt == "-m":
				set_type(topt.DISP_MIG)
			elif opt == "-fs":
				set_type(topt.DISP_ISOLFREE)
			elif opt == "-ms":
				set_type(topt.DISP_ISOLMIG)
			else:
				sys.exit(usage)

		elif i == argc - 1:
			m = pid_re.search(opt)
			if m != None and m.group() != "":
				if m.group(3) != None:
					f = pid_filter(m.group(3), m.group(3))
				else:
					f = pid_filter(m.group(1), m.group(2))
			else:
				try:
					comm_re=re.compile(opt)
				except:
					sys.stderr.write("invalid regex '%s'" % opt)
					sys.exit(usage)
				f = comm_filter(comm_re)

			chead.add_filter(f)
