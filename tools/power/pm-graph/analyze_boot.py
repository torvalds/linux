#!/usr/bin/python
#
# Tool for analyzing boot timing
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
# Description:
#	 This tool is designed to assist kernel and OS developers in optimizing
#	 their linux stack's boot time. It creates an html representation of
#	 the kernel boot timeline up to the start of the init process.
#

# ----------------- LIBRARIES --------------------

import sys
import time
import os
import string
import re
import platform
import shutil
from datetime import datetime, timedelta
from subprocess import call, Popen, PIPE
import analyze_suspend as aslib

# ----------------- CLASSES --------------------

# Class: SystemValues
# Description:
#	 A global, single-instance container used to
#	 store system values and test parameters
class SystemValues(aslib.SystemValues):
	title = 'BootGraph'
	version = 2.0
	hostname = 'localhost'
	testtime = ''
	kernel = ''
	dmesgfile = ''
	ftracefile = ''
	htmlfile = 'bootgraph.html'
	outfile = ''
	phoronix = False
	addlogs = False
	useftrace = False
	usedevsrc = True
	suspendmode = 'boot'
	max_graph_depth = 2
	graph_filter = 'do_one_initcall'
	reboot = False
	manual = False
	iscronjob = False
	timeformat = '%.6f'
	def __init__(self):
		if('LOG_FILE' in os.environ and 'TEST_RESULTS_IDENTIFIER' in os.environ):
			self.phoronix = True
			self.addlogs = True
			self.outfile = os.environ['LOG_FILE']
			self.htmlfile = os.environ['LOG_FILE']
		self.hostname = platform.node()
		self.testtime = datetime.now().strftime('%Y-%m-%d_%H:%M:%S')
		if os.path.exists('/proc/version'):
			fp = open('/proc/version', 'r')
			val = fp.read().strip()
			fp.close()
			self.kernel = self.kernelVersion(val)
		else:
			self.kernel = 'unknown'
	def kernelVersion(self, msg):
		return msg.split()[2]
	def kernelParams(self):
		cmdline = 'initcall_debug log_buf_len=32M'
		if self.useftrace:
			cmdline += ' trace_buf_size=128M trace_clock=global '\
			'trace_options=nooverwrite,funcgraph-abstime,funcgraph-cpu,'\
			'funcgraph-duration,funcgraph-proc,funcgraph-tail,'\
			'nofuncgraph-overhead,context-info,graph-time '\
			'ftrace=function_graph '\
			'ftrace_graph_max_depth=%d '\
			'ftrace_graph_filter=%s' % \
				(self.max_graph_depth, self.graph_filter)
		return cmdline
	def setGraphFilter(self, val):
		fp = open(self.tpath+'available_filter_functions')
		master = fp.read().split('\n')
		fp.close()
		for i in val.split(','):
			func = i.strip()
			if func not in master:
				doError('function "%s" not available for ftrace' % func)
		self.graph_filter = val
	def cronjobCmdString(self):
		cmdline = '%s -cronjob' % os.path.abspath(sys.argv[0])
		args = iter(sys.argv[1:])
		for arg in args:
			if arg in ['-h', '-v', '-cronjob', '-reboot']:
				continue
			elif arg in ['-o', '-dmesg', '-ftrace', '-filter']:
				args.next()
				continue
			cmdline += ' '+arg
		if self.graph_filter != 'do_one_initcall':
			cmdline += ' -filter "%s"' % self.graph_filter
		cmdline += ' -o "%s"' % os.path.abspath(self.htmlfile)
		return cmdline
	def manualRebootRequired(self):
		cmdline = self.kernelParams()
		print 'To generate a new timeline manually, follow these steps:\n'
		print '1. Add the CMDLINE string to your kernel command line.'
		print '2. Reboot the system.'
		print '3. After reboot, re-run this tool with the same arguments but no command (w/o -reboot or -manual).\n'
		print 'CMDLINE="%s"' % cmdline
		sys.exit()

sysvals = SystemValues()

