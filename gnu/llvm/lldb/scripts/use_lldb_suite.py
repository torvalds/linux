import inspect
import os
import sys


def find_lldb_root():
    lldb_root = os.path.dirname(inspect.getfile(inspect.currentframe()))
    while True:
        parent = os.path.dirname(lldb_root)
        if parent == lldb_root:  # dirname('/') == '/'
            raise Exception("use_lldb_suite_root.py not found")
        lldb_root = parent

        test_path = os.path.join(lldb_root, "use_lldb_suite_root.py")
        if os.path.isfile(test_path):
            return lldb_root


lldb_root = find_lldb_root()

import importlib.machinery
import importlib.util

path = os.path.join(lldb_root, "use_lldb_suite_root.py")
loader = importlib.machinery.SourceFileLoader("use_lldb_suite_root", path)
spec = importlib.util.spec_from_loader("use_lldb_suite_root", loader=loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)
