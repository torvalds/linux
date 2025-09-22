##===-- lldbutil.py ------------------------------------------*- Python -*-===##
##
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
##
##===----------------------------------------------------------------------===##

"""
This LLDB module contains miscellaneous utilities.
Some of the test suite takes advantage of the utility functions defined here.
They can also be useful for general purpose lldb scripting.
"""

import lldb
import os
import sys
import io

# ===================================================
# Utilities for locating/checking executable programs
# ===================================================


def is_exe(fpath):
    """Returns True if fpath is an executable."""
    return os.path.isfile(fpath) and os.access(fpath, os.X_OK)


def which(program):
    """Returns the full path to a program; None otherwise."""
    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None


# ===================================================
# Disassembly for an SBFunction or an SBSymbol object
# ===================================================


def disassemble(target, function_or_symbol):
    """Disassemble the function or symbol given a target.

    It returns the disassembly content in a string object.
    """
    buf = io.StringIO()
    insts = function_or_symbol.GetInstructions(target)
    for i in insts:
        print(i, file=buf)
    return buf.getvalue()


# ==========================================================
# Integer (byte size 1, 2, 4, and 8) to bytearray conversion
# ==========================================================


def int_to_bytearray(val, bytesize):
    """Utility function to convert an integer into a bytearray.

    It returns the bytearray in the little endian format.  It is easy to get the
    big endian format, just do ba.reverse() on the returned object.
    """
    import struct

    if bytesize == 1:
        return bytearray([val])

    # Little endian followed by a format character.
    template = "<%c"
    if bytesize == 2:
        fmt = template % "h"
    elif bytesize == 4:
        fmt = template % "i"
    elif bytesize == 4:
        fmt = template % "q"
    else:
        return None

    packed = struct.pack(fmt, val)
    return bytearray(ord(x) for x in packed)


def bytearray_to_int(bytes, bytesize):
    """Utility function to convert a bytearray into an integer.

    It interprets the bytearray in the little endian format. For a big endian
    bytearray, just do ba.reverse() on the object before passing it in.
    """
    import struct

    if bytesize == 1:
        return bytes[0]

    # Little endian followed by a format character.
    template = "<%c"
    if bytesize == 2:
        fmt = template % "h"
    elif bytesize == 4:
        fmt = template % "i"
    elif bytesize == 4:
        fmt = template % "q"
    else:
        return None

    unpacked = struct.unpack(fmt, str(bytes))
    return unpacked[0]


# ==============================================================
# Get the description of an lldb object or None if not available
# ==============================================================
def get_description(obj, option=None):
    """Calls lldb_obj.GetDescription() and returns a string, or None.

    For SBTarget, SBBreakpointLocation, and SBWatchpoint lldb objects, an extra
    option can be passed in to describe the detailed level of description
    desired:
        o lldb.eDescriptionLevelBrief
        o lldb.eDescriptionLevelFull
        o lldb.eDescriptionLevelVerbose
    """
    method = getattr(obj, "GetDescription")
    if not method:
        return None
    tuple = (lldb.SBTarget, lldb.SBBreakpointLocation, lldb.SBWatchpoint)
    if isinstance(obj, tuple):
        if option is None:
            option = lldb.eDescriptionLevelBrief

    stream = lldb.SBStream()
    if option is None:
        success = method(stream)
    else:
        success = method(stream, option)
    if not success:
        return None
    return stream.GetData()


# =================================================
# Convert some enum value to its string counterpart
# =================================================


def state_type_to_str(enum):
    """Returns the stateType string given an enum."""
    if enum == lldb.eStateInvalid:
        return "invalid"
    elif enum == lldb.eStateUnloaded:
        return "unloaded"
    elif enum == lldb.eStateConnected:
        return "connected"
    elif enum == lldb.eStateAttaching:
        return "attaching"
    elif enum == lldb.eStateLaunching:
        return "launching"
    elif enum == lldb.eStateStopped:
        return "stopped"
    elif enum == lldb.eStateRunning:
        return "running"
    elif enum == lldb.eStateStepping:
        return "stepping"
    elif enum == lldb.eStateCrashed:
        return "crashed"
    elif enum == lldb.eStateDetached:
        return "detached"
    elif enum == lldb.eStateExited:
        return "exited"
    elif enum == lldb.eStateSuspended:
        return "suspended"
    else:
        raise Exception("Unknown StateType enum")


