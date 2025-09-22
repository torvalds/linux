#!/usr/bin/env python

import subprocess
import optparse
import os
import os.path
import re
import sys


def extract_exe_symbol_names(arch, exe_path, match_str):
    command = 'dsymutil --arch %s -s "%s" | grep "%s" | colrm 1 69' % (
        arch,
        exe_path,
        match_str,
    )
    (command_exit_status, command_output) = subprocess.getstatusoutput(command)
    if command_exit_status == 0:
        if command_output:
            return command_output[0:-1].split("'\n")
        else:
            print("error: command returned no output")
    else:
        print(
            "error: command failed with exit status %i\n    command: %s"
            % (command_exit_status, command)
        )
    return list()


def verify_api(all_args):
    """Verify the API in the specified library is valid given one or more binaries."""
    usage = "usage: verify_api --library <path> [ --library <path> ...] executable1 [executable2 ...]"
    description = """Verify the API in the specified library is valid given one or more binaries.

    Example:

        verify_api.py --library ~/Documents/src/lldb/build/Debug/LLDB.framework/LLDB --arch x86_64 /Applications/Xcode.app/Contents/PlugIns/DebuggerLLDB.ideplugin/Contents/MacOS/DebuggerLLDB --api-regex lldb
    """
    parser = optparse.OptionParser(
        description=description, prog="verify_api", usage=usage
    )
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
        action="append",
        dest="archs",
        help="architecture to use when checking the api",
    )
    parser.add_option(
        "-r",
        "--api-regex",
        type="string",
        dest="api_regex_str",
        help="Exclude any undefined symbols that do not match this regular expression when searching for missing APIs.",
    )
    parser.add_option(
        "-l",
        "--library",
        type="string",
        action="append",
        dest="libraries",
        help="Specify one or more libraries that will contain all needed APIs for the executables.",
    )
    (options, args) = parser.parse_args(all_args)

    api_external_symbols = list()
    if options.archs:
        for arch in options.archs:
            for library in options.libraries:
                external_symbols = extract_exe_symbol_names(
                    arch, library, "(     SECT EXT)"
                )
                if external_symbols:
                    for external_symbol in external_symbols:
                        api_external_symbols.append(external_symbol)
                else:
                    sys.exit(1)
    else:
        print("error: must specify one or more architectures with the --arch option")
        sys.exit(4)
    if options.verbose:
        print("API symbols:")
        for i, external_symbol in enumerate(api_external_symbols):
            print("[%u] %s" % (i, external_symbol))

    api_regex = None
    if options.api_regex_str:
        api_regex = re.compile(options.api_regex_str)

    for arch in options.archs:
        for exe_path in args:
            print('Verifying (%s) "%s"...' % (arch, exe_path))
            exe_errors = 0
            undefined_symbols = extract_exe_symbol_names(
                arch, exe_path, "(     UNDF EXT)"
            )
            for undefined_symbol in undefined_symbols:
                if api_regex:
                    match = api_regex.search(undefined_symbol)
                    if not match:
                        if options.verbose:
                            print("ignoring symbol: %s" % (undefined_symbol))
                        continue
                if undefined_symbol in api_external_symbols:
                    if options.verbose:
                        print("verified symbol: %s" % (undefined_symbol))
                else:
                    print("missing symbol: %s" % (undefined_symbol))
                    exe_errors += 1
            if exe_errors:
                print(
                    "error: missing %u API symbols from %s"
                    % (exe_errors, options.libraries)
                )
            else:
                print("success")


if __name__ == "__main__":
    verify_api(sys.argv[1:])
