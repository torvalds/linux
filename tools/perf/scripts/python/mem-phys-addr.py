# mem-phys-addr.py: Resolve physical address samples
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2018, Intel Corporation.

import os
import sys
import re
import bisect
import collections
from dataclasses import dataclass
from typing import (Dict, Optional)

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
    '/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

@dataclass(frozen=True)
class IomemEntry:
    """Read from a line in /proc/iomem"""
    begin: int
    end: int
    indent: int
    label: str

# Physical memory layout from /proc/iomem. Key is the indent and then
# a list of ranges.
iomem: Dict[int, list[IomemEntry]] = collections.defaultdict(list)
# Child nodes from the iomem parent.
children: Dict[IomemEntry, set[IomemEntry]] = collections.defaultdict(set)
# Maximum indent seen before an entry in the iomem file.
max_indent: int = 0
# Count for each range of memory.
load_mem_type_cnt: Dict[IomemEntry, int] = collections.Counter()
# Perf event name set from the first sample in the data.
event_name: Optional[str] = None

def parse_iomem():
    """Populate iomem from /proc/iomem file"""
    global iomem
    global max_indent
    global children
    with open('/proc/iomem', 'r', encoding='ascii') as f:
        for line in f:
            indent = 0
            while line[indent] == ' ':
                indent += 1
            if indent > max_indent:
                max_indent = indent
            m = re.split('-|:', line, 2)
            begin = int(m[0], 16)
            end = int(m[1], 16)
            label = m[2].strip()
            entry = IomemEntry(begin, end, indent, label)
            # Before adding entry, search for a parent node using its begin.
            if indent > 0:
                parent = find_memory_type(begin)
                assert parent, f"Given indent expected a parent for {label}"
                children[parent].add(entry)
            iomem[indent].append(entry)

def find_memory_type(phys_addr) -> Optional[IomemEntry]:
    """Search iomem for the range containing phys_addr with the maximum indent"""
    for i in range(max_indent, -1, -1):
        if i not in iomem:
            continue
        position = bisect.bisect_right(iomem[i], phys_addr,
                                       key=lambda entry: entry.begin)
        if position is None:
            continue
        iomem_entry = iomem[i][position-1]
        if  iomem_entry.begin <= phys_addr <= iomem_entry.end:
            return iomem_entry
    print(f"Didn't find {phys_addr}")
    return None

def print_memory_type():
    print(f"Event: {event_name}")
    print(f"{'Memory type':<40}  {'count':>10}  {'percentage':>10}")
    print(f"{'-' * 40:<40}  {'-' * 10:>10}  {'-' * 10:>10}")
    total = sum(load_mem_type_cnt.values())
    # Add count from children into the parent.
    for i in range(max_indent, -1, -1):
        if i not in iomem:
            continue
        for entry in iomem[i]:
            global children
            for child in children[entry]:
                if load_mem_type_cnt[child] > 0:
                    load_mem_type_cnt[entry] += load_mem_type_cnt[child]

    def print_entries(entries):
        """Print counts from parents down to their children"""
        global children
        for entry in sorted(entries,
                            key = lambda entry: load_mem_type_cnt[entry],
                            reverse = True):
            count = load_mem_type_cnt[entry]
            if count > 0:
                mem_type = ' ' * entry.indent + f"{entry.begin:x}-{entry.end:x} : {entry.label}"
                percent = 100 * count / total
                print(f"{mem_type:<40}  {count:>10}  {percent:>10.1f}")
                print_entries(children[entry])

    print_entries(iomem[0])

def trace_begin():
    parse_iomem()

def trace_end():
    print_memory_type()

def process_event(param_dict):
    if "sample" not in param_dict:
        return

    sample = param_dict["sample"]
    if "phys_addr" not in sample:
        return

    phys_addr  = sample["phys_addr"]
    entry = find_memory_type(phys_addr)
    if entry:
        load_mem_type_cnt[entry] += 1

    global event_name
    if event_name is None:
        event_name  = param_dict["ev_name"]
