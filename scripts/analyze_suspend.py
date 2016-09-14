#!/usr/bin/python
#
# Tool for analyzing suspend/resume timing
# Copyright (c) 2013, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
#
# Authors:
#	 Todd Brandt <todd.e.brandt@linux.intel.com>
#
# Links:
#	 Home Page
#	   https://01.org/suspendresume
#	 Source repo
#	   https://github.com/01org/suspendresume
#	 Documentation
#	   Getting Started
#	     https://01.org/suspendresume/documentation/getting-started
#	   Command List:
#	     https://01.org/suspendresume/documentation/command-list
#
# Description:
#	 This tool is designed to assist kernel and OS developers in optimizing
#	 their linux stack's suspend/resume time. Using a kernel image built
#	 with a few extra options enabled, the tool will execute a suspend and
#	 will capture dmesg and ftrace data until resume is complete. This data
#	 is transformed into a device timeline and a callgraph to give a quick
#	 and detailed view of which devices and callbacks are taking the most
#	 time in suspend/resume. The output is a single html file which can be
#	 viewed in firefox or chrome.
#
#	 The following kernel build options are required:
#		 CONFIG_PM_DEBUG=y
#		 CONFIG_PM_SLEEP_DEBUG=y
#		 CONFIG_FTRACE=y
#		 CONFIG_FUNCTION_TRACER=y
#		 CONFIG_FUNCTION_GRAPH_TRACER=y
#		 CONFIG_KPROBES=y
#		 CONFIG_KPROBES_ON_FTRACE=y
#
#	 For kernel versions older than 3.15:
#	 The following additional kernel parameters are required:
#		 (e.g. in file /etc/default/grub)
#		 GRUB_CMDLINE_LINUX_DEFAULT="... initcall_debug log_buf_len=16M ..."
#

# ----------------- LIBRARIES --------------------

import sys
import time
import os
import string
import re
import platform
from datetime import datetime
import struct
import ConfigParser

# ----------------- CLASSES --------------------

# Class: SystemValues
# Description:
#	 A global, single-instance container used to
#	 store system values and test parameters
class SystemValues:
	ansi = False
	version = '4.2'
	verbose = False
	addlogs = False
	mindevlen = 0.001
	mincglen = 1.0
	srgap = 0
	cgexp = False
	outdir = ''
	testdir = '.'
	tpath = '/sys/kernel/debug/tracing/'
	fpdtpath = '/sys/firmware/acpi/tables/FPDT'
	epath = '/sys/kernel/debug/tracing/events/power/'
	traceevents = [
		'suspend_resume',
		'device_pm_callback_end',
		'device_pm_callback_start'
	]
	testcommand = ''
	mempath = '/dev/mem'
	powerfile = '/sys/power/state'
	suspendmode = 'mem'
	hostname = 'localhost'
	prefix = 'test'
	teststamp = ''
	dmesgstart = 0.0
	dmesgfile = ''
	ftracefile = ''
	htmlfile = ''
	embedded = False
	rtcwake = False
	rtcwaketime = 10
	rtcpath = ''
	devicefilter = []
	stamp = 0
	execcount = 1
	x2delay = 0
	usecallgraph = False
	usetraceevents = False
	usetraceeventsonly = False
	usetracemarkers = True
	usekprobes = True
	usedevsrc = False
	notestrun = False
	devprops = dict()
	postresumetime = 0
	devpropfmt = '# Device Properties: .*'
	tracertypefmt = '# tracer: (?P<t>.*)'
	firmwarefmt = '# fwsuspend (?P<s>[0-9]*) fwresume (?P<r>[0-9]*)$'
	postresumefmt = '# post resume time (?P<t>[0-9]*)$'
	stampfmt = '# suspend-(?P<m>[0-9]{2})(?P<d>[0-9]{2})(?P<y>[0-9]{2})-'+\
				'(?P<H>[0-9]{2})(?P<M>[0-9]{2})(?P<S>[0-9]{2})'+\
				' (?P<host>.*) (?P<mode>.*) (?P<kernel>.*)$'
	kprobecolor = 'rgba(204,204,204,0.5)'
	synccolor = 'rgba(204,204,204,0.5)'
	debugfuncs = []
	tracefuncs = {
		'sys_sync': dict(),
		'pm_prepare_console': dict(),
		'pm_notifier_call_chain': dict(),
		'freeze_processes': dict(),
		'freeze_kernel_threads': dict(),
		'pm_restrict_gfp_mask': dict(),
		'acpi_suspend_begin': dict(),
		'suspend_console': dict(),
		'acpi_pm_prepare': dict(),
		'syscore_suspend': dict(),
		'arch_enable_nonboot_cpus_end': dict(),
		'syscore_resume': dict(),
		'acpi_pm_finish': dict(),
		'resume_console': dict(),
		'acpi_pm_end': dict(),
		'pm_restore_gfp_mask': dict(),
		'thaw_processes': dict(),
		'pm_restore_console': dict(),
		'CPU_OFF': {
			'func':'_cpu_down',
			'args_x86_64': {'cpu':'%di:s32'},
			'format': 'CPU_OFF[{cpu}]',
			'mask': 'CPU_.*_DOWN'
		},
		'CPU_ON': {
			'func':'_cpu_up',
			'args_x86_64': {'cpu':'%di:s32'},
			'format': 'CPU_ON[{cpu}]',
			'mask': 'CPU_.*_UP'
		},
	}
	dev_tracefuncs = {
		# general wait/delay/sleep
		'msleep': { 'args_x86_64': {'time':'%di:s32'} },
		'udelay': { 'func':'__const_udelay', 'args_x86_64': {'loops':'%di:s32'} },
		'acpi_os_stall': dict(),
		# ACPI
		'acpi_resume_power_resources': dict(),
		'acpi_ps_parse_aml': dict(),
		# filesystem
		'ext4_sync_fs': dict(),
		# ATA
		'ata_eh_recover': { 'args_x86_64': {'port':'+36(%di):s32'} },
		# i915
		'i915_gem_restore_gtt_mappings': dict(),
		'intel_opregion_setup': dict(),
		'intel_dp_detect': dict(),
		'intel_hdmi_detect': dict(),
		'intel_opregion_init': dict(),
	}
	kprobes_postresume = [
		{
			'name': 'ataportrst',
			'func': 'ata_eh_recover',
			'args': {'port':'+36(%di):s32'},
			'format': 'ata{port}_port_reset',
			'mask': 'ata.*_port_reset'
		}
	]
	kprobes = dict()
	timeformat = '%.3f'
	def __init__(self):
		# if this is a phoronix test run, set some default options
		if('LOG_FILE' in os.environ and 'TEST_RESULTS_IDENTIFIER' in os.environ):
			self.embedded = True
			self.addlogs = True
			self.htmlfile = os.environ['LOG_FILE']
		self.hostname = platform.node()
		if(self.hostname == ''):
			self.hostname = 'localhost'
		rtc = "rtc0"
		if os.path.exists('/dev/rtc'):
			rtc = os.readlink('/dev/rtc')
		rtc = '/sys/class/rtc/'+rtc
		if os.path.exists(rtc) and os.path.exists(rtc+'/date') and \
			os.path.exists(rtc+'/time') and os.path.exists(rtc+'/wakealarm'):
			self.rtcpath = rtc
		if (hasattr(sys.stdout, 'isatty') and sys.stdout.isatty()):
			self.ansi = True
	def setPrecision(self, num):
		if num < 0 or num > 6:
			return
		self.timeformat = '%.{0}f'.format(num)
	def setOutputFile(self):
		if((self.htmlfile == '') and (self.dmesgfile != '')):
			m = re.match('(?P<name>.*)_dmesg\.txt$', self.dmesgfile)
			if(m):
				self.htmlfile = m.group('name')+'.html'
		if((self.htmlfile == '') and (self.ftracefile != '')):
			m = re.match('(?P<name>.*)_ftrace\.txt$', self.ftracefile)
			if(m):
				self.htmlfile = m.group('name')+'.html'
		if(self.htmlfile == ''):
			self.htmlfile = 'output.html'
	def initTestOutput(self, subdir, testpath=''):
		self.prefix = self.hostname
		v = open('/proc/version', 'r').read().strip()
		kver = string.split(v)[2]
		n = datetime.now()
		testtime = n.strftime('suspend-%m%d%y-%H%M%S')
		if not testpath:
			testpath = n.strftime('suspend-%y%m%d-%H%M%S')
		if(subdir != "."):
			self.testdir = subdir+"/"+testpath
		else:
			self.testdir = testpath
		self.teststamp = \
			'# '+testtime+' '+self.prefix+' '+self.suspendmode+' '+kver
		if(self.embedded):
			self.dmesgfile = \
				'/tmp/'+testtime+'_'+self.suspendmode+'_dmesg.txt'
			self.ftracefile = \
				'/tmp/'+testtime+'_'+self.suspendmode+'_ftrace.txt'
			return
		self.dmesgfile = \
			self.testdir+'/'+self.prefix+'_'+self.suspendmode+'_dmesg.txt'
		self.ftracefile = \
			self.testdir+'/'+self.prefix+'_'+self.suspendmode+'_ftrace.txt'
		self.htmlfile = \
			self.testdir+'/'+self.prefix+'_'+self.suspendmode+'.html'
		if not os.path.isdir(self.testdir):
			os.mkdir(self.testdir)
	def setDeviceFilter(self, devnames):
		self.devicefilter = string.split(devnames)
	def rtcWakeAlarmOn(self):
		os.system('echo 0 > '+self.rtcpath+'/wakealarm')
		outD = open(self.rtcpath+'/date', 'r').read().strip()
		outT = open(self.rtcpath+'/time', 'r').read().strip()
		mD = re.match('^(?P<y>[0-9]*)-(?P<m>[0-9]*)-(?P<d>[0-9]*)', outD)
		mT = re.match('^(?P<h>[0-9]*):(?P<m>[0-9]*):(?P<s>[0-9]*)', outT)
		if(mD and mT):
			# get the current time from hardware
			utcoffset = int((datetime.now() - datetime.utcnow()).total_seconds())
			dt = datetime(\
				int(mD.group('y')), int(mD.group('m')), int(mD.group('d')),
				int(mT.group('h')), int(mT.group('m')), int(mT.group('s')))
			nowtime = int(dt.strftime('%s')) + utcoffset
		else:
			# if hardware time fails, use the software time
			nowtime = int(datetime.now().strftime('%s'))
		alarm = nowtime + self.rtcwaketime
		os.system('echo %d > %s/wakealarm' % (alarm, self.rtcpath))
	def rtcWakeAlarmOff(self):
		os.system('echo 0 > %s/wakealarm' % self.rtcpath)
	def initdmesg(self):
		# get the latest time stamp from the dmesg log
		fp = os.popen('dmesg')
		ktime = '0'
		for line in fp:
			line = line.replace('\r\n', '')
			idx = line.find('[')
			if idx > 1:
				line = line[idx:]
			m = re.match('[ \t]*(\[ *)(?P<ktime>[0-9\.]*)(\]) (?P<msg>.*)', line)
			if(m):
				ktime = m.group('ktime')
		fp.close()
		self.dmesgstart = float(ktime)
	def getdmesg(self):
		# store all new dmesg lines since initdmesg was called
		fp = os.popen('dmesg')
		op = open(self.dmesgfile, 'a')
		for line in fp:
			line = line.replace('\r\n', '')
			idx = line.find('[')
			if idx > 1:
				line = line[idx:]
			m = re.match('[ \t]*(\[ *)(?P<ktime>[0-9\.]*)(\]) (?P<msg>.*)', line)
			if(not m):
				continue
			ktime = float(m.group('ktime'))
			if ktime > self.dmesgstart:
				op.write(line)
		fp.close()
		op.close()
	def addFtraceFilterFunctions(self, file):
		fp = open(file)
		list = fp.read().split('\n')
		fp.close()
		for i in list:
			if len(i) < 2:
				continue
			self.tracefuncs[i] = dict()
	def getFtraceFilterFunctions(self, current):
		rootCheck(True)
		if not current:
			os.system('cat '+self.tpath+'available_filter_functions')
			return
		fp = open(self.tpath+'available_filter_functions')
		master = fp.read().split('\n')
		fp.close()
		if len(self.debugfuncs) > 0:
			for i in self.debugfuncs:
				if i in master:
					print i
				else:
					print self.colorText(i)
		else:
			for i in self.tracefuncs:
				if 'func' in self.tracefuncs[i]:
					i = self.tracefuncs[i]['func']
				if i in master:
					print i
				else:
					print self.colorText(i)
	def setFtraceFilterFunctions(self, list):
		fp = open(self.tpath+'available_filter_functions')
		master = fp.read().split('\n')
		fp.close()
		flist = ''
		for i in list:
			if i not in master:
				continue
			if ' [' in i:
				flist += i.split(' ')[0]+'\n'
			else:
				flist += i+'\n'
		fp = open(self.tpath+'set_graph_function', 'w')
		fp.write(flist)
		fp.close()
	def kprobeMatch(self, name, target):
		if name not in self.kprobes:
			return False
		if re.match(self.kprobes[name]['mask'], target):
			return True
		return False
	def basicKprobe(self, name):
		self.kprobes[name] = {'name': name,'func': name,'args': dict(),'format': name,'mask': name}
	def defaultKprobe(self, name, kdata):
		k = kdata
		for field in ['name', 'format', 'mask', 'func']:
			if field not in k:
				k[field] = name
		archargs = 'args_'+platform.machine()
		if archargs in k:
			k['args'] = k[archargs]
		else:
			k['args'] = dict()
			k['format'] = name
		self.kprobes[name] = k
	def kprobeColor(self, name):
		if name not in self.kprobes or 'color' not in self.kprobes[name]:
			return ''
		return self.kprobes[name]['color']
	def kprobeDisplayName(self, name, dataraw):
		if name not in self.kprobes:
			self.basicKprobe(name)
		data = ''
		quote=0
		# first remvoe any spaces inside quotes, and the quotes
		for c in dataraw:
			if c == '"':
				quote = (quote + 1) % 2
			if quote and c == ' ':
				data += '_'
			elif c != '"':
				data += c
		fmt, args = self.kprobes[name]['format'], self.kprobes[name]['args']
		arglist = dict()
		# now process the args
		for arg in sorted(args):
			arglist[arg] = ''
			m = re.match('.* '+arg+'=(?P<arg>.*) ', data);
			if m:
				arglist[arg] = m.group('arg')
			else:
				m = re.match('.* '+arg+'=(?P<arg>.*)', data);
				if m:
					arglist[arg] = m.group('arg')
		out = fmt.format(**arglist)
		out = out.replace(' ', '_').replace('"', '')
		return out
	def kprobeText(self, kprobe):
		name, fmt, func, args = kprobe['name'], kprobe['format'], kprobe['func'], kprobe['args']
		if re.findall('{(?P<n>[a-z,A-Z,0-9]*)}', func):
			doError('Kprobe "%s" has format info in the function name "%s"' % (name, func), False)
		for arg in re.findall('{(?P<n>[a-z,A-Z,0-9]*)}', fmt):
			if arg not in args:
				doError('Kprobe "%s" is missing argument "%s"' % (name, arg), False)
		val = 'p:%s_cal %s' % (name, func)
		for i in sorted(args):
			val += ' %s=%s' % (i, args[i])
		val += '\nr:%s_ret %s $retval\n' % (name, func)
		return val
	def addKprobes(self):
		# first test each kprobe
		print('INITIALIZING KPROBES...')
		rejects = []
		for name in sorted(self.kprobes):
			if not self.testKprobe(self.kprobes[name]):
				rejects.append(name)
		# remove all failed ones from the list
		for name in rejects:
			vprint('Skipping KPROBE: %s' % name)
			self.kprobes.pop(name)
		self.fsetVal('', 'kprobe_events')
		kprobeevents = ''
		# set the kprobes all at once
		for kp in self.kprobes:
			val = self.kprobeText(self.kprobes[kp])
			vprint('Adding KPROBE: %s\n%s' % (kp, val.strip()))
			kprobeevents += self.kprobeText(self.kprobes[kp])
		self.fsetVal(kprobeevents, 'kprobe_events')
		# verify that the kprobes were set as ordered
		check = self.fgetVal('kprobe_events')
		linesout = len(kprobeevents.split('\n'))
		linesack = len(check.split('\n'))
		if linesack < linesout:
			# if not, try appending the kprobes 1 by 1
			for kp in self.kprobes:
				kprobeevents = self.kprobeText(self.kprobes[kp])
				self.fsetVal(kprobeevents, 'kprobe_events', 'a')
		self.fsetVal('1', 'events/kprobes/enable')
	def testKprobe(self, kprobe):
		kprobeevents = self.kprobeText(kprobe)
		if not kprobeevents:
			return False
		try:
			self.fsetVal(kprobeevents, 'kprobe_events')
			check = self.fgetVal('kprobe_events')
		except:
			return False
		linesout = len(kprobeevents.split('\n'))
		linesack = len(check.split('\n'))
		if linesack < linesout:
			return False
		return True
	def fsetVal(self, val, path, mode='w'):
		file = self.tpath+path
		if not os.path.exists(file):
			return False
		try:
			fp = open(file, mode)
			fp.write(val)
			fp.close()
		except:
			pass
		return True
	def fgetVal(self, path):
		file = self.tpath+path
		res = ''
		if not os.path.exists(file):
			return res
		try:
			fp = open(file, 'r')
			res = fp.read()
			fp.close()
		except:
			pass
		return res
	def cleanupFtrace(self):
		if(self.usecallgraph or self.usetraceevents):
			self.fsetVal('0', 'events/kprobes/enable')
			self.fsetVal('', 'kprobe_events')
	def setupAllKprobes(self):
		for name in self.tracefuncs:
			self.defaultKprobe(name, self.tracefuncs[name])
		for name in self.dev_tracefuncs:
			self.defaultKprobe(name, self.dev_tracefuncs[name])
	def isCallgraphFunc(self, name):
		if len(self.debugfuncs) < 1 and self.suspendmode == 'command':
			return True
		if name in self.debugfuncs:
			return True
		funclist = []
		for i in self.tracefuncs:
			if 'func' in self.tracefuncs[i]:
				funclist.append(self.tracefuncs[i]['func'])
			else:
				funclist.append(i)
		if name in funclist:
			return True
		return False
	def initFtrace(self, testing=False):
		tp = self.tpath
		print('INITIALIZING FTRACE...')
		# turn trace off
		self.fsetVal('0', 'tracing_on')
		self.cleanupFtrace()
		# set the trace clock to global
		self.fsetVal('global', 'trace_clock')
		# set trace buffer to a huge value
		self.fsetVal('nop', 'current_tracer')
		self.fsetVal('100000', 'buffer_size_kb')
		# go no further if this is just a status check
		if testing:
			return
		if self.usekprobes:
			# add tracefunc kprobes so long as were not using full callgraph
			if(not self.usecallgraph or len(self.debugfuncs) > 0):
				for name in self.tracefuncs:
					self.defaultKprobe(name, self.tracefuncs[name])
				if self.usedevsrc:
					for name in self.dev_tracefuncs:
						self.defaultKprobe(name, self.dev_tracefuncs[name])
			else:
				self.usedevsrc = False
			self.addKprobes()
		# initialize the callgraph trace, unless this is an x2 run
		if(self.usecallgraph):
			# set trace type
			self.fsetVal('function_graph', 'current_tracer')
			self.fsetVal('', 'set_ftrace_filter')
			# set trace format options
			self.fsetVal('print-parent', 'trace_options')
			self.fsetVal('funcgraph-abstime', 'trace_options')
			self.fsetVal('funcgraph-cpu', 'trace_options')
			self.fsetVal('funcgraph-duration', 'trace_options')
			self.fsetVal('funcgraph-proc', 'trace_options')
			self.fsetVal('funcgraph-tail', 'trace_options')
			self.fsetVal('nofuncgraph-overhead', 'trace_options')
			self.fsetVal('context-info', 'trace_options')
			self.fsetVal('graph-time', 'trace_options')
			self.fsetVal('0', 'max_graph_depth')
			if len(self.debugfuncs) > 0:
				self.setFtraceFilterFunctions(self.debugfuncs)
			elif self.suspendmode == 'command':
				self.fsetVal('', 'set_graph_function')
			else:
				cf = ['dpm_run_callback']
				if(self.usetraceeventsonly):
					cf += ['dpm_prepare', 'dpm_complete']
				for fn in self.tracefuncs:
					if 'func' in self.tracefuncs[fn]:
						cf.append(self.tracefuncs[fn]['func'])
					else:
						cf.append(fn)
				self.setFtraceFilterFunctions(cf)
		if(self.usetraceevents):
			# turn trace events on
			events = iter(self.traceevents)
			for e in events:
				self.fsetVal('1', 'events/power/'+e+'/enable')
		# clear the trace buffer
		self.fsetVal('', 'trace')
	def verifyFtrace(self):
		# files needed for any trace data
		files = ['buffer_size_kb', 'current_tracer', 'trace', 'trace_clock',
				 'trace_marker', 'trace_options', 'tracing_on']
		# files needed for callgraph trace data
		tp = self.tpath
		if(self.usecallgraph):
			files += [
				'available_filter_functions',
				'set_ftrace_filter',
				'set_graph_function'
			]
		for f in files:
			if(os.path.exists(tp+f) == False):
				return False
		return True
	def verifyKprobes(self):
		# files needed for kprobes to work
		files = ['kprobe_events', 'events']
		tp = self.tpath
		for f in files:
			if(os.path.exists(tp+f) == False):
				return False
		return True
	def colorText(self, str):
		if not self.ansi:
			return str
		return '\x1B[31;40m'+str+'\x1B[m'

sysvals = SystemValues()

# Class: DevProps
# Description:
#	 Simple class which holds property values collected
#	 for all the devices used in the timeline.
class DevProps:
	syspath = ''
	altname = ''
	async = True
	xtraclass = ''
	xtrainfo = ''
	def out(self, dev):
		return '%s,%s,%d;' % (dev, self.altname, self.async)
	def debug(self, dev):
		print '%s:\n\taltname = %s\n\t  async = %s' % (dev, self.altname, self.async)
	def altName(self, dev):
		if not self.altname or self.altname == dev:
			return dev
		return '%s [%s]' % (self.altname, dev)
	def xtraClass(self):
		if self.xtraclass:
			return ' '+self.xtraclass
		if not self.async:
			return ' sync'
		return ''
	def xtraInfo(self):
		if self.xtraclass:
			return ' '+self.xtraclass
		if self.async:
			return ' async'
		return ' sync'

# Class: DeviceNode
# Description:
#	 A container used to create a device hierachy, with a single root node
#	 and a tree of child nodes. Used by Data.deviceTopology()
class DeviceNode:
	name = ''
	children = 0
	depth = 0
	def __init__(self, nodename, nodedepth):
		self.name = nodename
		self.children = []
		self.depth = nodedepth

