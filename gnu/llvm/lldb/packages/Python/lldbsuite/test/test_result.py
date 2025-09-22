"""
Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

Provides the LLDBTestResult class, which holds information about progress
and results of a single test run.
"""

# System modules
import os
import traceback

# Third-party modules
import unittest

# LLDB Modules
from . import configuration
from lldbsuite.test_event import build_exception


class LLDBTestResult(unittest.TextTestResult):
    """
    Enforce a singleton pattern to allow introspection of test progress.

    Overwrite addError(), addFailure(), and addExpectedFailure() methods
    to enable each test instance to track its failure/error status.  It
    is used in the LLDB test framework to emit detailed trace messages
    to a log file for easier human inspection of test failures/errors.
    """

    __singleton__ = None
    __ignore_singleton__ = False

    @staticmethod
    def getTerminalSize():
        import os

        env = os.environ

        def ioctl_GWINSZ(fd):
            try:
                import fcntl
                import termios
                import struct

                cr = struct.unpack("hh", fcntl.ioctl(fd, termios.TIOCGWINSZ, "1234"))
            except:
                return
            return cr

        cr = ioctl_GWINSZ(0) or ioctl_GWINSZ(1) or ioctl_GWINSZ(2)
        if not cr:
            try:
                fd = os.open(os.ctermid(), os.O_RDONLY)
                cr = ioctl_GWINSZ(fd)
                os.close(fd)
            except:
                pass
        if not cr:
            cr = (env.get("LINES", 25), env.get("COLUMNS", 80))
        return int(cr[1]), int(cr[0])

    def __init__(self, *args):
        if not LLDBTestResult.__ignore_singleton__ and LLDBTestResult.__singleton__:
            raise Exception("LLDBTestResult instantiated more than once")
        super(LLDBTestResult, self).__init__(*args)
        LLDBTestResult.__singleton__ = self
        # Now put this singleton into the lldb module namespace.
        configuration.test_result = self
        # Computes the format string for displaying the counter.
        counterWidth = len(str(configuration.suite.countTestCases()))
        self.fmt = "%" + str(counterWidth) + "d: "
        self.indentation = " " * (counterWidth + 2)
        # This counts from 1 .. suite.countTestCases().
        self.counter = 0
        (width, height) = LLDBTestResult.getTerminalSize()

    def _config_string(self, test):
        compiler = getattr(test, "getCompiler", None)
        arch = getattr(test, "getArchitecture", None)
        return "%s-%s" % (compiler() if compiler else "", arch() if arch else "")

    def _exc_info_to_string(self, err, test):
        """Overrides superclass TestResult's method in order to append
        our test config info string to the exception info string."""
        if hasattr(test, "getArchitecture") and hasattr(test, "getCompiler"):
            return "%sConfig=%s-%s" % (
                super(LLDBTestResult, self)._exc_info_to_string(err, test),
                test.getArchitecture(),
                test.getCompiler(),
            )
        else:
            return super(LLDBTestResult, self)._exc_info_to_string(err, test)

    def getDescription(self, test):
        doc_first_line = test.shortDescription()
        if self.descriptions and doc_first_line:
            return "\n".join((str(test), self.indentation + doc_first_line))
        else:
            return str(test)

    def _getTestPath(self, test):
        # Use test.test_filename if the test was created with
        # lldbinline.MakeInlineTest().
        if test is None:
            return ""
        elif hasattr(test, "test_filename"):
            return test.test_filename
        else:
            import inspect

            return inspect.getsourcefile(test.__class__)

    def _getFileBasedCategories(self, test):
        """
        Returns the list of categories to which this test case belongs by
        collecting values of "categories" files. We start at the folder the test is in
        and traverse the hierarchy upwards until the test-suite root directory.
        """
        start_path = self._getTestPath(test)

        import os.path

        folder = os.path.dirname(start_path)

        from lldbsuite import lldb_test_root as test_root

        if test_root != os.path.commonprefix([folder, test_root]):
            raise Exception(
                "The test file %s is outside the test root directory" % start_path
            )

        categories = set()
        while not os.path.samefile(folder, test_root):
            categories_file_name = os.path.join(folder, "categories")
            if os.path.exists(categories_file_name):
                categories_file = open(categories_file_name, "r")
                categories_str = categories_file.readline().strip()
                categories_file.close()
                categories.update(categories_str.split(","))
            folder = os.path.dirname(folder)

        return list(categories)

    def getCategoriesForTest(self, test):
        """
        Gets all the categories for the currently running test method in test case
        """
        test_categories = []
        test_categories.extend(getattr(test, "categories", []))

        test_method = getattr(test, test._testMethodName)
        if test_method is not None:
            test_categories.extend(getattr(test_method, "categories", []))

        test_categories.extend(self._getFileBasedCategories(test))

        return test_categories

    def hardMarkAsSkipped(self, test):
        getattr(test, test._testMethodName).__func__.__unittest_skip__ = True
        getattr(
            test, test._testMethodName
        ).__func__.__unittest_skip_why__ = (
            "test case does not fall in any category of interest for this run"
        )

    def checkExclusion(self, exclusion_list, name):
        if exclusion_list:
            import re

            for item in exclusion_list:
                if re.search(item, name):
                    return True
        return False

    def checkCategoryExclusion(self, exclusion_list, test):
        return not set(exclusion_list).isdisjoint(self.getCategoriesForTest(test))

    def startTest(self, test):
        if configuration.shouldSkipBecauseOfCategories(self.getCategoriesForTest(test)):
            self.hardMarkAsSkipped(test)
        if self.checkExclusion(configuration.skip_tests, test.id()):
            self.hardMarkAsSkipped(test)

        self.counter += 1
        test.test_number = self.counter
        if self.showAll:
            self.stream.write(self.fmt % self.counter)
        super(LLDBTestResult, self).startTest(test)

    def addSuccess(self, test):
        if self.checkExclusion(
            configuration.xfail_tests, test.id()
        ) or self.checkCategoryExclusion(configuration.xfail_categories, test):
            self.addUnexpectedSuccess(test, None)
            return

        super(LLDBTestResult, self).addSuccess(test)
        self.stream.write(
            "PASS: LLDB (%s) :: %s\n" % (self._config_string(test), str(test))
        )

    def _isBuildError(self, err_tuple):
        exception = err_tuple[1]
        return isinstance(exception, build_exception.BuildError)

    def _saveBuildErrorTuple(self, test, err):
        # Adjust the error description so it prints the build command and build error
        # rather than an uninformative Python backtrace.
        build_error = err[1]
        error_description = "{}\nTest Directory:\n{}".format(
            str(build_error), os.path.dirname(self._getTestPath(test))
        )
        self.errors.append((test, error_description))
        self._mirrorOutput = True

    def addError(self, test, err):
        configuration.sdir_has_content = True
        if self._isBuildError(err):
            self._saveBuildErrorTuple(test, err)
        else:
            super(LLDBTestResult, self).addError(test, err)

        method = getattr(test, "markError", None)
        if method:
            method()
        self.stream.write(
            "FAIL: LLDB (%s) :: %s\n" % (self._config_string(test), str(test))
        )

    def addCleanupError(self, test, err):
        configuration.sdir_has_content = True
        super(LLDBTestResult, self).addCleanupError(test, err)
        method = getattr(test, "markCleanupError", None)
        if method:
            method()
        self.stream.write(
            "CLEANUP ERROR: LLDB (%s) :: %s\n%s\n"
            % (self._config_string(test), str(test), traceback.format_exc())
        )

    def addFailure(self, test, err):
        if self.checkExclusion(
            configuration.xfail_tests, test.id()
        ) or self.checkCategoryExclusion(configuration.xfail_categories, test):
            self.addExpectedFailure(test, err)
            return

        configuration.sdir_has_content = True
        super(LLDBTestResult, self).addFailure(test, err)
        method = getattr(test, "markFailure", None)
        if method:
            method()
        self.stream.write(
            "FAIL: LLDB (%s) :: %s\n" % (self._config_string(test), str(test))
        )
        if configuration.use_categories:
            test_categories = self.getCategoriesForTest(test)
            for category in test_categories:
                if category in configuration.failures_per_category:
                    configuration.failures_per_category[category] = (
                        configuration.failures_per_category[category] + 1
                    )
                else:
                    configuration.failures_per_category[category] = 1

    def addExpectedFailure(self, test, err):
        configuration.sdir_has_content = True
        super(LLDBTestResult, self).addExpectedFailure(test, err)
        method = getattr(test, "markExpectedFailure", None)
        if method:
            method(err)
        self.stream.write(
            "XFAIL: LLDB (%s) :: %s\n" % (self._config_string(test), str(test))
        )

    def addSkip(self, test, reason):
        configuration.sdir_has_content = True
        super(LLDBTestResult, self).addSkip(test, reason)
        method = getattr(test, "markSkippedTest", None)
        if method:
            method()
        self.stream.write(
            "UNSUPPORTED: LLDB (%s) :: %s (%s) \n"
            % (self._config_string(test), str(test), reason)
        )

    def addUnexpectedSuccess(self, test):
        configuration.sdir_has_content = True
        super(LLDBTestResult, self).addUnexpectedSuccess(test)
        method = getattr(test, "markUnexpectedSuccess", None)
        if method:
            method()
        self.stream.write(
            "XPASS: LLDB (%s) :: %s\n" % (self._config_string(test), str(test))
        )
