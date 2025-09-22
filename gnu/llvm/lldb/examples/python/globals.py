#!/usr/bin/env python

# ----------------------------------------------------------------------
# For the shells csh, tcsh:
#   ( setenv PYTHONPATH /Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework/Resources/Python ; ./globals.py <path> [<path> ...])
#
# For the shells sh, bash:
#   PYTHONPATH=/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework/Resources/Python ./globals.py <path> [<path> ...]
# ----------------------------------------------------------------------

import lldb
import optparse
import os
import shlex
import sys


def get_globals(raw_path, options):
    error = lldb.SBError()
    # Resolve the path if needed
    path = os.path.expanduser(raw_path)
    # Create a target using path + options
    target = lldb.debugger.CreateTarget(
        path, options.arch, options.platform, False, error
    )
    if target:
        # Get the executable module
        module = target.module[target.executable.basename]
        if module:
            # Keep track of which variables we have already looked up
            global_names = list()
            # Iterate through all symbols in the symbol table and watch for any
            # DATA symbols
            for symbol in module.symbols:
                if symbol.type == lldb.eSymbolTypeData:
                    # The symbol is a DATA symbol, lets try and find all global variables
                    # that match this name and print them
                    global_name = symbol.name
                    # Make sure we don't lookup the same variable twice
                    if global_name not in global_names:
                        global_names.append(global_name)
                        # Find all global variables by name
                        global_variable_list = module.FindGlobalVariables(
                            target, global_name, lldb.UINT32_MAX
                        )
                        if global_variable_list:
                            # Print results for anything that matched
                            for global_variable in global_variable_list:
                                # returns the global variable name as a string
                                print("name = %s" % global_variable.name)
                                # Returns the variable value as a string
                                print("value = %s" % global_variable.value)
                                print(
                                    "type = %s" % global_variable.type
                                )  # Returns an lldb.SBType object
                                # Returns an lldb.SBAddress (section offset
                                # address) for this global
                                print("addr = %s" % global_variable.addr)
                                # Returns the file virtual address for this
                                # global
                                print(
                                    "file_addr = 0x%x" % global_variable.addr.file_addr
                                )
                                # returns the global variable value as a string
                                print("location = %s" % global_variable.location)
                                # Returns the size in bytes of this global
                                # variable
                                print("size = %s" % global_variable.size)
                                print()


def globals(command_args):
    """Extract all globals from any arguments which must be paths to object files."""
    usage = "usage: %prog [options] <PATH> [PATH ...]"
    description = """This command will find all globals in the specified object file and return an list() of lldb.SBValue objects (which might be empty)."""
    parser = optparse.OptionParser(description=description, prog="globals", usage=usage)
    parser.add_option(
        "-v",
        "--verbose",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    parser.add_option(
        "-a",
        "--arch",
        type="string",
        metavar="arch",
        dest="arch",
        help="Specify an architecture (or triple) to use when extracting from a file.",
    )
    parser.add_option(
        "-p",
        "--platform",
        type="string",
        metavar="platform",
        dest="platform",
        help='Specify the platform to use when creating the debug target. Valid values include "localhost", "darwin-kernel", "ios-simulator", "remote-freebsd", "remote-macosx", "remote-ios", "remote-linux".',
    )
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return

    for path in args:
        get_globals(path, options)


if __name__ == "__main__":
    lldb.debugger = lldb.SBDebugger.Create()
    globals(sys.argv[1:])
