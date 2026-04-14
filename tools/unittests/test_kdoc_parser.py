#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2026: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=C0200,C0413,W0102,R0914

"""
Unit tests for kernel-doc parser.
"""

import logging
import os
import re
import shlex
import sys
import unittest

from textwrap import dedent
from unittest.mock import patch, MagicMock, mock_open

import yaml

SRC_DIR = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(SRC_DIR, "../lib/python"))

from kdoc.kdoc_files import KdocConfig
from kdoc.kdoc_item import KdocItem
from kdoc.kdoc_parser import KernelDoc
from kdoc.kdoc_output import RestFormat, ManFormat

from kdoc.xforms_lists import CTransforms

from unittest_helper import TestUnits


#
# Test file
#
TEST_FILE = os.path.join(SRC_DIR, "kdoc-test.yaml")

env = {
    "yaml_file": TEST_FILE
}

#
# Ancillary logic to clean whitespaces
#
#: Regex to help cleaning whitespaces
RE_WHITESPC = re.compile(r"[ \t]++")
RE_BEGINSPC = re.compile(r"^\s+", re.MULTILINE)
RE_ENDSPC = re.compile(r"\s+$", re.MULTILINE)

def clean_whitespc(val, relax_whitespace=False):
    """
    Cleanup whitespaces to avoid false positives.

    By default, strip only bein/end whitespaces, but, when relax_whitespace
    is true, also replace multiple whitespaces in the middle.
    """

    if isinstance(val, str):
        val = val.strip()
        if relax_whitespace:
            val = RE_WHITESPC.sub(" ", val)
            val = RE_BEGINSPC.sub("", val)
            val = RE_ENDSPC.sub("", val)
    elif isinstance(val, list):
        val = [clean_whitespc(item, relax_whitespace) for item in val]
    elif isinstance(val, dict):
        val = {k: clean_whitespc(v, relax_whitespace) for k, v in val.items()}
    return val

#
# Helper classes to help mocking with logger and config
#
class MockLogging(logging.Handler):
    """
    Simple class to store everything on a list
    """

    def __init__(self, level=logging.NOTSET):
        super().__init__(level)
        self.messages = []
        self.formatter = logging.Formatter()

    def emit(self, record: logging.LogRecord) -> None:
        """
        Append a formatted record to self.messages.
        """
        try:
            # The `format` method uses the handler's formatter.
            message = self.format(record)
            self.messages.append(message)
        except Exception:
            self.handleError(record)

class MockKdocConfig(KdocConfig):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.log = logging.getLogger(__file__)
        self.handler = MockLogging()
        self.log.addHandler(self.handler)

    def warning(self, msg):
        """Ancillary routine to output a warning and increment error count."""

        self.log.warning(msg)

#
# Helper class to generate KdocItem and validate its contents
#
# TODO: check self.config.handler.messages content
#
class GenerateKdocItem(unittest.TestCase):
    """
    Base class to run KernelDoc parser class
    """

    DEFAULT = vars(KdocItem("", "", "", 0))

    config = MockKdocConfig()
    xforms = CTransforms()

    def setUp(self):
        self.maxDiff = None

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
            if not isinstance(e, dict):
                e = vars(e)

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

                other_stuff = d.get("other_stuff", {})
                if "source" in other_stuff:
                    del other_stuff["source"]

                for key, value in expected.items():
                    if key == "other_stuff":
                        if "source" in value:
                            del value["source"]

                    result = clean_whitespc(d[key], relax_whitespace)
                    value = clean_whitespc(value, relax_whitespace)

                    if debug_level > 1:
                        sys.stderr.write(f"{key}: assert('{result}' == '{value}')\n")

                    self.assertEqual(result, value, msg=f"at {key}")

#
# Ancillary function that replicates kdoc_files way to generate output
#
def cleanup_timestamp(text):
    lines = text.split("\n")

    for i, line in enumerate(lines):
        if not line.startswith('.TH'):
            continue

        parts = shlex.split(line)
        if len(parts) > 3:
            parts[3] = ""

        lines[i] = " ".join(parts)


    return "\n".join(lines)