# Class: Data
# Description:
#	 The primary container for suspend/resume test data. There is one for
#	 each test run. The data is organized into a cronological hierarchy:
#	 Data.dmesg {
#		root structure, started as dmesg & ftrace, but now only ftrace
#		contents: times for suspend start/end, resume start/end, fwdata
#		phases {
#			10 sequential, non-overlapping phases of S/R
#			contents: times for phase start/end, order/color data for html
#			devlist {
#				device callback or action list for this phase
#				device {
#					a single device callback or generic action
#					contents: start/stop times, pid/cpu/driver info
#						parents/children, html id for timeline/callgraph
#						optionally includes an ftrace callgraph
#						optionally includes intradev trace events
#				}
#			}
#		}
#	}
#
class Data:
	dmesg = {}  # root data structure
	phases = [] # ordered list of phases
	start = 0.0 # test start
	end = 0.0   # test end
	tSuspended = 0.0 # low-level suspend start
	tResumed = 0.0   # low-level resume start
	tLow = 0.0       # time spent in low-level suspend (standby/freeze)
	fwValid = False  # is firmware data available
	fwSuspend = 0    # time spent in firmware suspend
	fwResume = 0     # time spent in firmware resume
	dmesgtext = []   # dmesg text file in memory
	testnumber = 0
	idstr = ''
	html_device_id = 0
	stamp = 0
	outfile = ''
	dev_ubiquitous = ['msleep', 'udelay']
	def __init__(self, num):
		idchar = 'abcdefghijklmnopqrstuvwxyz'
		self.testnumber = num
		self.idstr = idchar[num]
		self.dmesgtext = []
		self.phases = []
		self.dmesg = { # fixed list of 10 phases
			'suspend_prepare': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#CCFFCC', 'order': 0},
			        'suspend': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#88FF88', 'order': 1},
			   'suspend_late': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#00AA00', 'order': 2},
			  'suspend_noirq': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#008888', 'order': 3},
		    'suspend_machine': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#0000FF', 'order': 4},
			 'resume_machine': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#FF0000', 'order': 5},
			   'resume_noirq': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#FF9900', 'order': 6},
			   'resume_early': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#FFCC00', 'order': 7},
			         'resume': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#FFFF88', 'order': 8},
			'resume_complete': {'list': dict(), 'start': -1.0, 'end': -1.0,
								'row': 0, 'color': '#FFFFCC', 'order': 9}
		}
		self.phases = self.sortedPhases()
		self.devicegroups = []
		for phase in self.phases:
			self.devicegroups.append([phase])
	def getStart(self):
		return self.dmesg[self.phases[0]]['start']
	def setStart(self, time):
		self.start = time
		self.dmesg[self.phases[0]]['start'] = time
	def getEnd(self):
		return self.dmesg[self.phases[-1]]['end']
	def setEnd(self, time):
		self.end = time
		self.dmesg[self.phases[-1]]['end'] = time
	def isTraceEventOutsideDeviceCalls(self, pid, time):
		for phase in self.phases:
			list = self.dmesg[phase]['list']
			for dev in list:
				d = list[dev]
				if(d['pid'] == pid and time >= d['start'] and
					time < d['end']):
					return False
		return True
	def targetDevice(self, phaselist, start, end, pid=-1):
		tgtdev = ''
		for phase in phaselist:
			list = self.dmesg[phase]['list']
			for devname in list:
				dev = list[devname]
				if(pid >= 0 and dev['pid'] != pid):
					continue
				devS = dev['start']
				devE = dev['end']
				if(start < devS or start >= devE or end <= devS or end > devE):
					continue
				tgtdev = dev
				break
		return tgtdev
	def addDeviceFunctionCall(self, displayname, kprobename, proc, pid, start, end, cdata, rdata):
		machstart = self.dmesg['suspend_machine']['start']
		machend = self.dmesg['resume_machine']['end']
		tgtdev = self.targetDevice(self.phases, start, end, pid)
		if not tgtdev and start >= machstart and end < machend:
			# device calls in machine phases should be serial
			tgtdev = self.targetDevice(['suspend_machine', 'resume_machine'], start, end)
		if not tgtdev:
			if 'scsi_eh' in proc:
				self.newActionGlobal(proc, start, end, pid)
				self.addDeviceFunctionCall(displayname, kprobename, proc, pid, start, end, cdata, rdata)
			else:
				vprint('IGNORE: %s[%s](%d) [%f - %f] | %s | %s | %s' % (displayname, kprobename,
					pid, start, end, cdata, rdata, proc))
			return False
		# detail block fits within tgtdev
		if('src' not in tgtdev):
			tgtdev['src'] = []
		title = cdata+' '+rdata
		mstr = '\(.*\) *(?P<args>.*) *\((?P<caller>.*)\+.* arg1=(?P<ret>.*)'
		m = re.match(mstr, title)
		if m:
			c = m.group('caller')
			a = m.group('args').strip()
			r = m.group('ret')
			if len(r) > 6:
				r = ''
			else:
				r = 'ret=%s ' % r
			l = '%0.3fms' % ((end - start) * 1000)
			if kprobename in self.dev_ubiquitous:
				title = '%s(%s) <- %s, %s(%s)' % (displayname, a, c, r, l)
			else:
				title = '%s(%s) %s(%s)' % (displayname, a, r, l)
		e = TraceEvent(title, kprobename, start, end - start)
		tgtdev['src'].append(e)
		return True
	def trimTimeVal(self, t, t0, dT, left):
		if left:
			if(t > t0):
				if(t - dT < t0):
					return t0
				return t - dT
			else:
				return t
		else:
			if(t < t0 + dT):
				if(t > t0):
					return t0 + dT
				return t + dT
			else:
				return t
	def trimTime(self, t0, dT, left):
		self.tSuspended = self.trimTimeVal(self.tSuspended, t0, dT, left)
		self.tResumed = self.trimTimeVal(self.tResumed, t0, dT, left)
		self.start = self.trimTimeVal(self.start, t0, dT, left)
		self.end = self.trimTimeVal(self.end, t0, dT, left)
		for phase in self.phases:
			p = self.dmesg[phase]
			p['start'] = self.trimTimeVal(p['start'], t0, dT, left)
			p['end'] = self.trimTimeVal(p['end'], t0, dT, left)
			list = p['list']
			for name in list:
				d = list[name]
				d['start'] = self.trimTimeVal(d['start'], t0, dT, left)
				d['end'] = self.trimTimeVal(d['end'], t0, dT, left)
				if('ftrace' in d):
					cg = d['ftrace']
					cg.start = self.trimTimeVal(cg.start, t0, dT, left)
					cg.end = self.trimTimeVal(cg.end, t0, dT, left)
					for line in cg.list:
						line.time = self.trimTimeVal(line.time, t0, dT, left)
				if('src' in d):
					for e in d['src']:
						e.time = self.trimTimeVal(e.time, t0, dT, left)
	def normalizeTime(self, tZero):
		# trim out any standby or freeze clock time
		if(self.tSuspended != self.tResumed):
			if(self.tResumed > tZero):
				self.trimTime(self.tSuspended, \
					self.tResumed-self.tSuspended, True)
			else:
				self.trimTime(self.tSuspended, \
					self.tResumed-self.tSuspended, False)
	def newPhaseWithSingleAction(self, phasename, devname, start, end, color):
		for phase in self.phases:
			self.dmesg[phase]['order'] += 1
		self.html_device_id += 1
		devid = '%s%d' % (self.idstr, self.html_device_id)
		list = dict()
		list[devname] = \
			{'start': start, 'end': end, 'pid': 0, 'par': '',
			'length': (end-start), 'row': 0, 'id': devid, 'drv': '' };
		self.dmesg[phasename] = \
			{'list': list, 'start': start, 'end': end,
			'row': 0, 'color': color, 'order': 0}
		self.phases = self.sortedPhases()
	def newPhase(self, phasename, start, end, color, order):
		if(order < 0):
			order = len(self.phases)
		for phase in self.phases[order:]:
			self.dmesg[phase]['order'] += 1
		if(order > 0):
			p = self.phases[order-1]
			self.dmesg[p]['end'] = start
		if(order < len(self.phases)):
			p = self.phases[order]
			self.dmesg[p]['start'] = end
		list = dict()
		self.dmesg[phasename] = \
			{'list': list, 'start': start, 'end': end,
			'row': 0, 'color': color, 'order': order}
		self.phases = self.sortedPhases()
		self.devicegroups.append([phasename])
	def setPhase(self, phase, ktime, isbegin):
		if(isbegin):
			self.dmesg[phase]['start'] = ktime
		else:
			self.dmesg[phase]['end'] = ktime
	def dmesgSortVal(self, phase):
		return self.dmesg[phase]['order']
	def sortedPhases(self):
		return sorted(self.dmesg, key=self.dmesgSortVal)
	def sortedDevices(self, phase):
		list = self.dmesg[phase]['list']
		slist = []
		tmp = dict()
		for devname in list:
			dev = list[devname]
			tmp[dev['start']] = devname
		for t in sorted(tmp):
			slist.append(tmp[t])
		return slist
	def fixupInitcalls(self, phase, end):
		# if any calls never returned, clip them at system resume end
		phaselist = self.dmesg[phase]['list']
		for devname in phaselist:
			dev = phaselist[devname]
			if(dev['end'] < 0):
				for p in self.phases:
					if self.dmesg[p]['end'] > dev['start']:
						dev['end'] = self.dmesg[p]['end']
						break
				vprint('%s (%s): callback didnt return' % (devname, phase))
	def deviceFilter(self, devicefilter):
		# remove all by the relatives of the filter devnames
		filter = []
		for phase in self.phases:
			list = self.dmesg[phase]['list']
			for name in devicefilter:
				dev = name
				while(dev in list):
					if(dev not in filter):
						filter.append(dev)
					dev = list[dev]['par']
				children = self.deviceDescendants(name, phase)
				for dev in children:
					if(dev not in filter):
						filter.append(dev)
		for phase in self.phases:
			list = self.dmesg[phase]['list']
			rmlist = []
			for name in list:
				pid = list[name]['pid']
				if(name not in filter and pid >= 0):
					rmlist.append(name)
			for name in rmlist:
				del list[name]
	def fixupInitcallsThatDidntReturn(self):
		# if any calls never returned, clip them at system resume end
		for phase in self.phases:
			self.fixupInitcalls(phase, self.getEnd())
	def isInsideTimeline(self, start, end):
		if(self.start <= start and self.end > start):
			return True
		return False
	def phaseOverlap(self, phases):
		rmgroups = []
		newgroup = []
		for group in self.devicegroups:
			for phase in phases:
				if phase not in group:
					continue
				for p in group:
					if p not in newgroup:
						newgroup.append(p)
				if group not in rmgroups:
					rmgroups.append(group)
		for group in rmgroups:
			self.devicegroups.remove(group)
		self.devicegroups.append(newgroup)
	def newActionGlobal(self, name, start, end, pid=-1, color=''):
		# if event starts before timeline start, expand timeline
		if(start < self.start):
			self.setStart(start)
		# if event ends after timeline end, expand the timeline
		if(end > self.end):
			self.setEnd(end)
		# which phase is this device callback or action "in"
		targetphase = "none"
		htmlclass = ''
		overlap = 0.0
		phases = []
		for phase in self.phases:
			pstart = self.dmesg[phase]['start']
			pend = self.dmesg[phase]['end']
			o = max(0, min(end, pend) - max(start, pstart))
			if o > 0:
				phases.append(phase)
			if o > overlap:
				if overlap > 0 and phase == 'post_resume':
					continue
				targetphase = phase
				overlap = o
		if pid == -2:
			htmlclass = ' bg'
		if len(phases) > 1:
			htmlclass = ' bg'
			self.phaseOverlap(phases)
		if targetphase in self.phases:
			newname = self.newAction(targetphase, name, pid, '', start, end, '', htmlclass, color)
			return (targetphase, newname)
		return False
	def newAction(self, phase, name, pid, parent, start, end, drv, htmlclass='', color=''):
		# new device callback for a specific phase
		self.html_device_id += 1
		devid = '%s%d' % (self.idstr, self.html_device_id)
		list = self.dmesg[phase]['list']
		length = -1.0
		if(start >= 0 and end >= 0):
			length = end - start
		if pid == -2:
			i = 2
			origname = name
			while(name in list):
				name = '%s[%d]' % (origname, i)
				i += 1
		list[name] = {'start': start, 'end': end, 'pid': pid, 'par': parent,
					  'length': length, 'row': 0, 'id': devid, 'drv': drv }
		if htmlclass:
			list[name]['htmlclass'] = htmlclass
		if color:
			list[name]['color'] = color
		return name
	def deviceIDs(self, devlist, phase):
		idlist = []
		list = self.dmesg[phase]['list']
		for devname in list:
			if devname in devlist:
				idlist.append(list[devname]['id'])
		return idlist
	def deviceParentID(self, devname, phase):
		pdev = ''
		pdevid = ''
		list = self.dmesg[phase]['list']
		if devname in list:
			pdev = list[devname]['par']
		if pdev in list:
			return list[pdev]['id']
		return pdev
	def deviceChildren(self, devname, phase):
		devlist = []
		list = self.dmesg[phase]['list']
		for child in list:
			if(list[child]['par'] == devname):
				devlist.append(child)
		return devlist
	def deviceDescendants(self, devname, phase):
		children = self.deviceChildren(devname, phase)
		family = children
		for child in children:
			family += self.deviceDescendants(child, phase)
		return family
	def deviceChildrenIDs(self, devname, phase):
		devlist = self.deviceChildren(devname, phase)
		return self.deviceIDs(devlist, phase)
	def printDetails(self):
		vprint('          test start: %f' % self.start)
		for phase in self.phases:
			dc = len(self.dmesg[phase]['list'])
			vprint('    %16s: %f - %f (%d devices)' % (phase, \
				self.dmesg[phase]['start'], self.dmesg[phase]['end'], dc))
		vprint('            test end: %f' % self.end)
	def deviceChildrenAllPhases(self, devname):
		devlist = []
		for phase in self.phases:
			list = self.deviceChildren(devname, phase)
			for dev in list:
				if dev not in devlist:
					devlist.append(dev)
		return devlist
	def masterTopology(self, name, list, depth):
		node = DeviceNode(name, depth)
		for cname in list:
			# avoid recursions
			if name == cname:
				continue
			clist = self.deviceChildrenAllPhases(cname)
			cnode = self.masterTopology(cname, clist, depth+1)
			node.children.append(cnode)
		return node
	def printTopology(self, node):
		html = ''
		if node.name:
			info = ''
			drv = ''
			for phase in self.phases:
				list = self.dmesg[phase]['list']
				if node.name in list:
					s = list[node.name]['start']
					e = list[node.name]['end']
					if list[node.name]['drv']:
						drv = ' {'+list[node.name]['drv']+'}'
					info += ('<li>%s: %.3fms</li>' % (phase, (e-s)*1000))
			html += '<li><b>'+node.name+drv+'</b>'
			if info:
				html += '<ul>'+info+'</ul>'
			html += '</li>'
		if len(node.children) > 0:
			html += '<ul>'
			for cnode in node.children:
				html += self.printTopology(cnode)
			html += '</ul>'
		return html
	def rootDeviceList(self):
		# list of devices graphed
		real = []
		for phase in self.dmesg:
			list = self.dmesg[phase]['list']
			for dev in list:
				if list[dev]['pid'] >= 0 and dev not in real:
					real.append(dev)
		# list of top-most root devices
		rootlist = []
		for phase in self.dmesg:
			list = self.dmesg[phase]['list']
			for dev in list:
				pdev = list[dev]['par']
				pid = list[dev]['pid']
				if(pid < 0 or re.match('[0-9]*-[0-9]*\.[0-9]*[\.0-9]*\:[\.0-9]*$', pdev)):
					continue
				if pdev and pdev not in real and pdev not in rootlist:
					rootlist.append(pdev)
		return rootlist
	def deviceTopology(self):
		rootlist = self.rootDeviceList()
		master = self.masterTopology('', rootlist, 0)
		return self.printTopology(master)
	def selectTimelineDevices(self, widfmt, tTotal, mindevlen):
		# only select devices that will actually show up in html
		self.tdevlist = dict()
		for phase in self.dmesg:
			devlist = []
			list = self.dmesg[phase]['list']
			for dev in list:
				length = (list[dev]['end'] - list[dev]['start']) * 1000
				width = widfmt % (((list[dev]['end']-list[dev]['start'])*100)/tTotal)
				if width != '0.000000' and length >= mindevlen:
					devlist.append(dev)
			self.tdevlist[phase] = devlist

# Class: TraceEvent
# Description:
#	 A container for trace event data found in the ftrace file
class TraceEvent:
	text = ''
	time = 0.0
	length = 0.0
	title = ''
	row = 0
	def __init__(self, a, n, t, l):
		self.title = a
		self.text = n
		self.time = t
		self.length = l

# Class: FTraceLine
# Description:
#	 A container for a single line of ftrace data. There are six basic types:
#		 callgraph line:
#			  call: "  dpm_run_callback() {"
#			return: "  }"
#			  leaf: " dpm_run_callback();"
#		 trace event:
#			 tracing_mark_write: SUSPEND START or RESUME COMPLETE
#			 suspend_resume: phase or custom exec block data
#			 device_pm_callback: device callback info
class FTraceLine:
	time = 0.0
	length = 0.0
	fcall = False
	freturn = False
	fevent = False
	fkprobe = False
	depth = 0
	name = ''
	type = ''
	def __init__(self, t, m='', d=''):
		self.time = float(t)
		if not m and not d:
			return
		# is this a trace event
		if(d == 'traceevent' or re.match('^ *\/\* *(?P<msg>.*) \*\/ *$', m)):
			if(d == 'traceevent'):
				# nop format trace event
				msg = m
			else:
				# function_graph format trace event
				em = re.match('^ *\/\* *(?P<msg>.*) \*\/ *$', m)
				msg = em.group('msg')

			emm = re.match('^(?P<call>.*?): (?P<msg>.*)', msg)
			if(emm):
				self.name = emm.group('msg')
				self.type = emm.group('call')
			else:
				self.name = msg
			km = re.match('^(?P<n>.*)_cal$', self.type)
			if km:
				self.fcall = True
				self.fkprobe = True
				self.type = km.group('n')
				return
			km = re.match('^(?P<n>.*)_ret$', self.type)
			if km:
				self.freturn = True
				self.fkprobe = True
				self.type = km.group('n')
				return
			self.fevent = True
			return
		# convert the duration to seconds
		if(d):
			self.length = float(d)/1000000
		# the indentation determines the depth
		match = re.match('^(?P<d> *)(?P<o>.*)$', m)
		if(not match):
			return
		self.depth = self.getDepth(match.group('d'))
		m = match.group('o')
		# function return
		if(m[0] == '}'):
			self.freturn = True
			if(len(m) > 1):
				# includes comment with function name
				match = re.match('^} *\/\* *(?P<n>.*) *\*\/$', m)
				if(match):
					self.name = match.group('n').strip()
		# function call
		else:
			self.fcall = True
			# function call with children
			if(m[-1] == '{'):
				match = re.match('^(?P<n>.*) *\(.*', m)
				if(match):
					self.name = match.group('n').strip()
			# function call with no children (leaf)
			elif(m[-1] == ';'):
				self.freturn = True
				match = re.match('^(?P<n>.*) *\(.*', m)
				if(match):
					self.name = match.group('n').strip()
			# something else (possibly a trace marker)
			else:
				self.name = m
	def getDepth(self, str):
		return len(str)/2
	def debugPrint(self, dev=''):
		if(self.freturn and self.fcall):
			print('%s -- %f (%02d): %s(); (%.3f us)' % (dev, self.time, \
				self.depth, self.name, self.length*1000000))
		elif(self.freturn):
			print('%s -- %f (%02d): %s} (%.3f us)' % (dev, self.time, \
				self.depth, self.name, self.length*1000000))
		else:
			print('%s -- %f (%02d): %s() { (%.3f us)' % (dev, self.time, \
				self.depth, self.name, self.length*1000000))
	def startMarker(self):
		global sysvals
		# Is this the starting line of a suspend?
		if not self.fevent:
			return False
		if sysvals.usetracemarkers:
			if(self.name == 'SUSPEND START'):
				return True
			return False
		else:
			if(self.type == 'suspend_resume' and
				re.match('suspend_enter\[.*\] begin', self.name)):
				return True
			return False
	def endMarker(self):
		# Is this the ending line of a resume?
		if not self.fevent:
			return False
		if sysvals.usetracemarkers:
			if(self.name == 'RESUME COMPLETE'):
				return True
			return False
		else:
			if(self.type == 'suspend_resume' and
				re.match('thaw_processes\[.*\] end', self.name)):
				return True
			return False

