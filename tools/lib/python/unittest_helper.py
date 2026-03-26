#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025-2026: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=C0103,R0912,R0914,E1101

"""
Provides helper functions and classes execute python unit tests.

Those help functions provide a nice colored output summary of each
executed test and, when a test fails, it shows the different in diff
format when running in verbose mode, like::

    $ tools/unittests/nested_match.py -v
    ...
    Traceback (most recent call last):
    File "/new_devel/docs/tools/unittests/nested_match.py", line 69, in test_count_limit
        self.assertEqual(replaced, "bar(a); bar(b); foo(c)")
        ~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    AssertionError: 'bar(a) foo(b); foo(c)' != 'bar(a); bar(b); foo(c)'
    - bar(a) foo(b); foo(c)
    ?       ^^^^
    + bar(a); bar(b); foo(c)
    ?       ^^^^^
    ...

It also allows filtering what tests will be executed via ``-k`` parameter.

Typical usage is to do::

    from unittest_helper import run_unittest
    ...

    if __name__ == "__main__":
        run_unittest(__file__)

If passing arguments is needed, on a more complex scenario, it can be
used like on this example::

    from unittest_helper import TestUnits, run_unittest
    ...
    env = {'sudo': ""}
    ...
    if __name__ == "__main__":
        runner = TestUnits()
        base_parser = runner.parse_args()
        base_parser.add_argument('--sudo', action='store_true',
                                help='Enable tests requiring sudo privileges')

        args = base_parser.parse_args()

        # Update module-level flag
        if args.sudo:
            env['sudo'] = "1"

        # Run tests with customized arguments
        runner.run(__file__, parser=base_parser, args=args, env=env)
"""

import argparse
import atexit
import os
import re
import unittest
import sys

from unittest.mock import patch


class Summary(unittest.TestResult):
    """
    Overrides ``unittest.TestResult`` class to provide a nice colored
    summary. When in verbose mode, displays actual/expected difference in
    unified diff format.
    """
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        #: Dictionary to store organized test results.
        self.test_results = {}

        #: max length of the test names.
        self.max_name_length = 0

    def startTest(self, test):
        super().startTest(test)
        test_id = test.id()
        parts = test_id.split(".")

        # Extract module, class, and method names
        if len(parts) >= 3:
            module_name = parts[-3]
        else:
            module_name = ""
        if len(parts) >= 2:
            class_name = parts[-2]
        else:
            class_name = ""

        method_name = parts[-1]

        # Build the hierarchical structure
        if module_name not in self.test_results:
            self.test_results[module_name] = {}

        if class_name not in self.test_results[module_name]:
            self.test_results[module_name][class_name] = []

        # Track maximum test name length for alignment
        display_name = f"{method_name}:"

        self.max_name_length = max(len(display_name), self.max_name_length)

    def _record_test(self, test, status):
        test_id = test.id()
        parts = test_id.split(".")
        if len(parts) >= 3:
            module_name = parts[-3]
        else:
            module_name = ""
        if len(parts) >= 2:
            class_name = parts[-2]
        else:
            class_name = ""
        method_name = parts[-1]
        self.test_results[module_name][class_name].append((method_name, status))

    def addSuccess(self, test):
        super().addSuccess(test)
        self._record_test(test, "OK")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self._record_test(test, "FAIL")

    def addError(self, test, err):
        super().addError(test, err)
        self._record_test(test, "ERROR")

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._record_test(test, f"SKIP ({reason})")

    def printResults(self, verbose):
        """
        Print results using colors if tty.
        """
        # Check for ANSI color support
        use_color = sys.stdout.isatty()
        COLORS = {
            "OK":            "\033[32m",   # Green
            "FAIL":          "\033[31m",   # Red
            "SKIP":          "\033[1;33m", # Yellow
            "PARTIAL":       "\033[33m",   # Orange
            "EXPECTED_FAIL": "\033[36m",   # Cyan
            "reset":         "\033[0m",    # Reset to default terminal color
        }
        if not use_color:
            for c in COLORS:
                COLORS[c] = ""

        # Calculate maximum test name length
        if not self.test_results:
            return
        try:
            lengths = []
            for module in self.test_results.values():
                for tests in module.values():
                    for test_name, _ in tests:
                        lengths.append(len(test_name) + 1)  # +1 for colon
            max_length = max(lengths) + 2  # Additional padding
        except ValueError:
            sys.exit("Test list is empty")

        # Print results
        for module_name, classes in self.test_results.items():
            if verbose:
                print(f"{module_name}:")
            for class_name, tests in classes.items():
                if verbose:
                    print(f"    {class_name}:")
                for test_name, status in tests:
                    if not verbose and status in [ "OK", "EXPECTED_FAIL" ]:
                        continue

                    # Get base status without reason for SKIP
                    if status.startswith("SKIP"):
                        status_code = status.split()[0]
                    else:
                        status_code = status
                    color = COLORS.get(status_code, "")
                    print(
                        f"        {test_name + ':':<{max_length}}{color}{status}{COLORS['reset']}"
                    )
            if verbose:
                print()

        # Print summary
        print(f"\nRan {self.testsRun} tests", end="")
        if hasattr(self, "timeTaken"):
            print(f" in {self.timeTaken:.3f}s", end="")
        print()

        if not self.wasSuccessful():
            print(f"\n{COLORS['FAIL']}FAILED (", end="")
            failures = getattr(self, "failures", [])
            errors = getattr(self, "errors", [])
            if failures:
                print(f"failures={len(failures)}", end="")
            if errors:
                if failures:
                    print(", ", end="")
                print(f"errors={len(errors)}", end="")
            print(f"){COLORS['reset']}")


