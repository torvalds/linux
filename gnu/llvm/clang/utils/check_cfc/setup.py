"""For use on Windows. Run with:
    python.exe setup.py py2exe
    """
from __future__ import absolute_import, division, print_function
from distutils.core import setup

try:
    import py2exe
except ImportError:
    import platform
    import sys

    if platform.system() == "Windows":
        print("Could not find py2exe. Please install then run setup.py py2exe.")
        raise
    else:
        print("setup.py only required on Windows.")
        sys.exit(1)

setup(
    console=["check_cfc.py"],
    name="Check CFC",
    description="Check Compile Flow Consistency",
)