# Class: FTraceCallGraph
# Description:
#	 A container for the ftrace callgraph of a single recursive function.
#	 This can be a dpm_run_callback, dpm_prepare, or dpm_complete callgraph
#	 Each instance is tied to a single device in a single phase, and is
#	 comprised of an ordered list of FTraceLine objects
class FTraceCallGraph:
	start = -1.0
	end = -1.0
	list = []
	invalid = False
	depth = 0
	pid = 0
	def __init__(self, pid):
		self.start = -1.0
		self.end = -1.0
		self.list = []
		self.depth = 0
		self.pid = pid
	def addLine(self, line, debug=False):
		# if this is already invalid, just leave
		if(self.invalid):
			return False
		# invalidate on too much data or bad depth
		if(len(self.list) >= 1000000 or self.depth < 0):
			self.invalidate(line)
			return False
		# compare current depth with this lines pre-call depth
		prelinedep = line.depth
		if(line.freturn and not line.fcall):
			prelinedep += 1
		last = 0
		lasttime = line.time
		virtualfname = 'execution_misalignment'
		if len(self.list) > 0:
			last = self.list[-1]
			lasttime = last.time
		# handle low misalignments by inserting returns
		if prelinedep < self.depth:
			if debug and last:
				print '-------- task %d --------' % self.pid
				last.debugPrint()
			idx = 0
			# add return calls to get the depth down
			while prelinedep < self.depth:
				if debug:
					print 'MISALIGN LOW (add returns): C%d - eC%d' % (self.depth, prelinedep)
				self.depth -= 1
				if idx == 0 and last and last.fcall and not last.freturn:
					# special case, turn last call into a leaf
					last.depth = self.depth
					last.freturn = True
					last.length = line.time - last.time
					if debug:
						last.debugPrint()
				else:
					vline = FTraceLine(lasttime)
					vline.depth = self.depth
					vline.name = virtualfname
					vline.freturn = True
					self.list.append(vline)
					if debug:
						vline.debugPrint()
				idx += 1
			if debug:
				line.debugPrint()
				print ''
		# handle high misalignments by inserting calls
		elif prelinedep > self.depth:
			if debug and last:
				print '-------- task %d --------' % self.pid
				last.debugPrint()
			idx = 0
			# add calls to get the depth up
			while prelinedep > self.depth:
				if debug:
					print 'MISALIGN HIGH (add calls): C%d - eC%d' % (self.depth, prelinedep)
				if idx == 0 and line.freturn and not line.fcall:
					# special case, turn this return into a leaf
					line.fcall = True
					prelinedep -= 1
				else:
					vline = FTraceLine(lasttime)
					vline.depth = self.depth
					vline.name = virtualfname
					vline.fcall = True
					if debug:
						vline.debugPrint()
					self.list.append(vline)
					self.depth += 1
					if not last:
						self.start = vline.time
				idx += 1
			if debug:
				line.debugPrint()
				print ''
		# process the call and set the new depth
		if(line.fcall and not line.freturn):
			self.depth += 1
		elif(line.freturn and not line.fcall):
			self.depth -= 1
		if len(self.list) < 1:
			self.start = line.time
		self.list.append(line)
		if(line.depth == 0 and line.freturn):
			if(self.start < 0):
				self.start = line.time
			self.end = line.time
			if line.fcall:
				self.end += line.length
			if self.list[0].name == virtualfname:
				self.invalid = True
			return True
		return False
	def invalidate(self, line):
		if(len(self.list) > 0):
			first = self.list[0]
			self.list = []
			self.list.append(first)
		self.invalid = True
		id = 'task %s' % (self.pid)
		window = '(%f - %f)' % (self.start, line.time)
		if(self.depth < 0):
			vprint('Too much data for '+id+\
				' (buffer overflow), ignoring this callback')
		else:
			vprint('Too much data for '+id+\
				' '+window+', ignoring this callback')
	def slice(self, t0, tN):
		minicg = FTraceCallGraph(0)
		count = -1
		firstdepth = 0
		for l in self.list:
			if(l.time < t0 or l.time > tN):
				continue
			if(count < 0):
				if(not l.fcall or l.name == 'dev_driver_string'):
					continue
				firstdepth = l.depth
				count = 0
			l.depth -= firstdepth
			minicg.addLine(l)
			if((count == 0 and l.freturn and l.fcall) or
				(count > 0 and l.depth <= 0)):
				break
			count += 1
		return minicg
	def repair(self, enddepth):
		# bring the depth back to 0 with additional returns
		fixed = False
		last = self.list[-1]
		for i in reversed(range(enddepth)):
			t = FTraceLine(last.time)
			t.depth = i
			t.freturn = True
			fixed = self.addLine(t)
			if fixed:
				self.end = last.time
				return True
		return False
	def postProcess(self, debug=False):
		stack = dict()
		cnt = 0
		for l in self.list:
			if(l.fcall and not l.freturn):
				stack[l.depth] = l
				cnt += 1
			elif(l.freturn and not l.fcall):
				if(l.depth not in stack):
					if debug:
						print 'Post Process Error: Depth missing'
						l.debugPrint()
					return False
				# transfer total time from return line to call line
				stack[l.depth].length = l.length
				stack.pop(l.depth)
				l.length = 0
				cnt -= 1
		if(cnt == 0):
			# trace caught the whole call tree
			return True
		elif(cnt < 0):
			if debug:
				print 'Post Process Error: Depth is less than 0'
			return False
		# trace ended before call tree finished
		return self.repair(cnt)
	def deviceMatch(self, pid, data):
		found = False
		# add the callgraph data to the device hierarchy
		borderphase = {
			'dpm_prepare': 'suspend_prepare',
			'dpm_complete': 'resume_complete'
		}
		if(self.list[0].name in borderphase):
			p = borderphase[self.list[0].name]
			list = data.dmesg[p]['list']
			for devname in list:
				dev = list[devname]
				if(pid == dev['pid'] and
					self.start <= dev['start'] and
					self.end >= dev['end']):
					dev['ftrace'] = self.slice(dev['start'], dev['end'])
					found = True
			return found
		for p in data.phases:
			if(data.dmesg[p]['start'] <= self.start and
				self.start <= data.dmesg[p]['end']):
				list = data.dmesg[p]['list']
				for devname in list:
					dev = list[devname]
					if(pid == dev['pid'] and
						self.start <= dev['start'] and
						self.end >= dev['end']):
						dev['ftrace'] = self
						found = True
						break
				break
		return found
	def newActionFromFunction(self, data):
		name = self.list[0].name
		if name in ['dpm_run_callback', 'dpm_prepare', 'dpm_complete']:
			return
		fs = self.start
		fe = self.end
		if fs < data.start or fe > data.end:
			return
		phase = ''
		for p in data.phases:
			if(data.dmesg[p]['start'] <= self.start and
				self.start < data.dmesg[p]['end']):
				phase = p
				break
		if not phase:
			return
		out = data.newActionGlobal(name, fs, fe, -2)
		if out:
			phase, myname = out
			data.dmesg[phase]['list'][myname]['ftrace'] = self
	def debugPrint(self):
		print('[%f - %f] %s (%d)') % (self.start, self.end, self.list[0].name, self.pid)
		for l in self.list:
			if(l.freturn and l.fcall):
				print('%f (%02d): %s(); (%.3f us)' % (l.time, \
					l.depth, l.name, l.length*1000000))
			elif(l.freturn):
				print('%f (%02d): %s} (%.3f us)' % (l.time, \
					l.depth, l.name, l.length*1000000))
			else:
				print('%f (%02d): %s() { (%.3f us)' % (l.time, \
					l.depth, l.name, l.length*1000000))
		print(' ')

# Class: Timeline
# Description:
#	 A container for a device timeline which calculates
#	 all the html properties to display it correctly
class Timeline:
	html = {}
	height = 0	# total timeline height
	scaleH = 20	# timescale (top) row height
	rowH = 30	# device row height
	bodyH = 0	# body height
	rows = 0	# total timeline rows
	phases = []
	rowmaxlines = dict()
	rowcount = dict()
	rowheight = dict()
	def __init__(self, rowheight):
		self.rowH = rowheight
		self.html = {
			'header': '',
			'timeline': '',
			'legend': '',
		}
	# Function: getDeviceRows
	# Description:
	#    determine how may rows the device funcs will take
	# Arguments:
	#	 rawlist: the list of devices/actions for a single phase
	# Output:
	#	 The total number of rows needed to display this phase of the timeline
	def getDeviceRows(self, rawlist):
		# clear all rows and set them to undefined
		lendict = dict()
		for item in rawlist:
			item.row = -1
			lendict[item] = item.length
		list = []
		for i in sorted(lendict, key=lendict.get, reverse=True):
			list.append(i)
		remaining = len(list)
		rowdata = dict()
		row = 1
		# try to pack each row with as many ranges as possible
		while(remaining > 0):
			if(row not in rowdata):
				rowdata[row] = []
			for i in list:
				if(i.row >= 0):
					continue
				s = i.time
				e = i.time + i.length
				valid = True
				for ritem in rowdata[row]:
					rs = ritem.time
					re = ritem.time + ritem.length
					if(not (((s <= rs) and (e <= rs)) or
						((s >= re) and (e >= re)))):
						valid = False
						break
				if(valid):
					rowdata[row].append(i)
					i.row = row
					remaining -= 1
			row += 1
		return row
	# Function: getPhaseRows
	# Description:
	#	 Organize the timeline entries into the smallest
	#	 number of rows possible, with no entry overlapping
	# Arguments:
	#	 list: the list of devices/actions for a single phase
	#	 devlist: string list of device names to use
	# Output:
	#	 The total number of rows needed to display this phase of the timeline
	def getPhaseRows(self, dmesg, devlist):
		# clear all rows and set them to undefined
		remaining = len(devlist)
		rowdata = dict()
		row = 0
		lendict = dict()
		myphases = []
		for item in devlist:
			if item[0] not in self.phases:
				self.phases.append(item[0])
			if item[0] not in myphases:
				myphases.append(item[0])
				self.rowmaxlines[item[0]] = dict()
				self.rowheight[item[0]] = dict()
			dev = dmesg[item[0]]['list'][item[1]]
			dev['row'] = -1
			lendict[item] = float(dev['end']) - float(dev['start'])
			if 'src' in dev:
				dev['devrows'] = self.getDeviceRows(dev['src'])
		lenlist = []
		for i in sorted(lendict, key=lendict.get, reverse=True):
			lenlist.append(i)
		orderedlist = []
		for item in lenlist:
			dev = dmesg[item[0]]['list'][item[1]]
			if dev['pid'] == -2:
				orderedlist.append(item)
		for item in lenlist:
			if item not in orderedlist:
				orderedlist.append(item)
		# try to pack each row with as many ranges as possible
		while(remaining > 0):
			rowheight = 1
			if(row not in rowdata):
				rowdata[row] = []
			for item in orderedlist:
				dev = dmesg[item[0]]['list'][item[1]]
				if(dev['row'] < 0):
					s = dev['start']
					e = dev['end']
					valid = True
					for ritem in rowdata[row]:
						rs = ritem['start']
						re = ritem['end']
						if(not (((s <= rs) and (e <= rs)) or
							((s >= re) and (e >= re)))):
							valid = False
							break
					if(valid):
						rowdata[row].append(dev)
						dev['row'] = row
						remaining -= 1
						if 'devrows' in dev and dev['devrows'] > rowheight:
							rowheight = dev['devrows']
			for phase in myphases:
				self.rowmaxlines[phase][row] = rowheight
				self.rowheight[phase][row] = rowheight * self.rowH
			row += 1
		if(row > self.rows):
			self.rows = int(row)
		for phase in myphases:
			self.rowcount[phase] = row
		return row
	def phaseRowHeight(self, phase, row):
		return self.rowheight[phase][row]
	def phaseRowTop(self, phase, row):
		top = 0
		for i in sorted(self.rowheight[phase]):
			if i >= row:
				break
			top += self.rowheight[phase][i]
		return top
	# Function: calcTotalRows
	# Description:
	#	 Calculate the heights and offsets for the header and rows
	def calcTotalRows(self):
		maxrows = 0
		standardphases = []
		for phase in self.phases:
			total = 0
			for i in sorted(self.rowmaxlines[phase]):
				total += self.rowmaxlines[phase][i]
			if total > maxrows:
				maxrows = total
			if total == self.rowcount[phase]:
				standardphases.append(phase)
		self.height = self.scaleH + (maxrows*self.rowH)
		self.bodyH = self.height - self.scaleH
		for phase in standardphases:
			for i in sorted(self.rowheight[phase]):
				self.rowheight[phase][i] = self.bodyH/self.rowcount[phase]
	# Function: createTimeScale
	# Description:
	#	 Create the timescale for a timeline block
	# Arguments:
	#	 m0: start time (mode begin)
	#	 mMax: end time (mode end)
	#	 tTotal: total timeline time
	#	 mode: suspend or resume
	# Output:
	#	 The html code needed to display the time scale
	def createTimeScale(self, m0, mMax, tTotal, mode):
		timescale = '<div class="t" style="right:{0}%">{1}</div>\n'
		rline = '<div class="t" style="left:0;border-left:1px solid black;border-right:0;">Resume</div>\n'
		output = '<div class="timescale">\n'
		# set scale for timeline
		mTotal = mMax - m0
		tS = 0.1
		if(tTotal <= 0):
			return output+'</div>\n'
		if(tTotal > 4):
			tS = 1
		divTotal = int(mTotal/tS) + 1
		divEdge = (mTotal - tS*(divTotal-1))*100/mTotal
		for i in range(divTotal):
			htmlline = ''
			if(mode == 'resume'):
				pos = '%0.3f' % (100 - ((float(i)*tS*100)/mTotal))
				val = '%0.fms' % (float(i)*tS*1000)
				htmlline = timescale.format(pos, val)
				if(i == 0):
					htmlline = rline
			else:
				pos = '%0.3f' % (100 - ((float(i)*tS*100)/mTotal) - divEdge)
				val = '%0.fms' % (float(i-divTotal+1)*tS*1000)
				if(i == divTotal - 1):
					val = 'Suspend'
				htmlline = timescale.format(pos, val)
			output += htmlline
		output += '</div>\n'
		return output

# Class: TestProps
# Description:
#	 A list of values describing the properties of these test runs
class TestProps:
	stamp = ''
	tracertype = ''
	S0i3 = False
	fwdata = []
	ftrace_line_fmt_fg = \
		'^ *(?P<time>[0-9\.]*) *\| *(?P<cpu>[0-9]*)\)'+\
		' *(?P<proc>.*)-(?P<pid>[0-9]*) *\|'+\
		'[ +!#\*@$]*(?P<dur>[0-9\.]*) .*\|  (?P<msg>.*)'
	ftrace_line_fmt_nop = \
		' *(?P<proc>.*)-(?P<pid>[0-9]*) *\[(?P<cpu>[0-9]*)\] *'+\
		'(?P<flags>.{4}) *(?P<time>[0-9\.]*): *'+\
		'(?P<msg>.*)'
	ftrace_line_fmt = ftrace_line_fmt_nop
	cgformat = False
	data = 0
	ktemp = dict()
	def __init__(self):
		self.ktemp = dict()
	def setTracerType(self, tracer):
		self.tracertype = tracer
		if(tracer == 'function_graph'):
			self.cgformat = True
			self.ftrace_line_fmt = self.ftrace_line_fmt_fg
		elif(tracer == 'nop'):
			self.ftrace_line_fmt = self.ftrace_line_fmt_nop
		else:
			doError('Invalid tracer format: [%s]' % tracer, False)

# Class: TestRun
# Description:
#	 A container for a suspend/resume test run. This is necessary as
#	 there could be more than one, and they need to be separate.
class TestRun:
	ftemp = dict()
	ttemp = dict()
	data = 0
	def __init__(self, dataobj):
		self.data = dataobj
		self.ftemp = dict()
		self.ttemp = dict()

# ----------------- FUNCTIONS --------------------

# Function: vprint
# Description:
#	 verbose print (prints only with -verbose option)
# Arguments:
#	 msg: the debug/log message to print
def vprint(msg):
	global sysvals
	if(sysvals.verbose):
		print(msg)

# Function: parseStamp
# Description:
#	 Pull in the stamp comment line from the data file(s),
#	 create the stamp, and add it to the global sysvals object
# Arguments:
#	 m: the valid re.match output for the stamp line
def parseStamp(line, data):
	global sysvals

	m = re.match(sysvals.stampfmt, line)
	data.stamp = {'time': '', 'host': '', 'mode': ''}
	dt = datetime(int(m.group('y'))+2000, int(m.group('m')),
		int(m.group('d')), int(m.group('H')), int(m.group('M')),
		int(m.group('S')))
	data.stamp['time'] = dt.strftime('%B %d %Y, %I:%M:%S %p')
	data.stamp['host'] = m.group('host')
	data.stamp['mode'] = m.group('mode')
	data.stamp['kernel'] = m.group('kernel')
	sysvals.hostname = data.stamp['host']
	sysvals.suspendmode = data.stamp['mode']
	if not sysvals.stamp:
		sysvals.stamp = data.stamp

# Function: diffStamp
# Description:
#	compare the host, kernel, and mode fields in 3 stamps
# Arguments:
#	 stamp1: string array with mode, kernel, and host
#	 stamp2: string array with mode, kernel, and host
# Return:
#	True if stamps differ, False if they're the same
def diffStamp(stamp1, stamp2):
	if 'host' in stamp1 and 'host' in stamp2:
		if stamp1['host'] != stamp2['host']:
			return True
	if 'kernel' in stamp1 and 'kernel' in stamp2:
		if stamp1['kernel'] != stamp2['kernel']:
			return True
	if 'mode' in stamp1 and 'mode' in stamp2:
		if stamp1['mode'] != stamp2['mode']:
			return True
	return False

# Function: doesTraceLogHaveTraceEvents
# Description:
#	 Quickly determine if the ftrace log has some or all of the trace events
#	 required for primary parsing. Set the usetraceevents and/or
#	 usetraceeventsonly flags in the global sysvals object
def doesTraceLogHaveTraceEvents():
	global sysvals

	# check for kprobes
	sysvals.usekprobes = False
	out = os.system('grep -q "_cal: (" '+sysvals.ftracefile)
	if(out == 0):
		sysvals.usekprobes = True
	# check for callgraph data on trace event blocks
	out = os.system('grep -q "_cpu_down()" '+sysvals.ftracefile)
	if(out == 0):
		sysvals.usekprobes = True
	out = os.popen('head -1 '+sysvals.ftracefile).read().replace('\n', '')
	m = re.match(sysvals.stampfmt, out)
	if m and m.group('mode') == 'command':
		sysvals.usetraceeventsonly = True
		sysvals.usetraceevents = True
		return
	# figure out what level of trace events are supported
	sysvals.usetraceeventsonly = True
	sysvals.usetraceevents = False
	for e in sysvals.traceevents:
		out = os.system('grep -q "'+e+': " '+sysvals.ftracefile)
		if(out != 0):
			sysvals.usetraceeventsonly = False
		if(e == 'suspend_resume' and out == 0):
			sysvals.usetraceevents = True
	# determine is this log is properly formatted
	for e in ['SUSPEND START', 'RESUME COMPLETE']:
		out = os.system('grep -q "'+e+'" '+sysvals.ftracefile)
		if(out != 0):
			sysvals.usetracemarkers = False

# Function: appendIncompleteTraceLog
# Description:
#	 [deprecated for kernel 3.15 or newer]
#	 Legacy support of ftrace outputs that lack the device_pm_callback
#	 and/or suspend_resume trace events. The primary data should be
#	 taken from dmesg, and this ftrace is used only for callgraph data
#	 or custom actions in the timeline. The data is appended to the Data
#	 objects provided.
# Arguments:
#	 testruns: the array of Data objects obtained from parseKernelLog
def appendIncompleteTraceLog(testruns):
	global sysvals

	# create TestRun vessels for ftrace parsing
	testcnt = len(testruns)
	testidx = 0
	testrun = []
	for data in testruns:
		testrun.append(TestRun(data))

	# extract the callgraph and traceevent data
	vprint('Analyzing the ftrace data...')
	tp = TestProps()
	tf = open(sysvals.ftracefile, 'r')
	data = 0
	for line in tf:
		# remove any latent carriage returns
		line = line.replace('\r\n', '')
		# grab the time stamp
		m = re.match(sysvals.stampfmt, line)
		if(m):
			tp.stamp = line
			continue
		# determine the trace data type (required for further parsing)
		m = re.match(sysvals.tracertypefmt, line)
		if(m):
			tp.setTracerType(m.group('t'))
			continue
		# device properties line
		if(re.match(sysvals.devpropfmt, line)):
			devProps(line)
			continue
		# parse only valid lines, if this is not one move on
		m = re.match(tp.ftrace_line_fmt, line)
		if(not m):
			continue
		# gather the basic message data from the line
		m_time = m.group('time')
		m_pid = m.group('pid')
		m_msg = m.group('msg')
		if(tp.cgformat):
			m_param3 = m.group('dur')
		else:
			m_param3 = 'traceevent'
		if(m_time and m_pid and m_msg):
			t = FTraceLine(m_time, m_msg, m_param3)
			pid = int(m_pid)
		else:
			continue
		# the line should be a call, return, or event
		if(not t.fcall and not t.freturn and not t.fevent):
			continue
		# look for the suspend start marker
		if(t.startMarker()):
			data = testrun[testidx].data
			parseStamp(tp.stamp, data)
			data.setStart(t.time)
			continue
		if(not data):
			continue
		# find the end of resume
		if(t.endMarker()):
			data.setEnd(t.time)
			testidx += 1
			if(testidx >= testcnt):
				break
			continue
		# trace event processing
		if(t.fevent):
			# general trace events have two types, begin and end
			if(re.match('(?P<name>.*) begin$', t.name)):
				isbegin = True
			elif(re.match('(?P<name>.*) end$', t.name)):
				isbegin = False
			else:
				continue
			m = re.match('(?P<name>.*)\[(?P<val>[0-9]*)\] .*', t.name)
			if(m):
				val = m.group('val')
				if val == '0':
					name = m.group('name')
				else:
					name = m.group('name')+'['+val+']'
			else:
				m = re.match('(?P<name>.*) .*', t.name)
				name = m.group('name')
			# special processing for trace events
			if re.match('dpm_prepare\[.*', name):
				continue
			elif re.match('machine_suspend.*', name):
				continue
			elif re.match('suspend_enter\[.*', name):
				if(not isbegin):
					data.dmesg['suspend_prepare']['end'] = t.time
				continue
			elif re.match('dpm_suspend\[.*', name):
				if(not isbegin):
					data.dmesg['suspend']['end'] = t.time
				continue
			elif re.match('dpm_suspend_late\[.*', name):
				if(isbegin):
					data.dmesg['suspend_late']['start'] = t.time
				else:
					data.dmesg['suspend_late']['end'] = t.time
				continue
			elif re.match('dpm_suspend_noirq\[.*', name):
				if(isbegin):
					data.dmesg['suspend_noirq']['start'] = t.time
				else:
					data.dmesg['suspend_noirq']['end'] = t.time
				continue
			elif re.match('dpm_resume_noirq\[.*', name):
				if(isbegin):
					data.dmesg['resume_machine']['end'] = t.time
					data.dmesg['resume_noirq']['start'] = t.time
				else:
					data.dmesg['resume_noirq']['end'] = t.time
				continue
			elif re.match('dpm_resume_early\[.*', name):
				if(isbegin):
					data.dmesg['resume_early']['start'] = t.time
				else:
					data.dmesg['resume_early']['end'] = t.time
				continue
			elif re.match('dpm_resume\[.*', name):
				if(isbegin):
					data.dmesg['resume']['start'] = t.time
				else:
					data.dmesg['resume']['end'] = t.time
				continue
			elif re.match('dpm_complete\[.*', name):
				if(isbegin):
					data.dmesg['resume_complete']['start'] = t.time
				else:
					data.dmesg['resume_complete']['end'] = t.time
				continue
			# skip trace events inside devices calls
			if(not data.isTraceEventOutsideDeviceCalls(pid, t.time)):
				continue
			# global events (outside device calls) are simply graphed
			if(isbegin):
				# store each trace event in ttemp
				if(name not in testrun[testidx].ttemp):
					testrun[testidx].ttemp[name] = []
				testrun[testidx].ttemp[name].append(\
					{'begin': t.time, 'end': t.time})
			else:
				# finish off matching trace event in ttemp
				if(name in testrun[testidx].ttemp):
					testrun[testidx].ttemp[name][-1]['end'] = t.time
		# call/return processing
		elif sysvals.usecallgraph:
			# create a callgraph object for the data
			if(pid not in testrun[testidx].ftemp):
				testrun[testidx].ftemp[pid] = []
				testrun[testidx].ftemp[pid].append(FTraceCallGraph(pid))
			# when the call is finished, see which device matches it
			cg = testrun[testidx].ftemp[pid][-1]
			if(cg.addLine(t)):
				testrun[testidx].ftemp[pid].append(FTraceCallGraph(pid))
	tf.close()

	for test in testrun:
		# add the traceevent data to the device hierarchy
		if(sysvals.usetraceevents):
			for name in test.ttemp:
				for event in test.ttemp[name]:
					test.data.newActionGlobal(name, event['begin'], event['end'])

		# add the callgraph data to the device hierarchy
		for pid in test.ftemp:
			for cg in test.ftemp[pid]:
				if len(cg.list) < 1 or cg.invalid:
					continue
				if(not cg.postProcess()):
					id = 'task %s cpu %s' % (pid, m.group('cpu'))
					vprint('Sanity check failed for '+\
						id+', ignoring this callback')
					continue
				callstart = cg.start
				callend = cg.end
				for p in test.data.phases:
					if(test.data.dmesg[p]['start'] <= callstart and
						callstart <= test.data.dmesg[p]['end']):
						list = test.data.dmesg[p]['list']
						for devname in list:
							dev = list[devname]
							if(pid == dev['pid'] and
								callstart <= dev['start'] and
								callend >= dev['end']):
								dev['ftrace'] = cg
						break

		if(sysvals.verbose):
			test.data.printDetails()

