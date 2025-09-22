"""
Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

Provides classes used by the test results reporting infrastructure
within the LLDB test suite.


This module provides process-management support for the LLDB test
running infrastructure.
"""

# System imports
import os
import re
import signal
import subprocess
import sys
import threading


class CommunicatorThread(threading.Thread):
    """Provides a thread class that communicates with a subprocess."""

    def __init__(self, process, event, output_file):
        super(CommunicatorThread, self).__init__()
        # Don't let this thread prevent shutdown.
        self.daemon = True
        self.process = process
        self.pid = process.pid
        self.event = event
        self.output_file = output_file
        self.output = None

    def run(self):
        try:
            # Communicate with the child process.
            # This will not complete until the child process terminates.
            self.output = self.process.communicate()
        except Exception as exception:  # pylint: disable=broad-except
            if self.output_file:
                self.output_file.write(
                    "exception while using communicate() for pid: {}\n".format(
                        exception
                    )
                )
        finally:
            # Signal that the thread's run is complete.
            self.event.set()


# Provides a regular expression for matching gtimeout-based durations.
TIMEOUT_REGEX = re.compile(r"(^\d+)([smhd])?$")


def timeout_to_seconds(timeout):
    """Converts timeout/gtimeout timeout values into seconds.

    @param timeout a timeout in the form of xm representing x minutes.

    @return None if timeout is None, or the number of seconds as a float
    if a valid timeout format was specified.
    """
    if timeout is None:
        return None
    else:
        match = TIMEOUT_REGEX.match(timeout)
        if match:
            value = float(match.group(1))
            units = match.group(2)
            if units is None:
                # default is seconds.  No conversion necessary.
                return value
            elif units == "s":
                # Seconds.  No conversion necessary.
                return value
            elif units == "m":
                # Value is in minutes.
                return 60.0 * value
            elif units == "h":
                # Value is in hours.
                return (60.0 * 60.0) * value
            elif units == "d":
                # Value is in days.
                return 24 * (60.0 * 60.0) * value
            else:
                raise Exception("unexpected units value '{}'".format(units))
        else:
            raise Exception("could not parse TIMEOUT spec '{}'".format(timeout))


