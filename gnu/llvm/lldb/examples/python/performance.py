#!/usr/bin/env python

# ----------------------------------------------------------------------
# Be sure to add the python path that points to the LLDB shared library.
# On MacOSX csh, tcsh:
#   setenv PYTHONPATH /Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework/Resources/Python
# On MacOSX sh, bash:
#   export PYTHONPATH=/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework/Resources/Python
# ----------------------------------------------------------------------

import optparse
import os
import platform
import re
import resource
import sys
import subprocess
import time
import types

# ----------------------------------------------------------------------
# Code that auto imports LLDB
# ----------------------------------------------------------------------
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


class Timer:
    def __enter__(self):
        self.start = time.clock()
        return self

    def __exit__(self, *args):
        self.end = time.clock()
        self.interval = self.end - self.start


class Action(object):
    """Class that encapsulates actions to take when a thread stops for a reason."""

    def __init__(self, callback=None, callback_owner=None):
        self.callback = callback
        self.callback_owner = callback_owner

    def ThreadStopped(self, thread):
        assert (
            False
        ), "performance.Action.ThreadStopped(self, thread) must be overridden in a subclass"


class PlanCompleteAction(Action):
    def __init__(self, callback=None, callback_owner=None):
        Action.__init__(self, callback, callback_owner)

    def ThreadStopped(self, thread):
        if thread.GetStopReason() == lldb.eStopReasonPlanComplete:
            if self.callback:
                if self.callback_owner:
                    self.callback(self.callback_owner, thread)
                else:
                    self.callback(thread)
            return True
        return False


class BreakpointAction(Action):
    def __init__(
        self,
        callback=None,
        callback_owner=None,
        name=None,
        module=None,
        file=None,
        line=None,
        breakpoint=None,
    ):
        Action.__init__(self, callback, callback_owner)
        self.modules = lldb.SBFileSpecList()
        self.files = lldb.SBFileSpecList()
        self.breakpoints = list()
        # "module" can be a list or a string
        if breakpoint:
            self.breakpoints.append(breakpoint)
        else:
            if module:
                if isinstance(module, types.ListType):
                    for module_path in module:
                        self.modules.Append(lldb.SBFileSpec(module_path, False))
                elif isinstance(module, types.StringTypes):
                    self.modules.Append(lldb.SBFileSpec(module, False))
            if name:
                # "file" can be a list or a string
                if file:
                    if isinstance(file, types.ListType):
                        self.files = lldb.SBFileSpecList()
                        for f in file:
                            self.files.Append(lldb.SBFileSpec(f, False))
                    elif isinstance(file, types.StringTypes):
                        self.files.Append(lldb.SBFileSpec(file, False))
                self.breakpoints.append(
                    self.target.BreakpointCreateByName(name, self.modules, self.files)
                )
            elif file and line:
                self.breakpoints.append(
                    self.target.BreakpointCreateByLocation(file, line)
                )

    def ThreadStopped(self, thread):
        if thread.GetStopReason() == lldb.eStopReasonBreakpoint:
            for bp in self.breakpoints:
                if bp.GetID() == thread.GetStopReasonDataAtIndex(0):
                    if self.callback:
                        if self.callback_owner:
                            self.callback(self.callback_owner, thread)
                        else:
                            self.callback(thread)
                    return True
        return False


