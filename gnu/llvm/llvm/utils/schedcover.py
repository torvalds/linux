#!/usr/bin/env python

# This creates a CSV file from the output of the debug output of subtarget:
#   llvm-tblgen --gen-subtarget --debug-only=subtarget-emitter
# With thanks to Dave Estes for mentioning the idea at 2014 LLVM Developers' Meeting

import os
import sys
import re
import operator

table = {}
models = set()
filt = None


def add(instr, model, resource=None):
    global table, models

    entry = table.setdefault(instr, dict())
    entry[model] = resource
    models.add(model)


def filter_model(m):
    global filt
    if m and filt:
        return filt.search(m) != None
    else:
        return True


def display():
    global table, models

    # remove default and itinerary so we can control their sort order to make
    # them first
    models.discard("default")
    models.discard("itinerary")

    ordered_table = sorted(table.items(), key=operator.itemgetter(0))
    ordered_models = ["itinerary", "default"]
    ordered_models.extend(sorted(models))
    ordered_models = [m for m in ordered_models if filter_model(m)]

    # print header
    sys.stdout.write("instruction")
    for model in ordered_models:
        sys.stdout.write(", {}".format(model))
    sys.stdout.write(os.linesep)

    for (instr, mapping) in ordered_table:
        sys.stdout.write(instr)
        for model in ordered_models:
            if model in mapping and mapping[model] is not None:
                sys.stdout.write(", {}".format(mapping[model]))
            else:
                sys.stdout.write(", ")
        sys.stdout.write(os.linesep)


def machineModelCover(path):
    # The interesting bits
    re_sched_default = re.compile("SchedRW machine model for ([^ ]*) (.*)\n")
    re_sched_no_default = re.compile("No machine model for ([^ ]*)\n")
    re_sched_spec = re.compile("InstRW on ([^ ]*) for ([^ ]*) (.*)\n")
    re_sched_no_spec = re.compile("No machine model for ([^ ]*) on processor (.*)\n")
    re_sched_itin = re.compile("Itinerary for ([^ ]*): ([^ ]*)\n")

    # scan the file
    with open(path, "r") as f:
        for line in f.readlines():
            match = re_sched_default.match(line)
            if match:
                add(match.group(1), "default", match.group(2))
            match = re_sched_no_default.match(line)
            if match:
                add(match.group(1), "default")
            match = re_sched_spec.match(line)
            if match:
                add(match.group(2), match.group(1), match.group(3))
            match = re_sched_no_spec.match(line)
            if match:
                add(match.group(1), match.group(2))
            match = re_sched_itin.match(line)
            if match:
                add(match.group(1), "itinerary", match.group(2))

    display()


if len(sys.argv) > 2:
    filt = re.compile(sys.argv[2], re.IGNORECASE)
machineModelCover(sys.argv[1])
