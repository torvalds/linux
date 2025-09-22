#!/usr/bin/env python

import argparse
import itertools
import os
import re
import sys
from collections import defaultdict

from use_lldb_suite import lldb_root

parser = argparse.ArgumentParser(
    description="Analyze LLDB project #include dependencies."
)
parser.add_argument(
    "--show-counts",
    default=False,
    action="store_true",
    help="When true, show the number of dependencies from each subproject",
)
parser.add_argument(
    "--discover-cycles",
    default=False,
    action="store_true",
    help="When true, find and display all project dependency cycles.  Note,"
    "this option is very slow",
)

args = parser.parse_args()

src_dir = os.path.join(lldb_root, "source")
inc_dir = os.path.join(lldb_root, "include")

src_map = {}

include_regex = re.compile('#include "((lldb|Plugins|clang)(.*/)+).*"')


def is_sublist(small, big):
    it = iter(big)
    return all(c in it for c in small)


def normalize_host(str):
    if str.startswith("lldb/Host"):
        return "lldb/Host"
    if str.startswith("Plugins"):
        return "lldb/" + str
    if str.startswith("lldb/../../source"):
        return str.replace("lldb/../../source", "lldb")
    return str


def scan_deps(this_dir, file):
    global src_map
    deps = {}
    this_dir = normalize_host(this_dir)
    if this_dir in src_map:
        deps = src_map[this_dir]

    with open(file) as f:
        for line in list(f):
            m = include_regex.match(line)
            if m is None:
                continue
            relative = m.groups()[0].rstrip("/")
            if relative == this_dir:
                continue
            relative = normalize_host(relative)
            if relative in deps:
                deps[relative] += 1
            elif relative != this_dir:
                deps[relative] = 1
    if this_dir not in src_map and len(deps) > 0:
        src_map[this_dir] = deps


for base, dirs, files in os.walk(inc_dir):
    dir = os.path.basename(base)
    relative = os.path.relpath(base, inc_dir)
    inc_files = [x for x in files if os.path.splitext(x)[1] in [".h"]]
    relative = relative.replace("\\", "/")
    for inc in inc_files:
        inc_path = os.path.join(base, inc)
        scan_deps(relative, inc_path)

for base, dirs, files in os.walk(src_dir):
    dir = os.path.basename(base)
    relative = os.path.relpath(base, src_dir)
    src_files = [x for x in files if os.path.splitext(x)[1] in [".cpp", ".h", ".mm"]]
    norm_base_path = os.path.normpath(os.path.join("lldb", relative))
    norm_base_path = norm_base_path.replace("\\", "/")
    for src in src_files:
        src_path = os.path.join(base, src)
        scan_deps(norm_base_path, src_path)
    pass


def is_existing_cycle(path, cycles):
    # If we have a cycle like # A -> B -> C (with an implicit -> A at the end)
    # then we don't just want to check for an occurrence of A -> B -> C in the
    # list of known cycles, but every possible rotation of A -> B -> C.  For
    # example, if we previously encountered B -> C -> A (with an implicit -> B
    # at the end), then A -> B -> C is also a cycle.  This is an important
    # optimization which reduces the search space by multiple orders of
    # magnitude.
    for i in range(0, len(path)):
        if any(is_sublist(x, path) for x in cycles):
            return True
        path = [path[-1]] + path[0:-1]
    return False


def expand(path_queue, path_lengths, cycles, src_map):
    # We do a breadth first search, to make sure we visit all paths in order
    # of ascending length.  This is an important optimization to make sure that
    # short cycles are discovered first, which will allow us to discard longer
    # cycles which grow the search space exponentially the longer they get.
    while len(path_queue) > 0:
        cur_path = path_queue.pop(0)
        if is_existing_cycle(cur_path, cycles):
            continue

        next_len = path_lengths.pop(0) + 1
        last_component = cur_path[-1]

        for item in src_map.get(last_component, []):
            if item.startswith("clang"):
                continue

            if item in cur_path:
                # This is a cycle.  Minimize it and then check if the result is
                # already in the list of cycles.  Insert it (or not) and then
                # exit.
                new_index = cur_path.index(item)
                cycle = cur_path[new_index:]
                if not is_existing_cycle(cycle, cycles):
                    cycles.append(cycle)
                continue

            path_lengths.append(next_len)
            path_queue.append(cur_path + [item])
    pass


cycles = []

path_queue = [[x] for x in iter(src_map)]
path_lens = [1] * len(path_queue)

items = list(src_map.items())
items.sort(key=lambda A: A[0])

for path, deps in items:
    print(path + ":")
    sorted_deps = list(deps.items())
    if args.show_counts:
        sorted_deps.sort(key=lambda A: (A[1], A[0]))
        for dep in sorted_deps:
            print("\t{} [{}]".format(dep[0], dep[1]))
    else:
        sorted_deps.sort(key=lambda A: A[0])
        for dep in sorted_deps:
            print("\t{}".format(dep[0]))


def iter_cycles(cycles):
    global src_map
    for cycle in cycles:
        cycle.append(cycle[0])
        zipper = list(zip(cycle[0:-1], cycle[1:]))
        result = [(x, src_map[x][y], y) for (x, y) in zipper]
        total = 0
        smallest = result[0][1]
        for first, value, last in result:
            total += value
            smallest = min(smallest, value)
        yield (total, smallest, result)


if args.discover_cycles:
    print("Analyzing cycles...")

    expand(path_queue, path_lens, cycles, src_map)

    average = sum([len(x) + 1 for x in cycles]) / len(cycles)

    print("Found {} cycles.  Average cycle length = {}.".format(len(cycles), average))
    counted = list(iter_cycles(cycles))
    if args.show_counts:
        counted.sort(key=lambda A: A[0])
        for total, smallest, cycle in counted:
            sys.stdout.write("{} deps to break: ".format(total))
            sys.stdout.write(cycle[0][0])
            for first, count, last in cycle:
                sys.stdout.write(" [{}->] {}".format(count, last))
            sys.stdout.write("\n")
    else:
        for cycle in cycles:
            cycle.append(cycle[0])
            print(" -> ".join(cycle))

    print("Analyzing islands...")
    islands = []
    outgoing_counts = defaultdict(int)
    incoming_counts = defaultdict(int)
    for total, smallest, cycle in counted:
        for first, count, last in cycle:
            outgoing_counts[first] += count
            incoming_counts[last] += count
    for cycle in cycles:
        this_cycle = set(cycle)
        disjoints = [x for x in islands if this_cycle.isdisjoint(x)]
        overlaps = [x for x in islands if not this_cycle.isdisjoint(x)]
        islands = disjoints + [set.union(this_cycle, *overlaps)]
    print("Found {} disjoint cycle islands...".format(len(islands)))
    for island in islands:
        print("Island ({} elements)".format(len(island)))
        sorted = []
        for node in island:
            sorted.append((node, incoming_counts[node], outgoing_counts[node]))
        sorted.sort(key=lambda x: x[1] + x[2])
        for node, inc, outg in sorted:
            print("  {} [{} in, {} out]".format(node, inc, outg))
    sys.stdout.flush()
pass
