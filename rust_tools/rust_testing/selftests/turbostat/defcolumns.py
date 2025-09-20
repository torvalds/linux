#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
from shutil import which

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

#
# By default --list reports also "usec" and "Time_Of_Day_Seconds" columns
# which are only visible when running with --debug.
#
expected_columns_debug = proc_turbostat.stdout.replace(b',', b'\t').strip()
expected_columns = expected_columns_debug.replace(b'usec\t', b'').replace(b'Time_Of_Day_Seconds\t', b'').replace(b'X2APIC\t', b'').replace(b'APIC\t', b'')

#
# Run turbostat with no options for 10 seconds and send SIGINT
#
timeout_argv = [timeout, '--preserve-status', '-s', 'SIGINT', '-k', '3', '1s']
turbostat_argv = [turbostat, '-i', '0.250']

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
