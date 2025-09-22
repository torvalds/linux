from __future__ import absolute_import
import inspect
import os
import platform
import sys

import lit.Test
import lit.formats
import lit.TestingConfig
import lit.util

# LitConfig must be a new style class for properties to work
class LitConfig(object):
    """LitConfig - Configuration data for a 'lit' test runner instance, shared
    across all tests.

    The LitConfig object is also used to communicate with client configuration
    files, it is always passed in as the global variable 'lit' so that
    configuration files can access common functionality and internal components
    easily.
    """

    def __init__(
        self,
        progname,
        path,
        quiet,
        useValgrind,
        valgrindLeakCheck,
        valgrindArgs,
        noExecute,
        debug,
        isWindows,
        order,
        params,
        config_prefix=None,
        maxIndividualTestTime=0,
        parallelism_groups={},
        per_test_coverage=False,
        gtest_sharding=True,
    ):
        # The name of the test runner.
        self.progname = progname
        # The items to add to the PATH environment variable.
        self.path = [str(p) for p in path]
        self.quiet = bool(quiet)
        self.useValgrind = bool(useValgrind)
        self.valgrindLeakCheck = bool(valgrindLeakCheck)
        self.valgrindUserArgs = list(valgrindArgs)
        self.noExecute = noExecute
        self.debug = debug
        self.isWindows = bool(isWindows)
        self.order = order
        self.params = dict(params)
        self.bashPath = None

        # Configuration files to look for when discovering test suites.
        self.config_prefix = config_prefix or "lit"
        self.suffixes = ["cfg.py", "cfg"]
        self.config_names = ["%s.%s" % (self.config_prefix, x) for x in self.suffixes]
        self.site_config_names = [
            "%s.site.%s" % (self.config_prefix, x) for x in self.suffixes
        ]
        self.local_config_names = [
            "%s.local.%s" % (self.config_prefix, x) for x in self.suffixes
        ]

        self.numErrors = 0
        self.numWarnings = 0

        self.valgrindArgs = []
        if self.useValgrind:
            self.valgrindArgs = [
                "valgrind",
                "-q",
                "--run-libc-freeres=no",
                "--tool=memcheck",
                "--trace-children=yes",
                "--error-exitcode=123",
            ]
            if self.valgrindLeakCheck:
                self.valgrindArgs.append("--leak-check=full")
            else:
                # The default is 'summary'.
                self.valgrindArgs.append("--leak-check=no")
            self.valgrindArgs.extend(self.valgrindUserArgs)

        self.maxIndividualTestTime = maxIndividualTestTime
        self.parallelism_groups = parallelism_groups
        self.per_test_coverage = per_test_coverage
        self.gtest_sharding = bool(gtest_sharding)

    @property
    def maxIndividualTestTime(self):
        """
        Interface for getting maximum time to spend executing
        a single test
        """
        return self._maxIndividualTestTime

    @property
    def maxIndividualTestTimeIsSupported(self):
        """
        Returns a tuple (<supported> , <error message>)
        where
        `<supported>` is True if setting maxIndividualTestTime is supported
            on the current host, returns False otherwise.
        `<error message>` is an empty string if `<supported>` is True,
            otherwise is contains a string describing why setting
            maxIndividualTestTime is not supported.
        """
        return lit.util.killProcessAndChildrenIsSupported()

    @maxIndividualTestTime.setter
    def maxIndividualTestTime(self, value):
        """
        Interface for setting maximum time to spend executing
        a single test
        """
        if not isinstance(value, int):
            self.fatal("maxIndividualTestTime must set to a value of type int.")
        self._maxIndividualTestTime = value
        if self.maxIndividualTestTime > 0:
            # The current implementation needs psutil on some platforms to set
            # a timeout per test. Check it's available.
            # See lit.util.killProcessAndChildren()
            supported, errormsg = self.maxIndividualTestTimeIsSupported
            if not supported:
                self.fatal("Setting a timeout per test not supported. " + errormsg)
        elif self.maxIndividualTestTime < 0:
            self.fatal("The timeout per test must be >= 0 seconds")

    @property
    def per_test_coverage(self):
        """
        Interface for getting the per_test_coverage value
        """
        return self._per_test_coverage

    @per_test_coverage.setter
    def per_test_coverage(self, value):
        """
        Interface for setting the per_test_coverage value
        """
        if not isinstance(value, bool):
            self.fatal("per_test_coverage must set to a value of type bool.")
        self._per_test_coverage = value

    def load_config(self, config, path):
        """load_config(config, path) - Load a config object from an alternate
        path."""
        if self.debug:
            self.note("load_config from %r" % path)
        config.load_from_path(path, self)
        return config

    def getBashPath(self):
        """getBashPath - Get the path to 'bash'"""
        if self.bashPath is not None:
            return self.bashPath

        self.bashPath = lit.util.which("bash", os.pathsep.join(self.path))
        if self.bashPath is None:
            self.bashPath = lit.util.which("bash")

        if self.bashPath is None:
            self.bashPath = ""

        # Check whether the found version of bash is able to cope with paths in
        # the host path format. If not, don't return it as it can't be used to
        # run scripts. For example, WSL's bash.exe requires '/mnt/c/foo' rather
        # than 'C:\\foo' or 'C:/foo'.
        if self.isWindows and self.bashPath:
            command = [
                self.bashPath,
                "-c",
                '[[ -f "%s" ]]' % self.bashPath.replace("\\", "\\\\"),
            ]
            _, _, exitCode = lit.util.executeCommand(command)
            if exitCode:
                self.note(
                    "bash command failed: %s" % (" ".join('"%s"' % c for c in command))
                )
                self.bashPath = ""

        if not self.bashPath:
            self.warning("Unable to find a usable version of bash.")

        return self.bashPath

    def getToolsPath(self, dir, paths, tools):
        if dir is not None and os.path.isabs(dir) and os.path.isdir(dir):
            if not lit.util.checkToolsPath(dir, tools):
                return None
        else:
            dir = lit.util.whichTools(tools, paths)

        # bash
        self.bashPath = lit.util.which("bash", dir)
        if self.bashPath is None:
            self.bashPath = ""

        return dir

    def _write_message(self, kind, message):
        # Get the file/line where this message was generated.
        f = inspect.currentframe()
        # Step out of _write_message, and then out of wrapper.
        f = f.f_back.f_back
        file = os.path.abspath(inspect.getsourcefile(f))
        line = inspect.getlineno(f)
        sys.stderr.write(
            "%s: %s:%d: %s: %s\n" % (self.progname, file, line, kind, message)
        )
        if self.isWindows:
            # In a git bash terminal, the writes to sys.stderr aren't visible
            # on screen immediately. Flush them here to avoid broken/misoredered
            # output.
            sys.stderr.flush()

    def substitute(self, string):
        """substitute - Interpolate params into a string"""
        try:
            return string % self.params
        except KeyError as e:
            (key,) = e.args
            self.fatal(
                "unable to find %r parameter, use '--param=%s=VALUE'" % (key, key)
            )

    def note(self, message):
        if not self.quiet:
            self._write_message("note", message)

    def warning(self, message):
        if not self.quiet:
            self._write_message("warning", message)
        self.numWarnings += 1

    def error(self, message):
        self._write_message("error", message)
        self.numErrors += 1

    def fatal(self, message):
        self._write_message("fatal", message)
        sys.exit(2)