def stop_reason_to_str(enum):
    """Returns the stopReason string given an enum."""
    if enum == lldb.eStopReasonInvalid:
        return "invalid"
    elif enum == lldb.eStopReasonNone:
        return "none"
    elif enum == lldb.eStopReasonTrace:
        return "trace"
    elif enum == lldb.eStopReasonBreakpoint:
        return "breakpoint"
    elif enum == lldb.eStopReasonWatchpoint:
        return "watchpoint"
    elif enum == lldb.eStopReasonSignal:
        return "signal"
    elif enum == lldb.eStopReasonException:
        return "exception"
    elif enum == lldb.eStopReasonPlanComplete:
        return "plancomplete"
    elif enum == lldb.eStopReasonThreadExiting:
        return "threadexiting"
    else:
        raise Exception("Unknown StopReason enum")


def symbol_type_to_str(enum):
    """Returns the symbolType string given an enum."""
    if enum == lldb.eSymbolTypeInvalid:
        return "invalid"
    elif enum == lldb.eSymbolTypeAbsolute:
        return "absolute"
    elif enum == lldb.eSymbolTypeCode:
        return "code"
    elif enum == lldb.eSymbolTypeData:
        return "data"
    elif enum == lldb.eSymbolTypeTrampoline:
        return "trampoline"
    elif enum == lldb.eSymbolTypeRuntime:
        return "runtime"
    elif enum == lldb.eSymbolTypeException:
        return "exception"
    elif enum == lldb.eSymbolTypeSourceFile:
        return "sourcefile"
    elif enum == lldb.eSymbolTypeHeaderFile:
        return "headerfile"
    elif enum == lldb.eSymbolTypeObjectFile:
        return "objectfile"
    elif enum == lldb.eSymbolTypeCommonBlock:
        return "commonblock"
    elif enum == lldb.eSymbolTypeBlock:
        return "block"
    elif enum == lldb.eSymbolTypeLocal:
        return "local"
    elif enum == lldb.eSymbolTypeParam:
        return "param"
    elif enum == lldb.eSymbolTypeVariable:
        return "variable"
    elif enum == lldb.eSymbolTypeVariableType:
        return "variabletype"
    elif enum == lldb.eSymbolTypeLineEntry:
        return "lineentry"
    elif enum == lldb.eSymbolTypeLineHeader:
        return "lineheader"
    elif enum == lldb.eSymbolTypeScopeBegin:
        return "scopebegin"
    elif enum == lldb.eSymbolTypeScopeEnd:
        return "scopeend"
    elif enum == lldb.eSymbolTypeAdditional:
        return "additional"
    elif enum == lldb.eSymbolTypeCompiler:
        return "compiler"
    elif enum == lldb.eSymbolTypeInstrumentation:
        return "instrumentation"
    elif enum == lldb.eSymbolTypeUndefined:
        return "undefined"


def value_type_to_str(enum):
    """Returns the valueType string given an enum."""
    if enum == lldb.eValueTypeInvalid:
        return "invalid"
    elif enum == lldb.eValueTypeVariableGlobal:
        return "global_variable"
    elif enum == lldb.eValueTypeVariableStatic:
        return "static_variable"
    elif enum == lldb.eValueTypeVariableArgument:
        return "argument_variable"
    elif enum == lldb.eValueTypeVariableLocal:
        return "local_variable"
    elif enum == lldb.eValueTypeRegister:
        return "register"
    elif enum == lldb.eValueTypeRegisterSet:
        return "register_set"
    elif enum == lldb.eValueTypeConstResult:
        return "constant_result"
    else:
        raise Exception("Unknown ValueType enum")


# ==================================================
# Get stopped threads due to each stop reason.
# ==================================================


def sort_stopped_threads(
    process,
    breakpoint_threads=None,
    crashed_threads=None,
    watchpoint_threads=None,
    signal_threads=None,
    exiting_threads=None,
    other_threads=None,
):
    """Fills array *_threads with threads stopped for the corresponding stop
    reason.
    """
    for lst in [
        breakpoint_threads,
        watchpoint_threads,
        signal_threads,
        exiting_threads,
        other_threads,
    ]:
        if lst is not None:
            lst[:] = []

    for thread in process:
        dispatched = False
        for reason, list in [
            (lldb.eStopReasonBreakpoint, breakpoint_threads),
            (lldb.eStopReasonException, crashed_threads),
            (lldb.eStopReasonWatchpoint, watchpoint_threads),
            (lldb.eStopReasonSignal, signal_threads),
            (lldb.eStopReasonThreadExiting, exiting_threads),
            (None, other_threads),
        ]:
            if not dispatched and list is not None:
                if thread.GetStopReason() == reason or reason is None:
                    list.append(thread)
                    dispatched = True


