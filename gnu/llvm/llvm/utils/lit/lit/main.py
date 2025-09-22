"""
lit - LLVM Integrated Tester.

See lit.pod for more information.
"""

import itertools
import os
import platform
import sys
import time

import lit.cl_arguments
import lit.discovery
import lit.display
import lit.LitConfig
import lit.reports
import lit.run
import lit.Test
import lit.util
from lit.formats.googletest import GoogleTest
from lit.TestTimes import record_test_times


def main(builtin_params={}):
    opts = lit.cl_arguments.parse_args()
    params = create_params(builtin_params, opts.user_params)
    is_windows = platform.system() == "Windows"

    lit_config = lit.LitConfig.LitConfig(
        progname=os.path.basename(sys.argv[0]),
        path=opts.path,
        quiet=opts.quiet,
        useValgrind=opts.useValgrind,
        valgrindLeakCheck=opts.valgrindLeakCheck,
        valgrindArgs=opts.valgrindArgs,
        noExecute=opts.noExecute,
        debug=opts.debug,
        isWindows=is_windows,
        order=opts.order,
        params=params,
        config_prefix=opts.configPrefix,
        per_test_coverage=opts.per_test_coverage,
        gtest_sharding=opts.gtest_sharding,
    )

    discovered_tests = lit.discovery.find_tests_for_inputs(
        lit_config, opts.test_paths
    )
    if not discovered_tests:
        sys.stderr.write("error: did not discover any tests for provided path(s)\n")
        sys.exit(2)

    if opts.show_suites or opts.show_tests:
        print_discovered(discovered_tests, opts.show_suites, opts.show_tests)
        sys.exit(0)

    if opts.show_used_features:
        features = set(
            itertools.chain.from_iterable(
                t.getUsedFeatures()
                for t in discovered_tests
                if t.gtest_json_file is None
            )
        )
        print(" ".join(sorted(features)))
        sys.exit(0)

    # Command line overrides configuration for maxIndividualTestTime.
    if opts.maxIndividualTestTime is not None:  # `not None` is important (default: 0)
        if opts.maxIndividualTestTime != lit_config.maxIndividualTestTime:
            lit_config.note(
                (
                    "The test suite configuration requested an individual"
                    " test timeout of {0} seconds but a timeout of {1} seconds was"
                    " requested on the command line. Forcing timeout to be {1}"
                    " seconds."
                ).format(lit_config.maxIndividualTestTime, opts.maxIndividualTestTime)
            )
            lit_config.maxIndividualTestTime = opts.maxIndividualTestTime

    determine_order(discovered_tests, opts.order)

    selected_tests = [
        t
        for t in discovered_tests
        if opts.filter.search(t.getFullName())
        and not opts.filter_out.search(t.getFullName())
    ]

    if not selected_tests:
        sys.stderr.write(
            "error: filter did not match any tests "
            "(of %d discovered).  " % len(discovered_tests)
        )
        if opts.allow_empty_runs:
            sys.stderr.write(
                "Suppressing error because '--allow-empty-runs' " "was specified.\n"
            )
            sys.exit(0)
        else:
            sys.stderr.write("Use '--allow-empty-runs' to suppress this " "error.\n")
            sys.exit(2)

    # When running multiple shards, don't include skipped tests in the xunit
    # output since merging the files will result in duplicates.
    if opts.shard:
        (run, shards) = opts.shard
        selected_tests = filter_by_shard(selected_tests, run, shards, lit_config)
        if not selected_tests:
            sys.stderr.write(
                "warning: shard does not contain any tests.  "
                "Consider decreasing the number of shards.\n"
            )
            sys.exit(0)

    selected_tests = selected_tests[: opts.max_tests]

    mark_xfail(discovered_tests, opts)

    mark_excluded(discovered_tests, selected_tests)

    start = time.time()
    run_tests(selected_tests, lit_config, opts, len(discovered_tests))
    elapsed = time.time() - start

    if not opts.skip_test_time_recording:
        record_test_times(selected_tests, lit_config)

    selected_tests, discovered_tests = GoogleTest.post_process_shard_results(
        selected_tests, discovered_tests
    )

    if opts.time_tests:
        print_histogram(discovered_tests)

    print_results(discovered_tests, elapsed, opts)

    tests_for_report = selected_tests if opts.shard else discovered_tests
    for report in opts.reports:
        report.write_results(tests_for_report, elapsed)

    if lit_config.numErrors:
        sys.stderr.write("\n%d error(s) in tests\n" % lit_config.numErrors)
        sys.exit(2)

    if lit_config.numWarnings:
        sys.stderr.write("\n%d warning(s) in tests\n" % lit_config.numWarnings)

    has_failure = any(t.isFailure() for t in discovered_tests)
    if has_failure:
        if opts.ignoreFail:
            sys.stderr.write(
                "\nExiting with status 0 instead of 1 because "
                "'--ignore-fail' was specified.\n"
            )
        else:
            sys.exit(1)


