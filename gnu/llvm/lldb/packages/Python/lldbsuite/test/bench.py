#!/usr/bin/env python

"""
A simple bench runner which delegates to the ./dotest.py test driver to run the
benchmarks defined in the list named 'benches'.

You need to hand edit 'benches' to modify/change the command lines passed to the
test driver.

Use the following to get only the benchmark results in your terminal output:

    ./bench.py -e /Volumes/data/lldb/svn/regression/build/Debug/lldb -x '-F Driver::MainLoop()' 2>&1 | grep -P '^lldb.*benchmark:'
"""

import os
from optparse import OptionParser

# dotest.py invocation with no '-e exe-path' uses lldb as the inferior program,
# unless there is a mentioning of custom executable program.
benches = [
    # Measure startup delays creating a target, setting a breakpoint, and run
    # to breakpoint stop.
    "./dotest.py -v +b %E %X -n -p TestStartupDelays.py",
    # Measure 'frame variable' response after stopping at a breakpoint.
    "./dotest.py -v +b %E %X -n -p TestFrameVariableResponse.py",
    # Measure stepping speed after stopping at a breakpoint.
    "./dotest.py -v +b %E %X -n -p TestSteppingSpeed.py",
    # Measure expression cmd response with a simple custom executable program.
    "./dotest.py +b -n -p TestExpressionCmd.py",
    # Attach to a spawned process then run disassembly benchmarks.
    "./dotest.py -v +b -n %E -p TestDoAttachThenDisassembly.py",
]


def main():
    """Read the items from 'benches' and run the command line one by one."""
    parser = OptionParser(
        usage="""\
%prog [options]
Run the standard benchmarks defined in the list named 'benches'.\
"""
    )
    parser.add_option(
        "-e",
        "--executable",
        type="string",
        action="store",
        dest="exe",
        help="The target program launched by lldb.",
    )
    parser.add_option(
        "-x",
        "--breakpoint-spec",
        type="string",
        action="store",
        dest="break_spec",
        help="The lldb breakpoint spec for the target program.",
    )

    # Parses the options, if any.
    opts, args = parser.parse_args()

    print("Starting bench runner....")

    for item in benches:
        command = item.replace("%E", '-e "%s"' % opts.exe if opts.exe else "")
        command = command.replace(
            "%X", '-x "%s"' % opts.break_spec if opts.break_spec else ""
        )
        print("Running %s" % (command))
        os.system(command)

    print("Bench runner done.")


if __name__ == "__main__":
    main()
