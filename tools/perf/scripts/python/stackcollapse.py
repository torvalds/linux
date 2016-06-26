# stackcollapse.py - format perf samples with one line per distinct call stack
#
# This script's output has two space-separated fields.  The first is a semicolon
# separated stack including the program name (from the "comm" field) and the
# function names from the call stack.  The second is a count:
#
#  swapper;start_kernel;rest_init;cpu_idle;default_idle;native_safe_halt 2
#
# The file is sorted according to the first field.
#
# Input may be created and processed using:
#
#  perf record -a -g -F 99 sleep 60
#  perf script report stackcollapse > out.stacks-folded
#
# (perf script record stackcollapse works too).
#
# Written by Paolo Bonzini <pbonzini@redhat.com>
# Based on Brendan Gregg's stackcollapse-perf.pl script.

import os
import sys
from collections import defaultdict
from optparse import OptionParser, make_option

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
                '/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *
from EventClass import *

# command line parsing

option_list = [
    # formatting options for the bottom entry of the stack
    make_option("--include-tid", dest="include_tid",
                 action="store_true", default=False,
                 help="include thread id in stack"),
    make_option("--include-pid", dest="include_pid",
                 action="store_true", default=False,
                 help="include process id in stack"),
    make_option("--no-comm", dest="include_comm",
                 action="store_false", default=True,
                 help="do not separate stacks according to comm"),
    make_option("--tidy-java", dest="tidy_java",
                 action="store_true", default=False,
                 help="beautify Java signatures"),
    make_option("--kernel", dest="annotate_kernel",
                 action="store_true", default=False,
                 help="annotate kernel functions with _[k]")
]

parser = OptionParser(option_list=option_list)
(opts, args) = parser.parse_args()

if len(args) != 0:
    parser.error("unexpected command line argument")
if opts.include_tid and not opts.include_comm:
    parser.error("requesting tid but not comm is invalid")
if opts.include_pid and not opts.include_comm:
    parser.error("requesting pid but not comm is invalid")

# event handlers

lines = defaultdict(lambda: 0)

def process_event(param_dict):
    def tidy_function_name(sym, dso):
        if sym is None:
            sym = '[unknown]'

        sym = sym.replace(';', ':')
        if opts.tidy_java:
            # the original stackcollapse-perf.pl script gives the
            # example of converting this:
            #    Lorg/mozilla/javascript/MemberBox;.<init>(Ljava/lang/reflect/Method;)V
            # to this:
            #    org/mozilla/javascript/MemberBox:.init
            sym = sym.replace('<', '')
            sym = sym.replace('>', '')
            if sym[0] == 'L' and sym.find('/'):
                sym = sym[1:]
            try:
                sym = sym[:sym.index('(')]
            except ValueError:
                pass

        if opts.annotate_kernel and dso == '[kernel.kallsyms]':
            return sym + '_[k]'
        else:
            return sym

    stack = list()
    if 'callchain' in param_dict:
        for entry in param_dict['callchain']:
            entry.setdefault('sym', dict())
            entry['sym'].setdefault('name', None)
            entry.setdefault('dso', None)
            stack.append(tidy_function_name(entry['sym']['name'],
                                            entry['dso']))
    else:
        param_dict.setdefault('symbol', None)
        param_dict.setdefault('dso', None)
        stack.append(tidy_function_name(param_dict['symbol'],
                                        param_dict['dso']))

    if opts.include_comm:
        comm = param_dict["comm"].replace(' ', '_')
        sep = "-"
        if opts.include_pid:
            comm = comm + sep + str(param_dict['sample']['pid'])
            sep = "/"
        if opts.include_tid:
            comm = comm + sep + str(param_dict['sample']['tid'])
        stack.append(comm)

    stack_string = ';'.join(reversed(stack))
    lines[stack_string] = lines[stack_string] + 1

def trace_end():
    list = lines.keys()
    list.sort()
    for stack in list:
        print "%s %d" % (stack, lines[stack])
