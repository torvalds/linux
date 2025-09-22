#!/usr/bin/env python

# ----------------------------------------------------------------------
# Be sure to add the python path that points to the LLDB shared library.
#
# # To use this in the embedded python interpreter using "lldb" just
# import it with the full path using the "command script import"
# command
#   (lldb) command script import /path/to/cmdtemplate.py
# ----------------------------------------------------------------------

import platform
import os
import re
import sys
import subprocess

try:
    # Just try for LLDB in case PYTHONPATH is already correctly setup
    import lldb
except ImportError:
    lldb_python_dirs = list()
    # lldb is not in the PYTHONPATH, try some defaults for the current platform
    platform_system = platform.system()
    if platform_system == "Darwin":
        # On Darwin, try the currently selected Xcode directory
        xcode_dir = subprocess.check_output("xcode-select --print-path", shell=True)
        if xcode_dir:
            lldb_python_dirs.append(
                os.path.realpath(
                    xcode_dir + "/../SharedFrameworks/LLDB.framework/Resources/Python"
                )
            )
            lldb_python_dirs.append(
                xcode_dir + "/Library/PrivateFrameworks/LLDB.framework/Resources/Python"
            )
        lldb_python_dirs.append(
            "/System/Library/PrivateFrameworks/LLDB.framework/Resources/Python"
        )
    success = False
    for lldb_python_dir in lldb_python_dirs:
        if os.path.exists(lldb_python_dir):
            if not (sys.path.__contains__(lldb_python_dir)):
                sys.path.append(lldb_python_dir)
                try:
                    import lldb
                except ImportError:
                    pass
                else:
                    print('imported lldb from: "%s"' % (lldb_python_dir))
                    success = True
                    break
    if not success:
        print(
            "error: couldn't locate the 'lldb' module, please set PYTHONPATH correctly"
        )
        sys.exit(1)

import optparse
import shlex
import string
import struct
import time


def append_data_callback(option, opt_str, value, parser):
    if opt_str == "--uint8":
        int8 = int(value, 0)
        parser.values.data += struct.pack("1B", int8)
    if opt_str == "--uint16":
        int16 = int(value, 0)
        parser.values.data += struct.pack("1H", int16)
    if opt_str == "--uint32":
        int32 = int(value, 0)
        parser.values.data += struct.pack("1I", int32)
    if opt_str == "--uint64":
        int64 = int(value, 0)
        parser.values.data += struct.pack("1Q", int64)
    if opt_str == "--int8":
        int8 = int(value, 0)
        parser.values.data += struct.pack("1b", int8)
    if opt_str == "--int16":
        int16 = int(value, 0)
        parser.values.data += struct.pack("1h", int16)
    if opt_str == "--int32":
        int32 = int(value, 0)
        parser.values.data += struct.pack("1i", int32)
    if opt_str == "--int64":
        int64 = int(value, 0)
        parser.values.data += struct.pack("1q", int64)


def create_memfind_options():
    usage = "usage: %prog [options] STARTADDR [ENDADDR]"
    description = """This command can find data in a specified address range.
Options are used to specify the data that is to be looked for and the options
can be specified multiple times to look for longer streams of data.
"""
    parser = optparse.OptionParser(description=description, prog="memfind", usage=usage)
    parser.add_option(
        "-s",
        "--size",
        type="int",
        metavar="BYTESIZE",
        dest="size",
        help="Specify the byte size to search.",
        default=0,
    )
    parser.add_option(
        "--int8",
        action="callback",
        callback=append_data_callback,
        type="string",
        metavar="INT",
        dest="data",
        help="Specify a 8 bit signed integer value to search for in memory.",
        default="",
    )
    parser.add_option(
        "--int16",
        action="callback",
        callback=append_data_callback,
        type="string",
        metavar="INT",
        dest="data",
        help="Specify a 16 bit signed integer value to search for in memory.",
        default="",
    )
    parser.add_option(
        "--int32",
        action="callback",
        callback=append_data_callback,
        type="string",
        metavar="INT",
        dest="data",
        help="Specify a 32 bit signed integer value to search for in memory.",
        default="",
    )
    parser.add_option(
        "--int64",
        action="callback",
        callback=append_data_callback,
        type="string",
        metavar="INT",
        dest="data",
        help="Specify a 64 bit signed integer value to search for in memory.",
        default="",
    )
    parser.add_option(
        "--uint8",
        action="callback",
        callback=append_data_callback,
        type="string",
        metavar="INT",
        dest="data",
        help="Specify a 8 bit unsigned integer value to search for in memory.",
        default="",
    )
    parser.add_option(
        "--uint16",
        action="callback",
        callback=append_data_callback,
        type="string",
        metavar="INT",
        dest="data",
        help="Specify a 16 bit unsigned integer value to search for in memory.",
        default="",
    )
    parser.add_option(
        "--uint32",
        action="callback",
        callback=append_data_callback,
        type="string",
        metavar="INT",
        dest="data",
        help="Specify a 32 bit unsigned integer value to search for in memory.",
        default="",
    )
    parser.add_option(
        "--uint64",
        action="callback",
        callback=append_data_callback,
        type="string",
        metavar="INT",
        dest="data",
        help="Specify a 64 bit unsigned integer value to search for in memory.",
        default="",
    )
    return parser


