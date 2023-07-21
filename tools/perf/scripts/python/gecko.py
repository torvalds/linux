# firefox-gecko-converter.py - Convert perf record output to Firefox's gecko profile format
# SPDX-License-Identifier: GPL-2.0
#
# The script converts perf.data to Gecko Profile Format,
# which can be read by https://profiler.firefox.com/.
#
# Usage:
#
#     perf record -a -g -F 99 sleep 60
#     perf script report gecko > output.json

import os
import sys
from typing import Dict

# Add the Perf-Trace-Util library to the Python path
sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *

# start_time is intialiazed only once for the all event traces.
start_time = None

# Uses perf script python interface to parse each
# event and store the data in the thread builder.
def process_event(param_dict: Dict) -> None:
	global start_time
	global tid_to_thread
	time_stamp = (param_dict['sample']['time'] // 1000) / 1000
	pid = param_dict['sample']['pid']
	tid = param_dict['sample']['tid']
	comm = param_dict['comm']

	# Start time is the time of the first sample
	if not start_time:
		start_time = time_stamp

# Trace_end runs at the end and will be used to aggregate
# the data into the final json object and print it out to stdout.
def trace_end() -> None:
	pass
