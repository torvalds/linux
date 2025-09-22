import os
import platform
import subprocess
import sys
import itertools

import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil
from lldbsuite.test import configuration
from lldbsuite.test_event import build_exception


class Builder:
    def getArchitecture(self):
        """Returns the architecture in effect the test suite is running with."""
        return configuration.arch if configuration.arch else ""

    def getCompiler(self):
        """Returns the compiler in effect the test suite is running with."""
        compiler = configuration.compiler if configuration.compiler else "clang"
        compiler = lldbutil.which(compiler)
        return os.path.abspath(compiler)

    def getTriple(self, arch):
        """Returns the triple for the given architecture or None."""
        return None

    def getExtraMakeArgs(self):
        """
        Helper function to return extra argumentsfor the make system. This
        method is meant to be overridden by platform specific builders.
        """
        return []

    def getArchCFlags(self, architecture):
        """Returns the ARCH_CFLAGS for the make system."""
        return []

    def getMake(self, test_subdir, test_name):
        """Returns the invocation for GNU make.
        The first argument is a tuple of the relative path to the testcase
        and its filename stem."""
        # Construct the base make invocation.
        lldb_test = os.environ["LLDB_TEST"]
        if not (
            lldb_test
            and configuration.test_build_dir
            and test_subdir
            and test_name
            and (not os.path.isabs(test_subdir))
        ):
            raise Exception("Could not derive test directories")
        build_dir = os.path.join(configuration.test_build_dir, test_subdir, test_name)
        src_dir = os.path.join(configuration.test_src_root, test_subdir)
        # This is a bit of a hack to make inline testcases work.
        makefile = os.path.join(src_dir, "Makefile")
        if not os.path.isfile(makefile):
            makefile = os.path.join(build_dir, "Makefile")
        return [
            configuration.make_path,
            "VPATH=" + src_dir,
            "-C",
            build_dir,
            "-I",
            src_dir,
            "-I",
            os.path.join(lldb_test, "make"),
            "-f",
            makefile,
        ]

    def getCmdLine(self, d):
        """
        Helper function to return a command line argument string used for the
        make system.
        """

        # If d is None or an empty mapping, just return an empty list.
        if not d:
            return []

        def setOrAppendVariable(k, v):
            append_vars = ["CFLAGS", "CFLAGS_EXTRAS", "LD_EXTRAS"]
            if k in append_vars and k in os.environ:
                v = os.environ[k] + " " + v
            return "%s=%s" % (k, v)

        cmdline = [setOrAppendVariable(k, v) for k, v in list(d.items())]

        return cmdline

    def getArchSpec(self, architecture):
        """
        Helper function to return the key-value string to specify the architecture
        used for the make system.
        """
        return ["ARCH=" + architecture] if architecture else []

    def getCCSpec(self, compiler):
        """
        Helper function to return the key-value string to specify the compiler
        used for the make system.
        """
        cc = compiler if compiler else None
        if not cc and configuration.compiler:
            cc = configuration.compiler
        if cc:
            return ['CC="%s"' % cc]
        return []

    def getSDKRootSpec(self):
        """
        Helper function to return the key-value string to specify the SDK root
        used for the make system.
        """
        if configuration.sdkroot:
            return ["SDKROOT={}".format(configuration.sdkroot)]
        return []

    def getModuleCacheSpec(self):
        """
        Helper function to return the key-value string to specify the clang
        module cache used for the make system.
        """
        if configuration.clang_module_cache_dir:
            return [
                "CLANG_MODULE_CACHE_DIR={}".format(configuration.clang_module_cache_dir)
            ]
        return []

    def getLibCxxArgs(self):
        if configuration.libcxx_include_dir and configuration.libcxx_library_dir:
            libcpp_args = [
                "LIBCPP_INCLUDE_DIR={}".format(configuration.libcxx_include_dir),
                "LIBCPP_LIBRARY_DIR={}".format(configuration.libcxx_library_dir),
            ]
            if configuration.libcxx_include_target_dir:
                libcpp_args.append(
                    "LIBCPP_INCLUDE_TARGET_DIR={}".format(
                        configuration.libcxx_include_target_dir
                    )
                )
            return libcpp_args
        return []

    def getLLDBObjRoot(self):
        return ["LLDB_OBJ_ROOT={}".format(configuration.lldb_obj_root)]

    def _getDebugInfoArgs(self, debug_info):
        if debug_info is None:
            return []
        if debug_info == "dwarf":
            return ["MAKE_DSYM=NO"]
        if debug_info == "dwo":
            return ["MAKE_DSYM=NO", "MAKE_DWO=YES"]
        if debug_info == "gmodules":
            return ["MAKE_DSYM=NO", "MAKE_GMODULES=YES"]
        return None

    def getBuildCommand(
        self,
        debug_info,
        architecture=None,
        compiler=None,
        dictionary=None,
        testdir=None,
        testname=None,
        make_targets=None,
    ):
        debug_info_args = self._getDebugInfoArgs(debug_info)
        if debug_info_args is None:
            return None
        if make_targets is None:
            make_targets = ["all"]
        command_parts = [
            self.getMake(testdir, testname),
            debug_info_args,
            make_targets,
            self.getArchCFlags(architecture),
            self.getArchSpec(architecture),
            self.getCCSpec(compiler),
            self.getExtraMakeArgs(),
            self.getSDKRootSpec(),
            self.getModuleCacheSpec(),
            self.getLibCxxArgs(),
            self.getLLDBObjRoot(),
            self.getCmdLine(dictionary),
        ]
        command = list(itertools.chain(*command_parts))

        return command

    def cleanup(self, dictionary=None):
        """Perform a platform-specific cleanup after the test."""
        return True