class ProcessHelper(object):
    """Provides an interface for accessing process-related functionality.

    This class provides a factory method that gives the caller a
    platform-specific implementation instance of the class.

    Clients of the class should stick to the methods provided in this
    base class.

    \see ProcessHelper.process_helper()
    """

    def __init__(self):
        super(ProcessHelper, self).__init__()

    @classmethod
    def process_helper(cls):
        """Returns a platform-specific ProcessHelper instance.
        @return a ProcessHelper instance that does the right thing for
        the current platform.
        """

        # If you add a new platform, create an instance here and
        # return it.
        if os.name == "nt":
            return WindowsProcessHelper()
        else:
            # For all POSIX-like systems.
            return UnixProcessHelper()

    def create_piped_process(self, command, new_process_group=True):
        # pylint: disable=no-self-use,unused-argument
        # As expected.  We want derived classes to implement this.
        """Creates a subprocess.Popen-based class with I/O piped to the parent.

        @param command the command line list as would be passed to
        subprocess.Popen().  Use the list form rather than the string form.

        @param new_process_group indicates if the caller wants the
        process to be created in its own process group.  Each OS handles
        this concept differently.  It provides a level of isolation and
        can simplify or enable terminating the process tree properly.

        @return a subprocess.Popen-like object.
        """
        raise Exception("derived class must implement")

    def supports_soft_terminate(self):
        # pylint: disable=no-self-use
        # As expected.  We want derived classes to implement this.
        """Indicates if the platform supports soft termination.

        Soft termination is the concept of a terminate mechanism that
        allows the target process to shut down nicely, but with the
        catch that the process might choose to ignore it.

        Platform supporter note: only mark soft terminate as supported
        if the target process has some way to evade the soft terminate
        request; otherwise, just support the hard terminate method.

        @return True if the platform supports a soft terminate mechanism.
        """
        # By default, we do not support a soft terminate mechanism.
        return False

    def soft_terminate(self, popen_process, log_file=None, want_core=True):
        # pylint: disable=no-self-use,unused-argument
        # As expected.  We want derived classes to implement this.
        """Attempts to terminate the process in a polite way.

        This terminate method is intended to give the child process a
        chance to clean up and exit on its own, possibly with a request
        to drop a core file or equivalent (i.e. [mini-]crashdump, crashlog,
        etc.)  If new_process_group was set in the process creation method
        and the platform supports it, this terminate call will attempt to
        kill the whole process tree rooted in this child process.

        @param popen_process the subprocess.Popen-like object returned
        by one of the process-creation methods of this class.

        @param log_file file-like object used to emit error-related
        logging info.  May be None if no error-related info is desired.

        @param want_core True if the caller would like to get a core
        dump (or the analogous crash report) from the terminated process.
        """
        popen_process.terminate()

    def hard_terminate(self, popen_process, log_file=None):
        # pylint: disable=no-self-use,unused-argument
        # As expected.  We want derived classes to implement this.
        """Attempts to terminate the process immediately.

        This terminate method is intended to kill child process in
        a manner in which the child process has no ability to block,
        and also has no ability to clean up properly.  If new_process_group
        was specified when creating the process, and if the platform
        implementation supports it, this will attempt to kill the
        whole process tree rooted in the child process.

        @param popen_process the subprocess.Popen-like object returned
        by one of the process-creation methods of this class.

        @param log_file file-like object used to emit error-related
        logging info.  May be None if no error-related info is desired.
        """
        popen_process.kill()

    def was_soft_terminate(self, returncode, with_core):
        # pylint: disable=no-self-use,unused-argument
        # As expected.  We want derived classes to implement this.
        """Returns if Popen-like object returncode matches soft terminate.

        @param returncode the returncode from the Popen-like object that
        terminated with a given return code.

        @param with_core indicates whether the returncode should match
        a core-generating return signal.

        @return True when the returncode represents what the system would
        issue when a soft_terminate() with the given with_core arg occurred;
        False otherwise.
        """
        if not self.supports_soft_terminate():
            # If we don't support soft termination on this platform,
            # then this should always be False.
            return False
        else:
            # Once a platform claims to support soft terminate, it
            # needs to be able to identify it by overriding this method.
            raise Exception("platform needs to implement")

    def was_hard_terminate(self, returncode):
        # pylint: disable=no-self-use,unused-argument
        # As expected.  We want derived classes to implement this.
        """Returns if Popen-like object returncode matches that of a hard
        terminate attempt.

        @param returncode the returncode from the Popen-like object that
        terminated with a given return code.

        @return True when the returncode represents what the system would
        issue when a hard_terminate() occurred; False
        otherwise.
        """
        raise Exception("platform needs to implement")

    def soft_terminate_signals(self):
        # pylint: disable=no-self-use
        """Retrieve signal numbers that can be sent to soft terminate.
        @return a list of signal numbers that can be sent to soft terminate
        a process, or None if not applicable.
        """
        return None

    def is_exceptional_exit(self, popen_status):
        """Returns whether the program exit status is exceptional.

        Returns whether the return code from a Popen process is exceptional
        (e.g. signals on POSIX systems).

        Derived classes should override this if they can detect exceptional
        program exit.

        @return True if the given popen_status represents an exceptional
        program exit; False otherwise.
        """
        return False

    def exceptional_exit_details(self, popen_status):
        """Returns the normalized exceptional exit code and a description.

        Given an exceptional exit code, returns the integral value of the
        exception (e.g. signal number for POSIX) and a description (e.g.
        signal name on POSIX) for the result.

        Derived classes should override this if they can detect exceptional
        program exit.

        It is fine to not implement this so long as is_exceptional_exit()
        always returns False.

        @return (normalized exception code, symbolic exception description)
        """
        raise Exception("exception_exit_details() called on unsupported class")