# Function: parseTraceLog
# Description:
#	 Analyze an ftrace log output file generated from this app during
#	 the execution phase. Used when the ftrace log is the primary data source
#	 and includes the suspend_resume and device_pm_callback trace events
#	 The ftrace filename is taken from sysvals
# Output:
#	 An array of Data objects
def parseTraceLog():
	global sysvals

	vprint('Analyzing the ftrace data...')
	if(os.path.exists(sysvals.ftracefile) == False):
		doError('%s does not exist' % sysvals.ftracefile, False)

	sysvals.setupAllKprobes()
	tracewatch = ['suspend_enter']
	if sysvals.usekprobes:
		tracewatch += ['sync_filesystems', 'freeze_processes', 'syscore_suspend',
			'syscore_resume', 'resume_console', 'thaw_processes', 'CPU_ON', 'CPU_OFF']

	# extract the callgraph and traceevent data
	tp = TestProps()
	testruns = []
	testdata = []
	testrun = 0
	data = 0
	tf = open(sysvals.ftracefile, 'r')
	phase = 'suspend_prepare'
	for line in tf:
		# remove any latent carriage returns
		line = line.replace('\r\n', '')
		# stamp line: each stamp means a new test run
		m = re.match(sysvals.stampfmt, line)
		if(m):
			tp.stamp = line
			continue
		# firmware line: pull out any firmware data
		m = re.match(sysvals.firmwarefmt, line)
		if(m):
			tp.fwdata.append((int(m.group('s')), int(m.group('r'))))
			continue
		# tracer type line: determine the trace data type
		m = re.match(sysvals.tracertypefmt, line)
		if(m):
			tp.setTracerType(m.group('t'))
			continue
		# post resume time line: did this test run include post-resume data
		m = re.match(sysvals.postresumefmt, line)
		if(m):
			t = int(m.group('t'))
			if(t > 0):
				sysvals.postresumetime = t
			continue
		# device properties line
		if(re.match(sysvals.devpropfmt, line)):
			devProps(line)
			continue
		# ftrace line: parse only valid lines
		m = re.match(tp.ftrace_line_fmt, line)
		if(not m):
			continue
		# gather the basic message data from the line
		m_time = m.group('time')
		m_proc = m.group('proc')
		m_pid = m.group('pid')
		m_msg = m.group('msg')
		if(tp.cgformat):
			m_param3 = m.group('dur')
		else:
			m_param3 = 'traceevent'
		if(m_time and m_pid and m_msg):
			t = FTraceLine(m_time, m_msg, m_param3)
			pid = int(m_pid)
		else:
			continue
		# the line should be a call, return, or event
		if(not t.fcall and not t.freturn and not t.fevent):
			continue
		# find the start of suspend
		if(t.startMarker()):
			phase = 'suspend_prepare'
			data = Data(len(testdata))
			testdata.append(data)
			testrun = TestRun(data)
			testruns.append(testrun)
			parseStamp(tp.stamp, data)
			if len(tp.fwdata) > data.testnumber:
				data.fwSuspend, data.fwResume = tp.fwdata[data.testnumber]
				if(data.fwSuspend > 0 or data.fwResume > 0):
					data.fwValid = True
			data.setStart(t.time)
			continue
		if(not data):
			continue
		# find the end of resume
		if(t.endMarker()):
			if(sysvals.usetracemarkers and sysvals.postresumetime > 0):
				phase = 'post_resume'
				data.newPhase(phase, t.time, t.time, '#F0F0F0', -1)
			data.setEnd(t.time)
			if(not sysvals.usetracemarkers):
				# no trace markers? then quit and be sure to finish recording
				# the event we used to trigger resume end
				if(len(testrun.ttemp['thaw_processes']) > 0):
					# if an entry exists, assume this is its end
					testrun.ttemp['thaw_processes'][-1]['end'] = t.time
				break
			continue
		# trace event processing
		if(t.fevent):
			if(phase == 'post_resume'):
				data.setEnd(t.time)
			if(t.type == 'suspend_resume'):
				# suspend_resume trace events have two types, begin and end
				if(re.match('(?P<name>.*) begin$', t.name)):
					isbegin = True
				elif(re.match('(?P<name>.*) end$', t.name)):
					isbegin = False
				else:
					continue
				m = re.match('(?P<name>.*)\[(?P<val>[0-9]*)\] .*', t.name)
				if(m):
					val = m.group('val')
					if val == '0':
						name = m.group('name')
					else:
						name = m.group('name')+'['+val+']'
				else:
					m = re.match('(?P<name>.*) .*', t.name)
					name = m.group('name')
				# ignore these events
				if(name.split('[')[0] in tracewatch):
					continue
				# -- phase changes --
				# suspend_prepare start
				if(re.match('dpm_prepare\[.*', t.name)):
					phase = 'suspend_prepare'
					if(not isbegin):
						data.dmesg[phase]['end'] = t.time
					continue
				# suspend start
				elif(re.match('dpm_suspend\[.*', t.name)):
					phase = 'suspend'
					data.setPhase(phase, t.time, isbegin)
					continue
				# suspend_late start
				elif(re.match('dpm_suspend_late\[.*', t.name)):
					phase = 'suspend_late'
					data.setPhase(phase, t.time, isbegin)
					continue
				# suspend_noirq start
				elif(re.match('dpm_suspend_noirq\[.*', t.name)):
					phase = 'suspend_noirq'
					data.setPhase(phase, t.time, isbegin)
					if(not isbegin):
						phase = 'suspend_machine'
						data.dmesg[phase]['start'] = t.time
					continue
				# suspend_machine/resume_machine
				elif(re.match('machine_suspend\[.*', t.name)):
					if(isbegin):
						phase = 'suspend_machine'
						data.dmesg[phase]['end'] = t.time
						data.tSuspended = t.time
					else:
						if(sysvals.suspendmode in ['mem', 'disk'] and not tp.S0i3):
							data.dmesg['suspend_machine']['end'] = t.time
							data.tSuspended = t.time
						phase = 'resume_machine'
						data.dmesg[phase]['start'] = t.time
						data.tResumed = t.time
						data.tLow = data.tResumed - data.tSuspended
					continue
				# acpi_suspend
				elif(re.match('acpi_suspend\[.*', t.name)):
					# acpi_suspend[0] S0i3
					if(re.match('acpi_suspend\[0\] begin', t.name)):
						if(sysvals.suspendmode == 'mem'):
							tp.S0i3 = True
							data.dmesg['suspend_machine']['end'] = t.time
							data.tSuspended = t.time
					continue
				# resume_noirq start
				elif(re.match('dpm_resume_noirq\[.*', t.name)):
					phase = 'resume_noirq'
					data.setPhase(phase, t.time, isbegin)
					if(isbegin):
						data.dmesg['resume_machine']['end'] = t.time
					continue
				# resume_early start
				elif(re.match('dpm_resume_early\[.*', t.name)):
					phase = 'resume_early'
					data.setPhase(phase, t.time, isbegin)
					continue
				# resume start
				elif(re.match('dpm_resume\[.*', t.name)):
					phase = 'resume'
					data.setPhase(phase, t.time, isbegin)
					continue
				# resume complete start
				elif(re.match('dpm_complete\[.*', t.name)):
					phase = 'resume_complete'
					if(isbegin):
						data.dmesg[phase]['start'] = t.time
					continue
				# skip trace events inside devices calls
				if(not data.isTraceEventOutsideDeviceCalls(pid, t.time)):
					continue
				# global events (outside device calls) are graphed
				if(name not in testrun.ttemp):
					testrun.ttemp[name] = []
				if(isbegin):
					# create a new list entry
					testrun.ttemp[name].append(\
						{'begin': t.time, 'end': t.time, 'pid': pid})
				else:
					if(len(testrun.ttemp[name]) > 0):
						# if an entry exists, assume this is its end
						testrun.ttemp[name][-1]['end'] = t.time
					elif(phase == 'post_resume'):
						# post resume events can just have ends
						testrun.ttemp[name].append({
							'begin': data.dmesg[phase]['start'],
							'end': t.time})
			# device callback start
			elif(t.type == 'device_pm_callback_start'):
				m = re.match('(?P<drv>.*) (?P<d>.*), parent: *(?P<p>.*), .*',\
					t.name);
				if(not m):
					continue
				drv = m.group('drv')
				n = m.group('d')
				p = m.group('p')
				if(n and p):
					data.newAction(phase, n, pid, p, t.time, -1, drv)
			# device callback finish
			elif(t.type == 'device_pm_callback_end'):
				m = re.match('(?P<drv>.*) (?P<d>.*), err.*', t.name);
				if(not m):
					continue
				n = m.group('d')
				list = data.dmesg[phase]['list']
				if(n in list):
					dev = list[n]
					dev['length'] = t.time - dev['start']
					dev['end'] = t.time
		# kprobe event processing
		elif(t.fkprobe):
			kprobename = t.type
			kprobedata = t.name
			key = (kprobename, pid)
			# displayname is generated from kprobe data
			displayname = ''
			if(t.fcall):
				displayname = sysvals.kprobeDisplayName(kprobename, kprobedata)
				if not displayname:
					continue
				if(key not in tp.ktemp):
					tp.ktemp[key] = []
				tp.ktemp[key].append({
					'pid': pid,
					'begin': t.time,
					'end': t.time,
					'name': displayname,
					'cdata': kprobedata,
					'proc': m_proc,
				})
			elif(t.freturn):
				if(key not in tp.ktemp) or len(tp.ktemp[key]) < 1:
					continue
				e = tp.ktemp[key][-1]
				if e['begin'] < 0.0 or t.time - e['begin'] < 0.000001:
					tp.ktemp[key].pop()
				else:
					e['end'] = t.time
					e['rdata'] = kprobedata
		# callgraph processing
		elif sysvals.usecallgraph:
			# create a callgraph object for the data
			key = (m_proc, pid)
			if(key not in testrun.ftemp):
				testrun.ftemp[key] = []
				testrun.ftemp[key].append(FTraceCallGraph(pid))
			# when the call is finished, see which device matches it
			cg = testrun.ftemp[key][-1]
			if(cg.addLine(t)):
				testrun.ftemp[key].append(FTraceCallGraph(pid))
	tf.close()

	if sysvals.suspendmode == 'command':
		for test in testruns:
			for p in test.data.phases:
				if p == 'resume_complete':
					test.data.dmesg[p]['start'] = test.data.start
					test.data.dmesg[p]['end'] = test.data.end
				else:
					test.data.dmesg[p]['start'] = test.data.start
					test.data.dmesg[p]['end'] = test.data.start
			test.data.tSuspended = test.data.start
			test.data.tResumed = test.data.start
			test.data.tLow = 0
			test.data.fwValid = False

	for test in testruns:
		# add the traceevent data to the device hierarchy
		if(sysvals.usetraceevents):
			# add actual trace funcs
			for name in test.ttemp:
				for event in test.ttemp[name]:
					test.data.newActionGlobal(name, event['begin'], event['end'], event['pid'])
			# add the kprobe based virtual tracefuncs as actual devices
			for key in tp.ktemp:
				name, pid = key
				if name not in sysvals.tracefuncs:
					continue
				for e in tp.ktemp[key]:
					kb, ke = e['begin'], e['end']
					if kb == ke or not test.data.isInsideTimeline(kb, ke):
						continue
					test.data.newActionGlobal(e['name'], kb, ke, pid)
			# add config base kprobes and dev kprobes
			for key in tp.ktemp:
				name, pid = key
				if name in sysvals.tracefuncs:
					continue
				for e in tp.ktemp[key]:
					kb, ke = e['begin'], e['end']
					if kb == ke or not test.data.isInsideTimeline(kb, ke):
						continue
					color = sysvals.kprobeColor(e['name'])
					if name not in sysvals.dev_tracefuncs:
						# config base kprobe
						test.data.newActionGlobal(e['name'], kb, ke, -2, color)
					elif sysvals.usedevsrc:
						# dev kprobe
						data.addDeviceFunctionCall(e['name'], name, e['proc'], pid, kb,
							ke, e['cdata'], e['rdata'])
		if sysvals.usecallgraph:
			# add the callgraph data to the device hierarchy
			sortlist = dict()
			for key in test.ftemp:
				proc, pid = key
				for cg in test.ftemp[key]:
					if len(cg.list) < 1 or cg.invalid:
						continue
					if(not cg.postProcess()):
						id = 'task %s' % (pid)
						vprint('Sanity check failed for '+\
							id+', ignoring this callback')
						continue
					# match cg data to devices
					if sysvals.suspendmode == 'command' or not cg.deviceMatch(pid, test.data):
						sortkey = '%f%f%d' % (cg.start, cg.end, pid)
						sortlist[sortkey] = cg
			# create blocks for orphan cg data
			for sortkey in sorted(sortlist):
				cg = sortlist[sortkey]
				name = cg.list[0].name
				if sysvals.isCallgraphFunc(name):
					vprint('Callgraph found for task %d: %.3fms, %s' % (cg.pid, (cg.end - cg.start)*1000, name))
					cg.newActionFromFunction(test.data)

	if sysvals.suspendmode == 'command':
		if(sysvals.verbose):
			for data in testdata:
				data.printDetails()
		return testdata

	# fill in any missing phases
	for data in testdata:
		lp = data.phases[0]
		for p in data.phases:
			if(data.dmesg[p]['start'] < 0 and data.dmesg[p]['end'] < 0):
				print('WARNING: phase "%s" is missing!' % p)
			if(data.dmesg[p]['start'] < 0):
				data.dmesg[p]['start'] = data.dmesg[lp]['end']
				if(p == 'resume_machine'):
					data.tSuspended = data.dmesg[lp]['end']
					data.tResumed = data.dmesg[lp]['end']
					data.tLow = 0
			if(data.dmesg[p]['end'] < 0):
				data.dmesg[p]['end'] = data.dmesg[p]['start']
			lp = p

		if(len(sysvals.devicefilter) > 0):
			data.deviceFilter(sysvals.devicefilter)
		data.fixupInitcallsThatDidntReturn()
		if(sysvals.verbose):
			data.printDetails()

	return testdata

# Function: loadRawKernelLog
# Description:
#	 Load a raw kernel log that wasn't created by this tool, it might be
#	 possible to extract a valid suspend/resume log
def loadRawKernelLog(dmesgfile):
	global sysvals

	stamp = {'time': '', 'host': '', 'mode': 'mem', 'kernel': ''}
	stamp['time'] = datetime.now().strftime('%B %d %Y, %I:%M:%S %p')
	stamp['host'] = sysvals.hostname

	testruns = []
	data = 0
	lf = open(dmesgfile, 'r')
	for line in lf:
		line = line.replace('\r\n', '')
		idx = line.find('[')
		if idx > 1:
			line = line[idx:]
		m = re.match('[ \t]*(\[ *)(?P<ktime>[0-9\.]*)(\]) (?P<msg>.*)', line)
		if(not m):
			continue
		msg = m.group("msg")
		m = re.match('PM: Syncing filesystems.*', msg)
		if(m):
			if(data):
				testruns.append(data)
			data = Data(len(testruns))
			data.stamp = stamp
		if(data):
			m = re.match('.* *(?P<k>[0-9]\.[0-9]{2}\.[0-9]-.*) .*', msg)
			if(m):
				stamp['kernel'] = m.group('k')
			m = re.match('PM: Preparing system for (?P<m>.*) sleep', msg)
			if(m):
				stamp['mode'] = m.group('m')
			data.dmesgtext.append(line)
	if(data):
		testruns.append(data)
		sysvals.stamp = stamp
		sysvals.suspendmode = stamp['mode']
	lf.close()
	return testruns

# Function: loadKernelLog
# Description:
#	 [deprecated for kernel 3.15.0 or newer]
#	 load the dmesg file into memory and fix up any ordering issues
#	 The dmesg filename is taken from sysvals
# Output:
#	 An array of empty Data objects with only their dmesgtext attributes set
def loadKernelLog():
	global sysvals

	vprint('Analyzing the dmesg data...')
	if(os.path.exists(sysvals.dmesgfile) == False):
		doError('%s does not exist' % sysvals.dmesgfile, False)

	# there can be multiple test runs in a single file
	tp = TestProps()
	testruns = []
	data = 0
	lf = open(sysvals.dmesgfile, 'r')
	for line in lf:
		line = line.replace('\r\n', '')
		idx = line.find('[')
		if idx > 1:
			line = line[idx:]
		m = re.match(sysvals.stampfmt, line)
		if(m):
			tp.stamp = line
			continue
		m = re.match(sysvals.firmwarefmt, line)
		if(m):
			tp.fwdata.append((int(m.group('s')), int(m.group('r'))))
			continue
		m = re.match('[ \t]*(\[ *)(?P<ktime>[0-9\.]*)(\]) (?P<msg>.*)', line)
		if(not m):
			continue
		msg = m.group("msg")
		if(re.match('PM: Syncing filesystems.*', msg)):
			if(data):
				testruns.append(data)
			data = Data(len(testruns))
			parseStamp(tp.stamp, data)
			if len(tp.fwdata) > data.testnumber:
				data.fwSuspend, data.fwResume = tp.fwdata[data.testnumber]
				if(data.fwSuspend > 0 or data.fwResume > 0):
					data.fwValid = True
		if(re.match('ACPI: resume from mwait', msg)):
			print('NOTE: This suspend appears to be freeze rather than'+\
				' %s, it will be treated as such' % sysvals.suspendmode)
			sysvals.suspendmode = 'freeze'
		if(not data):
			continue
		data.dmesgtext.append(line)
	if(data):
		testruns.append(data)
	lf.close()

	if(len(testruns) < 1):
		# bad log, but see if you can extract something meaningful anyway
		testruns = loadRawKernelLog(sysvals.dmesgfile)

	if(len(testruns) < 1):
		doError(' dmesg log is completely unreadable: %s' \
			% sysvals.dmesgfile, False)

	# fix lines with same timestamp/function with the call and return swapped
	for data in testruns:
		last = ''
		for line in data.dmesgtext:
			mc = re.match('.*(\[ *)(?P<t>[0-9\.]*)(\]) calling  '+\
				'(?P<f>.*)\+ @ .*, parent: .*', line)
			mr = re.match('.*(\[ *)(?P<t>[0-9\.]*)(\]) call '+\
				'(?P<f>.*)\+ returned .* after (?P<dt>.*) usecs', last)
			if(mc and mr and (mc.group('t') == mr.group('t')) and
				(mc.group('f') == mr.group('f'))):
				i = data.dmesgtext.index(last)
				j = data.dmesgtext.index(line)
				data.dmesgtext[i] = line
				data.dmesgtext[j] = last
			last = line
	return testruns

