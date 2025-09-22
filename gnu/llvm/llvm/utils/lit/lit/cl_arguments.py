import argparse
import enum
import os
import shlex
import sys

import lit.reports
import lit.util


@enum.unique
class TestOrder(enum.Enum):
    LEXICAL = "lexical"
    RANDOM = "random"
    SMART = "smart"


def parse_args():
    parser = argparse.ArgumentParser(prog="lit", fromfile_prefix_chars="@")
    parser.add_argument(
        "test_paths",
        nargs="+",
        metavar="TEST_PATH",
        help="File or path to include in the test suite",
    )

    parser.add_argument(
        "--version", action="version", version="%(prog)s " + lit.__version__
    )

    parser.add_argument(
        "-j",
        "--threads",
        "--workers",
        dest="workers",
        metavar="N",
        help="Number of workers used for testing",
        type=_positive_int,
        default=os.getenv("LIT_MAX_WORKERS", lit.util.usable_core_count()),
    )
    parser.add_argument(
        "--config-prefix",
        dest="configPrefix",
        metavar="NAME",
        help="Prefix for 'lit' config files",
    )
    parser.add_argument(
        "-D",
        "--param",
        dest="user_params",
        metavar="NAME=VAL",
        help="Add 'NAME' = 'VAL' to the user defined parameters",
        action="append",
        default=[],
    )

    format_group = parser.add_argument_group("Output Format")
    # FIXME: I find these names very confusing, although I like the
    # functionality.
    format_group.add_argument(
        "-q", "--quiet", help="Suppress no error output", action="store_true"
    )
    format_group.add_argument(
        "-s",
        "--succinct",
        help="Reduce amount of output."
        " Additionally, show a progress bar,"
        " unless --no-progress-bar is specified.",
        action="store_true",
    )
    format_group.add_argument(
        "-v",
        "--verbose",
        dest="showOutput",
        help="For failed tests, show all output. For example, each command is"
        " printed before it is executed, so the last printed command is the one"
        " that failed.",
        action="store_true",
    )
    format_group.add_argument(
        "-vv",
        "--echo-all-commands",
        dest="showOutput",
        help="Deprecated alias for -v.",
        action="store_true",
    )
    format_group.add_argument(
        "-a",
        "--show-all",
        dest="showAllOutput",
        help="Enable -v, but for all tests not just failed tests.",
        action="store_true",
    )
    format_group.add_argument(
        "-o",
        "--output",
        type=lit.reports.JsonReport,
        help="Write test results to the provided path",
        metavar="PATH",
    )
    format_group.add_argument(
        "--no-progress-bar",
        dest="useProgressBar",
        help="Do not use curses based progress bar",
        action="store_false",
    )

    # Note: this does not generate flags for user-defined result codes.
    success_codes = [c for c in lit.Test.ResultCode.all_codes() if not c.isFailure]
    for code in success_codes:
        format_group.add_argument(
            "--show-{}".format(code.name.lower()),
            dest="shown_codes",
            help="Show {} tests ({})".format(code.label.lower(), code.name),
            action="append_const",
            const=code,
            default=[],
        )

    execution_group = parser.add_argument_group("Test Execution")
    execution_group.add_argument(
        "--gtest-sharding",
        help="Enable sharding for GoogleTest format",
        action="store_true",
        default=True,
    )
    execution_group.add_argument(
        "--no-gtest-sharding",
        dest="gtest_sharding",
        help="Disable sharding for GoogleTest format",
        action="store_false",
    )
    execution_group.add_argument(
        "--path",
        help="Additional paths to add to testing environment",
        action="append",
        default=[],
        type=os.path.abspath,
    )
    execution_group.add_argument(
        "--vg", dest="useValgrind", help="Run tests under valgrind", action="store_true"
    )
    execution_group.add_argument(
        "--vg-leak",
        dest="valgrindLeakCheck",
        help="Check for memory leaks under valgrind",
        action="store_true",
    )
    execution_group.add_argument(
        "--vg-arg",
        dest="valgrindArgs",
        metavar="ARG",
        help="Specify an extra argument for valgrind",
        action="append",
        default=[],
    )
    execution_group.add_argument(
        "--no-execute",
        dest="noExecute",
        help="Don't execute any tests (assume PASS)",
        action="store_true",
    )
    execution_group.add_argument(
        "--xunit-xml-output",
        type=lit.reports.XunitReport,
        help="Write XUnit-compatible XML test reports to the specified file",
    )
    execution_group.add_argument(
        "--resultdb-output",
        type=lit.reports.ResultDBReport,
        help="Write LuCI ResuldDB compatible JSON to the specified file",
    )
    execution_group.add_argument(
        "--time-trace-output",
        type=lit.reports.TimeTraceReport,
        help="Write Chrome tracing compatible JSON to the specified file",
    )
    execution_group.add_argument(
        "--timeout",
        dest="maxIndividualTestTime",
        help="Maximum time to spend running a single test (in seconds). "
        "0 means no time limit. [Default: 0]",
        type=_non_negative_int,
    )
    execution_group.add_argument(
        "--max-failures",
        help="Stop execution after the given number of failures.",
        type=_positive_int,
    )
    execution_group.add_argument(
        "--allow-empty-runs",
        help="Do not fail the run if all tests are filtered out",
        action="store_true",
    )
    execution_group.add_argument(
        "--per-test-coverage",
        dest="per_test_coverage",
        action="store_true",
        help="Enable individual test case coverage",
    )
    execution_group.add_argument(
        "--ignore-fail",
        dest="ignoreFail",
        action="store_true",
        help="Exit with status zero even if some tests fail",
    )
    execution_test_time_group = execution_group.add_mutually_exclusive_group()
    execution_test_time_group.add_argument(
        "--skip-test-time-recording",
        help="Do not track elapsed wall time for each test",
        action="store_true",
    )
    execution_test_time_group.add_argument(
        "--time-tests",
        help="Track elapsed wall time for each test printed in a histogram",
        action="store_true",
    )

    selection_group = parser.add_argument_group("Test Selection")
    selection_group.add_argument(
        "--max-tests",
        metavar="N",
        help="Maximum number of tests to run",
        type=_positive_int,
    )
    selection_group.add_argument(
        "--max-time",
        dest="timeout",
        metavar="N",
        help="Maximum time to spend testing (in seconds)",
        type=_positive_int,
    )
    selection_group.add_argument(
        "--order",
        choices=[x.value for x in TestOrder],
        default=TestOrder.SMART,
        help="Test order to use (default: smart)",
    )
    selection_group.add_argument(
        "--shuffle",
        dest="order",
        help="Run tests in random order (DEPRECATED: use --order=random)",
        action="store_const",
        const=TestOrder.RANDOM,
    )
    selection_group.add_argument(
        "-i",
        "--incremental",
        help="Run failed tests first (DEPRECATED: use --order=smart)",
        action="store_true",
    )
    selection_group.add_argument(
        "--filter",
        metavar="REGEX",
        type=_case_insensitive_regex,
        help="Only run tests with paths matching the given regular expression",
        default=os.environ.get("LIT_FILTER", ".*"),
    )
    selection_group.add_argument(
        "--filter-out",
        metavar="REGEX",
        type=_case_insensitive_regex,
        help="Filter out tests with paths matching the given regular expression",
        default=os.environ.get("LIT_FILTER_OUT", "^$"),
    )
    selection_group.add_argument(
        "--xfail",
        metavar="LIST",
        type=_semicolon_list,
        help="XFAIL tests with paths in the semicolon separated list",
        default=os.environ.get("LIT_XFAIL", ""),
    )
    selection_group.add_argument(
        "--xfail-not",
        metavar="LIST",
        type=_semicolon_list,
        help="do not XFAIL tests with paths in the semicolon separated list",
        default=os.environ.get("LIT_XFAIL_NOT", ""),
    )
    selection_group.add_argument(
        "--num-shards",
        dest="numShards",
        metavar="M",
        help="Split testsuite into M pieces and only run one",
        type=_positive_int,
        default=os.environ.get("LIT_NUM_SHARDS"),
    )
    selection_group.add_argument(
        "--run-shard",
        dest="runShard",
        metavar="N",
        help="Run shard #N of the testsuite",
        type=_positive_int,
        default=os.environ.get("LIT_RUN_SHARD"),
    )

    debug_group = parser.add_argument_group("Debug and Experimental Options")
    debug_group.add_argument(
        "--debug", help="Enable debugging (for 'lit' development)", action="store_true"
    )
    debug_group.add_argument(
        "--show-suites",
        help="Show discovered test suites and exit",
        action="store_true",
    )
    debug_group.add_argument(
        "--show-tests", help="Show all discovered tests and exit", action="store_true"
    )
    debug_group.add_argument(
        "--show-used-features",
        help="Show all features used in the test suite (in XFAIL, UNSUPPORTED and REQUIRES) and exit",
        action="store_true",
    )

    # LIT is special: environment variables override command line arguments.
    env_args = shlex.split(os.environ.get("LIT_OPTS", ""))
    args = sys.argv[1:] + env_args
    opts = parser.parse_args(args)

    # Validate command line options
    if opts.incremental:
        print(
            "WARNING: --incremental is deprecated. Failing tests now always run first."
        )

    if opts.numShards or opts.runShard:
        if not opts.numShards or not opts.runShard:
            parser.error("--num-shards and --run-shard must be used together")
        if opts.runShard > opts.numShards:
            parser.error("--run-shard must be between 1 and --num-shards (inclusive)")
        opts.shard = (opts.runShard, opts.numShards)
    else:
        opts.shard = None

    opts.reports = filter(
        None,
        [
            opts.output,
            opts.xunit_xml_output,
            opts.resultdb_output,
            opts.time_trace_output,
        ],
    )

    return opts


def _positive_int(arg):
    return _int(arg, "positive", lambda i: i > 0)


def _non_negative_int(arg):
    return _int(arg, "non-negative", lambda i: i >= 0)


def _int(arg, kind, pred):
    desc = "requires {} integer, but found '{}'"
    try:
        i = int(arg)
    except ValueError:
        raise _error(desc, kind, arg)
    if not pred(i):
        raise _error(desc, kind, arg)
    return i


def _case_insensitive_regex(arg):
    import re

    try:
        return re.compile(arg, re.IGNORECASE)
    except re.error as reason:
        raise _error("invalid regular expression: '{}', {}", arg, reason)


def _semicolon_list(arg):
    return arg.split(";")


def _error(desc, *args):
    msg = desc.format(*args)
    return argparse.ArgumentTypeError(msg)