# ==================================================
# Utility functions for setting breakpoints
# ==================================================


def run_break_set_by_file_and_line(
    test,
    file_name,
    line_number,
    extra_options=None,
    num_expected_locations=1,
    loc_exact=False,
    module_name=None,
):
    """Set a breakpoint by file and line, returning the breakpoint number.

    If extra_options is not None, then we append it to the breakpoint set command.

    If num_expected_locations is -1 we check that we got AT LEAST one location, otherwise we check that num_expected_locations equals the number of locations.

    If loc_exact is true, we check that there is one location, and that location must be at the input file and line number.
    """

    if file_name is None:
        command = "breakpoint set -l %d" % (line_number)
    else:
        command = 'breakpoint set -f "%s" -l %d' % (file_name, line_number)

    if module_name:
        command += " --shlib '%s'" % (module_name)

    if extra_options:
        command += " " + extra_options

    break_results = run_break_set_command(test, command)

    if num_expected_locations == 1 and loc_exact:
        check_breakpoint_result(
            test,
            break_results,
            num_locations=num_expected_locations,
            file_name=file_name,
            line_number=line_number,
            module_name=module_name,
        )
    else:
        check_breakpoint_result(
            test, break_results, num_locations=num_expected_locations
        )

    return get_bpno_from_match(break_results)


def run_break_set_by_symbol(
    test,
    symbol,
    extra_options=None,
    num_expected_locations=-1,
    sym_exact=False,
    module_name=None,
):
    """Set a breakpoint by symbol name.  Common options are the same as run_break_set_by_file_and_line.

    If sym_exact is true, then the output symbol must match the input exactly, otherwise we do a substring match.
    """
    command = 'breakpoint set -n "%s"' % (symbol)

    if module_name:
        command += " --shlib '%s'" % (module_name)

    if extra_options:
        command += " " + extra_options

    break_results = run_break_set_command(test, command)

    if num_expected_locations == 1 and sym_exact:
        check_breakpoint_result(
            test,
            break_results,
            num_locations=num_expected_locations,
            symbol_name=symbol,
            module_name=module_name,
        )
    else:
        check_breakpoint_result(
            test, break_results, num_locations=num_expected_locations
        )

    return get_bpno_from_match(break_results)


def run_break_set_by_selector(
    test, selector, extra_options=None, num_expected_locations=-1, module_name=None
):
    """Set a breakpoint by selector.  Common options are the same as run_break_set_by_file_and_line."""

    command = 'breakpoint set -S "%s"' % (selector)

    if module_name:
        command += ' --shlib "%s"' % (module_name)

    if extra_options:
        command += " " + extra_options

    break_results = run_break_set_command(test, command)

    if num_expected_locations == 1:
        check_breakpoint_result(
            test,
            break_results,
            num_locations=num_expected_locations,
            symbol_name=selector,
            symbol_match_exact=False,
            module_name=module_name,
        )
    else:
        check_breakpoint_result(
            test, break_results, num_locations=num_expected_locations
        )

    return get_bpno_from_match(break_results)


def run_break_set_by_regexp(
    test, regexp, extra_options=None, num_expected_locations=-1
):
    """Set a breakpoint by regular expression match on symbol name.  Common options are the same as run_break_set_by_file_and_line."""

    command = 'breakpoint set -r "%s"' % (regexp)
    if extra_options:
        command += " " + extra_options

    break_results = run_break_set_command(test, command)

    check_breakpoint_result(test, break_results, num_locations=num_expected_locations)

    return get_bpno_from_match(break_results)


def run_break_set_by_source_regexp(
    test, regexp, extra_options=None, num_expected_locations=-1
):
    """Set a breakpoint by source regular expression.  Common options are the same as run_break_set_by_file_and_line."""
    command = 'breakpoint set -p "%s"' % (regexp)
    if extra_options:
        command += " " + extra_options

    break_results = run_break_set_command(test, command)

    check_breakpoint_result(test, break_results, num_locations=num_expected_locations)

    return get_bpno_from_match(break_results)