# Function: parseKernelLog
# Description:
#	 [deprecated for kernel 3.15.0 or newer]
#	 Analyse a dmesg log output file generated from this app during
#	 the execution phase. Create a set of device structures in memory
#	 for subsequent formatting in the html output file
#	 This call is only for legacy support on kernels where the ftrace
#	 data lacks the suspend_resume or device_pm_callbacks trace events.
# Arguments:
#	 data: an empty Data object (with dmesgtext) obtained from loadKernelLog
# Output:
#	 The filled Data object
def parseKernelLog(data):
	global sysvals

	phase = 'suspend_runtime'

	if(data.fwValid):
		vprint('Firmware Suspend = %u ns, Firmware Resume = %u ns' % \
			(data.fwSuspend, data.fwResume))

	# dmesg phase match table
	dm = {
		'suspend_prepare': 'PM: Syncing filesystems.*',
		        'suspend': 'PM: Entering [a-z]* sleep.*',
		   'suspend_late': 'PM: suspend of devices complete after.*',
		  'suspend_noirq': 'PM: late suspend of devices complete after.*',
		'suspend_machine': 'PM: noirq suspend of devices complete after.*',
		 'resume_machine': 'ACPI: Low-level resume complete.*',
		   'resume_noirq': 'ACPI: Waking up from system sleep state.*',
		   'resume_early': 'PM: noirq resume of devices complete after.*',
		         'resume': 'PM: early resume of devices complete after.*',
		'resume_complete': 'PM: resume of devices complete after.*',
		    'post_resume': '.*Restarting tasks \.\.\..*',
	}
	if(sysvals.suspendmode == 'standby'):
		dm['resume_machine'] = 'PM: Restoring platform NVS memory'
	elif(sysvals.suspendmode == 'disk'):
		dm['suspend_late'] = 'PM: freeze of devices complete after.*'
		dm['suspend_noirq'] = 'PM: late freeze of devices complete after.*'
		dm['suspend_machine'] = 'PM: noirq freeze of devices complete after.*'
		dm['resume_machine'] = 'PM: Restoring platform NVS memory'
		dm['resume_early'] = 'PM: noirq restore of devices complete after.*'
		dm['resume'] = 'PM: early restore of devices complete after.*'
		dm['resume_complete'] = 'PM: restore of devices complete after.*'
	elif(sysvals.suspendmode == 'freeze'):
		dm['resume_machine'] = 'ACPI: resume from mwait'

	# action table (expected events that occur and show up in dmesg)
	at = {
		'sync_filesystems': {
			'smsg': 'PM: Syncing filesystems.*',
			'emsg': 'PM: Preparing system for mem sleep.*' },
		'freeze_user_processes': {
			'smsg': 'Freezing user space processes .*',
			'emsg': 'Freezing remaining freezable tasks.*' },
		'freeze_tasks': {
			'smsg': 'Freezing remaining freezable tasks.*',
			'emsg': 'PM: Entering (?P<mode>[a-z,A-Z]*) sleep.*' },
		'ACPI prepare': {
			'smsg': 'ACPI: Preparing to enter system sleep state.*',
			'emsg': 'PM: Saving platform NVS memory.*' },
		'PM vns': {
			'smsg': 'PM: Saving platform NVS memory.*',
			'emsg': 'Disabling non-boot CPUs .*' },
	}

	t0 = -1.0
	cpu_start = -1.0
	prevktime = -1.0
	actions = dict()
	for line in data.dmesgtext:
		# -- preprocessing --
		# parse each dmesg line into the time and message
		m = re.match('[ \t]*(\[ *)(?P<ktime>[0-9\.]*)(\]) (?P<msg>.*)', line)
		if(m):
			val = m.group('ktime')
			try:
				ktime = float(val)
			except:
				doWarning('INVALID DMESG LINE: '+\
					line.replace('\n', ''), 'dmesg')
				continue
			msg = m.group('msg')
			# initialize data start to first line time
			if t0 < 0:
				data.setStart(ktime)
				t0 = ktime
		else:
			continue

		# hack for determining resume_machine end for freeze
		if(not sysvals.usetraceevents and sysvals.suspendmode == 'freeze' \
			and phase == 'resume_machine' and \
			re.match('calling  (?P<f>.*)\+ @ .*, parent: .*', msg)):
			data.dmesg['resume_machine']['end'] = ktime
			phase = 'resume_noirq'
			data.dmesg[phase]['start'] = ktime

		# -- phase changes --
		# suspend start
		if(re.match(dm['suspend_prepare'], msg)):
			phase = 'suspend_prepare'
			data.dmesg[phase]['start'] = ktime
			data.setStart(ktime)
		# suspend start
		elif(re.match(dm['suspend'], msg)):
			data.dmesg['suspend_prepare']['end'] = ktime
			phase = 'suspend'
			data.dmesg[phase]['start'] = ktime
		# suspend_late start
		elif(re.match(dm['suspend_late'], msg)):
			data.dmesg['suspend']['end'] = ktime
			phase = 'suspend_late'
			data.dmesg[phase]['start'] = ktime
		# suspend_noirq start
		elif(re.match(dm['suspend_noirq'], msg)):
			data.dmesg['suspend_late']['end'] = ktime
			phase = 'suspend_noirq'
			data.dmesg[phase]['start'] = ktime
		# suspend_machine start
		elif(re.match(dm['suspend_machine'], msg)):
			data.dmesg['suspend_noirq']['end'] = ktime
			phase = 'suspend_machine'
			data.dmesg[phase]['start'] = ktime
		# resume_machine start
		elif(re.match(dm['resume_machine'], msg)):
			if(sysvals.suspendmode in ['freeze', 'standby']):
				data.tSuspended = prevktime
				data.dmesg['suspend_machine']['end'] = prevktime
			else:
				data.tSuspended = ktime
				data.dmesg['suspend_machine']['end'] = ktime
			phase = 'resume_machine'
			data.tResumed = ktime
			data.tLow = data.tResumed - data.tSuspended
			data.dmesg[phase]['start'] = ktime
		# resume_noirq start
		elif(re.match(dm['resume_noirq'], msg)):
			data.dmesg['resume_machine']['end'] = ktime
			phase = 'resume_noirq'
			data.dmesg[phase]['start'] = ktime
		# resume_early start
		elif(re.match(dm['resume_early'], msg)):
			data.dmesg['resume_noirq']['end'] = ktime
			phase = 'resume_early'
			data.dmesg[phase]['start'] = ktime
		# resume start
		elif(re.match(dm['resume'], msg)):
			data.dmesg['resume_early']['end'] = ktime
			phase = 'resume'
			data.dmesg[phase]['start'] = ktime
		# resume complete start
		elif(re.match(dm['resume_complete'], msg)):
			data.dmesg['resume']['end'] = ktime
			phase = 'resume_complete'
			data.dmesg[phase]['start'] = ktime
		# post resume start
		elif(re.match(dm['post_resume'], msg)):
			data.dmesg['resume_complete']['end'] = ktime
			data.setEnd(ktime)
			phase = 'post_resume'
			break

		# -- device callbacks --
		if(phase in data.phases):
			# device init call
			if(re.match('calling  (?P<f>.*)\+ @ .*, parent: .*', msg)):
				sm = re.match('calling  (?P<f>.*)\+ @ '+\
					'(?P<n>.*), parent: (?P<p>.*)', msg);
				f = sm.group('f')
				n = sm.group('n')
				p = sm.group('p')
				if(f and n and p):
					data.newAction(phase, f, int(n), p, ktime, -1, '')
			# device init return
			elif(re.match('call (?P<f>.*)\+ returned .* after '+\
				'(?P<t>.*) usecs', msg)):
				sm = re.match('call (?P<f>.*)\+ returned .* after '+\
					'(?P<t>.*) usecs(?P<a>.*)', msg);
				f = sm.group('f')
				t = sm.group('t')
				list = data.dmesg[phase]['list']
				if(f in list):
					dev = list[f]
					dev['length'] = int(t)
					dev['end'] = ktime

		# -- non-devicecallback actions --
		# if trace events are not available, these are better than nothing
		if(not sysvals.usetraceevents):
			# look for known actions
			for a in at:
				if(re.match(at[a]['smsg'], msg)):
					if(a not in actions):
						actions[a] = []
					actions[a].append({'begin': ktime, 'end': ktime})
				if(re.match(at[a]['emsg'], msg)):
					if(a in actions):
						actions[a][-1]['end'] = ktime
			# now look for CPU on/off events
			if(re.match('Disabling non-boot CPUs .*', msg)):
				# start of first cpu suspend
				cpu_start = ktime
			elif(re.match('Enabling non-boot CPUs .*', msg)):
				# start of first cpu resume
				cpu_start = ktime
			elif(re.match('smpboot: CPU (?P<cpu>[0-9]*) is now offline', msg)):
				# end of a cpu suspend, start of the next
				m = re.match('smpboot: CPU (?P<cpu>[0-9]*) is now offline', msg)
				cpu = 'CPU'+m.group('cpu')
				if(cpu not in actions):
					actions[cpu] = []
				actions[cpu].append({'begin': cpu_start, 'end': ktime})
				cpu_start = ktime
			elif(re.match('CPU(?P<cpu>[0-9]*) is up', msg)):
				# end of a cpu resume, start of the next
				m = re.match('CPU(?P<cpu>[0-9]*) is up', msg)
				cpu = 'CPU'+m.group('cpu')
				if(cpu not in actions):
					actions[cpu] = []
				actions[cpu].append({'begin': cpu_start, 'end': ktime})
				cpu_start = ktime
		prevktime = ktime

	# fill in any missing phases
	lp = data.phases[0]
	for p in data.phases:
		if(data.dmesg[p]['start'] < 0 and data.dmesg[p]['end'] < 0):
			print('WARNING: phase "%s" is missing, something went wrong!' % p)
			print('    In %s, this dmesg line denotes the start of %s:' % \
				(sysvals.suspendmode, p))
			print('        "%s"' % dm[p])
		if(data.dmesg[p]['start'] < 0):
			data.dmesg[p]['start'] = data.dmesg[lp]['end']
			if(p == 'resume_machine'):
				data.tSuspended = data.dmesg[lp]['end']
				data.tResumed = data.dmesg[lp]['end']
				data.tLow = 0
		if(data.dmesg[p]['end'] < 0):
			data.dmesg[p]['end'] = data.dmesg[p]['start']
		lp = p

	# fill in any actions we've found
	for name in actions:
		for event in actions[name]:
			data.newActionGlobal(name, event['begin'], event['end'])

	if(sysvals.verbose):
		data.printDetails()
	if(len(sysvals.devicefilter) > 0):
		data.deviceFilter(sysvals.devicefilter)
	data.fixupInitcallsThatDidntReturn()
	return True

# Function: createHTMLSummarySimple
# Description:
#	 Create summary html file for a series of tests
# Arguments:
#	 testruns: array of Data objects from parseTraceLog
def createHTMLSummarySimple(testruns, htmlfile):
	global sysvals

	# print out the basic summary of all the tests
	hf = open(htmlfile, 'w')

	# write the html header first (html head, css code, up to body start)
	html = '<!DOCTYPE html>\n<html>\n<head>\n\
	<meta http-equiv="content-type" content="text/html; charset=UTF-8">\n\
	<title>AnalyzeSuspend Summary</title>\n\
	<style type=\'text/css\'>\n\
		body {overflow-y: scroll;}\n\
		.stamp {width: 100%;text-align:center;background-color:#495E09;line-height:30px;color:white;font: 25px Arial;}\n\
		table {width:100%;border-collapse: collapse;}\n\
		.summary {font: 22px Arial;border:1px solid;}\n\
		th {border: 1px solid black;background-color:#A7C942;color:white;}\n\
		td {text-align: center;}\n\
		tr.alt td {background-color:#EAF2D3;}\n\
		tr.avg td {background-color:#BDE34C;}\n\
		a:link {color: #90B521;}\n\
		a:visited {color: #495E09;}\n\
		a:hover {color: #B1DF28;}\n\
		a:active {color: #FFFFFF;}\n\
	</style>\n</head>\n<body>\n'

	# group test header
	count = len(testruns)
	headline_stamp = '<div class="stamp">{0} {1} {2} {3} ({4} tests)</div>\n'
	html += headline_stamp.format(sysvals.stamp['host'],
		sysvals.stamp['kernel'], sysvals.stamp['mode'],
		sysvals.stamp['time'], count)

	# check to see if all the tests have the same value
	stampcolumns = False
	for data in testruns:
		if diffStamp(sysvals.stamp, data.stamp):
			stampcolumns = True
			break

	th = '\t<th>{0}</th>\n'
	td = '\t<td>{0}</td>\n'
	tdlink = '\t<td><a href="{0}">Click Here</a></td>\n'

	# table header
	html += '<table class="summary">\n<tr>\n'
	html += th.format("Test #")
	if stampcolumns:
		html += th.format("Hostname")
		html += th.format("Kernel Version")
		html += th.format("Suspend Mode")
	html += th.format("Test Time")
	html += th.format("Suspend Time")
	html += th.format("Resume Time")
	html += th.format("Detail")
	html += '</tr>\n'

	# test data, 1 row per test
	sTimeAvg = 0.0
	rTimeAvg = 0.0
	num = 1
	for data in testruns:
		# data.end is the end of post_resume
		resumeEnd = data.dmesg['resume_complete']['end']
		if num % 2 == 1:
			html += '<tr class="alt">\n'
		else:
			html += '<tr>\n'

		# test num
		html += td.format("test %d" % num)
		num += 1
		if stampcolumns:
			# host name
			val = "unknown"
			if('host' in data.stamp):
				val = data.stamp['host']
			html += td.format(val)
			# host kernel
			val = "unknown"
			if('kernel' in data.stamp):
				val = data.stamp['kernel']
			html += td.format(val)
			# suspend mode
			val = "unknown"
			if('mode' in data.stamp):
				val = data.stamp['mode']
			html += td.format(val)
		# test time
		val = "unknown"
		if('time' in data.stamp):
			val = data.stamp['time']
		html += td.format(val)
		# suspend time
		sTime = (data.tSuspended - data.start)*1000
		sTimeAvg += sTime
		html += td.format("%3.3f ms" % sTime)
		# resume time
		rTime = (resumeEnd - data.tResumed)*1000
		rTimeAvg += rTime
		html += td.format("%3.3f ms" % rTime)
		# link to the output html
		html += tdlink.format(data.outfile)

		html += '</tr>\n'

	# last line: test average
	if(count > 0):
		sTimeAvg /= count
		rTimeAvg /= count
	html += '<tr class="avg">\n'
	html += td.format('Average') 	# name
	if stampcolumns:
		html += td.format('')			# host
		html += td.format('')			# kernel
		html += td.format('')			# mode
	html += td.format('')			# time
	html += td.format("%3.3f ms" % sTimeAvg)	# suspend time
	html += td.format("%3.3f ms" % rTimeAvg)	# resume time
	html += td.format('')			# output link
	html += '</tr>\n'

	# flush the data to file
	hf.write(html+'</table>\n')
	hf.write('</body>\n</html>\n')
	hf.close()

def htmlTitle():
	global sysvals
	modename = {
		'freeze': 'Freeze (S0)',
		'standby': 'Standby (S1)',
		'mem': 'Suspend (S3)',
		'disk': 'Hibernate (S4)'
	}
	kernel = sysvals.stamp['kernel']
	host = sysvals.hostname[0].upper()+sysvals.hostname[1:]
	mode = sysvals.suspendmode
	if sysvals.suspendmode in modename:
		mode = modename[sysvals.suspendmode]
	return host+' '+mode+' '+kernel

def ordinal(value):
	suffix = 'th'
	if value < 10 or value > 19:
		if value % 10 == 1:
			suffix = 'st'
		elif value % 10 == 2:
			suffix = 'nd'
		elif value % 10 == 3:
			suffix = 'rd'
	return '%d%s' % (value, suffix)