class UnixProcessHelper(ProcessHelper):
    """Provides a ProcessHelper for Unix-like operating systems.

    This implementation supports anything that looks Posix-y
    (e.g. Darwin, Linux, *BSD, etc.)
    """

    def __init__(self):
        super(UnixProcessHelper, self).__init__()

    @classmethod
    def _create_new_process_group(cls):
        """Creates a new process group for the calling process."""
        os.setpgid(os.getpid(), os.getpid())

    def create_piped_process(self, command, new_process_group=True):
        # Determine what to run after the fork but before the exec.
        if new_process_group:
            preexec_func = self._create_new_process_group
        else:
            preexec_func = None

        # Create the process.
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,  # Elicits automatic byte -> string decoding in Py3
            close_fds=True,
            preexec_fn=preexec_func,
        )

        # Remember whether we're using process groups for this
        # process.
        process.using_process_groups = new_process_group
        return process

    def supports_soft_terminate(self):
        # POSIX does support a soft terminate via:
        # * SIGTERM (no core requested)
        # * SIGQUIT (core requested if enabled, see ulimit -c)
        return True

    @classmethod
    def _validate_pre_terminate(cls, popen_process, log_file):
        # Validate args.
        if popen_process is None:
            raise ValueError("popen_process is None")

        # Ensure we have something that looks like a valid process.
        if popen_process.pid < 1:
            if log_file:
                log_file.write("skipping soft_terminate(): no process id")
            return False

        # We only do the process liveness check if we're not using
        # process groups.  With process groups, checking if the main
        # inferior process is dead and short circuiting here is no
        # good - children of it in the process group could still be
        # alive, and they should be killed during a timeout.
        if not popen_process.using_process_groups:
            # Don't kill if it's already dead.
            popen_process.poll()
            if popen_process.returncode is not None:
                # It has a returncode.  It has already stopped.
                if log_file:
                    log_file.write(
                        "requested to terminate pid {} but it has already "
                        "terminated, returncode {}".format(
                            popen_process.pid, popen_process.returncode
                        )
                    )
                # Move along...
                return False

        # Good to go.
        return True

    def _kill_with_signal(self, popen_process, log_file, signum):
        # Validate we're ready to terminate this.
        if not self._validate_pre_terminate(popen_process, log_file):
            return

        # Choose kill mechanism based on whether we're targeting
        # a process group or just a process.
        try:
            if popen_process.using_process_groups:
                # if log_file:
                #    log_file.write(
                #        "sending signum {} to process group {} now\n".format(
                #            signum, popen_process.pid))
                os.killpg(popen_process.pid, signum)
            else:
                # if log_file:
                #    log_file.write(
                #        "sending signum {} to process {} now\n".format(
                #            signum, popen_process.pid))
                os.kill(popen_process.pid, signum)
        except OSError as error:
            import errno

            if error.errno == errno.ESRCH:
                # This is okay - failed to find the process.  It may be that
                # that the timeout pre-kill hook eliminated the process.  We'll
                # ignore.
                pass
            else:
                raise

    def soft_terminate(self, popen_process, log_file=None, want_core=True):
        # Choose signal based on desire for core file.
        if want_core:
            # SIGQUIT will generate core by default.  Can be caught.
            signum = signal.SIGQUIT
        else:
            # SIGTERM is the traditional nice way to kill a process.
            # Can be caught, doesn't generate a core.
            signum = signal.SIGTERM

        self._kill_with_signal(popen_process, log_file, signum)

    def hard_terminate(self, popen_process, log_file=None):
        self._kill_with_signal(popen_process, log_file, signal.SIGKILL)

    def was_soft_terminate(self, returncode, with_core):
        if with_core:
            return returncode == -signal.SIGQUIT
        else:
            return returncode == -signal.SIGTERM

    def was_hard_terminate(self, returncode):
        return returncode == -signal.SIGKILL

    def soft_terminate_signals(self):
        return [signal.SIGQUIT, signal.SIGTERM]

    def is_exceptional_exit(self, popen_status):
        return popen_status < 0

    @classmethod
    def _signal_names_by_number(cls):
        return dict(
            (k, v)
            for v, k in reversed(sorted(signal.__dict__.items()))
            if v.startswith("SIG") and not v.startswith("SIG_")
        )

    def exceptional_exit_details(self, popen_status):
        signo = -popen_status
        signal_names_by_number = self._signal_names_by_number()
        signal_name = signal_names_by_number.get(signo, "")
        return (signo, signal_name)


class WindowsProcessHelper(ProcessHelper):
    """Provides a Windows implementation of the ProcessHelper class."""

    def __init__(self):
        super(WindowsProcessHelper, self).__init__()

    def create_piped_process(self, command, new_process_group=True):
        if new_process_group:
            # We need this flag if we want os.kill() to work on the subprocess.
            creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            creation_flags = 0

        return subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,  # Elicits automatic byte -> string decoding in Py3
            creationflags=creation_flags,
        )

    def was_hard_terminate(self, returncode):
        return returncode != 0


