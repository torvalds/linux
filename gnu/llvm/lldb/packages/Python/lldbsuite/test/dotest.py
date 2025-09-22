"""
A simple testing framework for lldb using python's unit testing framework.

Tests for lldb are written as python scripts which take advantage of the script
bridging provided by LLDB.framework to interact with lldb core.

A specific naming pattern is followed by the .py script to be recognized as
a module which implements a test scenario, namely, Test*.py.

To specify the directories where "Test*.py" python test scripts are located,
you need to pass in a list of directory names.  By default, the current
working directory is searched if nothing is specified on the command line.

Type:

./dotest.py -h

for available options.
"""

# System modules
import atexit
import datetime
import errno
import logging
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile

# Third-party modules
import unittest

# LLDB Modules
import lldbsuite
from . import configuration
from . import dotest_args
from . import lldbtest_config
from . import test_categories
from . import test_result
from ..support import seven


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
    fpath, _ = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None


def usage(parser):
    parser.print_help()
    if configuration.verbose > 0:
        print(
            """
Examples:

This is an example of using the -f option to pinpoint to a specific test class
and test method to be run:

$ ./dotest.py -f ClassTypesTestCase.test_with_dsym_and_run_command
----------------------------------------------------------------------
Collected 1 test

test_with_dsym_and_run_command (TestClassTypes.ClassTypesTestCase)
Test 'frame variable this' when stopped on a class constructor. ... ok

----------------------------------------------------------------------
Ran 1 test in 1.396s

OK

And this is an example of using the -p option to run a single file (the filename
matches the pattern 'ObjC' and it happens to be 'TestObjCMethods.py'):

$ ./dotest.py -v -p ObjC
----------------------------------------------------------------------
Collected 4 tests

test_break_with_dsym (TestObjCMethods.FoundationTestCase)
Test setting objc breakpoints using '_regexp-break' and 'breakpoint set'. ... ok
test_break_with_dwarf (TestObjCMethods.FoundationTestCase)
Test setting objc breakpoints using '_regexp-break' and 'breakpoint set'. ... ok
test_data_type_and_expr_with_dsym (TestObjCMethods.FoundationTestCase)
Lookup objective-c data types and evaluate expressions. ... ok
test_data_type_and_expr_with_dwarf (TestObjCMethods.FoundationTestCase)
Lookup objective-c data types and evaluate expressions. ... ok

----------------------------------------------------------------------
Ran 4 tests in 16.661s

OK

Running of this script also sets up the LLDB_TEST environment variable so that
individual test cases can locate their supporting files correctly.  The script
tries to set up Python's search paths for modules by looking at the build tree
relative to this script.  See also the '-i' option in the following example.

Finally, this is an example of using the lldb.py module distributed/installed by
Xcode4 to run against the tests under the 'forward' directory, and with the '-w'
option to add some delay between two tests.  It uses ARCH=x86_64 to specify that
as the architecture and CC=clang to specify the compiler used for the test run:

$ PYTHONPATH=/Xcode4/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/Python ARCH=x86_64 CC=clang ./dotest.py -v -w -i forward

Session logs for test failures/errors will go into directory '2010-11-11-13_56_16'
----------------------------------------------------------------------
Collected 2 tests

test_with_dsym_and_run_command (TestForwardDeclaration.ForwardDeclarationTestCase)
Display *bar_ptr when stopped on a function with forward declaration of struct bar. ... ok
test_with_dwarf_and_run_command (TestForwardDeclaration.ForwardDeclarationTestCase)
Display *bar_ptr when stopped on a function with forward declaration of struct bar. ... ok

----------------------------------------------------------------------
Ran 2 tests in 5.659s

OK

The 'Session ...' verbiage is recently introduced (see also the '-s' option) to
notify the directory containing the session logs for test failures or errors.
In case there is any test failure/error, a similar message is appended at the
end of the stderr output for your convenience.

ENABLING LOGS FROM TESTS

Option 1:

Writing logs into different files per test case::

$ ./dotest.py --channel "lldb all"

$ ./dotest.py --channel "lldb all" --channel "gdb-remote packets"

These log files are written to:

<session-dir>/<test-id>-host.log (logs from lldb host process)
<session-dir>/<test-id>-server.log (logs from debugserver/lldb-server)
<session-dir>/<test-id>-<test-result>.log (console logs)

By default, logs from successful runs are deleted.  Use the --log-success flag
to create reference logs for debugging.

$ ./dotest.py --log-success

"""
        )
    sys.exit(0)


