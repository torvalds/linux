# Module level initialization for the `lldbsuite` module.

import inspect
import os
import sys


def find_lldb_root():
    lldb_root = os.path.realpath(
        os.path.dirname(inspect.getfile(inspect.currentframe()))
    )
    while True:
        parent = os.path.dirname(lldb_root)
        if parent == lldb_root:  # dirname('/') == '/'
            raise Exception("use_lldb_suite_root.py not found")
        lldb_root = parent

        test_path = os.path.join(lldb_root, "use_lldb_suite_root.py")
        if os.path.isfile(test_path):
            return lldb_root


# lldbsuite.lldb_root refers to the root of the git/svn source checkout
lldb_root = find_lldb_root()

# lldbsuite.lldb_test_src_root refers to the root of the python test case tree
# (i.e. the actual unit tests).
lldb_test_root = os.path.join(lldb_root, "test", "API")
