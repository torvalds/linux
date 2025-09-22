#!/usr/bin/env python

import lldb
import optparse
import shlex
import string
import sys


class DumpLineTables:
    command_name = "dump-line-tables"
    short_description = (
        "Dumps full paths to compile unit files and optionally all line table files."
    )
    description = "Dumps all line tables from all compile units for any modules specified as arguments. Specifying the --verbose flag will output address ranges for each line entry."
    usage = "usage: %prog [options] MODULE1 [MODULE2 ...]"

    def create_options(self):
        self.parser = optparse.OptionParser(
            description=self.description, prog=self.command_name, usage=self.usage
        )

        self.parser.add_option(
            "-v",
            "--verbose",
            action="store_true",
            dest="verbose",
            help="Display verbose output.",
            default=False,
        )

    def get_short_help(self):
        return self.short_description

    def get_long_help(self):
        return self.help_string

    def __init__(self, debugger, unused):
        self.create_options()
        self.help_string = self.parser.format_help()

    def __call__(self, debugger, command, exe_ctx, result):
        # Use the Shell Lexer to properly parse up command options just like a
        # shell would
        command_args = shlex.split(command)

        try:
            (options, args) = self.parser.parse_args(command_args)
        except:
            # if you don't handle exceptions, passing an incorrect argument to the OptionParser will cause LLDB to exit
            # (courtesy of OptParse dealing with argument errors by throwing SystemExit)
            result.SetError("option parsing failed")
            return

        # Always get program state from the SBExecutionContext passed in as exe_ctx
        target = exe_ctx.GetTarget()
        if not target.IsValid():
            result.SetError("invalid target")
            return

        for module_path in args:
            module = target.module[module_path]
            if not module:
                result.SetError('no module found that matches "%s".' % (module_path))
                return
            num_cus = module.GetNumCompileUnits()
            print('Module: "%s"' % (module.file.fullpath), end=" ", file=result)
            if num_cus == 0:
                print("no debug info.", file=result)
                continue
            print("has %u compile units:" % (num_cus), file=result)
            for cu_idx in range(num_cus):
                cu = module.GetCompileUnitAtIndex(cu_idx)
                print("  Compile Unit: %s" % (cu.file.fullpath), file=result)
                for line_idx in range(cu.GetNumLineEntries()):
                    line_entry = cu.GetLineEntryAtIndex(line_idx)
                    start_file_addr = line_entry.addr.file_addr
                    end_file_addr = line_entry.end_addr.file_addr
                    # If the two addresses are equal, this line table entry
                    # is a termination entry
                    if options.verbose:
                        if start_file_addr != end_file_addr:
                            result.PutCString(
                                "    [%#x - %#x): %s"
                                % (start_file_addr, end_file_addr, line_entry)
                            )
                    else:
                        if start_file_addr == end_file_addr:
                            result.PutCString("    %#x: END" % (start_file_addr))
                        else:
                            result.PutCString(
                                "    %#x: %s" % (start_file_addr, line_entry)
                            )
                    if start_file_addr == end_file_addr:
                        result.PutCString("\n")


class DumpFiles:
    command_name = "dump-files"
    short_description = (
        "Dumps full paths to compile unit files and optionally all line table files."
    )
    usage = "usage: %prog [options] MODULE1 [MODULE2 ...]"
    description = """This class adds a dump-files command to the LLDB interpreter.

This command will dump all compile unit file paths found for each source file
for the binaries specified as arguments in the current target. Specify the
--support-files or -s option to see all file paths that a compile unit uses in
its lines tables. This is handy for troubleshooting why breakpoints aren't
working in IDEs that specify full paths to source files when setting file and
line breakpoints. Sometimes symlinks cause the debug info to contain the symlink
path and an IDE will resolve the path to the actual file and use the resolved
path when setting breakpoints.
"""

    def create_options(self):
        # Pass add_help_option = False, since this keeps the command in line with lldb commands,
        # and we wire up "help command" to work by providing the long & short help methods below.
        self.parser = optparse.OptionParser(
            description=self.description,
            prog=self.command_name,
            usage=self.usage,
            add_help_option=False,
        )

        self.parser.add_option(
            "-s",
            "--support-files",
            action="store_true",
            dest="support_files",
            help="Dumps full paths to all files used in a compile unit.",
            default=False,
        )

    def get_short_help(self):
        return self.short_description

    def get_long_help(self):
        return self.help_string

    def __init__(self, debugger, unused):
        self.create_options()
        self.help_string = self.parser.format_help()

    def __call__(self, debugger, command, exe_ctx, result):
        # Use the Shell Lexer to properly parse up command options just like a
        # shell would
        command_args = shlex.split(command)

        try:
            (options, args) = self.parser.parse_args(command_args)
        except:
            # if you don't handle exceptions, passing an incorrect argument to the OptionParser will cause LLDB to exit
            # (courtesy of OptParse dealing with argument errors by throwing SystemExit)
            result.SetError("option parsing failed")
            return

        # Always get program state from the SBExecutionContext passed in as exe_ctx
        target = exe_ctx.GetTarget()
        if not target.IsValid():
            result.SetError("invalid target")
            return

        if len(args) == 0:
            result.SetError("one or more executable paths must be specified")
            return

        for module_path in args:
            module = target.module[module_path]
            if not module:
                result.SetError('no module found that matches "%s".' % (module_path))
                return
            num_cus = module.GetNumCompileUnits()
            print('Module: "%s"' % (module.file.fullpath), end=" ", file=result)
            if num_cus == 0:
                print("no debug info.", file=result)
                continue
            print("has %u compile units:" % (num_cus), file=result)
            for i in range(num_cus):
                cu = module.GetCompileUnitAtIndex(i)
                print("  Compile Unit: %s" % (cu.file.fullpath), file=result)
                if options.support_files:
                    num_support_files = cu.GetNumSupportFiles()
                    for j in range(num_support_files):
                        path = cu.GetSupportFileAtIndex(j).fullpath
                        print("    file[%u]: %s" % (j, path), file=result)


def __lldb_init_module(debugger, dict):
    # This initializer is being run from LLDB in the embedded command interpreter

    # Add any commands contained in this module to LLDB
    debugger.HandleCommand(
        "command script add -o -c %s.DumpLineTables %s"
        % (__name__, DumpLineTables.command_name)
    )
    debugger.HandleCommand(
        "command script add -o -c %s.DumpFiles %s" % (__name__, DumpFiles.command_name)
    )
    print(
        'The "%s" and "%s" commands have been installed.'
        % (DumpLineTables.command_name, DumpFiles.command_name)
    )
