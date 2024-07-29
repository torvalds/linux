#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
from shutil import which
from os import pread

class PerfCounterInfo:
	def __init__(self, subsys, event):
		self.subsys = subsys
		self.event = event

	def get_perf_event_name(self):
		return f'{self.subsys}/{self.event}/'

	def get_turbostat_perf_id(self, counter_scope, counter_type, column_name):
		return f'perf/{self.subsys}/{self.event},{counter_scope},{counter_type},{column_name}'

PERF_COUNTERS_CANDIDATES = [
	PerfCounterInfo('msr', 'mperf'),
	PerfCounterInfo('msr', 'aperf'),
	PerfCounterInfo('msr', 'tsc'),
	PerfCounterInfo('cstate_core', 'c1-residency'),
	PerfCounterInfo('cstate_core', 'c6-residency'),
	PerfCounterInfo('cstate_core', 'c7-residency'),
	PerfCounterInfo('cstate_pkg', 'c2-residency'),
	PerfCounterInfo('cstate_pkg', 'c3-residency'),
	PerfCounterInfo('cstate_pkg', 'c6-residency'),
	PerfCounterInfo('cstate_pkg', 'c7-residency'),
	PerfCounterInfo('cstate_pkg', 'c8-residency'),
	PerfCounterInfo('cstate_pkg', 'c9-residency'),
	PerfCounterInfo('cstate_pkg', 'c10-residency'),
]
present_perf_counters = []

def check_perf_access():
	perf = which('perf')
	if perf is None:
		print('SKIP: Could not find perf binary, thus could not determine perf access.')
		return False

	def has_perf_counter_access(counter_name):
		proc_perf = subprocess.run([perf, 'stat', '-e', counter_name, '--timeout', '10'],
							 capture_output = True)

		if proc_perf.returncode != 0:
			print(f'SKIP: Could not read {counter_name} perf counter.')
			return False

		if b'<not supported>' in proc_perf.stderr:
			print(f'SKIP: Could not read {counter_name} perf counter.')
			return False

		return True

	for counter in PERF_COUNTERS_CANDIDATES:
		if has_perf_counter_access(counter.get_perf_event_name()):
			present_perf_counters.append(counter)

	if len(present_perf_counters) == 0:
		print('SKIP: Could not read any perf counter.')
		return False

	if len(present_perf_counters) != len(PERF_COUNTERS_CANDIDATES):
		print(f'WARN: Could not access all of the counters - some will be left untested')

	return True

if not check_perf_access():
	exit(0)

turbostat_counter_source_opts = ['']

turbostat = which('turbostat')
if turbostat is None:
	print('Could not find turbostat binary')
	exit(1)

timeout = which('timeout')
if timeout is None:
	print('Could not find timeout binary')
	exit(1)

proc_turbostat = subprocess.run([turbostat, '--list'], capture_output = True)
if proc_turbostat.returncode != 0:
	print(f'turbostat failed with {proc_turbostat.returncode}')
	exit(1)

EXPECTED_COLUMNS_DEBUG_DEFAULT = [b'usec', b'Time_Of_Day_Seconds', b'APIC', b'X2APIC']

expected_columns = [b'CPU']
counters_argv = []
for counter in present_perf_counters:
	if counter.subsys == 'cstate_core':
		counter_scope = 'core'
	elif counter.subsys == 'cstate_pkg':
		counter_scope = 'package'
	else:
		counter_scope = 'cpu'

	counter_type = 'delta'
	column_name = counter.event

	cparams = counter.get_turbostat_perf_id(
		counter_scope = counter_scope,
		counter_type = counter_type,
		column_name = column_name
	)
	expected_columns.append(column_name.encode())
	counters_argv.extend(['--add', cparams])

expected_columns_debug = EXPECTED_COLUMNS_DEBUG_DEFAULT + expected_columns

def gen_user_friendly_cmdline(argv_):
	argv = argv_[:]
	ret = ''

	while len(argv) != 0:
		arg = argv.pop(0)
		arg_next = ''

		if arg in ('-i', '--show', '--add'):
			arg_next = argv.pop(0) if len(argv) > 0 else ''

		ret += f'{arg} {arg_next} \\\n\t'

	# Remove the last separator and return
	return ret[:-4]

#
# Run turbostat for some time and send SIGINT
#
timeout_argv = [timeout, '--preserve-status', '-s', 'SIGINT', '-k', '3', '0.2s']
turbostat_argv = [turbostat, '-i', '0.50', '--show', 'CPU'] + counters_argv

def check_columns_or_fail(expected_columns: list, actual_columns: list):
	if len(actual_columns) != len(expected_columns):
		print(f'turbostat column check failed\n{expected_columns=}\n{actual_columns=}')
		exit(1)

	failed = False
	for expected_column in expected_columns:
		if expected_column not in actual_columns:
			print(f'turbostat column check failed: missing column {expected_column.decode()}')
			failed = True

	if failed:
		exit(1)

cmdline = gen_user_friendly_cmdline(turbostat_argv)
print(f'Running turbostat with:\n\t{cmdline}\n... ', end = '', flush = True)
proc_turbostat = subprocess.run(timeout_argv + turbostat_argv, capture_output = True)
if proc_turbostat.returncode != 0:
	print(f'turbostat failed with {proc_turbostat.returncode}')
	exit(1)

actual_columns = proc_turbostat.stdout.split(b'\n')[0].split(b'\t')
check_columns_or_fail(expected_columns, actual_columns)
print('OK')

#
# Same, but with --debug
#
# We explicitly specify '--show CPU' to make sure turbostat
# don't show a bunch of default counters instead.
#
turbostat_argv.append('--debug')

cmdline = gen_user_friendly_cmdline(turbostat_argv)
print(f'Running turbostat (in debug mode) with:\n\t{cmdline}\n... ', end = '', flush = True)
proc_turbostat = subprocess.run(timeout_argv + turbostat_argv, capture_output = True)
if proc_turbostat.returncode != 0:
	print(f'turbostat failed with {proc_turbostat.returncode}')
	exit(1)

actual_columns = proc_turbostat.stdout.split(b'\n')[0].split(b'\t')
check_columns_or_fail(expected_columns_debug, actual_columns)
print('OK')