def memfind_command(debugger, command, result, dict):
    # Use the Shell Lexer to properly parse up command options just like a
    # shell would
    command_args = shlex.split(command)
    parser = create_memfind_options()
    (options, args) = parser.parse_args(command_args)
    # try:
    #     (options, args) = parser.parse_args(command_args)
    # except:
    #     # if you don't handle exceptions, passing an incorrect argument to the OptionParser will cause LLDB to exit
    #     # (courtesy of OptParse dealing with argument errors by throwing SystemExit)
    #     result.SetStatus (lldb.eReturnStatusFailed)
    #     print >>result, "error: option parsing failed" # returning a string is the same as returning an error whose description is the string
    #     return
    memfind(debugger.GetSelectedTarget(), options, args, result)


def print_error(str, show_usage, result):
    print(str, file=result)
    if show_usage:
        print(create_memfind_options().format_help(), file=result)


def memfind(target, options, args, result):
    num_args = len(args)
    start_addr = 0
    if num_args == 1:
        if options.size > 0:
            print_error(
                "error: --size must be specified if there is no ENDADDR argument",
                True,
                result,
            )
            return
        start_addr = int(args[0], 0)
    elif num_args == 2:
        if options.size != 0:
            print_error(
                "error: --size can't be specified with an ENDADDR argument",
                True,
                result,
            )
            return
        start_addr = int(args[0], 0)
        end_addr = int(args[1], 0)
        if start_addr >= end_addr:
            print_error(
                "error: inavlid memory range [%#x - %#x)" % (start_addr, end_addr),
                True,
                result,
            )
            return
        options.size = end_addr - start_addr
    else:
        print_error("error: memfind takes 1 or 2 arguments", True, result)
        return

    if not options.data:
        print("error: no data specified to search for", file=result)
        return

    if not target:
        print("error: invalid target", file=result)
        return
    process = target.process
    if not process:
        print("error: invalid process", file=result)
        return

    error = lldb.SBError()
    bytes = process.ReadMemory(start_addr, options.size, error)
    if error.Success():
        num_matches = 0
        print(
            "Searching memory range [%#x - %#x) for" % (start_addr, end_addr),
            end=" ",
            file=result,
        )
        for byte in options.data:
            print("%2.2x" % ord(byte), end=" ", file=result)
        print(file=result)

        match_index = string.find(bytes, options.data)
        while match_index != -1:
            num_matches = num_matches + 1
            print(
                "%#x: %#x + %u" % (start_addr + match_index, start_addr, match_index),
                file=result,
            )
            match_index = string.find(bytes, options.data, match_index + 1)

        if num_matches == 0:
            print("error: no matches found", file=result)
    else:
        print("error: %s" % (error.GetCString()), file=result)


if __name__ == "__main__":
    print(
        "error: this script is designed to be used within the embedded script interpreter in LLDB"
    )


def __lldb_init_module(debugger, internal_dict):
    memfind_command.__doc__ = create_memfind_options().format_help()
    debugger.HandleCommand("command script add -o -f memory.memfind_command memfind")
    print('"memfind" command installed, use the "--help" option for detailed help')