def run_break_set_command(test, command):
    """Run the command passed in - it must be some break set variant - and analyze the result.
    Returns a dictionary of information gleaned from the command-line results.
    Will assert if the breakpoint setting fails altogether.

    Dictionary will contain:
        bpno          - breakpoint of the newly created breakpoint, -1 on error.
        num_locations - number of locations set for the breakpoint.

    If there is only one location, the dictionary MAY contain:
        file          - source file name
        line_no       - source line number
        symbol        - symbol name
        inline_symbol - inlined symbol name
        offset        - offset from the original symbol
        module        - module
        address       - address at which the breakpoint was set."""

    patterns = [
        r"^Breakpoint (?P<bpno>[0-9]+): (?P<num_locations>[0-9]+) locations\.$",
        r"^Breakpoint (?P<bpno>[0-9]+): (?P<num_locations>no) locations \(pending\)\.",
        r"^Breakpoint (?P<bpno>[0-9]+): where = (?P<module>.*)`(?P<symbol>[+\-]{0,1}[^+]+)( \+ (?P<offset>[0-9]+)){0,1}( \[inlined\] (?P<inline_symbol>.*)){0,1} at (?P<file>[^:]+):(?P<line_no>[0-9]+), address = (?P<address>0x[0-9a-fA-F]+)$",
        r"^Breakpoint (?P<bpno>[0-9]+): where = (?P<module>.*)`(?P<symbol>.*)( \+ (?P<offset>[0-9]+)){0,1}, address = (?P<address>0x[0-9a-fA-F]+)$",
    ]
    match_object = test.match(command, patterns)
    break_results = match_object.groupdict()

    # We always insert the breakpoint number, setting it to -1 if we couldn't find it
    # Also, make sure it gets stored as an integer.
    if not "bpno" in break_results:
        break_results["bpno"] = -1
    else:
        break_results["bpno"] = int(break_results["bpno"])

    # We always insert the number of locations
    # If ONE location is set for the breakpoint, then the output doesn't mention locations, but it has to be 1...
    # We also make sure it is an integer.

    if not "num_locations" in break_results:
        num_locations = 1
    else:
        num_locations = break_results["num_locations"]
        if num_locations == "no":
            num_locations = 0
        else:
            num_locations = int(break_results["num_locations"])

    break_results["num_locations"] = num_locations

    if "line_no" in break_results:
        break_results["line_no"] = int(break_results["line_no"])

    return break_results


def get_bpno_from_match(break_results):
    return int(break_results["bpno"])


def check_breakpoint_result(
    test,
    break_results,
    file_name=None,
    line_number=-1,
    symbol_name=None,
    symbol_match_exact=True,
    module_name=None,
    offset=-1,
    num_locations=-1,
):
    out_num_locations = break_results["num_locations"]

    if num_locations == -1:
        test.assertTrue(
            out_num_locations > 0, "Expecting one or more locations, got none."
        )
    else:
        test.assertTrue(
            num_locations == out_num_locations,
            "Expecting %d locations, got %d." % (num_locations, out_num_locations),
        )

    if file_name:
        out_file_name = ""
        if "file" in break_results:
            out_file_name = break_results["file"]
        test.assertTrue(
            file_name == out_file_name,
            "Breakpoint file name '%s' doesn't match resultant name '%s'."
            % (file_name, out_file_name),
        )

    if line_number != -1:
        out_file_line = -1
        if "line_no" in break_results:
            out_line_number = break_results["line_no"]

        test.assertTrue(
            line_number == out_line_number,
            "Breakpoint line number %s doesn't match resultant line %s."
            % (line_number, out_line_number),
        )

    if symbol_name:
        out_symbol_name = ""
        # Look first for the inlined symbol name, otherwise use the symbol
        # name:
        if "inline_symbol" in break_results and break_results["inline_symbol"]:
            out_symbol_name = break_results["inline_symbol"]
        elif "symbol" in break_results:
            out_symbol_name = break_results["symbol"]

        if symbol_match_exact:
            test.assertTrue(
                symbol_name == out_symbol_name,
                "Symbol name '%s' doesn't match resultant symbol '%s'."
                % (symbol_name, out_symbol_name),
            )
        else:
            test.assertTrue(
                out_symbol_name.find(symbol_name) != -1,
                "Symbol name '%s' isn't in resultant symbol '%s'."
                % (symbol_name, out_symbol_name),
            )

    if module_name:
        out_nodule_name = None
        if "module" in break_results:
            out_module_name = break_results["module"]

        test.assertTrue(
            module_name.find(out_module_name) != -1,
            "Symbol module name '%s' isn't in expected module name '%s'."
            % (out_module_name, module_name),
        )