def create_params(builtin_params, user_params):
    def parse(p):
        return p.split("=", 1) if "=" in p else (p, "")

    params = dict(builtin_params)
    params.update([parse(p) for p in user_params])
    return params


def print_discovered(tests, show_suites, show_tests):
    tests.sort(key=lit.reports.by_suite_and_test_path)

    if show_suites:
        tests_by_suite = itertools.groupby(tests, lambda t: t.suite)
        print("-- Test Suites --")
        for suite, test_iter in tests_by_suite:
            test_count = sum(1 for _ in test_iter)
            print("  %s - %d tests" % (suite.name, test_count))
            print("    Source Root: %s" % suite.source_root)
            print("    Exec Root  : %s" % suite.exec_root)
            features = " ".join(sorted(suite.config.available_features))
            print("    Available Features: %s" % features)
            substitutions = sorted(suite.config.substitutions)
            substitutions = ("%s => %s" % (x, y) for (x, y) in substitutions)
            substitutions = "\n".ljust(30).join(substitutions)
            print("    Available Substitutions: %s" % substitutions)

    if show_tests:
        print("-- Available Tests --")
        for t in tests:
            print("  %s" % t.getFullName())


def determine_order(tests, order):
    from lit.cl_arguments import TestOrder

    enum_order = TestOrder(order)
    if enum_order == TestOrder.RANDOM:
        import random

        random.shuffle(tests)
    elif enum_order == TestOrder.LEXICAL:
        tests.sort(key=lambda t: t.getFullName())
    else:
        assert enum_order == TestOrder.SMART, "Unknown TestOrder value"
        tests.sort(
            key=lambda t: (not t.previous_failure, -t.previous_elapsed, t.getFullName())
        )


def filter_by_shard(tests, run, shards, lit_config):
    test_ixs = range(run - 1, len(tests), shards)
    selected_tests = [tests[i] for i in test_ixs]

    # For clarity, generate a preview of the first few test indices in the shard
    # to accompany the arithmetic expression.
    preview_len = 3
    preview = ", ".join([str(i + 1) for i in test_ixs[:preview_len]])
    if len(test_ixs) > preview_len:
        preview += ", ..."
    msg = (
        f"Selecting shard {run}/{shards} = "
        f"size {len(selected_tests)}/{len(tests)} = "
        f"tests #({shards}*k)+{run} = [{preview}]"
    )
    lit_config.note(msg)
    return selected_tests


def mark_xfail(selected_tests, opts):
    for t in selected_tests:
        test_file = os.sep.join(t.path_in_suite)
        test_full_name = t.getFullName()
        if test_file in opts.xfail or test_full_name in opts.xfail:
            t.xfails += "*"
        if test_file in opts.xfail_not or test_full_name in opts.xfail_not:
            t.xfail_not = True