def flatten_suite(suite):
    """Flatten test suite hierarchy."""
    tests = []
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            tests.extend(flatten_suite(item))
        else:
            tests.append(item)
    return tests


class TestUnits:
    """
    Helper class to set verbosity level.

    This class discover test files, import its unittest classes and
    executes the test on it.
    """
    def parse_args(self):
        """Returns a parser for command line arguments."""
        parser = argparse.ArgumentParser(description="Test runner with regex filtering")
        parser.add_argument("-v", "--verbose", action="count", default=1)
        parser.add_argument("-q", "--quiet", action="store_true")
        parser.add_argument("-f", "--failfast", action="store_true")
        parser.add_argument("-k", "--keyword",
                            help="Regex pattern to filter test methods")
        return parser

    def run(self, caller_file=None, pattern=None,
            suite=None, parser=None, args=None, env=None):
        """
        Execute all tests from the unity test file.

        It contains several optional parameters:

        ``caller_file``:
            -  name of the file that contains test.

               typical usage is to place __file__ at the caller test, e.g.::

                    if __name__ == "__main__":
                        TestUnits().run(__file__)

        ``pattern``:
            - optional pattern to match multiple file names. Defaults
              to basename of ``caller_file``.

        ``suite``:
            - an unittest suite initialized by the caller using
              ``unittest.TestLoader().discover()``.

        ``parser``:
            - an argparse parser. If not defined, this helper will create
              one.

        ``args``:
            - an ``argparse.Namespace`` data filled by the caller.

        ``env``:
            - environment variables that will be passed to the test suite

        At least ``caller_file`` or ``suite`` must be used, otherwise a
        ``TypeError`` will be raised.
        """
        if not args:
            if not parser:
                parser = self.parse_args()
            args = parser.parse_args()

        if not caller_file and not suite:
            raise TypeError("Either caller_file or suite is needed at TestUnits")

        if args.quiet:
            verbose = 0
        else:
            verbose = args.verbose

        if not env:
            env = os.environ.copy()

        env["VERBOSE"] = f"{verbose}"

        patcher = patch.dict(os.environ, env)
        patcher.start()
        # ensure it gets stopped after
        atexit.register(patcher.stop)


        if verbose >= 2:
            unittest.TextTestRunner(verbosity=verbose).run = lambda suite: suite

        # Load ONLY tests from the calling file
        if not suite:
            if not pattern:
                pattern = caller_file

            loader = unittest.TestLoader()
            suite = loader.discover(start_dir=os.path.dirname(caller_file),
                                    pattern=os.path.basename(caller_file))

        # Flatten the suite for environment injection
        tests_to_inject = flatten_suite(suite)

        # Filter tests by method name if -k specified
        if args.keyword:
            try:
                pattern = re.compile(args.keyword)
                filtered_suite = unittest.TestSuite()
                for test in tests_to_inject:  # Use the pre-flattened list
                    method_name = test.id().split(".")[-1]
                    if pattern.search(method_name):
                        filtered_suite.addTest(test)
                suite = filtered_suite
            except re.error as e:
                sys.stderr.write(f"Invalid regex pattern: {e}\n")
                sys.exit(1)
        else:
            # Maintain original suite structure if no keyword filtering
            suite = unittest.TestSuite(tests_to_inject)

        if verbose >= 2:
            resultclass = None
        else:
            resultclass = Summary

        runner = unittest.TextTestRunner(verbosity=args.verbose,
                                            resultclass=resultclass,
                                            failfast=args.failfast)
        result = runner.run(suite)
        if resultclass:
            result.printResults(verbose)

        sys.exit(not result.wasSuccessful())


def run_unittest(fname):
    """
    Basic usage of TestUnits class.

    Use it when there's no need to pass any extra argument to the tests
    with. The recommended way is to place this at the end of each
    unittest module::

        if __name__ == "__main__":
            run_unittest(__file__)
    """
    TestUnits().run(fname)
