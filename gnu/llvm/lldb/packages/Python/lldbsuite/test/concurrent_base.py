"""
A stress-test of sorts for LLDB's handling of threads in the inferior.

This test sets a breakpoint in the main thread where test parameters (numbers of
threads) can be adjusted, runs the inferior to that point, and modifies the
locals that control the event thread counts. This test also sets a breakpoint in
breakpoint_func (the function executed by each 'breakpoint' thread) and a
watchpoint on a global modified in watchpoint_func. The inferior is continued
until exit or a crash takes place, and the number of events seen by LLDB is
verified to match the expected number of events.
"""


import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class ConcurrentEventsBase(TestBase):
    # Concurrency is the primary test factor here, not debug info variants.
    NO_DEBUG_INFO_TESTCASE = True

    def setUp(self):
        # Call super's setUp().
        super(ConcurrentEventsBase, self).setUp()
        # Find the line number for our breakpoint.
        self.filename = "main.cpp"
        self.thread_breakpoint_line = line_number(
            self.filename, "// Set breakpoint here"
        )
        self.setup_breakpoint_line = line_number(
            self.filename, "// Break here and adjust num"
        )
        self.finish_breakpoint_line = line_number(
            self.filename, "// Break here and verify one thread is active"
        )

    def describe_threads(self):
        ret = []
        for x in self.inferior_process:
            id = x.GetIndexID()
            reason = x.GetStopReason()
            status = "stopped" if x.IsStopped() else "running"
            reason_str = lldbutil.stop_reason_to_str(reason)
            if reason == lldb.eStopReasonBreakpoint:
                bpid = x.GetStopReasonDataAtIndex(0)
                bp = self.inferior_target.FindBreakpointByID(bpid)
                reason_str = "%s hit %d times" % (
                    lldbutil.get_description(bp),
                    bp.GetHitCount(),
                )
            elif reason == lldb.eStopReasonWatchpoint:
                watchid = x.GetStopReasonDataAtIndex(0)
                watch = self.inferior_target.FindWatchpointByID(watchid)
                reason_str = "%s hit %d times" % (
                    lldbutil.get_description(watch),
                    watch.GetHitCount(),
                )
            elif reason == lldb.eStopReasonSignal:
                signals = self.inferior_process.GetUnixSignals()
                signal_name = signals.GetSignalAsCString(x.GetStopReasonDataAtIndex(0))
                reason_str = "signal %s" % signal_name

            location = "\t".join(
                [
                    lldbutil.get_description(x.GetFrameAtIndex(i))
                    for i in range(x.GetNumFrames())
                ]
            )
            ret.append(
                "thread %d %s due to %s at\n\t%s" % (id, status, reason_str, location)
            )
        return ret

    def add_breakpoint(self, line, descriptions):
        """Adds a breakpoint at self.filename:line and appends its description to descriptions, and
        returns the LLDB SBBreakpoint object.
        """

        bpno = lldbutil.run_break_set_by_file_and_line(
            self, self.filename, line, num_expected_locations=-1
        )
        bp = self.inferior_target.FindBreakpointByID(bpno)
        descriptions.append(": file = 'main.cpp', line = %d" % line)
        return bp

    def inferior_done(self):
        """Returns true if the inferior is done executing all the event threads (and is stopped at self.finish_breakpoint,
        or has terminated execution.
        """
        return (
            self.finish_breakpoint.GetHitCount() > 0
            or self.crash_count > 0
            or self.inferior_process.GetState() == lldb.eStateExited
        )

    def count_signaled_threads(self):
        count = 0
        for thread in self.inferior_process:
            if (
                thread.GetStopReason() == lldb.eStopReasonSignal
                and thread.GetStopReasonDataAtIndex(0)
                == self.inferior_process.GetUnixSignals().GetSignalNumberFromName(
                    "SIGUSR1"
                )
            ):
                count += 1
        return count

    def do_thread_actions(
        self,
        num_breakpoint_threads=0,
        num_signal_threads=0,
        num_watchpoint_threads=0,
        num_crash_threads=0,
        num_delay_breakpoint_threads=0,
        num_delay_signal_threads=0,
        num_delay_watchpoint_threads=0,
        num_delay_crash_threads=0,
    ):
        """Sets a breakpoint in the main thread where test parameters (numbers of threads) can be adjusted, runs the inferior
        to that point, and modifies the locals that control the event thread counts. Also sets a breakpoint in
        breakpoint_func (the function executed by each 'breakpoint' thread) and a watchpoint on a global modified in
        watchpoint_func. The inferior is continued until exit or a crash takes place, and the number of events seen by LLDB
        is verified to match the expected number of events.
        """
        exe = self.getBuildArtifact("a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Get the target
        self.inferior_target = self.dbg.GetSelectedTarget()

        expected_bps = []

        # Initialize all the breakpoints (main thread/aux thread)
        self.setup_breakpoint = self.add_breakpoint(
            self.setup_breakpoint_line, expected_bps
        )
        self.finish_breakpoint = self.add_breakpoint(
            self.finish_breakpoint_line, expected_bps
        )

        # Set the thread breakpoint
        if num_breakpoint_threads + num_delay_breakpoint_threads > 0:
            self.thread_breakpoint = self.add_breakpoint(
                self.thread_breakpoint_line, expected_bps
            )

        # Verify breakpoints
        self.expect(
            "breakpoint list -f",
            "Breakpoint locations shown correctly",
            substrs=expected_bps,
        )

        # Run the program.
        self.runCmd("run", RUN_SUCCEEDED)

        # Check we are at line self.setup_breakpoint
        self.expect(
            "thread backtrace",
            STOPPED_DUE_TO_BREAKPOINT,
            substrs=["stop reason = breakpoint 1."],
        )

        # Initialize the (single) watchpoint on the global variable (g_watchme)
        if num_watchpoint_threads + num_delay_watchpoint_threads > 0:
            # The concurrent tests have multiple threads modifying a variable
            # with the same value.  The default "modify" style watchpoint will
            # only report this as 1 hit for all threads, because they all wrote
            # the same value.  The testsuite needs "write" style watchpoints to
            # get the correct number of hits reported.
            self.runCmd("watchpoint set variable -w write g_watchme")
            for w in self.inferior_target.watchpoint_iter():
                self.thread_watchpoint = w
                self.assertTrue(
                    "g_watchme" in str(self.thread_watchpoint),
                    "Watchpoint location not shown correctly",
                )

        # Get the process
        self.inferior_process = self.inferior_target.GetProcess()

        # We should be stopped at the setup site where we can set the number of
        # threads doing each action (break/crash/signal/watch)
        self.assertEqual(
            self.inferior_process.GetNumThreads(),
            1,
            "Expected to stop before any additional threads are spawned.",
        )

        self.runCmd("expr num_breakpoint_threads=%d" % num_breakpoint_threads)
        self.runCmd("expr num_crash_threads=%d" % num_crash_threads)
        self.runCmd("expr num_signal_threads=%d" % num_signal_threads)
        self.runCmd("expr num_watchpoint_threads=%d" % num_watchpoint_threads)

        self.runCmd(
            "expr num_delay_breakpoint_threads=%d" % num_delay_breakpoint_threads
        )
        self.runCmd("expr num_delay_crash_threads=%d" % num_delay_crash_threads)
        self.runCmd("expr num_delay_signal_threads=%d" % num_delay_signal_threads)
        self.runCmd(
            "expr num_delay_watchpoint_threads=%d" % num_delay_watchpoint_threads
        )

        # Continue the inferior so threads are spawned
        self.runCmd("continue")

        # Make sure we see all the threads. The inferior program's threads all synchronize with a pseudo-barrier; that is,
        # the inferior program ensures all threads are started and running
        # before any thread triggers its 'event'.
        num_threads = self.inferior_process.GetNumThreads()
        expected_num_threads = (
            num_breakpoint_threads
            + num_delay_breakpoint_threads
            + num_signal_threads
            + num_delay_signal_threads
            + num_watchpoint_threads
            + num_delay_watchpoint_threads
            + num_crash_threads
            + num_delay_crash_threads
            + 1
        )
        self.assertEqual(
            num_threads,
            expected_num_threads,
            "Expected to see %d threads, but seeing %d. Details:\n%s"
            % (expected_num_threads, num_threads, "\n\t".join(self.describe_threads())),
        )

        self.signal_count = self.count_signaled_threads()
        self.crash_count = len(
            lldbutil.get_crashed_threads(self, self.inferior_process)
        )

        # Run to completion (or crash)
        while not self.inferior_done():
            if self.TraceOn():
                self.runCmd("thread backtrace all")
            self.runCmd("continue")
            self.signal_count += self.count_signaled_threads()
            self.crash_count += len(
                lldbutil.get_crashed_threads(self, self.inferior_process)
            )

        if num_crash_threads > 0 or num_delay_crash_threads > 0:
            # Expecting a crash
            self.assertTrue(
                self.crash_count > 0,
                "Expecting at least one thread to crash. Details: %s"
                % "\t\n".join(self.describe_threads()),
            )

            # Ensure the zombie process is reaped
            self.runCmd("process kill")

        elif num_crash_threads == 0 and num_delay_crash_threads == 0:
            # There should be a single active thread (the main one) which hit
            # the breakpoint after joining
            self.assertEqual(
                1,
                self.finish_breakpoint.GetHitCount(),
                "Expected main thread (finish) breakpoint to be hit once",
            )

            # There should be a single active thread (the main one) which hit
            # the breakpoint after joining.  Depending on the pthread
            # implementation we may have a worker thread finishing the pthread_join()
            # after it has returned.  Filter the threads to only count those
            # with user functions on them from our test case file,
            # lldb/test/API/functionalities/thread/concurrent_events/main.cpp
            user_code_funcnames = [
                "breakpoint_func",
                "crash_func",
                "do_action_args",
                "dotest",
                "main",
                "register_signal_handler",
                "signal_func",
                "sigusr1_handler",
                "start_threads",
                "watchpoint_func",
            ]
            num_threads_with_usercode = 0
            for t in self.inferior_process.threads:
                thread_has_user_code = False
                for f in t.frames:
                    for funcname in user_code_funcnames:
                        if funcname in f.GetDisplayFunctionName():
                            thread_has_user_code = True
                            break
                if thread_has_user_code:
                    num_threads_with_usercode += 1

            self.assertEqual(
                1,
                num_threads_with_usercode,
                "Expecting 1 thread but seeing %d. Details:%s"
                % (num_threads_with_usercode, "\n\t".join(self.describe_threads())),
            )
            self.runCmd("continue")

            # The inferior process should have exited without crashing
            self.assertEqual(
                0, self.crash_count, "Unexpected thread(s) in crashed state"
            )
            self.assertEqual(
                self.inferior_process.GetState(), lldb.eStateExited, PROCESS_EXITED
            )

            # Verify the number of actions took place matches expected numbers
            expected_breakpoint_threads = (
                num_delay_breakpoint_threads + num_breakpoint_threads
            )
            breakpoint_hit_count = (
                self.thread_breakpoint.GetHitCount()
                if expected_breakpoint_threads > 0
                else 0
            )
            self.assertEqual(
                expected_breakpoint_threads,
                breakpoint_hit_count,
                "Expected %d breakpoint hits, but got %d"
                % (expected_breakpoint_threads, breakpoint_hit_count),
            )

            expected_signal_threads = num_delay_signal_threads + num_signal_threads
            self.assertEqual(
                expected_signal_threads,
                self.signal_count,
                "Expected %d stops due to signal delivery, but got %d"
                % (expected_signal_threads, self.signal_count),
            )

            expected_watchpoint_threads = (
                num_delay_watchpoint_threads + num_watchpoint_threads
            )
            watchpoint_hit_count = (
                self.thread_watchpoint.GetHitCount()
                if expected_watchpoint_threads > 0
                else 0
            )
            self.assertEqual(
                expected_watchpoint_threads,
                watchpoint_hit_count,
                "Expected %d watchpoint hits, got %d"
                % (expected_watchpoint_threads, watchpoint_hit_count),
            )
