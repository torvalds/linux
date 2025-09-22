#!/usr/bin/env python
# ===- lib/fuzzer/scripts/unbalanced_allocs.py ------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#
#
# Post-process -trace_malloc=2 output and printout only allocations and frees
# unbalanced inside of fuzzer runs.
# Usage:
#   my_fuzzer -trace_malloc=2 -runs=10 2>&1 | unbalanced_allocs.py -skip=5
#
# ===------------------------------------------------------------------------===#

import argparse
import sys

_skip = 0


def PrintStack(line, stack):
    global _skip
    if _skip > 0:
        return
    print("Unbalanced " + line.rstrip())
    for l in stack:
        print(l.rstrip())


def ProcessStack(line, f):
    stack = []
    while line and line.startswith("    #"):
        stack += [line]
        line = f.readline()
    return line, stack


def ProcessFree(line, f, allocs):
    if not line.startswith("FREE["):
        return f.readline()

    addr = int(line.split()[1], 16)
    next_line, stack = ProcessStack(f.readline(), f)
    if addr in allocs:
        del allocs[addr]
    else:
        PrintStack(line, stack)
    return next_line


def ProcessMalloc(line, f, allocs):
    if not line.startswith("MALLOC["):
        return ProcessFree(line, f, allocs)

    addr = int(line.split()[1], 16)
    assert not addr in allocs

    next_line, stack = ProcessStack(f.readline(), f)
    allocs[addr] = (line, stack)
    return next_line


def ProcessRun(line, f):
    if not line.startswith("MallocFreeTracer: START"):
        return ProcessMalloc(line, f, {})

    allocs = {}
    print(line.rstrip())
    line = f.readline()
    while line:
        if line.startswith("MallocFreeTracer: STOP"):
            global _skip
            _skip = _skip - 1
            for _, (l, s) in allocs.items():
                PrintStack(l, s)
            print(line.rstrip())
            return f.readline()
        line = ProcessMalloc(line, f, allocs)
    return line


def ProcessFile(f):
    line = f.readline()
    while line:
        line = ProcessRun(line, f)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip", default=0, help="number of runs to ignore")
    args = parser.parse_args()
    global _skip
    _skip = int(args.skip) + 1
    ProcessFile(sys.stdin)


if __name__ == "__main__":
    main(sys.argv)