class ProcessDriver(object):
    """Drives a child process, notifies on important events, and can timeout.

    Clients are expected to derive from this class and override the
    on_process_started and on_process_exited methods if they want to
    hook either of those.

    This class supports timing out the child process in a platform-agnostic
    way.  The on_process_exited method is informed if the exit was natural
    or if it was due to a timeout.
    """

    def __init__(self, soft_terminate_timeout=10.0):
        super(ProcessDriver, self).__init__()
        self.process_helper = ProcessHelper.process_helper()
        self.pid = None
        # Create the synchronization event for notifying when the
        # inferior dotest process is complete.
        self.done_event = threading.Event()
        self.io_thread = None
        self.process = None
        # Number of seconds to wait for the soft terminate to
        # wrap up, before moving to more drastic measures.
        # Might want this longer if core dumps are generated and
        # take a long time to write out.
        self.soft_terminate_timeout = soft_terminate_timeout
        # Number of seconds to wait for the hard terminate to
        # wrap up, before giving up on the io thread.  This should
        # be fast.
        self.hard_terminate_timeout = 5.0
        self.returncode = None

    # =============================================
    # Methods for subclasses to override if desired.
    # =============================================

    def on_process_started(self):
        pass

    def on_process_exited(self, command, output, was_timeout, exit_status):
        pass

    def on_timeout_pre_kill(self):
        """Called after the timeout interval elapses but before killing it.

        This method is added to enable derived classes the ability to do
        something to the process prior to it being killed.  For example,
        this would be a good spot to run a program that samples the process
        to see what it was doing (or not doing).

        Do not attempt to reap the process (i.e. use wait()) in this method.
        That will interfere with the kill mechanism and return code processing.
        """

    def write(self, content):
        # pylint: disable=no-self-use
        # Intended - we want derived classes to be able to override
        # this and use any self state they may contain.
        sys.stdout.write(content)

    # ==============================================================
    # Operations used to drive processes.  Clients will want to call
    # one of these.
    # ==============================================================

    def run_command(self, command):
        # Start up the child process and the thread that does the
        # communication pump.
        self._start_process_and_io_thread(command)

        # Wait indefinitely for the child process to finish
        # communicating.  This indicates it has closed stdout/stderr
        # pipes and is done.
        self.io_thread.join()
        self.returncode = self.process.wait()
        if self.returncode is None:
            raise Exception(
                "no exit status available for pid {} after the "
                " inferior dotest.py should have completed".format(self.process.pid)
            )

        # Notify of non-timeout exit.
        self.on_process_exited(command, self.io_thread.output, False, self.returncode)

    def run_command_with_timeout(self, command, timeout, want_core):
        # Figure out how many seconds our timeout description is requesting.
        timeout_seconds = timeout_to_seconds(timeout)

        # Start up the child process and the thread that does the
        # communication pump.
        self._start_process_and_io_thread(command)

        self._wait_with_timeout(timeout_seconds, command, want_core)

    # ================
    # Internal details.
    # ================

    def _start_process_and_io_thread(self, command):
        # Create the process.
        self.process = self.process_helper.create_piped_process(command)
        self.pid = self.process.pid
        self.on_process_started()

        # Ensure the event is cleared that is used for signaling
        # from the communication() thread when communication is
        # complete (i.e. the inferior process has finished).
        self.done_event.clear()

        self.io_thread = CommunicatorThread(self.process, self.done_event, self.write)
        self.io_thread.start()

    def _attempt_soft_kill(self, want_core):
        # The inferior dotest timed out.  Attempt to clean it
        # with a non-drastic method (so it can clean up properly
        # and/or generate a core dump).  Often the OS can't guarantee
        # that the process will really terminate after this.
        self.process_helper.soft_terminate(
            self.process, want_core=want_core, log_file=self
        )

        # Now wait up to a certain timeout period for the io thread
        # to say that the communication ended.  If that wraps up
        # within our soft terminate timeout, we're all done here.
        self.io_thread.join(self.soft_terminate_timeout)
        if not self.io_thread.is_alive():
            # stdout/stderr were closed on the child process side. We
            # should be able to wait and reap the child process here.
            self.returncode = self.process.wait()
            # We terminated, and the done_trying result is n/a
            terminated = True
            done_trying = None
        else:
            self.write(
                "soft kill attempt of process {} timed out "
                "after {} seconds\n".format(
                    self.process.pid, self.soft_terminate_timeout
                )
            )
            terminated = False
            done_trying = False
        return terminated, done_trying

    def _attempt_hard_kill(self):
        # Instruct the process to terminate and really force it to
        # happen.  Don't give the process a chance to ignore.
        self.process_helper.hard_terminate(self.process, log_file=self)

        # Reap the child process.  This should not hang as the
        # hard_kill() mechanism is supposed to really kill it.
        # Improvement option:
        # If this does ever hang, convert to a self.process.poll()
        # loop checking on self.process.returncode until it is not
        # None or the timeout occurs.
        self.returncode = self.process.wait()

        # Wait a few moments for the io thread to finish...
        self.io_thread.join(self.hard_terminate_timeout)
        if self.io_thread.is_alive():
            # ... but this is not critical if it doesn't end for some
            # reason.
            self.write(
                "hard kill of process {} timed out after {} seconds waiting "
                "for the io thread (ignoring)\n".format(
                    self.process.pid, self.hard_terminate_timeout
                )
            )

        # Set if it terminated.  (Set up for optional improvement above).
        terminated = self.returncode is not None
        # Nothing else to try.
        done_trying = True

        return terminated, done_trying

    def _attempt_termination(self, attempt_count, want_core):
        if self.process_helper.supports_soft_terminate():
            # When soft termination is supported, we first try to stop
            # the process with a soft terminate.  Failing that, we try
            # the hard terminate option.
            if attempt_count == 1:
                return self._attempt_soft_kill(want_core)
            elif attempt_count == 2:
                return self._attempt_hard_kill()
            else:
                # We don't have anything else to try.
                terminated = self.returncode is not None
                done_trying = True
                return terminated, done_trying
        else:
            # We only try the hard terminate option when there
            # is no soft terminate available.
            if attempt_count == 1:
                return self._attempt_hard_kill()
            else:
                # We don't have anything else to try.
                terminated = self.returncode is not None
                done_trying = True
                return terminated, done_trying

    def _wait_with_timeout(self, timeout_seconds, command, want_core):
        # Allow up to timeout seconds for the io thread to wrap up.
        # If that completes, the child process should be done.
        completed_normally = self.done_event.wait(timeout_seconds)
        if completed_normally:
            # Reap the child process here.
            self.returncode = self.process.wait()
        else:
            # Allow derived classes to do some work after we detected
            # a timeout but before we touch the timed-out process.
            self.on_timeout_pre_kill()

            # Prepare to stop the process
            process_terminated = completed_normally
            terminate_attempt_count = 0

            # Try as many attempts as we support for trying to shut down
            # the child process if it's not already shut down.
            while not process_terminated:
                terminate_attempt_count += 1
                # Attempt to terminate.
                process_terminated, done_trying = self._attempt_termination(
                    terminate_attempt_count, want_core
                )
                # Check if there's nothing more to try.
                if done_trying:
                    # Break out of our termination attempt loop.
                    break

        # At this point, we're calling it good.  The process
        # finished gracefully, was shut down after one or more
        # attempts, or we failed but gave it our best effort.
        self.on_process_exited(
            command, self.io_thread.output, not completed_normally, self.returncode
        )