def parseExclusion(exclusion_file):
    """Parse an exclusion file, of the following format, where
    'skip files', 'skip methods', 'xfail files', and 'xfail methods'
    are the possible list heading values:

    skip files
    <file name>
    <file name>

    xfail methods
    <method name>
    """
    excl_type = None

    with open(exclusion_file) as f:
        for line in f:
            line = line.strip()
            if not excl_type:
                excl_type = line
                continue

            if not line:
                excl_type = None
            elif excl_type == "skip":
                if not configuration.skip_tests:
                    configuration.skip_tests = []
                configuration.skip_tests.append(line)
            elif excl_type == "xfail":
                if not configuration.xfail_tests:
                    configuration.xfail_tests = []
                configuration.xfail_tests.append(line)


def parseOptionsAndInitTestdirs():
    """Initialize the list of directories containing our unittest scripts.

    '-h/--help as the first option prints out usage info and exit the program.
    """

    do_help = False

    platform_system = platform.system()
    platform_machine = platform.machine()

    try:
        parser = dotest_args.create_parser()
        args = parser.parse_args()
    except:
        raise

    if args.unset_env_varnames:
        for env_var in args.unset_env_varnames:
            if env_var in os.environ:
                # From Python Doc: When unsetenv() is supported, deletion of items in os.environ
                # is automatically translated into a corresponding call to
                # unsetenv().
                del os.environ[env_var]
                # os.unsetenv(env_var)

    if args.set_env_vars:
        for env_var in args.set_env_vars:
            parts = env_var.split("=", 1)
            if len(parts) == 1:
                os.environ[parts[0]] = ""
            else:
                os.environ[parts[0]] = parts[1]

    if args.set_inferior_env_vars:
        lldbtest_config.inferior_env = " ".join(args.set_inferior_env_vars)

    if args.h:
        do_help = True

    if args.compiler:
        configuration.compiler = os.path.abspath(args.compiler)
        if not is_exe(configuration.compiler):
            configuration.compiler = which(args.compiler)
        if not is_exe(configuration.compiler):
            logging.error(
                '"%s" is not a valid compiler executable; aborting...', args.compiler
            )
            sys.exit(-1)
    else:
        # Use a compiler appropriate appropriate for the Apple SDK if one was
        # specified
        if platform_system == "Darwin" and args.apple_sdk:
            configuration.compiler = seven.get_command_output(
                'xcrun -sdk "%s" -find clang 2> /dev/null' % (args.apple_sdk)
            )
        else:
            # 'clang' on ubuntu 14.04 is 3.4 so we try clang-3.5 first
            candidateCompilers = ["clang-3.5", "clang", "gcc"]
            for candidate in candidateCompilers:
                if which(candidate):
                    configuration.compiler = candidate
                    break

    if args.make:
        configuration.make_path = args.make
    elif platform_system == "FreeBSD" or platform_system == "NetBSD":
        configuration.make_path = "gmake"
    else:
        configuration.make_path = "make"

    if args.dsymutil:
        configuration.dsymutil = args.dsymutil
    elif platform_system == "Darwin":
        configuration.dsymutil = seven.get_command_output(
            "xcrun -find -toolchain default dsymutil"
        )
    if args.llvm_tools_dir:
        configuration.filecheck = shutil.which("FileCheck", path=args.llvm_tools_dir)
        configuration.yaml2obj = shutil.which("yaml2obj", path=args.llvm_tools_dir)

    if not configuration.get_filecheck_path():
        logging.warning("No valid FileCheck executable; some tests may fail...")
        logging.warning("(Double-check the --llvm-tools-dir argument to dotest.py)")

    if args.libcxx_include_dir or args.libcxx_library_dir:
        if args.lldb_platform_name:
            logging.warning(
                "Custom libc++ is not supported for remote runs: ignoring --libcxx arguments"
            )
        elif not (args.libcxx_include_dir and args.libcxx_library_dir):
            logging.error(
                "Custom libc++ requires both --libcxx-include-dir and --libcxx-library-dir"
            )
            sys.exit(-1)
    configuration.libcxx_include_dir = args.libcxx_include_dir
    configuration.libcxx_include_target_dir = args.libcxx_include_target_dir
    configuration.libcxx_library_dir = args.libcxx_library_dir

    if args.channels:
        lldbtest_config.channels = args.channels

    if args.log_success:
        lldbtest_config.log_success = args.log_success

    if args.out_of_tree_debugserver:
        lldbtest_config.out_of_tree_debugserver = args.out_of_tree_debugserver

    # Set SDKROOT if we are using an Apple SDK
    if args.sysroot is not None:
        configuration.sdkroot = args.sysroot
    elif platform_system == "Darwin" and args.apple_sdk:
        configuration.sdkroot = seven.get_command_output(
            'xcrun --sdk "%s" --show-sdk-path 2> /dev/null' % (args.apple_sdk)
        )
        if not configuration.sdkroot:
            logging.error("No SDK found with the name %s; aborting...", args.apple_sdk)
            sys.exit(-1)

    if args.arch:
        configuration.arch = args.arch
    else:
        configuration.arch = platform_machine

    if args.categories_list:
        configuration.categories_list = set(
            test_categories.validate(args.categories_list, False)
        )
        configuration.use_categories = True
    else:
        configuration.categories_list = []

    if args.skip_categories:
        configuration.skip_categories += test_categories.validate(
            args.skip_categories, False
        )

    if args.xfail_categories:
        configuration.xfail_categories += test_categories.validate(
            args.xfail_categories, False
        )

    if args.E:
        os.environ["CFLAGS_EXTRAS"] = args.E

    if args.dwarf_version:
        configuration.dwarf_version = args.dwarf_version
        # We cannot modify CFLAGS_EXTRAS because they're used in test cases
        # that explicitly require no debug info.
        os.environ["CFLAGS"] = "-gdwarf-{}".format(configuration.dwarf_version)

    if args.settings:
        for setting in args.settings:
            if not len(setting) == 1 or not setting[0].count("="):
                logging.error(
                    '"%s" is not a setting in the form "key=value"', setting[0]
                )
                sys.exit(-1)
            setting_list = setting[0].split("=", 1)
            configuration.settings.append((setting_list[0], setting_list[1]))

    if args.d:
        sys.stdout.write(
            "Suspending the process %d to wait for debugger to attach...\n"
            % os.getpid()
        )
        sys.stdout.flush()
        os.kill(os.getpid(), signal.SIGSTOP)

    if args.f:
        if any([x.startswith("-") for x in args.f]):
            usage(parser)
        configuration.filters.extend(args.f)

    if args.framework:
        configuration.lldb_framework_path = args.framework

    if args.executable:
        # lldb executable is passed explicitly
        lldbtest_config.lldbExec = os.path.abspath(args.executable)
        if not is_exe(lldbtest_config.lldbExec):
            lldbtest_config.lldbExec = which(args.executable)
        if not is_exe(lldbtest_config.lldbExec):
            logging.error(
                "%s is not a valid executable to test; aborting...", args.executable
            )
            sys.exit(-1)

    if args.excluded:
        for excl_file in args.excluded:
            parseExclusion(excl_file)

    if args.p:
        if args.p.startswith("-"):
            usage(parser)
        configuration.regexp = args.p

    if args.t:
        os.environ["LLDB_COMMAND_TRACE"] = "YES"

    if args.v:
        configuration.verbose = 2

    # argparse makes sure we have a number
    if args.sharp:
        configuration.count = args.sharp

    if sys.platform.startswith("win32"):
        os.environ["LLDB_DISABLE_CRASH_DIALOG"] = str(args.disable_crash_dialog)
        os.environ["LLDB_LAUNCH_INFERIORS_WITHOUT_CONSOLE"] = str(True)

    if do_help:
        usage(parser)

    if args.lldb_platform_name:
        configuration.lldb_platform_name = args.lldb_platform_name
    if args.lldb_platform_url:
        configuration.lldb_platform_url = args.lldb_platform_url
    if args.lldb_platform_working_dir:
        configuration.lldb_platform_working_dir = args.lldb_platform_working_dir
    if platform_system == "Darwin" and args.apple_sdk:
        configuration.apple_sdk = args.apple_sdk
    if args.test_build_dir:
        configuration.test_build_dir = args.test_build_dir
    if args.lldb_module_cache_dir:
        configuration.lldb_module_cache_dir = args.lldb_module_cache_dir
    else:
        configuration.lldb_module_cache_dir = os.path.join(
            configuration.test_build_dir, "module-cache-lldb"
        )

    if args.clang_module_cache_dir:
        configuration.clang_module_cache_dir = args.clang_module_cache_dir
    else:
        configuration.clang_module_cache_dir = os.path.join(
            configuration.test_build_dir, "module-cache-clang"
        )

    if args.lldb_libs_dir:
        configuration.lldb_libs_dir = args.lldb_libs_dir
    if args.lldb_obj_root:
        configuration.lldb_obj_root = args.lldb_obj_root

    if args.enabled_plugins:
        configuration.enabled_plugins = args.enabled_plugins

    # Gather all the dirs passed on the command line.
    if len(args.args) > 0:
        configuration.testdirs = [
            os.path.realpath(os.path.abspath(x)) for x in args.args
        ]


