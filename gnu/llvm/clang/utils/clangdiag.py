#!/usr/bin/env python

# ----------------------------------------------------------------------
# Be sure to add the python path that points to the LLDB shared library.
#
# # To use this in the embedded python interpreter using "lldb" just
# import it with the full path using the "command script import"
# command
#   (lldb) command script import /path/to/clandiag.py
# ----------------------------------------------------------------------

from __future__ import absolute_import, division, print_function
import lldb
import argparse
import shlex
import os
import re
import subprocess


class MyParser(argparse.ArgumentParser):
    def format_help(self):
        return """     Commands for managing clang diagnostic breakpoints

Syntax: clangdiag enable [<warning>|<diag-name>]
        clangdiag disable
        clangdiag diagtool [<path>|reset]

The following subcommands are supported:

      enable   -- Enable clang diagnostic breakpoints.
      disable  -- Disable all clang diagnostic breakpoints.
      diagtool -- Return, set, or reset diagtool path.

This command sets breakpoints in clang, and clang based tools, that
emit diagnostics.  When a diagnostic is emitted, and clangdiag is
enabled, it will use the appropriate diagtool application to determine
the name of the DiagID, and set breakpoints in all locations that
'diag::name' appears in the source.  Since the new breakpoints are set
after they are encountered, users will need to launch the executable a
second time in order to hit the new breakpoints.

For in-tree builds, the diagtool application, used to map DiagID's to
names, is found automatically in the same directory as the target
executable.  However, out-or-tree builds must use the 'diagtool'
subcommand to set the appropriate path for diagtool in the clang debug
bin directory.  Since this mapping is created at build-time, it's
important for users to use the same version that was generated when
clang was compiled, or else the id's won't match.

Notes:
- Substrings can be passed for both <warning> and <diag-name>.
- If <warning> is passed, only enable the DiagID(s) for that warning.
- If <diag-name> is passed, only enable that DiagID.
- Rerunning enable clears existing breakpoints.
- diagtool is used in breakpoint callbacks, so it can be changed
  without the need to rerun enable.
- Adding this to your ~.lldbinit file makes clangdiag available at startup:
  "command script import /path/to/clangdiag.py"

"""


def create_diag_options():
    parser = MyParser(prog="clangdiag")
    subparsers = parser.add_subparsers(
        title="subcommands", dest="subcommands", metavar=""
    )
    disable_parser = subparsers.add_parser("disable")
    enable_parser = subparsers.add_parser("enable")
    enable_parser.add_argument("id", nargs="?")
    diagtool_parser = subparsers.add_parser("diagtool")
    diagtool_parser.add_argument("path", nargs="?")
    return parser


def getDiagtool(target, diagtool=None):
    id = target.GetProcess().GetProcessID()
    if "diagtool" not in getDiagtool.__dict__:
        getDiagtool.diagtool = {}
    if diagtool:
        if diagtool == "reset":
            getDiagtool.diagtool[id] = None
        elif os.path.exists(diagtool):
            getDiagtool.diagtool[id] = diagtool
        else:
            print("clangdiag: %s not found." % diagtool)
    if not id in getDiagtool.diagtool or not getDiagtool.diagtool[id]:
        getDiagtool.diagtool[id] = None
        exe = target.GetExecutable()
        if not exe.Exists():
            print("clangdiag: Target (%s) not set." % exe.GetFilename())
        else:
            diagtool = os.path.join(exe.GetDirectory(), "diagtool")
            if os.path.exists(diagtool):
                getDiagtool.diagtool[id] = diagtool
            else:
                print("clangdiag: diagtool not found along side %s" % exe)

    return getDiagtool.diagtool[id]


def setDiagBreakpoint(frame, bp_loc, dict):
    id = frame.FindVariable("DiagID").GetValue()
    if id is None:
        print("clangdiag: id is None")
        return False

    # Don't need to test this time, since we did that in enable.
    target = frame.GetThread().GetProcess().GetTarget()
    diagtool = getDiagtool(target)
    name = subprocess.check_output([diagtool, "find-diagnostic-id", id]).rstrip()
    # Make sure we only consider errors, warnings, and extensions.
    # FIXME: Make this configurable?
    prefixes = ["err_", "warn_", "exp_"]
    if len([prefix for prefix in prefixes + [""] if name.startswith(prefix)][0]):
        bp = target.BreakpointCreateBySourceRegex(name, lldb.SBFileSpec())
        bp.AddName("clang::Diagnostic")

    return False


def enable(exe_ctx, args):
    # Always disable existing breakpoints
    disable(exe_ctx)

    target = exe_ctx.GetTarget()
    numOfBreakpoints = target.GetNumBreakpoints()

    if args.id:
        # Make sure we only consider errors, warnings, and extensions.
        # FIXME: Make this configurable?
        prefixes = ["err_", "warn_", "exp_"]
        if len([prefix for prefix in prefixes + [""] if args.id.startswith(prefix)][0]):
            bp = target.BreakpointCreateBySourceRegex(args.id, lldb.SBFileSpec())
            bp.AddName("clang::Diagnostic")
        else:
            diagtool = getDiagtool(target)
            list = subprocess.check_output([diagtool, "list-warnings"]).rstrip()
            for line in list.splitlines(True):
                m = re.search(r" *(.*) .*\[\-W" + re.escape(args.id) + r".*].*", line)
                # Make sure we only consider warnings.
                if m and m.group(1).startswith("warn_"):
                    bp = target.BreakpointCreateBySourceRegex(
                        m.group(1), lldb.SBFileSpec()
                    )
                    bp.AddName("clang::Diagnostic")
    else:
        print("Adding callbacks.")
        bp = target.BreakpointCreateByName("DiagnosticsEngine::Report")
        bp.SetScriptCallbackFunction("clangdiag.setDiagBreakpoint")
        bp.AddName("clang::Diagnostic")

    count = target.GetNumBreakpoints() - numOfBreakpoints
    print("%i breakpoint%s added." % (count, "s"[count == 1 :]))

    return


def disable(exe_ctx):
    target = exe_ctx.GetTarget()
    # Remove all diag breakpoints.
    bkpts = lldb.SBBreakpointList(target)
    target.FindBreakpointsByName("clang::Diagnostic", bkpts)
    for i in range(bkpts.GetSize()):
        target.BreakpointDelete(bkpts.GetBreakpointAtIndex(i).GetID())

    return


def the_diag_command(debugger, command, exe_ctx, result, dict):
    # Use the Shell Lexer to properly parse up command options just like a
    # shell would
    command_args = shlex.split(command)
    parser = create_diag_options()
    try:
        args = parser.parse_args(command_args)
    except:
        return

    if args.subcommands == "enable":
        enable(exe_ctx, args)
    elif args.subcommands == "disable":
        disable(exe_ctx)
    else:
        diagtool = getDiagtool(exe_ctx.GetTarget(), args.path)
        print("diagtool = %s" % diagtool)

    return


def __lldb_init_module(debugger, dict):
    # This initializer is being run from LLDB in the embedded command interpreter
    # Make the options so we can generate the help text for the new LLDB
    # command line command prior to registering it with LLDB below
    parser = create_diag_options()
    the_diag_command.__doc__ = parser.format_help()
    # Add any commands contained in this module to LLDB
    debugger.HandleCommand("command script add -f clangdiag.the_diag_command clangdiag")
    print(
        'The "clangdiag" command has been installed, type "help clangdiag" or "clangdiag --help" for detailed help.'
    )
