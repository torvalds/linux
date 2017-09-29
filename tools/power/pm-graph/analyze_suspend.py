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
# Authors:
#	 Todd Brandt <todd.e.brandt@linux.intel.com>
#
# Links:
#	 Home Page
#	   https://01.org/suspendresume
#	 Source repo
#	   https://github.com/01org/pm-graph
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
from threading import Thread
from subprocess import call, Popen, PIPE

# ----------------- CLASSES --------------------

# Class: SystemValues
# Description:
#	 A global, single-instance container used to
#	 store system values and test parameters
class SystemValues:
	title = 'SleepGraph'
	version = '4.7'
	ansi = False
	verbose = False
	testlog = True
	dmesglog = False
	ftracelog = False
	mindevlen = 0.0
	mincglen = 0.0
	cgphase = ''
	cgtest = -1
	max_graph_depth = 0
	callloopmaxgap = 0.0001
	callloopmaxlen = 0.005
	cpucount = 0
	memtotal = 204800
	srgap = 0
	cgexp = False
	testdir = ''
	tpath = '/sys/kernel/debug/tracing/'
	fpdtpath = '/sys/firmware/acpi/tables/FPDT'
	epath = '/sys/kernel/debug/tracing/events/power/'
	traceevents = [
		'suspend_resume',
		'device_pm_callback_end',
		'device_pm_callback_start'
	]
	logmsg = ''
	testcommand = ''
	mempath = '/dev/mem'
	powerfile = '/sys/power/state'
	mempowerfile = '/sys/power/mem_sleep'
	suspendmode = 'mem'
	memmode = ''
	hostname = 'localhost'
	prefix = 'test'
	teststamp = ''
	sysstamp = ''
	dmesgstart = 0.0
	dmesgfile = ''
	ftracefile = ''
	htmlfile = 'output.html'
	embedded = False
	rtcwake = True
	rtcwaketime = 15
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
	useprocmon = False
	notestrun = False
	mixedphaseheight = True
	devprops = dict()
	predelay = 0
	postdelay = 0
	procexecfmt = 'ps - (?P<ps>.*)$'
	devpropfmt = '# Device Properties: .*'
	tracertypefmt = '# tracer: (?P<t>.*)'
	firmwarefmt = '# fwsuspend (?P<s>[0-9]*) fwresume (?P<r>[0-9]*)$'
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
			'format': 'CPU_OFF[{cpu}]'
		},
		'CPU_ON': {
			'func':'_cpu_up',
			'args_x86_64': {'cpu':'%di:s32'},
			'format': 'CPU_ON[{cpu}]'
		},
	}
	dev_tracefuncs = {
		# general wait/delay/sleep
		'msleep': { 'args_x86_64': {'time':'%di:s32'}, 'ub': 1 },
		'schedule_timeout_uninterruptible': { 'args_x86_64': {'timeout':'%di:s32'}, 'ub': 1 },
		'schedule_timeout': { 'args_x86_64': {'timeout':'%di:s32'}, 'ub': 1 },
		'udelay': { 'func':'__const_udelay', 'args_x86_64': {'loops':'%di:s32'}, 'ub': 1 },
		'usleep_range': { 'args_x86_64': {'min':'%di:s32', 'max':'%si:s32'}, 'ub': 1 },
		'mutex_lock_slowpath': { 'func':'__mutex_lock_slowpath', 'ub': 1 },
		'acpi_os_stall': {'ub': 1},
		# ACPI
		'acpi_resume_power_resources': dict(),
		'acpi_ps_parse_aml': dict(),
		# filesystem
		'ext4_sync_fs': dict(),
		# 80211
		'iwlagn_mac_start': dict(),
		'iwlagn_alloc_bcast_station': dict(),
		'iwl_trans_pcie_start_hw': dict(),
		'iwl_trans_pcie_start_fw': dict(),
		'iwl_run_init_ucode': dict(),
		'iwl_load_ucode_wait_alive': dict(),
		'iwl_alive_start': dict(),
		'iwlagn_mac_stop': dict(),
		'iwlagn_mac_suspend': dict(),
		'iwlagn_mac_resume': dict(),
		'iwlagn_mac_add_interface': dict(),
		'iwlagn_mac_remove_interface': dict(),
		'iwlagn_mac_change_interface': dict(),
		'iwlagn_mac_config': dict(),
		'iwlagn_configure_filter': dict(),
		'iwlagn_mac_hw_scan': dict(),
		'iwlagn_bss_info_changed': dict(),
		'iwlagn_mac_channel_switch': dict(),
		'iwlagn_mac_flush': dict(),
		# ATA
		'ata_eh_recover': { 'args_x86_64': {'port':'+36(%di):s32'} },
		# i915
		'i915_gem_resume': dict(),
		'i915_restore_state': dict(),
		'intel_opregion_setup': dict(),
		'g4x_pre_enable_dp': dict(),
		'vlv_pre_enable_dp': dict(),
		'chv_pre_enable_dp': dict(),
		'g4x_enable_dp': dict(),
		'vlv_enable_dp': dict(),
		'intel_hpd_init': dict(),
		'intel_opregion_register': dict(),
		'intel_dp_detect': dict(),
		'intel_hdmi_detect': dict(),
		'intel_opregion_init': dict(),
		'intel_fbdev_set_suspend': dict(),
	}
	kprobes = dict()
	timeformat = '%.3f'
	def __init__(self):
		# if this is a phoronix test run, set some default options
		if('LOG_FILE' in os.environ and 'TEST_RESULTS_IDENTIFIER' in os.environ):
			self.embedded = True
			self.dmesglog = self.ftracelog = True
			self.htmlfile = os.environ['LOG_FILE']
		self.archargs = 'args_'+platform.machine()
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
		self.testdir = datetime.now().strftime('suspend-%y%m%d-%H%M%S')
	def rootCheck(self, fatal=True):
		if(os.access(self.powerfile, os.W_OK)):
			return True
		if fatal:
			doError('This command requires sysfs mount and root access')
		return False
	def rootUser(self, fatal=False):
		if 'USER' in os.environ and os.environ['USER'] == 'root':
			return True
		if fatal:
			doError('This command must be run as root')
		return False
	def setPrecision(self, num):
		if num < 0 or num > 6:
			return
		self.timeformat = '%.{0}f'.format(num)
	def setOutputFolder(self, value):
		args = dict()
		n = datetime.now()
		args['date'] = n.strftime('%y%m%d')
		args['time'] = n.strftime('%H%M%S')
		args['hostname'] = self.hostname
		return value.format(**args)
	def setOutputFile(self):
		if self.dmesgfile != '':
			m = re.match('(?P<name>.*)_dmesg\.txt$', self.dmesgfile)
			if(m):
				self.htmlfile = m.group('name')+'.html'
		if self.ftracefile != '':
			m = re.match('(?P<name>.*)_ftrace\.txt$', self.ftracefile)
			if(m):
				self.htmlfile = m.group('name')+'.html'
	def systemInfo(self, info):
		p = c = m = b = ''
		if 'baseboard-manufacturer' in info:
			m = info['baseboard-manufacturer']
		elif 'system-manufacturer' in info:
			m = info['system-manufacturer']
		if 'baseboard-product-name' in info:
			p = info['baseboard-product-name']
		elif 'system-product-name' in info:
			p = info['system-product-name']
		if 'processor-version' in info:
			c = info['processor-version']
		if 'bios-version' in info:
			b = info['bios-version']
		self.sysstamp = '# sysinfo | man:%s | plat:%s | cpu:%s | bios:%s | numcpu:%d | memsz:%d' % \
			(m, p, c, b, self.cpucount, self.memtotal)
	def printSystemInfo(self):
		self.rootCheck(True)
		out = dmidecode(self.mempath, True)
		fmt = '%-24s: %s'
		for name in sorted(out):
			print fmt % (name, out[name])
		print fmt % ('cpucount', ('%d' % self.cpucount))
		print fmt % ('memtotal', ('%d kB' % self.memtotal))
	def cpuInfo(self):
		self.cpucount = 0
		fp = open('/proc/cpuinfo', 'r')
		for line in fp:
			if re.match('^processor[ \t]*:[ \t]*[0-9]*', line):
				self.cpucount += 1
		fp.close()
		fp = open('/proc/meminfo', 'r')
		for line in fp:
			m = re.match('^MemTotal:[ \t]*(?P<sz>[0-9]*) *kB', line)
			if m:
				self.memtotal = int(m.group('sz'))
				break
		fp.close()
	def initTestOutput(self, name):
		self.prefix = self.hostname
		v = open('/proc/version', 'r').read().strip()
		kver = string.split(v)[2]
		fmt = name+'-%m%d%y-%H%M%S'
		testtime = datetime.now().strftime(fmt)
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
	def setDeviceFilter(self, value):
		self.devicefilter = []
		if value:
			value = value.split(',')
		for i in value:
			self.devicefilter.append(i.strip())
	def rtcWakeAlarmOn(self):
		call('echo 0 > '+self.rtcpath+'/wakealarm', shell=True)
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
		call('echo %d > %s/wakealarm' % (alarm, self.rtcpath), shell=True)
	def rtcWakeAlarmOff(self):
		call('echo 0 > %s/wakealarm' % self.rtcpath, shell=True)
	def initdmesg(self):
		# get the latest time stamp from the dmesg log
		fp = Popen('dmesg', stdout=PIPE).stdout
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
		fp = Popen('dmesg', stdout=PIPE).stdout
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
		self.rootCheck(True)
		if not current:
			call('cat '+self.tpath+'available_filter_functions', shell=True)
			return
		fp = open(self.tpath+'available_filter_functions')
		master = fp.read().split('\n')
		fp.close()
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
	def basicKprobe(self, name):
		self.kprobes[name] = {'name': name,'func': name,'args': dict(),'format': name}
	def defaultKprobe(self, name, kdata):
		k = kdata
		for field in ['name', 'format', 'func']:
			if field not in k:
				k[field] = name
		if self.archargs in k:
			k['args'] = k[self.archargs]
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
	def kprobeText(self, kname, kprobe):
		name = fmt = func = kname
		args = dict()
		if 'name' in kprobe:
			name = kprobe['name']
		if 'format' in kprobe:
			fmt = kprobe['format']
		if 'func' in kprobe:
			func = kprobe['func']
		if self.archargs in kprobe:
			args = kprobe[self.archargs]
		if 'args' in kprobe:
			args = kprobe['args']
		if re.findall('{(?P<n>[a-z,A-Z,0-9]*)}', func):
			doError('Kprobe "%s" has format info in the function name "%s"' % (name, func))
		for arg in re.findall('{(?P<n>[a-z,A-Z,0-9]*)}', fmt):
			if arg not in args:
				doError('Kprobe "%s" is missing argument "%s"' % (name, arg))
		val = 'p:%s_cal %s' % (name, func)
		for i in sorted(args):
			val += ' %s=%s' % (i, args[i])
		val += '\nr:%s_ret %s $retval\n' % (name, func)
		return val
	def addKprobes(self, output=False):
		if len(self.kprobes) < 1:
			return
		if output:
			print('    kprobe functions in this kernel:')
		# first test each kprobe
		rejects = []
		# sort kprobes: trace, ub-dev, custom, dev
		kpl = [[], [], [], []]
		for name in sorted(self.kprobes):
			res = self.colorText('YES', 32)
			if not self.testKprobe(name, self.kprobes[name]):
				res = self.colorText('NO')
				rejects.append(name)
			else:
				if name in self.tracefuncs:
					kpl[0].append(name)
				elif name in self.dev_tracefuncs:
					if 'ub' in self.dev_tracefuncs[name]:
						kpl[1].append(name)
					else:
						kpl[3].append(name)
				else:
					kpl[2].append(name)
			if output:
				print('         %s: %s' % (name, res))
		kplist = kpl[0] + kpl[1] + kpl[2] + kpl[3]
		# remove all failed ones from the list
		for name in rejects:
			self.kprobes.pop(name)
		# set the kprobes all at once
		self.fsetVal('', 'kprobe_events')
		kprobeevents = ''
		for kp in kplist:
			kprobeevents += self.kprobeText(kp, self.kprobes[kp])
		self.fsetVal(kprobeevents, 'kprobe_events')
		# verify that the kprobes were set as ordered
		check = self.fgetVal('kprobe_events')
		linesout = len(kprobeevents.split('\n')) - 1
		linesack = len(check.split('\n')) - 1
		if output:
			res = '%d/%d' % (linesack, linesout)
			if linesack < linesout:
				res = self.colorText(res, 31)
			else:
				res = self.colorText(res, 32)
			print('    working kprobe functions enabled: %s' % res)
		self.fsetVal('1', 'events/kprobes/enable')
	def testKprobe(self, kname, kprobe):
		self.fsetVal('0', 'events/kprobes/enable')
		kprobeevents = self.kprobeText(kname, kprobe)
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
			fp = open(file, mode, 0)
			fp.write(val)
			fp.flush()
			fp.close()
		except:
			return False
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
		if len(self.tracefuncs) < 1 and self.suspendmode == 'command':
			return True
		for i in self.tracefuncs:
			if 'func' in self.tracefuncs[i]:
				f = self.tracefuncs[i]['func']
			else:
				f = i
			if name == f:
				return True
		return False
	def initFtrace(self, testing=False):
		print('INITIALIZING FTRACE...')
		# turn trace off
		self.fsetVal('0', 'tracing_on')
		self.cleanupFtrace()
		# set the trace clock to global
		self.fsetVal('global', 'trace_clock')
		self.fsetVal('nop', 'current_tracer')
		# set trace buffer to a huge value
		if self.usecallgraph or self.usedevsrc:
			tgtsize = min(self.memtotal / 2, 2*1024*1024)
			maxbuf = '%d' % (tgtsize / max(1, self.cpucount))
			if self.cpucount < 1 or not self.fsetVal(maxbuf, 'buffer_size_kb'):
				self.fsetVal('131072', 'buffer_size_kb')
		else:
			self.fsetVal('16384', 'buffer_size_kb')
		# go no further if this is just a status check
		if testing:
			return
		# initialize the callgraph trace
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
			self.fsetVal('%d' % self.max_graph_depth, 'max_graph_depth')
			cf = ['dpm_run_callback']
			if(self.usetraceeventsonly):
				cf += ['dpm_prepare', 'dpm_complete']
			for fn in self.tracefuncs:
				if 'func' in self.tracefuncs[fn]:
					cf.append(self.tracefuncs[fn]['func'])
				else:
					cf.append(fn)
			self.setFtraceFilterFunctions(cf)
		# initialize the kprobe trace
		elif self.usekprobes:
			for name in self.tracefuncs:
				self.defaultKprobe(name, self.tracefuncs[name])
			if self.usedevsrc:
				for name in self.dev_tracefuncs:
					self.defaultKprobe(name, self.dev_tracefuncs[name])
			print('INITIALIZING KPROBES...')
			self.addKprobes(self.verbose)
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
	def colorText(self, str, color=31):
		if not self.ansi:
			return str
		return '\x1B[%d;40m%s\x1B[m' % (color, str)
	def writeDatafileHeader(self, filename, fwdata=[]):
		fp = open(filename, 'w')
		fp.write(self.teststamp+'\n')
		fp.write(self.sysstamp+'\n')
		if(self.suspendmode == 'mem' or self.suspendmode == 'command'):
			for fw in fwdata:
				if(fw):
					fp.write('# fwsuspend %u fwresume %u\n' % (fw[0], fw[1]))
		fp.close()

