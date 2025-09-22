"""
Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

Sync lldb and related source from a local machine to a remote machine.

This facilitates working on the lldb sourcecode on multiple machines
and multiple OS types, verifying changes across all.

Provides helper support for adding lldb test paths to the python path.
"""

# System modules
import os
import platform
import subprocess
import sys

# Third-party modules

# LLDB modules


def add_lldb_test_paths(check_dir):
    # pylint: disable=line-too-long
    """Adds lldb test-related paths to the python path.

    Starting with the given directory and working upward through
    each parent directory up to the root, it looks for the lldb
    test directory.  When found, the lldb test directory and its
    child test_runner/lib directory will be added to the python
    system path.

    Instructions for use:

    This method supports a simple way of getting pylint to be able
    to reliably lint lldb python test scripts (including the test
    infrastructure itself).  To do so, add the following to a
    .pylintrc file in your home directory:

    [Master]
    init-hook='import os; import sys; sys.path.append(os.path.expanduser("~/path/to/lldb/packages/Python/lldbsuite/test")); import lldb_pylint_helper; lldb_pylint_helper.add_lldb_test_paths(os.getcwd()); print("sys.path={}\n".format(sys.path))'

    Replace ~/path/to/lldb with a valid path to your local lldb source
    tree.  Note you can have multiple lldb source trees on your system, and
    this will work just fine.  The path in your .pylintrc is just needed to
    find the paths needed for pylint in whatever lldb source tree you're in.
    pylint will use the python files in whichever tree it is run from.

    Note it is critical that the init-hook line be contained on a single line.
    You can remove the print line at the end once you know the pythonpath is
    getting set up the way you expect.

    With these changes, you will be able to run the following, for example.

    cd lldb/sourcetree/1-of-many/test/lang/c/anonymous
    pylint TestAnonymous.py

    This will work, and include all the lldb/sourcetree/1-of-many lldb-specific
    python directories to your path.

    You can then run it in another lldb source tree on the same machine like
    so:

    cd lldb/sourcetree/2-of-many/test/functionalities/inferior-assert
    pyline TestInferiorAssert.py

    and this will properly lint that file, using the lldb-specific python
    directories from the 2-of-many source tree.

    Note at the time I'm writing this, our tests are in pretty sad shape
    as far as a stock pylint setup goes.  But we need to start somewhere :-)

    @param check_dir specifies a directory that will be used to start
    looking for the lldb test infrastructure python library paths.
    """
    # Add the test-related packages themselves.
    add_lldb_test_package_paths(check_dir)

    # Add the lldb directory itself
    add_lldb_module_directory()


def add_lldb_module_directory():
    """
    Desired Approach:

    Part A: find an lldb

    1. Walk up the parent chain from the current directory, looking for
    a directory matching *build*.  If we find that, use it as the
    root of a directory search for an lldb[.exe] executable.

    2. If 1 fails, use the path and look for an lldb[.exe] in there.

    If Part A ends up with an lldb, go to part B.  Otherwise, give up
    on the lldb python module path.

    Part B: use the output from 'lldb[.exe] -P' to find the lldb dir.

    Current approach:
    If Darwin, use 'xcrun lldb -P'; others: find lldb on path.

    Drawback to current approach:
    If the tester is changing the SB API (adding new methods), pylint
    will not know about them as it is using the wrong lldb python module.
    In practice, this should be minor.
    """
    try:
        lldb_module_path = None

        if platform.system() == "Darwin":
            # Use xcrun to find the selected lldb.
            lldb_module_path = subprocess.check_output(["xcrun", "lldb", "-P"])
        elif platform.system() == "Windows":
            lldb_module_path = subprocess.check_output(["lldb.exe", "-P"], shell=True)
        else:
            # Use the shell to run lldb from the path.
            lldb_module_path = subprocess.check_output(["lldb", "-P"], shell=True)

        # Trim the result.
        if lldb_module_path is not None:
            lldb_module_path = lldb_module_path.strip()

        # If we have a result, add it to the path
        if lldb_module_path is not None and len(lldb_module_path) > 0:
            sys.path.insert(0, lldb_module_path)
    # pylint: disable=broad-except
    except Exception as exception:
        print("failed to find python path: {}".format(exception))


def add_lldb_test_package_paths(check_dir):
    """Adds the lldb test infrastructure modules to the python path.

    See add_lldb_test_paths for more details.

    @param check_dir the directory of the test.
    """

    def child_dirs(parent_dir):
        return [
            os.path.join(parent_dir, child)
            for child in os.listdir(parent_dir)
            if os.path.isdir(os.path.join(parent_dir, child))
        ]

    check_dir = os.path.realpath(check_dir)
    while check_dir and len(check_dir) > 0:
        # If the current directory contains a packages/Python
        # directory, add that directory to the path.
        packages_python_child_dir = os.path.join(check_dir, "packages", "Python")
        if os.path.exists(packages_python_child_dir):
            sys.path.insert(0, packages_python_child_dir)
            sys.path.insert(
                0, os.path.join(packages_python_child_dir, "test_runner", "lib")
            )

            # We're done.
            break

        # Continue looking up the parent chain until we have no more
        # directories to check.
        new_check_dir = os.path.dirname(check_dir)
        # We're done when the new check dir is not different
        # than the current one.
        if new_check_dir == check_dir:
            break
        check_dir = new_check_dir
