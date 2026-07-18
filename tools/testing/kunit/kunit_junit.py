# SPDX-License-Identifier: GPL-2.0
#
# Generates JUnit XML files from KUnit test results
#
# Copyright (C) 2026, Google LLC and David Gow.

from xml.sax.saxutils import quoteattr, XMLGenerator
import xml.etree.ElementTree as ET
from kunit_parser import Test, TestStatus
from typing import Optional

# Get a string representing a tes suite (including subtests) in JUnit XML
def get_test_suite(test: Test, parent: Optional[ET.Element]) -> ET.Element:
	suite_attrs = {
		'name': test.name,
		'tests': str(test.counts.total()),
		'failures': str(test.counts.failed),
		'skipped': str(test.counts.skipped),
		'errors': str(test.counts.crashed + test.counts.errors),
	}

	if parent is not None:
		test_suite_element = ET.SubElement(parent, 'testsuite', suite_attrs)
	else:
		test_suite_element = ET.Element('testsuite', suite_attrs)

	for subtest in test.subtests:
		if subtest.subtests:
			get_test_suite(subtest, test_suite_element)
			continue
		test_case_element = ET.SubElement(test_suite_element, 'testcase', {'name': subtest.name})
		if subtest.status == TestStatus.FAILURE:
			ET.SubElement(test_case_element, 'failure', {}).text = 'Test Failed'
		elif subtest.status == TestStatus.SKIPPED:
			ET.SubElement(test_case_element, 'skipped', {}).text = subtest.skip_reason
		elif subtest.status == TestStatus.TEST_CRASHED:
			ET.SubElement(test_case_element, 'error', {}).text = 'Test Crashed'

		if subtest.log:
			ET.SubElement(test_case_element, 'system-out', {}).text = "\n".join(subtest.log)

	return test_suite_element

# Get a string for an entire XML file for the test structure starting at test
def get_junit_result(test: Test) -> str:
	root_element = get_test_suite(test, None)
	ET.indent(root_element)
	return ET.tostring(root_element, encoding="unicode", xml_declaration=True)

# Print a JUnit result to stdout.
def print_junit_result(test: Test) -> None:
	root_element = get_test_suite(test, None)
	ET.indent(root_element)
	ET.dump(root_element)

# Write an entire XML file for the test structure starting at test
def write_junit_result(test: Test, filename: str) -> None:
	root_element = get_test_suite(test, None)
	ET.indent(root_element)
	root_et = ET.ElementTree(root_element)
	root_et.write(filename, encoding='utf-8', xml_declaration=True)