def patched_init(self, *args, **kwargs):
    self.original_init(*args, **kwargs)
    # Initialize our condition variable that protects wait()/poll().
    self.wait_condition = threading.Condition()


def patched_wait(self, *args, **kwargs):
    self.wait_condition.acquire()
    try:
        result = self.original_wait(*args, **kwargs)
        # The process finished.  Signal the condition.
        self.wait_condition.notify_all()
        return result
    finally:
        self.wait_condition.release()


def patched_poll(self, *args, **kwargs):
    self.wait_condition.acquire()
    try:
        result = self.original_poll(*args, **kwargs)
        if self.returncode is not None:
            # We did complete, and we have the return value.
            # Signal the event to indicate we're done.
            self.wait_condition.notify_all()
        return result
    finally:
        self.wait_condition.release()


def patch_up_subprocess_popen():
    subprocess.Popen.original_init = subprocess.Popen.__init__
    subprocess.Popen.__init__ = patched_init

    subprocess.Popen.original_wait = subprocess.Popen.wait
    subprocess.Popen.wait = patched_wait

    subprocess.Popen.original_poll = subprocess.Popen.poll
    subprocess.Popen.poll = patched_poll


# Replace key subprocess.Popen() threading-unprotected methods with
# threading-protected versions.
patch_up_subprocess_popen()