def mark_excluded(discovered_tests, selected_tests):
    excluded_tests = set(discovered_tests) - set(selected_tests)
    result = lit.Test.Result(lit.Test.EXCLUDED)
    for t in excluded_tests:
        t.setResult(result)


def run_tests(tests, lit_config, opts, discovered_tests):
    workers = min(len(tests), opts.workers)
    display = lit.display.create_display(opts, tests, discovered_tests, workers)

    run = lit.run.Run(
        tests, lit_config, workers, display.update, opts.max_failures, opts.timeout
    )

    display.print_header()

    interrupted = False
    error = None
    try:
        execute_in_tmp_dir(run, lit_config)
    except KeyboardInterrupt:
        interrupted = True
        error = "  interrupted by user"
    except lit.run.MaxFailuresError:
        error = "warning: reached maximum number of test failures"
    except lit.run.TimeoutError:
        error = "warning: reached timeout"

    display.clear(interrupted)
    if error:
        sys.stderr.write("%s, skipping remaining tests\n" % error)


def execute_in_tmp_dir(run, lit_config):
    # Create a temp directory inside the normal temp directory so that we can
    # try to avoid temporary test file leaks. The user can avoid this behavior
    # by setting LIT_PRESERVES_TMP in the environment, so they can easily use
    # their own temp directory to monitor temporary file leaks or handle them at
    # the buildbot level.
    tmp_dir = None
    if "LIT_PRESERVES_TMP" not in os.environ:
        import tempfile

        # z/OS linker does not support '_' in paths, so use '-'.
        tmp_dir = tempfile.mkdtemp(prefix="lit-tmp-")
        tmp_dir_envs = {k: tmp_dir for k in ["TMP", "TMPDIR", "TEMP", "TEMPDIR"]}
        os.environ.update(tmp_dir_envs)
        for cfg in {t.config for t in run.tests}:
            cfg.environment.update(tmp_dir_envs)
    try:
        run.execute()
    finally:
        if tmp_dir:
            try:
                import shutil

                shutil.rmtree(tmp_dir)
            except Exception as e:
                lit_config.warning(
                    "Failed to delete temp directory '%s', try upgrading your version of Python to fix this"
                    % tmp_dir
                )


def print_histogram(tests):
    test_times = [
        (t.getFullName(), t.result.elapsed) for t in tests if t.result.elapsed
    ]
    if test_times:
        lit.util.printHistogram(test_times, title="Tests")


def print_results(tests, elapsed, opts):
    tests_by_code = {code: [] for code in lit.Test.ResultCode.all_codes()}
    total_tests = len(tests)
    for test in tests:
        tests_by_code[test.result.code].append(test)

    for code in lit.Test.ResultCode.all_codes():
        print_group(
            sorted(tests_by_code[code], key=lambda t: t.getFullName()),
            code,
            opts.shown_codes,
        )

    print_summary(total_tests, tests_by_code, opts.quiet, elapsed)


def print_group(tests, code, shown_codes):
    if not tests:
        return
    if not code.isFailure and code not in shown_codes:
        return
    print("*" * 20)
    print("{} Tests ({}):".format(code.label, len(tests)))
    for test in tests:
        print("  %s" % test.getFullName())
    sys.stdout.write("\n")


def print_summary(total_tests, tests_by_code, quiet, elapsed):
    if not quiet:
        print("\nTesting Time: %.2fs" % elapsed)

    print("\nTotal Discovered Tests: %s" % (total_tests))
    codes = [c for c in lit.Test.ResultCode.all_codes() if not quiet or c.isFailure]
    groups = [(c.label, len(tests_by_code[c])) for c in codes]
    groups = [(label, count) for label, count in groups if count]
    if not groups:
        return

    max_label_len = max(len(label) for label, _ in groups)
    max_count_len = max(len(str(count)) for _, count in groups)

    for (label, count) in groups:
        label = label.ljust(max_label_len)
        count = str(count).rjust(max_count_len)
        print("  %s: %s (%.2f%%)" % (label, count, float(count) / total_tests * 100))
