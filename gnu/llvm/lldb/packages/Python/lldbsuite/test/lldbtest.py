"""
LLDB module which provides the abstract base class of lldb test case.

The concrete subclass can override lldbtest.TestBase in order to inherit the
common behavior for unitest.TestCase.setUp/tearDown implemented in this file.

./dotest.py provides a test driver which sets up the environment to run the
entire of part of the test suite .  Example:

# Exercises the test suite in the types directory....
/Volumes/data/lldb/svn/ToT/test $ ./dotest.py -A x86_64 types
...

Session logs for test failures/errors/unexpected successes will go into directory '2012-05-16-13_35_42'
Command invoked: python ./dotest.py -A x86_64 types
compilers=['clang']

Configuration: arch=x86_64 compiler=clang
----------------------------------------------------------------------
Collected 72 tests

........................................................................
----------------------------------------------------------------------
Ran 72 tests in 135.468s

OK
$
"""

# System modules
import abc
from functools import wraps
import gc
import glob
import io
import json
import os.path
import re
import shutil
import signal
from subprocess import *
import sys
import time
import traceback

# Third-party modules
import unittest

# LLDB modules
import lldb
from . import configuration
from . import decorators
from . import lldbplatformutil
from . import lldbtest_config
from . import lldbutil
from . import test_categories
from lldbsuite.support import encoded_file
from lldbsuite.support import funcutils
from lldbsuite.support import seven
from lldbsuite.test_event import build_exception

# See also dotest.parseOptionsAndInitTestdirs(), where the environment variables
# LLDB_COMMAND_TRACE is set from '-t' option.

# By default, traceAlways is False.
if "LLDB_COMMAND_TRACE" in os.environ and os.environ["LLDB_COMMAND_TRACE"] == "YES":
    traceAlways = True
else:
    traceAlways = False

# By default, doCleanup is True.
if "LLDB_DO_CLEANUP" in os.environ and os.environ["LLDB_DO_CLEANUP"] == "NO":
    doCleanup = False
else:
    doCleanup = True


#
# Some commonly used assert messages.
#

COMMAND_FAILED_AS_EXPECTED = "Command has failed as expected"

CURRENT_EXECUTABLE_SET = "Current executable set successfully"

PROCESS_IS_VALID = "Process is valid"

PROCESS_KILLED = "Process is killed successfully"

PROCESS_EXITED = "Process exited successfully"

PROCESS_STOPPED = "Process status should be stopped"

RUN_SUCCEEDED = "Process is launched successfully"

RUN_COMPLETED = "Process exited successfully"

BACKTRACE_DISPLAYED_CORRECTLY = "Backtrace displayed correctly"

BREAKPOINT_CREATED = "Breakpoint created successfully"

BREAKPOINT_STATE_CORRECT = "Breakpoint state is correct"

BREAKPOINT_PENDING_CREATED = "Pending breakpoint created successfully"

BREAKPOINT_HIT_ONCE = "Breakpoint resolved with hit count = 1"

BREAKPOINT_HIT_TWICE = "Breakpoint resolved with hit count = 2"

BREAKPOINT_HIT_THRICE = "Breakpoint resolved with hit count = 3"

MISSING_EXPECTED_REGISTERS = "At least one expected register is unavailable."

OBJECT_PRINTED_CORRECTLY = "Object printed correctly"

SOURCE_DISPLAYED_CORRECTLY = "Source code displayed correctly"

STEP_IN_SUCCEEDED = "Thread step-in succeeded"

STEP_OUT_SUCCEEDED = "Thread step-out succeeded"

STOPPED_DUE_TO_EXC_BAD_ACCESS = "Process should be stopped due to bad access exception"

STOPPED_DUE_TO_ASSERT = "Process should be stopped due to an assertion"

STOPPED_DUE_TO_BREAKPOINT = "Process should be stopped due to breakpoint"

STOPPED_DUE_TO_BREAKPOINT_WITH_STOP_REASON_AS = "%s, %s" % (
    STOPPED_DUE_TO_BREAKPOINT,
    "instead, the actual stop reason is: '%s'",
)

STOPPED_DUE_TO_BREAKPOINT_CONDITION = "Stopped due to breakpoint condition"

STOPPED_DUE_TO_BREAKPOINT_IGNORE_COUNT = "Stopped due to breakpoint and ignore count"

STOPPED_DUE_TO_BREAKPOINT_JITTED_CONDITION = (
    "Stopped due to breakpoint jitted condition"
)

STOPPED_DUE_TO_SIGNAL = "Process state is stopped due to signal"

STOPPED_DUE_TO_STEP_IN = "Process state is stopped due to step in"

STOPPED_DUE_TO_WATCHPOINT = "Process should be stopped due to watchpoint"

DATA_TYPES_DISPLAYED_CORRECTLY = "Data type(s) displayed correctly"

VALID_BREAKPOINT = "Got a valid breakpoint"

VALID_BREAKPOINT_LOCATION = "Got a valid breakpoint location"

VALID_COMMAND_INTERPRETER = "Got a valid command interpreter"

VALID_FILESPEC = "Got a valid filespec"

VALID_MODULE = "Got a valid module"

VALID_PROCESS = "Got a valid process"

VALID_SYMBOL = "Got a valid symbol"

VALID_TARGET = "Got a valid target"

VALID_PLATFORM = "Got a valid platform"

VALID_TYPE = "Got a valid type"

VALID_VARIABLE = "Got a valid variable"

VARIABLES_DISPLAYED_CORRECTLY = "Variable(s) displayed correctly"

WATCHPOINT_CREATED = "Watchpoint created successfully"


def CMD_MSG(str):
    """A generic "Command '%s' did not return successfully" message generator."""
    return "Command '%s' did not return successfully" % str


def COMPLETION_MSG(str_before, str_after, completions):
    """A generic assertion failed message generator for the completion mechanism."""
    return "'%s' successfully completes to '%s', but completions were:\n%s" % (
        str_before,
        str_after,
        "\n".join(completions),
    )


def EXP_MSG(str, actual, exe):
    """A generic "'%s' returned unexpected result" message generator if exe.
    Otherwise, it generates "'%s' does not match expected result" message."""

    return "'%s' %s result, got '%s'" % (
        str,
        "returned unexpected" if exe else "does not match expected",
        actual.strip(),
    )


def SETTING_MSG(setting):
    """A generic "Value of setting '%s' is not correct" message generator."""
    return "Value of setting '%s' is not correct" % setting


def line_number(filename, string_to_match):
    """Helper function to return the line number of the first matched string."""
    with io.open(filename, mode="r", encoding="utf-8") as f:
        for i, line in enumerate(f):
            if line.find(string_to_match) != -1:
                # Found our match.
                return i + 1
    raise Exception("Unable to find '%s' within file %s" % (string_to_match, filename))


def get_line(filename, line_number):
    """Return the text of the line at the 1-based line number."""
    with io.open(filename, mode="r", encoding="utf-8") as f:
        return f.readlines()[line_number - 1]


def pointer_size():
    """Return the pointer size of the host system."""
    import ctypes

    a_pointer = ctypes.c_void_p(0xFFFF)
    return 8 * ctypes.sizeof(a_pointer)


def is_exe(fpath):
    """Returns true if fpath is an executable."""
    if fpath is None:
        return False
    if sys.platform == "win32":
        if not fpath.endswith(".exe"):
            fpath += ".exe"
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


class ValueCheck:
    def __init__(
        self,
        name=None,
        value=None,
        type=None,
        summary=None,
        children=None,
        dereference=None,
    ):
        """
        :param name: The name that the SBValue should have. None if the summary
                     should not be checked.
        :param summary: The summary that the SBValue should have. None if the
                        summary should not be checked.
        :param value: The value that the SBValue should have. None if the value
                      should not be checked.
        :param type: The type that the SBValue result should have. None if the
                     type should not be checked.
        :param children: A list of ValueChecks that need to match the children
                         of this SBValue. None if children shouldn't be checked.
                         The order of checks is the order of the checks in the
                         list. The number of checks has to match the number of
                         children.
        :param dereference: A ValueCheck for the SBValue returned by the
                            `Dereference` function.
        """
        self.expect_name = name
        self.expect_value = value
        self.expect_type = type
        self.expect_summary = summary
        self.children = children
        self.dereference = dereference

    def check_value(self, test_base, val, error_msg=None):
        """
        Checks that the given value matches the currently set properties
        of this ValueCheck. If a match failed, the given TestBase will
        be used to emit an error. A custom error message can be specified
        that will be used to describe failed check for this SBValue (but
        not errors in the child values).
        """

        this_error_msg = error_msg if error_msg else ""
        this_error_msg += "\nChecking SBValue: " + str(val)

        test_base.assertSuccess(val.GetError())

        # Python 3.6 doesn't declare a `re.Pattern` type, get the dynamic type.
        pattern_type = type(re.compile(""))

        if self.expect_name:
            test_base.assertEqual(self.expect_name, val.GetName(), this_error_msg)
        if self.expect_value:
            if isinstance(self.expect_value, pattern_type):
                test_base.assertRegex(val.GetValue(), self.expect_value, this_error_msg)
            else:
                test_base.assertEqual(self.expect_value, val.GetValue(), this_error_msg)
        if self.expect_type:
            test_base.assertEqual(
                self.expect_type, val.GetDisplayTypeName(), this_error_msg
            )
        if self.expect_summary:
            if isinstance(self.expect_summary, pattern_type):
                test_base.assertRegex(
                    val.GetSummary(), self.expect_summary, this_error_msg
                )
            else:
                test_base.assertEqual(
                    self.expect_summary, val.GetSummary(), this_error_msg
                )
        if self.children is not None:
            self.check_value_children(test_base, val, error_msg)

        if self.dereference is not None:
            self.dereference.check_value(test_base, val.Dereference(), error_msg)

    def check_value_children(self, test_base, val, error_msg=None):
        """
        Checks that the children of a SBValue match a certain structure and
        have certain properties.

        :param test_base: The current test's TestBase object.
        :param val: The SBValue to check.
        """

        this_error_msg = error_msg if error_msg else ""
        this_error_msg += "\nChecking SBValue: " + str(val)

        test_base.assertEqual(len(self.children), val.GetNumChildren(), this_error_msg)

        for i in range(0, val.GetNumChildren()):
            expected_child = self.children[i]
            actual_child = val.GetChildAtIndex(i)
            child_error = "Checking child with index " + str(i) + ":\n" + error_msg
            expected_child.check_value(test_base, actual_child, child_error)