def registerFaulthandler():
    try:
        import faulthandler
    except ImportError:
        # faulthandler is not available until python3
        return

    faulthandler.enable()
    # faulthandler.register is not available on Windows.
    if getattr(faulthandler, "register", None):
        faulthandler.register(signal.SIGTERM, chain=True)


def setupSysPath():
    """
    Add LLDB.framework/Resources/Python to the search paths for modules.
    As a side effect, we also discover the 'lldb' executable and export it here.
    """

    # Get the directory containing the current script.
    if "DOTEST_PROFILE" in os.environ and "DOTEST_SCRIPT_DIR" in os.environ:
        scriptPath = os.environ["DOTEST_SCRIPT_DIR"]
    else:
        scriptPath = os.path.dirname(os.path.abspath(__file__))
    if not scriptPath.endswith("test"):
        print("This script expects to reside in lldb's test directory.")
        sys.exit(-1)

    os.environ["LLDB_TEST"] = scriptPath

    # Set up the root build directory.
    if not configuration.test_build_dir:
        raise Exception("test_build_dir is not set")
    configuration.test_build_dir = os.path.abspath(configuration.test_build_dir)

    # Set up the LLDB_SRC environment variable, so that the tests can locate
    # the LLDB source code.
    os.environ["LLDB_SRC"] = lldbsuite.lldb_root

    pluginPath = os.path.join(scriptPath, "plugins")
    toolsLLDBDAP = os.path.join(scriptPath, "tools", "lldb-dap")
    toolsLLDBServerPath = os.path.join(scriptPath, "tools", "lldb-server")
    intelpt = os.path.join(scriptPath, "tools", "intelpt")

    # Insert script dir, plugin dir and lldb-server dir to the sys.path.
    sys.path.insert(0, pluginPath)
    # Adding test/tools/lldb-dap to the path makes it easy to
    # "import lldb_dap_testcase" from the DAP tests
    sys.path.insert(0, toolsLLDBDAP)
    # Adding test/tools/lldb-server to the path makes it easy
    # to "import lldbgdbserverutils" from the lldb-server tests
    sys.path.insert(0, toolsLLDBServerPath)
    # Adding test/tools/intelpt to the path makes it easy
    # to "import intelpt_testcase" from the lldb-server tests
    sys.path.insert(0, intelpt)

    # This is the root of the lldb git/svn checkout
    # When this changes over to a package instead of a standalone script, this
    # will be `lldbsuite.lldb_root`
    lldbRootDirectory = lldbsuite.lldb_root

    # Some of the tests can invoke the 'lldb' command directly.
    # We'll try to locate the appropriate executable right here.

    # The lldb executable can be set from the command line
    # if it's not set, we try to find it now
    # first, we try the environment
    if not lldbtest_config.lldbExec:
        # First, you can define an environment variable LLDB_EXEC specifying the
        # full pathname of the lldb executable.
        if "LLDB_EXEC" in os.environ:
            lldbtest_config.lldbExec = os.environ["LLDB_EXEC"]

    if not lldbtest_config.lldbExec:
        # Last, check the path
        lldbtest_config.lldbExec = which("lldb")

    if lldbtest_config.lldbExec and not is_exe(lldbtest_config.lldbExec):
        print(
            "'{}' is not a path to a valid executable".format(lldbtest_config.lldbExec)
        )
        lldbtest_config.lldbExec = None

    if not lldbtest_config.lldbExec:
        print(
            "The 'lldb' executable cannot be located.  Some of the tests may not be run as a result."
        )
        sys.exit(-1)

    os.system("%s -v" % lldbtest_config.lldbExec)

    lldbDir = os.path.dirname(lldbtest_config.lldbExec)

    lldbDAPExec = os.path.join(lldbDir, "lldb-dap")
    if is_exe(lldbDAPExec):
        os.environ["LLDBDAP_EXEC"] = lldbDAPExec

    lldbPythonDir = None  # The directory that contains 'lldb/__init__.py'

    # If our lldb supports the -P option, use it to find the python path:
    lldb_dash_p_result = subprocess.check_output(
        [lldbtest_config.lldbExec, "-P"], universal_newlines=True
    )
    if lldb_dash_p_result:
        for line in lldb_dash_p_result.splitlines():
            if os.path.isdir(line) and os.path.exists(
                os.path.join(line, "lldb", "__init__.py")
            ):
                lldbPythonDir = line
                break

    if not lldbPythonDir:
        print(
            "Unable to load lldb extension module.  Possible reasons for this include:"
        )
        print("  1) LLDB was built with LLDB_ENABLE_PYTHON=0")
        print(
            "  2) PYTHONPATH and PYTHONHOME are not set correctly.  PYTHONHOME should refer to"
        )
        print(
            "     the version of Python that LLDB built and linked against, and PYTHONPATH"
        )
        print(
            "     should contain the Lib directory for the same python distro, as well as the"
        )
        print("     location of LLDB's site-packages folder.")
        print(
            "  3) A different version of Python than that which was built against is exported in"
        )
        print("     the system's PATH environment variable, causing conflicts.")
        print(
            "  4) The executable '%s' could not be found.  Please check "
            % lldbtest_config.lldbExec
        )
        print("     that it exists and is executable.")

    if lldbPythonDir:
        lldbPythonDir = os.path.normpath(lldbPythonDir)
        # Some of the code that uses this path assumes it hasn't resolved the Versions... link.
        # If the path we've constructed looks like that, then we'll strip out
        # the Versions/A part.
        (before, frameWithVersion, after) = lldbPythonDir.rpartition(
            "LLDB.framework/Versions/A"
        )
        if frameWithVersion != "":
            lldbPythonDir = before + "LLDB.framework" + after

        lldbPythonDir = os.path.abspath(lldbPythonDir)

        if "freebsd" in sys.platform or "linux" in sys.platform:
            os.environ["LLDB_LIB_DIR"] = os.path.join(lldbPythonDir, "..", "..")

        # If tests need to find LLDB_FRAMEWORK, now they can do it
        os.environ["LLDB_FRAMEWORK"] = os.path.dirname(os.path.dirname(lldbPythonDir))

        # This is to locate the lldb.py module.  Insert it right after
        # sys.path[0].
        sys.path[1:1] = [lldbPythonDir]