sysvals = SystemValues()
suspendmodename = {
	'freeze': 'Freeze (S0)',
	'standby': 'Standby (S1)',
	'mem': 'Suspend (S3)',
	'disk': 'Hibernate (S4)'
}

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
			return ' async_device'
		return ' sync_device'

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
#						optionally includes dev/ps data
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
	tKernSus = 0.0   # kernel level suspend start
	tKernRes = 0.0   # kernel level resume end
	tLow = 0.0       # time spent in low-level suspend (standby/freeze)
	fwValid = False  # is firmware data available
	fwSuspend = 0    # time spent in firmware suspend
	fwResume = 0     # time spent in firmware resume
	dmesgtext = []   # dmesg text file in memory
	pstl = 0         # process timeline
	testnumber = 0
	idstr = ''
	html_device_id = 0
	stamp = 0
	outfile = ''
	devpids = []
	kerror = False
	def __init__(self, num):
		idchar = 'abcdefghij'
		self.pstl = dict()
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
		self.errorinfo = {'suspend':[],'resume':[]}
	def extractErrorInfo(self, dmesg):
		error = ''
		tm = 0.0
		for i in range(len(dmesg)):
			if 'Call Trace:' in dmesg[i]:
				m = re.match('[ \t]*(\[ *)(?P<ktime>[0-9\.]*)(\]) .*', dmesg[i])
				if not m:
					continue
				tm = float(m.group('ktime'))
				if tm < self.start or tm > self.end:
					continue
				for j in range(i-10, i+1):
					error += dmesg[j]
				continue
			if error:
				m = re.match('[ \t]*\[ *[0-9\.]*\]  \[\<[0-9a-fA-F]*\>\] .*', dmesg[i])
				if m:
					error += dmesg[i]
				else:
					if tm < self.tSuspended:
						dir = 'suspend'
					else:
						dir = 'resume'
					error = error.replace('<', '&lt').replace('>', '&gt')
					vprint('kernel error found in %s at %f' % (dir, tm))
					self.errorinfo[dir].append((tm, error))
					self.kerror = True
					error = ''
	def setStart(self, time):
		self.start = time
	def setEnd(self, time):
		self.end = time
	def isTraceEventOutsideDeviceCalls(self, pid, time):
		for phase in self.phases:
			list = self.dmesg[phase]['list']
			for dev in list:
				d = list[dev]
				if(d['pid'] == pid and time >= d['start'] and
					time < d['end']):
					return False
		return True
	def sourcePhase(self, start):
		for phase in self.phases:
			pend = self.dmesg[phase]['end']
			if start <= pend:
				return phase
		return 'resume_complete'
	def sourceDevice(self, phaselist, start, end, pid, type):
		tgtdev = ''
		for phase in phaselist:
			list = self.dmesg[phase]['list']
			for devname in list:
				dev = list[devname]
				# pid must match
				if dev['pid'] != pid:
					continue
				devS = dev['start']
				devE = dev['end']
				if type == 'device':
					# device target event is entirely inside the source boundary
					if(start < devS or start >= devE or end <= devS or end > devE):
						continue
				elif type == 'thread':
					# thread target event will expand the source boundary
					if start < devS:
						dev['start'] = start
					if end > devE:
						dev['end'] = end
				tgtdev = dev
				break
		return tgtdev
	def addDeviceFunctionCall(self, displayname, kprobename, proc, pid, start, end, cdata, rdata):
		# try to place the call in a device
		tgtdev = self.sourceDevice(self.phases, start, end, pid, 'device')
		# calls with device pids that occur outside device bounds are dropped
		# TODO: include these somehow
		if not tgtdev and pid in self.devpids:
			return False
		# try to place the call in a thread
		if not tgtdev:
			tgtdev = self.sourceDevice(self.phases, start, end, pid, 'thread')
		# create new thread blocks, expand as new calls are found
		if not tgtdev:
			if proc == '<...>':
				threadname = 'kthread-%d' % (pid)
			else:
				threadname = '%s-%d' % (proc, pid)
			tgtphase = self.sourcePhase(start)
			self.newAction(tgtphase, threadname, pid, '', start, end, '', ' kth', '')
			return self.addDeviceFunctionCall(displayname, kprobename, proc, pid, start, end, cdata, rdata)
		# this should not happen
		if not tgtdev:
			vprint('[%f - %f] %s-%d %s %s %s' % \
				(start, end, proc, pid, kprobename, cdata, rdata))
			return False
		# place the call data inside the src element of the tgtdev
		if('src' not in tgtdev):
			tgtdev['src'] = []
		dtf = sysvals.dev_tracefuncs
		ubiquitous = False
		if kprobename in dtf and 'ub' in dtf[kprobename]:
			ubiquitous = True
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
			if ubiquitous and c in dtf and 'ub' in dtf[c]:
				return False
		color = sysvals.kprobeColor(kprobename)
		e = DevFunction(displayname, a, c, r, start, end, ubiquitous, proc, pid, color)
		tgtdev['src'].append(e)
		return True
	def overflowDevices(self):
		# get a list of devices that extend beyond the end of this test run
		devlist = []
		for phase in self.phases:
			list = self.dmesg[phase]['list']
			for devname in list:
				dev = list[devname]
				if dev['end'] > self.end:
					devlist.append(dev)
		return devlist
	def mergeOverlapDevices(self, devlist):
		# merge any devices that overlap devlist
		for dev in devlist:
			devname = dev['name']
			for phase in self.phases:
				list = self.dmesg[phase]['list']
				if devname not in list:
					continue
				tdev = list[devname]
				o = min(dev['end'], tdev['end']) - max(dev['start'], tdev['start'])
				if o <= 0:
					continue
				dev['end'] = tdev['end']
				if 'src' not in dev or 'src' not in tdev:
					continue
				dev['src'] += tdev['src']
				del list[devname]
	def usurpTouchingThread(self, name, dev):
		# the caller test has priority of this thread, give it to him
		for phase in self.phases:
			list = self.dmesg[phase]['list']
			if name in list:
				tdev = list[name]
				if tdev['start'] - dev['end'] < 0.1:
					dev['end'] = tdev['end']
					if 'src' not in dev:
						dev['src'] = []
					if 'src' in tdev:
						dev['src'] += tdev['src']
					del list[name]
				break
	def stitchTouchingThreads(self, testlist):
		# merge any threads between tests that touch
		for phase in self.phases:
			list = self.dmesg[phase]['list']
			for devname in list:
				dev = list[devname]
				if 'htmlclass' not in dev or 'kth' not in dev['htmlclass']:
					continue
				for data in testlist:
					data.usurpTouchingThread(devname, dev)
	def optimizeDevSrc(self):
		# merge any src call loops to reduce timeline size
		for phase in self.phases:
			list = self.dmesg[phase]['list']
			for dev in list:
				if 'src' not in list[dev]:
					continue
				src = list[dev]['src']
				p = 0
				for e in sorted(src, key=lambda event: event.time):
					if not p or not e.repeat(p):
						p = e
						continue
					# e is another iteration of p, move it into p
					p.end = e.end
					p.length = p.end - p.time
					p.count += 1
					src.remove(e)
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
		self.tKernSus = self.trimTimeVal(self.tKernSus, t0, dT, left)
		self.tKernRes = self.trimTimeVal(self.tKernRes, t0, dT, left)
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
	def getTimeValues(self):
		sktime = (self.dmesg['suspend_machine']['end'] - \
			self.tKernSus) * 1000
		rktime = (self.dmesg['resume_complete']['end'] - \
			self.dmesg['resume_machine']['start']) * 1000
		return (sktime, rktime)
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
			if dev['length'] == 0:
				continue
			tmp[dev['start']] = devname
		for t in sorted(tmp):
			slist.append(tmp[t])
		return slist
	def fixupInitcalls(self, phase):
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
		for phase in self.phases:
			list = self.dmesg[phase]['list']
			rmlist = []
			for name in list:
				keep = False
				for filter in devicefilter:
					if filter in name or \
						('drv' in list[name] and filter in list[name]['drv']):
						keep = True
				if not keep:
					rmlist.append(name)
			for name in rmlist:
				del list[name]
	def fixupInitcallsThatDidntReturn(self):
		# if any calls never returned, clip them at system resume end
		for phase in self.phases:
			self.fixupInitcalls(phase)
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
		# which phase is this device callback or action in
		targetphase = 'none'
		htmlclass = ''
		overlap = 0.0
		phases = []
		for phase in self.phases:
			pstart = self.dmesg[phase]['start']
			pend = self.dmesg[phase]['end']
			# see if the action overlaps this phase
			o = max(0, min(end, pend) - max(start, pstart))
			if o > 0:
				phases.append(phase)
			# set the target phase to the one that overlaps most
			if o > overlap:
				if overlap > 0 and phase == 'post_resume':
					continue
				targetphase = phase
				overlap = o
		# if no target phase was found, pin it to the edge
		if targetphase == 'none':
			p0start = self.dmesg[self.phases[0]]['start']
			if start <= p0start:
				targetphase = self.phases[0]
			else:
				targetphase = self.phases[-1]
		if pid == -2:
			htmlclass = ' bg'
		elif pid == -3:
			htmlclass = ' ps'
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
		list[name] = {'name': name, 'start': start, 'end': end, 'pid': pid,
			'par': parent, 'length': length, 'row': 0, 'id': devid, 'drv': drv }
		if htmlclass:
			list[name]['htmlclass'] = htmlclass
		if color:
			list[name]['color'] = color
		return name
	def deviceChildren(self, devname, phase):
		devlist = []
		list = self.dmesg[phase]['list']
		for child in list:
			if(list[child]['par'] == devname):
				devlist.append(child)
		return devlist
	def printDetails(self):
		vprint('Timeline Details:')
		vprint('          test start: %f' % self.start)
		vprint('kernel suspend start: %f' % self.tKernSus)
		for phase in self.phases:
			dc = len(self.dmesg[phase]['list'])
			vprint('    %16s: %f - %f (%d devices)' % (phase, \
				self.dmesg[phase]['start'], self.dmesg[phase]['end'], dc))
		vprint('   kernel resume end: %f' % self.tKernRes)
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
	def addHorizontalDivider(self, devname, devend):
		phase = 'suspend_prepare'
		self.newAction(phase, devname, -2, '', \
			self.start, devend, '', ' sec', '')
		if phase not in self.tdevlist:
			self.tdevlist[phase] = []
		self.tdevlist[phase].append(devname)
		d = DevItem(0, phase, self.dmesg[phase]['list'][devname])
		return d
	def addProcessUsageEvent(self, name, times):
		# get the start and end times for this process
		maxC = 0
		tlast = 0
		start = -1
		end = -1
		for t in sorted(times):
			if tlast == 0:
				tlast = t
				continue
			if name in self.pstl[t]:
				if start == -1 or tlast < start:
					start = tlast
				if end == -1 or t > end:
					end = t
			tlast = t
		if start == -1 or end == -1:
			return 0
		# add a new action for this process and get the object
		out = self.newActionGlobal(name, start, end, -3)
		if not out:
			return 0
		phase, devname = out
		dev = self.dmesg[phase]['list'][devname]
		# get the cpu exec data
		tlast = 0
		clast = 0
		cpuexec = dict()
		for t in sorted(times):
			if tlast == 0 or t <= start or t > end:
				tlast = t
				continue
			list = self.pstl[t]
			c = 0
			if name in list:
				c = list[name]
			if c > maxC:
				maxC = c
			if c != clast:
				key = (tlast, t)
				cpuexec[key] = c
				tlast = t
				clast = c
		dev['cpuexec'] = cpuexec
		return maxC
	def createProcessUsageEvents(self):
		# get an array of process names
		proclist = []
		for t in self.pstl:
			pslist = self.pstl[t]
			for ps in pslist:
				if ps not in proclist:
					proclist.append(ps)
		# get a list of data points for suspend and resume
		tsus = []
		tres = []
		for t in sorted(self.pstl):
			if t < self.tSuspended:
				tsus.append(t)
			else:
				tres.append(t)
		# process the events for suspend and resume
		if len(proclist) > 0:
			vprint('Process Execution:')
		for ps in proclist:
			c = self.addProcessUsageEvent(ps, tsus)
			if c > 0:
				vprint('%25s (sus): %d' % (ps, c))
			c = self.addProcessUsageEvent(ps, tres)
			if c > 0:
				vprint('%25s (res): %d' % (ps, c))

# Class: DevFunction
# Description:
#	 A container for kprobe function data we want in the dev timeline
class DevFunction:
	row = 0
	count = 1
	def __init__(self, name, args, caller, ret, start, end, u, proc, pid, color):
		self.name = name
		self.args = args
		self.caller = caller
		self.ret = ret
		self.time = start
		self.length = end - start
		self.end = end
		self.ubiquitous = u
		self.proc = proc
		self.pid = pid
		self.color = color
	def title(self):
		cnt = ''
		if self.count > 1:
			cnt = '(x%d)' % self.count
		l = '%0.3fms' % (self.length * 1000)
		if self.ubiquitous:
			title = '%s(%s)%s <- %s, %s(%s)' % \
				(self.name, self.args, cnt, self.caller, self.ret, l)
		else:
			title = '%s(%s) %s%s(%s)' % (self.name, self.args, self.ret, cnt, l)
		return title.replace('"', '')
	def text(self):
		if self.count > 1:
			text = '%s(x%d)' % (self.name, self.count)
		else:
			text = self.name
		return text
	def repeat(self, tgt):
		# is the tgt call just a repeat of this call (e.g. are we in a loop)
		dt = self.time - tgt.end
		# only combine calls if -all- attributes are identical
		if tgt.caller == self.caller and \
			tgt.name == self.name and tgt.args == self.args and \
			tgt.proc == self.proc and tgt.pid == self.pid and \
			tgt.ret == self.ret and dt >= 0 and \
			dt <= sysvals.callloopmaxgap and \
			self.length < sysvals.callloopmaxlen:
			return True
		return False

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
	id = ''
	start = -1.0
	end = -1.0
	list = []
	invalid = False
	depth = 0
	pid = 0
	name = ''
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
		virtualfname = 'missing_function_name'
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
		if len(self.list) > 0:
			self.name = self.list[0].name
		stack = dict()
		cnt = 0
		last = 0
		for l in self.list:
			# ftrace bug: reported duration is not reliable
			# check each leaf and clip it at max possible length
			if(last and last.freturn and last.fcall):
				if last.length > l.time - last.time:
					last.length = l.time - last.time
			if(l.fcall and not l.freturn):
				stack[l.depth] = l
				cnt += 1
			elif(l.freturn and not l.fcall):
				if(l.depth not in stack):
					if debug:
						print 'Post Process Error: Depth missing'
						l.debugPrint()
					return False
				# calculate call length from call/return lines
				stack[l.depth].length = l.time - stack[l.depth].time
				stack.pop(l.depth)
				l.length = 0
				cnt -= 1
			last = l
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
		if(self.name in borderphase):
			p = borderphase[self.name]
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
		name = self.name
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
		print('[%f - %f] %s (%d)') % (self.start, self.end, self.name, self.pid)
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

class DevItem:
	def __init__(self, test, phase, dev):
		self.test = test
		self.phase = phase
		self.dev = dev
	def isa(self, cls):
		if 'htmlclass' in self.dev and cls in self.dev['htmlclass']:
			return True
		return False