# Function: createHTML
# Description:
#	 Create the output html file from the resident test data
# Arguments:
#	 testruns: array of Data objects from parseKernelLog or parseTraceLog
# Output:
#	 True if the html file was created, false if it failed
def createHTML(testruns):
	global sysvals

	if len(testruns) < 1:
		print('ERROR: Not enough test data to build a timeline')
		return

	for data in testruns:
		data.normalizeTime(testruns[-1].tSuspended)

	x2changes = ['', 'absolute']
	if len(testruns) > 1:
		x2changes = ['1', 'relative']
	# html function templates
	headline_version = '<div class="version"><a href="https://01.org/suspendresume">AnalyzeSuspend v%s</a></div>' % sysvals.version
	headline_stamp = '<div class="stamp">{0} {1} {2} {3}</div>\n'
	html_devlist1 = '<button id="devlist1" class="devlist" style="float:left;">Device Detail%s</button>' % x2changes[0]
	html_zoombox = '<center><button id="zoomin">ZOOM IN</button><button id="zoomout">ZOOM OUT</button><button id="zoomdef">ZOOM 1:1</button></center>\n'
	html_devlist2 = '<button id="devlist2" class="devlist" style="float:right;">Device Detail2</button>\n'
	html_timeline = '<div id="dmesgzoombox" class="zoombox">\n<div id="{0}" class="timeline" style="height:{1}px">\n'
	html_tblock = '<div id="block{0}" class="tblock" style="left:{1}%;width:{2}%;">\n'
	html_device = '<div id="{0}" title="{1}" class="thread{7}" style="left:{2}%;top:{3}px;height:{4}px;width:{5}%;{8}">{6}</div>\n'
	html_traceevent = '<div title="{0}" class="traceevent" style="left:{1}%;top:{2}px;height:{3}px;width:{4}%;line-height:{3}px;">{5}</div>\n'
	html_phase = '<div class="phase" style="left:{0}%;width:{1}%;top:{2}px;height:{3}px;background-color:{4}">{5}</div>\n'
	html_phaselet = '<div id="{0}" class="phaselet" style="left:{1}%;width:{2}%;background-color:{3}"></div>\n'
	html_legend = '<div id="p{3}" class="square" style="left:{0}%;background-color:{1}">&nbsp;{2}</div>\n'
	html_timetotal = '<table class="time1">\n<tr>'\
		'<td class="green">{2} Suspend Time: <b>{0} ms</b></td>'\
		'<td class="yellow">{2} Resume Time: <b>{1} ms</b></td>'\
		'</tr>\n</table>\n'
	html_timetotal2 = '<table class="time1">\n<tr>'\
		'<td class="green">{3} Suspend Time: <b>{0} ms</b></td>'\
		'<td class="gray">'+sysvals.suspendmode+' time: <b>{1} ms</b></td>'\
		'<td class="yellow">{3} Resume Time: <b>{2} ms</b></td>'\
		'</tr>\n</table>\n'
	html_timetotal3 = '<table class="time1">\n<tr>'\
		'<td class="green">Execution Time: <b>{0} ms</b></td>'\
		'<td class="yellow">Command: <b>{1}</b></td>'\
		'</tr>\n</table>\n'
	html_timegroups = '<table class="time2">\n<tr>'\
		'<td class="green">{4}Kernel Suspend: {0} ms</td>'\
		'<td class="purple">{4}Firmware Suspend: {1} ms</td>'\
		'<td class="purple">{4}Firmware Resume: {2} ms</td>'\
		'<td class="yellow">{4}Kernel Resume: {3} ms</td>'\
		'</tr>\n</table>\n'

	# html format variables
	rowheight = 30
	devtextS = '14px'
	devtextH = '30px'
	hoverZ = 'z-index:10;'

	if sysvals.usedevsrc:
		hoverZ = ''

	# device timeline
	vprint('Creating Device Timeline...')

	devtl = Timeline(rowheight)

	# Generate the header for this timeline
	for data in testruns:
		tTotal = data.end - data.start
		tEnd = data.dmesg['resume_complete']['end']
		if(tTotal == 0):
			print('ERROR: No timeline data')
			sys.exit()
		if(data.tLow > 0):
			low_time = '%.0f'%(data.tLow*1000)
		if sysvals.suspendmode == 'command':
			run_time = '%.0f'%((data.end-data.start)*1000)
			if sysvals.testcommand:
				testdesc = sysvals.testcommand
			else:
				testdesc = 'unknown'
			if(len(testruns) > 1):
				testdesc = ordinal(data.testnumber+1)+' '+testdesc
			thtml = html_timetotal3.format(run_time, testdesc)
			devtl.html['header'] += thtml
		elif data.fwValid:
			suspend_time = '%.0f'%((data.tSuspended-data.start)*1000 + \
				(data.fwSuspend/1000000.0))
			resume_time = '%.0f'%((tEnd-data.tSuspended)*1000 + \
				(data.fwResume/1000000.0))
			testdesc1 = 'Total'
			testdesc2 = ''
			if(len(testruns) > 1):
				testdesc1 = testdesc2 = ordinal(data.testnumber+1)
				testdesc2 += ' '
			if(data.tLow == 0):
				thtml = html_timetotal.format(suspend_time, \
					resume_time, testdesc1)
			else:
				thtml = html_timetotal2.format(suspend_time, low_time, \
					resume_time, testdesc1)
			devtl.html['header'] += thtml
			sktime = '%.3f'%((data.dmesg['suspend_machine']['end'] - \
				data.getStart())*1000)
			sftime = '%.3f'%(data.fwSuspend / 1000000.0)
			rftime = '%.3f'%(data.fwResume / 1000000.0)
			rktime = '%.3f'%((data.dmesg['resume_complete']['end'] - \
				data.dmesg['resume_machine']['start'])*1000)
			devtl.html['header'] += html_timegroups.format(sktime, \
				sftime, rftime, rktime, testdesc2)
		else:
			suspend_time = '%.0f'%((data.tSuspended-data.start)*1000)
			resume_time = '%.0f'%((tEnd-data.tSuspended)*1000)
			testdesc = 'Kernel'
			if(len(testruns) > 1):
				testdesc = ordinal(data.testnumber+1)+' '+testdesc
			if(data.tLow == 0):
				thtml = html_timetotal.format(suspend_time, \
					resume_time, testdesc)
			else:
				thtml = html_timetotal2.format(suspend_time, low_time, \
					resume_time, testdesc)
			devtl.html['header'] += thtml

	# time scale for potentially multiple datasets
	t0 = testruns[0].start
	tMax = testruns[-1].end
	tSuspended = testruns[-1].tSuspended
	tTotal = tMax - t0

	# determine the maximum number of rows we need to draw
	for data in testruns:
		data.selectTimelineDevices('%f', tTotal, sysvals.mindevlen)
		for group in data.devicegroups:
			devlist = []
			for phase in group:
				for devname in data.tdevlist[phase]:
					devlist.append((phase,devname))
			devtl.getPhaseRows(data.dmesg, devlist)
	devtl.calcTotalRows()

	# create bounding box, add buttons
	if sysvals.suspendmode != 'command':
		devtl.html['timeline'] += html_devlist1
		if len(testruns) > 1:
			devtl.html['timeline'] += html_devlist2
	devtl.html['timeline'] += html_zoombox
	devtl.html['timeline'] += html_timeline.format('dmesg', devtl.height)

	# draw the full timeline
	phases = {'suspend':[],'resume':[]}
	for phase in data.dmesg:
		if 'resume' in phase:
			phases['resume'].append(phase)
		else:
			phases['suspend'].append(phase)

	# draw each test run chronologically
	for data in testruns:
		# if nore than one test, draw a block to represent user mode
		if(data.testnumber > 0):
			m0 = testruns[data.testnumber-1].end
			mMax = testruns[data.testnumber].start
			mTotal = mMax - m0
			name = 'usermode%d' % data.testnumber
			top = '%d' % devtl.scaleH
			left = '%f' % (((m0-t0)*100.0)/tTotal)
			width = '%f' % ((mTotal*100.0)/tTotal)
			title = 'user mode (%0.3f ms) ' % (mTotal*1000)
			devtl.html['timeline'] += html_device.format(name, \
				title, left, top, '%d'%devtl.bodyH, width, '', '', '')
		# now draw the actual timeline blocks
		for dir in phases:
			# draw suspend and resume blocks separately
			bname = '%s%d' % (dir[0], data.testnumber)
			if dir == 'suspend':
				m0 = testruns[data.testnumber].start
				mMax = testruns[data.testnumber].tSuspended
				mTotal = mMax - m0
				left = '%f' % (((m0-t0)*100.0)/tTotal)
			else:
				m0 = testruns[data.testnumber].tSuspended
				mMax = testruns[data.testnumber].end
				mTotal = mMax - m0
				left = '%f' % ((((m0-t0)*100.0)+sysvals.srgap/2)/tTotal)
			# if a timeline block is 0 length, skip altogether
			if mTotal == 0:
				continue
			width = '%f' % (((mTotal*100.0)-sysvals.srgap/2)/tTotal)
			devtl.html['timeline'] += html_tblock.format(bname, left, width)
			for b in sorted(phases[dir]):
				# draw the phase color background
				phase = data.dmesg[b]
				length = phase['end']-phase['start']
				left = '%f' % (((phase['start']-m0)*100.0)/mTotal)
				width = '%f' % ((length*100.0)/mTotal)
				devtl.html['timeline'] += html_phase.format(left, width, \
					'%.3f'%devtl.scaleH, '%.3f'%devtl.bodyH, \
					data.dmesg[b]['color'], '')
				# draw the devices for this phase
				phaselist = data.dmesg[b]['list']
				for d in data.tdevlist[b]:
					name = d
					drv = ''
					dev = phaselist[d]
					xtraclass = ''
					xtrainfo = ''
					xtrastyle = ''
					if 'htmlclass' in dev:
						xtraclass = dev['htmlclass']
						xtrainfo = dev['htmlclass']
					if 'color' in dev:
						xtrastyle = 'background-color:%s;' % dev['color']
					if(d in sysvals.devprops):
						name = sysvals.devprops[d].altName(d)
						xtraclass = sysvals.devprops[d].xtraClass()
						xtrainfo = sysvals.devprops[d].xtraInfo()
					if('drv' in dev and dev['drv']):
						drv = ' {%s}' % dev['drv']
					rowheight = devtl.phaseRowHeight(b, dev['row'])
					rowtop = devtl.phaseRowTop(b, dev['row'])
					top = '%.3f' % (rowtop + devtl.scaleH)
					left = '%f' % (((dev['start']-m0)*100)/mTotal)
					width = '%f' % (((dev['end']-dev['start'])*100)/mTotal)
					length = ' (%0.3f ms) ' % ((dev['end']-dev['start'])*1000)
					if sysvals.suspendmode == 'command':
						title = name+drv+xtrainfo+length+'cmdexec'
					else:
						title = name+drv+xtrainfo+length+b
					devtl.html['timeline'] += html_device.format(dev['id'], \
						title, left, top, '%.3f'%rowheight, width, \
						d+drv, xtraclass, xtrastyle)
					if('src' not in dev):
						continue
					# draw any trace events for this device
					vprint('Debug trace events found for device %s' % d)
					vprint('%20s %20s %10s %8s' % ('title', \
						'name', 'time(ms)', 'length(ms)'))
					for e in dev['src']:
						vprint('%20s %20s %10.3f %8.3f' % (e.title, \
							e.text, e.time*1000, e.length*1000))
						height = devtl.rowH
						top = '%.3f' % (rowtop + devtl.scaleH + (e.row*devtl.rowH))
						left = '%f' % (((e.time-m0)*100)/mTotal)
						width = '%f' % (e.length*100/mTotal)
						color = 'rgba(204,204,204,0.5)'
						devtl.html['timeline'] += \
							html_traceevent.format(e.title, \
								left, top, '%.3f'%height, \
								width, e.text)
			# draw the time scale, try to make the number of labels readable
			devtl.html['timeline'] += devtl.createTimeScale(m0, mMax, tTotal, dir)
			devtl.html['timeline'] += '</div>\n'

	# timeline is finished
	devtl.html['timeline'] += '</div>\n</div>\n'

	# draw a legend which describes the phases by color
	if sysvals.suspendmode != 'command':
		data = testruns[-1]
		devtl.html['legend'] = '<div class="legend">\n'
		pdelta = 100.0/len(data.phases)
		pmargin = pdelta / 4.0
		for phase in data.phases:
			tmp = phase.split('_')
			id = tmp[0][0]
			if(len(tmp) > 1):
				id += tmp[1][0]
			order = '%.2f' % ((data.dmesg[phase]['order'] * pdelta) + pmargin)
			name = string.replace(phase, '_', ' &nbsp;')
			devtl.html['legend'] += html_legend.format(order, \
				data.dmesg[phase]['color'], name, id)
		devtl.html['legend'] += '</div>\n'

	hf = open(sysvals.htmlfile, 'w')

	if not sysvals.cgexp:
		cgchk = 'checked'
		cgnchk = 'not(:checked)'
	else:
		cgchk = 'not(:checked)'
		cgnchk = 'checked'

	# write the html header first (html head, css code, up to body start)
	html_header = '<!DOCTYPE html>\n<html>\n<head>\n\
	<meta http-equiv="content-type" content="text/html; charset=UTF-8">\n\
	<title>'+htmlTitle()+'</title>\n\
	<style type=\'text/css\'>\n\
		body {overflow-y:scroll;}\n\
		.stamp {width:100%;text-align:center;background-color:gray;line-height:30px;color:white;font:25px Arial;}\n\
		.callgraph {margin-top:30px;box-shadow:5px 5px 20px black;}\n\
		.callgraph article * {padding-left:28px;}\n\
		h1 {color:black;font:bold 30px Times;}\n\
		t0 {color:black;font:bold 30px Times;}\n\
		t1 {color:black;font:30px Times;}\n\
		t2 {color:black;font:25px Times;}\n\
		t3 {color:black;font:20px Times;white-space:nowrap;}\n\
		t4 {color:black;font:bold 30px Times;line-height:60px;white-space:nowrap;}\n\
		cS {color:blue;font:bold 11px Times;}\n\
		cR {color:red;font:bold 11px Times;}\n\
		table {width:100%;}\n\
		.gray {background-color:rgba(80,80,80,0.1);}\n\
		.green {background-color:rgba(204,255,204,0.4);}\n\
		.purple {background-color:rgba(128,0,128,0.2);}\n\
		.yellow {background-color:rgba(255,255,204,0.4);}\n\
		.time1 {font:22px Arial;border:1px solid;}\n\
		.time2 {font:15px Arial;border-bottom:1px solid;border-left:1px solid;border-right:1px solid;}\n\
		td {text-align:center;}\n\
		r {color:#500000;font:15px Tahoma;}\n\
		n {color:#505050;font:15px Tahoma;}\n\
		.tdhl {color:red;}\n\
		.hide {display:none;}\n\
		.pf {display:none;}\n\
		.pf:'+cgchk+' + label {background:url(\'data:image/svg+xml;utf,<?xml version="1.0" standalone="no"?><svg xmlns="http://www.w3.org/2000/svg" height="18" width="18" version="1.1"><circle cx="9" cy="9" r="8" stroke="black" stroke-width="1" fill="white"/><rect x="4" y="8" width="10" height="2" style="fill:black;stroke-width:0"/><rect x="8" y="4" width="2" height="10" style="fill:black;stroke-width:0"/></svg>\') no-repeat left center;}\n\
		.pf:'+cgnchk+' ~ label {background:url(\'data:image/svg+xml;utf,<?xml version="1.0" standalone="no"?><svg xmlns="http://www.w3.org/2000/svg" height="18" width="18" version="1.1"><circle cx="9" cy="9" r="8" stroke="black" stroke-width="1" fill="white"/><rect x="4" y="8" width="10" height="2" style="fill:black;stroke-width:0"/></svg>\') no-repeat left center;}\n\
		.pf:'+cgchk+' ~ *:not(:nth-child(2)) {display:none;}\n\
		.zoombox {position:relative;width:100%;overflow-x:scroll;}\n\
		.timeline {position:relative;font-size:14px;cursor:pointer;width:100%; overflow:hidden;background:linear-gradient(#cccccc, white);}\n\
		.thread {position:absolute;height:0%;overflow:hidden;line-height:'+devtextH+';font-size:'+devtextS+';border:1px solid;text-align:center;white-space:nowrap;background-color:rgba(204,204,204,0.5);}\n\
		.thread.sync {background-color:'+sysvals.synccolor+';}\n\
		.thread.bg {background-color:'+sysvals.kprobecolor+';}\n\
		.thread:hover {background-color:white;border:1px solid red;'+hoverZ+'}\n\
		.hover {background-color:white;border:1px solid red;'+hoverZ+'}\n\
		.hover.sync {background-color:white;}\n\
		.hover.bg {background-color:white;}\n\
		.traceevent {position:absolute;font-size:10px;overflow:hidden;color:black;text-align:center;white-space:nowrap;border-radius:5px;border:1px solid black;background:linear-gradient(to bottom right,rgba(204,204,204,1),rgba(150,150,150,1));}\n\
		.traceevent:hover {background:white;}\n\
		.phase {position:absolute;overflow:hidden;border:0px;text-align:center;}\n\
		.phaselet {position:absolute;overflow:hidden;border:0px;text-align:center;height:100px;font-size:24px;}\n\
		.t {z-index:2;position:absolute;pointer-events:none;top:0%;height:100%;border-right:1px solid black;}\n\
		.legend {position:relative; width:100%; height:40px; text-align:center;margin-bottom:20px}\n\
		.legend .square {position:absolute;cursor:pointer;top:10px; width:0px;height:20px;border:1px solid;padding-left:20px;}\n\
		button {height:40px;width:200px;margin-bottom:20px;margin-top:20px;font-size:24px;}\n\
		.logbtn {position:relative;float:right;height:25px;width:50px;margin-top:3px;margin-bottom:0;font-size:10px;text-align:center;}\n\
		.devlist {position:'+x2changes[1]+';width:190px;}\n\
		a:link {color:white;text-decoration:none;}\n\
		a:visited {color:white;}\n\
		a:hover {color:white;}\n\
		a:active {color:white;}\n\
		.version {position:relative;float:left;color:white;font-size:10px;line-height:30px;margin-left:10px;}\n\
		#devicedetail {height:100px;box-shadow:5px 5px 20px black;}\n\
		.tblock {position:absolute;height:100%;}\n\
		.bg {z-index:1;}\n\
	</style>\n</head>\n<body>\n'

	# no header or css if its embedded
	if(sysvals.embedded):
		hf.write('pass True tSus %.3f tRes %.3f tLow %.3f fwvalid %s tSus %.3f tRes %.3f\n' %
			(data.tSuspended-data.start, data.end-data.tSuspended, data.tLow, data.fwValid, \
				data.fwSuspend/1000000, data.fwResume/1000000))
	else:
		hf.write(html_header)

	# write the test title and general info header
	if(sysvals.stamp['time'] != ""):
		hf.write(headline_version)
		if sysvals.addlogs and sysvals.dmesgfile:
			hf.write('<button id="showdmesg" class="logbtn">dmesg</button>')
		if sysvals.addlogs and sysvals.ftracefile:
			hf.write('<button id="showftrace" class="logbtn">ftrace</button>')
		hf.write(headline_stamp.format(sysvals.stamp['host'],
			sysvals.stamp['kernel'], sysvals.stamp['mode'], \
				sysvals.stamp['time']))

	# write the device timeline
	hf.write(devtl.html['header'])
	hf.write(devtl.html['timeline'])
	hf.write(devtl.html['legend'])
	hf.write('<div id="devicedetailtitle"></div>\n')
	hf.write('<div id="devicedetail" style="display:none;">\n')
	# draw the colored boxes for the device detail section
	for data in testruns:
		hf.write('<div id="devicedetail%d">\n' % data.testnumber)
		for b in data.phases:
			phase = data.dmesg[b]
			length = phase['end']-phase['start']
			left = '%.3f' % (((phase['start']-t0)*100.0)/tTotal)
			width = '%.3f' % ((length*100.0)/tTotal)
			hf.write(html_phaselet.format(b, left, width, \
				data.dmesg[b]['color']))
		if sysvals.suspendmode == 'command':
			hf.write(html_phaselet.format('cmdexec', '0', '0', \
				data.dmesg['resume_complete']['color']))
		hf.write('</div>\n')
	hf.write('</div>\n')

	# write the ftrace data (callgraph)
	data = testruns[-1]
	if(sysvals.usecallgraph and not sysvals.embedded):
		hf.write('<section id="callgraphs" class="callgraph">\n')
		# write out the ftrace data converted to html
		html_func_top = '<article id="{0}" class="atop" style="background-color:{1}">\n<input type="checkbox" class="pf" id="f{2}" checked/><label for="f{2}">{3} {4}</label>\n'
		html_func_start = '<article>\n<input type="checkbox" class="pf" id="f{0}" checked/><label for="f{0}">{1} {2}</label>\n'
		html_func_end = '</article>\n'
		html_func_leaf = '<article>{0} {1}</article>\n'
		num = 0
		for p in data.phases:
			list = data.dmesg[p]['list']
			for devname in data.sortedDevices(p):
				if('ftrace' not in list[devname]):
					continue
				devid = list[devname]['id']
				cg = list[devname]['ftrace']
				clen = (cg.end - cg.start) * 1000
				if clen < sysvals.mincglen:
					continue
				fmt = '<r>(%.3f ms @ '+sysvals.timeformat+' to '+sysvals.timeformat+')</r>'
				flen = fmt % (clen, cg.start, cg.end)
				name = devname
				if(devname in sysvals.devprops):
					name = sysvals.devprops[devname].altName(devname)
				if sysvals.suspendmode == 'command':
					ftitle = name
				else:
					ftitle = name+' '+p
				hf.write(html_func_top.format(devid, data.dmesg[p]['color'], \
					num, ftitle, flen))
				num += 1
				for line in cg.list:
					if(line.length < 0.000000001):
						flen = ''
					else:
						fmt = '<n>(%.3f ms @ '+sysvals.timeformat+')</n>'
						flen = fmt % (line.length*1000, line.time)
					if(line.freturn and line.fcall):
						hf.write(html_func_leaf.format(line.name, flen))
					elif(line.freturn):
						hf.write(html_func_end)
					else:
						hf.write(html_func_start.format(num, line.name, flen))
						num += 1
				hf.write(html_func_end)
		hf.write('\n\n    </section>\n')

	# add the dmesg log as a hidden div
	if sysvals.addlogs and sysvals.dmesgfile:
		hf.write('<div id="dmesglog" style="display:none;">\n')
		lf = open(sysvals.dmesgfile, 'r')
		for line in lf:
			hf.write(line)
		lf.close()
		hf.write('</div>\n')
	# add the ftrace log as a hidden div
	if sysvals.addlogs and sysvals.ftracefile:
		hf.write('<div id="ftracelog" style="display:none;">\n')
		lf = open(sysvals.ftracefile, 'r')
		for line in lf:
			hf.write(line)
		lf.close()
		hf.write('</div>\n')

	if(not sysvals.embedded):
		# write the footer and close
		addScriptCode(hf, testruns)
		hf.write('</body>\n</html>\n')
	else:
		# embedded out will be loaded in a page, skip the js
		t0 = (testruns[0].start - testruns[-1].tSuspended) * 1000
		tMax = (testruns[-1].end - testruns[-1].tSuspended) * 1000
		# add js code in a div entry for later evaluation
		detail = 'var bounds = [%f,%f];\n' % (t0, tMax)
		detail += 'var devtable = [\n'
		for data in testruns:
			topo = data.deviceTopology()
			detail += '\t"%s",\n' % (topo)
		detail += '];\n'
		hf.write('<div id=customcode style=display:none>\n'+detail+'</div>\n')
	hf.close()
	return True

# Function: addScriptCode
# Description:
#	 Adds the javascript code to the output html
# Arguments:
#	 hf: the open html file pointer
#	 testruns: array of Data objects from parseKernelLog or parseTraceLog
def addScriptCode(hf, testruns):
	t0 = testruns[0].start * 1000
	tMax = testruns[-1].end * 1000
	# create an array in javascript memory with the device details
	detail = '	var devtable = [];\n'
	for data in testruns:
		topo = data.deviceTopology()
		detail += '	devtable[%d] = "%s";\n' % (data.testnumber, topo)
	detail += '	var bounds = [%f,%f];\n' % (t0, tMax)
	# add the code which will manipulate the data in the browser
	script_code = \
	'<script type="text/javascript">\n'+detail+\
	'	var resolution = -1;\n'\
	'	function redrawTimescale(t0, tMax, tS) {\n'\
	'		var rline = \'<div class="t" style="left:0;border-left:1px solid black;border-right:0;"><cR><-R</cR></div>\';\n'\
	'		var tTotal = tMax - t0;\n'\
	'		var list = document.getElementsByClassName("tblock");\n'\
	'		for (var i = 0; i < list.length; i++) {\n'\
	'			var timescale = list[i].getElementsByClassName("timescale")[0];\n'\
	'			var m0 = t0 + (tTotal*parseFloat(list[i].style.left)/100);\n'\
	'			var mTotal = tTotal*parseFloat(list[i].style.width)/100;\n'\
	'			var mMax = m0 + mTotal;\n'\
	'			var html = "";\n'\
	'			var divTotal = Math.floor(mTotal/tS) + 1;\n'\
	'			if(divTotal > 1000) continue;\n'\
	'			var divEdge = (mTotal - tS*(divTotal-1))*100/mTotal;\n'\
	'			var pos = 0.0, val = 0.0;\n'\
	'			for (var j = 0; j < divTotal; j++) {\n'\
	'				var htmlline = "";\n'\
	'				if(list[i].id[5] == "r") {\n'\
	'					pos = 100 - (((j)*tS*100)/mTotal);\n'\
	'					val = (j)*tS;\n'\
	'					htmlline = \'<div class="t" style="right:\'+pos+\'%">\'+val+\'ms</div>\';\n'\
	'					if(j == 0)\n'\
	'						htmlline = rline;\n'\
	'				} else {\n'\
	'					pos = 100 - (((j)*tS*100)/mTotal) - divEdge;\n'\
	'					val = (j-divTotal+1)*tS;\n'\
	'					if(j == divTotal - 1)\n'\
	'						htmlline = \'<div class="t" style="right:\'+pos+\'%"><cS>S-></cS></div>\';\n'\
	'					else\n'\
	'						htmlline = \'<div class="t" style="right:\'+pos+\'%">\'+val+\'ms</div>\';\n'\
	'				}\n'\
	'				html += htmlline;\n'\
	'			}\n'\
	'			timescale.innerHTML = html;\n'\
	'		}\n'\
	'	}\n'\
	'	function zoomTimeline() {\n'\
	'		var dmesg = document.getElementById("dmesg");\n'\
	'		var zoombox = document.getElementById("dmesgzoombox");\n'\
	'		var val = parseFloat(dmesg.style.width);\n'\
	'		var newval = 100;\n'\
	'		var sh = window.outerWidth / 2;\n'\
	'		if(this.id == "zoomin") {\n'\
	'			newval = val * 1.2;\n'\
	'			if(newval > 910034) newval = 910034;\n'\
	'			dmesg.style.width = newval+"%";\n'\
	'			zoombox.scrollLeft = ((zoombox.scrollLeft + sh) * newval / val) - sh;\n'\
	'		} else if (this.id == "zoomout") {\n'\
	'			newval = val / 1.2;\n'\
	'			if(newval < 100) newval = 100;\n'\
	'			dmesg.style.width = newval+"%";\n'\
	'			zoombox.scrollLeft = ((zoombox.scrollLeft + sh) * newval / val) - sh;\n'\
	'		} else {\n'\
	'			zoombox.scrollLeft = 0;\n'\
	'			dmesg.style.width = "100%";\n'\
	'		}\n'\
	'		var tS = [10000, 5000, 2000, 1000, 500, 200, 100, 50, 20, 10, 5, 2, 1];\n'\
	'		var t0 = bounds[0];\n'\
	'		var tMax = bounds[1];\n'\
	'		var tTotal = tMax - t0;\n'\
	'		var wTotal = tTotal * 100.0 / newval;\n'\
	'		var idx = 7*window.innerWidth/1100;\n'\
	'		for(var i = 0; (i < tS.length)&&((wTotal / tS[i]) < idx); i++);\n'\
	'		if(i >= tS.length) i = tS.length - 1;\n'\
	'		if(tS[i] == resolution) return;\n'\
	'		resolution = tS[i];\n'\
	'		redrawTimescale(t0, tMax, tS[i]);\n'\
	'	}\n'\
	'	function deviceHover() {\n'\
	'		var name = this.title.slice(0, this.title.indexOf(" ("));\n'\
	'		var dmesg = document.getElementById("dmesg");\n'\
	'		var dev = dmesg.getElementsByClassName("thread");\n'\
	'		var cpu = -1;\n'\
	'		if(name.match("CPU_ON\[[0-9]*\]"))\n'\
	'			cpu = parseInt(name.slice(7));\n'\
	'		else if(name.match("CPU_OFF\[[0-9]*\]"))\n'\
	'			cpu = parseInt(name.slice(8));\n'\
	'		for (var i = 0; i < dev.length; i++) {\n'\
	'			dname = dev[i].title.slice(0, dev[i].title.indexOf(" ("));\n'\
	'			var cname = dev[i].className.slice(dev[i].className.indexOf("thread"));\n'\
	'			if((cpu >= 0 && dname.match("CPU_O[NF]*\\\[*"+cpu+"\\\]")) ||\n'\
	'				(name == dname))\n'\
	'			{\n'\
	'				dev[i].className = "hover "+cname;\n'\
	'			} else {\n'\
	'				dev[i].className = cname;\n'\
	'			}\n'\
	'		}\n'\
	'	}\n'\
	'	function deviceUnhover() {\n'\
	'		var dmesg = document.getElementById("dmesg");\n'\
	'		var dev = dmesg.getElementsByClassName("thread");\n'\
	'		for (var i = 0; i < dev.length; i++) {\n'\
	'			dev[i].className = dev[i].className.slice(dev[i].className.indexOf("thread"));\n'\
	'		}\n'\
	'	}\n'\
	'	function deviceTitle(title, total, cpu) {\n'\
	'		var prefix = "Total";\n'\
	'		if(total.length > 3) {\n'\
	'			prefix = "Average";\n'\
	'			total[1] = (total[1]+total[3])/2;\n'\
	'			total[2] = (total[2]+total[4])/2;\n'\
	'		}\n'\
	'		var devtitle = document.getElementById("devicedetailtitle");\n'\
	'		var name = title.slice(0, title.indexOf(" ("));\n'\
	'		if(cpu >= 0) name = "CPU"+cpu;\n'\
	'		var driver = "";\n'\
	'		var tS = "<t2>(</t2>";\n'\
	'		var tR = "<t2>)</t2>";\n'\
	'		if(total[1] > 0)\n'\
	'			tS = "<t2>("+prefix+" Suspend:</t2><t0> "+total[1].toFixed(3)+" ms</t0> ";\n'\
	'		if(total[2] > 0)\n'\
	'			tR = " <t2>"+prefix+" Resume:</t2><t0> "+total[2].toFixed(3)+" ms<t2>)</t2></t0>";\n'\
	'		var s = title.indexOf("{");\n'\
	'		var e = title.indexOf("}");\n'\
	'		if((s >= 0) && (e >= 0))\n'\
	'			driver = title.slice(s+1, e) + " <t1>@</t1> ";\n'\
	'		if(total[1] > 0 && total[2] > 0)\n'\
	'			devtitle.innerHTML = "<t0>"+driver+name+"</t0> "+tS+tR;\n'\
	'		else\n'\
	'			devtitle.innerHTML = "<t0>"+title+"</t0>";\n'\
	'		return name;\n'\
	'	}\n'\
	'	function deviceDetail() {\n'\
	'		var devinfo = document.getElementById("devicedetail");\n'\
	'		devinfo.style.display = "block";\n'\
	'		var name = this.title.slice(0, this.title.indexOf(" ("));\n'\
	'		var cpu = -1;\n'\
	'		if(name.match("CPU_ON\[[0-9]*\]"))\n'\
	'			cpu = parseInt(name.slice(7));\n'\
	'		else if(name.match("CPU_OFF\[[0-9]*\]"))\n'\
	'			cpu = parseInt(name.slice(8));\n'\
	'		var dmesg = document.getElementById("dmesg");\n'\
	'		var dev = dmesg.getElementsByClassName("thread");\n'\
	'		var idlist = [];\n'\
	'		var pdata = [[]];\n'\
	'		if(document.getElementById("devicedetail1"))\n'\
	'			pdata = [[], []];\n'\
	'		var pd = pdata[0];\n'\
	'		var total = [0.0, 0.0, 0.0];\n'\
	'		for (var i = 0; i < dev.length; i++) {\n'\
	'			dname = dev[i].title.slice(0, dev[i].title.indexOf(" ("));\n'\
	'			if((cpu >= 0 && dname.match("CPU_O[NF]*\\\[*"+cpu+"\\\]")) ||\n'\
	'				(name == dname))\n'\
	'			{\n'\
	'				idlist[idlist.length] = dev[i].id;\n'\
	'				var tidx = 1;\n'\
	'				if(dev[i].id[0] == "a") {\n'\
	'					pd = pdata[0];\n'\
	'				} else {\n'\
	'					if(pdata.length == 1) pdata[1] = [];\n'\
	'					if(total.length == 3) total[3]=total[4]=0.0;\n'\
	'					pd = pdata[1];\n'\
	'					tidx = 3;\n'\
	'				}\n'\
	'				var info = dev[i].title.split(" ");\n'\
	'				var pname = info[info.length-1];\n'\
	'				pd[pname] = parseFloat(info[info.length-3].slice(1));\n'\
	'				total[0] += pd[pname];\n'\
	'				if(pname.indexOf("suspend") >= 0)\n'\
	'					total[tidx] += pd[pname];\n'\
	'				else\n'\
	'					total[tidx+1] += pd[pname];\n'\
	'			}\n'\
	'		}\n'\
	'		var devname = deviceTitle(this.title, total, cpu);\n'\
	'		var left = 0.0;\n'\
	'		for (var t = 0; t < pdata.length; t++) {\n'\
	'			pd = pdata[t];\n'\
	'			devinfo = document.getElementById("devicedetail"+t);\n'\
	'			var phases = devinfo.getElementsByClassName("phaselet");\n'\
	'			for (var i = 0; i < phases.length; i++) {\n'\
	'				if(phases[i].id in pd) {\n'\
	'					var w = 100.0*pd[phases[i].id]/total[0];\n'\
	'					var fs = 32;\n'\
	'					if(w < 8) fs = 4*w | 0;\n'\
	'					var fs2 = fs*3/4;\n'\
	'					phases[i].style.width = w+"%";\n'\
	'					phases[i].style.left = left+"%";\n'\
	'					phases[i].title = phases[i].id+" "+pd[phases[i].id]+" ms";\n'\
	'					left += w;\n'\
	'					var time = "<t4 style=\\"font-size:"+fs+"px\\">"+pd[phases[i].id]+" ms<br></t4>";\n'\
	'					var pname = "<t3 style=\\"font-size:"+fs2+"px\\">"+phases[i].id.replace("_", " ")+"</t3>";\n'\
	'					phases[i].innerHTML = time+pname;\n'\
	'				} else {\n'\
	'					phases[i].style.width = "0%";\n'\
	'					phases[i].style.left = left+"%";\n'\
	'				}\n'\
	'			}\n'\
	'		}\n'\
	'		var cglist = document.getElementById("callgraphs");\n'\
	'		if(!cglist) return;\n'\
	'		var cg = cglist.getElementsByClassName("atop");\n'\
	'		if(cg.length < 10) return;\n'\
	'		for (var i = 0; i < cg.length; i++) {\n'\
	'			if(idlist.indexOf(cg[i].id) >= 0) {\n'\
	'				cg[i].style.display = "block";\n'\
	'			} else {\n'\
	'				cg[i].style.display = "none";\n'\
	'			}\n'\
	'		}\n'\
	'	}\n'\
	'	function devListWindow(e) {\n'\
	'		var sx = e.clientX;\n'\
	'		if(sx > window.innerWidth - 440)\n'\
	'			sx = window.innerWidth - 440;\n'\
	'		var cfg="top="+e.screenY+", left="+sx+", width=440, height=720, scrollbars=yes";\n'\
	'		var win = window.open("", "_blank", cfg);\n'\
	'		if(window.chrome) win.moveBy(sx, 0);\n'\
	'		var html = "<title>"+e.target.innerHTML+"</title>"+\n'\
	'			"<style type=\\"text/css\\">"+\n'\
	'			"   ul {list-style-type:circle;padding-left:10px;margin-left:10px;}"+\n'\
	'			"</style>"\n'\
	'		var dt = devtable[0];\n'\
	'		if(e.target.id != "devlist1")\n'\
	'			dt = devtable[1];\n'\
	'		win.document.write(html+dt);\n'\
	'	}\n'\
	'	function logWindow(e) {\n'\
	'		var name = e.target.id.slice(4);\n'\
	'		var win = window.open();\n'\
	'		var log = document.getElementById(name+"log");\n'\
	'		var title = "<title>"+document.title.split(" ")[0]+" "+name+" log</title>";\n'\
	'		win.document.write(title+"<pre>"+log.innerHTML+"</pre>");\n'\
	'		win.document.close();\n'\
	'	}\n'\
	'	function onClickPhase(e) {\n'\
	'	}\n'\
	'	window.addEventListener("resize", function () {zoomTimeline();});\n'\
	'	window.addEventListener("load", function () {\n'\
	'		var dmesg = document.getElementById("dmesg");\n'\
	'		dmesg.style.width = "100%"\n'\
	'		document.getElementById("zoomin").onclick = zoomTimeline;\n'\
	'		document.getElementById("zoomout").onclick = zoomTimeline;\n'\
	'		document.getElementById("zoomdef").onclick = zoomTimeline;\n'\
	'		var list = document.getElementsByClassName("square");\n'\
	'		for (var i = 0; i < list.length; i++)\n'\
	'			list[i].onclick = onClickPhase;\n'\
	'		var list = document.getElementsByClassName("logbtn");\n'\
	'		for (var i = 0; i < list.length; i++)\n'\
	'			list[i].onclick = logWindow;\n'\
	'		list = document.getElementsByClassName("devlist");\n'\
	'		for (var i = 0; i < list.length; i++)\n'\
	'			list[i].onclick = devListWindow;\n'\
	'		var dev = dmesg.getElementsByClassName("thread");\n'\
	'		for (var i = 0; i < dev.length; i++) {\n'\
	'			dev[i].onclick = deviceDetail;\n'\
	'			dev[i].onmouseover = deviceHover;\n'\
	'			dev[i].onmouseout = deviceUnhover;\n'\
	'		}\n'\
	'		zoomTimeline();\n'\
	'	});\n'\
	'</script>\n'
	hf.write(script_code);

