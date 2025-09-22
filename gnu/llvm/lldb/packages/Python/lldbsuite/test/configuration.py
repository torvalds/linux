"""
Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

Provides the configuration class, which holds all information related to
how this invocation of the test suite should be run.
"""

# System modules
import os


# Third-party modules
import unittest

# LLDB Modules
import lldbsuite


# The test suite.
suite = unittest.TestSuite()

# The list of categories we said we care about
categories_list = None
# set to true if we are going to use categories for cherry-picking test cases
use_categories = False
# Categories we want to skip
skip_categories = []
# Categories we expect to fail
xfail_categories = []
# use this to track per-category failures
failures_per_category = {}

# The path to LLDB.framework is optional.
lldb_framework_path = None

# Test suite repeat count.  Can be overwritten with '-# count'.
count = 1

# The 'arch' and 'compiler' can be specified via command line.
arch = None
compiler = None
dsymutil = None
sdkroot = None
make_path = None

# The overriden dwarf verison.
dwarf_version = 0

# Any overridden settings.
settings = []

# Path to the FileCheck testing tool. Not optional.
filecheck = None

# Path to the yaml2obj tool. Not optional.
yaml2obj = None

# The arch might dictate some specific CFLAGS to be passed to the toolchain to build
# the inferior programs.  The global variable cflags_extras provides a hook to do
# just that.
cflags_extras = ""

# The filters (testclass.testmethod) used to admit tests into our test suite.
filters = []

# The regular expression pattern to match against eligible filenames as
# our test cases.
regexp = None

# Sets of tests which are excluded at runtime
skip_tests = None
xfail_tests = None

# Set this flag if there is any session info dumped during the test run.
sdir_has_content = False
# svn_info stores the output from 'svn info lldb.base.dir'.
svn_info = ""

# Default verbosity is 0.
verbose = 0

# By default, search from the script directory.
# We can't use sys.path[0] to determine the script directory
# because it doesn't work under a debugger
testdirs = [lldbsuite.lldb_test_root]

# The root of the test case tree (where the actual tests reside, not the test
# infrastructure).
test_src_root = lldbsuite.lldb_test_root

# Separator string.
separator = "-" * 70

failed = False

# LLDB Remote platform setting
lldb_platform_name = None
lldb_platform_url = None
lldb_platform_working_dir = None

# Apple SDK
apple_sdk = None

# The base directory in which the tests are being built.
test_build_dir = None

# The clang module cache directory used by lldb.
lldb_module_cache_dir = None
# The clang module cache directory used by clang.
clang_module_cache_dir = None

# Test results handling globals
test_result = None

# The names of all tests. Used to assert we don't have two tests with the
# same base name.
all_tests = set()

# LLDB library directory.
lldb_libs_dir = None
lldb_obj_root = None

libcxx_include_dir = None
libcxx_include_target_dir = None
libcxx_library_dir = None

# A plugin whose tests will be enabled, like intel-pt.
enabled_plugins = []


def shouldSkipBecauseOfCategories(test_categories):
    if use_categories:
        if (
            len(test_categories) == 0
            or len(categories_list & set(test_categories)) == 0
        ):
            return True

    for category in skip_categories:
        if category in test_categories:
            return True

    return False


def get_filecheck_path():
    """
    Get the path to the FileCheck testing tool.
    """
    if filecheck and os.path.lexists(filecheck):
        return filecheck


def get_yaml2obj_path():
    """
    Get the path to the yaml2obj tool.
    """
    if yaml2obj and os.path.lexists(yaml2obj):
        return yaml2obj