# Class: Timeline
# Description:
#	 A container for a device timeline which calculates
#	 all the html properties to display it correctly
class Timeline:
	html = ''
	height = 0	# total timeline height
	scaleH = 20	# timescale (top) row height
	rowH = 30	# device row height
	bodyH = 0	# body height
	rows = 0	# total timeline rows
	rowlines = dict()
	rowheight = dict()
	html_tblock = '<div id="block{0}" class="tblock" style="left:{1}%;width:{2}%;"><div class="tback" style="height:{3}px"></div>\n'
	html_device = '<div id="{0}" title="{1}" class="thread{7}" style="left:{2}%;top:{3}px;height:{4}px;width:{5}%;{8}">{6}</div>\n'
	html_phase = '<div class="phase" style="left:{0}%;width:{1}%;top:{2}px;height:{3}px;background:{4}">{5}</div>\n'
	html_phaselet = '<div id="{0}" class="phaselet" style="left:{1}%;width:{2}%;background:{3}"></div>\n'
	html_legend = '<div id="p{3}" class="square" style="left:{0}%;background:{1}">&nbsp;{2}</div>\n'
	def __init__(self, rowheight, scaleheight):
		self.rowH = rowheight
		self.scaleH = scaleheight
		self.html = ''
	def createHeader(self, sv):
		if(not sv.stamp['time']):
			return
		self.html += '<div class="version"><a href="https://01.org/suspendresume">%s v%s</a></div>' \
			% (sv.title, sv.version)
		if sv.logmsg and sv.testlog:
			self.html += '<button id="showtest" class="logbtn btnfmt">log</button>'
		if sv.dmesglog:
			self.html += '<button id="showdmesg" class="logbtn btnfmt">dmesg</button>'
		if sv.ftracelog:
			self.html += '<button id="showftrace" class="logbtn btnfmt">ftrace</button>'
		headline_stamp = '<div class="stamp">{0} {1} {2} {3}</div>\n'
		self.html += headline_stamp.format(sv.stamp['host'], sv.stamp['kernel'],
			sv.stamp['mode'], sv.stamp['time'])
		if 'man' in sv.stamp and 'plat' in sv.stamp and 'cpu' in sv.stamp:
			headline_sysinfo = '<div class="stamp sysinfo">{0} {1} <i>with</i> {2}</div>\n'
			self.html += headline_sysinfo.format(sv.stamp['man'],
				sv.stamp['plat'], sv.stamp['cpu'])

	# Function: getDeviceRows
	# Description:
	#    determine how may rows the device funcs will take
	# Arguments:
	#	 rawlist: the list of devices/actions for a single phase
	# Output:
	#	 The total number of rows needed to display this phase of the timeline
	def getDeviceRows(self, rawlist):
		# clear all rows and set them to undefined
		sortdict = dict()
		for item in rawlist:
			item.row = -1
			sortdict[item] = item.length
		sortlist = sorted(sortdict, key=sortdict.get, reverse=True)
		remaining = len(sortlist)
		rowdata = dict()
		row = 1
		# try to pack each row with as many ranges as possible
		while(remaining > 0):
			if(row not in rowdata):
				rowdata[row] = []
			for i in sortlist:
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
	#	 devlist: the list of devices/actions in a group of contiguous phases
	# Output:
	#	 The total number of rows needed to display this phase of the timeline
	def getPhaseRows(self, devlist, row=0, sortby='length'):
		# clear all rows and set them to undefined
		remaining = len(devlist)
		rowdata = dict()
		sortdict = dict()
		myphases = []
		# initialize all device rows to -1 and calculate devrows
		for item in devlist:
			dev = item.dev
			tp = (item.test, item.phase)
			if tp not in myphases:
				myphases.append(tp)
			dev['row'] = -1
			if sortby == 'start':
				# sort by start 1st, then length 2nd
				sortdict[item] = (-1*float(dev['start']), float(dev['end']) - float(dev['start']))
			else:
				# sort by length 1st, then name 2nd
				sortdict[item] = (float(dev['end']) - float(dev['start']), item.dev['name'])
			if 'src' in dev:
				dev['devrows'] = self.getDeviceRows(dev['src'])
		# sort the devlist by length so that large items graph on top
		sortlist = sorted(sortdict, key=sortdict.get, reverse=True)
		orderedlist = []
		for item in sortlist:
			if item.dev['pid'] == -2:
				orderedlist.append(item)
		for item in sortlist:
			if item not in orderedlist:
				orderedlist.append(item)
		# try to pack each row with as many devices as possible
		while(remaining > 0):
			rowheight = 1
			if(row not in rowdata):
				rowdata[row] = []
			for item in orderedlist:
				dev = item.dev
				if(dev['row'] < 0):
					s = dev['start']
					e = dev['end']
					valid = True
					for ritem in rowdata[row]:
						rs = ritem.dev['start']
						re = ritem.dev['end']
						if(not (((s <= rs) and (e <= rs)) or
							((s >= re) and (e >= re)))):
							valid = False
							break
					if(valid):
						rowdata[row].append(item)
						dev['row'] = row
						remaining -= 1
						if 'devrows' in dev and dev['devrows'] > rowheight:
							rowheight = dev['devrows']
			for t, p in myphases:
				if t not in self.rowlines or t not in self.rowheight:
					self.rowlines[t] = dict()
					self.rowheight[t] = dict()
				if p not in self.rowlines[t] or p not in self.rowheight[t]:
					self.rowlines[t][p] = dict()
					self.rowheight[t][p] = dict()
				rh = self.rowH
				# section headers should use a different row height
				if len(rowdata[row]) == 1 and \
					'htmlclass' in rowdata[row][0].dev and \
					'sec' in rowdata[row][0].dev['htmlclass']:
					rh = 15
				self.rowlines[t][p][row] = rowheight
				self.rowheight[t][p][row] = rowheight * rh
			row += 1
		if(row > self.rows):
			self.rows = int(row)
		return row
	def phaseRowHeight(self, test, phase, row):
		return self.rowheight[test][phase][row]
	def phaseRowTop(self, test, phase, row):
		top = 0
		for i in sorted(self.rowheight[test][phase]):
			if i >= row:
				break
			top += self.rowheight[test][phase][i]
		return top
	def calcTotalRows(self):
		# Calculate the heights and offsets for the header and rows
		maxrows = 0
		standardphases = []
		for t in self.rowlines:
			for p in self.rowlines[t]:
				total = 0
				for i in sorted(self.rowlines[t][p]):
					total += self.rowlines[t][p][i]
				if total > maxrows:
					maxrows = total
				if total == len(self.rowlines[t][p]):
					standardphases.append((t, p))
		self.height = self.scaleH + (maxrows*self.rowH)
		self.bodyH = self.height - self.scaleH
		# if there is 1 line per row, draw them the standard way
		for t, p in standardphases:
			for i in sorted(self.rowheight[t][p]):
				self.rowheight[t][p][i] = self.bodyH/len(self.rowlines[t][p])
	def createZoomBox(self, mode='command', testcount=1):
		# Create bounding box, add buttons
		html_zoombox = '<center><button id="zoomin">ZOOM IN +</button><button id="zoomout">ZOOM OUT -</button><button id="zoomdef">ZOOM 1:1</button></center>\n'
		html_timeline = '<div id="dmesgzoombox" class="zoombox">\n<div id="{0}" class="timeline" style="height:{1}px">\n'
		html_devlist1 = '<button id="devlist1" class="devlist" style="float:left;">Device Detail{0}</button>'
		html_devlist2 = '<button id="devlist2" class="devlist" style="float:right;">Device Detail2</button>\n'
		if mode != 'command':
			if testcount > 1:
				self.html += html_devlist2
				self.html += html_devlist1.format('1')
			else:
				self.html += html_devlist1.format('')
		self.html += html_zoombox
		self.html += html_timeline.format('dmesg', self.height)
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
		rline = '<div class="t" style="left:0;border-left:1px solid black;border-right:0;">{0}</div>\n'
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
			if(mode == 'suspend'):
				pos = '%0.3f' % (100 - ((float(i)*tS*100)/mTotal) - divEdge)
				val = '%0.fms' % (float(i-divTotal+1)*tS*1000)
				if(i == divTotal - 1):
					val = mode
				htmlline = timescale.format(pos, val)
			else:
				pos = '%0.3f' % (100 - ((float(i)*tS*100)/mTotal))
				val = '%0.fms' % (float(i)*tS*1000)
				htmlline = timescale.format(pos, val)
				if(i == 0):
					htmlline = rline.format(mode)
			output += htmlline
		self.html += output+'</div>\n'

# Class: TestProps
# Description:
#	 A list of values describing the properties of these test runs
class TestProps:
	stamp = ''
	sysinfo = ''
	S0i3 = False
	fwdata = []
	stampfmt = '# [a-z]*-(?P<m>[0-9]{2})(?P<d>[0-9]{2})(?P<y>[0-9]{2})-'+\
				'(?P<H>[0-9]{2})(?P<M>[0-9]{2})(?P<S>[0-9]{2})'+\
				' (?P<host>.*) (?P<mode>.*) (?P<kernel>.*)$'
	sysinfofmt = '^# sysinfo .*'
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
		if(tracer == 'function_graph'):
			self.cgformat = True
			self.ftrace_line_fmt = self.ftrace_line_fmt_fg
		elif(tracer == 'nop'):
			self.ftrace_line_fmt = self.ftrace_line_fmt_nop
		else:
			doError('Invalid tracer format: [%s]' % tracer)
	def parseStamp(self, data, sv):
		m = re.match(self.stampfmt, self.stamp)
		data.stamp = {'time': '', 'host': '', 'mode': ''}
		dt = datetime(int(m.group('y'))+2000, int(m.group('m')),
			int(m.group('d')), int(m.group('H')), int(m.group('M')),
			int(m.group('S')))
		data.stamp['time'] = dt.strftime('%B %d %Y, %I:%M:%S %p')
		data.stamp['host'] = m.group('host')
		data.stamp['mode'] = m.group('mode')
		data.stamp['kernel'] = m.group('kernel')
		if re.match(self.sysinfofmt, self.sysinfo):
			for f in self.sysinfo.split('|'):
				if '#' in f:
					continue
				tmp = f.strip().split(':', 1)
				key = tmp[0]
				val = tmp[1]
				data.stamp[key] = val
		sv.hostname = data.stamp['host']
		sv.suspendmode = data.stamp['mode']
		if sv.suspendmode == 'command' and sv.ftracefile != '':
			modes = ['on', 'freeze', 'standby', 'mem']
			out = Popen(['grep', 'suspend_enter', sv.ftracefile],
				stderr=PIPE, stdout=PIPE).stdout.read()
			m = re.match('.* suspend_enter\[(?P<mode>.*)\]', out)
			if m and m.group('mode') in ['1', '2', '3']:
				sv.suspendmode = modes[int(m.group('mode'))]
				data.stamp['mode'] = sv.suspendmode
		if not sv.stamp:
			sv.stamp = data.stamp

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

class ProcessMonitor:
	proclist = dict()
	running = False
	def procstat(self):
		c = ['cat /proc/[1-9]*/stat 2>/dev/null']
		process = Popen(c, shell=True, stdout=PIPE)
		running = dict()
		for line in process.stdout:
			data = line.split()
			pid = data[0]
			name = re.sub('[()]', '', data[1])
			user = int(data[13])
			kern = int(data[14])
			kjiff = ujiff = 0
			if pid not in self.proclist:
				self.proclist[pid] = {'name' : name, 'user' : user, 'kern' : kern}
			else:
				val = self.proclist[pid]
				ujiff = user - val['user']
				kjiff = kern - val['kern']
				val['user'] = user
				val['kern'] = kern
			if ujiff > 0 or kjiff > 0:
				running[pid] = ujiff + kjiff
		process.wait()
		out = ''
		for pid in running:
			jiffies = running[pid]
			val = self.proclist[pid]
			if out:
				out += ','
			out += '%s-%s %d' % (val['name'], pid, jiffies)
		return 'ps - '+out
	def processMonitor(self, tid):
		while self.running:
			out = self.procstat()
			if out:
				sysvals.fsetVal(out, 'trace_marker')
	def start(self):
		self.thread = Thread(target=self.processMonitor, args=(0,))
		self.running = True
		self.thread.start()
	def stop(self):
		self.running = False

# ----------------- FUNCTIONS --------------------

# Function: vprint
# Description:
#	 verbose print (prints only with -verbose option)
# Arguments:
#	 msg: the debug/log message to print
def vprint(msg):
	sysvals.logmsg += msg+'\n'
	if(sysvals.verbose):
		print(msg)