# Class: Data
# Description:
#	 The primary container for test data.
class Data(aslib.Data):
	dmesg = {}  # root data structure
	start = 0.0 # test start
	end = 0.0   # test end
	dmesgtext = []   # dmesg text file in memory
	testnumber = 0
	idstr = ''
	html_device_id = 0
	valid = False
	initstart = 0.0
	boottime = ''
	phases = ['boot']
	do_one_initcall = False
	def __init__(self, num):
		self.testnumber = num
		self.idstr = 'a'
		self.dmesgtext = []
		self.dmesg = {
			'boot': {'list': dict(), 'start': -1.0, 'end': -1.0, 'row': 0, 'color': '#dddddd'}
		}
	def deviceTopology(self):
		return ''
	def newAction(self, phase, name, start, end, ret, ulen):
		# new device callback for a specific phase
		self.html_device_id += 1
		devid = '%s%d' % (self.idstr, self.html_device_id)
		list = self.dmesg[phase]['list']
		length = -1.0
		if(start >= 0 and end >= 0):
			length = end - start
		i = 2
		origname = name
		while(name in list):
			name = '%s[%d]' % (origname, i)
			i += 1
		list[name] = {'name': name, 'start': start, 'end': end,
			'pid': 0, 'length': length, 'row': 0, 'id': devid,
			'ret': ret, 'ulen': ulen }
		return name
	def deviceMatch(self, cg):
		if cg.end - cg.start == 0:
			return True
		list = self.dmesg['boot']['list']
		for devname in list:
			dev = list[devname]
			if cg.name == 'do_one_initcall':
				if(cg.start <= dev['start'] and cg.end >= dev['end'] and dev['length'] > 0):
					dev['ftrace'] = cg
					self.do_one_initcall = True
					return True
			else:
				if(cg.start > dev['start'] and cg.end < dev['end']):
					if 'ftraces' not in dev:
						dev['ftraces'] = []
					dev['ftraces'].append(cg)
					return True
		return False

# ----------------- FUNCTIONS --------------------

# Function: loadKernelLog
# Description:
#	 Load a raw kernel log from dmesg
def loadKernelLog():
	data = Data(0)
	data.dmesg['boot']['start'] = data.start = ktime = 0.0
	sysvals.stamp = {
		'time': datetime.now().strftime('%B %d %Y, %I:%M:%S %p'),
		'host': sysvals.hostname,
		'mode': 'boot', 'kernel': ''}

	devtemp = dict()
	if(sysvals.dmesgfile):
		lf = open(sysvals.dmesgfile, 'r')
	else:
		lf = Popen('dmesg', stdout=PIPE).stdout
	for line in lf:
		line = line.replace('\r\n', '')
		idx = line.find('[')
		if idx > 1:
			line = line[idx:]
		m = re.match('[ \t]*(\[ *)(?P<ktime>[0-9\.]*)(\]) (?P<msg>.*)', line)
		if(not m):
			continue
		ktime = float(m.group('ktime'))
		if(ktime > 120):
			break
		msg = m.group('msg')
		data.end = data.initstart = ktime
		data.dmesgtext.append(line)
		if(ktime == 0.0 and re.match('^Linux version .*', msg)):
			if(not sysvals.stamp['kernel']):
				sysvals.stamp['kernel'] = sysvals.kernelVersion(msg)
			continue
		m = re.match('.* setting system clock to (?P<t>.*) UTC.*', msg)
		if(m):
			bt = datetime.strptime(m.group('t'), '%Y-%m-%d %H:%M:%S')
			bt = bt - timedelta(seconds=int(ktime))
			data.boottime = bt.strftime('%Y-%m-%d_%H:%M:%S')
			sysvals.stamp['time'] = bt.strftime('%B %d %Y, %I:%M:%S %p')
			continue
		m = re.match('^calling *(?P<f>.*)\+.*', msg)
		if(m):
			devtemp[m.group('f')] = ktime
			continue
		m = re.match('^initcall *(?P<f>.*)\+.* returned (?P<r>.*) after (?P<t>.*) usecs', msg)
		if(m):
			data.valid = True
			f, r, t = m.group('f', 'r', 't')
			if(f in devtemp):
				data.newAction('boot', f, devtemp[f], ktime, int(r), int(t))
				data.end = ktime
				del devtemp[f]
			continue
		if(re.match('^Freeing unused kernel memory.*', msg)):
			break

	data.dmesg['boot']['end'] = data.end
	lf.close()
	return data