class recording(io.StringIO):
    """
    A nice little context manager for recording the debugger interactions into
    our session object.  If trace flag is ON, it also emits the interactions
    into the stderr.
    """

    def __init__(self, test, trace):
        """Create a io.StringIO instance; record the session obj and trace flag."""
        io.StringIO.__init__(self)
        # The test might not have undergone the 'setUp(self)' phase yet, so that
        # the attribute 'session' might not even exist yet.
        self.session = getattr(test, "session", None) if test else None
        self.trace = trace

    def __enter__(self):
        """
        Context management protocol on entry to the body of the with statement.
        Just return the io.StringIO object.
        """
        return self

    def __exit__(self, type, value, tb):
        """
        Context management protocol on exit from the body of the with statement.
        If trace is ON, it emits the recordings into stderr.  Always add the
        recordings to our session object.  And close the io.StringIO object, too.
        """
        if self.trace:
            print(self.getvalue(), file=sys.stderr)
        if self.session:
            print(self.getvalue(), file=self.session)
        self.close()


class _BaseProcess(object, metaclass=abc.ABCMeta):
    @abc.abstractproperty
    def pid(self):
        """Returns process PID if has been launched already."""

    @abc.abstractmethod
    def launch(self, executable, args, extra_env):
        """Launches new process with given executable and args."""

    @abc.abstractmethod
    def terminate(self):
        """Terminates previously launched process.."""


class _LocalProcess(_BaseProcess):
    def __init__(self, trace_on):
        self._proc = None
        self._trace_on = trace_on
        self._delayafterterminate = 0.1

    @property
    def pid(self):
        return self._proc.pid

    def launch(self, executable, args, extra_env):
        env = None
        if extra_env:
            env = dict(os.environ)
            env.update([kv.split("=", 1) for kv in extra_env])

        self._proc = Popen(
            [executable] + args,
            stdout=open(os.devnull) if not self._trace_on else None,
            stdin=PIPE,
            env=env,
        )

    def terminate(self):
        if self._proc.poll() is None:
            # Terminate _proc like it does the pexpect
            signals_to_try = [
                sig for sig in ["SIGHUP", "SIGCONT", "SIGINT"] if sig in dir(signal)
            ]
            for sig in signals_to_try:
                try:
                    self._proc.send_signal(getattr(signal, sig))
                    time.sleep(self._delayafterterminate)
                    if self._proc.poll() is not None:
                        return
                except ValueError:
                    pass  # Windows says SIGINT is not a valid signal to send
            self._proc.terminate()
            time.sleep(self._delayafterterminate)
            if self._proc.poll() is not None:
                return
            self._proc.kill()
            time.sleep(self._delayafterterminate)

    def poll(self):
        return self._proc.poll()

    def wait(self, timeout=None):
        return self._proc.wait(timeout)


class _RemoteProcess(_BaseProcess):
    def __init__(self, install_remote):
        self._pid = None
        self._install_remote = install_remote

    @property
    def pid(self):
        return self._pid

    def launch(self, executable, args, extra_env):
        if self._install_remote:
            src_path = executable
            dst_path = lldbutil.join_remote_paths(
                lldb.remote_platform.GetWorkingDirectory(), os.path.basename(executable)
            )

            dst_file_spec = lldb.SBFileSpec(dst_path, False)
            err = lldb.remote_platform.Install(
                lldb.SBFileSpec(src_path, True), dst_file_spec
            )
            if err.Fail():
                raise Exception(
                    "remote_platform.Install('%s', '%s') failed: %s"
                    % (src_path, dst_path, err)
                )
        else:
            dst_path = executable
            dst_file_spec = lldb.SBFileSpec(executable, False)

        launch_info = lldb.SBLaunchInfo(args)
        launch_info.SetExecutableFile(dst_file_spec, True)
        launch_info.SetWorkingDirectory(lldb.remote_platform.GetWorkingDirectory())

        # Redirect stdout and stderr to /dev/null
        launch_info.AddSuppressFileAction(1, False, True)
        launch_info.AddSuppressFileAction(2, False, True)

        if extra_env:
            launch_info.SetEnvironmentEntries(extra_env, True)

        err = lldb.remote_platform.Launch(launch_info)
        if err.Fail():
            raise Exception(
                "remote_platform.Launch('%s', '%s') failed: %s" % (dst_path, args, err)
            )
        self._pid = launch_info.GetProcessID()

    def terminate(self):
        lldb.remote_platform.Kill(self._pid)


def getsource_if_available(obj):
    """
    Return the text of the source code for an object if available.  Otherwise,
    a print representation is returned.
    """
    import inspect

    try:
        return inspect.getsource(obj)
    except:
        return repr(obj)


def builder_module():
    return lldbplatformutil.builder_module()