def gen_output(fname, out_style, symbols, expected,
               config=None, relax_whitespace=False):
    """
    Use the output class to return an output content from KdocItem symbols.
    """

    if not config:
        config = MockKdocConfig()

    out_style.set_config(config)

    msg = out_style.output_symbols(fname, symbols)

    result = clean_whitespc(msg, relax_whitespace)
    result = cleanup_timestamp(result)

    expected = clean_whitespc(expected, relax_whitespace)
    expected = cleanup_timestamp(expected)

    return result, expected

#
# Classes to be used by dynamic test generation from YAML
#
class CToKdocItem(GenerateKdocItem):
    def setUp(self):
        self.maxDiff = None

    def run_parser_test(self, source, symbols, exports, fname):
        if isinstance(symbols, dict):
            symbols = [symbols]

        if isinstance(exports, str):
            exports=set([exports])
        elif isinstance(exports, list):
            exports=set(exports)

        self.run_test(source, symbols, exports=exports,
                      fname=fname, relax_whitespace=True)

class KdocItemToMan(unittest.TestCase):
    out_style = ManFormat()

    def setUp(self):
        self.maxDiff = None

    def run_out_test(self, fname, symbols, expected):
        """
        Generate output using out_style,
        """
        result, expected = gen_output(fname, self.out_style,
                                      symbols, expected)

        self.assertEqual(result, expected)

class KdocItemToRest(unittest.TestCase):
    out_style = RestFormat()

    def setUp(self):
        self.maxDiff = None

    def run_out_test(self, fname, symbols, expected):
        """
        Generate output using out_style,
        """
        result, expected = gen_output(fname, self.out_style, symbols,
                                      expected, relax_whitespace=True)

        self.assertEqual(result, expected)


class CToMan(unittest.TestCase):
    out_style = ManFormat()
    config = MockKdocConfig()
    xforms = CTransforms()

    def setUp(self):
        self.maxDiff = None

    def run_out_test(self, fname, source, expected):
        """
        Generate output using out_style,
        """
        patcher = patch('builtins.open',
                        new_callable=mock_open, read_data=source)

        kernel_doc = KernelDoc(self.config, fname, self.xforms)

        with patcher:
            export_table, entries = kernel_doc.parse_kdoc()

        result, expected = gen_output(fname, self.out_style,
                                      entries, expected, config=self.config)

        self.assertEqual(result, expected)


class CToRest(unittest.TestCase):
    out_style = RestFormat()
    config = MockKdocConfig()
    xforms = CTransforms()

    def setUp(self):
        self.maxDiff = None

    def run_out_test(self, fname, source, expected):
        """
        Generate output using out_style,
        """
        patcher = patch('builtins.open',
                        new_callable=mock_open, read_data=source)

        kernel_doc = KernelDoc(self.config, fname, self.xforms)

        with patcher:
            export_table, entries = kernel_doc.parse_kdoc()

        result, expected = gen_output(fname, self.out_style, entries,
                                      expected, relax_whitespace=True,
                                      config=self.config)

        self.assertEqual(result, expected)