# Function: doesTraceLogHaveTraceEvents
# Description:
#	 Quickly determine if the ftrace log has some or all of the trace events
#	 required for primary parsing. Set the usetraceevents and/or
#	 usetraceeventsonly flags in the global sysvals object
def doesTraceLogHaveTraceEvents():
	# check for kprobes
	sysvals.usekprobes = False
	out = call('grep -q "_cal: (" '+sysvals.ftracefile, shell=True)
	if(out == 0):
		sysvals.usekprobes = True
	# check for callgraph data on trace event blocks
	out = call('grep -q "_cpu_down()" '+sysvals.ftracefile, shell=True)
	if(out == 0):
		sysvals.usekprobes = True
	out = Popen(['head', '-1', sysvals.ftracefile],
		stderr=PIPE, stdout=PIPE).stdout.read().replace('\n', '')
	# figure out what level of trace events are supported
	sysvals.usetraceeventsonly = True
	sysvals.usetraceevents = False
	for e in sysvals.traceevents:
		out = call('grep -q "'+e+': " '+sysvals.ftracefile, shell=True)
		if(out != 0):
			sysvals.usetraceeventsonly = False
		if(e == 'suspend_resume' and out == 0):
			sysvals.usetraceevents = True
	# determine is this log is properly formatted
	for e in ['SUSPEND START', 'RESUME COMPLETE']:
		out = call('grep -q "'+e+'" '+sysvals.ftracefile, shell=True)
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
		# grab the stamp and sysinfo
		if re.match(tp.stampfmt, line):
			tp.stamp = line
			continue
		elif re.match(tp.sysinfofmt, line):
			tp.sysinfo = line
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
			tp.parseStamp(data, sysvals)
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
	vprint('Analyzing the ftrace data...')
	if(os.path.exists(sysvals.ftracefile) == False):
		doError('%s does not exist' % sysvals.ftracefile)

	sysvals.setupAllKprobes()
	tracewatch = []
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
		# stamp and sysinfo lines
		if re.match(tp.stampfmt, line):
			tp.stamp = line
			continue
		elif re.match(tp.sysinfofmt, line):
			tp.sysinfo = line
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
		# device properties line
		if(re.match(sysvals.devpropfmt, line)):
			devProps(line)
			continue
		# ignore all other commented lines
		if line[0] == '#':
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
			tp.parseStamp(data, sysvals)
			data.setStart(t.time)
			data.tKernSus = t.time
			continue
		if(not data):
			continue
		# process cpu exec line
		if t.type == 'tracing_mark_write':
			m = re.match(sysvals.procexecfmt, t.name)
			if(m):
				proclist = dict()
				for ps in m.group('ps').split(','):
					val = ps.split()
					if not val:
						continue
					name = val[0].replace('--', '-')
					proclist[name] = int(val[1])
				data.pstl[t.time] = proclist
				continue
		# find the end of resume
		if(t.endMarker()):
			data.setEnd(t.time)
			if data.tKernRes == 0.0:
				data.tKernRes = t.time
			if data.dmesg['resume_complete']['end'] < 0:
				data.dmesg['resume_complete']['end'] = t.time
			if sysvals.suspendmode == 'mem' and len(tp.fwdata) > data.testnumber:
				data.fwSuspend, data.fwResume = tp.fwdata[data.testnumber]
				if(data.tSuspended != 0 and data.tResumed != 0 and \
					(data.fwSuspend > 0 or data.fwResume > 0)):
					data.fwValid = True
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
				# start of kernel suspend
				if(re.match('suspend_enter\[.*', t.name)):
					if(isbegin):
						data.dmesg[phase]['start'] = t.time
						data.tKernSus = t.time
					continue
				# suspend_prepare start
				elif(re.match('dpm_prepare\[.*', t.name)):
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
					if pid not in data.devpids:
						data.devpids.append(pid)
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
				# end of kernel resume
				if(kprobename == 'pm_notifier_call_chain' or \
					kprobename == 'pm_restore_console'):
					data.dmesg[phase]['end'] = t.time
					data.tKernRes = t.time

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
				if p == 'suspend_prepare':
					test.data.dmesg[p]['start'] = test.data.start
					test.data.dmesg[p]['end'] = test.data.end
				else:
					test.data.dmesg[p]['start'] = test.data.end
					test.data.dmesg[p]['end'] = test.data.end
			test.data.tSuspended = test.data.end
			test.data.tResumed = test.data.end
			test.data.tLow = 0
			test.data.fwValid = False

	# dev source and procmon events can be unreadable with mixed phase height
	if sysvals.usedevsrc or sysvals.useprocmon:
		sysvals.mixedphaseheight = False

	for i in range(len(testruns)):
		test = testruns[i]
		data = test.data
		# find the total time range for this test (begin, end)
		tlb, tle = data.start, data.end
		if i < len(testruns) - 1:
			tle = testruns[i+1].data.start
		# add the process usage data to the timeline
		if sysvals.useprocmon:
			data.createProcessUsageEvents()
		# add the traceevent data to the device hierarchy
		if(sysvals.usetraceevents):
			# add actual trace funcs
			for name in test.ttemp:
				for event in test.ttemp[name]:
					data.newActionGlobal(name, event['begin'], event['end'], event['pid'])
			# add the kprobe based virtual tracefuncs as actual devices
			for key in tp.ktemp:
				name, pid = key
				if name not in sysvals.tracefuncs:
					continue
				for e in tp.ktemp[key]:
					kb, ke = e['begin'], e['end']
					if kb == ke or tlb > kb or tle <= kb:
						continue
					color = sysvals.kprobeColor(name)
					data.newActionGlobal(e['name'], kb, ke, pid, color)
			# add config base kprobes and dev kprobes
			if sysvals.usedevsrc:
				for key in tp.ktemp:
					name, pid = key
					if name in sysvals.tracefuncs or name not in sysvals.dev_tracefuncs:
						continue
					for e in tp.ktemp[key]:
						kb, ke = e['begin'], e['end']
						if kb == ke or tlb > kb or tle <= kb:
							continue
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
					if sysvals.suspendmode == 'command' or not cg.deviceMatch(pid, data):
						sortkey = '%f%f%d' % (cg.start, cg.end, pid)
						sortlist[sortkey] = cg
			# create blocks for orphan cg data
			for sortkey in sorted(sortlist):
				cg = sortlist[sortkey]
				name = cg.name
				if sysvals.isCallgraphFunc(name):
					vprint('Callgraph found for task %d: %.3fms, %s' % (cg.pid, (cg.end - cg.start)*1000, name))
					cg.newActionFromFunction(data)

	if sysvals.suspendmode == 'command':
		for data in testdata:
			data.printDetails()
		return testdata

	# fill in any missing phases
	for data in testdata:
		lp = data.phases[0]
		for p in data.phases:
			if(data.dmesg[p]['start'] < 0 and data.dmesg[p]['end'] < 0):
				vprint('WARNING: phase "%s" is missing!' % p)
			if(data.dmesg[p]['start'] < 0):
				data.dmesg[p]['start'] = data.dmesg[lp]['end']
				if(p == 'resume_machine'):
					data.tSuspended = data.dmesg[lp]['end']
					data.tResumed = data.dmesg[lp]['end']
					data.tLow = 0
			if(data.dmesg[p]['end'] < 0):
				data.dmesg[p]['end'] = data.dmesg[p]['start']
			if(p != lp and not ('machine' in p and 'machine' in lp)):
				data.dmesg[lp]['end'] = data.dmesg[p]['start']
			lp = p

		if(len(sysvals.devicefilter) > 0):
			data.deviceFilter(sysvals.devicefilter)
		data.fixupInitcallsThatDidntReturn()
		if sysvals.usedevsrc:
			data.optimizeDevSrc()
		data.printDetails()

	# x2: merge any overlapping devices between test runs
	if sysvals.usedevsrc and len(testdata) > 1:
		tc = len(testdata)
		for i in range(tc - 1):
			devlist = testdata[i].overflowDevices()
			for j in range(i + 1, tc):
				testdata[j].mergeOverlapDevices(devlist)
		testdata[0].stitchTouchingThreads(testdata[1:])
	return testdata

# Function: loadKernelLog
# Description:
#	 [deprecated for kernel 3.15.0 or newer]
#	 load the dmesg file into memory and fix up any ordering issues
#	 The dmesg filename is taken from sysvals
# Output:
#	 An array of empty Data objects with only their dmesgtext attributes set
def loadKernelLog(justtext=False):
	vprint('Analyzing the dmesg data...')
	if(os.path.exists(sysvals.dmesgfile) == False):
		doError('%s does not exist' % sysvals.dmesgfile)

	if justtext:
		dmesgtext = []
	# there can be multiple test runs in a single file
	tp = TestProps()
	tp.stamp = datetime.now().strftime('# suspend-%m%d%y-%H%M%S localhost mem unknown')
	testruns = []
	data = 0
	lf = open(sysvals.dmesgfile, 'r')
	for line in lf:
		line = line.replace('\r\n', '')
		idx = line.find('[')
		if idx > 1:
			line = line[idx:]
		# grab the stamp and sysinfo
		if re.match(tp.stampfmt, line):
			tp.stamp = line
			continue
		elif re.match(tp.sysinfofmt, line):
			tp.sysinfo = line
			continue
		m = re.match(sysvals.firmwarefmt, line)
		if(m):
			tp.fwdata.append((int(m.group('s')), int(m.group('r'))))
			continue
		m = re.match('[ \t]*(\[ *)(?P<ktime>[0-9\.]*)(\]) (?P<msg>.*)', line)
		if(not m):
			continue
		msg = m.group("msg")
		if justtext:
			dmesgtext.append(line)
			continue
		if(re.match('PM: Syncing filesystems.*', msg)):
			if(data):
				testruns.append(data)
			data = Data(len(testruns))
			tp.parseStamp(data, sysvals)
			if len(tp.fwdata) > data.testnumber:
				data.fwSuspend, data.fwResume = tp.fwdata[data.testnumber]
				if(data.fwSuspend > 0 or data.fwResume > 0):
					data.fwValid = True
		if(not data):
			continue
		m = re.match('.* *(?P<k>[0-9]\.[0-9]{2}\.[0-9]-.*) .*', msg)
		if(m):
			sysvals.stamp['kernel'] = m.group('k')
		m = re.match('PM: Preparing system for (?P<m>.*) sleep', msg)
		if(m):
			sysvals.stamp['mode'] = sysvals.suspendmode = m.group('m')
		data.dmesgtext.append(line)
	lf.close()

	if justtext:
		return dmesgtext
	if data:
		testruns.append(data)
	if len(testruns) < 1:
		doError(' dmesg log has no suspend/resume data: %s' \
			% sysvals.dmesgfile)

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
		# parse each dmesg line into the time and message
		m = re.match('[ \t]*(\[ *)(?P<ktime>[0-9\.]*)(\]) (?P<msg>.*)', line)
		if(m):
			val = m.group('ktime')
			try:
				ktime = float(val)
			except:
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

		# suspend start
		if(re.match(dm['suspend_prepare'], msg)):
			phase = 'suspend_prepare'
			data.dmesg[phase]['start'] = ktime
			data.setStart(ktime)
			data.tKernSus = ktime
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
			data.tKernRes = ktime
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

	data.printDetails()
	if(len(sysvals.devicefilter) > 0):
		data.deviceFilter(sysvals.devicefilter)
	data.fixupInitcallsThatDidntReturn()
	return True

def callgraphHTML(sv, hf, num, cg, title, color, devid):
	html_func_top = '<article id="{0}" class="atop" style="background:{1}">\n<input type="checkbox" class="pf" id="f{2}" checked/><label for="f{2}">{3} {4}</label>\n'
	html_func_start = '<article>\n<input type="checkbox" class="pf" id="f{0}" checked/><label for="f{0}">{1} {2}</label>\n'
	html_func_end = '</article>\n'
	html_func_leaf = '<article>{0} {1}</article>\n'

	cgid = devid
	if cg.id:
		cgid += cg.id
	cglen = (cg.end - cg.start) * 1000
	if cglen < sv.mincglen:
		return num

	fmt = '<r>(%.3f ms @ '+sv.timeformat+' to '+sv.timeformat+')</r>'
	flen = fmt % (cglen, cg.start, cg.end)
	hf.write(html_func_top.format(cgid, color, num, title, flen))
	num += 1
	for line in cg.list:
		if(line.length < 0.000000001):
			flen = ''
		else:
			fmt = '<n>(%.3f ms @ '+sv.timeformat+')</n>'
			flen = fmt % (line.length*1000, line.time)
		if(line.freturn and line.fcall):
			hf.write(html_func_leaf.format(line.name, flen))
		elif(line.freturn):
			hf.write(html_func_end)
		else:
			hf.write(html_func_start.format(num, line.name, flen))
			num += 1
	hf.write(html_func_end)
	return num

def addCallgraphs(sv, hf, data):
	hf.write('<section id="callgraphs" class="callgraph">\n')
	# write out the ftrace data converted to html
	num = 0
	for p in data.phases:
		if sv.cgphase and p != sv.cgphase:
			continue
		list = data.dmesg[p]['list']
		for devname in data.sortedDevices(p):
			if len(sv.devicefilter) > 0 and devname not in sv.devicefilter:
				continue
			dev = list[devname]
			color = 'white'
			if 'color' in data.dmesg[p]:
				color = data.dmesg[p]['color']
			if 'color' in dev:
				color = dev['color']
			name = devname
			if(devname in sv.devprops):
				name = sv.devprops[devname].altName(devname)
			if sv.suspendmode in suspendmodename:
				name += ' '+p
			if('ftrace' in dev):
				cg = dev['ftrace']
				num = callgraphHTML(sv, hf, num, cg,
					name, color, dev['id'])
			if('ftraces' in dev):
				for cg in dev['ftraces']:
					num = callgraphHTML(sv, hf, num, cg,
						name+' &rarr; '+cg.name, color, dev['id'])

	hf.write('\n\n    </section>\n')

