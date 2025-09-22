"""
Provides definitions for various lldb test categories
"""

# System modules
import sys

# Third-party modules

# LLDB modules
from lldbsuite.support import gmodules

# Key: Category name
# Value: should be used in lldbtest's debug-info replication
debug_info_categories = {"dwarf": True, "dwo": True, "dsym": True, "gmodules": False}

all_categories = {
    "basic_process": "Basic process execution sniff tests.",
    "cmdline": "Tests related to the LLDB command-line interface",
    "dataformatters": "Tests related to the type command and the data formatters subsystem",
    "debugserver": "Debugserver tests",
    "dsym": "Tests that can be run with DSYM debug information",
    "dwarf": "Tests that can be run with DWARF debug information",
    "dwo": "Tests that can be run with DWO debug information",
    "dyntype": "Tests related to dynamic type support",
    "expression": "Tests related to the expression parser",
    "flakey": "Flakey test cases, i.e. tests that do not reliably pass at each execution",
    "fork": "Tests requiring the process plugin fork/vfork event support",
    "gmodules": "Tests that can be run with -gmodules debug information",
    "instrumentation-runtime": "Tests for the instrumentation runtime plugins",
    "libc++": "Test for libc++ data formatters",
    "libstdcxx": "Test for libstdcxx data formatters",
    "lldb-server": "Tests related to lldb-server",
    "lldb-dap": "Tests for the Debug Adaptor Protocol with lldb-dap",
    "llgs": "Tests for the gdb-server functionality of lldb-server",
    "pexpect": "Tests requiring the pexpect library to be available",
    "objc": "Tests related to the Objective-C programming language support",
    "pyapi": "Tests related to the Python API",
    "std-module": "Tests related to importing the std module",
    "stresstest": "Tests related to stressing lldb limits",
    "watchpoint": "Watchpoint-related tests",
}


def unique_string_match(yourentry, list):
    candidate = None
    for item in list:
        if not item.startswith(yourentry):
            continue
        if candidate:
            return None
        candidate = item
    return candidate


def is_supported_on_platform(category, platform, compiler_path):
    if category == "dwo":
        # -gsplit-dwarf is not implemented by clang on Windows.
        return platform in ["linux", "freebsd"]
    elif category == "dsym":
        return platform in ["darwin", "macosx", "ios", "watchos", "tvos", "bridgeos"]
    elif category == "gmodules":
        # First, check to see if the platform can even support gmodules.
        if platform not in ["darwin", "macosx", "ios", "watchos", "tvos", "bridgeos"]:
            return False
        return gmodules.is_compiler_clang_with_gmodules(compiler_path)
    return True


def validate(categories, exact_match):
    """
    For each category in categories, ensure that it's a valid category (if exact_match is false,
    unique prefixes are also accepted). If a category is invalid, print a message and quit.
       If all categories are valid, return the list of categories. Prefixes are expanded in the
       returned list.
    """
    result = []
    for category in categories:
        origCategory = category
        if category not in all_categories and not exact_match:
            category = unique_string_match(category, all_categories)
        if (category not in all_categories) or category is None:
            print(
                "fatal error: category '" + origCategory + "' is not a valid category"
            )
            print(
                "if you have added a new category, please edit test_categories.py, adding your new category to all_categories"
            )
            print(
                "else, please specify one or more of the following: "
                + str(list(all_categories.keys()))
            )
            sys.exit(1)
        result.append(category)
    return result