def visit_file(dir, name):
    # Try to match the regexp pattern, if specified.
    if configuration.regexp:
        if not re.search(configuration.regexp, name):
            # We didn't match the regex, we're done.
            return

    if configuration.skip_tests:
        for file_regexp in configuration.skip_tests:
            if re.search(file_regexp, name):
                return

    # We found a match for our test.  Add it to the suite.

    # Update the sys.path first.
    if not sys.path.count(dir):
        sys.path.insert(0, dir)
    base = os.path.splitext(name)[0]

    # Thoroughly check the filterspec against the base module and admit
    # the (base, filterspec) combination only when it makes sense.

    def check(obj, parts):
        for part in parts:
            try:
                parent, obj = obj, getattr(obj, part)
            except AttributeError:
                # The filterspec has failed.
                return False
        return True

    module = __import__(base)

    def iter_filters():
        for filterspec in configuration.filters:
            parts = filterspec.split(".")
            if check(module, parts):
                yield filterspec
            elif parts[0] == base and len(parts) > 1 and check(module, parts[1:]):
                yield ".".join(parts[1:])
            else:
                for key, value in module.__dict__.items():
                    if check(value, parts):
                        yield key + "." + filterspec

    filtered = False
    for filterspec in iter_filters():
        filtered = True
        print("adding filter spec %s to module %s" % (filterspec, repr(module)))
        tests = unittest.defaultTestLoader.loadTestsFromName(filterspec, module)
        configuration.suite.addTests(tests)

    # Forgo this module if the (base, filterspec) combo is invalid
    if configuration.filters and not filtered:
        return

    if not filtered:
        # Add the entire file's worth of tests since we're not filtered.
        # Also the fail-over case when the filterspec branch
        # (base, filterspec) combo doesn't make sense.
        configuration.suite.addTests(unittest.defaultTestLoader.loadTestsFromName(base))