# Function: createHTMLSummarySimple
# Description:
#	 Create summary html file for a series of tests
# Arguments:
#	 testruns: array of Data objects from parseTraceLog
def createHTMLSummarySimple(testruns, htmlfile, folder):
	# write the html header first (html head, css code, up to body start)
	html = '<!DOCTYPE html>\n<html>\n<head>\n\
	<meta http-equiv="content-type" content="text/html; charset=UTF-8">\n\
	<title>SleepGraph Summary</title>\n\
	<style type=\'text/css\'>\n\
		.stamp {width: 100%;text-align:center;background:#888;line-height:30px;color:white;font: 25px Arial;}\n\
		table {width:100%;border-collapse: collapse;}\n\
		.summary {border:1px solid;}\n\
		th {border: 1px solid black;background:#222;color:white;}\n\
		td {font: 16px "Times New Roman";text-align: center;}\n\
		tr.alt td {background:#ddd;}\n\
		tr.avg td {background:#aaa;}\n\
	</style>\n</head>\n<body>\n'

	# group test header
	html += '<div class="stamp">%s (%d tests)</div>\n' % (folder, len(testruns))
	th = '\t<th>{0}</th>\n'
	td = '\t<td>{0}</td>\n'
	tdlink = '\t<td><a href="{0}">html</a></td>\n'

	# table header
	html += '<table class="summary">\n<tr>\n' + th.format('#') +\
		th.format('Mode') + th.format('Host') + th.format('Kernel') +\
		th.format('Test Time') + th.format('Suspend') + th.format('Resume') +\
		th.format('Detail') + '</tr>\n'

	# test data, 1 row per test
	avg = '<tr class="avg"><td></td><td></td><td></td><td></td>'+\
		'<td>Average of {0} {1} tests</td><td>{2}</td><td>{3}</td><td></td></tr>\n'
	sTimeAvg = rTimeAvg = 0.0
	mode = ''
	num = 0
	for data in sorted(testruns, key=lambda v:(v['mode'], v['host'], v['kernel'])):
		if mode != data['mode']:
			# test average line
			if(num > 0):
				sTimeAvg /= (num - 1)
				rTimeAvg /= (num - 1)
				html += avg.format('%d' % (num - 1), mode,
					'%3.3f ms' % sTimeAvg, '%3.3f ms' % rTimeAvg)
			sTimeAvg = rTimeAvg = 0.0
			mode = data['mode']
			num = 1
		# alternate row color
		if num % 2 == 1:
			html += '<tr class="alt">\n'
		else:
			html += '<tr>\n'
		html += td.format("%d" % num)
		num += 1
		# basic info
		for item in ['mode', 'host', 'kernel', 'time']:
			val = "unknown"
			if(item in data):
				val = data[item]
			html += td.format(val)
		# suspend time
		sTime = float(data['suspend'])
		sTimeAvg += sTime
		html += td.format('%.3f ms' % sTime)
		# resume time
		rTime = float(data['resume'])
		rTimeAvg += rTime
		html += td.format('%.3f ms' % rTime)
		# link to the output html
		html += tdlink.format(data['url']) + '</tr>\n'
	# last test average line
	if(num > 0):
		sTimeAvg /= (num - 1)
		rTimeAvg /= (num - 1)
		html += avg.format('%d' % (num - 1), mode,
			'%3.3f ms' % sTimeAvg, '%3.3f ms' % rTimeAvg)

	# flush the data to file
	hf = open(htmlfile, 'w')
	hf.write(html+'</table>\n</body>\n</html>\n')
	hf.close()

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
	if len(testruns) < 1:
		print('ERROR: Not enough test data to build a timeline')
		return

	kerror = False
	for data in testruns:
		if data.kerror:
			kerror = True
		data.normalizeTime(testruns[-1].tSuspended)

	# html function templates
	html_error = '<div id="{1}" title="kernel error/warning" class="err" style="right:{0}%">ERROR&rarr;</div>\n'
	html_traceevent = '<div title="{0}" class="traceevent{6}" style="left:{1}%;top:{2}px;height:{3}px;width:{4}%;line-height:{3}px;{7}">{5}</div>\n'
	html_cpuexec = '<div class="jiffie" style="left:{0}%;top:{1}px;height:{2}px;width:{3}%;background:{4};"></div>\n'
	html_timetotal = '<table class="time1">\n<tr>'\
		'<td class="green" title="{3}">{2} Suspend Time: <b>{0} ms</b></td>'\
		'<td class="yellow" title="{4}">{2} Resume Time: <b>{1} ms</b></td>'\
		'</tr>\n</table>\n'
	html_timetotal2 = '<table class="time1">\n<tr>'\
		'<td class="green" title="{4}">{3} Suspend Time: <b>{0} ms</b></td>'\
		'<td class="gray" title="time spent in low-power mode with clock running">'+sysvals.suspendmode+' time: <b>{1} ms</b></td>'\
		'<td class="yellow" title="{5}">{3} Resume Time: <b>{2} ms</b></td>'\
		'</tr>\n</table>\n'
	html_timetotal3 = '<table class="time1">\n<tr>'\
		'<td class="green">Execution Time: <b>{0} ms</b></td>'\
		'<td class="yellow">Command: <b>{1}</b></td>'\
		'</tr>\n</table>\n'
	html_timegroups = '<table class="time2">\n<tr>'\
		'<td class="green" title="time from kernel enter_state({5}) to firmware mode [kernel time only]">{4}Kernel Suspend: {0} ms</td>'\
		'<td class="purple">{4}Firmware Suspend: {1} ms</td>'\
		'<td class="purple">{4}Firmware Resume: {2} ms</td>'\
		'<td class="yellow" title="time from firmware mode to return from kernel enter_state({5}) [kernel time only]">{4}Kernel Resume: {3} ms</td>'\
		'</tr>\n</table>\n'

	# html format variables
	scaleH = 20
	if kerror:
		scaleH = 40

	# device timeline
	vprint('Creating Device Timeline...')

	devtl = Timeline(30, scaleH)

	# write the test title and general info header
	devtl.createHeader(sysvals)

	# Generate the header for this timeline
	for data in testruns:
		tTotal = data.end - data.start
		sktime, rktime = data.getTimeValues()
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
			devtl.html += thtml
		elif data.fwValid:
			suspend_time = '%.0f'%(sktime + (data.fwSuspend/1000000.0))
			resume_time = '%.0f'%(rktime + (data.fwResume/1000000.0))
			testdesc1 = 'Total'
			testdesc2 = ''
			stitle = 'time from kernel enter_state(%s) to low-power mode [kernel & firmware time]' % sysvals.suspendmode
			rtitle = 'time from low-power mode to return from kernel enter_state(%s) [firmware & kernel time]' % sysvals.suspendmode
			if(len(testruns) > 1):
				testdesc1 = testdesc2 = ordinal(data.testnumber+1)
				testdesc2 += ' '
			if(data.tLow == 0):
				thtml = html_timetotal.format(suspend_time, \
					resume_time, testdesc1, stitle, rtitle)
			else:
				thtml = html_timetotal2.format(suspend_time, low_time, \
					resume_time, testdesc1, stitle, rtitle)
			devtl.html += thtml
			sftime = '%.3f'%(data.fwSuspend / 1000000.0)
			rftime = '%.3f'%(data.fwResume / 1000000.0)
			devtl.html += html_timegroups.format('%.3f'%sktime, \
				sftime, rftime, '%.3f'%rktime, testdesc2, sysvals.suspendmode)
		else:
			suspend_time = '%.3f' % sktime
			resume_time = '%.3f' % rktime
			testdesc = 'Kernel'
			stitle = 'time from kernel enter_state(%s) to firmware mode [kernel time only]' % sysvals.suspendmode
			rtitle = 'time from firmware mode to return from kernel enter_state(%s) [kernel time only]' % sysvals.suspendmode
			if(len(testruns) > 1):
				testdesc = ordinal(data.testnumber+1)+' '+testdesc
			if(data.tLow == 0):
				thtml = html_timetotal.format(suspend_time, \
					resume_time, testdesc, stitle, rtitle)
			else:
				thtml = html_timetotal2.format(suspend_time, low_time, \
					resume_time, testdesc, stitle, rtitle)
			devtl.html += thtml

	# time scale for potentially multiple datasets
	t0 = testruns[0].start
	tMax = testruns[-1].end
	tTotal = tMax - t0

	# determine the maximum number of rows we need to draw
	fulllist = []
	threadlist = []
	pscnt = 0
	devcnt = 0
	for data in testruns:
		data.selectTimelineDevices('%f', tTotal, sysvals.mindevlen)
		for group in data.devicegroups:
			devlist = []
			for phase in group:
				for devname in data.tdevlist[phase]:
					d = DevItem(data.testnumber, phase, data.dmesg[phase]['list'][devname])
					devlist.append(d)
					if d.isa('kth'):
						threadlist.append(d)
					else:
						if d.isa('ps'):
							pscnt += 1
						else:
							devcnt += 1
						fulllist.append(d)
			if sysvals.mixedphaseheight:
				devtl.getPhaseRows(devlist)
	if not sysvals.mixedphaseheight:
		if len(threadlist) > 0 and len(fulllist) > 0:
			if pscnt > 0 and devcnt > 0:
				msg = 'user processes & device pm callbacks'
			elif pscnt > 0:
				msg = 'user processes'
			else:
				msg = 'device pm callbacks'
			d = testruns[0].addHorizontalDivider(msg, testruns[-1].end)
			fulllist.insert(0, d)
		devtl.getPhaseRows(fulllist)
		if len(threadlist) > 0:
			d = testruns[0].addHorizontalDivider('asynchronous kernel threads', testruns[-1].end)
			threadlist.insert(0, d)
			devtl.getPhaseRows(threadlist, devtl.rows)
	devtl.calcTotalRows()

	# draw the full timeline
	devtl.createZoomBox(sysvals.suspendmode, len(testruns))
	phases = {'suspend':[],'resume':[]}
	for phase in data.dmesg:
		if 'resume' in phase:
			phases['resume'].append(phase)
		else:
			phases['suspend'].append(phase)

	# draw each test run chronologically
	for data in testruns:
		# now draw the actual timeline blocks
		for dir in phases:
			# draw suspend and resume blocks separately
			bname = '%s%d' % (dir[0], data.testnumber)
			if dir == 'suspend':
				m0 = data.start
				mMax = data.tSuspended
				left = '%f' % (((m0-t0)*100.0)/tTotal)
			else:
				m0 = data.tSuspended
				mMax = data.end
				# in an x2 run, remove any gap between blocks
				if len(testruns) > 1 and data.testnumber == 0:
					mMax = testruns[1].start
				left = '%f' % ((((m0-t0)*100.0)+sysvals.srgap/2)/tTotal)
			mTotal = mMax - m0
			# if a timeline block is 0 length, skip altogether
			if mTotal == 0:
				continue
			width = '%f' % (((mTotal*100.0)-sysvals.srgap/2)/tTotal)
			devtl.html += devtl.html_tblock.format(bname, left, width, devtl.scaleH)
			for b in sorted(phases[dir]):
				# draw the phase color background
				phase = data.dmesg[b]
				length = phase['end']-phase['start']
				left = '%f' % (((phase['start']-m0)*100.0)/mTotal)
				width = '%f' % ((length*100.0)/mTotal)
				devtl.html += devtl.html_phase.format(left, width, \
					'%.3f'%devtl.scaleH, '%.3f'%devtl.bodyH, \
					data.dmesg[b]['color'], '')
			for e in data.errorinfo[dir]:
				# draw red lines for any kernel errors found
				t, err = e
				right = '%f' % (((mMax-t)*100.0)/mTotal)
				devtl.html += html_error.format(right, err)
			for b in sorted(phases[dir]):
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
					if 'color' in dev:
						xtrastyle = 'background:%s;' % dev['color']
					if(d in sysvals.devprops):
						name = sysvals.devprops[d].altName(d)
						xtraclass = sysvals.devprops[d].xtraClass()
						xtrainfo = sysvals.devprops[d].xtraInfo()
					elif xtraclass == ' kth':
						xtrainfo = ' kernel_thread'
					if('drv' in dev and dev['drv']):
						drv = ' {%s}' % dev['drv']
					rowheight = devtl.phaseRowHeight(data.testnumber, b, dev['row'])
					rowtop = devtl.phaseRowTop(data.testnumber, b, dev['row'])
					top = '%.3f' % (rowtop + devtl.scaleH)
					left = '%f' % (((dev['start']-m0)*100)/mTotal)
					width = '%f' % (((dev['end']-dev['start'])*100)/mTotal)
					length = ' (%0.3f ms) ' % ((dev['end']-dev['start'])*1000)
					title = name+drv+xtrainfo+length
					if sysvals.suspendmode == 'command':
						title += sysvals.testcommand
					elif xtraclass == ' ps':
						if 'suspend' in b:
							title += 'pre_suspend_process'
						else:
							title += 'post_resume_process'
					else:
						title += b
					devtl.html += devtl.html_device.format(dev['id'], \
						title, left, top, '%.3f'%rowheight, width, \
						d+drv, xtraclass, xtrastyle)
					if('cpuexec' in dev):
						for t in sorted(dev['cpuexec']):
							start, end = t
							j = float(dev['cpuexec'][t]) / 5
							if j > 1.0:
								j = 1.0
							height = '%.3f' % (rowheight/3)
							top = '%.3f' % (rowtop + devtl.scaleH + 2*rowheight/3)
							left = '%f' % (((start-m0)*100)/mTotal)
							width = '%f' % ((end-start)*100/mTotal)
							color = 'rgba(255, 0, 0, %f)' % j
							devtl.html += \
								html_cpuexec.format(left, top, height, width, color)
					if('src' not in dev):
						continue
					# draw any trace events for this device
					for e in dev['src']:
						height = '%.3f' % devtl.rowH
						top = '%.3f' % (rowtop + devtl.scaleH + (e.row*devtl.rowH))
						left = '%f' % (((e.time-m0)*100)/mTotal)
						width = '%f' % (e.length*100/mTotal)
						xtrastyle = ''
						if e.color:
							xtrastyle = 'background:%s;' % e.color
						devtl.html += \
							html_traceevent.format(e.title(), \
								left, top, height, width, e.text(), '', xtrastyle)
			# draw the time scale, try to make the number of labels readable
			devtl.createTimeScale(m0, mMax, tTotal, dir)
			devtl.html += '</div>\n'

	# timeline is finished
	devtl.html += '</div>\n</div>\n'

	# draw a legend which describes the phases by color
	if sysvals.suspendmode != 'command':
		data = testruns[-1]
		devtl.html += '<div class="legend">\n'
		pdelta = 100.0/len(data.phases)
		pmargin = pdelta / 4.0
		for phase in data.phases:
			tmp = phase.split('_')
			id = tmp[0][0]
			if(len(tmp) > 1):
				id += tmp[1][0]
			order = '%.2f' % ((data.dmesg[phase]['order'] * pdelta) + pmargin)
			name = string.replace(phase, '_', ' &nbsp;')
			devtl.html += devtl.html_legend.format(order, \
				data.dmesg[phase]['color'], name, id)
		devtl.html += '</div>\n'

	hf = open(sysvals.htmlfile, 'w')

	# no header or css if its embedded
	if(sysvals.embedded):
		hf.write('pass True tSus %.3f tRes %.3f tLow %.3f fwvalid %s tSus %.3f tRes %.3f\n' %
			(data.tSuspended-data.start, data.end-data.tSuspended, data.tLow, data.fwValid, \
				data.fwSuspend/1000000, data.fwResume/1000000))
	else:
		addCSS(hf, sysvals, len(testruns), kerror)

	# write the device timeline
	hf.write(devtl.html)
	hf.write('<div id="devicedetailtitle"></div>\n')
	hf.write('<div id="devicedetail" style="display:none;">\n')
	# draw the colored boxes for the device detail section
	for data in testruns:
		hf.write('<div id="devicedetail%d">\n' % data.testnumber)
		pscolor = 'linear-gradient(to top left, #ccc, #eee)'
		hf.write(devtl.html_phaselet.format('pre_suspend_process', \
			'0', '0', pscolor))
		for b in data.phases:
			phase = data.dmesg[b]
			length = phase['end']-phase['start']
			left = '%.3f' % (((phase['start']-t0)*100.0)/tTotal)
			width = '%.3f' % ((length*100.0)/tTotal)
			hf.write(devtl.html_phaselet.format(b, left, width, \
				data.dmesg[b]['color']))
		hf.write(devtl.html_phaselet.format('post_resume_process', \
			'0', '0', pscolor))
		if sysvals.suspendmode == 'command':
			hf.write(devtl.html_phaselet.format('cmdexec', '0', '0', pscolor))
		hf.write('</div>\n')
	hf.write('</div>\n')

	# write the ftrace data (callgraph)
	if sysvals.cgtest >= 0 and len(testruns) > sysvals.cgtest:
		data = testruns[sysvals.cgtest]
	else:
		data = testruns[-1]
	if(sysvals.usecallgraph and not sysvals.embedded):
		addCallgraphs(sysvals, hf, data)

	# add the test log as a hidden div
	if sysvals.testlog and sysvals.logmsg:
		hf.write('<div id="testlog" style="display:none;">\n'+sysvals.logmsg+'</div>\n')
	# add the dmesg log as a hidden div
	if sysvals.dmesglog and sysvals.dmesgfile:
		hf.write('<div id="dmesglog" style="display:none;">\n')
		lf = open(sysvals.dmesgfile, 'r')
		for line in lf:
			line = line.replace('<', '&lt').replace('>', '&gt')
			hf.write(line)
		lf.close()
		hf.write('</div>\n')
	# add the ftrace log as a hidden div
	if sysvals.ftracelog and sysvals.ftracefile:
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