# ==================================================
# Utility functions related to Threads and Processes
# ==================================================


def get_stopped_threads(process, reason):
    """Returns the thread(s) with the specified stop reason in a list.

    The list can be empty if no such thread exists.
    """
    threads = []
    for t in process:
        if t.GetStopReason() == reason:
            threads.append(t)
    return threads


def get_stopped_thread(process, reason):
    """A convenience function which returns the first thread with the given stop
    reason or None.

    Example usages:

    1. Get the stopped thread due to a breakpoint condition

    ...
        from lldbutil import get_stopped_thread
        thread = get_stopped_thread(process, lldb.eStopReasonPlanComplete)
        self.assertTrue(thread.IsValid(), "There should be a thread stopped due to breakpoint condition")
    ...

    2. Get the thread stopped due to a breakpoint

    ...
        from lldbutil import get_stopped_thread
        thread = get_stopped_thread(process, lldb.eStopReasonBreakpoint)
        self.assertTrue(thread.IsValid(), "There should be a thread stopped due to breakpoint")
    ...

    """
    threads = get_stopped_threads(process, reason)
    if len(threads) == 0:
        return None
    return threads[0]


def get_threads_stopped_at_breakpoint(process, bkpt):
    """For a stopped process returns the thread stopped at the breakpoint passed in bkpt"""
    stopped_threads = []
    threads = []

    stopped_threads = get_stopped_threads(process, lldb.eStopReasonBreakpoint)

    if len(stopped_threads) == 0:
        return threads

    for thread in stopped_threads:
        # Make sure we've hit our breakpoint...
        break_id = thread.GetStopReasonDataAtIndex(0)
        if break_id == bkpt.GetID():
            threads.append(thread)

    return threads


def continue_to_breakpoint(process, bkpt):
    """Continues the process, if it stops, returns the threads stopped at bkpt; otherwise, returns None"""
    process.Continue()
    if process.GetState() != lldb.eStateStopped:
        return None
    else:
        return get_threads_stopped_at_breakpoint(process, bkpt)


def get_caller_symbol(thread):
    """
    Returns the symbol name for the call site of the leaf function.
    """
    depth = thread.GetNumFrames()
    if depth <= 1:
        return None
    caller = thread.GetFrameAtIndex(1).GetSymbol()
    if caller:
        return caller.GetName()
    else:
        return None


def get_function_names(thread):
    """
    Returns a sequence of function names from the stack frames of this thread.
    """

    def GetFuncName(i):
        return thread.GetFrameAtIndex(i).GetFunctionName()

    return [GetFuncName(i) for i in range(thread.GetNumFrames())]


def get_symbol_names(thread):
    """
    Returns a sequence of symbols for this thread.
    """

    def GetSymbol(i):
        return thread.GetFrameAtIndex(i).GetSymbol().GetName()

    return [GetSymbol(i) for i in range(thread.GetNumFrames())]


def get_pc_addresses(thread):
    """
    Returns a sequence of pc addresses for this thread.
    """

    def GetPCAddress(i):
        return thread.GetFrameAtIndex(i).GetPCAddress()

    return [GetPCAddress(i) for i in range(thread.GetNumFrames())]


def get_filenames(thread):
    """
    Returns a sequence of file names from the stack frames of this thread.
    """

    def GetFilename(i):
        return thread.GetFrameAtIndex(i).GetLineEntry().GetFileSpec().GetFilename()

    return [GetFilename(i) for i in range(thread.GetNumFrames())]


def get_line_numbers(thread):
    """
    Returns a sequence of line numbers from the stack frames of this thread.
    """

    def GetLineNumber(i):
        return thread.GetFrameAtIndex(i).GetLineEntry().GetLine()

    return [GetLineNumber(i) for i in range(thread.GetNumFrames())]