class Base(unittest.TestCase):
    """
    Abstract base for performing lldb (see TestBase) or other generic tests (see
    BenchBase for one example).  lldbtest.Base works with the test driver to
    accomplish things.

    """

    # The concrete subclass should override this attribute.
    mydir = None

    # Keep track of the old current working directory.
    oldcwd = None

    # Maximum allowed attempts when launching the inferior process.
    # Can be overridden by the LLDB_MAX_LAUNCH_COUNT environment variable.
    maxLaunchCount = 1

    # Time to wait before the next launching attempt in second(s).
    # Can be overridden by the LLDB_TIME_WAIT_NEXT_LAUNCH environment variable.
    timeWaitNextLaunch = 1.0

    @staticmethod
    def compute_mydir(test_file):
        """Subclasses should call this function to correctly calculate the
        required "mydir" attribute as follows:

         mydir = TestBase.compute_mydir(__file__)
        """
        # /abs/path/to/packages/group/subdir/mytest.py -> group/subdir
        lldb_test_src = configuration.test_src_root
        if not test_file.startswith(lldb_test_src):
            raise Exception(
                "Test file '%s' must reside within lldb_test_src "
                "(which is '%s')." % (test_file, lldb_test_src)
            )
        return os.path.dirname(os.path.relpath(test_file, start=lldb_test_src))

    def TraceOn(self):
        """Returns True if we are in trace mode (tracing detailed test execution)."""
        return traceAlways

    def trace(self, *args, **kwargs):
        with recording(self, self.TraceOn()) as sbuf:
            print(*args, file=sbuf, **kwargs)

    @classmethod
    def setUpClass(cls):
        """
        Python unittest framework class setup fixture.
        Do current directory manipulation.
        """
        # Fail fast if 'mydir' attribute is not overridden.
        if not cls.mydir:
            cls.mydir = Base.compute_mydir(sys.modules[cls.__module__].__file__)
        if not cls.mydir:
            raise Exception("Subclasses must override the 'mydir' attribute.")

        # Save old working directory.
        cls.oldcwd = os.getcwd()

        full_dir = os.path.join(configuration.test_src_root, cls.mydir)
        if traceAlways:
            print("Change dir to:", full_dir, file=sys.stderr)
        os.chdir(full_dir)

        # Set platform context.
        cls.platformContext = lldbplatformutil.createPlatformContext()

    @classmethod
    def tearDownClass(cls):
        """
        Python unittest framework class teardown fixture.
        Do class-wide cleanup.
        """

        if doCleanup:
            # First, let's do the platform-specific cleanup.
            module = builder_module()
            module.cleanup()

            # Subclass might have specific cleanup function defined.
            if getattr(cls, "classCleanup", None):
                if traceAlways:
                    print(
                        "Call class-specific cleanup function for class:",
                        cls,
                        file=sys.stderr,
                    )
                try:
                    cls.classCleanup()
                except:
                    exc_type, exc_value, exc_tb = sys.exc_info()
                    traceback.print_exception(exc_type, exc_value, exc_tb)

        # Restore old working directory.
        if traceAlways:
            print("Restore dir to:", cls.oldcwd, file=sys.stderr)
        os.chdir(cls.oldcwd)

    def enableLogChannelsForCurrentTest(self):
        if len(lldbtest_config.channels) == 0:
            return

        # if debug channels are specified in lldbtest_config.channels,
        # create a new set of log files for every test
        log_basename = self.getLogBasenameForCurrentTest()

        # confirm that the file is writeable
        host_log_path = "{}-host.log".format(log_basename)
        open(host_log_path, "w").close()
        self.log_files.append(host_log_path)

        log_enable = "log enable -Tpn -f {} ".format(host_log_path)
        for channel_with_categories in lldbtest_config.channels:
            channel_then_categories = channel_with_categories.split(" ", 1)
            channel = channel_then_categories[0]
            if len(channel_then_categories) > 1:
                categories = channel_then_categories[1]
            else:
                categories = "default"

            if channel == "gdb-remote" and lldb.remote_platform is None:
                # communicate gdb-remote categories to debugserver
                os.environ["LLDB_DEBUGSERVER_LOG_FLAGS"] = categories

            self.ci.HandleCommand(log_enable + channel_with_categories, self.res)
            if not self.res.Succeeded():
                raise Exception(
                    "log enable failed (check LLDB_LOG_OPTION env variable)"
                )

        # Communicate log path name to debugserver & lldb-server
        # For remote debugging, these variables need to be set when starting the platform
        # instance.
        if lldb.remote_platform is None:
            server_log_path = "{}-server.log".format(log_basename)
            open(server_log_path, "w").close()
            self.log_files.append(server_log_path)
            os.environ["LLDB_DEBUGSERVER_LOG_FILE"] = server_log_path

            # Communicate channels to lldb-server
            os.environ["LLDB_SERVER_LOG_CHANNELS"] = ":".join(lldbtest_config.channels)

        self.addTearDownHook(self.disableLogChannelsForCurrentTest)

    def disableLogChannelsForCurrentTest(self):
        # close all log files that we opened
        for channel_and_categories in lldbtest_config.channels:
            # channel format - <channel-name> [<category0> [<category1> ...]]
            channel = channel_and_categories.split(" ", 1)[0]
            self.ci.HandleCommand("log disable " + channel, self.res)
            if not self.res.Succeeded():
                raise Exception(
                    "log disable failed (check LLDB_LOG_OPTION env variable)"
                )

        # Retrieve the server log (if any) from the remote system. It is assumed the server log
        # is writing to the "server.log" file in the current test directory. This can be
        # achieved by setting LLDB_DEBUGSERVER_LOG_FILE="server.log" when starting remote
        # platform.
        if lldb.remote_platform:
            server_log_path = self.getLogBasenameForCurrentTest() + "-server.log"
            if lldb.remote_platform.Get(
                lldb.SBFileSpec("server.log"), lldb.SBFileSpec(server_log_path)
            ).Success():
                self.log_files.append(server_log_path)

    def setPlatformWorkingDir(self):
        if not lldb.remote_platform or not configuration.lldb_platform_working_dir:
            return

        components = self.mydir.split(os.path.sep) + [
            str(self.test_number),
            self.getBuildDirBasename(),
        ]
        remote_test_dir = configuration.lldb_platform_working_dir
        for c in components:
            remote_test_dir = lldbutil.join_remote_paths(remote_test_dir, c)
            error = lldb.remote_platform.MakeDirectory(
                remote_test_dir, 448
            )  # 448 = 0o700
            if error.Fail():
                raise Exception(
                    "making remote directory '%s': %s" % (remote_test_dir, error)
                )

        lldb.remote_platform.SetWorkingDirectory(remote_test_dir)

        # This function removes all files from the current working directory while leaving
        # the directories in place. The cleanup is required to reduce the disk space required
        # by the test suite while leaving the directories untouched is neccessary because
        # sub-directories might belong to an other test
        def clean_working_directory():
            # TODO: Make it working on Windows when we need it for remote debugging support
            # TODO: Replace the heuristic to remove the files with a logic what collects the
            # list of files we have to remove during test runs.
            shell_cmd = lldb.SBPlatformShellCommand("rm %s/*" % remote_test_dir)
            lldb.remote_platform.Run(shell_cmd)

        self.addTearDownHook(clean_working_directory)

    def getSourceDir(self):
        """Return the full path to the current test."""
        return os.path.join(configuration.test_src_root, self.mydir)

    def getBuildDirBasename(self):
        return self.__class__.__module__ + "." + self.testMethodName

    def getBuildDir(self):
        """Return the full path to the current test."""
        return os.path.join(
            configuration.test_build_dir, self.mydir, self.getBuildDirBasename()
        )

    def makeBuildDir(self):
        """Create the test-specific working directory, deleting any previous
        contents."""
        bdir = self.getBuildDir()
        if os.path.isdir(bdir):
            shutil.rmtree(bdir)
        lldbutil.mkdir_p(bdir)

    def getBuildArtifact(self, name="a.out"):
        """Return absolute path to an artifact in the test's build directory."""
        return os.path.join(self.getBuildDir(), name)

    def getSourcePath(self, name):
        """Return absolute path to a file in the test's source directory."""
        return os.path.join(self.getSourceDir(), name)

    @classmethod
    def setUpCommands(cls):
        commands = [
            # First of all, clear all settings to have clean state of global properties.
            "settings clear -all",
            # Disable Spotlight lookup. The testsuite creates
            # different binaries with the same UUID, because they only
            # differ in the debug info, which is not being hashed.
            "settings set symbols.enable-external-lookup false",
            # Inherit the TCC permissions from the inferior's parent.
            "settings set target.inherit-tcc true",
            # Based on https://discourse.llvm.org/t/running-lldb-in-a-container/76801/4
            "settings set target.disable-aslr false",
            # Kill rather than detach from the inferior if something goes wrong.
            "settings set target.detach-on-error false",
            # Disable fix-its by default so that incorrect expressions in tests don't
            # pass just because Clang thinks it has a fix-it.
            "settings set target.auto-apply-fixits false",
            # Testsuite runs in parallel and the host can have also other load.
            "settings set plugin.process.gdb-remote.packet-timeout 60",
            'settings set symbols.clang-modules-cache-path "{}"'.format(
                configuration.lldb_module_cache_dir
            ),
            "settings set use-color false",
        ]

        # Set any user-overridden settings.
        for setting, value in configuration.settings:
            commands.append("setting set %s %s" % (setting, value))

        # Make sure that a sanitizer LLDB's environment doesn't get passed on.
        if (
            cls.platformContext
            and cls.platformContext.shlib_environment_var in os.environ
        ):
            commands.append(
                "settings set target.env-vars {}=".format(
                    cls.platformContext.shlib_environment_var
                )
            )

        # Set environment variables for the inferior.
        if lldbtest_config.inferior_env:
            commands.append(
                "settings set target.env-vars {}".format(lldbtest_config.inferior_env)
            )
        return commands

    def setUp(self):
        """Fixture for unittest test case setup.

        It works with the test driver to conditionally skip tests and does other
        initializations."""
        # import traceback
        # traceback.print_stack()

        if "LLDB_MAX_LAUNCH_COUNT" in os.environ:
            self.maxLaunchCount = int(os.environ["LLDB_MAX_LAUNCH_COUNT"])

        if "LLDB_TIME_WAIT_NEXT_LAUNCH" in os.environ:
            self.timeWaitNextLaunch = float(os.environ["LLDB_TIME_WAIT_NEXT_LAUNCH"])

        if "LIBCXX_PATH" in os.environ:
            self.libcxxPath = os.environ["LIBCXX_PATH"]
        else:
            self.libcxxPath = None

        if "LLDBDAP_EXEC" in os.environ:
            self.lldbDAPExec = os.environ["LLDBDAP_EXEC"]
        else:
            self.lldbDAPExec = None

        self.lldbOption = " ".join("-o '" + s + "'" for s in self.setUpCommands())

        # If we spawn an lldb process for test (via pexpect), do not load the
        # init file unless told otherwise.
        if os.environ.get("NO_LLDBINIT") != "NO":
            self.lldbOption += " --no-lldbinit"

        # Assign the test method name to self.testMethodName.
        #
        # For an example of the use of this attribute, look at test/types dir.
        # There are a bunch of test cases under test/types and we don't want the
        # module cacheing subsystem to be confused with executable name "a.out"
        # used for all the test cases.
        self.testMethodName = self._testMethodName

        # This is for the case of directly spawning 'lldb'/'gdb' and interacting
        # with it using pexpect.
        self.child = None
        self.child_prompt = "(lldb) "
        # If the child is interacting with the embedded script interpreter,
        # there are two exits required during tear down, first to quit the
        # embedded script interpreter and second to quit the lldb command
        # interpreter.
        self.child_in_script_interpreter = False

        # These are for customized teardown cleanup.
        self.dict = None
        self.doTearDownCleanup = False
        # And in rare cases where there are multiple teardown cleanups.
        self.dicts = []
        self.doTearDownCleanups = False

        # List of spawned subproces.Popen objects
        self.subprocesses = []

        # List of log files produced by the current test.
        self.log_files = []

        # Create the build directory.
        # The logs are stored in the build directory, so we have to create it
        # before creating the first log file.
        self.makeBuildDir()

        session_file = self.getLogBasenameForCurrentTest() + ".log"
        self.log_files.append(session_file)

        # Python 3 doesn't support unbuffered I/O in text mode.  Open buffered.
        self.session = encoded_file.open(session_file, "utf-8", mode="w")

        # Optimistically set __errored__, __failed__, __expected__ to False
        # initially.  If the test errored/failed, the session info
        # (self.session) is then dumped into a session specific file for
        # diagnosis.
        self.__cleanup_errored__ = False
        self.__errored__ = False
        self.__failed__ = False
        self.__expected__ = False
        # We are also interested in unexpected success.
        self.__unexpected__ = False
        # And skipped tests.
        self.__skipped__ = False

        # See addTearDownHook(self, hook) which allows the client to add a hook
        # function to be run during tearDown() time.
        self.hooks = []

        # See HideStdout(self).
        self.sys_stdout_hidden = False

        if self.platformContext:
            # set environment variable names for finding shared libraries
            self.dylibPath = self.platformContext.shlib_environment_var

        # Create the debugger instance.
        self.dbg = lldb.SBDebugger.Create()
        # Copy selected platform from a global instance if it exists.
        if lldb.selected_platform is not None:
            self.dbg.SetSelectedPlatform(lldb.selected_platform)

        if not self.dbg:
            raise Exception("Invalid debugger instance")

        # Retrieve the associated command interpreter instance.
        self.ci = self.dbg.GetCommandInterpreter()
        if not self.ci:
            raise Exception("Could not get the command interpreter")

        # And the result object.
        self.res = lldb.SBCommandReturnObject()

        self.setPlatformWorkingDir()
        self.enableLogChannelsForCurrentTest()

        self.lib_lldb = None
        self.framework_dir = None
        self.darwinWithFramework = False

        if sys.platform.startswith("darwin") and configuration.lldb_framework_path:
            framework = configuration.lldb_framework_path
            lib = os.path.join(framework, "LLDB")
            if os.path.exists(lib):
                self.framework_dir = os.path.dirname(framework)
                self.lib_lldb = lib
                self.darwinWithFramework = self.platformIsDarwin()

    def setAsync(self, value):
        """Sets async mode to True/False and ensures it is reset after the testcase completes."""
        old_async = self.dbg.GetAsync()
        self.dbg.SetAsync(value)
        self.addTearDownHook(lambda: self.dbg.SetAsync(old_async))

    def cleanupSubprocesses(self):
        # Terminate subprocesses in reverse order from how they were created.
        for p in reversed(self.subprocesses):
            p.terminate()
            del p
        del self.subprocesses[:]

    def spawnSubprocess(self, executable, args=[], extra_env=None, install_remote=True):
        """Creates a subprocess.Popen object with the specified executable and arguments,
        saves it in self.subprocesses, and returns the object.
        """
        proc = (
            _RemoteProcess(install_remote)
            if lldb.remote_platform
            else _LocalProcess(self.TraceOn())
        )
        proc.launch(executable, args, extra_env=extra_env)
        self.subprocesses.append(proc)
        return proc

    def runCmd(self, cmd, msg=None, check=True, trace=False, inHistory=False):
        """
        Ask the command interpreter to handle the command and then check its
        return status.
        """
        # Fail fast if 'cmd' is not meaningful.
        if cmd is None:
            raise Exception("Bad 'cmd' parameter encountered")

        trace = True if traceAlways else trace

        if cmd.startswith("target create "):
            cmd = cmd.replace("target create ", "file ")

        running = cmd.startswith("run") or cmd.startswith("process launch")

        for i in range(self.maxLaunchCount if running else 1):
            with recording(self, trace) as sbuf:
                print("runCmd:", cmd, file=sbuf)
                if not check:
                    print("check of return status not required", file=sbuf)

            self.ci.HandleCommand(cmd, self.res, inHistory)

            with recording(self, trace) as sbuf:
                if self.res.Succeeded():
                    print("output:", self.res.GetOutput(), file=sbuf)
                else:
                    print("runCmd failed!", file=sbuf)
                    print(self.res.GetError(), file=sbuf)

            if self.res.Succeeded():
                break
            elif running:
                # For process launch, wait some time before possible next try.
                time.sleep(self.timeWaitNextLaunch)
                with recording(self, trace) as sbuf:
                    print("Command '" + cmd + "' failed!", file=sbuf)

        if check:
            output = ""
            if self.res.GetOutput():
                output += "\nCommand output:\n" + self.res.GetOutput()
            if self.res.GetError():
                output += "\nError output:\n" + self.res.GetError()
            if msg:
                msg += output
            if cmd:
                cmd += output
            self.assertTrue(self.res.Succeeded(), msg if (msg) else CMD_MSG(cmd))

    def HideStdout(self):
        """Hide output to stdout from the user.

        During test execution, there might be cases where we don't want to show the
        standard output to the user.  For example,

            self.runCmd(r'''sc print("\n\n\tHello!\n")''')

        tests whether command abbreviation for 'script' works or not.  There is no
        need to show the 'Hello' output to the user as long as the 'script' command
        succeeds and we are not in TraceOn() mode (see the '-t' option).

        In this case, the test method calls self.HideStdout(self) to redirect the
        sys.stdout to a null device, and restores the sys.stdout upon teardown.

        Note that you should only call this method at most once during a test case
        execution.  Any subsequent call has no effect at all."""
        if self.sys_stdout_hidden:
            return

        self.sys_stdout_hidden = True
        old_stdout = sys.stdout
        sys.stdout = open(os.devnull, "w")

        def restore_stdout():
            sys.stdout = old_stdout

        self.addTearDownHook(restore_stdout)

    # =======================================================================
    # Methods for customized teardown cleanups as well as execution of hooks.
    # =======================================================================

    def setTearDownCleanup(self, dictionary=None):
        """Register a cleanup action at tearDown() time with a dictionary"""
        self.dict = dictionary
        self.doTearDownCleanup = True

    def addTearDownCleanup(self, dictionary):
        """Add a cleanup action at tearDown() time with a dictionary"""
        self.dicts.append(dictionary)
        self.doTearDownCleanups = True

    def addTearDownHook(self, hook):
        """
        Add a function to be run during tearDown() time.

        Hooks are executed in a first come first serve manner.
        """
        if callable(hook):
            with recording(self, traceAlways) as sbuf:
                print("Adding tearDown hook:", getsource_if_available(hook), file=sbuf)
            self.hooks.append(hook)

        return self

    def deletePexpectChild(self):
        # This is for the case of directly spawning 'lldb' and interacting with it
        # using pexpect.
        if self.child and self.child.isalive():
            import pexpect

            with recording(self, traceAlways) as sbuf:
                print("tearing down the child process....", file=sbuf)
            try:
                if self.child_in_script_interpreter:
                    self.child.sendline("quit()")
                    self.child.expect_exact(self.child_prompt)
                self.child.sendline("settings set interpreter.prompt-on-quit false")
                self.child.sendline("quit")
                self.child.expect(pexpect.EOF)
            except (ValueError, pexpect.ExceptionPexpect):
                # child is already terminated
                pass
            except OSError as exception:
                import errno

                if exception.errno != errno.EIO:
                    # unexpected error
                    raise
                # child is already terminated
            finally:
                # Give it one final blow to make sure the child is terminated.
                self.child.close()

    def tearDown(self):
        """Fixture for unittest test case teardown."""
        self.deletePexpectChild()

        # Check and run any hook functions.
        for hook in reversed(self.hooks):
            with recording(self, traceAlways) as sbuf:
                print(
                    "Executing tearDown hook:", getsource_if_available(hook), file=sbuf
                )
            if funcutils.requires_self(hook):
                hook(self)
            else:
                hook()  # try the plain call and hope it works

        del self.hooks

        # Perform registered teardown cleanup.
        if doCleanup and self.doTearDownCleanup:
            self.cleanup(dictionary=self.dict)

        # In rare cases where there are multiple teardown cleanups added.
        if doCleanup and self.doTearDownCleanups:
            if self.dicts:
                for dict in reversed(self.dicts):
                    self.cleanup(dictionary=dict)

        # Remove subprocesses created by the test.
        self.cleanupSubprocesses()

        # This must be the last statement, otherwise teardown hooks or other
        # lines might depend on this still being active.
        lldb.SBDebugger.Destroy(self.dbg)
        del self.dbg

        # All modules should be orphaned now so that they can be cleared from
        # the shared module cache.
        lldb.SBModule.GarbageCollectAllocatedModules()

        # Assert that the global module cache is empty.
        # FIXME: This assert fails on Windows.
        if self.getPlatform() != "windows":
            self.assertEqual(lldb.SBModule.GetNumberAllocatedModules(), 0)

    # =========================================================
    # Various callbacks to allow introspection of test progress
    # =========================================================

    def markError(self):
        """Callback invoked when an error (unexpected exception) errored."""
        self.__errored__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "ERROR" to the stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            print("ERROR", file=sbuf)

    def markCleanupError(self):
        """Callback invoked when an error occurs while a test is cleaning up."""
        self.__cleanup_errored__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "CLEANUP_ERROR" to the stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            print("CLEANUP_ERROR", file=sbuf)

    def markFailure(self):
        """Callback invoked when a failure (test assertion failure) occurred."""
        self.__failed__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "FAIL" to the stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            print("FAIL", file=sbuf)

    def markExpectedFailure(self, err):
        """Callback invoked when an expected failure/error occurred."""
        self.__expected__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "expected failure" to the
            # stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            print("expected failure", file=sbuf)

    def markSkippedTest(self):
        """Callback invoked when a test is skipped."""
        self.__skipped__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "skipped test" to the
            # stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            print("skipped test", file=sbuf)

    def markUnexpectedSuccess(self):
        """Callback invoked when an unexpected success occurred."""
        self.__unexpected__ = True
        with recording(self, False) as sbuf:
            # False because there's no need to write "unexpected success" to the
            # stderr twice.
            # Once by the Python unittest framework, and a second time by us.
            print("unexpected success", file=sbuf)

    def getRerunArgs(self):
        return " -f %s.%s" % (self.__class__.__name__, self._testMethodName)

    def getLogBasenameForCurrentTest(self, prefix="Incomplete"):
        """
        returns a partial path that can be used as the beginning of the name of multiple
        log files pertaining to this test
        """
        return os.path.join(self.getBuildDir(), prefix)

    def dumpSessionInfo(self):
        """
        Dump the debugger interactions leading to a test error/failure.  This
        allows for more convenient postmortem analysis.

        See also LLDBTestResult (dotest.py) which is a singlton class derived
        from TextTestResult and overwrites addError, addFailure, and
        addExpectedFailure methods to allow us to to mark the test instance as
        such.
        """

        # We are here because self.tearDown() detected that this test instance
        # either errored or failed.  The lldb.test_result singleton contains
        # two lists (errors and failures) which get populated by the unittest
        # framework.  Look over there for stack trace information.
        #
        # The lists contain 2-tuples of TestCase instances and strings holding
        # formatted tracebacks.
        #
        # See http://docs.python.org/library/unittest.html#unittest.TestResult.

        # output tracebacks into session
        pairs = []
        if self.__errored__:
            pairs = configuration.test_result.errors
            prefix = "Error"
        elif self.__cleanup_errored__:
            pairs = configuration.test_result.cleanup_errors
            prefix = "CleanupError"
        elif self.__failed__:
            pairs = configuration.test_result.failures
            prefix = "Failure"
        elif self.__expected__:
            pairs = configuration.test_result.expectedFailures
            prefix = "ExpectedFailure"
        elif self.__skipped__:
            prefix = "SkippedTest"
        elif self.__unexpected__:
            prefix = "UnexpectedSuccess"
        else:
            prefix = "Success"

        if not self.__unexpected__ and not self.__skipped__:
            for test, traceback in pairs:
                if test is self:
                    print(traceback, file=self.session)

        import datetime

        print(
            "Session info generated @",
            datetime.datetime.now().ctime(),
            file=self.session,
        )
        self.session.close()
        del self.session

        # process the log files
        if prefix != "Success" or lldbtest_config.log_success:
            # keep all log files, rename them to include prefix
            src_log_basename = self.getLogBasenameForCurrentTest()
            dst_log_basename = self.getLogBasenameForCurrentTest(prefix)
            for src in self.log_files:
                if os.path.isfile(src):
                    dst = src.replace(src_log_basename, dst_log_basename)
                    if os.name == "nt" and os.path.isfile(dst):
                        # On Windows, renaming a -> b will throw an exception if
                        # b exists.  On non-Windows platforms it silently
                        # replaces the destination.  Ultimately this means that
                        # atomic renames are not guaranteed to be possible on
                        # Windows, but we need this to work anyway, so just
                        # remove the destination first if it already exists.
                        remove_file(dst)

                    lldbutil.mkdir_p(os.path.dirname(dst))
                    os.rename(src, dst)
        else:
            # success!  (and we don't want log files) delete log files
            for log_file in self.log_files:
                if os.path.isfile(log_file):
                    remove_file(log_file)

    # ====================================================
    # Config. methods supported through a plugin interface
    # (enables reading of the current test configuration)
    # ====================================================

    def hasXMLSupport(self):
        """Returns True if lldb was built with XML support. Use this check to
        enable parts of tests, if you want to skip a whole test use skipIfXmlSupportMissing
        instead."""
        return (
            lldb.SBDebugger.GetBuildConfiguration()
            .GetValueForKey("xml")
            .GetValueForKey("value")
            .GetBooleanValue(False)
        )

    def isMIPS(self):
        """Returns true if the architecture is MIPS."""
        arch = self.getArchitecture()
        if re.match("mips", arch):
            return True
        return False

    def isPPC64le(self):
        """Returns true if the architecture is PPC64LE."""
        arch = self.getArchitecture()
        if re.match("powerpc64le", arch):
            return True
        return False

    def getCPUInfo(self):
        triple = self.dbg.GetSelectedPlatform().GetTriple()

        # TODO other platforms, please implement this function
        if not re.match(".*-.*-linux", triple):
            return ""

        # Need to do something different for non-Linux/Android targets
        cpuinfo_path = self.getBuildArtifact("cpuinfo")
        if configuration.lldb_platform_name:
            self.runCmd('platform get-file "/proc/cpuinfo" ' + cpuinfo_path)
        else:
            cpuinfo_path = "/proc/cpuinfo"

        try:
            with open(cpuinfo_path, "r") as f:
                cpuinfo = f.read()
        except:
            return ""

        return cpuinfo

    def isAArch64(self):
        """Returns true if the architecture is AArch64."""
        arch = self.getArchitecture().lower()
        return arch in ["aarch64", "arm64", "arm64e"]

    def isAArch64SVE(self):
        return self.isAArch64() and "sve" in self.getCPUInfo()

    def isAArch64SME(self):
        return self.isAArch64() and "sme" in self.getCPUInfo()

    def isAArch64SME2(self):
        # If you have sme2, you also have sme.
        return self.isAArch64() and "sme2" in self.getCPUInfo()

    def isAArch64SMEFA64(self):
        # smefa64 allows the use of the full A64 instruction set in streaming
        # mode. This is required by certain test programs to setup register
        # state.
        cpuinfo = self.getCPUInfo()
        return self.isAArch64() and "sme" in cpuinfo and "smefa64" in cpuinfo

    def isAArch64MTE(self):
        return self.isAArch64() and "mte" in self.getCPUInfo()

    def isAArch64PAuth(self):
        if self.getArchitecture() == "arm64e":
            return True
        return self.isAArch64() and "paca" in self.getCPUInfo()

    def isAArch64Windows(self):
        """Returns true if the architecture is AArch64 and platform windows."""
        if self.getPlatform() == "windows":
            arch = self.getArchitecture().lower()
            return arch in ["aarch64", "arm64", "arm64e"]
        return False

    def getArchitecture(self):
        """Returns the architecture in effect the test suite is running with."""
        return lldbplatformutil.getArchitecture()

    def getLldbArchitecture(self):
        """Returns the architecture of the lldb binary."""
        return lldbplatformutil.getLLDBArchitecture()

    def getCompiler(self):
        """Returns the compiler in effect the test suite is running with."""
        return lldbplatformutil.getCompiler()

    def getCompilerBinary(self):
        """Returns the compiler binary the test suite is running with."""
        return lldbplatformutil.getCompilerBinary()

    def getCompilerVersion(self):
        """Returns a string that represents the compiler version.
        Supports: llvm, clang.
        """
        return lldbplatformutil.getCompilerVersion()

    def getDwarfVersion(self):
        """Returns the dwarf version generated by clang or '0'."""
        return lldbplatformutil.getDwarfVersion()

    def platformIsDarwin(self):
        """Returns true if the OS triple for the selected platform is any valid apple OS"""
        return lldbplatformutil.platformIsDarwin()

    def hasDarwinFramework(self):
        return self.darwinWithFramework

    def getPlatform(self):
        """Returns the target platform the test suite is running on."""
        return lldbplatformutil.getPlatform()

    def isIntelCompiler(self):
        """Returns true if using an Intel (ICC) compiler, false otherwise."""
        return any([x in self.getCompiler() for x in ["icc", "icpc", "icl"]])

    def expectedCompilerVersion(self, compiler_version):
        """Returns True iff compiler_version[1] matches the current compiler version.
        Use compiler_version[0] to specify the operator used to determine if a match has occurred.
        Any operator other than the following defaults to an equality test:
          '>', '>=', "=>", '<', '<=', '=<', '!=', "!" or 'not'

        If the current compiler version cannot be determined, we assume it is close to the top
        of trunk, so any less-than or equal-to comparisons will return False, and any
        greater-than or not-equal-to comparisons will return True.
        """
        return lldbplatformutil.expectedCompilerVersion(compiler_version)

    def expectedCompiler(self, compilers):
        """Returns True iff any element of compilers is a sub-string of the current compiler."""
        return lldbplatformutil.expectedCompiler(compilers)

    def expectedArch(self, archs):
        """Returns True iff any element of archs is a sub-string of the current architecture."""
        if archs is None:
            return True

        for arch in archs:
            if arch in self.getArchitecture():
                return True

        return False

    def getRunOptions(self):
        """Command line option for -A and -C to run this test again, called from
        self.dumpSessionInfo()."""
        arch = self.getArchitecture()
        comp = self.getCompiler()
        option_str = ""
        if arch:
            option_str = "-A " + arch
        if comp:
            option_str += " -C " + comp
        return option_str

    def getDebugInfo(self):
        method = getattr(self, self.testMethodName)
        return getattr(method, "debug_info", None)

    def build(
        self,
        debug_info=None,
        architecture=None,
        compiler=None,
        dictionary=None,
        make_targets=None,
    ):
        """Platform specific way to build binaries."""
        if not architecture and configuration.arch:
            architecture = configuration.arch

        if debug_info is None:
            debug_info = self.getDebugInfo()

        dictionary = lldbplatformutil.finalize_build_dictionary(dictionary)

        testdir = self.mydir
        testname = self.getBuildDirBasename()

        module = builder_module()
        command = builder_module().getBuildCommand(
            debug_info,
            architecture,
            compiler,
            dictionary,
            testdir,
            testname,
            make_targets,
        )
        if command is None:
            raise Exception("Don't know how to build binary")

        self.runBuildCommand(command)

    def runBuildCommand(self, command):
        self.trace(seven.join_for_shell(command))
        try:
            output = check_output(command, stderr=STDOUT, errors="replace")
        except CalledProcessError as cpe:
            raise build_exception.BuildError(cpe)
        self.trace(output)

    # ==================================================
    # Build methods supported through a plugin interface
    # ==================================================

    def getstdlibFlag(self):
        """Returns the proper -stdlib flag, or empty if not required."""
        if (
            self.platformIsDarwin()
            or self.getPlatform() == "freebsd"
            or self.getPlatform() == "openbsd"
        ):
            stdlibflag = "-stdlib=libc++"
        else:  # this includes NetBSD
            stdlibflag = ""
        return stdlibflag

    def getstdFlag(self):
        """Returns the proper stdflag."""
        if "gcc" in self.getCompiler() and "4.6" in self.getCompilerVersion():
            stdflag = "-std=c++0x"
        else:
            stdflag = "-std=c++11"
        return stdflag

    def buildDriver(self, sources, exe_name):
        """Platform-specific way to build a program that links with LLDB (via the liblldb.so
        or LLDB.framework).
        """
        stdflag = self.getstdFlag()
        stdlibflag = self.getstdlibFlag()

        lib_dir = configuration.lldb_libs_dir
        if self.hasDarwinFramework():
            d = {
                "CXX_SOURCES": sources,
                "EXE": exe_name,
                "CFLAGS_EXTRAS": "%s %s" % (stdflag, stdlibflag),
                "FRAMEWORK_INCLUDES": "-F%s" % self.framework_dir,
                "LD_EXTRAS": "%s -Wl,-rpath,%s" % (self.lib_lldb, self.framework_dir),
            }
        elif sys.platform.startswith("win"):
            d = {
                "CXX_SOURCES": sources,
                "EXE": exe_name,
                "CFLAGS_EXTRAS": "%s %s -I%s -I%s"
                % (
                    stdflag,
                    stdlibflag,
                    os.path.join(os.environ["LLDB_SRC"], "include"),
                    os.path.join(configuration.lldb_obj_root, "include"),
                ),
                "LD_EXTRAS": "-L%s -lliblldb" % lib_dir,
            }
        else:
            d = {
                "CXX_SOURCES": sources,
                "EXE": exe_name,
                "CFLAGS_EXTRAS": "%s %s -I%s -I%s"
                % (
                    stdflag,
                    stdlibflag,
                    os.path.join(os.environ["LLDB_SRC"], "include"),
                    os.path.join(configuration.lldb_obj_root, "include"),
                ),
                "LD_EXTRAS": "-L%s -llldb -Wl,-rpath,%s" % (lib_dir, lib_dir),
            }
        if self.TraceOn():
            print("Building LLDB Driver (%s) from sources %s" % (exe_name, sources))

        self.build(dictionary=d)

    def buildLibrary(self, sources, lib_name):
        """Platform specific way to build a default library."""

        stdflag = self.getstdFlag()

        lib_dir = configuration.lldb_libs_dir
        if self.hasDarwinFramework():
            d = {
                "DYLIB_CXX_SOURCES": sources,
                "DYLIB_NAME": lib_name,
                "CFLAGS_EXTRAS": "%s -stdlib=libc++ -I%s"
                % (stdflag, os.path.join(configuration.lldb_obj_root, "include")),
                "FRAMEWORK_INCLUDES": "-F%s" % self.framework_dir,
                "LD_EXTRAS": "%s -Wl,-rpath,%s -dynamiclib"
                % (self.lib_lldb, self.framework_dir),
            }
        elif self.getPlatform() == "windows":
            d = {
                "DYLIB_CXX_SOURCES": sources,
                "DYLIB_NAME": lib_name,
                "CFLAGS_EXTRAS": "%s -I%s -I%s"
                % (
                    stdflag,
                    os.path.join(os.environ["LLDB_SRC"], "include"),
                    os.path.join(configuration.lldb_obj_root, "include"),
                ),
                "LD_EXTRAS": "-shared -l%s\\liblldb.lib" % lib_dir,
            }
        else:
            d = {
                "DYLIB_CXX_SOURCES": sources,
                "DYLIB_NAME": lib_name,
                "CFLAGS_EXTRAS": "%s -I%s -I%s -fPIC"
                % (
                    stdflag,
                    os.path.join(os.environ["LLDB_SRC"], "include"),
                    os.path.join(configuration.lldb_obj_root, "include"),
                ),
                "LD_EXTRAS": "-shared -L%s -llldb -Wl,-rpath,%s" % (lib_dir, lib_dir),
            }
        if self.TraceOn():
            print("Building LLDB Library (%s) from sources %s" % (lib_name, sources))

        self.build(dictionary=d)

    def buildProgram(self, sources, exe_name):
        """Platform specific way to build an executable from C/C++ sources."""
        d = {"CXX_SOURCES": sources, "EXE": exe_name}
        self.build(dictionary=d)

    def findBuiltClang(self):
        """Tries to find and use Clang from the build directory as the compiler (instead of the system compiler)."""
        paths_to_try = [
            "llvm-build/Release+Asserts/x86_64/bin/clang",
            "llvm-build/Debug+Asserts/x86_64/bin/clang",
            "llvm-build/Release/x86_64/bin/clang",
            "llvm-build/Debug/x86_64/bin/clang",
        ]
        lldb_root_path = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..")
        for p in paths_to_try:
            path = os.path.join(lldb_root_path, p)
            if os.path.exists(path):
                return path

        # Tries to find clang at the same folder as the lldb
        lldb_dir = os.path.dirname(lldbtest_config.lldbExec)
        path = shutil.which("clang", path=lldb_dir)
        if path is not None:
            return path

        return os.environ["CC"]

    def yaml2obj(self, yaml_path, obj_path, max_size=None):
        """
        Create an object file at the given path from a yaml file.

        Throws subprocess.CalledProcessError if the object could not be created.
        """
        yaml2obj_bin = configuration.get_yaml2obj_path()
        if not yaml2obj_bin:
            self.assertTrue(False, "No valid yaml2obj executable specified")
        command = [yaml2obj_bin, "-o=%s" % obj_path, yaml_path]
        if max_size is not None:
            command += ["--max-size=%d" % max_size]
        self.runBuildCommand(command)

    def cleanup(self, dictionary=None):
        """Platform specific way to do cleanup after build."""
        module = builder_module()
        if not module.cleanup(dictionary):
            raise Exception(
                "Don't know how to do cleanup with dictionary: " + dictionary
            )

    def invoke(self, obj, name, trace=False):
        """Use reflection to call a method dynamically with no argument."""
        trace = True if traceAlways else trace

        method = getattr(obj, name)
        import inspect

        self.assertTrue(
            inspect.ismethod(method), name + "is a method name of object: " + str(obj)
        )
        result = method()
        with recording(self, trace) as sbuf:
            print(str(method) + ":", result, file=sbuf)
        return result

    def getLibcPlusPlusLibs(self):
        if self.getPlatform() in ("freebsd", "linux", "netbsd", "openbsd"):
            return ["libc++.so.1"]
        else:
            return ["libc++.1.dylib", "libc++abi."]

    def run_platform_command(self, cmd):
        platform = self.dbg.GetSelectedPlatform()
        shell_command = lldb.SBPlatformShellCommand(cmd)
        err = platform.Run(shell_command)
        return (err, shell_command.GetStatus(), shell_command.GetOutput())

    def get_stats(self, options=None):
        """
        Get the output of the "statistics dump" with optional extra options
        and return the JSON as a python dictionary.
        """
        return_obj = lldb.SBCommandReturnObject()
        command = "statistics dump "
        if options is not None:
            command += options
        self.ci.HandleCommand(command, return_obj, False)
        metrics_json = return_obj.GetOutput()
        return json.loads(metrics_json)


