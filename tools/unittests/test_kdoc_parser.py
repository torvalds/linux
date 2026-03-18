#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2026: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=C0200,C0413,W0102,R0914

"""
Unit tests for kernel-doc parser.
"""

import os
import unittest
import re
import sys

from textwrap import dedent
from unittest.mock import patch, MagicMock, mock_open

SRC_DIR = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(SRC_DIR, "../lib/python"))

from kdoc.kdoc_parser import KernelDoc
from kdoc.kdoc_item import KdocItem
from kdoc.xforms_lists import CTransforms
from unittest_helper import run_unittest

#: Regex to help cleaning whitespaces
RE_WHITESPC = re.compile(r"\s++")

def clean_whitespc(val, relax_whitespace=False):
    """
    Cleanup whitespaces to avoid false positives.

    By default, strip only bein/end whitespaces, but, when relax_whitespace
    is true, also replace multiple whitespaces in the middle.
    """

    if isinstance(val, str):
        val = val.strip()
        if relax_whitespace:
            val = RE_WHITESPC.sub("", val)
    elif isinstance(val, list):
        val = [clean_whitespc(item, relax_whitespace) for item in val]
    elif isinstance(val, dict):
        val = {k: clean_whitespc(v, relax_whitespace) for k, v in val.items()}
    return val

#
# Helper class to help mocking with
#
class KdocParser(unittest.TestCase):
    """
    Base class to run KernelDoc parser class
    """

    DEFAULT = vars(KdocItem("", "", "", 0))

    def setUp(self):
        self.maxDiff = None
        self.config = MagicMock()
        self.config.log = MagicMock()
        self.config.log.debug = MagicMock()
        self.xforms = CTransforms()


    def run_test(self, source, __expected_list, exports={}, fname="test.c",
                 relax_whitespace=False):
        """
        Stores expected values and patch the test to use source as
        a "file" input.
        """
        debug_level = int(os.getenv("VERBOSE", "0"))
        source = dedent(source)

        # Ensure that default values will be there
        expected_list = []
        for e in __expected_list:
            new_e = self.DEFAULT.copy()
            new_e["fname"] = fname
            for key, value in e.items():
                new_e[key] = value

            expected_list.append(new_e)

        patcher = patch('builtins.open',
                        new_callable=mock_open, read_data=source)

        kernel_doc = KernelDoc(self.config, fname, self.xforms)

        with patcher:
            export_table, entries = kernel_doc.parse_kdoc()

            self.assertEqual(export_table, exports)
            self.assertEqual(len(entries), len(expected_list))

            for i in range(0, len(entries)):

                entry = entries[i]
                expected = expected_list[i]
                self.assertNotEqual(expected, None)
                self.assertNotEqual(expected, {})
                self.assertIsInstance(entry, KdocItem)

                d = vars(entry)
                for key, value in expected.items():
                    result = clean_whitespc(d[key], relax_whitespace)
                    value = clean_whitespc(value, relax_whitespace)

                    if debug_level > 1:
                        sys.stderr.write(f"{key}: assert('{result}' == '{value}')\n")

                    self.assertEqual(result, value, msg=f"at {key}")


#
# Selttest class
#
class TestSelfValidate(KdocParser):
    """
    Tests to check if logic inside KdocParser.run_test() is working.
    """

    SOURCE = """
        /**
         * function3: Exported function
         * @arg1: @arg1 does nothing
         *
         * Does nothing
         *
         * return:
         *    always return 0.
         */
        int function3(char *arg1) { return 0; };
        EXPORT_SYMBOL(function3);
    """

    EXPECTED = [{
        'name': 'function3',
        'type': 'function',
        'declaration_start_line': 2,

        'sections_start_lines': {
            'Description': 4,
            'Return': 7,
        },
        'sections': {
            'Description': 'Does nothing\n\n',
            'Return': '\nalways return 0.\n'
        },
        'other_stuff': {
            'func_macro': False,
            'functiontype': 'int',
            'purpose': 'Exported function',
            'typedef': False
        },
        'parameterdescs': {'arg1': '@arg1 does nothing\n'},
        'parameterlist': ['arg1'],
        'parameterdesc_start_lines': {'arg1': 3},
        'parametertypes': {'arg1': 'char *arg1'},
    }]

    EXPORTS = {"function3"}

    def test_parse_pass(self):
        """
        Test if export_symbol is properly handled.
        """
        self.run_test(self.SOURCE, self.EXPECTED, self.EXPORTS)

    @unittest.expectedFailure
    def test_no_exports(self):
        """
        Test if export_symbol is properly handled.
        """
        self.run_test(self.SOURCE, [], {})

    @unittest.expectedFailure
    def test_with_empty_expected(self):
        """
        Test if export_symbol is properly handled.
        """
        self.run_test(self.SOURCE, [], self.EXPORTS)

    @unittest.expectedFailure
    def test_with_unfilled_expected(self):
        """
        Test if export_symbol is properly handled.
        """
        self.run_test(self.SOURCE, [{}], self.EXPORTS)

    @unittest.expectedFailure
    def test_with_default_expected(self):
        """
        Test if export_symbol is properly handled.
        """
        self.run_test(self.SOURCE, [self.DEFAULT.copy()], self.EXPORTS)

#
# Run all tests
#
if __name__ == "__main__":
    run_unittest(__file__)
