# gecko.py - Convert perf record output to Firefox's gecko profile format
# SPDX-License-Identifier: GPL-2.0
#
# The script converts perf.data to Gecko Profile Format,
# which can be read by https://profiler.firefox.com/.
#
# Usage:
#
#     perf record -a -g -F 99 sleep 60
#     perf script report gecko
#
# Combined:
#
#     perf script gecko -F 99 -a sleep 60

import os
import sys
import time
import json
import string
import random
import argparse
import threading
import webbrowser
import urllib.parse
from os import system
from functools import reduce
from dataclasses import dataclass, field
from http.server import HTTPServer, SimpleHTTPRequestHandler, test
from typing import List, Dict, Optional, NamedTuple, Set, Tuple, Any

# Add the Perf-Trace-Util library to the Python path
sys.path.append(os.environ['PERF_EXEC_PATH'] + \
	'/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *

StringID = int
StackID = int
FrameID = int
CategoryID = int
Milliseconds = float

# start_time is intialiazed only once for the all event traces.
start_time = None

# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/profile.js#L425
# Follow Brendan Gregg's Flamegraph convention: orange for kernel and yellow for user space by default.
CATEGORIES = None

# The product name is used by the profiler UI to show the Operating system and Processor.
PRODUCT = os.popen('uname -op').read().strip()

# store the output file
output_file = None

# Here key = tid, value = Thread
tid_to_thread = dict()

# The HTTP server is used to serve the profile to the profiler UI.
http_server_thread = None

# The category index is used by the profiler UI to show the color of the flame graph.
USER_CATEGORY_INDEX = 0
KERNEL_CATEGORY_INDEX = 1

# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L156
class Frame(NamedTuple):
	string_id: StringID
	relevantForJS: bool
	innerWindowID: int
	implementation: None
	optimizations: None
	line: None
	column: None
	category: CategoryID
	subcategory: int

# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L216
class Stack(NamedTuple):
	prefix_id: Optional[StackID]
	frame_id: FrameID

# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L90
class Sample(NamedTuple):
	stack_id: Optional[StackID]
	time_ms: Milliseconds
	responsiveness: int

@dataclass
class Thread:
	"""A builder for a profile of the thread.

	Attributes:
		comm: Thread command-line (name).
		pid: process ID of containing process.
		tid: thread ID.
		samples: Timeline of profile samples.
		frameTable: interned stack frame ID -> stack frame.
		stringTable: interned string ID -> string.
		stringMap: interned string -> string ID.
		stackTable: interned stack ID -> stack.
		stackMap: (stack prefix ID, leaf stack frame ID) -> interned Stack ID.
		frameMap: Stack Frame string -> interned Frame ID.
		comm: str
		pid: int
		tid: int
		samples: List[Sample] = field(default_factory=list)
		frameTable: List[Frame] = field(default_factory=list)
		stringTable: List[str] = field(default_factory=list)
		stringMap: Dict[str, int] = field(default_factory=dict)
		stackTable: List[Stack] = field(default_factory=list)
		stackMap: Dict[Tuple[Optional[int], int], int] = field(default_factory=dict)
		frameMap: Dict[str, int] = field(default_factory=dict)
	"""
	comm: str
	pid: int
	tid: int
	samples: List[Sample] = field(default_factory=list)
	frameTable: List[Frame] = field(default_factory=list)
	stringTable: List[str] = field(default_factory=list)
	stringMap: Dict[str, int] = field(default_factory=dict)
	stackTable: List[Stack] = field(default_factory=list)
	stackMap: Dict[Tuple[Optional[int], int], int] = field(default_factory=dict)
	frameMap: Dict[str, int] = field(default_factory=dict)

	def _intern_stack(self, frame_id: int, prefix_id: Optional[int]) -> int:
		"""Gets a matching stack, or saves the new stack. Returns a Stack ID."""
		key = f"{frame_id}" if prefix_id is None else f"{frame_id},{prefix_id}"
		# key = (prefix_id, frame_id)
		stack_id = self.stackMap.get(key)
		if stack_id is None:
			# return stack_id
			stack_id = len(self.stackTable)
			self.stackTable.append(Stack(prefix_id=prefix_id, frame_id=frame_id))
			self.stackMap[key] = stack_id
		return stack_id

	def _intern_string(self, string: str) -> int:
		"""Gets a matching string, or saves the new string. Returns a String ID."""
		string_id = self.stringMap.get(string)
		if string_id is not None:
			return string_id
		string_id = len(self.stringTable)
		self.stringTable.append(string)
		self.stringMap[string] = string_id
		return string_id

	def _intern_frame(self, frame_str: str) -> int:
		"""Gets a matching stack frame, or saves the new frame. Returns a Frame ID."""
		frame_id = self.frameMap.get(frame_str)
		if frame_id is not None:
			return frame_id
		frame_id = len(self.frameTable)
		self.frameMap[frame_str] = frame_id
		string_id = self._intern_string(frame_str)

		symbol_name_to_category = KERNEL_CATEGORY_INDEX if frame_str.find('kallsyms') != -1 \
		or frame_str.find('/vmlinux') != -1 \
		or frame_str.endswith('.ko)') \
		else USER_CATEGORY_INDEX

		self.frameTable.append(Frame(
			string_id=string_id,
			relevantForJS=False,
			innerWindowID=0,
			implementation=None,
			optimizations=None,
			line=None,
			column=None,
			category=symbol_name_to_category,
			subcategory=None,
		))
		return frame_id

	def _add_sample(self, comm: str, stack: List[str], time_ms: Milliseconds) -> None:
		"""Add a timestamped stack trace sample to the thread builder.
		Args:
			comm: command-line (name) of the thread at this sample
			stack: sampled stack frames. Root first, leaf last.
			time_ms: timestamp of sample in milliseconds.
		"""
		# Ihreads may not set their names right after they are created.
		# Instead, they might do it later. In such situations, to use the latest name they have set.
		if self.comm != comm:
			self.comm = comm

		prefix_stack_id = reduce(lambda prefix_id, frame: self._intern_stack
						(self._intern_frame(frame), prefix_id), stack, None)
		if prefix_stack_id is not None:
			self.samples.append(Sample(stack_id=prefix_stack_id,
									time_ms=time_ms,
									responsiveness=0))

	def _to_json_dict(self) -> Dict:
		"""Converts current Thread to GeckoThread JSON format."""
		# Gecko profile format is row-oriented data as List[List],
		# And a schema for interpreting each index.
		# Schema:
		# https://github.com/firefox-devtools/profiler/blob/main/docs-developer/gecko-profile-format.md
		# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L230
		return {
			"tid": self.tid,
			"pid": self.pid,
			"name": self.comm,
			# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L51
			"markers": {
				"schema": {
					"name": 0,
					"startTime": 1,
					"endTime": 2,
					"phase": 3,
					"category": 4,
					"data": 5,
				},
				"data": [],
			},

			# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L90
			"samples": {
				"schema": {
					"stack": 0,
					"time": 1,
					"responsiveness": 2,
				},
				"data": self.samples
			},

			# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L156
			"frameTable": {
				"schema": {
					"location": 0,
					"relevantForJS": 1,
					"innerWindowID": 2,
					"implementation": 3,
					"optimizations": 4,
					"line": 5,
					"column": 6,
					"category": 7,
					"subcategory": 8,
				},
				"data": self.frameTable,
			},

			# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L216
			"stackTable": {
				"schema": {
					"prefix": 0,
					"frame": 1,
				},
				"data": self.stackTable,
			},
			"stringTable": self.stringTable,
			"registerTime": 0,
			"unregisterTime": None,
			"processType": "default",
		}

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

	# Parse and append the callchain of the current sample into a stack.
	stack = []
	if param_dict['callchain']:
		for call in param_dict['callchain']:
			if 'sym' not in call:
				continue
			stack.append(f'{call["sym"]["name"]} (in {call["dso"]})')
		if len(stack) != 0:
			# Reverse the stack, as root come first and the leaf at the end.
			stack = stack[::-1]

	# During perf record if -g is not used, the callchain is not available.
	# In that case, the symbol and dso are available in the event parameters.
	else:
		func = param_dict['symbol'] if 'symbol' in param_dict else '[unknown]'
		dso = param_dict['dso'] if 'dso' in param_dict else '[unknown]'
		stack.append(f'{func} (in {dso})')

	# Add sample to the specific thread.
	thread = tid_to_thread.get(tid)
	if thread is None:
		thread = Thread(comm=comm, pid=pid, tid=tid)
		tid_to_thread[tid] = thread
	thread._add_sample(comm=comm, stack=stack, time_ms=time_stamp)

def trace_begin() -> None:
	global output_file
	if (output_file is None):
		print("Staring Firefox Profiler on your default browser...")
		global http_server_thread
		http_server_thread = threading.Thread(target=test, args=(CORSRequestHandler, HTTPServer,))
		http_server_thread.daemon = True
		http_server_thread.start()

# Trace_end runs at the end and will be used to aggregate
# the data into the final json object and print it out to stdout.
def trace_end() -> None:
	global output_file
	threads = [thread._to_json_dict() for thread in tid_to_thread.values()]

	# Schema: https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L305
	gecko_profile_with_meta = {
		"meta": {
			"interval": 1,
			"processType": 0,
			"product": PRODUCT,
			"stackwalk": 1,
			"debug": 0,
			"gcpoison": 0,
			"asyncstack": 1,
			"startTime": start_time,
			"shutdownTime": None,
			"version": 24,
			"presymbolicated": True,
			"categories": CATEGORIES,
			"markerSchema": [],
			},
		"libs": [],
		"threads": threads,
		"processes": [],
		"pausedRanges": [],
	}
	# launch the profiler on local host if not specified --save-only args, otherwise print to file
	if (output_file is None):
		output_file = 'gecko_profile.json'
		with open(output_file, 'w') as f:
			json.dump(gecko_profile_with_meta, f, indent=2)
		launchFirefox(output_file)
		time.sleep(1)
		print(f'[ perf gecko: Captured and wrote into {output_file} ]')
	else:
		print(f'[ perf gecko: Captured and wrote into {output_file} ]')
		with open(output_file, 'w') as f:
			json.dump(gecko_profile_with_meta, f, indent=2)

# Used to enable Cross-Origin Resource Sharing (CORS) for requests coming from 'https://profiler.firefox.com', allowing it to access resources from this server.
class CORSRequestHandler(SimpleHTTPRequestHandler):
	def end_headers (self):
		self.send_header('Access-Control-Allow-Origin', 'https://profiler.firefox.com')
		SimpleHTTPRequestHandler.end_headers(self)

# start a local server to serve the gecko_profile.json file to the profiler.firefox.com
def launchFirefox(file):
	safe_string = urllib.parse.quote_plus(f'http://localhost:8000/{file}')
	url = 'https://profiler.firefox.com/from-url/' + safe_string
	webbrowser.open(f'{url}')

def main() -> None:
	global output_file
	global CATEGORIES
	parser = argparse.ArgumentParser(description="Convert perf.data to Firefox\'s Gecko Profile format which can be uploaded to profiler.firefox.com for visualization")

	# Add the command-line options
	# Colors must be defined according to this:
	# https://github.com/firefox-devtools/profiler/blob/50124adbfa488adba6e2674a8f2618cf34b59cd2/res/css/categories.css
	parser.add_argument('--user-color', default='yellow', help='Color for the User category', choices=['yellow', 'blue', 'purple', 'green', 'orange', 'red', 'grey', 'magenta'])
	parser.add_argument('--kernel-color', default='orange', help='Color for the Kernel category', choices=['yellow', 'blue', 'purple', 'green', 'orange', 'red', 'grey', 'magenta'])
	# If --save-only is specified, the output will be saved to a file instead of opening Firefox's profiler directly.
	parser.add_argument('--save-only', help='Save the output to a file instead of opening Firefox\'s profiler')

	# Parse the command-line arguments
	args = parser.parse_args()
	# Access the values provided by the user
	user_color = args.user_color
	kernel_color = args.kernel_color
	output_file = args.save_only

	CATEGORIES = [
		{
			"name": 'User',
			"color": user_color,
			"subcategories": ['Other']
		},
		{
			"name": 'Kernel',
			"color": kernel_color,
			"subcategories": ['Other']
		},
	]

if __name__ == '__main__':
	main()
