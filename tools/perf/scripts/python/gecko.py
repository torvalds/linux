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
import json
import argparse
from dataclasses import dataclass, field
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

# Trace_end runs at the end and will be used to aggregate
# the data into the final json object and print it out to stdout.
def trace_end() -> None:
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
		# threads will be implemented in later commits.
		# "threads": threads,
		"processes": [],
		"pausedRanges": [],
	}
	json.dump(gecko_profile_with_meta, sys.stdout, indent=2)

def main() -> None:
	global CATEGORIES
	parser = argparse.ArgumentParser(description="Convert perf.data to Firefox\'s Gecko Profile format")

	# Add the command-line options
	# Colors must be defined according to this:
	# https://github.com/firefox-devtools/profiler/blob/50124adbfa488adba6e2674a8f2618cf34b59cd2/res/css/categories.css
	parser.add_argument('--user-color', default='yellow', help='Color for the User category')
	parser.add_argument('--kernel-color', default='orange', help='Color for the Kernel category')
	# Parse the command-line arguments
	args = parser.parse_args()
	# Access the values provided by the user
	user_color = args.user_color
	kernel_color = args.kernel_color

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