# Function: loadTraceLog
# Description:
#	 Check if trace is available and copy to a temp file
def loadTraceLog(data):
	# load the data to a temp file if none given
	if not sysvals.ftracefile:
		lib = aslib.sysvals
		aslib.rootCheck(True)
		if not lib.verifyFtrace():
			doError('ftrace not available')
		if lib.fgetVal('current_tracer').strip() != 'function_graph':
			doError('ftrace not configured for a boot callgraph')
		sysvals.ftracefile = '/tmp/boot_ftrace.%s.txt' % os.getpid()
		call('cat '+lib.tpath+'trace > '+sysvals.ftracefile, shell=True)
	if not sysvals.ftracefile:
		doError('No trace data available')

	# parse the trace log
	ftemp = dict()
	tp = aslib.TestProps()
	tp.setTracerType('function_graph')
	tf = open(sysvals.ftracefile, 'r')
	for line in tf:
		if line[0] == '#':
			continue
		m = re.match(tp.ftrace_line_fmt, line.strip())
		if(not m):
			continue
		m_time, m_proc, m_pid, m_msg, m_dur = \
			m.group('time', 'proc', 'pid', 'msg', 'dur')
		if float(m_time) > data.end:
			break
		if(m_time and m_pid and m_msg):
			t = aslib.FTraceLine(m_time, m_msg, m_dur)
			pid = int(m_pid)
		else:
			continue
		if t.fevent or t.fkprobe:
			continue
		key = (m_proc, pid)
		if(key not in ftemp):
			ftemp[key] = []
			ftemp[key].append(aslib.FTraceCallGraph(pid))
		cg = ftemp[key][-1]
		if(cg.addLine(t)):
			ftemp[key].append(aslib.FTraceCallGraph(pid))
	tf.close()

	# add the callgraph data to the device hierarchy
	for key in ftemp:
		proc, pid = key
		for cg in ftemp[key]:
			if len(cg.list) < 1 or cg.invalid:
				continue
			if(not cg.postProcess()):
				print('Sanity check failed for %s-%d' % (proc, pid))
				continue
			# match cg data to devices
			if not data.deviceMatch(cg):
				print ' BAD: %s %s-%d [%f - %f]' % (cg.name, proc, pid, cg.start, cg.end)

# Function: colorForName
# Description:
#	 Generate a repeatable color from a list for a given name
def colorForName(name):
	list = [
		('c1', '#ec9999'),
		('c2', '#ffc1a6'),
		('c3', '#fff0a6'),
		('c4', '#adf199'),
		('c5', '#9fadea'),
		('c6', '#a699c1'),
		('c7', '#ad99b4'),
		('c8', '#eaffea'),
		('c9', '#dcecfb'),
		('c10', '#ffffea')
	]
	i = 0
	total = 0
	count = len(list)
	while i < len(name):
		total += ord(name[i])
		i += 1
	return list[total % count]

def cgOverview(cg, minlen):
	stats = dict()
	large = []
	for l in cg.list:
		if l.fcall and l.depth == 1:
			if l.length >= minlen:
				large.append(l)
			if l.name not in stats:
				stats[l.name] = [0, 0.0]
			stats[l.name][0] += (l.length * 1000.0)
			stats[l.name][1] += 1
	return (large, stats)