# Metaclass for TestBase to change the list of test metods when a new TestCase is loaded.
# We change the test methods to create a new test method for each test for each debug info we are
# testing. The name of the new test method will be '<original-name>_<debug-info>' and with adding
# the new test method we remove the old method at the same time. This functionality can be
# supressed by at test case level setting the class attribute NO_DEBUG_INFO_TESTCASE or at test
# level by using the decorator @no_debug_info_test.


class LLDBTestCaseFactory(type):
    def __new__(cls, name, bases, attrs):
        original_testcase = super(LLDBTestCaseFactory, cls).__new__(
            cls, name, bases, attrs
        )
        if original_testcase.NO_DEBUG_INFO_TESTCASE:
            return original_testcase

        # Default implementation for skip/xfail reason based on the debug category,
        # where "None" means to run the test as usual.
        def no_reason(_):
            return None

        newattrs = {}
        for attrname, attrvalue in attrs.items():
            if attrname.startswith("test") and not getattr(
                attrvalue, "__no_debug_info_test__", False
            ):
                # If any debug info categories were explicitly tagged, assume that list to be
                # authoritative.  If none were specified, try with all debug
                # info formats.
                all_dbginfo_categories = set(
                    test_categories.debug_info_categories.keys()
                )
                categories = (
                    set(getattr(attrvalue, "categories", [])) & all_dbginfo_categories
                )
                if not categories:
                    categories = [
                        category
                        for category, can_replicate in test_categories.debug_info_categories.items()
                        if can_replicate
                    ]

                xfail_for_debug_info_cat_fn = getattr(
                    attrvalue, "__xfail_for_debug_info_cat_fn__", no_reason
                )
                skip_for_debug_info_cat_fn = getattr(
                    attrvalue, "__skip_for_debug_info_cat_fn__", no_reason
                )
                for cat in categories:

                    @decorators.add_test_categories([cat])
                    @wraps(attrvalue)
                    def test_method(self, attrvalue=attrvalue):
                        return attrvalue(self)

                    method_name = attrname + "_" + cat
                    test_method.__name__ = method_name
                    test_method.debug_info = cat

                    xfail_reason = xfail_for_debug_info_cat_fn(cat)
                    if xfail_reason:
                        test_method = unittest.expectedFailure(test_method)

                    skip_reason = skip_for_debug_info_cat_fn(cat)
                    if skip_reason:
                        test_method = unittest.skip(skip_reason)(test_method)

                    newattrs[method_name] = test_method

            else:
                newattrs[attrname] = attrvalue
        return super(LLDBTestCaseFactory, cls).__new__(cls, name, bases, newattrs)