#
# Selftest class
#
class TestSelfValidate(GenerateKdocItem):
    """
    Tests to check if logic inside GenerateKdocItem.run_test() is working.
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

        'sections_start_lines': {
            'Description': 4,
            'Return': 7,
        },

        'parameterdescs': {'arg1': '@arg1 does nothing\n'},
        'parameterlist': ['arg1'],
        'parameterdesc_start_lines': {'arg1': 3},
        'parametertypes': {'arg1': 'char *arg1'},

        'other_stuff': {
            'func_macro': False,
            'functiontype': 'int',
            'purpose': 'Exported function',
            'typedef': False
        },
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
# Class and logic to create dynamic tests from YAML
#

class KernelDocDynamicTests():
    """
    Dynamically create a set of tests from a YAML file.
    """

    @classmethod
    def create_parser_test(cls, name, fname, source, symbols, exports):
        """
        Return a function that will be attached to the test class.
        """
        def test_method(self):
            """Lambda-like function to run tests with provided vars"""
            self.run_parser_test(source, symbols, exports, fname)

        test_method.__name__ = f"test_gen_{name}"

        setattr(CToKdocItem, test_method.__name__, test_method)

    @classmethod
    def create_out_test(cls, name, fname, symbols, out_type, data):
        """
        Return a function that will be attached to the test class.
        """
        def test_method(self):
            """Lambda-like function to run tests with provided vars"""
            self.run_out_test(fname, symbols, data)

        test_method.__name__ = f"test_{out_type}_{name}"

        if out_type == "man":
            setattr(KdocItemToMan, test_method.__name__, test_method)
        else:
            setattr(KdocItemToRest, test_method.__name__, test_method)

    @classmethod
    def create_src2out_test(cls, name, fname, source, out_type, data):
        """
        Return a function that will be attached to the test class.
        """
        def test_method(self):
            """Lambda-like function to run tests with provided vars"""
            self.run_out_test(fname, source,  data)

        test_method.__name__ = f"test_{out_type}_{name}"

        if out_type == "man":
            setattr(CToMan, test_method.__name__, test_method)
        else:
            setattr(CToRest, test_method.__name__, test_method)

    @classmethod
    def create_tests(cls):
        """
        Iterate over all scenarios and add a method to the class for each.

        The logic in this function assumes a valid test that are compliant
        with kdoc-test-schema.yaml. There is an unit test to check that.
        As such, it picks mandatory values directly, and uses get() for the
        optional ones.
        """

        test_file = os.environ.get("yaml_file", TEST_FILE)

        with open(test_file, encoding="utf-8") as fp:
            testset = yaml.safe_load(fp)

        tests = testset["tests"]

        for idx, test in enumerate(tests):
            name = test["name"]
            fname = test["fname"]
            source = test["source"]
            expected_list = test["expected"]

            exports = test.get("exports", [])

            #
            # The logic below allows setting up to 5 types of test:
            # 1. from source to kdoc_item: test KernelDoc class;
            # 2. from kdoc_item to man: test ManOutput class;
            # 3. from kdoc_item to rst: test RestOutput class;
            # 4. from source to man without checking expected KdocItem;
            # 5. from source to rst without checking expected KdocItem.
            #
            for expected in expected_list:
                kdoc_item = expected.get("kdoc_item")
                man = expected.get("man", [])
                rst = expected.get("rst", [])

                if kdoc_item:
                    if isinstance(kdoc_item, dict):
                        kdoc_item = [kdoc_item]

                    symbols = []

                    for arg in kdoc_item:
                        arg["fname"] = fname
                        arg["start_line"] = 1

                        symbols.append(KdocItem.from_dict(arg))

                    if source:
                        cls.create_parser_test(name, fname, source,
                                               symbols, exports)

                    if man:
                        cls.create_out_test(name, fname, symbols, "man", man)

                    if rst:
                        cls.create_out_test(name, fname, symbols, "rst", rst)

                elif source:
                    if man:
                        cls.create_src2out_test(name, fname, source, "man", man)

                    if rst:
                        cls.create_src2out_test(name, fname, source, "rst", rst)

KernelDocDynamicTests.create_tests()

#
# Run all tests
#
if __name__ == "__main__":
    runner = TestUnits()
    parser = runner.parse_args()
    parser.add_argument("-y", "--yaml-file", "--yaml",
                        help='Name of the yaml file to load')

    args = parser.parse_args()

    if args.yaml_file:
        env["yaml_file"] = os.path.expanduser(args.yaml_file)

    # Run tests with customized arguments
    runner.run(__file__, parser=parser, args=args, env=env)
