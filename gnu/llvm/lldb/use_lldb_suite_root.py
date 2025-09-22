import inspect
import os
import sys


def add_lldbsuite_packages_dir(lldb_root):
    packages_dir = os.path.join(lldb_root, "packages", "Python")
    sys.path.insert(0, packages_dir)


lldb_root = os.path.dirname(inspect.getfile(inspect.currentframe()))

add_lldbsuite_packages_dir(lldb_root)