def visit(prefix, dir, names):
    """Visitor function for os.path.walk(path, visit, arg)."""

    dir_components = set(dir.split(os.sep))
    excluded_components = set([".svn", ".git"])
    if dir_components.intersection(excluded_components):
        return

    # Gather all the Python test file names that follow the Test*.py pattern.
    python_test_files = [
        name for name in names if name.endswith(".py") and name.startswith(prefix)
    ]

    # Visit all the python test files.
    for name in python_test_files:
        # Ensure we error out if we have multiple tests with the same
        # base name.
        # Future improvement: find all the places where we work with base
        # names and convert to full paths.  We have directory structure
        # to disambiguate these, so we shouldn't need this constraint.
        if name in configuration.all_tests:
            raise Exception("Found multiple tests with the name %s" % name)
        configuration.all_tests.add(name)

        # Run the relevant tests in the python file.
        visit_file(dir, name)


# ======================================== #
#                                          #
# Execution of the test driver starts here #
#                                          #
# ======================================== #


def checkDsymForUUIDIsNotOn():
    cmd = ["defaults", "read", "com.apple.DebugSymbols"]
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    cmd_output = process.stdout.read()
    output_str = cmd_output.decode("utf-8")
    if "DBGFileMappedPaths = " in output_str:
        print("%s =>" % " ".join(cmd))
        print(output_str)
        print(
            "Disable automatic lookup and caching of dSYMs before running the test suite!"
        )
        print("Exiting...")
        sys.exit(0)