# Function: createBootGraph
# Description:
#	 Create the output html file from the resident test data
# Arguments:
#	 testruns: array of Data objects from parseKernelLog or parseTraceLog
# Output:
#	 True if the html file was created, false if it failed
def createBootGraph(data, embedded):
	# html function templates
	html_srccall = '<div id={6} title="{5}" class="srccall" style="left:{1}%;top:{2}px;height:{3}px;width:{4}%;line-height:{3}px;">{0}</div>\n'
	html_timetotal = '<table class="time1">\n<tr>'\
		'<td class="blue">Time from Kernel Boot to start of User Mode: <b>{0} ms</b></td>'\
		'</tr>\n</table>\n'

	# device timeline
	devtl = aslib.Timeline(100, 20)

	# write the test title and general info header
	devtl.createHeader(sysvals, 'noftrace')

	# Generate the header for this timeline
	t0 = data.start
	tMax = data.end
	tTotal = tMax - t0
	if(tTotal == 0):
		print('ERROR: No timeline data')
		return False
	boot_time = '%.0f'%(tTotal*1000)
	devtl.html += html_timetotal.format(boot_time)

	# determine the maximum number of rows we need to draw
	phase = 'boot'
	list = data.dmesg[phase]['list']
	devlist = []
	for devname in list:
		d = aslib.DevItem(0, phase, list[devname])
		devlist.append(d)
	devtl.getPhaseRows(devlist)
	devtl.calcTotalRows()

	# draw the timeline background
	devtl.createZoomBox()
	boot = data.dmesg[phase]
	length = boot['end']-boot['start']
	left = '%.3f' % (((boot['start']-t0)*100.0)/tTotal)
	width = '%.3f' % ((length*100.0)/tTotal)
	devtl.html += devtl.html_tblock.format(phase, left, width, devtl.scaleH)
	devtl.html += devtl.html_phase.format('0', '100', \
		'%.3f'%devtl.scaleH, '%.3f'%devtl.bodyH, \
		'white', '')

	# draw the device timeline
	num = 0
	devstats = dict()
	for devname in sorted(list):
		cls, color = colorForName(devname)
		dev = list[devname]
		info = '@|%.3f|%.3f|%.3f|%d' % (dev['start']*1000.0, dev['end']*1000.0,
			dev['ulen']/1000.0, dev['ret'])
		devstats[dev['id']] = {'info':info}
		dev['color'] = color
		height = devtl.phaseRowHeight(0, phase, dev['row'])
		top = '%.6f' % ((dev['row']*height) + devtl.scaleH)
		left = '%.6f' % (((dev['start']-t0)*100)/tTotal)
		width = '%.6f' % (((dev['end']-dev['start'])*100)/tTotal)
		length = ' (%0.3f ms) ' % ((dev['end']-dev['start'])*1000)
		devtl.html += devtl.html_device.format(dev['id'],
			devname+length+'kernel_mode', left, top, '%.3f'%height,
			width, devname, ' '+cls, '')
		rowtop = devtl.phaseRowTop(0, phase, dev['row'])
		height = '%.6f' % (devtl.rowH / 2)
		top = '%.6f' % (rowtop + devtl.scaleH + (devtl.rowH / 2))
		if data.do_one_initcall:
			if('ftrace' not in dev):
				continue
			cg = dev['ftrace']
			large, stats = cgOverview(cg, 0.001)
			devstats[dev['id']]['fstat'] = stats
			for l in large:
				left = '%f' % (((l.time-t0)*100)/tTotal)
				width = '%f' % (l.length*100/tTotal)
				title = '%s (%0.3fms)' % (l.name, l.length * 1000.0)
				devtl.html += html_srccall.format(l.name, left,
					top, height, width, title, 'x%d'%num)
				num += 1
			continue
		if('ftraces' not in dev):
			continue
		for cg in dev['ftraces']:
			left = '%f' % (((cg.start-t0)*100)/tTotal)
			width = '%f' % ((cg.end-cg.start)*100/tTotal)
			cglen = (cg.end - cg.start) * 1000.0
			title = '%s (%0.3fms)' % (cg.name, cglen)
			cg.id = 'x%d' % num
			devtl.html += html_srccall.format(cg.name, left,
				top, height, width, title, dev['id']+cg.id)
			num += 1

	# draw the time scale, try to make the number of labels readable
	devtl.createTimeScale(t0, tMax, tTotal, phase)
	devtl.html += '</div>\n'

	# timeline is finished
	devtl.html += '</div>\n</div>\n'

	if(sysvals.outfile == sysvals.htmlfile):
		hf = open(sysvals.htmlfile, 'a')
	else:
		hf = open(sysvals.htmlfile, 'w')

	# add the css if this is not an embedded run
	extra = '\
		.c1 {background:rgba(209,0,0,0.4);}\n\
		.c2 {background:rgba(255,102,34,0.4);}\n\
		.c3 {background:rgba(255,218,33,0.4);}\n\
		.c4 {background:rgba(51,221,0,0.4);}\n\
		.c5 {background:rgba(17,51,204,0.4);}\n\
		.c6 {background:rgba(34,0,102,0.4);}\n\
		.c7 {background:rgba(51,0,68,0.4);}\n\
		.c8 {background:rgba(204,255,204,0.4);}\n\
		.c9 {background:rgba(169,208,245,0.4);}\n\
		.c10 {background:rgba(255,255,204,0.4);}\n\
		.vt {transform:rotate(-60deg);transform-origin:0 0;}\n\
		table.fstat {table-layout:fixed;padding:150px 15px 0 0;font-size:10px;column-width:30px;}\n\
		.fstat th {width:55px;}\n\
		.fstat td {text-align:left;width:35px;}\n\
		.srccall {position:absolute;font-size:10px;z-index:7;overflow:hidden;color:black;text-align:center;white-space:nowrap;border-radius:5px;border:1px solid black;background:linear-gradient(to bottom right,#CCC,#969696);}\n\
		.srccall:hover {color:white;font-weight:bold;border:1px solid white;}\n'
	if(not embedded):
		aslib.addCSS(hf, sysvals, 1, False, extra)

	# write the device timeline
	hf.write(devtl.html)

	# add boot specific html
	statinfo = 'var devstats = {\n'
	for n in sorted(devstats):
		statinfo += '\t"%s": [\n\t\t"%s",\n' % (n, devstats[n]['info'])
		if 'fstat' in devstats[n]:
			funcs = devstats[n]['fstat']
			for f in sorted(funcs, key=funcs.get, reverse=True):
				if funcs[f][0] < 0.01 and len(funcs) > 10:
					break
				statinfo += '\t\t"%f|%s|%d",\n' % (funcs[f][0], f, funcs[f][1])
		statinfo += '\t],\n'
	statinfo += '};\n'
	html = \
		'<div id="devicedetailtitle"></div>\n'\
		'<div id="devicedetail" style="display:none;">\n'\
		'<div id="devicedetail0">\n'\
		'<div id="kernel_mode" class="phaselet" style="left:0%;width:100%;background:#DDDDDD"></div>\n'\
		'</div>\n</div>\n'\
		'<script type="text/javascript">\n'+statinfo+\
		'</script>\n'
	hf.write(html)

	# add the callgraph html
	if(sysvals.usecallgraph):
		aslib.addCallgraphs(sysvals, hf, data)

	# add the dmesg log as a hidden div
	if sysvals.addlogs:
		hf.write('<div id="dmesglog" style="display:none;">\n')
		for line in data.dmesgtext:
			line = line.replace('<', '&lt').replace('>', '&gt')
			hf.write(line)
		hf.write('</div>\n')

	if(not embedded):
		# write the footer and close
		aslib.addScriptCode(hf, [data])
		hf.write('</body>\n</html>\n')
	else:
		# embedded out will be loaded in a page, skip the js
		hf.write('<div id=bounds style=display:none>%f,%f</div>' % \
			(data.start*1000, data.initstart*1000))
	hf.close()
	return True