def get_module_names(thread):
    """
    Returns a sequence of module names from the stack frames of this thread.
    """

    def GetModuleName(i):
        return thread.GetFrameAtIndex(i).GetModule().GetFileSpec().GetFilename()

    return [GetModuleName(i) for i in range(thread.GetNumFrames())]


def get_stack_frames(thread):
    """
    Returns a sequence of stack frames for this thread.
    """

    def GetStackFrame(i):
        return thread.GetFrameAtIndex(i)

    return [GetStackFrame(i) for i in range(thread.GetNumFrames())]


def print_stacktrace(thread, string_buffer=False):
    """Prints a simple stack trace of this thread."""

    output = io.StringIO() if string_buffer else sys.stdout
    target = thread.GetProcess().GetTarget()

    depth = thread.GetNumFrames()

    mods = get_module_names(thread)
    funcs = get_function_names(thread)
    symbols = get_symbol_names(thread)
    files = get_filenames(thread)
    lines = get_line_numbers(thread)
    addrs = get_pc_addresses(thread)

    if thread.GetStopReason() != lldb.eStopReasonInvalid:
        desc = "stop reason=" + stop_reason_to_str(thread.GetStopReason())
    else:
        desc = ""
    print(
        "Stack trace for thread id={0:#x} name={1} queue={2} ".format(
            thread.GetThreadID(), thread.GetName(), thread.GetQueueName()
        )
        + desc,
        file=output,
    )

    for i in range(depth):
        frame = thread.GetFrameAtIndex(i)
        function = frame.GetFunction()

        load_addr = addrs[i].GetLoadAddress(target)
        if not function:
            file_addr = addrs[i].GetFileAddress()
            start_addr = frame.GetSymbol().GetStartAddress().GetFileAddress()
            symbol_offset = file_addr - start_addr
            print(
                "  frame #{num}: {addr:#016x} {mod}`{symbol} + {offset}".format(
                    num=i,
                    addr=load_addr,
                    mod=mods[i],
                    symbol=symbols[i],
                    offset=symbol_offset,
                ),
                file=output,
            )
        else:
            print(
                "  frame #{num}: {addr:#016x} {mod}`{func} at {file}:{line} {args}".format(
                    num=i,
                    addr=load_addr,
                    mod=mods[i],
                    func="%s [inlined]" % funcs[i] if frame.IsInlined() else funcs[i],
                    file=files[i],
                    line=lines[i],
                    args=get_args_as_string(frame, showFuncName=False)
                    if not frame.IsInlined()
                    else "()",
                ),
                file=output,
            )

    if string_buffer:
        return output.getvalue()


def print_stacktraces(process, string_buffer=False):
    """Prints the stack traces of all the threads."""

    output = io.StringIO() if string_buffer else sys.stdout

    print("Stack traces for " + str(process), file=output)

    for thread in process:
        print(print_stacktrace(thread, string_buffer=True), file=output)

    if string_buffer:
        return output.getvalue()


# ===================================
# Utility functions related to Frames
# ===================================


def get_parent_frame(frame):
    """
    Returns the parent frame of the input frame object; None if not available.
    """
    thread = frame.GetThread()
    parent_found = False
    for f in thread:
        if parent_found:
            return f
        if f.GetFrameID() == frame.GetFrameID():
            parent_found = True

    # If we reach here, no parent has been found, return None.
    return None


def get_args_as_string(frame, showFuncName=True):
    """
    Returns the args of the input frame object as a string.
    """
    # arguments     => True
    # locals        => False
    # statics       => False
    # in_scope_only => True
    vars = frame.GetVariables(True, False, False, True)  # type of SBValueList
    args = []  # list of strings
    for var in vars:
        args.append("(%s)%s=%s" % (var.GetTypeName(), var.GetName(), var.GetValue()))
    if frame.GetFunction():
        name = frame.GetFunction().GetName()
    elif frame.GetSymbol():
        name = frame.GetSymbol().GetName()
    else:
        name = ""
    if showFuncName:
        return "%s(%s)" % (name, ", ".join(args))
    else:
        return "(%s)" % (", ".join(args))