def addCSS(hf, sv, testcount=1, kerror=False, extra=''):
	kernel = sv.stamp['kernel']
	host = sv.hostname[0].upper()+sv.hostname[1:]
	mode = sv.suspendmode
	if sv.suspendmode in suspendmodename:
		mode = suspendmodename[sv.suspendmode]
	title = host+' '+mode+' '+kernel

	# various format changes by flags
	cgchk = 'checked'
	cgnchk = 'not(:checked)'
	if sv.cgexp:
		cgchk = 'not(:checked)'
		cgnchk = 'checked'

	hoverZ = 'z-index:8;'
	if sv.usedevsrc:
		hoverZ = ''

	devlistpos = 'absolute'
	if testcount > 1:
		devlistpos = 'relative'

	scaleTH = 20
	if kerror:
		scaleTH = 60

	# write the html header first (html head, css code, up to body start)
	html_header = '<!DOCTYPE html>\n<html>\n<head>\n\
	<meta http-equiv="content-type" content="text/html; charset=UTF-8">\n\
	<title>'+title+'</title>\n\
	<style type=\'text/css\'>\n\
		body {overflow-y:scroll;}\n\
		.stamp {width:100%;text-align:center;background:gray;line-height:30px;color:white;font:25px Arial;}\n\
		.stamp.sysinfo {font:10px Arial;}\n\
		.callgraph {margin-top:30px;box-shadow:5px 5px 20px black;}\n\
		.callgraph article * {padding-left:28px;}\n\
		h1 {color:black;font:bold 30px Times;}\n\
		t0 {color:black;font:bold 30px Times;}\n\
		t1 {color:black;font:30px Times;}\n\
		t2 {color:black;font:25px Times;}\n\
		t3 {color:black;font:20px Times;white-space:nowrap;}\n\
		t4 {color:black;font:bold 30px Times;line-height:60px;white-space:nowrap;}\n\
		cS {font:bold 13px Times;}\n\
		table {width:100%;}\n\
		.gray {background:rgba(80,80,80,0.1);}\n\
		.green {background:rgba(204,255,204,0.4);}\n\
		.purple {background:rgba(128,0,128,0.2);}\n\
		.yellow {background:rgba(255,255,204,0.4);}\n\
		.blue {background:rgba(169,208,245,0.4);}\n\
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
		.zoombox {position:relative;width:100%;overflow-x:scroll;-webkit-user-select:none;-moz-user-select:none;user-select:none;}\n\
		.timeline {position:relative;font-size:14px;cursor:pointer;width:100%; overflow:hidden;background:linear-gradient(#cccccc, white);}\n\
		.thread {position:absolute;height:0%;overflow:hidden;z-index:7;line-height:30px;font-size:14px;border:1px solid;text-align:center;white-space:nowrap;}\n\
		.thread.ps {border-radius:3px;background:linear-gradient(to top, #ccc, #eee);}\n\
		.thread:hover {background:white;border:1px solid red;'+hoverZ+'}\n\
		.thread.sec,.thread.sec:hover {background:black;border:0;color:white;line-height:15px;font-size:10px;}\n\
		.hover {background:white;border:1px solid red;'+hoverZ+'}\n\
		.hover.sync {background:white;}\n\
		.hover.bg,.hover.kth,.hover.sync,.hover.ps {background:white;}\n\
		.jiffie {position:absolute;pointer-events: none;z-index:8;}\n\
		.traceevent {position:absolute;font-size:10px;z-index:7;overflow:hidden;color:black;text-align:center;white-space:nowrap;border-radius:5px;border:1px solid black;background:linear-gradient(to bottom right,#CCC,#969696);}\n\
		.traceevent:hover {color:white;font-weight:bold;border:1px solid white;}\n\
		.phase {position:absolute;overflow:hidden;border:0px;text-align:center;}\n\
		.phaselet {float:left;overflow:hidden;border:0px;text-align:center;min-height:100px;font-size:24px;}\n\
		.t {position:absolute;line-height:'+('%d'%scaleTH)+'px;pointer-events:none;top:0;height:100%;border-right:1px solid black;z-index:6;}\n\
		.err {position:absolute;top:0%;height:100%;border-right:3px solid red;color:red;font:bold 14px Times;line-height:18px;}\n\
		.legend {position:relative; width:100%; height:40px; text-align:center;margin-bottom:20px}\n\
		.legend .square {position:absolute;cursor:pointer;top:10px; width:0px;height:20px;border:1px solid;padding-left:20px;}\n\
		button {height:40px;width:200px;margin-bottom:20px;margin-top:20px;font-size:24px;}\n\
		.btnfmt {position:relative;float:right;height:25px;width:auto;margin-top:3px;margin-bottom:0;font-size:10px;text-align:center;}\n\
		.devlist {position:'+devlistpos+';width:190px;}\n\
		a:link {color:white;text-decoration:none;}\n\
		a:visited {color:white;}\n\
		a:hover {color:white;}\n\
		a:active {color:white;}\n\
		.version {position:relative;float:left;color:white;font-size:10px;line-height:30px;margin-left:10px;}\n\
		#devicedetail {min-height:100px;box-shadow:5px 5px 20px black;}\n\
		.tblock {position:absolute;height:100%;background:#ddd;}\n\
		.tback {position:absolute;width:100%;background:linear-gradient(#ccc, #ddd);}\n\
		.bg {z-index:1;}\n\
'+extra+'\
	</style>\n</head>\n<body>\n'
	hf.write(html_header)

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
	'	var dragval = [0, 0];\n'\
	'	function redrawTimescale(t0, tMax, tS) {\n'\
	'		var rline = \'<div class="t" style="left:0;border-left:1px solid black;border-right:0;">\';\n'\
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
	'				var mode = list[i].id[5];\n'\
	'				if(mode == "s") {\n'\
	'					pos = 100 - (((j)*tS*100)/mTotal) - divEdge;\n'\
	'					val = (j-divTotal+1)*tS;\n'\
	'					if(j == divTotal - 1)\n'\
	'						htmlline = \'<div class="t" style="right:\'+pos+\'%"><cS>S&rarr;</cS></div>\';\n'\
	'					else\n'\
	'						htmlline = \'<div class="t" style="right:\'+pos+\'%">\'+val+\'ms</div>\';\n'\
	'				} else {\n'\
	'					pos = 100 - (((j)*tS*100)/mTotal);\n'\
	'					val = (j)*tS;\n'\
	'					htmlline = \'<div class="t" style="right:\'+pos+\'%">\'+val+\'ms</div>\';\n'\
	'					if(j == 0)\n'\
	'						if(mode == "r")\n'\
	'							htmlline = rline+"<cS>&larr;R</cS></div>";\n'\
	'						else\n'\
	'							htmlline = rline+"<cS>0ms</div>";\n'\
	'				}\n'\
	'				html += htmlline;\n'\
	'			}\n'\
	'			timescale.innerHTML = html;\n'\
	'		}\n'\
	'	}\n'\
	'	function zoomTimeline() {\n'\
	'		var dmesg = document.getElementById("dmesg");\n'\
	'		var zoombox = document.getElementById("dmesgzoombox");\n'\
	'		var left = zoombox.scrollLeft;\n'\
	'		var val = parseFloat(dmesg.style.width);\n'\
	'		var newval = 100;\n'\
	'		var sh = window.outerWidth / 2;\n'\
	'		if(this.id == "zoomin") {\n'\
	'			newval = val * 1.2;\n'\
	'			if(newval > 910034) newval = 910034;\n'\
	'			dmesg.style.width = newval+"%";\n'\
	'			zoombox.scrollLeft = ((left + sh) * newval / val) - sh;\n'\
	'		} else if (this.id == "zoomout") {\n'\
	'			newval = val / 1.2;\n'\
	'			if(newval < 100) newval = 100;\n'\
	'			dmesg.style.width = newval+"%";\n'\
	'			zoombox.scrollLeft = ((left + sh) * newval / val) - sh;\n'\
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
	'	function deviceName(title) {\n'\
	'		var name = title.slice(0, title.indexOf(" ("));\n'\
	'		return name;\n'\
	'	}\n'\
	'	function deviceHover() {\n'\
	'		var name = deviceName(this.title);\n'\
	'		var dmesg = document.getElementById("dmesg");\n'\
	'		var dev = dmesg.getElementsByClassName("thread");\n'\
	'		var cpu = -1;\n'\
	'		if(name.match("CPU_ON\[[0-9]*\]"))\n'\
	'			cpu = parseInt(name.slice(7));\n'\
	'		else if(name.match("CPU_OFF\[[0-9]*\]"))\n'\
	'			cpu = parseInt(name.slice(8));\n'\
	'		for (var i = 0; i < dev.length; i++) {\n'\
	'			dname = deviceName(dev[i].title);\n'\
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
	'		var name = deviceName(title);\n'\
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
	'		var name = deviceName(this.title);\n'\
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
	'			dname = deviceName(dev[i].title);\n'\
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
	'					var pname = "<t3 style=\\"font-size:"+fs2+"px\\">"+phases[i].id.replace(new RegExp("_", "g"), " ")+"</t3>";\n'\
	'					phases[i].innerHTML = time+pname;\n'\
	'				} else {\n'\
	'					phases[i].style.width = "0%";\n'\
	'					phases[i].style.left = left+"%";\n'\
	'				}\n'\
	'			}\n'\
	'		}\n'\
	'		if(typeof devstats !== \'undefined\')\n'\
	'			callDetail(this.id, this.title);\n'\
	'		var cglist = document.getElementById("callgraphs");\n'\
	'		if(!cglist) return;\n'\
	'		var cg = cglist.getElementsByClassName("atop");\n'\
	'		if(cg.length < 10) return;\n'\
	'		for (var i = 0; i < cg.length; i++) {\n'\
	'			cgid = cg[i].id.split("x")[0]\n'\
	'			if(idlist.indexOf(cgid) >= 0) {\n'\
	'				cg[i].style.display = "block";\n'\
	'			} else {\n'\
	'				cg[i].style.display = "none";\n'\
	'			}\n'\
	'		}\n'\
	'	}\n'\
	'	function callDetail(devid, devtitle) {\n'\
	'		if(!(devid in devstats) || devstats[devid].length < 1)\n'\
	'			return;\n'\
	'		var list = devstats[devid];\n'\
	'		var tmp = devtitle.split(" ");\n'\
	'		var name = tmp[0], phase = tmp[tmp.length-1];\n'\
	'		var dd = document.getElementById(phase);\n'\
	'		var total = parseFloat(tmp[1].slice(1));\n'\
	'		var mlist = [];\n'\
	'		var maxlen = 0;\n'\
	'		var info = []\n'\
	'		for(var i in list) {\n'\
	'			if(list[i][0] == "@") {\n'\
	'				info = list[i].split("|");\n'\
	'				continue;\n'\
	'			}\n'\
	'			var tmp = list[i].split("|");\n'\
	'			var t = parseFloat(tmp[0]), f = tmp[1], c = parseInt(tmp[2]);\n'\
	'			var p = (t*100.0/total).toFixed(2);\n'\
	'			mlist[mlist.length] = [f, c, t.toFixed(2), p+"%"];\n'\
	'			if(f.length > maxlen)\n'\
	'				maxlen = f.length;\n'\
	'		}\n'\
	'		var pad = 5;\n'\
	'		if(mlist.length == 0) pad = 30;\n'\
	'		var html = \'<div style="padding-top:\'+pad+\'px"><t3> <b>\'+name+\':</b>\';\n'\
	'		if(info.length > 2)\n'\
	'			html += " start=<b>"+info[1]+"</b>, end=<b>"+info[2]+"</b>";\n'\
	'		if(info.length > 3)\n'\
	'			html += ", length<i>(w/o overhead)</i>=<b>"+info[3]+" ms</b>";\n'\
	'		if(info.length > 4)\n'\
	'			html += ", return=<b>"+info[4]+"</b>";\n'\
	'		html += "</t3></div>";\n'\
	'		if(mlist.length > 0) {\n'\
	'			html += \'<table class=fstat style="padding-top:\'+(maxlen*5)+\'px;"><tr><th>Function</th>\';\n'\
	'			for(var i in mlist)\n'\
	'				html += "<td class=vt>"+mlist[i][0]+"</td>";\n'\
	'			html += "</tr><tr><th>Calls</th>";\n'\
	'			for(var i in mlist)\n'\
	'				html += "<td>"+mlist[i][1]+"</td>";\n'\
	'			html += "</tr><tr><th>Time(ms)</th>";\n'\
	'			for(var i in mlist)\n'\
	'				html += "<td>"+mlist[i][2]+"</td>";\n'\
	'			html += "</tr><tr><th>Percent</th>";\n'\
	'			for(var i in mlist)\n'\
	'				html += "<td>"+mlist[i][3]+"</td>";\n'\
	'			html += "</tr></table>";\n'\
	'		}\n'\
	'		dd.innerHTML = html;\n'\
	'		var height = (maxlen*5)+100;\n'\
	'		dd.style.height = height+"px";\n'\
	'		document.getElementById("devicedetail").style.height = height+"px";\n'\
	'	}\n'\
	'	function callSelect() {\n'\
	'		var cglist = document.getElementById("callgraphs");\n'\
	'		if(!cglist) return;\n'\
	'		var cg = cglist.getElementsByClassName("atop");\n'\
	'		for (var i = 0; i < cg.length; i++) {\n'\
	'			if(this.id == cg[i].id) {\n'\
	'				cg[i].style.display = "block";\n'\
	'			} else {\n'\
	'				cg[i].style.display = "none";\n'\
	'			}\n'\
	'		}\n'\
	'	}\n'\
	'	function devListWindow(e) {\n'\
	'		var win = window.open();\n'\
	'		var html = "<title>"+e.target.innerHTML+"</title>"+\n'\
	'			"<style type=\\"text/css\\">"+\n'\
	'			"   ul {list-style-type:circle;padding-left:10px;margin-left:10px;}"+\n'\
	'			"</style>"\n'\
	'		var dt = devtable[0];\n'\
	'		if(e.target.id != "devlist1")\n'\
	'			dt = devtable[1];\n'\
	'		win.document.write(html+dt);\n'\
	'	}\n'\
	'	function errWindow() {\n'\
	'		var text = this.id;\n'\
	'		var win = window.open();\n'\
	'		win.document.write("<pre>"+text+"</pre>");\n'\
	'		win.document.close();\n'\
	'	}\n'\
	'	function logWindow(e) {\n'\
	'		var name = e.target.id.slice(4);\n'\
	'		var win = window.open();\n'\
	'		var log = document.getElementById(name+"log");\n'\
	'		var title = "<title>"+document.title.split(" ")[0]+" "+name+" log</title>";\n'\
	'		win.document.write(title+"<pre>"+log.innerHTML+"</pre>");\n'\
	'		win.document.close();\n'\
	'	}\n'\
	'	function onMouseDown(e) {\n'\
	'		dragval[0] = e.clientX;\n'\
	'		dragval[1] = document.getElementById("dmesgzoombox").scrollLeft;\n'\
	'		document.onmousemove = onMouseMove;\n'\
	'	}\n'\
	'	function onMouseMove(e) {\n'\
	'		var zoombox = document.getElementById("dmesgzoombox");\n'\
	'		zoombox.scrollLeft = dragval[1] + dragval[0] - e.clientX;\n'\
	'	}\n'\
	'	function onMouseUp(e) {\n'\
	'		document.onmousemove = null;\n'\
	'	}\n'\
	'	function onKeyPress(e) {\n'\
	'		var c = e.charCode;\n'\
	'		if(c != 42 && c != 43 && c != 45) return;\n'\
	'		var click = document.createEvent("Events");\n'\
	'		click.initEvent("click", true, false);\n'\
	'		if(c == 43)  \n'\
	'			document.getElementById("zoomin").dispatchEvent(click);\n'\
	'		else if(c == 45)\n'\
	'			document.getElementById("zoomout").dispatchEvent(click);\n'\
	'		else if(c == 42)\n'\
	'			document.getElementById("zoomdef").dispatchEvent(click);\n'\
	'	}\n'\
	'	window.addEventListener("resize", function () {zoomTimeline();});\n'\
	'	window.addEventListener("load", function () {\n'\
	'		var dmesg = document.getElementById("dmesg");\n'\
	'		dmesg.style.width = "100%"\n'\
	'		dmesg.onmousedown = onMouseDown;\n'\
	'		document.onmouseup = onMouseUp;\n'\
	'		document.onkeypress = onKeyPress;\n'\
	'		document.getElementById("zoomin").onclick = zoomTimeline;\n'\
	'		document.getElementById("zoomout").onclick = zoomTimeline;\n'\
	'		document.getElementById("zoomdef").onclick = zoomTimeline;\n'\
	'		var list = document.getElementsByClassName("err");\n'\
	'		for (var i = 0; i < list.length; i++)\n'\
	'			list[i].onclick = errWindow;\n'\
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
	'		var dev = dmesg.getElementsByClassName("srccall");\n'\
	'		for (var i = 0; i < dev.length; i++)\n'\
	'			dev[i].onclick = callSelect;\n'\
	'		zoomTimeline();\n'\
	'	});\n'\
	'</script>\n'
	hf.write(script_code);

# Function: executeSuspend
# Description:
#	 Execute system suspend through the sysfs interface, then copy the output
#	 dmesg and ftrace files to the test output directory.
def executeSuspend():
	pm = ProcessMonitor()
	tp = sysvals.tpath
	fwdata = []
	# mark the start point in the kernel ring buffer just as we start
	sysvals.initdmesg()
	# start ftrace
	if(sysvals.usecallgraph or sysvals.usetraceevents):
		print('START TRACING')
		sysvals.fsetVal('1', 'tracing_on')
		if sysvals.useprocmon:
			pm.start()
	# execute however many s/r runs requested
	for count in range(1,sysvals.execcount+1):
		# x2delay in between test runs
		if(count > 1 and sysvals.x2delay > 0):
			sysvals.fsetVal('WAIT %d' % sysvals.x2delay, 'trace_marker')
			time.sleep(sysvals.x2delay/1000.0)
			sysvals.fsetVal('WAIT END', 'trace_marker')
		# start message
		if sysvals.testcommand != '':
			print('COMMAND START')
		else:
			if(sysvals.rtcwake):
				print('SUSPEND START')
			else:
				print('SUSPEND START (press a key to resume)')
		# set rtcwake
		if(sysvals.rtcwake):
			print('will issue an rtcwake in %d seconds' % sysvals.rtcwaketime)
			sysvals.rtcWakeAlarmOn()
		# start of suspend trace marker
		if(sysvals.usecallgraph or sysvals.usetraceevents):
			sysvals.fsetVal('SUSPEND START', 'trace_marker')
		# predelay delay
		if(count == 1 and sysvals.predelay > 0):
			sysvals.fsetVal('WAIT %d' % sysvals.predelay, 'trace_marker')
			time.sleep(sysvals.predelay/1000.0)
			sysvals.fsetVal('WAIT END', 'trace_marker')
		# initiate suspend or command
		if sysvals.testcommand != '':
			call(sysvals.testcommand+' 2>&1', shell=True);
		else:
			mode = sysvals.suspendmode
			if sysvals.memmode and os.path.exists(sysvals.mempowerfile):
				mode = 'mem'
				pf = open(sysvals.mempowerfile, 'w')
				pf.write(sysvals.memmode)
				pf.close()
			pf = open(sysvals.powerfile, 'w')
			pf.write(mode)
			# execution will pause here
			try:
				pf.close()
			except:
				pass
		if(sysvals.rtcwake):
			sysvals.rtcWakeAlarmOff()
		# postdelay delay
		if(count == sysvals.execcount and sysvals.postdelay > 0):
			sysvals.fsetVal('WAIT %d' % sysvals.postdelay, 'trace_marker')
			time.sleep(sysvals.postdelay/1000.0)
			sysvals.fsetVal('WAIT END', 'trace_marker')
		# return from suspend
		print('RESUME COMPLETE')
		if(sysvals.usecallgraph or sysvals.usetraceevents):
			sysvals.fsetVal('RESUME COMPLETE', 'trace_marker')
		if(sysvals.suspendmode == 'mem' or sysvals.suspendmode == 'command'):
			fwdata.append(getFPDT(False))
	# stop ftrace
	if(sysvals.usecallgraph or sysvals.usetraceevents):
		if sysvals.useprocmon:
			pm.stop()
		sysvals.fsetVal('0', 'tracing_on')
		print('CAPTURING TRACE')
		sysvals.writeDatafileHeader(sysvals.ftracefile, fwdata)
		call('cat '+tp+'trace >> '+sysvals.ftracefile, shell=True)
		sysvals.fsetVal('', 'trace')
		devProps()
	# grab a copy of the dmesg output
	print('CAPTURING DMESG')
	sysvals.writeDatafileHeader(sysvals.dmesgfile, fwdata)
	sysvals.getdmesg()

# Function: setUSBDevicesAuto
# Description:
#	 Set the autosuspend control parameter of all USB devices to auto
#	 This can be dangerous, so use at your own risk, most devices are set
#	 to always-on since the kernel cant determine if the device can
#	 properly autosuspend
def setUSBDevicesAuto():
	sysvals.rootCheck(True)
	for dirname, dirnames, filenames in os.walk('/sys/devices'):
		if(re.match('.*/usb[0-9]*.*', dirname) and
			'idVendor' in filenames and 'idProduct' in filenames):
			call('echo auto > %s/power/control' % dirname, shell=True)
			name = dirname.split('/')[-1]
			desc = Popen(['cat', '%s/product' % dirname],
				stderr=PIPE, stdout=PIPE).stdout.read().replace('\n', '')
			ctrl = Popen(['cat', '%s/power/control' % dirname],
				stderr=PIPE, stdout=PIPE).stdout.read().replace('\n', '')
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
				field[i] = Popen(['cat', '%s/%s' % (dirname, i)],
					stderr=PIPE, stdout=PIPE).stdout.read().replace('\n', '')
			name = dirname.split('/')[-1]
			for i in power:
				power[i] = Popen(['cat', '%s/power/%s' % (dirname, i)],
					stderr=PIPE, stdout=PIPE).stdout.read().replace('\n', '')
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
		doError('%s does not exist' % sysvals.ftracefile)

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
		dev = m.group('d')
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
	modes = []
	if(os.path.exists(sysvals.powerfile)):
		fp = open(sysvals.powerfile, 'r')
		modes = string.split(fp.read())
		fp.close()
	if(os.path.exists(sysvals.mempowerfile)):
		deep = False
		fp = open(sysvals.mempowerfile, 'r')
		for m in string.split(fp.read()):
			memmode = m.strip('[]')
			if memmode == 'deep':
				deep = True
			else:
				modes.append('mem-%s' % memmode)
		fp.close()
		if 'mem' in modes and not deep:
			modes.remove('mem')
	return modes

# Function: dmidecode
# Description:
#	 Read the bios tables and pull out system info
# Arguments:
#	 mempath: /dev/mem or custom mem path
#	 fatal: True to exit on error, False to return empty dict
# Output:
#	 A dict object with all available key/values
def dmidecode(mempath, fatal=False):
	out = dict()

	# the list of values to retrieve, with hardcoded (type, idx)
	info = {
		'bios-vendor': (0, 4),
		'bios-version': (0, 5),
		'bios-release-date': (0, 8),
		'system-manufacturer': (1, 4),
		'system-product-name': (1, 5),
		'system-version': (1, 6),
		'system-serial-number': (1, 7),
		'baseboard-manufacturer': (2, 4),
		'baseboard-product-name': (2, 5),
		'baseboard-version': (2, 6),
		'baseboard-serial-number': (2, 7),
		'chassis-manufacturer': (3, 4),
		'chassis-type': (3, 5),
		'chassis-version': (3, 6),
		'chassis-serial-number': (3, 7),
		'processor-manufacturer': (4, 7),
		'processor-version': (4, 16),
	}
	if(not os.path.exists(mempath)):
		if(fatal):
			doError('file does not exist: %s' % mempath)
		return out
	if(not os.access(mempath, os.R_OK)):
		if(fatal):
			doError('file is not readable: %s' % mempath)
		return out

	# by default use legacy scan, but try to use EFI first
	memaddr = 0xf0000
	memsize = 0x10000
	for ep in ['/sys/firmware/efi/systab', '/proc/efi/systab']:
		if not os.path.exists(ep) or not os.access(ep, os.R_OK):
			continue
		fp = open(ep, 'r')
		buf = fp.read()
		fp.close()
		i = buf.find('SMBIOS=')
		if i >= 0:
			try:
				memaddr = int(buf[i+7:], 16)
				memsize = 0x20
			except:
				continue

	# read in the memory for scanning
	fp = open(mempath, 'rb')
	try:
		fp.seek(memaddr)
		buf = fp.read(memsize)
	except:
		if(fatal):
			doError('DMI table is unreachable, sorry')
		else:
			return out
	fp.close()

	# search for either an SM table or DMI table
	i = base = length = num = 0
	while(i < memsize):
		if buf[i:i+4] == '_SM_' and i < memsize - 16:
			length = struct.unpack('H', buf[i+22:i+24])[0]
			base, num = struct.unpack('IH', buf[i+24:i+30])
			break
		elif buf[i:i+5] == '_DMI_':
			length = struct.unpack('H', buf[i+6:i+8])[0]
			base, num = struct.unpack('IH', buf[i+8:i+14])
			break
		i += 16
	if base == 0 and length == 0 and num == 0:
		if(fatal):
			doError('Neither SMBIOS nor DMI were found')
		else:
			return out

	# read in the SM or DMI table
	fp = open(mempath, 'rb')
	try:
		fp.seek(base)
		buf = fp.read(length)
	except:
		if(fatal):
			doError('DMI table is unreachable, sorry')
		else:
			return out
	fp.close()

	# scan the table for the values we want
	count = i = 0
	while(count < num and i <= len(buf) - 4):
		type, size, handle = struct.unpack('BBH', buf[i:i+4])
		n = i + size
		while n < len(buf) - 1:
			if 0 == struct.unpack('H', buf[n:n+2])[0]:
				break
			n += 1
		data = buf[i+size:n+2].split('\0')
		for name in info:
			itype, idxadr = info[name]
			if itype == type:
				idx = struct.unpack('B', buf[i+idxadr])[0]
				if idx > 0 and idx < len(data) - 1:
					s = data[idx-1].strip()
					if s and s.lower() != 'to be filled by o.e.m.':
						out[name] = data[idx-1]
		i = n + 2
		count += 1
	return out

# Function: getFPDT
# Description:
#	 Read the acpi bios tables and pull out FPDT, the firmware data
# Arguments:
#	 output: True to output the info to stdout, False otherwise
def getFPDT(output):
	rectype = {}
	rectype[0] = 'Firmware Basic Boot Performance Record'
	rectype[1] = 'S3 Performance Table Record'
	prectype = {}
	prectype[0] = 'Basic S3 Resume Performance Record'
	prectype[1] = 'Basic S3 Suspend Performance Record'

	sysvals.rootCheck(True)
	if(not os.path.exists(sysvals.fpdtpath)):
		if(output):
			doError('file does not exist: %s' % sysvals.fpdtpath)
		return False
	if(not os.access(sysvals.fpdtpath, os.R_OK)):
		if(output):
			doError('file is not readable: %s' % sysvals.fpdtpath)
		return False
	if(not os.path.exists(sysvals.mempath)):
		if(output):
			doError('file does not exist: %s' % sysvals.mempath)
		return False
	if(not os.access(sysvals.mempath, os.R_OK)):
		if(output):
			doError('file is not readable: %s' % sysvals.mempath)
		return False

	fp = open(sysvals.fpdtpath, 'rb')
	buf = fp.read()
	fp.close()

	if(len(buf) < 36):
		if(output):
			doError('Invalid FPDT table data, should '+\
				'be at least 36 bytes')
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
	status = True

	print('Checking this system (%s)...' % platform.node())

	# check we have root access
	res = sysvals.colorText('NO (No features of this tool will work!)')
	if(sysvals.rootCheck(False)):
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

	# verify kprobes
	if sysvals.usekprobes:
		for name in sysvals.tracefuncs:
			sysvals.defaultKprobe(name, sysvals.tracefuncs[name])
		if sysvals.usedevsrc:
			for name in sysvals.dev_tracefuncs:
				sysvals.defaultKprobe(name, sysvals.dev_tracefuncs[name])
		sysvals.addKprobes(True)

	return status

# Function: doError
# Description:
#	 generic error function for catastrphic failures
# Arguments:
#	 msg: the error message to print
#	 help: True if printHelp should be called after, False otherwise
def doError(msg, help=False):
	if(help == True):
		printHelp()
	print('ERROR: %s\n') % msg
	sys.exit()

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

def processData():
	print('PROCESSING DATA')
	if(sysvals.usetraceeventsonly):
		testruns = parseTraceLog()
		if sysvals.dmesgfile:
			dmesgtext = loadKernelLog(True)
			for data in testruns:
				data.extractErrorInfo(dmesgtext)
	else:
		testruns = loadKernelLog()
		for data in testruns:
			parseKernelLog(data)
		if(sysvals.ftracefile and (sysvals.usecallgraph or sysvals.usetraceevents)):
			appendIncompleteTraceLog(testruns)
	createHTML(testruns)
	return testruns

# Function: rerunTest
# Description:
#	 generate an output from an existing set of ftrace/dmesg logs
def rerunTest():
	if sysvals.ftracefile:
		doesTraceLogHaveTraceEvents()
	if not sysvals.dmesgfile and not sysvals.usetraceeventsonly:
		doError('recreating this html output requires a dmesg file')
	sysvals.setOutputFile()
	vprint('Output file: %s' % sysvals.htmlfile)
	if os.path.exists(sysvals.htmlfile):
		if not os.path.isfile(sysvals.htmlfile):
			doError('a directory already exists with this name: %s' % sysvals.htmlfile)
		elif not os.access(sysvals.htmlfile, os.W_OK):
			doError('missing permission to write to %s' % sysvals.htmlfile)
	return processData()

# Function: runTest
# Description:
#	 execute a suspend/resume, gather the logs, and generate the output
def runTest():
	# prepare for the test
	sysvals.initFtrace()
	sysvals.initTestOutput('suspend')
	vprint('Output files:\n\t%s\n\t%s\n\t%s' % \
		(sysvals.dmesgfile, sysvals.ftracefile, sysvals.htmlfile))

	# execute the test
	executeSuspend()
	sysvals.cleanupFtrace()
	processData()

	# if running as root, change output dir owner to sudo_user
	if os.path.isdir(sysvals.testdir) and os.getuid() == 0 and \
		'SUDO_USER' in os.environ:
		cmd = 'chown -R {0}:{0} {1} > /dev/null 2>&1'
		call(cmd.format(os.environ['SUDO_USER'], sysvals.testdir), shell=True)

def find_in_html(html, strs, div=False):
	for str in strs:
		l = len(str)
		i = html.find(str)
		if i >= 0:
			break
	if i < 0:
		return ''
	if not div:
		return re.search(r'[-+]?\d*\.\d+|\d+', html[i+l:i+l+50]).group()
	n = html[i+l:].find('</div>')
	if n < 0:
		return ''
	return html[i+l:i+l+n]

# Function: runSummary
# Description:
#	 create a summary of tests in a sub-directory
def runSummary(subdir, local=True):
	inpath = os.path.abspath(subdir)
	outpath = inpath
	if local:
		outpath = os.path.abspath('.')
	print('Generating a summary of folder "%s"' % inpath)
	testruns = []
	for dirname, dirnames, filenames in os.walk(subdir):
		for filename in filenames:
			if(not re.match('.*.html', filename)):
				continue
			file = os.path.join(dirname, filename)
			html = open(file, 'r').read(10000)
			suspend = find_in_html(html,
				['Kernel Suspend: ', 'Kernel Suspend Time: '])
			resume = find_in_html(html,
				['Kernel Resume: ', 'Kernel Resume Time: '])
			line = find_in_html(html, ['<div class="stamp">'], True)
			stmp = line.split()
			if not suspend or not resume or len(stmp) < 4:
				continue
			data = {
				'host': stmp[0],
				'kernel': stmp[1],
				'mode': stmp[2],
				'time': string.join(stmp[3:], ' '),
				'suspend': suspend,
				'resume': resume,
				'url': os.path.relpath(file, outpath),
			}
			if len(stmp) == 7:
				data['kernel'] = 'unknown'
				data['mode'] = stmp[1]
				data['time'] = string.join(stmp[2:], ' ')
			testruns.append(data)
	outfile = os.path.join(outpath, 'summary.html')
	print('Summary file: %s' % outfile)
	createHTMLSummarySimple(testruns, outfile, inpath)

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
	Config = ConfigParser.ConfigParser()

	Config.read(file)
	sections = Config.sections()
	overridekprobes = False
	overridedevkprobes = False
	if 'Settings' in sections:
		for opt in Config.options('Settings'):
			value = Config.get('Settings', opt).lower()
			if(opt.lower() == 'verbose'):
				sysvals.verbose = checkArgBool(value)
			elif(opt.lower() == 'addlogs'):
				sysvals.dmesglog = sysvals.ftracelog = checkArgBool(value)
			elif(opt.lower() == 'dev'):
				sysvals.usedevsrc = checkArgBool(value)
			elif(opt.lower() == 'proc'):
				sysvals.useprocmon = checkArgBool(value)
			elif(opt.lower() == 'x2'):
				if checkArgBool(value):
					sysvals.execcount = 2
			elif(opt.lower() == 'callgraph'):
				sysvals.usecallgraph = checkArgBool(value)
			elif(opt.lower() == 'override-timeline-functions'):
				overridekprobes = checkArgBool(value)
			elif(opt.lower() == 'override-dev-timeline-functions'):
				overridedevkprobes = checkArgBool(value)
			elif(opt.lower() == 'devicefilter'):
				sysvals.setDeviceFilter(value)
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
			elif(opt.lower() == 'predelay'):
				sysvals.predelay = getArgInt('-predelay', value, 0, 60000, False)
			elif(opt.lower() == 'postdelay'):
				sysvals.postdelay = getArgInt('-postdelay', value, 0, 60000, False)
			elif(opt.lower() == 'maxdepth'):
				sysvals.max_graph_depth = getArgInt('-maxdepth', value, 0, 1000, False)
			elif(opt.lower() == 'rtcwake'):
				if value.lower() == 'off':
					sysvals.rtcwake = False
				else:
					sysvals.rtcwake = True
					sysvals.rtcwaketime = getArgInt('-rtcwake', value, 0, 3600, False)
			elif(opt.lower() == 'timeprec'):
				sysvals.setPrecision(getArgInt('-timeprec', value, 0, 6, False))
			elif(opt.lower() == 'mindev'):
				sysvals.mindevlen = getArgFloat('-mindev', value, 0.0, 10000.0, False)
			elif(opt.lower() == 'callloop-maxgap'):
				sysvals.callloopmaxgap = getArgFloat('-callloop-maxgap', value, 0.0, 1.0, False)
			elif(opt.lower() == 'callloop-maxlen'):
				sysvals.callloopmaxgap = getArgFloat('-callloop-maxlen', value, 0.0, 1.0, False)
			elif(opt.lower() == 'mincg'):
				sysvals.mincglen = getArgFloat('-mincg', value, 0.0, 10000.0, False)
			elif(opt.lower() == 'output-dir'):
				sysvals.testdir = sysvals.setOutputFolder(value)

	if sysvals.suspendmode == 'command' and not sysvals.testcommand:
		doError('No command supplied for mode "command"')

	# compatibility errors
	if sysvals.usedevsrc and sysvals.usecallgraph:
		doError('-dev is not compatible with -f')
	if sysvals.usecallgraph and sysvals.useprocmon:
		doError('-proc is not compatible with -f')

	if overridekprobes:
		sysvals.tracefuncs = dict()
	if overridedevkprobes:
		sysvals.dev_tracefuncs = dict()

	kprobes = dict()
	kprobesec = 'dev_timeline_functions_'+platform.machine()
	if kprobesec in sections:
		for name in Config.options(kprobesec):
			text = Config.get(kprobesec, name)
			kprobes[name] = (text, True)
	kprobesec = 'timeline_functions_'+platform.machine()
	if kprobesec in sections:
		for name in Config.options(kprobesec):
			if name in kprobes:
				doError('Duplicate timeline function found "%s"' % (name))
			text = Config.get(kprobesec, name)
			kprobes[name] = (text, False)

	for name in kprobes:
		function = name
		format = name
		color = ''
		args = dict()
		text, dev = kprobes[name]
		data = text.split()
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
			doError('Invalid kprobe: %s' % name)
		for arg in re.findall('{(?P<n>[a-z,A-Z,0-9]*)}', format):
			if arg not in args:
				doError('Kprobe "%s" is missing argument "%s"' % (name, arg))
		if (dev and name in sysvals.dev_tracefuncs) or (not dev and name in sysvals.tracefuncs):
			doError('Duplicate timeline function found "%s"' % (name))

		kp = {
			'name': name,
			'func': function,
			'format': format,
			sysvals.archargs: args
		}
		if color:
			kp['color'] = color
		if dev:
			sysvals.dev_tracefuncs[name] = kp
		else:
			sysvals.tracefuncs[name] = kp

# Function: printHelp
# Description:
#	 print out the help text
def printHelp():
	print('')
	print('%s v%s' % (sysvals.title, sysvals.version))
	print('Usage: sudo sleepgraph <options> <commands>')
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
	print('  If no specific command is given, the default behavior is to initiate')
	print('  a suspend/resume and capture the dmesg/ftrace output as an html timeline.')
	print('')
	print('  Generates output files in subdirectory: suspend-yymmdd-HHMMSS')
	print('   HTML output:                    <hostname>_<mode>.html')
	print('   raw dmesg output:               <hostname>_<mode>_dmesg.txt')
	print('   raw ftrace output:              <hostname>_<mode>_ftrace.txt')
	print('')
	print('Options:')
	print('   -h           Print this help text')
	print('   -v           Print the current tool version')
	print('   -config fn   Pull arguments and config options from file fn')
	print('   -verbose     Print extra information during execution and analysis')
	print('   -m mode      Mode to initiate for suspend (default: %s)') % (sysvals.suspendmode)
	print('   -o name      Overrides the output subdirectory name when running a new test')
	print('                default: suspend-{date}-{time}')
	print('   -rtcwake t   Wakeup t seconds after suspend, set t to "off" to disable (default: 15)')
	print('   -addlogs     Add the dmesg and ftrace logs to the html output')
	print('   -srgap       Add a visible gap in the timeline between sus/res (default: disabled)')
	print('  [advanced]')
	print('   -cmd {s}     Run the timeline over a custom command, e.g. "sync -d"')
	print('   -proc        Add usermode process info into the timeline (default: disabled)')
	print('   -dev         Add kernel function calls and threads to the timeline (default: disabled)')
	print('   -x2          Run two suspend/resumes back to back (default: disabled)')
	print('   -x2delay t   Include t ms delay between multiple test runs (default: 0 ms)')
	print('   -predelay t  Include t ms delay before 1st suspend (default: 0 ms)')
	print('   -postdelay t Include t ms delay after last resume (default: 0 ms)')
	print('   -mindev ms   Discard all device blocks shorter than ms milliseconds (e.g. 0.001 for us)')
	print('   -multi n d   Execute <n> consecutive tests at <d> seconds intervals. The outputs will')
	print('                be created in a new subdirectory with a summary page.')
	print('  [debug]')
	print('   -f           Use ftrace to create device callgraphs (default: disabled)')
	print('   -maxdepth N  limit the callgraph data to N call levels (default: 0=all)')
	print('   -expandcg    pre-expand the callgraph data in the html output (default: disabled)')
	print('   -fadd file   Add functions to be graphed in the timeline from a list in a text file')
	print('   -filter "d1,d2,..." Filter out all but this comma-delimited list of device names')
	print('   -mincg  ms   Discard all callgraphs shorter than ms milliseconds (e.g. 0.001 for us)')
	print('   -cgphase P   Only show callgraph data for phase P (e.g. suspend_late)')
	print('   -cgtest N    Only show callgraph data for test N (e.g. 0 or 1 in an x2 run)')
	print('   -timeprec N  Number of significant digits in timestamps (0:S, [3:ms], 6:us)')
	print('')
	print('Other commands:')
	print('   -modes       List available suspend modes')
	print('   -status      Test to see if the system is enabled to run this tool')
	print('   -fpdt        Print out the contents of the ACPI Firmware Performance Data Table')
	print('   -sysinfo     Print out system info extracted from BIOS')
	print('   -usbtopo     Print out the current USB topology with power info')
	print('   -usbauto     Enable autosuspend for all connected USB devices')
	print('   -flist       Print the list of functions currently being captured in ftrace')
	print('   -flistall    Print all functions capable of being captured in ftrace')
	print('   -summary directory  Create a summary of all test in this dir')
	print('  [redo]')
	print('   -ftrace ftracefile  Create HTML output using ftrace input (used with -dmesg)')
	print('   -dmesg dmesgfile    Create HTML output using dmesg (used with -ftrace)')
	print('')
	return True

# ----------------- MAIN --------------------
# exec start (skipped if script is loaded as library)
if __name__ == '__main__':
	cmd = ''
	outdir = ''
	multitest = {'run': False, 'count': 0, 'delay': 0}
	simplecmds = ['-sysinfo', '-modes', '-fpdt', '-flist', '-flistall', '-usbtopo', '-usbauto', '-status']
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
		elif(arg == '-x2delay'):
			sysvals.x2delay = getArgInt('-x2delay', args, 0, 60000)
		elif(arg == '-predelay'):
			sysvals.predelay = getArgInt('-predelay', args, 0, 60000)
		elif(arg == '-postdelay'):
			sysvals.postdelay = getArgInt('-postdelay', args, 0, 60000)
		elif(arg == '-f'):
			sysvals.usecallgraph = True
		elif(arg == '-addlogs'):
			sysvals.dmesglog = sysvals.ftracelog = True
		elif(arg == '-verbose'):
			sysvals.verbose = True
		elif(arg == '-proc'):
			sysvals.useprocmon = True
		elif(arg == '-dev'):
			sysvals.usedevsrc = True
		elif(arg == '-maxdepth'):
			sysvals.max_graph_depth = getArgInt('-maxdepth', args, 0, 1000)
		elif(arg == '-rtcwake'):
			try:
				val = args.next()
			except:
				doError('No rtcwake time supplied', True)
			if val.lower() == 'off':
				sysvals.rtcwake = False
			else:
				sysvals.rtcwake = True
				sysvals.rtcwaketime = getArgInt('-rtcwake', val, 0, 3600, False)
		elif(arg == '-timeprec'):
			sysvals.setPrecision(getArgInt('-timeprec', args, 0, 6))
		elif(arg == '-mindev'):
			sysvals.mindevlen = getArgFloat('-mindev', args, 0.0, 10000.0)
		elif(arg == '-mincg'):
			sysvals.mincglen = getArgFloat('-mincg', args, 0.0, 10000.0)
		elif(arg == '-cgtest'):
			sysvals.cgtest = getArgInt('-cgtest', args, 0, 1)
		elif(arg == '-cgphase'):
			try:
				val = args.next()
			except:
				doError('No phase name supplied', True)
			d = Data(0)
			if val not in d.phases:
				doError('Invalid phase, valid phaess are %s' % d.phases, True)
			sysvals.cgphase = val
		elif(arg == '-callloop-maxgap'):
			sysvals.callloopmaxgap = getArgFloat('-callloop-maxgap', args, 0.0, 1.0)
		elif(arg == '-callloop-maxlen'):
			sysvals.callloopmaxlen = getArgFloat('-callloop-maxlen', args, 0.0, 1.0)
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
			outdir = sysvals.setOutputFolder(val)
		elif(arg == '-config'):
			try:
				val = args.next()
			except:
				doError('No text file supplied', True)
			if(os.path.exists(val) == False):
				doError('%s does not exist' % val)
			configFromFile(val)
		elif(arg == '-fadd'):
			try:
				val = args.next()
			except:
				doError('No text file supplied', True)
			if(os.path.exists(val) == False):
				doError('%s does not exist' % val)
			sysvals.addFtraceFilterFunctions(val)
		elif(arg == '-dmesg'):
			try:
				val = args.next()
			except:
				doError('No dmesg file supplied', True)
			sysvals.notestrun = True
			sysvals.dmesgfile = val
			if(os.path.exists(sysvals.dmesgfile) == False):
				doError('%s does not exist' % sysvals.dmesgfile)
		elif(arg == '-ftrace'):
			try:
				val = args.next()
			except:
				doError('No ftrace file supplied', True)
			sysvals.notestrun = True
			sysvals.ftracefile = val
			if(os.path.exists(sysvals.ftracefile) == False):
				doError('%s does not exist' % sysvals.ftracefile)
		elif(arg == '-summary'):
			try:
				val = args.next()
			except:
				doError('No directory supplied', True)
			cmd = 'summary'
			outdir = val
			sysvals.notestrun = True
			if(os.path.isdir(val) == False):
				doError('%s is not accesible' % val)
		elif(arg == '-filter'):
			try:
				val = args.next()
			except:
				doError('No devnames supplied', True)
			sysvals.setDeviceFilter(val)
		else:
			doError('Invalid argument: '+arg, True)

	# compatibility errors
	if(sysvals.usecallgraph and sysvals.usedevsrc):
		doError('-dev is not compatible with -f')
	if(sysvals.usecallgraph and sysvals.useprocmon):
		doError('-proc is not compatible with -f')

	# callgraph size cannot exceed device size
	if sysvals.mincglen < sysvals.mindevlen:
		sysvals.mincglen = sysvals.mindevlen

	# just run a utility command and exit
	sysvals.cpuInfo()
	if(cmd != ''):
		if(cmd == 'status'):
			statusCheck(True)
		elif(cmd == 'fpdt'):
			getFPDT(True)
		elif(cmd == 'sysinfo'):
			sysvals.printSystemInfo()
		elif(cmd == 'usbtopo'):
			detectUSB()
		elif(cmd == 'modes'):
			print getModes()
		elif(cmd == 'flist'):
			sysvals.getFtraceFilterFunctions(True)
		elif(cmd == 'flistall'):
			sysvals.getFtraceFilterFunctions(False)
		elif(cmd == 'usbauto'):
			setUSBDevicesAuto()
		elif(cmd == 'summary'):
			runSummary(outdir, True)
		sys.exit()

	# if instructed, re-analyze existing data files
	if(sysvals.notestrun):
		rerunTest()
		sys.exit()

	# verify that we can run a test
	if(not statusCheck()):
		print('Check FAILED, aborting the test run!')
		sys.exit()

	# extract mem modes and convert
	mode = sysvals.suspendmode
	if 'mem' == mode[:3]:
		if '-' in mode:
			memmode = mode.split('-')[-1]
		else:
			memmode = 'deep'
		if memmode == 'shallow':
			mode = 'standby'
		elif memmode ==  's2idle':
			mode = 'freeze'
		else:
			mode = 'mem'
		sysvals.memmode = memmode
		sysvals.suspendmode = mode

	sysvals.systemInfo(dmidecode(sysvals.mempath))

	if multitest['run']:
		# run multiple tests in a separate subdirectory
		if not outdir:
			s = 'suspend-x%d' % multitest['count']
			outdir = datetime.now().strftime(s+'-%y%m%d-%H%M%S')
		if not os.path.isdir(outdir):
			os.mkdir(outdir)
		for i in range(multitest['count']):
			if(i != 0):
				print('Waiting %d seconds...' % (multitest['delay']))
				time.sleep(multitest['delay'])
			print('TEST (%d/%d) START' % (i+1, multitest['count']))
			fmt = 'suspend-%y%m%d-%H%M%S'
			sysvals.testdir = os.path.join(outdir, datetime.now().strftime(fmt))
			runTest()
			print('TEST (%d/%d) COMPLETE' % (i+1, multitest['count']))
		runSummary(outdir, False)
	else:
		if outdir:
			sysvals.testdir = outdir
		# run the test in the current directory
		runTest()
