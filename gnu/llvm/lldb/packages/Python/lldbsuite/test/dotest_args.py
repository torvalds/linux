# System modules
import argparse
import sys
import os
import textwrap

# LLDB modules
from . import configuration


def create_parser():
    parser = argparse.ArgumentParser(
        description="description", prefix_chars="+-", add_help=False
    )
    group = None

    # Helper function for boolean options (group will point to the current
    # group when executing X)
    X = lambda optstr, helpstr, **kwargs: group.add_argument(
        optstr, help=helpstr, action="store_true", **kwargs
    )

    group = parser.add_argument_group("Help")
    group.add_argument(
        "-h",
        "--help",
        dest="h",
        action="store_true",
        help="Print this help message and exit.  Add '-v' for more detailed help.",
    )

    # C and Python toolchain options
    group = parser.add_argument_group("Toolchain options")
    group.add_argument(
        "-A",
        "--arch",
        metavar="arch",
        dest="arch",
        help=textwrap.dedent(
            """Specify the architecture(s) to test. This option can be specified more than once"""
        ),
    )
    group.add_argument(
        "-C",
        "--compiler",
        metavar="compiler",
        dest="compiler",
        help=textwrap.dedent(
            """Specify the compiler(s) used to build the inferior executables. The compiler path can be an executable basename or a full path to a compiler executable. This option can be specified multiple times."""
        ),
    )
    group.add_argument(
        "--sysroot",
        metavar="sysroot",
        dest="sysroot",
        default="",
        help=textwrap.dedent(
            """Specify the path to sysroot. This overrides apple_sdk sysroot."""
        ),
    )
    if sys.platform == "darwin":
        group.add_argument(
            "--apple-sdk",
            metavar="apple_sdk",
            dest="apple_sdk",
            default="",
            help=textwrap.dedent(
                """Specify the name of the Apple SDK (macosx, macosx.internal, iphoneos, iphoneos.internal, or path to SDK) and use the appropriate tools from that SDK's toolchain."""
            ),
        )
    group.add_argument(
        "--libcxx-include-dir",
        help=textwrap.dedent(
            "Specify the path to a custom libc++ include directory. Must be used in conjunction with --libcxx-library-dir."
        ),
    )
    group.add_argument(
        "--libcxx-include-target-dir",
        help=textwrap.dedent(
            "Specify the path to a custom libc++ include target directory to use in addition to --libcxx-include-dir. Optional."
        ),
    )
    group.add_argument(
        "--libcxx-library-dir",
        help=textwrap.dedent(
            "Specify the path to a custom libc++ library directory. Must be used in conjunction with --libcxx-include-dir."
        ),
    )
    # FIXME? This won't work for different extra flags according to each arch.
    group.add_argument(
        "-E",
        metavar="extra-flags",
        help=textwrap.dedent(
            """Specify the extra flags to be passed to the toolchain when building the inferior programs to be debugged
                                                           suggestions: do not lump the "-A arch1 -A arch2" together such that the -E option applies to only one of the architectures"""
        ),
    )

    group.add_argument(
        "--make",
        metavar="make",
        dest="make",
        help=textwrap.dedent("Specify which make to use."),
    )
    group.add_argument(
        "--dsymutil",
        metavar="dsymutil",
        dest="dsymutil",
        help=textwrap.dedent("Specify which dsymutil to use."),
    )
    group.add_argument(
        "--llvm-tools-dir",
        metavar="dir",
        dest="llvm_tools_dir",
        help=textwrap.dedent(
            "The location of llvm tools used for testing (yaml2obj, FileCheck, etc.)."
        ),
    )

    # Test filtering options
    group = parser.add_argument_group("Test filtering options")
    group.add_argument(
        "-f",
        metavar="filterspec",
        action="append",
        help=(
            'Specify a filter, which looks like "TestModule.TestClass.test_name".  '
            + "You may also use shortened filters, such as "
            + '"TestModule.TestClass", "TestClass.test_name", or just "test_name".'
        ),
    )
    group.add_argument(
        "-p",
        metavar="pattern",
        help="Specify a regexp filename pattern for inclusion in the test suite",
    )
    group.add_argument(
        "--excluded",
        metavar="exclusion-file",
        action="append",
        help=textwrap.dedent(
            """Specify a file for tests to exclude. File should contain lists of regular expressions for test files or methods,
                                with each list under a matching header (xfail files, xfail methods, skip files, skip methods)"""
        ),
    )
    group.add_argument(
        "-G",
        "--category",
        metavar="category",
        action="append",
        dest="categories_list",
        help=textwrap.dedent(
            """Specify categories of test cases of interest. Can be specified more than once."""
        ),
    )
    group.add_argument(
        "--skip-category",
        metavar="category",
        action="append",
        dest="skip_categories",
        help=textwrap.dedent(
            """Specify categories of test cases to skip. Takes precedence over -G. Can be specified more than once."""
        ),
    )
    group.add_argument(
        "--xfail-category",
        metavar="category",
        action="append",
        dest="xfail_categories",
        help=textwrap.dedent(
            """Specify categories of test cases that are expected to fail. Can be specified more than once."""
        ),
    )

    # Configuration options
    group = parser.add_argument_group("Configuration options")
    group.add_argument(
        "--framework", metavar="framework-path", help="The path to LLDB.framework"
    )
    group.add_argument(
        "--executable",
        metavar="executable-path",
        help="The path to the lldb executable",
    )
    group.add_argument(
        "--out-of-tree-debugserver",
        dest="out_of_tree_debugserver",
        action="store_true",
        help="A flag to indicate an out-of-tree debug server is being used",
    )
    group.add_argument(
        "--dwarf-version",
        metavar="dwarf_version",
        dest="dwarf_version",
        type=int,
        help="Override the DWARF version.",
    )
    group.add_argument(
        "--setting",
        metavar="SETTING=VALUE",
        dest="settings",
        type=str,
        nargs=1,
        action="append",
        help='Run "setting set SETTING VALUE" before executing any test.',
    )
    group.add_argument(
        "-y",
        type=int,
        metavar="count",
        help="Specify the iteration count used to collect our benchmarks. An example is the number of times to do 'thread step-over' to measure stepping speed.",
    )
    group.add_argument(
        "-#",
        type=int,
        metavar="sharp",
        dest="sharp",
        help="Repeat the test suite for a specified number of times",
    )
    group.add_argument(
        "--channel",
        metavar="channel",
        dest="channels",
        action="append",
        help=textwrap.dedent(
            "Specify the log channels (and optional categories) e.g. 'lldb all' or 'gdb-remote packets' if no categories are specified, 'default' is used"
        ),
    )
    group.add_argument(
        "--log-success",
        dest="log_success",
        action="store_true",
        help="Leave logs/traces even for successful test runs (useful for creating reference log files during debugging.)",
    )
    group.add_argument(
        "--build-dir",
        dest="test_build_dir",
        metavar="Test build directory",
        default="lldb-test-build.noindex",
        help="The root build directory for the tests. It will be removed before running.",
    )
    group.add_argument(
        "--lldb-module-cache-dir",
        dest="lldb_module_cache_dir",
        metavar="The clang module cache directory used by LLDB",
        help="The clang module cache directory used by LLDB. Defaults to <test build directory>/module-cache-lldb.",
    )
    group.add_argument(
        "--clang-module-cache-dir",
        dest="clang_module_cache_dir",
        metavar="The clang module cache directory used by Clang",
        help="The clang module cache directory used in the Make files by Clang while building tests. Defaults to <test build directory>/module-cache-clang.",
    )
    group.add_argument(
        "--lldb-obj-root",
        dest="lldb_obj_root",
        metavar="path",
        help="The path to the LLDB object files.",
    )
    group.add_argument(
        "--lldb-libs-dir",
        dest="lldb_libs_dir",
        metavar="path",
        help="The path to LLDB library directory (containing liblldb).",
    )
    group.add_argument(
        "--enable-plugin",
        dest="enabled_plugins",
        action="append",
        type=str,
        metavar="A plugin whose tests will be enabled",
        help="A plugin whose tests will be enabled. The only currently supported plugin is intel-pt.",
    )

    # Configuration options
    group = parser.add_argument_group("Remote platform options")
    group.add_argument(
        "--platform-name",
        dest="lldb_platform_name",
        metavar="platform-name",
        help="The name of a remote platform to use",
    )
    group.add_argument(
        "--platform-url",
        dest="lldb_platform_url",
        metavar="platform-url",
        help="A LLDB platform URL to use when connecting to a remote platform to run the test suite",
    )
    group.add_argument(
        "--platform-working-dir",
        dest="lldb_platform_working_dir",
        metavar="platform-working-dir",
        help="The directory to use on the remote platform.",
    )

    # Test-suite behaviour
    group = parser.add_argument_group("Runtime behaviour options")
    X(
        "-d",
        "Suspend the process after launch to wait indefinitely for a debugger to attach",
    )
    X("-t", "Turn on tracing of lldb command and other detailed test executions")
    group.add_argument(
        "-u",
        dest="unset_env_varnames",
        metavar="variable",
        action="append",
        help="Specify an environment variable to unset before running the test cases. e.g., -u DYLD_INSERT_LIBRARIES -u MallocScribble",
    )
    group.add_argument(
        "--env",
        dest="set_env_vars",
        metavar="variable",
        action="append",
        help="Specify an environment variable to set to the given value before running the test cases e.g.: --env CXXFLAGS=-O3 --env DYLD_INSERT_LIBRARIES",
    )
    group.add_argument(
        "--inferior-env",
        dest="set_inferior_env_vars",
        metavar="variable",
        action="append",
        help="Specify an environment variable to set to the given value for the inferior.",
    )
    X(
        "-v",
        "Do verbose mode of unittest framework (print out each test case invocation)",
    )
    group.add_argument(
        "--enable-crash-dialog",
        dest="disable_crash_dialog",
        action="store_false",
        help="(Windows only) When LLDB crashes, display the Windows crash dialog.",
    )
    group.set_defaults(disable_crash_dialog=True)

    # Remove the reference to our helper function
    del X

    group = parser.add_argument_group("Test directories")
    group.add_argument(
        "args",
        metavar="test-dir",
        nargs="*",
        help="Specify a list of directory names to search for test modules named after Test*.py (test discovery). If empty, search from the current working directory instead.",
    )

    return parser
