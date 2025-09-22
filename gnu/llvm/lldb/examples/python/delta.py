#!/usr/bin/env python

# ----------------------------------------------------------------------
# This module will enable GDB remote packet logging when the
# 'start_gdb_log' command is called with a filename to log to. When the
# 'stop_gdb_log' command is called, it will disable the logging and
# print out statistics about how long commands took to execute and also
# will primnt ou
# Be sure to add the python path that points to the LLDB shared library.
#
# To use this in the embedded python interpreter using "lldb" just
# import it with the full path using the "command script import"
# command. This can be done from the LLDB command line:
#   (lldb) command script import /path/to/gdbremote.py
# Or it can be added to your ~/.lldbinit file so this module is always
# available.
# ----------------------------------------------------------------------

import optparse
import os
import shlex
import re
import tempfile


def start_gdb_log(debugger, command, result, dict):
    """Start logging GDB remote packets by enabling logging with timestamps and
    thread safe logging. Follow a call to this function with a call to "stop_gdb_log"
    in order to dump out the commands."""
    global log_file
    if log_file:
        result.PutCString(
            'error: logging is already in progress with file "%s"', log_file
        )
    else:
        args_len = len(args)
        if args_len == 0:
            log_file = tempfile.mktemp()
        elif len(args) == 1:
            log_file = args[0]

        if log_file:
            debugger.HandleCommand(
                'log enable --threadsafe --timestamp --file "%s" gdb-remote packets'
                % log_file
            )
            result.PutCString(
                "GDB packet logging enable with log file '%s'\nUse the 'stop_gdb_log' command to stop logging and show packet statistics."
                % log_file
            )
            return

        result.PutCString("error: invalid log file path")
    result.PutCString(usage)


def parse_time_log(debugger, command, result, dict):
    # Any commands whose names might be followed by more valid C identifier
    # characters must be listed here
    command_args = shlex.split(command)
    parse_time_log_args(command_args)


def parse_time_log_args(command_args):
    usage = "usage: parse_time_log [options] [<LOGFILEPATH>]"
    description = """Parse a log file that contains timestamps and convert the timestamps to delta times between log lines."""
    parser = optparse.OptionParser(
        description=description, prog="parse_time_log", usage=usage
    )
    parser.add_option(
        "-v",
        "--verbose",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return
    for log_file in args:
        parse_log_file(log_file, options)


def parse_log_file(file, options):
    """Parse a log file that was contains timestamps. These logs are typically
    generated using:
    (lldb) log enable --threadsafe --timestamp --file <FILE> ....

    This log file will contain timestamps and this function will then normalize
    those packets to be relative to the first value timestamp that is found and
    show delta times between log lines and also keep track of how long it takes
    for GDB remote commands to make a send/receive round trip. This can be
    handy when trying to figure out why some operation in the debugger is taking
    a long time during a preset set of debugger commands."""

    print("#----------------------------------------------------------------------")
    print("# Log file: '%s'" % file)
    print("#----------------------------------------------------------------------")

    timestamp_regex = re.compile("(\s*)([1-9][0-9]+\.[0-9]+)([^0-9].*)$")

    base_time = 0.0
    last_time = 0.0
    file = open(file)
    lines = file.read().splitlines()
    for line in lines:
        match = timestamp_regex.match(line)
        if match:
            curr_time = float(match.group(2))
            delta = 0.0
            if base_time:
                delta = curr_time - last_time
            else:
                base_time = curr_time

            print(
                "%s%.6f %+.6f%s"
                % (match.group(1), curr_time - base_time, delta, match.group(3))
            )
            last_time = curr_time
        else:
            print(line)


if __name__ == "__main__":
    import sys

    parse_time_log_args(sys.argv[1:])


def __lldb_init_module(debugger, internal_dict):
    # This initializer is being run from LLDB in the embedded command interpreter
    # Add any commands contained in this module to LLDB
    debugger.HandleCommand(
        "command script add -o -f delta.parse_time_log parse_time_log"
    )
    print(
        'The "parse_time_log" command is now installed and ready for use, type "parse_time_log --help" for more information'
    )
