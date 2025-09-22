#!/usr/bin/env python

# ---------------------------------------------------------------------
# Be sure to add the python path that points to the LLDB shared library.
#
# # To use this in the embedded python interpreter using "lldb" just
# import it with the full path using the "command script import"
# command
#   (lldb) command script import /path/to/cmdtemplate.py
# ---------------------------------------------------------------------

import inspect
import lldb
import sys
from lldb.plugins.parsed_cmd import ParsedCommand

class FrameStatCommand(ParsedCommand):
    program = "framestats"

    @classmethod
    def register_lldb_command(cls, debugger, module_name):
        ParsedCommand.do_register_cmd(cls, debugger, module_name)
        print(
            'The "{0}" command has been installed, type "help {0}" or "{0} '
            '--help" for detailed help.'.format(cls.program)
        )

    def setup_command_definition(self):

        self.ov_parser.add_option(
            "i",
            "in-scope",
            help = "in_scope_only = True",
            value_type = lldb.eArgTypeBoolean,
            dest = "bool_arg",
            default = True,
        )

        self.ov_parser.add_option(
            "i",
            "in-scope",
            help = "in_scope_only = True",
            value_type = lldb.eArgTypeBoolean,
            dest = "inscope",
            default=True,
        )
        
        self.ov_parser.add_option(
            "a",
            "arguments",
            help = "arguments = True",
            value_type = lldb.eArgTypeBoolean,
            dest = "arguments",
            default = True,
        )

        self.ov_parser.add_option(
            "l",
            "locals",
            help = "locals = True",
            value_type = lldb.eArgTypeBoolean,
            dest = "locals",
            default = True,
        )

        self.ov_parser.add_option(
            "s",
            "statics",
            help = "statics = True",
            value_type = lldb.eArgTypeBoolean,
            dest = "statics",
            default = True,
        )

    def get_repeat_command(self, args):
        """As an example, make the command not auto-repeat:"""
        return ""

    def get_short_help(self):
        return "Example command for use in debugging"

    def get_long_help(self):
        return ("This command is meant to be an example of how to make "
            "an LLDB command that does something useful, follows "
            "best practices, and exploits the SB API. "
            "Specifically, this command computes the aggregate "
            "and average size of the variables in the current "
            "frame and allows you to tweak exactly which variables "
            "are to be accounted in the computation.")


    def __init__(self, debugger, unused):
        super().__init__(debugger, unused)

    def __call__(self, debugger, command, exe_ctx, result):
        # Always get program state from the lldb.SBExecutionContext passed
        # in as exe_ctx
        frame = exe_ctx.GetFrame()
        if not frame.IsValid():
            result.SetError("invalid frame")
            return

        variables_list = frame.GetVariables(
            self.ov_parser.arguments, self.ov_parser.locals, self.ov_parser.statics, self.ov_parser.inscope
        )
        variables_count = variables_list.GetSize()
        if variables_count == 0:
            print("no variables here", file=result)
            return
        total_size = 0
        for i in range(0, variables_count):
            variable = variables_list.GetValueAtIndex(i)
            variable_type = variable.GetType()
            total_size = total_size + variable_type.GetByteSize()
            average_size = float(total_size) / variables_count
            print(
                "Your frame has %d variables. Their total size "
                "is %d bytes. The average size is %f bytes"
                % (variables_count, total_size, average_size),
                file=result,
            )
        # not returning anything is akin to returning success


def __lldb_init_module(debugger, dict):
    # Register all classes that have a register_lldb_command method
    for _name, cls in inspect.getmembers(sys.modules[__name__]):
        if inspect.isclass(cls) and callable(
            getattr(cls, "register_lldb_command", None)
        ):
            cls.register_lldb_command(debugger, __name__)