def exitTestSuite(exitCode=None):
    # lldb.py does SBDebugger.Initialize().
    # Call SBDebugger.Terminate() on exit.
    import lldb

    lldb.SBDebugger.Terminate()
    if exitCode:
        sys.exit(exitCode)


def getVersionForSDK(sdk):
    sdk = str.lower(sdk)
    full_path = seven.get_command_output("xcrun -sdk %s --show-sdk-path" % sdk)
    basename = os.path.basename(full_path)
    basename = os.path.splitext(basename)[0]
    basename = str.lower(basename)
    ver = basename.replace(sdk, "")
    return ver


def checkCompiler():
    # Add some intervention here to sanity check that the compiler requested is sane.
    # If found not to be an executable program, we abort.
    c = configuration.compiler
    if which(c):
        return

    if not sys.platform.startswith("darwin"):
        raise Exception(c + " is not a valid compiler")

    pipe = subprocess.Popen(
        ["xcrun", "-find", c], stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    cmd_output = pipe.stdout.read()
    if not cmd_output or "not found" in cmd_output:
        raise Exception(c + " is not a valid compiler")

    configuration.compiler = cmd_output.split("\n")[0]
    print("'xcrun -find %s' returning %s" % (c, configuration.compiler))


def canRunLibcxxTests():
    from lldbsuite.test import lldbplatformutil

    platform = lldbplatformutil.getPlatform()

    if lldbplatformutil.target_is_android() or lldbplatformutil.platformIsDarwin():
        return True, "libc++ always present"

    if platform == "linux":
        with tempfile.NamedTemporaryFile() as f:
            cmd = [configuration.compiler, "-xc++", "-stdlib=libc++", "-o", f.name, "-"]
            p = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
            )
            _, stderr = p.communicate("#include <cassert>\nint main() {}")
            if not p.returncode:
                return True, "Compiling with -stdlib=libc++ works"
            return (
                False,
                "Compiling with -stdlib=libc++ fails with the error: %s" % stderr,
            )

    return False, "Don't know how to build with libc++ on %s" % platform


def checkLibcxxSupport():
    result, reason = canRunLibcxxTests()
    if result:
        return  # libc++ supported
    if "libc++" in configuration.categories_list:
        return  # libc++ category explicitly requested, let it run.
    if configuration.verbose:
        print("libc++ tests will not be run because: " + reason)
    configuration.skip_categories.append("libc++")


def canRunLibstdcxxTests():
    from lldbsuite.test import lldbplatformutil

    platform = lldbplatformutil.getPlatform()
    if lldbplatformutil.target_is_android():
        platform = "android"
    if platform == "linux":
        return True, "libstdcxx always present"
    return False, "Don't know how to build with libstdcxx on %s" % platform


def checkLibstdcxxSupport():
    result, reason = canRunLibstdcxxTests()
    if result:
        return  # libstdcxx supported
    if "libstdcxx" in configuration.categories_list:
        return  # libstdcxx category explicitly requested, let it run.
    if configuration.verbose:
        print("libstdcxx tests will not be run because: " + reason)
    configuration.skip_categories.append("libstdcxx")


def canRunWatchpointTests():
    from lldbsuite.test import lldbplatformutil

    platform = lldbplatformutil.getPlatform()
    if platform == "netbsd":
        if os.geteuid() == 0:
            return True, "root can always write dbregs"
        try:
            output = (
                subprocess.check_output(
                    ["/sbin/sysctl", "-n", "security.models.extensions.user_set_dbregs"]
                )
                .decode()
                .strip()
            )
            if output == "1":
                return True, "security.models.extensions.user_set_dbregs enabled"
        except subprocess.CalledProcessError:
            pass
        return False, "security.models.extensions.user_set_dbregs disabled"
    elif platform == "freebsd" and configuration.arch == "aarch64":
        import lldb

        if lldb.SBPlatform.GetHostPlatform().GetOSMajorVersion() < 13:
            return False, "Watchpoint support on arm64 requires FreeBSD 13.0"
    return True, "watchpoint support available"


