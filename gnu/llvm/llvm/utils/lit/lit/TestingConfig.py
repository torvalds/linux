import os
import sys


class TestingConfig(object):
    """
    TestingConfig - Information on the tests inside a suite.
    """

    @staticmethod
    def fromdefaults(litConfig):
        """
        fromdefaults(litConfig) -> TestingConfig

        Create a TestingConfig object with default values.
        """
        # Set the environment based on the command line arguments.
        environment = {
            "PATH": os.pathsep.join(litConfig.path + [os.environ.get("PATH", "")]),
            "LLVM_DISABLE_CRASH_REPORT": "1",
        }

        pass_vars = [
            "LIBRARY_PATH",
            "LD_LIBRARY_PATH",
            "SYSTEMROOT",
            "TERM",
            "CLANG",
            "LLDB",
            "LD_PRELOAD",
            "LLVM_SYMBOLIZER_PATH",
            "LLVM_PROFILE_FILE",
            "ASAN_SYMBOLIZER_PATH",
            "HWASAN_SYMBOLIZER_PATH",
            "LSAN_SYMBOLIZER_PATH",
            "MSAN_SYMBOLIZER_PATH",
            "TSAN_SYMBOLIZER_PATH",
            "UBSAN_SYMBOLIZER_PATH",
            "ASAN_OPTIONS",
            "LSAN_OPTIONS",
            "HWASAN_OPTIONS",
            "MSAN_OPTIONS",
            "TSAN_OPTIONS",
            "UBSAN_OPTIONS",
            "ADB",
            "ADB_SERVER_SOCKET",
            "ANDROID_SERIAL",
            "SSH_AUTH_SOCK",
            "SANITIZER_IGNORE_CVE_2016_2143",
            "TMPDIR",
            "TMP",
            "TEMP",
            "TEMPDIR",
            "AVRLIT_BOARD",
            "AVRLIT_PORT",
            "FILECHECK_OPTS",
            "VCINSTALLDIR",
            "VCToolsinstallDir",
            "VSINSTALLDIR",
            "WindowsSdkDir",
            "WindowsSDKLibVersion",
            "SOURCE_DATE_EPOCH",
            "GTEST_FILTER",
            "DFLTCC",
        ]

        if sys.platform.startswith("aix"):
            pass_vars += ["LIBPATH"]
        elif sys.platform == "win32":
            pass_vars += [
                "COMSPEC",
                "INCLUDE",
                "LIB",
                "PATHEXT",
                "USERPROFILE",
            ]
            environment["PYTHONBUFFERED"] = "1"
            # Avoid Windows heuristics which try to detect potential installer
            # programs (which may need to run with elevated privileges) and ask
            # if the user wants to run them in that way. This heuristic may
            # match for executables containing the substrings "patch" (which is
            # a substring of "dispatch"), "update", "setup", etc. Set this
            # environment variable indicating that we want to execute them with
            # the current user.
            environment["__COMPAT_LAYER"] = "RunAsInvoker"

        for var in pass_vars:
            val = os.environ.get(var, "")
            # Check for empty string as some variables such as LD_PRELOAD cannot be empty
            # ('') for OS's such as OpenBSD.
            if val:
                environment[var] = val

        # Set the default available features based on the LitConfig.
        available_features = []
        if litConfig.useValgrind:
            available_features.append("valgrind")
            if litConfig.valgrindLeakCheck:
                available_features.append("vg_leak")

        return TestingConfig(
            None,
            name="<unnamed>",
            suffixes=set(),
            test_format=None,
            environment=environment,
            substitutions=[],
            unsupported=False,
            test_exec_root=None,
            test_source_root=None,
            excludes=[],
            available_features=available_features,
            pipefail=True,
            standalone_tests=False,
        )

    def load_from_path(self, path, litConfig):
        """
        load_from_path(path, litConfig)

        Load the configuration module at the provided path into the given config
        object.
        """

        # Load the config script data.
        data = None
        f = open(path)
        try:
            data = f.read()
        except:
            litConfig.fatal("unable to load config file: %r" % (path,))
        f.close()

        # Execute the config script to initialize the object.
        cfg_globals = dict(globals())
        cfg_globals["config"] = self
        cfg_globals["lit_config"] = litConfig
        cfg_globals["__file__"] = path
        try:
            exec(compile(data, path, "exec"), cfg_globals, None)
            if litConfig.debug:
                litConfig.note("... loaded config %r" % path)
        except SystemExit:
            e = sys.exc_info()[1]
            # We allow normal system exit inside a config file to just
            # return control without error.
            if e.args:
                raise
        except:
            import traceback

            litConfig.fatal(
                "unable to parse config file %r, traceback: %s"
                % (path, traceback.format_exc())
            )
        self.finish(litConfig)

    def __init__(
        self,
        parent,
        name,
        suffixes,
        test_format,
        environment,
        substitutions,
        unsupported,
        test_exec_root,
        test_source_root,
        excludes,
        available_features,
        pipefail,
        limit_to_features=[],
        is_early=False,
        parallelism_group=None,
        standalone_tests=False,
    ):
        self.parent = parent
        self.name = str(name)
        self.suffixes = set(suffixes)
        self.test_format = test_format
        self.environment = dict(environment)
        self.substitutions = list(substitutions)
        self.unsupported = unsupported
        self.test_exec_root = test_exec_root
        self.test_source_root = test_source_root
        self.excludes = set(excludes)
        self.available_features = set(available_features)
        self.pipefail = pipefail
        self.standalone_tests = standalone_tests
        # This list is used by TestRunner.py to restrict running only tests that
        # require one of the features in this list if this list is non-empty.
        # Configurations can set this list to restrict the set of tests to run.
        self.limit_to_features = set(limit_to_features)
        self.parallelism_group = parallelism_group
        self._recursiveExpansionLimit = None

    @property
    def recursiveExpansionLimit(self):
        return self._recursiveExpansionLimit

    @recursiveExpansionLimit.setter
    def recursiveExpansionLimit(self, value):
        if value is not None and not isinstance(value, int):
            raise ValueError(
                "recursiveExpansionLimit must be either None or an integer (got <{}>)".format(
                    value
                )
            )
        if isinstance(value, int) and value < 0:
            raise ValueError(
                "recursiveExpansionLimit must be a non-negative integer (got <{}>)".format(
                    value
                )
            )
        self._recursiveExpansionLimit = value

    def finish(self, litConfig):
        """finish() - Finish this config object, after loading is complete."""

        self.name = str(self.name)
        self.suffixes = set(self.suffixes)
        self.environment = dict(self.environment)
        self.substitutions = list(self.substitutions)
        if self.test_exec_root is not None:
            # FIXME: This should really only be suite in test suite config
            # files. Should we distinguish them?
            self.test_exec_root = str(self.test_exec_root)
        if self.test_source_root is not None:
            # FIXME: This should really only be suite in test suite config
            # files. Should we distinguish them?
            self.test_source_root = str(self.test_source_root)
        self.excludes = set(self.excludes)

    @property
    def root(self):
        """root attribute - The root configuration for the test suite."""
        if self.parent is None:
            return self
        else:
            return self.parent.root


class SubstituteCaptures:
    """
    Helper class to indicate that the substitutions contains backreferences.

    This can be used as the following in lit.cfg to mark subsitutions as having
    back-references::

        config.substutions.append(('\b[^ ]*.cpp', SubstituteCaptures('\0.txt')))

    """

    def __init__(self, substitution):
        self.substitution = substitution

    def replace(self, pattern, replacement):
        return self.substitution

    def __str__(self):
        return self.substitution

    def __len__(self):
        return len(self.substitution)

    def __getitem__(self, item):
        return self.substitution.__getitem__(item)