# Function: executeSuspend
# Description:
#	 Execute system suspend through the sysfs interface, then copy the output
#	 dmesg and ftrace files to the test output directory.
def executeSuspend():
	global sysvals

	t0 = time.time()*1000
	tp = sysvals.tpath
	fwdata = []
	# mark the start point in the kernel ring buffer just as we start
	sysvals.initdmesg()
	# start ftrace
	if(sysvals.usecallgraph or sysvals.usetraceevents):
		print('START TRACING')
		sysvals.fsetVal('1', 'tracing_on')
	# execute however many s/r runs requested
	for count in range(1,sysvals.execcount+1):
		# if this is test2 and there's a delay, start here
		if(count > 1 and sysvals.x2delay > 0):
			tN = time.time()*1000
			while (tN - t0) < sysvals.x2delay:
				tN = time.time()*1000
				time.sleep(0.001)
		# initiate suspend
		if(sysvals.usecallgraph or sysvals.usetraceevents):
			sysvals.fsetVal('SUSPEND START', 'trace_marker')
		if sysvals.suspendmode == 'command':
			print('COMMAND START')
			if(sysvals.rtcwake):
				print('will issue an rtcwake in %d seconds' % sysvals.rtcwaketime)
				sysvals.rtcWakeAlarmOn()
			os.system(sysvals.testcommand)
		else:
			if(sysvals.rtcwake):
				print('SUSPEND START')
				print('will autoresume in %d seconds' % sysvals.rtcwaketime)
				sysvals.rtcWakeAlarmOn()
			else:
				print('SUSPEND START (press a key to resume)')
			pf = open(sysvals.powerfile, 'w')
			pf.write(sysvals.suspendmode)
			# execution will pause here
			try:
				pf.close()
			except:
				pass
		t0 = time.time()*1000
		if(sysvals.rtcwake):
			sysvals.rtcWakeAlarmOff()
		# return from suspend
		print('RESUME COMPLETE')
		if(sysvals.usecallgraph or sysvals.usetraceevents):
			sysvals.fsetVal('RESUME COMPLETE', 'trace_marker')
		if(sysvals.suspendmode == 'mem'):
			fwdata.append(getFPDT(False))
	# look for post resume events after the last test run
	t = sysvals.postresumetime
	if(t > 0):
		print('Waiting %d seconds for POST-RESUME trace events...' % t)
		time.sleep(t)
	# stop ftrace
	if(sysvals.usecallgraph or sysvals.usetraceevents):
		sysvals.fsetVal('0', 'tracing_on')
		print('CAPTURING TRACE')
		writeDatafileHeader(sysvals.ftracefile, fwdata)
		os.system('cat '+tp+'trace >> '+sysvals.ftracefile)
		sysvals.fsetVal('', 'trace')
		devProps()
	# grab a copy of the dmesg output
	print('CAPTURING DMESG')
	writeDatafileHeader(sysvals.dmesgfile, fwdata)
	sysvals.getdmesg()

def writeDatafileHeader(filename, fwdata):
	global sysvals

	prt = sysvals.postresumetime
	fp = open(filename, 'a')
	fp.write(sysvals.teststamp+'\n')
	if(sysvals.suspendmode == 'mem'):
		for fw in fwdata:
			if(fw):
				fp.write('# fwsuspend %u fwresume %u\n' % (fw[0], fw[1]))
	if(prt > 0):
		fp.write('# post resume time %u\n' % prt)
	fp.close()

# Function: setUSBDevicesAuto
# Description:
#	 Set the autosuspend control parameter of all USB devices to auto
#	 This can be dangerous, so use at your own risk, most devices are set
#	 to always-on since the kernel cant determine if the device can
#	 properly autosuspend
def setUSBDevicesAuto():
	global sysvals

	rootCheck(True)
	for dirname, dirnames, filenames in os.walk('/sys/devices'):
		if(re.match('.*/usb[0-9]*.*', dirname) and
			'idVendor' in filenames and 'idProduct' in filenames):
			os.system('echo auto > %s/power/control' % dirname)
			name = dirname.split('/')[-1]
			desc = os.popen('cat %s/product 2>/dev/null' % \
				dirname).read().replace('\n', '')
			ctrl = os.popen('cat %s/power/control 2>/dev/null' % \
				dirname).read().replace('\n', '')
			print('control is %s for %6s: %s' % (ctrl, name, desc))

# Function: yesno
# Description:
#	 Print out an equivalent Y or N for a set of known parameter values
# Output:
#	 'Y', 'N', or ' ' if the value is unknown
def yesno(val):
	yesvals = ['auto', 'enabled', 'active', '1']
	novals = ['on', 'disabled', 'suspended', 'forbidden', 'unsupported']
	if val in yesvals:
		return 'Y'
	elif val in novals:
		return 'N'
	return ' '

# Function: ms2nice
# Description:
#	 Print out a very concise time string in minutes and seconds
# Output:
#	 The time string, e.g. "1901m16s"
def ms2nice(val):
	ms = 0
	try:
		ms = int(val)
	except:
		return 0.0
	m = ms / 60000
	s = (ms / 1000) - (m * 60)
	return '%3dm%2ds' % (m, s)

# Function: detectUSB
# Description:
#	 Detect all the USB hosts and devices currently connected and add
#	 a list of USB device names to sysvals for better timeline readability
def detectUSB():
	global sysvals

	field = {'idVendor':'', 'idProduct':'', 'product':'', 'speed':''}
	power = {'async':'', 'autosuspend':'', 'autosuspend_delay_ms':'',
			 'control':'', 'persist':'', 'runtime_enabled':'',
			 'runtime_status':'', 'runtime_usage':'',
			'runtime_active_time':'',
			'runtime_suspended_time':'',
			'active_duration':'',
			'connected_duration':''}

	print('LEGEND')
	print('---------------------------------------------------------------------------------------------')
	print('  A = async/sync PM queue Y/N                       D = autosuspend delay (seconds)')
	print('  S = autosuspend Y/N                         rACTIVE = runtime active (min/sec)')
	print('  P = persist across suspend Y/N              rSUSPEN = runtime suspend (min/sec)')
	print('  E = runtime suspend enabled/forbidden Y/N    ACTIVE = active duration (min/sec)')
	print('  R = runtime status active/suspended Y/N     CONNECT = connected duration (min/sec)')
	print('  U = runtime usage count')
	print('---------------------------------------------------------------------------------------------')
	print('  NAME       ID      DESCRIPTION         SPEED A S P E R U D rACTIVE rSUSPEN  ACTIVE CONNECT')
	print('---------------------------------------------------------------------------------------------')

	for dirname, dirnames, filenames in os.walk('/sys/devices'):
		if(re.match('.*/usb[0-9]*.*', dirname) and
			'idVendor' in filenames and 'idProduct' in filenames):
			for i in field:
				field[i] = os.popen('cat %s/%s 2>/dev/null' % \
					(dirname, i)).read().replace('\n', '')
			name = dirname.split('/')[-1]
			for i in power:
				power[i] = os.popen('cat %s/power/%s 2>/dev/null' % \
					(dirname, i)).read().replace('\n', '')
			if(re.match('usb[0-9]*', name)):
				first = '%-8s' % name
			else:
				first = '%8s' % name
			print('%s [%s:%s] %-20s %-4s %1s %1s %1s %1s %1s %1s %1s %s %s %s %s' % \
				(first, field['idVendor'], field['idProduct'], \
				field['product'][0:20], field['speed'], \
				yesno(power['async']), \
				yesno(power['control']), \
				yesno(power['persist']), \
				yesno(power['runtime_enabled']), \
				yesno(power['runtime_status']), \
				power['runtime_usage'], \
				power['autosuspend'], \
				ms2nice(power['runtime_active_time']), \
				ms2nice(power['runtime_suspended_time']), \
				ms2nice(power['active_duration']), \
				ms2nice(power['connected_duration'])))

# Function: devProps
# Description:
#	 Retrieve a list of properties for all devices in the trace log
def devProps(data=0):
	global sysvals
	props = dict()

	if data:
		idx = data.index(': ') + 2
		if idx >= len(data):
			return
		devlist = data[idx:].split(';')
		for dev in devlist:
			f = dev.split(',')
			if len(f) < 3:
				continue
			dev = f[0]
			props[dev] = DevProps()
			props[dev].altname = f[1]
			if int(f[2]):
				props[dev].async = True
			else:
				props[dev].async = False
			sysvals.devprops = props
		if sysvals.suspendmode == 'command' and 'testcommandstring' in props:
			sysvals.testcommand = props['testcommandstring'].altname
		return

	if(os.path.exists(sysvals.ftracefile) == False):
		doError('%s does not exist' % sysvals.ftracefile, False)

	# first get the list of devices we need properties for
	msghead = 'Additional data added by AnalyzeSuspend'
	alreadystamped = False
	tp = TestProps()
	tf = open(sysvals.ftracefile, 'r')
	for line in tf:
		if msghead in line:
			alreadystamped = True
			continue
		# determine the trace data type (required for further parsing)
		m = re.match(sysvals.tracertypefmt, line)
		if(m):
			tp.setTracerType(m.group('t'))
			continue
		# parse only valid lines, if this is not one move on
		m = re.match(tp.ftrace_line_fmt, line)
		if(not m or 'device_pm_callback_start' not in line):
			continue
		m = re.match('.*: (?P<drv>.*) (?P<d>.*), parent: *(?P<p>.*), .*', m.group('msg'));
		if(not m):
			continue
		drv, dev, par = m.group('drv'), m.group('d'), m.group('p')
		if dev not in props:
			props[dev] = DevProps()
	tf.close()

	if not alreadystamped and sysvals.suspendmode == 'command':
		out = '#\n# '+msghead+'\n# Device Properties: '
		out += 'testcommandstring,%s,0;' % (sysvals.testcommand)
		with open(sysvals.ftracefile, 'a') as fp:
			fp.write(out+'\n')
		sysvals.devprops = props
		return

	# now get the syspath for each of our target devices
	for dirname, dirnames, filenames in os.walk('/sys/devices'):
		if(re.match('.*/power', dirname) and 'async' in filenames):
			dev = dirname.split('/')[-2]
			if dev in props and (not props[dev].syspath or len(dirname) < len(props[dev].syspath)):
				props[dev].syspath = dirname[:-6]

	# now fill in the properties for our target devices
	for dev in props:
		dirname = props[dev].syspath
		if not dirname or not os.path.exists(dirname):
			continue
		with open(dirname+'/power/async') as fp:
			text = fp.read()
			props[dev].async = False
			if 'enabled' in text:
				props[dev].async = True
		fields = os.listdir(dirname)
		if 'product' in fields:
			with open(dirname+'/product') as fp:
				props[dev].altname = fp.read()
		elif 'name' in fields:
			with open(dirname+'/name') as fp:
				props[dev].altname = fp.read()
		elif 'model' in fields:
			with open(dirname+'/model') as fp:
				props[dev].altname = fp.read()
		elif 'description' in fields:
			with open(dirname+'/description') as fp:
				props[dev].altname = fp.read()
		elif 'id' in fields:
			with open(dirname+'/id') as fp:
				props[dev].altname = fp.read()
		elif 'idVendor' in fields and 'idProduct' in fields:
			idv, idp = '', ''
			with open(dirname+'/idVendor') as fp:
				idv = fp.read().strip()
			with open(dirname+'/idProduct') as fp:
				idp = fp.read().strip()
			props[dev].altname = '%s:%s' % (idv, idp)

		if props[dev].altname:
			out = props[dev].altname.strip().replace('\n', ' ')
			out = out.replace(',', ' ')
			out = out.replace(';', ' ')
			props[dev].altname = out

	# and now write the data to the ftrace file
	if not alreadystamped:
		out = '#\n# '+msghead+'\n# Device Properties: '
		for dev in sorted(props):
			out += props[dev].out(dev)
		with open(sysvals.ftracefile, 'a') as fp:
			fp.write(out+'\n')

	sysvals.devprops = props

# Function: getModes
# Description:
#	 Determine the supported power modes on this system
# Output:
#	 A string list of the available modes
def getModes():
	global sysvals
	modes = ''
	if(os.path.exists(sysvals.powerfile)):
		fp = open(sysvals.powerfile, 'r')
		modes = string.split(fp.read())
		fp.close()
	return modes

# Function: getFPDT
# Description:
#	 Read the acpi bios tables and pull out FPDT, the firmware data
# Arguments:
#	 output: True to output the info to stdout, False otherwise
def getFPDT(output):
	global sysvals

	rectype = {}
	rectype[0] = 'Firmware Basic Boot Performance Record'
	rectype[1] = 'S3 Performance Table Record'
	prectype = {}
	prectype[0] = 'Basic S3 Resume Performance Record'
	prectype[1] = 'Basic S3 Suspend Performance Record'

	rootCheck(True)
	if(not os.path.exists(sysvals.fpdtpath)):
		if(output):
			doError('file does not exist: %s' % sysvals.fpdtpath, False)
		return False
	if(not os.access(sysvals.fpdtpath, os.R_OK)):
		if(output):
			doError('file is not readable: %s' % sysvals.fpdtpath, False)
		return False
	if(not os.path.exists(sysvals.mempath)):
		if(output):
			doError('file does not exist: %s' % sysvals.mempath, False)
		return False
	if(not os.access(sysvals.mempath, os.R_OK)):
		if(output):
			doError('file is not readable: %s' % sysvals.mempath, False)
		return False

	fp = open(sysvals.fpdtpath, 'rb')
	buf = fp.read()
	fp.close()

	if(len(buf) < 36):
		if(output):
			doError('Invalid FPDT table data, should '+\
				'be at least 36 bytes', False)
		return False

	table = struct.unpack('4sIBB6s8sI4sI', buf[0:36])
	if(output):
		print('')
		print('Firmware Performance Data Table (%s)' % table[0])
		print('                  Signature : %s' % table[0])
		print('               Table Length : %u' % table[1])
		print('                   Revision : %u' % table[2])
		print('                   Checksum : 0x%x' % table[3])
		print('                     OEM ID : %s' % table[4])
		print('               OEM Table ID : %s' % table[5])
		print('               OEM Revision : %u' % table[6])
		print('                 Creator ID : %s' % table[7])
		print('           Creator Revision : 0x%x' % table[8])
		print('')

	if(table[0] != 'FPDT'):
		if(output):
			doError('Invalid FPDT table')
		return False
	if(len(buf) <= 36):
		return False
	i = 0
	fwData = [0, 0]
	records = buf[36:]
	fp = open(sysvals.mempath, 'rb')
	while(i < len(records)):
		header = struct.unpack('HBB', records[i:i+4])
		if(header[0] not in rectype):
			i += header[1]
			continue
		if(header[1] != 16):
			i += header[1]
			continue
		addr = struct.unpack('Q', records[i+8:i+16])[0]
		try:
			fp.seek(addr)
			first = fp.read(8)
		except:
			if(output):
				print('Bad address 0x%x in %s' % (addr, sysvals.mempath))
			return [0, 0]
		rechead = struct.unpack('4sI', first)
		recdata = fp.read(rechead[1]-8)
		if(rechead[0] == 'FBPT'):
			record = struct.unpack('HBBIQQQQQ', recdata)
			if(output):
				print('%s (%s)' % (rectype[header[0]], rechead[0]))
				print('                  Reset END : %u ns' % record[4])
				print('  OS Loader LoadImage Start : %u ns' % record[5])
				print(' OS Loader StartImage Start : %u ns' % record[6])
				print('     ExitBootServices Entry : %u ns' % record[7])
				print('      ExitBootServices Exit : %u ns' % record[8])
		elif(rechead[0] == 'S3PT'):
			if(output):
				print('%s (%s)' % (rectype[header[0]], rechead[0]))
			j = 0
			while(j < len(recdata)):
				prechead = struct.unpack('HBB', recdata[j:j+4])
				if(prechead[0] not in prectype):
					continue
				if(prechead[0] == 0):
					record = struct.unpack('IIQQ', recdata[j:j+prechead[1]])
					fwData[1] = record[2]
					if(output):
						print('    %s' % prectype[prechead[0]])
						print('               Resume Count : %u' % \
							record[1])
						print('                 FullResume : %u ns' % \
							record[2])
						print('              AverageResume : %u ns' % \
							record[3])
				elif(prechead[0] == 1):
					record = struct.unpack('QQ', recdata[j+4:j+prechead[1]])
					fwData[0] = record[1] - record[0]
					if(output):
						print('    %s' % prectype[prechead[0]])
						print('               SuspendStart : %u ns' % \
							record[0])
						print('                 SuspendEnd : %u ns' % \
							record[1])
						print('                SuspendTime : %u ns' % \
							fwData[0])
				j += prechead[1]
		if(output):
			print('')
		i += header[1]
	fp.close()
	return fwData