def checkWatchpointSupport():
    result, reason = canRunWatchpointTests()
    if result:
        return  # watchpoints supported
    if "watchpoint" in configuration.categories_list:
        return  # watchpoint category explicitly requested, let it run.
    if configuration.verbose:
        print("watchpoint tests will not be run because: " + reason)
    configuration.skip_categories.append("watchpoint")


def checkObjcSupport():
    from lldbsuite.test import lldbplatformutil

    if not lldbplatformutil.platformIsDarwin():
        if configuration.verbose:
            print("objc tests will be skipped because of unsupported platform")
        configuration.skip_categories.append("objc")


def checkDebugInfoSupport():
    from lldbsuite.test import lldbplatformutil

    platform = lldbplatformutil.getPlatform()
    compiler = configuration.compiler
    for cat in test_categories.debug_info_categories:
        if cat in configuration.categories_list:
            continue  # Category explicitly requested, let it run.
        if test_categories.is_supported_on_platform(cat, platform, compiler):
            continue
        configuration.skip_categories.append(cat)


def checkDebugServerSupport():
    from lldbsuite.test import lldbplatformutil
    import lldb

    skip_msg = "Skipping %s tests, as they are not compatible with remote testing on this platform"
    if lldbplatformutil.platformIsDarwin():
        configuration.skip_categories.append("llgs")
        if lldb.remote_platform:
            # <rdar://problem/34539270>
            configuration.skip_categories.append("debugserver")
            if configuration.verbose:
                print(skip_msg % "debugserver")
    else:
        configuration.skip_categories.append("debugserver")
        if lldb.remote_platform and lldbplatformutil.getPlatform() == "windows":
            configuration.skip_categories.append("llgs")
            if configuration.verbose:
                print(skip_msg % "lldb-server")


def checkForkVForkSupport():
    from lldbsuite.test import lldbplatformutil

    platform = lldbplatformutil.getPlatform()
    if platform not in ["freebsd", "linux", "netbsd"]:
        configuration.skip_categories.append("fork")


def checkPexpectSupport():
    from lldbsuite.test import lldbplatformutil

    platform = lldbplatformutil.getPlatform()

    # llvm.org/pr22274: need a pexpect replacement for windows
    if platform in ["windows"]:
        if configuration.verbose:
            print("pexpect tests will be skipped because of unsupported platform")
        configuration.skip_categories.append("pexpect")


def checkDAPSupport():
    import lldb

    if "LLDBDAP_EXEC" not in os.environ:
        msg = (
            "The 'lldb-dap' executable cannot be located and its tests will not be run."
        )
    elif lldb.remote_platform:
        msg = "lldb-dap tests are not compatible with remote platforms and will not be run."
    else:
        msg = None

    if msg:
        if configuration.verbose:
            print(msg)
        configuration.skip_categories.append("lldb-dap")