# Function: updateCron
# Description:
#    (restore=False) Set the tool to run automatically on reboot
#    (restore=True) Restore the original crontab
def updateCron(restore=False):
	if not restore:
		sysvals.rootUser(True)
	crondir = '/var/spool/cron/crontabs/'
	cronfile = crondir+'root'
	backfile = crondir+'root-analyze_boot-backup'
	if not os.path.exists(crondir):
		doError('%s not found' % crondir)
	out = Popen(['which', 'crontab'], stdout=PIPE).stdout.read()
	if not out:
		doError('crontab not found')
	# on restore: move the backup cron back into place
	if restore:
		if os.path.exists(backfile):
			shutil.move(backfile, cronfile)
		return
	# backup current cron and install new one with reboot
	if os.path.exists(cronfile):
		shutil.move(cronfile, backfile)
	else:
		fp = open(backfile, 'w')
		fp.close()
	res = -1
	try:
		fp = open(backfile, 'r')
		op = open(cronfile, 'w')
		for line in fp:
			if '@reboot' not in line:
				op.write(line)
				continue
		fp.close()
		op.write('@reboot python %s\n' % sysvals.cronjobCmdString())
		op.close()
		res = call('crontab %s' % cronfile, shell=True)
	except Exception, e:
		print 'Exception: %s' % str(e)
		shutil.move(backfile, cronfile)
		res = -1
	if res != 0:
		doError('crontab failed')