# Function: statusCheck
# Description:
#	 Verify that the requested command and options will work, and
#	 print the results to the terminal
# Output:
#	 True if the test will work, False if not
def statusCheck(probecheck=False):
	global sysvals
	status = True

	print('Checking this system (%s)...' % platform.node())

	# check we have root access
	res = sysvals.colorText('NO (No features of this tool will work!)')
	if(rootCheck(False)):
		res = 'YES'
	print('    have root access: %s' % res)
	if(res != 'YES'):
		print('    Try running this script with sudo')
		return False

	# check sysfs is mounted
	res = sysvals.colorText('NO (No features of this tool will work!)')
	if(os.path.exists(sysvals.powerfile)):
		res = 'YES'
	print('    is sysfs mounted: %s' % res)
	if(res != 'YES'):
		return False

	# check target mode is a valid mode
	if sysvals.suspendmode != 'command':
		res = sysvals.colorText('NO')
		modes = getModes()
		if(sysvals.suspendmode in modes):
			res = 'YES'
		else:
			status = False
		print('    is "%s" a valid power mode: %s' % (sysvals.suspendmode, res))
		if(res == 'NO'):
			print('      valid power modes are: %s' % modes)
			print('      please choose one with -m')

	# check if ftrace is available
	res = sysvals.colorText('NO')
	ftgood = sysvals.verifyFtrace()
	if(ftgood):
		res = 'YES'
	elif(sysvals.usecallgraph):
		status = False
	print('    is ftrace supported: %s' % res)

	# check if kprobes are available
	res = sysvals.colorText('NO')
	sysvals.usekprobes = sysvals.verifyKprobes()
	if(sysvals.usekprobes):
		res = 'YES'
	else:
		sysvals.usedevsrc = False
	print('    are kprobes supported: %s' % res)

	# what data source are we using
	res = 'DMESG'
	if(ftgood):
		sysvals.usetraceeventsonly = True
		sysvals.usetraceevents = False
		for e in sysvals.traceevents:
			check = False
			if(os.path.exists(sysvals.epath+e)):
				check = True
			if(not check):
				sysvals.usetraceeventsonly = False
			if(e == 'suspend_resume' and check):
				sysvals.usetraceevents = True
		if(sysvals.usetraceevents and sysvals.usetraceeventsonly):
			res = 'FTRACE (all trace events found)'
		elif(sysvals.usetraceevents):
			res = 'DMESG and FTRACE (suspend_resume trace event found)'
	print('    timeline data source: %s' % res)

	# check if rtcwake
	res = sysvals.colorText('NO')
	if(sysvals.rtcpath != ''):
		res = 'YES'
	elif(sysvals.rtcwake):
		status = False
	print('    is rtcwake supported: %s' % res)

	if not probecheck:
		return status

	if (sysvals.usecallgraph and len(sysvals.debugfuncs) > 0) or len(sysvals.kprobes) > 0:
		sysvals.initFtrace(True)

	# verify callgraph debugfuncs
	if sysvals.usecallgraph and len(sysvals.debugfuncs) > 0:
		print('    verifying these ftrace callgraph functions work:')
		sysvals.setFtraceFilterFunctions(sysvals.debugfuncs)
		fp = open(sysvals.tpath+'set_graph_function', 'r')
		flist = fp.read().split('\n')
		fp.close()
		for func in sysvals.debugfuncs:
			res = sysvals.colorText('NO')
			if func in flist:
				res = 'YES'
			else:
				for i in flist:
					if ' [' in i and func == i.split(' ')[0]:
						res = 'YES'
						break
			print('         %s: %s' % (func, res))

	# verify kprobes
	if len(sysvals.kprobes) > 0:
		print('    verifying these kprobes work:')
		for name in sorted(sysvals.kprobes):
			if name in sysvals.tracefuncs:
				continue
			res = sysvals.colorText('NO')
			if sysvals.testKprobe(sysvals.kprobes[name]):
				res = 'YES'
			print('         %s: %s' % (name, res))

	return status

# Function: doError
# Description:
#	 generic error function for catastrphic failures
# Arguments:
#	 msg: the error message to print
#	 help: True if printHelp should be called after, False otherwise
def doError(msg, help):
	if(help == True):
		printHelp()
	print('ERROR: %s\n') % msg
	sys.exit()

# Function: doWarning
# Description:
#	 generic warning function for non-catastrophic anomalies
# Arguments:
#	 msg: the warning message to print
#	 file: If not empty, a filename to request be sent to the owner for debug
def doWarning(msg, file=''):
	print('/* %s */') % msg
	if(file):
		print('/* For a fix, please send this'+\
			' %s file to <todd.e.brandt@intel.com> */' % file)

# Function: rootCheck
# Description:
#	 quick check to see if we have root access
def rootCheck(fatal):
	global sysvals
	if(os.access(sysvals.powerfile, os.W_OK)):
		return True
	if fatal:
		doError('This command must be run as root', False)
	return False

# Function: getArgInt
# Description:
#	 pull out an integer argument from the command line with checks
def getArgInt(name, args, min, max, main=True):
	if main:
		try:
			arg = args.next()
		except:
			doError(name+': no argument supplied', True)
	else:
		arg = args
	try:
		val = int(arg)
	except:
		doError(name+': non-integer value given', True)
	if(val < min or val > max):
		doError(name+': value should be between %d and %d' % (min, max), True)
	return val

# Function: getArgFloat
# Description:
#	 pull out a float argument from the command line with checks
def getArgFloat(name, args, min, max, main=True):
	if main:
		try:
			arg = args.next()
		except:
			doError(name+': no argument supplied', True)
	else:
		arg = args
	try:
		val = float(arg)
	except:
		doError(name+': non-numerical value given', True)
	if(val < min or val > max):
		doError(name+': value should be between %f and %f' % (min, max), True)
	return val

# Function: rerunTest
# Description:
#	 generate an output from an existing set of ftrace/dmesg logs
def rerunTest():
	global sysvals

	if(sysvals.ftracefile != ''):
		doesTraceLogHaveTraceEvents()
	if(sysvals.dmesgfile == '' and not sysvals.usetraceeventsonly):
		doError('recreating this html output '+\
			'requires a dmesg file', False)
	sysvals.setOutputFile()
	vprint('Output file: %s' % sysvals.htmlfile)
	print('PROCESSING DATA')
	if(sysvals.usetraceeventsonly):
		testruns = parseTraceLog()
	else:
		testruns = loadKernelLog()
		for data in testruns:
			parseKernelLog(data)
		if(sysvals.ftracefile != ''):
			appendIncompleteTraceLog(testruns)
	createHTML(testruns)

# Function: runTest
# Description:
#	 execute a suspend/resume, gather the logs, and generate the output
def runTest(subdir, testpath=''):
	global sysvals

	# prepare for the test
	sysvals.initFtrace()
	sysvals.initTestOutput(subdir, testpath)

	vprint('Output files:\n    %s' % sysvals.dmesgfile)
	if(sysvals.usecallgraph or
		sysvals.usetraceevents or
		sysvals.usetraceeventsonly):
		vprint('    %s' % sysvals.ftracefile)
	vprint('    %s' % sysvals.htmlfile)

	# execute the test
	executeSuspend()
	sysvals.cleanupFtrace()

	# analyze the data and create the html output
	print('PROCESSING DATA')
	if(sysvals.usetraceeventsonly):
		# data for kernels 3.15 or newer is entirely in ftrace
		testruns = parseTraceLog()
	else:
		# data for kernels older than 3.15 is primarily in dmesg
		testruns = loadKernelLog()
		for data in testruns:
			parseKernelLog(data)
		if(sysvals.usecallgraph or sysvals.usetraceevents):
			appendIncompleteTraceLog(testruns)
	createHTML(testruns)

# Function: runSummary
# Description:
#	 create a summary of tests in a sub-directory
def runSummary(subdir, output):
	global sysvals

	# get a list of ftrace output files
	files = []
	for dirname, dirnames, filenames in os.walk(subdir):
		for filename in filenames:
			if(re.match('.*_ftrace.txt', filename)):
				files.append("%s/%s" % (dirname, filename))

	# process the files in order and get an array of data objects
	testruns = []
	for file in sorted(files):
		if output:
			print("Test found in %s" % os.path.dirname(file))
		sysvals.ftracefile = file
		sysvals.dmesgfile = file.replace('_ftrace.txt', '_dmesg.txt')
		doesTraceLogHaveTraceEvents()
		sysvals.usecallgraph = False
		if not sysvals.usetraceeventsonly:
			if(not os.path.exists(sysvals.dmesgfile)):
				print("Skipping %s: not a valid test input" % file)
				continue
			else:
				if output:
					f = os.path.basename(sysvals.ftracefile)
					d = os.path.basename(sysvals.dmesgfile)
					print("\tInput files: %s and %s" % (f, d))
				testdata = loadKernelLog()
				data = testdata[0]
				parseKernelLog(data)
				testdata = [data]
				appendIncompleteTraceLog(testdata)
		else:
			if output:
				print("\tInput file: %s" % os.path.basename(sysvals.ftracefile))
			testdata = parseTraceLog()
			data = testdata[0]
		data.normalizeTime(data.tSuspended)
		link = file.replace(subdir+'/', '').replace('_ftrace.txt', '.html')
		data.outfile = link
		testruns.append(data)

	createHTMLSummarySimple(testruns, subdir+'/summary.html')

# Function: checkArgBool
# Description:
#	 check if a boolean string value is true or false
def checkArgBool(value):
	yes = ['1', 'true', 'yes', 'on']
	if value.lower() in yes:
		return True
	return False

# Function: configFromFile
# Description:
#	 Configure the script via the info in a config file
def configFromFile(file):
	global sysvals
	Config = ConfigParser.ConfigParser()

	ignorekprobes = False
	Config.read(file)
	sections = Config.sections()
	if 'Settings' in sections:
		for opt in Config.options('Settings'):
			value = Config.get('Settings', opt).lower()
			if(opt.lower() == 'verbose'):
				sysvals.verbose = checkArgBool(value)
			elif(opt.lower() == 'addlogs'):
				sysvals.addlogs = checkArgBool(value)
			elif(opt.lower() == 'dev'):
				sysvals.usedevsrc = checkArgBool(value)
			elif(opt.lower() == 'ignorekprobes'):
				ignorekprobes = checkArgBool(value)
			elif(opt.lower() == 'x2'):
				if checkArgBool(value):
					sysvals.execcount = 2
			elif(opt.lower() == 'callgraph'):
				sysvals.usecallgraph = checkArgBool(value)
			elif(opt.lower() == 'callgraphfunc'):
				sysvals.debugfuncs = []
				if value:
					value = value.split(',')
				for i in value:
					sysvals.debugfuncs.append(i.strip())
			elif(opt.lower() == 'expandcg'):
				sysvals.cgexp = checkArgBool(value)
			elif(opt.lower() == 'srgap'):
				if checkArgBool(value):
					sysvals.srgap = 5
			elif(opt.lower() == 'mode'):
				sysvals.suspendmode = value
			elif(opt.lower() == 'command'):
				sysvals.testcommand = value
			elif(opt.lower() == 'x2delay'):
				sysvals.x2delay = getArgInt('-x2delay', value, 0, 60000, False)
			elif(opt.lower() == 'postres'):
				sysvals.postresumetime = getArgInt('-postres', value, 0, 3600, False)
			elif(opt.lower() == 'rtcwake'):
				sysvals.rtcwake = True
				sysvals.rtcwaketime = getArgInt('-rtcwake', value, 0, 3600, False)
			elif(opt.lower() == 'timeprec'):
				sysvals.setPrecision(getArgInt('-timeprec', value, 0, 6, False))
			elif(opt.lower() == 'mindev'):
				sysvals.mindevlen = getArgFloat('-mindev', value, 0.0, 10000.0, False)
			elif(opt.lower() == 'mincg'):
				sysvals.mincglen = getArgFloat('-mincg', value, 0.0, 10000.0, False)
			elif(opt.lower() == 'kprobecolor'):
				try:
					val = int(value, 16)
					sysvals.kprobecolor = '#'+value
				except:
					sysvals.kprobecolor = value
			elif(opt.lower() == 'synccolor'):
				try:
					val = int(value, 16)
					sysvals.synccolor = '#'+value
				except:
					sysvals.synccolor = value
			elif(opt.lower() == 'output-dir'):
				args = dict()
				n = datetime.now()
				args['date'] = n.strftime('%y%m%d')
				args['time'] = n.strftime('%H%M%S')
				args['hostname'] = sysvals.hostname
				sysvals.outdir = value.format(**args)

	if sysvals.suspendmode == 'command' and not sysvals.testcommand:
		doError('No command supplied for mode "command"', False)
	if sysvals.usedevsrc and sysvals.usecallgraph:
		doError('dev and callgraph cannot both be true', False)
	if sysvals.usecallgraph and sysvals.execcount > 1:
		doError('-x2 is not compatible with -f', False)

	if ignorekprobes:
		return

	kprobes = dict()
	archkprobe = 'Kprobe_'+platform.machine()
	if archkprobe in sections:
		for name in Config.options(archkprobe):
			kprobes[name] = Config.get(archkprobe, name)
	if 'Kprobe' in sections:
		for name in Config.options('Kprobe'):
			kprobes[name] = Config.get('Kprobe', name)

	for name in kprobes:
		function = name
		format = name
		color = ''
		args = dict()
		data = kprobes[name].split()
		i = 0
		for val in data:
			# bracketted strings are special formatting, read them separately
			if val[0] == '[' and val[-1] == ']':
				for prop in val[1:-1].split(','):
					p = prop.split('=')
					if p[0] == 'color':
						try:
							color = int(p[1], 16)
							color = '#'+p[1]
						except:
							color = p[1]
				continue
			# first real arg should be the format string
			if i == 0:
				format = val
			# all other args are actual function args
			else:
				d = val.split('=')
				args[d[0]] = d[1]
			i += 1
		if not function or not format:
			doError('Invalid kprobe: %s' % name, False)
		for arg in re.findall('{(?P<n>[a-z,A-Z,0-9]*)}', format):
			if arg not in args:
				doError('Kprobe "%s" is missing argument "%s"' % (name, arg), False)
		if name in sysvals.kprobes:
			doError('Duplicate kprobe found "%s"' % (name), False)
		vprint('Adding KPROBE: %s %s %s %s' % (name, function, format, args))
		sysvals.kprobes[name] = {
			'name': name,
			'func': function,
			'format': format,
			'args': args,
			'mask': re.sub('{(?P<n>[a-z,A-Z,0-9]*)}', '.*', format)
		}
		if color:
			sysvals.kprobes[name]['color'] = color

# Function: printHelp
# Description:
#	 print out the help text
def printHelp():
	global sysvals
	modes = getModes()

	print('')
	print('AnalyzeSuspend v%s' % sysvals.version)
	print('Usage: sudo analyze_suspend.py <options>')
	print('')
	print('Description:')
	print('  This tool is designed to assist kernel and OS developers in optimizing')
	print('  their linux stack\'s suspend/resume time. Using a kernel image built')
	print('  with a few extra options enabled, the tool will execute a suspend and')
	print('  capture dmesg and ftrace data until resume is complete. This data is')
	print('  transformed into a device timeline and an optional callgraph to give')
	print('  a detailed view of which devices/subsystems are taking the most')
	print('  time in suspend/resume.')
	print('')
	print('  Generates output files in subdirectory: suspend-mmddyy-HHMMSS')
	print('   HTML output:                    <hostname>_<mode>.html')
	print('   raw dmesg output:               <hostname>_<mode>_dmesg.txt')
	print('   raw ftrace output:              <hostname>_<mode>_ftrace.txt')
	print('')
	print('Options:')
	print('  [general]')
	print('    -h          Print this help text')
	print('    -v          Print the current tool version')
	print('    -config file Pull arguments and config options from a file')
	print('    -verbose    Print extra information during execution and analysis')
	print('    -status     Test to see if the system is enabled to run this tool')
	print('    -modes      List available suspend modes')
	print('    -m mode     Mode to initiate for suspend %s (default: %s)') % (modes, sysvals.suspendmode)
	print('    -o subdir   Override the output subdirectory')
	print('  [advanced]')
	print('    -rtcwake t  Use rtcwake to autoresume after <t> seconds (default: disabled)')
	print('    -addlogs    Add the dmesg and ftrace logs to the html output')
	print('    -multi n d  Execute <n> consecutive tests at <d> seconds intervals. The outputs will')
	print('                be created in a new subdirectory with a summary page.')
	print('    -srgap      Add a visible gap in the timeline between sus/res (default: disabled)')
	print('    -cmd {s}    Instead of suspend/resume, run a command, e.g. "sync -d"')
	print('    -mindev ms  Discard all device blocks shorter than ms milliseconds (e.g. 0.001 for us)')
	print('    -mincg  ms  Discard all callgraphs shorter than ms milliseconds (e.g. 0.001 for us)')
	print('    -timeprec N Number of significant digits in timestamps (0:S, [3:ms], 6:us)')
	print('  [debug]')
	print('    -f          Use ftrace to create device callgraphs (default: disabled)')
	print('    -expandcg   pre-expand the callgraph data in the html output (default: disabled)')
	print('    -flist      Print the list of functions currently being captured in ftrace')
	print('    -flistall   Print all functions capable of being captured in ftrace')
	print('    -fadd file  Add functions to be graphed in the timeline from a list in a text file')
	print('    -filter "d1 d2 ..." Filter out all but this list of device names')
	print('    -dev        Display common low level functions in the timeline')
	print('  [post-resume task analysis]')
	print('    -x2         Run two suspend/resumes back to back (default: disabled)')
	print('    -x2delay t  Minimum millisecond delay <t> between the two test runs (default: 0 ms)')
	print('    -postres t  Time after resume completion to wait for post-resume events (default: 0 S)')
	print('  [utilities]')
	print('    -fpdt       Print out the contents of the ACPI Firmware Performance Data Table')
	print('    -usbtopo    Print out the current USB topology with power info')
	print('    -usbauto    Enable autosuspend for all connected USB devices')
	print('  [re-analyze data from previous runs]')
	print('    -ftrace ftracefile  Create HTML output using ftrace input')
	print('    -dmesg dmesgfile    Create HTML output using dmesg (not needed for kernel >= 3.15)')
	print('    -summary directory  Create a summary of all test in this dir')
	print('')
	return True

# ----------------- MAIN --------------------
# exec start (skipped if script is loaded as library)
if __name__ == '__main__':
	cmd = ''
	cmdarg = ''
	multitest = {'run': False, 'count': 0, 'delay': 0}
	simplecmds = ['-modes', '-fpdt', '-flist', '-flistall', '-usbtopo', '-usbauto', '-status']
	# loop through the command line arguments
	args = iter(sys.argv[1:])
	for arg in args:
		if(arg == '-m'):
			try:
				val = args.next()
			except:
				doError('No mode supplied', True)
			if val == 'command' and not sysvals.testcommand:
				doError('No command supplied for mode "command"', True)
			sysvals.suspendmode = val
		elif(arg in simplecmds):
			cmd = arg[1:]
		elif(arg == '-h'):
			printHelp()
			sys.exit()
		elif(arg == '-v'):
			print("Version %s" % sysvals.version)
			sys.exit()
		elif(arg == '-x2'):
			sysvals.execcount = 2
			if(sysvals.usecallgraph):
				doError('-x2 is not compatible with -f', False)
		elif(arg == '-x2delay'):
			sysvals.x2delay = getArgInt('-x2delay', args, 0, 60000)
		elif(arg == '-postres'):
			sysvals.postresumetime = getArgInt('-postres', args, 0, 3600)
		elif(arg == '-f'):
			sysvals.usecallgraph = True
			if(sysvals.execcount > 1):
				doError('-x2 is not compatible with -f', False)
			if(sysvals.usedevsrc):
				doError('-dev is not compatible with -f', False)
		elif(arg == '-addlogs'):
			sysvals.addlogs = True
		elif(arg == '-verbose'):
			sysvals.verbose = True
		elif(arg == '-dev'):
			sysvals.usedevsrc = True
			if(sysvals.usecallgraph):
				doError('-dev is not compatible with -f', False)
		elif(arg == '-rtcwake'):
			sysvals.rtcwake = True
			sysvals.rtcwaketime = getArgInt('-rtcwake', args, 0, 3600)
		elif(arg == '-timeprec'):
			sysvals.setPrecision(getArgInt('-timeprec', args, 0, 6))
		elif(arg == '-mindev'):
			sysvals.mindevlen = getArgFloat('-mindev', args, 0.0, 10000.0)
		elif(arg == '-mincg'):
			sysvals.mincglen = getArgFloat('-mincg', args, 0.0, 10000.0)
		elif(arg == '-cmd'):
			try:
				val = args.next()
			except:
				doError('No command string supplied', True)
			sysvals.testcommand = val
			sysvals.suspendmode = 'command'
		elif(arg == '-expandcg'):
			sysvals.cgexp = True
		elif(arg == '-srgap'):
			sysvals.srgap = 5
		elif(arg == '-multi'):
			multitest['run'] = True
			multitest['count'] = getArgInt('-multi n (exec count)', args, 2, 1000000)
			multitest['delay'] = getArgInt('-multi d (delay between tests)', args, 0, 3600)
		elif(arg == '-o'):
			try:
				val = args.next()
			except:
				doError('No subdirectory name supplied', True)
			sysvals.outdir = val
		elif(arg == '-config'):
			try:
				val = args.next()
			except:
				doError('No text file supplied', True)
			if(os.path.exists(val) == False):
				doError('%s does not exist' % val, False)
			configFromFile(val)
		elif(arg == '-fadd'):
			try:
				val = args.next()
			except:
				doError('No text file supplied', True)
			if(os.path.exists(val) == False):
				doError('%s does not exist' % val, False)
			sysvals.addFtraceFilterFunctions(val)
		elif(arg == '-dmesg'):
			try:
				val = args.next()
			except:
				doError('No dmesg file supplied', True)
			sysvals.notestrun = True
			sysvals.dmesgfile = val
			if(os.path.exists(sysvals.dmesgfile) == False):
				doError('%s does not exist' % sysvals.dmesgfile, False)
		elif(arg == '-ftrace'):
			try:
				val = args.next()
			except:
				doError('No ftrace file supplied', True)
			sysvals.notestrun = True
			sysvals.ftracefile = val
			if(os.path.exists(sysvals.ftracefile) == False):
				doError('%s does not exist' % sysvals.ftracefile, False)
		elif(arg == '-summary'):
			try:
				val = args.next()
			except:
				doError('No directory supplied', True)
			cmd = 'summary'
			cmdarg = val
			sysvals.notestrun = True
			if(os.path.isdir(val) == False):
				doError('%s is not accesible' % val, False)
		elif(arg == '-filter'):
			try:
				val = args.next()
			except:
				doError('No devnames supplied', True)
			sysvals.setDeviceFilter(val)
		else:
			doError('Invalid argument: '+arg, True)

	# callgraph size cannot exceed device size
	if sysvals.mincglen < sysvals.mindevlen:
		sysvals.mincglen = sysvals.mindevlen

	# just run a utility command and exit
	if(cmd != ''):
		if(cmd == 'status'):
			statusCheck(True)
		elif(cmd == 'fpdt'):
			getFPDT(True)
		elif(cmd == 'usbtopo'):
			detectUSB()
		elif(cmd == 'modes'):
			modes = getModes()
			print modes
		elif(cmd == 'flist'):
			sysvals.getFtraceFilterFunctions(True)
		elif(cmd == 'flistall'):
			sysvals.getFtraceFilterFunctions(False)
		elif(cmd == 'usbauto'):
			setUSBDevicesAuto()
		elif(cmd == 'summary'):
			print("Generating a summary of folder \"%s\"" % cmdarg)
			runSummary(cmdarg, True)
		sys.exit()

	# if instructed, re-analyze existing data files
	if(sysvals.notestrun):
		rerunTest()
		sys.exit()

	# verify that we can run a test
	if(not statusCheck()):
		print('Check FAILED, aborting the test run!')
		sys.exit()

	if multitest['run']:
		# run multiple tests in a separate subdirectory
		s = 'x%d' % multitest['count']
		if not sysvals.outdir:
			sysvals.outdir = datetime.now().strftime('suspend-'+s+'-%m%d%y-%H%M%S')
		if not os.path.isdir(sysvals.outdir):
			os.mkdir(sysvals.outdir)
		for i in range(multitest['count']):
			if(i != 0):
				print('Waiting %d seconds...' % (multitest['delay']))
				time.sleep(multitest['delay'])
			print('TEST (%d/%d) START' % (i+1, multitest['count']))
			runTest(sysvals.outdir)
			print('TEST (%d/%d) COMPLETE' % (i+1, multitest['count']))
		runSummary(sysvals.outdir, False)
	else:
		# run the test in the current directory
		runTest('.', sysvals.outdir)