def run_suite():
    # On MacOS X, check to make sure that domain for com.apple.DebugSymbols defaults
    # does not exist before proceeding to running the test suite.
    if sys.platform.startswith("darwin"):
        checkDsymForUUIDIsNotOn()

    # Start the actions by first parsing the options while setting up the test
    # directories, followed by setting up the search paths for lldb utilities;
    # then, we walk the directory trees and collect the tests into our test suite.
    #
    parseOptionsAndInitTestdirs()

    # Print a stack trace if the test hangs or is passed SIGTERM.
    registerFaulthandler()

    setupSysPath()

    import lldb

    lldb.SBDebugger.Initialize()
    lldb.SBDebugger.PrintStackTraceOnError()

    # Use host platform by default.
    lldb.remote_platform = None
    lldb.selected_platform = lldb.SBPlatform.GetHostPlatform()

    # Now we can also import lldbutil
    from lldbsuite.test import lldbutil

    if configuration.lldb_platform_name:
        print("Setting up remote platform '%s'" % (configuration.lldb_platform_name))
        lldb.remote_platform = lldb.SBPlatform(configuration.lldb_platform_name)
        lldb.selected_platform = lldb.remote_platform
        if not lldb.remote_platform.IsValid():
            print(
                "error: unable to create the LLDB platform named '%s'."
                % (configuration.lldb_platform_name)
            )
            exitTestSuite(1)
        if configuration.lldb_platform_url:
            # We must connect to a remote platform if a LLDB platform URL was
            # specified
            print(
                "Connecting to remote platform '%s' at '%s'..."
                % (configuration.lldb_platform_name, configuration.lldb_platform_url)
            )
            platform_connect_options = lldb.SBPlatformConnectOptions(
                configuration.lldb_platform_url
            )
            err = lldb.remote_platform.ConnectRemote(platform_connect_options)
            if err.Success():
                print("Connected.")
            else:
                print(
                    "error: failed to connect to remote platform using URL '%s': %s"
                    % (configuration.lldb_platform_url, err)
                )
                exitTestSuite(1)
        else:
            configuration.lldb_platform_url = None

    if configuration.lldb_platform_working_dir:
        print(
            "Setting remote platform working directory to '%s'..."
            % (configuration.lldb_platform_working_dir)
        )
        error = lldb.remote_platform.MakeDirectory(
            configuration.lldb_platform_working_dir, 448
        )  # 448 = 0o700
        if error.Fail():
            raise Exception(
                "making remote directory '%s': %s"
                % (configuration.lldb_platform_working_dir, error)
            )

        if not lldb.remote_platform.SetWorkingDirectory(
            configuration.lldb_platform_working_dir
        ):
            raise Exception(
                "failed to set working directory '%s'"
                % configuration.lldb_platform_working_dir
            )
        lldb.selected_platform = lldb.remote_platform
    else:
        lldb.remote_platform = None
        configuration.lldb_platform_working_dir = None
        configuration.lldb_platform_url = None

    # Set up the working directory.
    # Note that it's not dotest's job to clean this directory.
    lldbutil.mkdir_p(configuration.test_build_dir)

    checkLibcxxSupport()
    checkLibstdcxxSupport()
    checkWatchpointSupport()
    checkDebugInfoSupport()
    checkDebugServerSupport()
    checkObjcSupport()
    checkForkVForkSupport()
    checkPexpectSupport()
    checkDAPSupport()

    skipped_categories_list = ", ".join(configuration.skip_categories)
    print(
        "Skipping the following test categories: {}".format(
            configuration.skip_categories
        )
    )

    for testdir in configuration.testdirs:
        for dirpath, dirnames, filenames in os.walk(testdir):
            visit("Test", dirpath, filenames)

    #
    # Now that we have loaded all the test cases, run the whole test suite.
    #

    # Install the control-c handler.
    unittest.signals.installHandler()

    #
    # Invoke the default TextTestRunner to run the test suite
    #
    checkCompiler()

    if configuration.verbose:
        print("compiler=%s" % configuration.compiler)

    # Iterating over all possible architecture and compiler combinations.
    configString = "arch=%s compiler=%s" % (configuration.arch, configuration.compiler)

    # Output the configuration.
    if configuration.verbose:
        sys.stderr.write("\nConfiguration: " + configString + "\n")

    # First, write out the number of collected test cases.
    if configuration.verbose:
        sys.stderr.write(configuration.separator + "\n")
        sys.stderr.write(
            "Collected %d test%s\n\n"
            % (
                configuration.suite.countTestCases(),
                configuration.suite.countTestCases() != 1 and "s" or "",
            )
        )

    if configuration.suite.countTestCases() == 0:
        logging.error("did not discover any matching tests")
        exitTestSuite(1)

    # Invoke the test runner.
    if configuration.count == 1:
        result = unittest.TextTestRunner(
            stream=sys.stderr,
            verbosity=configuration.verbose,
            resultclass=test_result.LLDBTestResult,
        ).run(configuration.suite)
    else:
        # We are invoking the same test suite more than once.  In this case,
        # mark __ignore_singleton__ flag as True so the signleton pattern is
        # not enforced.
        test_result.LLDBTestResult.__ignore_singleton__ = True
        for i in range(configuration.count):
            result = unittest.TextTestRunner(
                stream=sys.stderr,
                verbosity=configuration.verbose,
                resultclass=test_result.LLDBTestResult,
            ).run(configuration.suite)

    configuration.failed = not result.wasSuccessful()

    if configuration.sdir_has_content and configuration.verbose:
        sys.stderr.write(
            "Session logs for test failures/errors/unexpected successes"
            " can be found in the test build directory\n"
        )

    if configuration.use_categories and len(configuration.failures_per_category) > 0:
        sys.stderr.write("Failures per category:\n")
        for category in configuration.failures_per_category:
            sys.stderr.write(
                "%s - %d\n" % (category, configuration.failures_per_category[category])
            )

    # Exiting.
    exitTestSuite(configuration.failed)


if __name__ == "__main__":
    print(
        __file__
        + " is for use as a module only.  It should not be run as a standalone script."
    )
    sys.exit(-1)
