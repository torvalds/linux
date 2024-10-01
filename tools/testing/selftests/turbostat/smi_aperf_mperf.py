#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
from shutil import which
from os import pread

# CDLL calls dlopen underneath.
# Calling it with None (null), we get handle to the our own image (python interpreter).
# We hope to find sched_getcpu() inside ;]
# This is a bit ugly, but helps shipping working software, so..
try:
	import ctypes

	this_image = ctypes.CDLL(None)
	BASE_CPU = this_image.sched_getcpu()
except:
	BASE_CPU = 0 # If we fail, set to 0 and pray it's not offline.

MSR_IA32_MPERF = 0x000000e7
MSR_IA32_APERF = 0x000000e8

def check_perf_access():
	perf = which('perf')
	if perf is None:
		print('SKIP: Could not find perf binary, thus could not determine perf access.')
		return False

	def has_perf_counter_access(counter_name):
		proc_perf = subprocess.run([perf, 'stat', '-e', counter_name, '--timeout', '10'],
					    capture_output = True)

		if proc_perf.returncode != 0:
			print(f'SKIP: Could not read {counter_name} perf counter, assuming no access.')
			return False

		if b'<not supported>' in proc_perf.stderr:
			print(f'SKIP: Could not read {counter_name} perf counter, assuming no access.')
			return False

		return True

	if not has_perf_counter_access('msr/mperf/'):
		return False
	if not has_perf_counter_access('msr/aperf/'):
		return False
	if not has_perf_counter_access('msr/smi/'):
		return False

	return True

def check_msr_access():
	try:
		file_msr = open(f'/dev/cpu/{BASE_CPU}/msr', 'rb')
	except:
		return False

	if len(pread(file_msr.fileno(), 8, MSR_IA32_MPERF)) != 8:
		return False

	if len(pread(file_msr.fileno(), 8, MSR_IA32_APERF)) != 8:
		return False

	return True

has_perf_access = check_perf_access()
has_msr_access = check_msr_access()

turbostat_counter_source_opts = ['']

if has_msr_access:
	turbostat_counter_source_opts.append('--no-perf')
else:
	print('SKIP: doesn\'t have MSR access, skipping run with --no-perf')

if has_perf_access:
	turbostat_counter_source_opts.append('--no-msr')
else:
	print('SKIP: doesn\'t have perf access, skipping run with --no-msr')

if not has_msr_access and not has_perf_access:
	print('SKIP: No MSR nor perf access detected. Skipping the tests entirely')
	exit(0)

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

EXPECTED_COLUMNS_DEBUG_DEFAULT = b'usec\tTime_Of_Day_Seconds\tAPIC\tX2APIC'

SMI_APERF_MPERF_DEPENDENT_BICS = [
	'SMI',
	'Avg_MHz',
	'Busy%',
	'Bzy_MHz',
]
if has_perf_access:
	SMI_APERF_MPERF_DEPENDENT_BICS.append('IPC')

for bic in SMI_APERF_MPERF_DEPENDENT_BICS:
	for counter_source_opt in turbostat_counter_source_opts:

		# Ugly special case, but it is what it is..
		if counter_source_opt == '--no-perf' and bic == 'IPC':
			continue

		expected_columns = bic.encode()
		expected_columns_debug = EXPECTED_COLUMNS_DEBUG_DEFAULT + f'\t{bic}'.encode()

		#
		# Run turbostat for some time and send SIGINT
		#
		timeout_argv = [timeout, '--preserve-status', '-s', 'SIGINT', '-k', '3', '0.2s']
		turbostat_argv = [turbostat, '-i', '0.50', '--show', bic]

		if counter_source_opt:
			turbostat_argv.append(counter_source_opt)

		print(f'Running turbostat with {turbostat_argv=}... ', end = '', flush = True)
		proc_turbostat = subprocess.run(timeout_argv + turbostat_argv, capture_output = True)
		if proc_turbostat.returncode != 0:
			print(f'turbostat failed with {proc_turbostat.returncode}')
			exit(1)

		actual_columns = proc_turbostat.stdout.split(b'\n')[0]
		if expected_columns != actual_columns:
			print(f'turbostat column check failed\n{expected_columns=}\n{actual_columns=}')
			exit(1)
		print('OK')

		#
		# Same, but with --debug
		#
		turbostat_argv.append('--debug')

		print(f'Running turbostat with {turbostat_argv=}... ', end = '', flush = True)
		proc_turbostat = subprocess.run(timeout_argv + turbostat_argv, capture_output = True)
		if proc_turbostat.returncode != 0:
			print(f'turbostat failed with {proc_turbostat.returncode}')
			exit(1)

		actual_columns = proc_turbostat.stdout.split(b'\n')[0]
		if expected_columns_debug != actual_columns:
			print(f'turbostat column check failed\n{expected_columns_debug=}\n{actual_columns=}')
			exit(1)
		print('OK')
