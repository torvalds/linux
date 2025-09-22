#!/usr/bin/env python

# Given a -print-before-all and/or -print-after-all -print-module-scope log from
# an opt invocation, chunk it into a series of individual IR files, one for each
# pass invocation. If the log ends with an obvious stack trace, try to split off
# a separate "crashinfo.txt" file leaving only the valid input IR in the last
# chunk. Files are written to current working directory.

import sys
import re

chunk_id = 0

# This function gets the pass name from the following line:
# *** IR Dump Before/After PASS_NAME... ***
def get_pass_name(line, prefix):
    short_line = line[line.find(prefix) + len(prefix) + 1 :]
    return re.split(" |<", short_line)[0]


def print_chunk(lines, prefix, pass_name):
    global chunk_id
    fname = str(chunk_id).zfill(4) + "-" + prefix + "-" + pass_name + ".ll"
    chunk_id = chunk_id + 1
    print("writing chunk " + fname + " (" + str(len(lines)) + " lines)")
    with open(fname, "w") as f:
        f.writelines(lines)


is_dump = False
cur = []
for line in sys.stdin:
    if "*** IR Dump Before " in line:
        if len(cur) != 0:
            print_chunk(cur, "before", pass_name)
            cur = []
        cur.append("; " + line)
        pass_name = get_pass_name(line, "Before")
    elif "*** IR Dump After " in line:
        if len(cur) != 0:
            print_chunk(cur, "after", pass_name)
            cur = []
        cur.append("; " + line)
        pass_name = get_pass_name(line, "After")
    elif line.startswith("Stack dump:"):
        print_chunk(cur, "crash", pass_name)
        cur = []
        cur.append(line)
        is_dump = True
    else:
        cur.append(line)

if is_dump:
    print("writing crashinfo.txt (" + str(len(cur)) + " lines)")
    with open("crashinfo.txt", "w") as f:
        f.writelines(cur)
else:
    print_chunk(cur, "last", pass_name)