# Function: updateGrub
# Description:
#	 update grub.cfg for all kernels with our parameters
def updateGrub(restore=False):
	# call update-grub on restore
	if restore:
		try:
			call(['update-grub'], stderr=PIPE, stdout=PIPE,
				env={'PATH': '.:/sbin:/usr/sbin:/usr/bin:/sbin:/bin'})
		except Exception, e:
			print 'Exception: %s\n' % str(e)
		return
	# verify we can do this
	sysvals.rootUser(True)
	grubfile = '/etc/default/grub'
	if not os.path.exists(grubfile):
		print 'ERROR: Unable to set the kernel parameters via grub.\n'
		sysvals.manualRebootRequired()
	out = Popen(['which', 'update-grub'], stdout=PIPE).stdout.read()
	if not out:
		print 'ERROR: Unable to set the kernel parameters via grub.\n'
		sysvals.manualRebootRequired()

	# extract the option and create a grub config without it
	tgtopt = 'GRUB_CMDLINE_LINUX_DEFAULT'
	cmdline = ''
	tempfile = '/etc/default/grub.analyze_boot'
	shutil.move(grubfile, tempfile)
	res = -1
	try:
		fp = open(tempfile, 'r')
		op = open(grubfile, 'w')
		cont = False
		for line in fp:
			line = line.strip()
			if len(line) == 0 or line[0] == '#':
				continue
			opt = line.split('=')[0].strip()
			if opt == tgtopt:
				cmdline = line.split('=', 1)[1].strip('\\')
				if line[-1] == '\\':
					cont = True
			elif cont:
				cmdline += line.strip('\\')
				if line[-1] != '\\':
					cont = False
			else:
				op.write('%s\n' % line)
		fp.close()
		# if the target option value is in quotes, strip them
		sp = '"'
		val = cmdline.strip()
		if val[0] == '\'' or val[0] == '"':
			sp = val[0]
			val = val.strip(sp)
		cmdline = val
		# append our cmd line options
		if len(cmdline) > 0:
			cmdline += ' '
		cmdline += sysvals.kernelParams()
		# write out the updated target option
		op.write('\n%s=%s%s%s\n' % (tgtopt, sp, cmdline, sp))
		op.close()
		res = call('update-grub')
		os.remove(grubfile)
	except Exception, e:
		print 'Exception: %s' % str(e)
		res = -1
	# cleanup
	shutil.move(tempfile, grubfile)
	if res != 0:
		doError('update-grub failed')

# Function: doError
# Description:
#	 generic error function for catastrphic failures
# Arguments:
#	 msg: the error message to print
#	 help: True if printHelp should be called after, False otherwise
def doError(msg, help=False):
	if help == True:
		printHelp()
	print 'ERROR: %s\n' % msg
	sys.exit()

# Function: printHelp
# Description:
#	 print out the help text
def printHelp():
	print('')
	print('%s v%.1f' % (sysvals.title, sysvals.version))
	print('Usage: bootgraph <options> <command>')
	print('')
	print('Description:')
	print('  This tool reads in a dmesg log of linux kernel boot and')
	print('  creates an html representation of the boot timeline up to')
	print('  the start of the init process.')
	print('')
	print('  If no specific command is given the tool reads the current dmesg')
	print('  and/or ftrace log and outputs bootgraph.html')
	print('')
	print('Options:')
	print('  -h            Print this help text')
	print('  -v            Print the current tool version')
	print('  -addlogs      Add the dmesg log to the html output')
	print('  -o file       Html timeline name (default: bootgraph.html)')
	print(' [advanced]')
	print('  -f            Use ftrace to add function detail (default: disabled)')
	print('  -callgraph    Add callgraph detail, can be very large (default: disabled)')
	print('  -maxdepth N   limit the callgraph data to N call levels (default: 2)')
	print('  -mincg ms     Discard all callgraphs shorter than ms milliseconds (e.g. 0.001 for us)')
	print('  -timeprec N   Number of significant digits in timestamps (0:S, 3:ms, [6:us])')
	print('  -expandcg     pre-expand the callgraph data in the html output (default: disabled)')
	print('  -filter list  Limit ftrace to comma-delimited list of functions (default: do_one_initcall)')
	print(' [commands]')
	print('  -reboot       Reboot the machine automatically and generate a new timeline')
	print('  -manual       Show the requirements to generate a new timeline manually')
	print('  -dmesg file   Load a stored dmesg file (used with -ftrace)')
	print('  -ftrace file  Load a stored ftrace file (used with -dmesg)')
	print('  -flistall     Print all functions capable of being captured in ftrace')
	print('')
	return True

