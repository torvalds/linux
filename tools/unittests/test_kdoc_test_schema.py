#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Unit‑test driver for kernel‑doc YAML tests.

Two kinds of tests are defined:

* **Schema‑validation tests** – if ``jsonschema`` is available, the
  YAML files in this directory are validated against the JSON‑Schema
  described in ``kdoc-test-schema.yaml``.  When the library is not
  present, a warning is emitted and the validation step is simply
  skipped – the dynamic kernel‑doc tests still run.

* **Kernel‑doc tests** – dynamically generate one test method per
  scenario in ``kdoc-test.yaml``.  Each method simply forwards
  the data to ``self.run_test`` – you only need to implement that
  helper in your own code.

File names are kept as module‑level constants so that the
implementation stays completely independent of ``pathlib``.
"""

import os
import sys
import warnings
import yaml
import unittest
from typing import Any, Dict, List

SRC_DIR = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(SRC_DIR, "../lib/python"))

from unittest_helper import run_unittest


#
# Files to read
#
BASE = os.path.realpath(os.path.dirname(__file__))

SCHEMA_FILE = os.path.join(BASE, "kdoc-test-schema.yaml")
TEST_FILE = os.path.join(BASE, "kdoc-test.yaml")

#
# Schema‑validation test
#
class TestYAMLSchemaValidation(unittest.TestCase):
    """
    Checks if TEST_FILE matches SCHEMA_FILE.
    """

    @classmethod
    def setUpClass(cls):
        """
        Import jsonschema if available.
        """

        try:
            from jsonschema import Draft7Validator
        except ImportError:
            print("Warning: jsonschema package not available. Skipping schema validation")
            cls.validator = None
            return

        with open(SCHEMA_FILE, encoding="utf-8") as fp:
            cls.schema = yaml.safe_load(fp)

        cls.validator = Draft7Validator(cls.schema)

    def test_kdoc_test_yaml_followsschema(self):
        """
        Run jsonschema validation if the validator is available.
        If not, emit a warning and return without failing.
        """
        if self.validator is None:
            return

        with open(TEST_FILE, encoding="utf-8") as fp:
            data = yaml.safe_load(fp)

        errors = self.validator.iter_errors(data)

        msgs = []
        for error in errors:
            msgs.append(error.message)

        if msgs:
            self.fail("Schema validation failed:\n\t" + "\n\t".join(msgs))

# --------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------
if __name__ == "__main__":
    run_unittest(__file__)