# Setup the metaclass for this class to change the list of the test
# methods when a new class is loaded


class TestBase(Base, metaclass=LLDBTestCaseFactory):
    """
    This abstract base class is meant to be subclassed.  It provides default
    implementations for setUpClass(), tearDownClass(), setUp(), and tearDown(),
    among other things.

    Important things for test class writers:

        - The setUp method sets up things to facilitate subsequent interactions
          with the debugger as part of the test.  These include:
              - populate the test method name
              - create/get a debugger set with synchronous mode (self.dbg)
              - get the command interpreter from with the debugger (self.ci)
              - create a result object for use with the command interpreter
                (self.res)
              - plus other stuffs

        - The tearDown method tries to perform some necessary cleanup on behalf
          of the test to return the debugger to a good state for the next test.
          These include:
              - execute any tearDown hooks registered by the test method with
                TestBase.addTearDownHook(); examples can be found in
                settings/TestSettings.py
              - kill the inferior process associated with each target, if any,
                and, then delete the target from the debugger's target list
              - perform build cleanup before running the next test method in the
                same test class; examples of registering for this service can be
                found in types/TestIntegerTypes.py with the call:
                    - self.setTearDownCleanup(dictionary=d)

        - Similarly setUpClass and tearDownClass perform classwise setup and
          teardown fixtures.  The tearDownClass method invokes a default build
          cleanup for the entire test class;  also, subclasses can implement the
          classmethod classCleanup(cls) to perform special class cleanup action.

        - The instance methods runCmd and expect are used heavily by existing
          test cases to send a command to the command interpreter and to perform
          string/pattern matching on the output of such command execution.  The
          expect method also provides a mode to peform string/pattern matching
          without running a command.

        - The build method is used to build the binaries used during a
          particular test scenario.  A plugin should be provided for the
          sys.platform running the test suite.  The Mac OS X implementation is
          located in builders/darwin.py.
    """

    # Subclasses can set this to true (if they don't depend on debug info) to avoid running the
    # test multiple times with various debug info types.
    NO_DEBUG_INFO_TESTCASE = False

    def generateSource(self, source):
        template = source + ".template"
        temp = os.path.join(self.getSourceDir(), template)
        with open(temp, "r") as f:
            content = f.read()

        public_api_dir = os.path.join(os.environ["LLDB_SRC"], "include", "lldb", "API")

        # Look under the include/lldb/API directory and add #include statements
        # for all the SB API headers.
        public_headers = os.listdir(public_api_dir)
        # For different platforms, the include statement can vary.
        if self.hasDarwinFramework():
            include_stmt = "'#include <%s>' % os.path.join('LLDB', header)"
        else:
            include_stmt = (
                "'#include <%s>' % os.path.join(r'" + public_api_dir + "', header)"
            )
        list = [
            eval(include_stmt)
            for header in public_headers
            if (header.startswith("SB") and header.endswith(".h"))
        ]
        includes = "\n".join(list)
        new_content = content.replace("%include_SB_APIs%", includes)
        new_content = new_content.replace("%SOURCE_DIR%", self.getSourceDir())
        src = os.path.join(self.getBuildDir(), source)
        with open(src, "w") as f:
            f.write(new_content)

        self.addTearDownHook(lambda: os.remove(src))

    def setUp(self):
        # Works with the test driver to conditionally skip tests via
        # decorators.
        Base.setUp(self)

        for s in self.setUpCommands():
            self.runCmd(s)

        # We want our debugger to be synchronous.
        self.dbg.SetAsync(False)

        # Retrieve the associated command interpreter instance.
        self.ci = self.dbg.GetCommandInterpreter()
        if not self.ci:
            raise Exception("Could not get the command interpreter")

        # And the result object.
        self.res = lldb.SBCommandReturnObject()

    def registerSharedLibrariesWithTarget(self, target, shlibs):
        """If we are remotely running the test suite, register the shared libraries with the target so they get uploaded, otherwise do nothing

        Any modules in the target that have their remote install file specification set will
        get uploaded to the remote host. This function registers the local copies of the
        shared libraries with the target and sets their remote install locations so they will
        be uploaded when the target is run.
        """
        if not shlibs or not self.platformContext:
            return None

        shlib_environment_var = self.platformContext.shlib_environment_var
        shlib_prefix = self.platformContext.shlib_prefix
        shlib_extension = "." + self.platformContext.shlib_extension

        dirs = []
        # Add any shared libraries to our target if remote so they get
        # uploaded into the working directory on the remote side
        for name in shlibs:
            # The path can be a full path to a shared library, or a make file name like "Foo" for
            # "libFoo.dylib" or "libFoo.so", or "Foo.so" for "Foo.so" or "libFoo.so", or just a
            # basename like "libFoo.so". So figure out which one it is and resolve the local copy
            # of the shared library accordingly
            if os.path.isfile(name):
                local_shlib_path = (
                    name  # name is the full path to the local shared library
                )
            else:
                # Check relative names
                local_shlib_path = os.path.join(
                    self.getBuildDir(), shlib_prefix + name + shlib_extension
                )
                if not os.path.exists(local_shlib_path):
                    local_shlib_path = os.path.join(
                        self.getBuildDir(), name + shlib_extension
                    )
                    if not os.path.exists(local_shlib_path):
                        local_shlib_path = os.path.join(self.getBuildDir(), name)

                # Make sure we found the local shared library in the above code
                self.assertTrue(os.path.exists(local_shlib_path))

            # Add the shared library to our target
            shlib_module = target.AddModule(local_shlib_path, None, None, None)
            if lldb.remote_platform:
                # We must set the remote install location if we want the shared library
                # to get uploaded to the remote target
                remote_shlib_path = lldbutil.append_to_process_working_directory(
                    self, os.path.basename(local_shlib_path)
                )
                shlib_module.SetRemoteInstallFileSpec(
                    lldb.SBFileSpec(remote_shlib_path, False)
                )
                dir_to_add = self.get_process_working_directory()
            else:
                dir_to_add = os.path.dirname(local_shlib_path)

            if dir_to_add not in dirs:
                dirs.append(dir_to_add)

        env_value = self.platformContext.shlib_path_separator.join(dirs)
        return ["%s=%s" % (shlib_environment_var, env_value)]

    def registerSanitizerLibrariesWithTarget(self, target):
        runtimes = []
        for m in target.module_iter():
            libspec = m.GetFileSpec()
            if "clang_rt" in libspec.GetFilename():
                runtimes.append(
                    os.path.join(libspec.GetDirectory(), libspec.GetFilename())
                )
        return self.registerSharedLibrariesWithTarget(target, runtimes)

    # utility methods that tests can use to access the current objects
    def target(self):
        if not self.dbg:
            raise Exception("Invalid debugger instance")
        return self.dbg.GetSelectedTarget()

    def process(self):
        if not self.dbg:
            raise Exception("Invalid debugger instance")
        return self.dbg.GetSelectedTarget().GetProcess()

    def thread(self):
        if not self.dbg:
            raise Exception("Invalid debugger instance")
        return self.dbg.GetSelectedTarget().GetProcess().GetSelectedThread()

    def frame(self):
        if not self.dbg:
            raise Exception("Invalid debugger instance")
        return (
            self.dbg.GetSelectedTarget()
            .GetProcess()
            .GetSelectedThread()
            .GetSelectedFrame()
        )

    def get_process_working_directory(self):
        """Get the working directory that should be used when launching processes for local or remote processes."""
        if lldb.remote_platform:
            # Remote tests set the platform working directory up in
            # TestBase.setUp()
            return lldb.remote_platform.GetWorkingDirectory()
        else:
            # local tests change directory into each test subdirectory
            return self.getBuildDir()

    def tearDown(self):
        # Ensure all the references to SB objects have gone away so that we can
        # be sure that all test-specific resources have been freed before we
        # attempt to delete the targets.
        gc.collect()

        # Delete the target(s) from the debugger as a general cleanup step.
        # This includes terminating the process for each target, if any.
        # We'd like to reuse the debugger for our next test without incurring
        # the initialization overhead.
        targets = []
        for target in self.dbg:
            if target:
                targets.append(target)
                process = target.GetProcess()
                if process:
                    rc = self.invoke(process, "Kill")
                    assert rc.Success()
        for target in targets:
            self.dbg.DeleteTarget(target)

        # Assert that all targets are deleted.
        self.assertEqual(self.dbg.GetNumTargets(), 0)

        # Do this last, to make sure it's in reverse order from how we setup.
        Base.tearDown(self)

    def switch_to_thread_with_stop_reason(self, stop_reason):
        """
        Run the 'thread list' command, and select the thread with stop reason as
        'stop_reason'.  If no such thread exists, no select action is done.
        """
        from .lldbutil import stop_reason_to_str

        self.runCmd("thread list")
        output = self.res.GetOutput()
        thread_line_pattern = re.compile(
            "^[ *] thread #([0-9]+):.*stop reason = %s"
            % stop_reason_to_str(stop_reason)
        )
        for line in output.splitlines():
            matched = thread_line_pattern.match(line)
            if matched:
                self.runCmd("thread select %s" % matched.group(1))

    def match(
        self, str, patterns, msg=None, trace=False, error=False, matching=True, exe=True
    ):
        """run command in str, and match the result against regexp in patterns returning the match object for the first matching pattern

        Otherwise, all the arguments have the same meanings as for the expect function
        """

        trace = True if traceAlways else trace

        if exe:
            # First run the command.  If we are expecting error, set check=False.
            # Pass the assert message along since it provides more semantic
            # info.
            self.runCmd(str, msg=msg, trace=(True if trace else False), check=not error)

            # Then compare the output against expected strings.
            output = self.res.GetError() if error else self.res.GetOutput()

            # If error is True, the API client expects the command to fail!
            if error:
                self.assertFalse(
                    self.res.Succeeded(), "Command '" + str + "' is expected to fail!"
                )
        else:
            # No execution required, just compare str against the golden input.
            output = str
            with recording(self, trace) as sbuf:
                print("looking at:", output, file=sbuf)

        # The heading says either "Expecting" or "Not expecting".
        heading = "Expecting" if matching else "Not expecting"

        for pattern in patterns:
            # Match Objects always have a boolean value of True.
            match_object = re.search(pattern, output)
            matched = bool(match_object)
            with recording(self, trace) as sbuf:
                print("%s pattern: %s" % (heading, pattern), file=sbuf)
                print("Matched" if matched else "Not matched", file=sbuf)
            if matched:
                break

        self.assertTrue(
            matched if matching else not matched,
            msg if msg else EXP_MSG(str, output, exe),
        )

        return match_object

    def check_completion_with_desc(
        self, str_input, match_desc_pairs, enforce_order=False
    ):
        """
        Checks that when the given input is completed at the given list of
        completions and descriptions is returned.
        :param str_input: The input that should be completed. The completion happens at the end of the string.
        :param match_desc_pairs: A list of pairs that indicate what completions have to be in the list of
                                 completions returned by LLDB. The first element of the pair is the completion
                                 string that LLDB should generate and the second element the description.
        :param enforce_order: True iff the order in which the completions are returned by LLDB
                              should match the order of the match_desc_pairs pairs.
        """
        interp = self.dbg.GetCommandInterpreter()
        match_strings = lldb.SBStringList()
        description_strings = lldb.SBStringList()
        num_matches = interp.HandleCompletionWithDescriptions(
            str_input, len(str_input), 0, -1, match_strings, description_strings
        )
        self.assertEqual(len(description_strings), len(match_strings))

        # The index of the last matched description in description_strings or
        # -1 if no description has been matched yet.
        last_found_index = -1
        out_of_order_errors = ""
        missing_pairs = []
        for pair in match_desc_pairs:
            found_pair = False
            for i in range(num_matches + 1):
                match_candidate = match_strings.GetStringAtIndex(i)
                description_candidate = description_strings.GetStringAtIndex(i)
                if match_candidate == pair[0] and description_candidate == pair[1]:
                    found_pair = True
                    if enforce_order and last_found_index > i:
                        new_err = (
                            "Found completion "
                            + pair[0]
                            + " at index "
                            + str(i)
                            + " in returned completion list but "
                            + "should have been after completion "
                            + match_strings.GetStringAtIndex(last_found_index)
                            + " (index:"
                            + str(last_found_index)
                            + ")\n"
                        )
                        out_of_order_errors += new_err
                    last_found_index = i
                    break
            if not found_pair:
                missing_pairs.append(pair)

        error_msg = ""
        got_failure = False
        if len(missing_pairs):
            got_failure = True
            error_msg += "Missing pairs:\n"
            for pair in missing_pairs:
                error_msg += " [" + pair[0] + ":" + pair[1] + "]\n"
        if len(out_of_order_errors):
            got_failure = True
            error_msg += out_of_order_errors
        if got_failure:
            error_msg += (
                "Got the following " + str(num_matches) + " completions back:\n"
            )
            for i in range(num_matches + 1):
                match_candidate = match_strings.GetStringAtIndex(i)
                description_candidate = description_strings.GetStringAtIndex(i)
                error_msg += (
                    "["
                    + match_candidate
                    + ":"
                    + description_candidate
                    + "] index "
                    + str(i)
                    + "\n"
                )
            self.assertFalse(got_failure, error_msg)

    def complete_from_to(self, str_input, patterns):
        """Test that the completion mechanism completes str_input to patterns,
        where patterns could be a single pattern-string or a list of
        pattern-strings.

        If there is only one pattern and it is exactly equal to str_input, this
        assumes that there should be no completions provided and that the result
        should be the same as the input."""

        # Patterns should not be None in order to proceed.
        self.assertFalse(patterns is None)
        # And should be either a string or list of strings.  Check for list type
        # below, if not, make a list out of the singleton string.  If patterns
        # is not a string or not a list of strings, there'll be runtime errors
        # later on.
        if not isinstance(patterns, list):
            patterns = [patterns]

        interp = self.dbg.GetCommandInterpreter()
        match_strings = lldb.SBStringList()
        num_matches = interp.HandleCompletion(
            str_input, len(str_input), 0, -1, match_strings
        )
        common_match = match_strings.GetStringAtIndex(0)
        if num_matches == 0:
            compare_string = str_input
        else:
            if common_match is not None and len(common_match) > 0:
                compare_string = str_input + common_match
            else:
                compare_string = ""
                for idx in range(1, num_matches + 1):
                    compare_string += match_strings.GetStringAtIndex(idx) + "\n"

        if len(patterns) == 1 and str_input == patterns[0] and num_matches:
            self.fail("Expected no completions but got:\n" + compare_string)

        for p in patterns:
            self.expect(
                compare_string,
                msg=COMPLETION_MSG(str_input, p, match_strings),
                exe=False,
                substrs=[p],
            )

    def completions_match(self, command, completions):
        """Checks that the completions for the given command are equal to the
        given list of completions"""
        interp = self.dbg.GetCommandInterpreter()
        match_strings = lldb.SBStringList()
        interp.HandleCompletion(command, len(command), 0, -1, match_strings)
        # match_strings is a 1-indexed list, so we have to slice...
        self.assertCountEqual(
            completions, list(match_strings)[1:], "List of returned completion is wrong"
        )

    def completions_contain(self, command, completions):
        """Checks that the completions for the given command contain the given
        list of completions."""
        interp = self.dbg.GetCommandInterpreter()
        match_strings = lldb.SBStringList()
        interp.HandleCompletion(command, len(command), 0, -1, match_strings)
        for completion in completions:
            # match_strings is a 1-indexed list, so we have to slice...
            self.assertIn(
                completion, list(match_strings)[1:], "Couldn't find expected completion"
            )

    def filecheck(
        self, command, check_file, filecheck_options="", expect_cmd_failure=False
    ):
        # Run the command.
        self.runCmd(
            command,
            check=(not expect_cmd_failure),
            msg="FileCheck'ing result of `{0}`".format(command),
        )

        self.assertTrue((not expect_cmd_failure) == self.res.Succeeded())

        # Get the error text if there was an error, and the regular text if not.
        output = self.res.GetOutput() if self.res.Succeeded() else self.res.GetError()

        # Assemble the absolute path to the check file. As a convenience for
        # LLDB inline tests, assume that the check file is a relative path to
        # a file within the inline test directory.
        if check_file.endswith(".pyc"):
            check_file = check_file[:-1]
        check_file_abs = os.path.abspath(check_file)

        # Run FileCheck.
        filecheck_bin = configuration.get_filecheck_path()
        if not filecheck_bin:
            self.assertTrue(False, "No valid FileCheck executable specified")
        filecheck_args = [filecheck_bin, check_file_abs]
        if filecheck_options:
            filecheck_args.append(filecheck_options)
        subproc = Popen(
            filecheck_args,
            stdin=PIPE,
            stdout=PIPE,
            stderr=PIPE,
            universal_newlines=True,
        )
        cmd_stdout, cmd_stderr = subproc.communicate(input=output)
        cmd_status = subproc.returncode

        filecheck_cmd = " ".join(filecheck_args)
        filecheck_trace = """
--- FileCheck trace (code={0}) ---
{1}

FileCheck input:
{2}

FileCheck output:
{3}
{4}
""".format(
            cmd_status, filecheck_cmd, output, cmd_stdout, cmd_stderr
        )

        trace = cmd_status != 0 or traceAlways
        with recording(self, trace) as sbuf:
            print(filecheck_trace, file=sbuf)

        self.assertTrue(cmd_status == 0)

    def expect(
        self,
        string,
        msg=None,
        patterns=None,
        startstr=None,
        endstr=None,
        substrs=None,
        trace=False,
        error=False,
        ordered=True,
        matching=True,
        exe=True,
        inHistory=False,
    ):
        """
        Similar to runCmd; with additional expect style output matching ability.

        Ask the command interpreter to handle the command and then check its
        return status.  The 'msg' parameter specifies an informational assert
        message.  We expect the output from running the command to start with
        'startstr', matches the substrings contained in 'substrs', and regexp
        matches the patterns contained in 'patterns'.

        When matching is true and ordered is true, which are both the default,
        the strings in the substrs array have to appear in the command output
        in the order in which they appear in the array.

        If the keyword argument error is set to True, it signifies that the API
        client is expecting the command to fail.  In this case, the error stream
        from running the command is retrieved and compared against the golden
        input, instead.

        If the keyword argument matching is set to False, it signifies that the API
        client is expecting the output of the command not to match the golden
        input.

        Finally, the required argument 'string' represents the lldb command to be
        sent to the command interpreter.  In case the keyword argument 'exe' is
        set to False, the 'string' is treated as a string to be matched/not-matched
        against the golden input.
        """
        # Catch cases where `expect` has been miscalled. Specifically, prevent
        # this easy to make mistake:
        #     self.expect("lldb command", "some substr")
        # The `msg` parameter is used only when a failed match occurs. A failed
        # match can only occur when one of `patterns`, `startstr`, `endstr`, or
        # `substrs` has been given. Thus, if a `msg` is given, it's an error to
        # not also provide one of the matcher parameters.
        if msg and not (patterns or startstr or endstr or substrs or error):
            assert False, "expect() missing a matcher argument"

        # Check `patterns` and `substrs` are not accidentally given as strings.
        assert not isinstance(patterns, str), "patterns must be a collection of strings"
        assert not isinstance(substrs, str), "substrs must be a collection of strings"

        trace = True if traceAlways else trace

        if exe:
            # First run the command.  If we are expecting error, set check=False.
            # Pass the assert message along since it provides more semantic
            # info.
            self.runCmd(
                string,
                msg=msg,
                trace=(True if trace else False),
                check=not error,
                inHistory=inHistory,
            )

            # Then compare the output against expected strings.
            output = self.res.GetError() if error else self.res.GetOutput()

            # If error is True, the API client expects the command to fail!
            if error:
                self.assertFalse(
                    self.res.Succeeded(),
                    "Command '" + string + "' is expected to fail!",
                )
        else:
            # No execution required, just compare string against the golden input.
            if isinstance(string, lldb.SBCommandReturnObject):
                output = string.GetOutput()
            else:
                output = string
            with recording(self, trace) as sbuf:
                print("looking at:", output, file=sbuf)

        expecting_str = "Expecting" if matching else "Not expecting"

        def found_str(matched):
            return "was found" if matched else "was not found"

        # To be used as assert fail message and/or trace content
        log_lines = [
            "{}:".format("Ran command" if exe else "Checking string"),
            '"{}"'.format(string),
            # Space out command and output
            "",
        ]
        if exe:
            # Newline before output to make large strings more readable
            log_lines.append("Got output:\n{}".format(output))

        # Assume that we start matched if we want a match
        # Meaning if you have no conditions, matching or
        # not matching will always pass
        matched = matching

        # We will stop checking on first failure
        if startstr:
            matched = output.startswith(startstr)
            log_lines.append(
                '{} start string: "{}" ({})'.format(
                    expecting_str, startstr, found_str(matched)
                )
            )

        if endstr and matched == matching:
            matched = output.endswith(endstr)
            log_lines.append(
                '{} end string: "{}" ({})'.format(
                    expecting_str, endstr, found_str(matched)
                )
            )

        if substrs and matched == matching:
            start = 0
            for substr in substrs:
                index = output[start:].find(substr)
                start = start + index + len(substr) if ordered and matching else 0
                matched = index != -1
                log_lines.append(
                    '{} sub string: "{}" ({})'.format(
                        expecting_str, substr, found_str(matched)
                    )
                )

                if matched != matching:
                    break

        if patterns and matched == matching:
            for pattern in patterns:
                matched = re.search(pattern, output)

                pattern_line = '{} regex pattern: "{}" ({}'.format(
                    expecting_str, pattern, found_str(matched)
                )
                if matched:
                    pattern_line += ', matched "{}"'.format(matched.group(0))
                pattern_line += ")"
                log_lines.append(pattern_line)

                # Convert to bool because match objects
                # are True-ish but is not True itself
                matched = bool(matched)
                if matched != matching:
                    break

        # If a check failed, add any extra assert message
        if msg is not None and matched != matching:
            log_lines.append(msg)

        log_msg = "\n".join(log_lines)
        with recording(self, trace) as sbuf:
            print(log_msg, file=sbuf)
        if matched != matching:
            self.fail(log_msg)

    def expect_expr(
        self,
        expr,
        result_summary=None,
        result_value=None,
        result_type=None,
        result_children=None,
    ):
        """
        Evaluates the given expression and verifies the result.
        :param expr: The expression as a string.
        :param result_summary: The summary that the expression should have. None if the summary should not be checked.
        :param result_value: The value that the expression should have. None if the value should not be checked.
        :param result_type: The type that the expression result should have. None if the type should not be checked.
        :param result_children: The expected children of the expression result
                                as a list of ValueChecks. None if the children shouldn't be checked.
        """
        self.assertTrue(
            expr.strip() == expr,
            "Expression contains trailing/leading whitespace: '" + expr + "'",
        )

        frame = self.frame()
        options = lldb.SBExpressionOptions()

        # Disable fix-its that tests don't pass by accident.
        options.SetAutoApplyFixIts(False)

        # Set the usual default options for normal expressions.
        options.SetIgnoreBreakpoints(True)

        if self.frame().IsValid():
            options.SetLanguage(frame.GuessLanguage())
            eval_result = self.frame().EvaluateExpression(expr, options)
        else:
            target = self.target()
            # If there is no selected target, run the expression in the dummy
            # target.
            if not target.IsValid():
                target = self.dbg.GetDummyTarget()
            eval_result = target.EvaluateExpression(expr, options)

        value_check = ValueCheck(
            type=result_type,
            value=result_value,
            summary=result_summary,
            children=result_children,
        )
        value_check.check_value(self, eval_result, str(eval_result))
        return eval_result

    def expect_var_path(
        self, var_path, summary=None, value=None, type=None, children=None
    ):
        """
        Evaluates the given variable path and verifies the result.
        See also 'frame variable' and SBFrame.GetValueForVariablePath.
        :param var_path: The variable path as a string.
        :param summary: The summary that the variable should have. None if the summary should not be checked.
        :param value: The value that the variable should have. None if the value should not be checked.
        :param type: The type that the variable result should have. None if the type should not be checked.
        :param children: The expected children of the variable  as a list of ValueChecks.
                         None if the children shouldn't be checked.
        """
        self.assertTrue(
            var_path.strip() == var_path,
            "Expression contains trailing/leading whitespace: '" + var_path + "'",
        )

        frame = self.frame()
        eval_result = frame.GetValueForVariablePath(var_path)

        value_check = ValueCheck(
            type=type, value=value, summary=summary, children=children
        )
        value_check.check_value(self, eval_result, str(eval_result))
        return eval_result

    """Assert that an lldb.SBError is in the "success" state."""

    def assertSuccess(self, obj, msg=None):
        if not obj.Success():
            error = obj.GetCString()
            self.fail(self._formatMessage(msg, "'{}' is not success".format(error)))

    """Assert that an lldb.SBError is in the "failure" state."""

    def assertFailure(self, obj, error_str=None, msg=None):
        if obj.Success():
            self.fail(self._formatMessage(msg, "Error not in a fail state"))

        if error_str is None:
            return

        error = obj.GetCString()
        self.assertEqual(error, error_str, msg)

    """Assert that a command return object is successful"""

    def assertCommandReturn(self, obj, msg=None):
        if not obj.Succeeded():
            error = obj.GetError()
            self.fail(self._formatMessage(msg, "'{}' is not success".format(error)))

    """Assert two states are equal"""

    def assertState(self, first, second, msg=None):
        if first != second:
            error = "{} ({}) != {} ({})".format(
                lldbutil.state_type_to_str(first),
                first,
                lldbutil.state_type_to_str(second),
                second,
            )
            self.fail(self._formatMessage(msg, error))

    """Assert two stop reasons are equal"""

    def assertStopReason(self, first, second, msg=None):
        if first != second:
            error = "{} ({}) != {} ({})".format(
                lldbutil.stop_reason_to_str(first),
                first,
                lldbutil.stop_reason_to_str(second),
                second,
            )
            self.fail(self._formatMessage(msg, error))

    def createTestTarget(self, file_path=None, msg=None, load_dependent_modules=True):
        """
        Creates a target from the file found at the given file path.
        Asserts that the resulting target is valid.
        :param file_path: The file path that should be used to create the target.
                          The default argument opens the current default test
                          executable in the current test directory.
        :param msg: A custom error message.
        """
        if file_path is None:
            file_path = self.getBuildArtifact("a.out")
        error = lldb.SBError()
        triple = ""
        platform = ""
        target = self.dbg.CreateTarget(
            file_path, triple, platform, load_dependent_modules, error
        )
        if error.Fail():
            err = "Couldn't create target for path '{}': {}".format(
                file_path, str(error)
            )
            self.fail(self._formatMessage(msg, err))

        self.assertTrue(target.IsValid(), "Got invalid target without error")
        return target

    # =================================================
    # Misc. helper methods for debugging test execution
    # =================================================

    def DebugSBValue(self, val):
        """Debug print a SBValue object, if traceAlways is True."""
        from .lldbutil import value_type_to_str

        if not traceAlways:
            return

        err = sys.stderr
        err.write(val.GetName() + ":\n")
        err.write("\t" + "TypeName         -> " + val.GetTypeName() + "\n")
        err.write("\t" + "ByteSize         -> " + str(val.GetByteSize()) + "\n")
        err.write("\t" + "NumChildren      -> " + str(val.GetNumChildren()) + "\n")
        err.write("\t" + "Value            -> " + str(val.GetValue()) + "\n")
        err.write("\t" + "ValueAsUnsigned  -> " + str(val.GetValueAsUnsigned()) + "\n")
        err.write(
            "\t" + "ValueType        -> " + value_type_to_str(val.GetValueType()) + "\n"
        )
        err.write("\t" + "Summary          -> " + str(val.GetSummary()) + "\n")
        err.write("\t" + "IsPointerType    -> " + str(val.TypeIsPointerType()) + "\n")
        err.write("\t" + "Location         -> " + val.GetLocation() + "\n")

    def DebugSBType(self, type):
        """Debug print a SBType object, if traceAlways is True."""
        if not traceAlways:
            return

        err = sys.stderr
        err.write(type.GetName() + ":\n")
        err.write("\t" + "ByteSize        -> " + str(type.GetByteSize()) + "\n")
        err.write("\t" + "IsAggregateType   -> " + str(type.IsAggregateType()) + "\n")
        err.write("\t" + "IsPointerType   -> " + str(type.IsPointerType()) + "\n")
        err.write("\t" + "IsReferenceType -> " + str(type.IsReferenceType()) + "\n")

    def DebugPExpect(self, child):
        """Debug the spwaned pexpect object."""
        if not traceAlways:
            return

        print(child)

    @classmethod
    def RemoveTempFile(cls, file):
        if os.path.exists(file):
            remove_file(file)


# On Windows, the first attempt to delete a recently-touched file can fail
# because of a race with antimalware scanners.  This function will detect a
# failure and retry.


def remove_file(file, num_retries=1, sleep_duration=0.5):
    for i in range(num_retries + 1):
        try:
            os.remove(file)
            return True
        except:
            time.sleep(sleep_duration)
            continue
    return False