# ----------------- MAIN --------------------
# exec start (skipped if script is loaded as library)
if __name__ == '__main__':
	# loop through the command line arguments
	cmd = ''
	simplecmds = ['-updategrub', '-flistall']
	args = iter(sys.argv[1:])
	for arg in args:
		if(arg == '-h'):
			printHelp()
			sys.exit()
		elif(arg == '-v'):
			print("Version %.1f" % sysvals.version)
			sys.exit()
		elif(arg in simplecmds):
			cmd = arg[1:]
		elif(arg == '-f'):
			sysvals.useftrace = True
		elif(arg == '-callgraph'):
			sysvals.useftrace = True
			sysvals.usecallgraph = True
		elif(arg == '-mincg'):
			sysvals.mincglen = aslib.getArgFloat('-mincg', args, 0.0, 10000.0)
		elif(arg == '-timeprec'):
			sysvals.setPrecision(aslib.getArgInt('-timeprec', args, 0, 6))
		elif(arg == '-maxdepth'):
			sysvals.max_graph_depth = aslib.getArgInt('-maxdepth', args, 0, 1000)
		elif(arg == '-filter'):
			try:
				val = args.next()
			except:
				doError('No filter functions supplied', True)
			aslib.rootCheck(True)
			sysvals.setGraphFilter(val)
		elif(arg == '-ftrace'):
			try:
				val = args.next()
			except:
				doError('No ftrace file supplied', True)
			if(os.path.exists(val) == False):
				doError('%s does not exist' % val)
			sysvals.ftracefile = val
		elif(arg == '-addlogs'):
			sysvals.addlogs = True
		elif(arg == '-expandcg'):
			sysvals.cgexp = True
		elif(arg == '-dmesg'):
			try:
				val = args.next()
			except:
				doError('No dmesg file supplied', True)
			if(os.path.exists(val) == False):
				doError('%s does not exist' % val)
			if(sysvals.htmlfile == val or sysvals.outfile == val):
				doError('Output filename collision')
			sysvals.dmesgfile = val
		elif(arg == '-o'):
			try:
				val = args.next()
			except:
				doError('No HTML filename supplied', True)
			if(sysvals.dmesgfile == val or sysvals.ftracefile == val):
				doError('Output filename collision')
			sysvals.htmlfile = val
		elif(arg == '-reboot'):
			if sysvals.iscronjob:
				doError('-reboot and -cronjob are incompatible')
			sysvals.reboot = True
		elif(arg == '-manual'):
			sysvals.reboot = True
			sysvals.manual = True
		# remaining options are only for cron job use
		elif(arg == '-cronjob'):
			sysvals.iscronjob = True
			if sysvals.reboot:
				doError('-reboot and -cronjob are incompatible')
		else:
			doError('Invalid argument: '+arg, True)

	if cmd != '':
		if cmd == 'updategrub':
			updateGrub()
		elif cmd == 'flistall':
			sysvals.getFtraceFilterFunctions(False)
		sys.exit()

	# update grub, setup a cronjob, and reboot
	if sysvals.reboot:
		if not sysvals.manual:
			updateGrub()
			updateCron()
			call('reboot')
		else:
			sysvals.manualRebootRequired()
		sys.exit()

	# disable the cronjob
	if sysvals.iscronjob:
		updateCron(True)
		updateGrub(True)

	data = loadKernelLog()
	if sysvals.useftrace:
		loadTraceLog(data)
		if sysvals.iscronjob:
			try:
				sysvals.fsetVal('0', 'tracing_on')
			except:
				pass

	if(sysvals.outfile and sysvals.phoronix):
		fp = open(sysvals.outfile, 'w')
		fp.write('pass %s initstart %.3f end %.3f boot %s\n' %
			(data.valid, data.initstart*1000, data.end*1000, data.boottime))
		fp.close()
	if(not data.valid):
		if sysvals.dmesgfile:
			doError('No initcall data found in %s' % sysvals.dmesgfile)
		else:
			doError('No initcall data found, is initcall_debug enabled?')

	print('          Host: %s' % sysvals.hostname)
	print('     Test time: %s' % sysvals.testtime)
	print('     Boot time: %s' % data.boottime)
	print('Kernel Version: %s' % sysvals.kernel)
	print('  Kernel start: %.3f' % (data.start * 1000))
	print('    init start: %.3f' % (data.initstart * 1000))

	createBootGraph(data, sysvals.phoronix)
