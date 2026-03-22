#!/bin/env python3
import os
import unittest
import sys

TOOLS_DIR=os.path.join(os.path.dirname(os.path.realpath(__file__)), "..")
sys.path.insert(0, TOOLS_DIR)

from lib.python.unittest_helper import TestUnits

if __name__ == "__main__":
    loader = unittest.TestLoader()

    suite = loader.discover(start_dir=os.path.join(TOOLS_DIR, "unittests"),
                            pattern="test*.py")

    TestUnits().run("", suite=suite)
