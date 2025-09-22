#!/usr/bin/env python3

# Automatically formatted with yapf (https://github.com/google/yapf)

# Fake 'opt' program that can be made to crash on request. For testing
# the 'reduce_pipeline.py' automatic 'opt' NPM pipeline reducer.

import argparse
import os
import shutil
import signal

parser = argparse.ArgumentParser()
parser.add_argument("-passes", action="store", dest="passes", required=True)
parser.add_argument(
    "-print-pipeline-passes", dest="print_pipeline_passes", action="store_true"
)
parser.add_argument("-crash-seq", action="store", dest="crash_seq", required=True)
parser.add_argument("-o", action="store", dest="output")
parser.add_argument("input")
[args, unknown_args] = parser.parse_known_args()

# Expand pipeline if '-print-pipeline-passes'.
if args.print_pipeline_passes:
    if args.passes == "EXPAND_a_to_f":
        print("a,b,c,d,e,f")
    else:
        print(args.passes)
    exit(0)

# Parse '-crash-seq'.
crash_seq = []
tok = ""
for c in args.crash_seq:
    if c == ",":
        if tok != "":
            crash_seq.append(tok)
        tok = ""
    else:
        tok += c
if tok != "":
    crash_seq.append(tok)
print(crash_seq)

# Parse '-passes' and see if we need to crash.
tok = ""
for c in args.passes:
    if c == ",":
        if len(crash_seq) > 0 and crash_seq[0] == tok:
            crash_seq.pop(0)
        tok = ""
    elif c == "(":
        tok = ""
    elif c == ")":
        if len(crash_seq) > 0 and crash_seq[0] == tok:
            crash_seq.pop(0)
        tok = ""
    else:
        tok += c
if len(crash_seq) > 0 and crash_seq[0] == tok:
    crash_seq.pop(0)

# Copy input to output.
if args.output:
    shutil.copy(args.input, args.output)

# Crash if all 'crash_seq' passes occurred in right order.
if len(crash_seq) == 0:
    print("crash")
    os.kill(os.getpid(), signal.SIGKILL)
else:
    print("no crash")
    exit(0)