class TestCase:
    """Class that aids in running performance tests."""

    def __init__(self):
        self.verbose = False
        self.debugger = lldb.SBDebugger.Create()
        self.target = None
        self.process = None
        self.thread = None
        self.launch_info = None
        self.done = False
        self.listener = self.debugger.GetListener()
        self.user_actions = list()
        self.builtin_actions = list()
        self.bp_id_to_dict = dict()

    def Setup(self, args):
        self.launch_info = lldb.SBLaunchInfo(args)

    def Run(self, args):
        assert False, "performance.TestCase.Run(self, args) must be subclassed"

    def Launch(self):
        if self.target:
            error = lldb.SBError()
            self.process = self.target.Launch(self.launch_info, error)
            if not error.Success():
                print("error: %s" % error.GetCString())
            if self.process:
                self.process.GetBroadcaster().AddListener(
                    self.listener,
                    lldb.SBProcess.eBroadcastBitStateChanged
                    | lldb.SBProcess.eBroadcastBitInterrupt,
                )
                return True
        return False

    def WaitForNextProcessEvent(self):
        event = None
        if self.process:
            while event is None:
                process_event = lldb.SBEvent()
                if self.listener.WaitForEvent(lldb.UINT32_MAX, process_event):
                    state = lldb.SBProcess.GetStateFromEvent(process_event)
                    if self.verbose:
                        print("event = %s" % (lldb.SBDebugger.StateAsCString(state)))
                    if lldb.SBProcess.GetRestartedFromEvent(process_event):
                        continue
                    if (
                        state == lldb.eStateInvalid
                        or state == lldb.eStateDetached
                        or state == lldb.eStateCrashed
                        or state == lldb.eStateUnloaded
                        or state == lldb.eStateExited
                    ):
                        event = process_event
                        self.done = True
                    elif (
                        state == lldb.eStateConnected
                        or state == lldb.eStateAttaching
                        or state == lldb.eStateLaunching
                        or state == lldb.eStateRunning
                        or state == lldb.eStateStepping
                        or state == lldb.eStateSuspended
                    ):
                        continue
                    elif state == lldb.eStateStopped:
                        event = process_event
                        call_test_step = True
                        fatal = False
                        selected_thread = False
                        for thread in self.process:
                            frame = thread.GetFrameAtIndex(0)
                            select_thread = False

                            stop_reason = thread.GetStopReason()
                            if self.verbose:
                                print(
                                    "tid = %#x pc = %#x "
                                    % (thread.GetThreadID(), frame.GetPC()),
                                    end=" ",
                                )
                            if stop_reason == lldb.eStopReasonNone:
                                if self.verbose:
                                    print("none")
                            elif stop_reason == lldb.eStopReasonTrace:
                                select_thread = True
                                if self.verbose:
                                    print("trace")
                            elif stop_reason == lldb.eStopReasonPlanComplete:
                                select_thread = True
                                if self.verbose:
                                    print("plan complete")
                            elif stop_reason == lldb.eStopReasonThreadExiting:
                                if self.verbose:
                                    print("thread exiting")
                            elif stop_reason == lldb.eStopReasonExec:
                                if self.verbose:
                                    print("exec")
                            elif stop_reason == lldb.eStopReasonInvalid:
                                if self.verbose:
                                    print("invalid")
                            elif stop_reason == lldb.eStopReasonException:
                                select_thread = True
                                if self.verbose:
                                    print("exception")
                                fatal = True
                            elif stop_reason == lldb.eStopReasonBreakpoint:
                                select_thread = True
                                bp_id = thread.GetStopReasonDataAtIndex(0)
                                bp_loc_id = thread.GetStopReasonDataAtIndex(1)
                                if self.verbose:
                                    print("breakpoint id = %d.%d" % (bp_id, bp_loc_id))
                            elif stop_reason == lldb.eStopReasonWatchpoint:
                                select_thread = True
                                if self.verbose:
                                    print(
                                        "watchpoint id = %d"
                                        % (thread.GetStopReasonDataAtIndex(0))
                                    )
                            elif stop_reason == lldb.eStopReasonSignal:
                                select_thread = True
                                if self.verbose:
                                    print(
                                        "signal %d"
                                        % (thread.GetStopReasonDataAtIndex(0))
                                    )
                            elif stop_reason == lldb.eStopReasonFork:
                                if self.verbose:
                                    print(
                                        "fork pid = %d"
                                        % (thread.GetStopReasonDataAtIndex(0))
                                    )
                            elif stop_reason == lldb.eStopReasonVFork:
                                if self.verbose:
                                    print(
                                        "vfork pid = %d"
                                        % (thread.GetStopReasonDataAtIndex(0))
                                    )
                            elif stop_reason == lldb.eStopReasonVForkDone:
                                if self.verbose:
                                    print("vfork done")

                            if select_thread and not selected_thread:
                                self.thread = thread
                                selected_thread = self.process.SetSelectedThread(thread)

                            for action in self.user_actions:
                                action.ThreadStopped(thread)

                        if fatal:
                            # if self.verbose:
                            #     Xcode.RunCommand(self.debugger,"bt all",true)
                            sys.exit(1)
        return event


