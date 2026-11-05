#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""mem-phys-addr.py: Resolve physical address samples"""
from __future__ import annotations
import argparse
import bisect
import collections
from dataclasses import dataclass
import re
from typing import (Dict, List, Optional)

import perf

@dataclass(frozen=True)
class IomemEntry:
    """Read from a line in /proc/iomem"""
    begin: int
    end: int
    indent: int
    label: str

    def __lt__(self, other) -> bool:
        if isinstance(other, int):
            return self.begin < other
        return self.begin < other.begin

    def __gt__(self, other) -> bool:
        if isinstance(other, int):
            return self.begin > other
        return self.begin > other.begin

# Physical memory layout from /proc/iomem. Key is the indent and then
# a list of ranges.
iomem: Dict[int, List[IomemEntry]] = collections.defaultdict(list)
# Child nodes from the iomem parent.
children: Dict[IomemEntry, List[IomemEntry]] = collections.defaultdict(list)
# Maximum indent seen before an entry in the iomem file.
_STATE: Dict[str, int] = {"max_indent": 0}
# Per-event counts for each range of memory.
event_counts: Dict[str, collections.Counter] = collections.defaultdict(collections.Counter)

def parse_iomem(iomem_path: str):
    """Populate iomem from iomem file"""
    with open(iomem_path, 'r', encoding='ascii') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.isspace():
                continue
            indent = 0
            while indent < len(line) and line[indent] == ' ':
                indent += 1
            _STATE["max_indent"] = max(_STATE["max_indent"], indent)
            m = re.split('-|:', line, maxsplit=2)
            if len(m) < 3:
                continue
            begin = int(m[0].strip(), 16)
            end = int(m[1].strip(), 16)
            label = m[2].strip()
            entry = IomemEntry(begin, end, indent, label)
            # Before adding entry, search for a parent node using its begin.
            if indent > 0:
                parent = find_memory_type(begin)
                assert parent, f"Given indent expected a parent for {label}"
                children[parent].append(entry)
            iomem[indent].append(entry)

def find_memory_type(phys_addr) -> Optional[IomemEntry]:
    """Search iomem for the range containing phys_addr with the maximum indent"""
    for i in range(_STATE["max_indent"], -1, -1):
        if i not in iomem:
            continue
        position = bisect.bisect_right(iomem[i], phys_addr)
        if position == 0:
            continue
        iomem_entry = iomem[i][position-1]
        if  iomem_entry.begin <= phys_addr <= iomem_entry.end:
            return iomem_entry
    return None

def _print_entries(entries, load_mem_type_cnt, total):
    """Print counts from parents down to their children"""
    for entry in sorted(entries,
                        key=lambda e: (-load_mem_type_cnt[e], e.begin)):
        count = load_mem_type_cnt[entry]
        if count > 0:
            mem_type = ' ' * entry.indent + f"{entry.begin:x}-{entry.end:x} : {entry.label}"
            percent = 100 * count / total
            print(f"{mem_type:<40}  {count:>10}  {percent:>10.1f}")
            _print_entries(children[entry], load_mem_type_cnt, total)

def print_memory_type():
    """Print the resolved memory types and their counts."""
    if not event_counts:
        print("No valid physical address samples found in perf data.")
        return

    for event_name, load_mem_type_cnt in event_counts.items():
        print(f"Event: {event_name}")
        print(f"{'Memory type':<40}  {'count':>10}  {'percentage':>10}")
        print(f"{'-' * 40:<40}  {'-' * 10:>10}  {'-' * 10:>10}")
        total = sum(load_mem_type_cnt.values())
        if total == 0:
            continue

        # Add count from children into the parent.
        for i in range(_STATE["max_indent"], -1, -1):
            if i not in iomem:
                continue
            for entry in iomem[i]:
                for child in children[entry]:
                    if load_mem_type_cnt[child] > 0:
                        load_mem_type_cnt[entry] += load_mem_type_cnt[child]

        _print_entries(iomem[0], load_mem_type_cnt, total)
        print()
if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Resolve physical address samples")
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    ap.add_argument("--iomem", default="/proc/iomem", help="Path to iomem file")
    args = ap.parse_args()

    def process_event(sample):
        """Process a single sample event."""
        phys_addr = sample.sample_phys_addr or 0
        if not phys_addr:
            return
        entry = find_memory_type(phys_addr)
        if entry:
            event_name = str(sample.evsel)
            if event_name.startswith("evsel(") and event_name.endswith(")"):
                event_name = event_name[6:-1]
            event_counts[event_name][entry] += 1

    parse_iomem(args.iomem)
    perf.session(perf.data(args.input), sample=process_event).process_events()
    print_memory_type()