def print_registers(frame, string_buffer=False):
    """Prints all the register sets of the frame."""

    output = io.StringIO() if string_buffer else sys.stdout

    print("Register sets for " + str(frame), file=output)

    registerSet = frame.GetRegisters()  # Return type of SBValueList.
    print(
        "Frame registers (size of register set = %d):" % registerSet.GetSize(),
        file=output,
    )
    for value in registerSet:
        # print >> output, value
        print(
            "%s (number of children = %d):" % (value.GetName(), value.GetNumChildren()),
            file=output,
        )
        for child in value:
            print(
                "Name: %s, Value: %s" % (child.GetName(), child.GetValue()), file=output
            )

    if string_buffer:
        return output.getvalue()


def get_registers(frame, kind):
    """Returns the registers given the frame and the kind of registers desired.

    Returns None if there's no such kind.
    """
    registerSet = frame.GetRegisters()  # Return type of SBValueList.
    for value in registerSet:
        if kind.lower() in value.GetName().lower():
            return value

    return None


def get_GPRs(frame):
    """Returns the general purpose registers of the frame as an SBValue.

    The returned SBValue object is iterable.  An example:
        ...
        from lldbutil import get_GPRs
        regs = get_GPRs(frame)
        for reg in regs:
            print "%s => %s" % (reg.GetName(), reg.GetValue())
        ...
    """
    return get_registers(frame, "general purpose")


def get_FPRs(frame):
    """Returns the floating point registers of the frame as an SBValue.

    The returned SBValue object is iterable.  An example:
        ...
        from lldbutil import get_FPRs
        regs = get_FPRs(frame)
        for reg in regs:
            print "%s => %s" % (reg.GetName(), reg.GetValue())
        ...
    """
    return get_registers(frame, "floating point")


def get_ESRs(frame):
    """Returns the exception state registers of the frame as an SBValue.

    The returned SBValue object is iterable.  An example:
        ...
        from lldbutil import get_ESRs
        regs = get_ESRs(frame)
        for reg in regs:
            print "%s => %s" % (reg.GetName(), reg.GetValue())
        ...
    """
    return get_registers(frame, "exception state")


# ======================================
# Utility classes/functions for SBValues
# ======================================


class BasicFormatter(object):
    """The basic formatter inspects the value object and prints the value."""

    def format(self, value, buffer=None, indent=0):
        if not buffer:
            output = io.StringIO()
        else:
            output = buffer
        # If there is a summary, it suffices.
        val = value.GetSummary()
        # Otherwise, get the value.
        if val is None:
            val = value.GetValue()
        if val is None and value.GetNumChildren() > 0:
            val = "%s (location)" % value.GetLocation()
        print(
            "{indentation}({type}) {name} = {value}".format(
                indentation=" " * indent,
                type=value.GetTypeName(),
                name=value.GetName(),
                value=val,
            ),
            file=output,
        )
        return output.getvalue()


class ChildVisitingFormatter(BasicFormatter):
    """The child visiting formatter prints the value and its immediate children.

    The constructor takes a keyword arg: indent_child, which defaults to 2.
    """

    def __init__(self, indent_child=2):
        """Default indentation of 2 SPC's for the children."""
        self.cindent = indent_child

    def format(self, value, buffer=None):
        if not buffer:
            output = io.StringIO()
        else:
            output = buffer

        BasicFormatter.format(self, value, buffer=output)
        for child in value:
            BasicFormatter.format(self, child, buffer=output, indent=self.cindent)

        return output.getvalue()


class RecursiveDecentFormatter(BasicFormatter):
    """The recursive decent formatter prints the value and the decendents.

    The constructor takes two keyword args: indent_level, which defaults to 0,
    and indent_child, which defaults to 2.  The current indentation level is
    determined by indent_level, while the immediate children has an additional
    indentation by inden_child.
    """

    def __init__(self, indent_level=0, indent_child=2):
        self.lindent = indent_level
        self.cindent = indent_child

    def format(self, value, buffer=None):
        if not buffer:
            output = io.StringIO()
        else:
            output = buffer

        BasicFormatter.format(self, value, buffer=output, indent=self.lindent)
        new_indent = self.lindent + self.cindent
        for child in value:
            if child.GetSummary() is not None:
                BasicFormatter.format(self, child, buffer=output, indent=new_indent)
            else:
                if child.GetNumChildren() > 0:
                    rdf = RecursiveDecentFormatter(indent_level=new_indent)
                    rdf.format(child, buffer=output)
                else:
                    BasicFormatter.format(self, child, buffer=output, indent=new_indent)

        return output.getvalue()
