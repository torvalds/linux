#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2026: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=C0413,R0904


"""
Unit tests for kernel-doc CMatch.
"""

import os
import re
import sys
import unittest


# Import Python modules

SRC_DIR = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(SRC_DIR, "../lib/python"))

from kdoc.c_lex import CMatch
from kdoc.xforms_lists import CTransforms
from unittest_helper import run_unittest

#
# Override unittest.TestCase to better compare diffs ignoring whitespaces
#
class TestCaseDiff(unittest.TestCase):
    """
    Disable maximum limit on diffs and add a method to better
    handle diffs with whitespace differences.
    """

    @classmethod
    def setUpClass(cls):
        """Ensure that there won't be limit for diffs"""
        cls.maxDiff = None


#
# Tests doing with different macros
#

class TestSearch(TestCaseDiff):
    """
    Test search mechanism
    """

    def test_search_acquires_simple(self):
        line = "__acquires(ctx) foo();"
        result = ", ".join(CMatch("__acquires").search(line))
        self.assertEqual(result, "__acquires(ctx)")

    def test_search_acquires_multiple(self):
        line = "__acquires(ctx) __acquires(other) bar();"
        result = ", ".join(CMatch("__acquires").search(line))
        self.assertEqual(result, "__acquires(ctx), __acquires(other)")

    def test_search_acquires_nested_paren(self):
        line = "__acquires((ctx1, ctx2)) baz();"
        result = ", ".join(CMatch("__acquires").search(line))
        self.assertEqual(result, "__acquires((ctx1, ctx2))")

    def test_search_must_hold(self):
        line = "__must_hold(&lock) do_something();"
        result = ", ".join(CMatch("__must_hold").search(line))
        self.assertEqual(result, "__must_hold(&lock)")

    def test_search_must_hold_shared(self):
        line = "__must_hold_shared(RCU) other();"
        result = ", ".join(CMatch("__must_hold_shared").search(line))
        self.assertEqual(result, "__must_hold_shared(RCU)")

    def test_search_no_false_positive(self):
        line = "call__acquires(foo);  // should stay intact"
        result = ", ".join(CMatch(r"\b__acquires").search(line))
        self.assertEqual(result, "")

    def test_search_no_macro_remains(self):
        line = "do_something_else();"
        result = ", ".join(CMatch("__acquires").search(line))
        self.assertEqual(result, "")

    def test_search_no_function(self):
        line = "something"
        result = ", ".join(CMatch(line).search(line))
        self.assertEqual(result, "")

#
# Run all tests
#
if __name__ == "__main__":
    run_unittest(__file__)