class Measurement:
    """A class that encapsulates a measurement"""

    def __init__(self):
        object.__init__(self)

    def Measure(self):
        assert False, "performance.Measurement.Measure() must be subclassed"


class MemoryMeasurement(Measurement):
    """A class that can measure memory statistics for a process."""

    def __init__(self, pid):
        Measurement.__init__(self)
        self.pid = pid
        self.stats = [
            "rprvt",
            "rshrd",
            "rsize",
            "vsize",
            "vprvt",
            "kprvt",
            "kshrd",
            "faults",
            "cow",
            "pageins",
        ]
        self.command = "top -l 1 -pid %u -stats %s" % (self.pid, ",".join(self.stats))
        self.value = dict()

    def Measure(self):
        output = subprocess.getoutput(self.command).split("\n")[-1]
        values = re.split("[-+\s]+", output)
        for idx, stat in enumerate(values):
            multiplier = 1
            if stat:
                if stat[-1] == "K":
                    multiplier = 1024
                    stat = stat[:-1]
                elif stat[-1] == "M":
                    multiplier = 1024 * 1024
                    stat = stat[:-1]
                elif stat[-1] == "G":
                    multiplier = 1024 * 1024 * 1024
                elif stat[-1] == "T":
                    multiplier = 1024 * 1024 * 1024 * 1024
                    stat = stat[:-1]
                self.value[self.stats[idx]] = int(stat) * multiplier

    def __str__(self):
        """Dump the MemoryMeasurement current value"""
        s = ""
        for key in self.value.keys():
            if s:
                s += "\n"
            s += "%8s = %s" % (key, self.value[key])
        return s


class TesterTestCase(TestCase):
    def __init__(self):
        TestCase.__init__(self)
        self.verbose = True
        self.num_steps = 5

    def BreakpointHit(self, thread):
        bp_id = thread.GetStopReasonDataAtIndex(0)
        loc_id = thread.GetStopReasonDataAtIndex(1)
        print(
            "Breakpoint %i.%i hit: %s"
            % (bp_id, loc_id, thread.process.target.FindBreakpointByID(bp_id))
        )
        thread.StepOver()

    def PlanComplete(self, thread):
        if self.num_steps > 0:
            thread.StepOver()
            self.num_steps = self.num_steps - 1
        else:
            thread.process.Kill()

    def Run(self, args):
        self.Setup(args)
        with Timer() as total_time:
            self.target = self.debugger.CreateTarget(args[0])
            if self.target:
                with Timer() as breakpoint_timer:
                    bp = self.target.BreakpointCreateByName("main")
                print("Breakpoint time = %.03f sec." % breakpoint_timer.interval)

                self.user_actions.append(
                    BreakpointAction(
                        breakpoint=bp,
                        callback=TesterTestCase.BreakpointHit,
                        callback_owner=self,
                    )
                )
                self.user_actions.append(
                    PlanCompleteAction(
                        callback=TesterTestCase.PlanComplete, callback_owner=self
                    )
                )

                if self.Launch():
                    while not self.done:
                        self.WaitForNextProcessEvent()
                else:
                    print("error: failed to launch process")
            else:
                print("error: failed to create target with '%s'" % (args[0]))
        print("Total time = %.03f sec." % total_time.interval)


if __name__ == "__main__":
    lldb.SBDebugger.Initialize()
    test = TesterTestCase()
    test.Run(sys.argv[1:])
    mem = MemoryMeasurement(os.getpid())
    mem.Measure()
    print(str(mem))
    lldb.SBDebugger.Terminate()
    # print "sleeeping for 100 seconds"
    # time.sleep(100)
