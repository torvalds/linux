# SPDX-License-Identifier: GPL-2.0
#
# Parses test results from a kernel dmesg log.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>

import re

from collections import namedtuple
from datetime import datetime
from enum import Enum, auto
from functools import reduce
from typing import List

TestResult = namedtuple('TestResult', ['status','suites','log'])

class TestSuite(object):
	def __init__(self):
		self.status = None
		self.name = None
		self.cases = []

	def __str__(self):
		return 'TestSuite(' + self.status + ',' + self.name + ',' + str(self.cases) + ')'

	def __repr__(self):
		return str(self)

class TestCase(object):
	def __init__(self):
		self.status = None
		self.name = ''
		self.log = []

	def __str__(self):
		return 'TestCase(' + self.status + ',' + self.name + ',' + str(self.log) + ')'

	def __repr__(self):
		return str(self)

class TestStatus(Enum):
	SUCCESS = auto()
	FAILURE = auto()
	TEST_CRASHED = auto()
	NO_TESTS = auto()

kunit_start_re = re.compile(r'TAP version [0-9]+$')
kunit_end_re = re.compile('(List of all partitions:|'
			  'Kernel panic - not syncing: VFS:|reboot: System halted)')

def isolate_kunit_output(kernel_output):
	started = False
	for line in kernel_output:
		if kunit_start_re.search(line):
			prefix_len = len(line.split('TAP version')[0])
			started = True
			yield line[prefix_len:] if prefix_len > 0 else line
		elif kunit_end_re.search(line):
			break
		elif started:
			yield line[prefix_len:] if prefix_len > 0 else line

def raw_output(kernel_output):
	for line in kernel_output:
		print(line)
		yield line

DIVIDER = '=' * 60

RESET = '\033[0;0m'

def red(text):
	return '\033[1;31m' + text + RESET

def yellow(text):
	return '\033[1;33m' + text + RESET

def green(text):
	return '\033[1;32m' + text + RESET

def print_with_timestamp(message):
	print('[%s] %s' % (datetime.now().strftime('%H:%M:%S'), message))

def format_suite_divider(message):
	return '======== ' + message + ' ========'

def print_suite_divider(message):
	print_with_timestamp(DIVIDER)
	print_with_timestamp(format_suite_divider(message))

def print_log(log):
	for m in log:
		print_with_timestamp(m)

TAP_ENTRIES = re.compile(r'^(TAP|[\s]*ok|[\s]*not ok|[\s]*[0-9]+\.\.[0-9]+|[\s]*#).*$')

def consume_non_diagnositic(lines: List[str]) -> None:
	while lines and not TAP_ENTRIES.match(lines[0]):
		lines.pop(0)

def save_non_diagnositic(lines: List[str], test_case: TestCase) -> None:
	while lines and not TAP_ENTRIES.match(lines[0]):
		test_case.log.append(lines[0])
		lines.pop(0)

OkNotOkResult = namedtuple('OkNotOkResult', ['is_ok','description', 'text'])

OK_NOT_OK_SUBTEST = re.compile(r'^[\s]+(ok|not ok) [0-9]+ - (.*)$')

OK_NOT_OK_MODULE = re.compile(r'^(ok|not ok) [0-9]+ - (.*)$')

def parse_ok_not_ok_test_case(lines: List[str], test_case: TestCase) -> bool:
	save_non_diagnositic(lines, test_case)
	if not lines:
		test_case.status = TestStatus.TEST_CRASHED
		return True
	line = lines[0]
	match = OK_NOT_OK_SUBTEST.match(line)
	while not match and lines:
		line = lines.pop(0)
		match = OK_NOT_OK_SUBTEST.match(line)
	if match:
		test_case.log.append(lines.pop(0))
		test_case.name = match.group(2)
		if test_case.status == TestStatus.TEST_CRASHED:
			return True
		if match.group(1) == 'ok':
			test_case.status = TestStatus.SUCCESS
		else:
			test_case.status = TestStatus.FAILURE
		return True
	else:
		return False

SUBTEST_DIAGNOSTIC = re.compile(r'^[\s]+# .*?: (.*)$')
DIAGNOSTIC_CRASH_MESSAGE = 'kunit test case crashed!'

def parse_diagnostic(lines: List[str], test_case: TestCase) -> bool:
	save_non_diagnositic(lines, test_case)
	if not lines:
		return False
	line = lines[0]
	match = SUBTEST_DIAGNOSTIC.match(line)
	if match:
		test_case.log.append(lines.pop(0))
		if match.group(1) == DIAGNOSTIC_CRASH_MESSAGE:
			test_case.status = TestStatus.TEST_CRASHED
		return True
	else:
		return False

def parse_test_case(lines: List[str]) -> TestCase:
	test_case = TestCase()
	save_non_diagnositic(lines, test_case)
	while parse_diagnostic(lines, test_case):
		pass
	if parse_ok_not_ok_test_case(lines, test_case):
		return test_case
	else:
		return None

SUBTEST_HEADER = re.compile(r'^[\s]+# Subtest: (.*)$')

def parse_subtest_header(lines: List[str]) -> str:
	consume_non_diagnositic(lines)
	if not lines:
		return None
	match = SUBTEST_HEADER.match(lines[0])
	if match:
		lines.pop(0)
		return match.group(1)
	else:
		return None

SUBTEST_PLAN = re.compile(r'[\s]+[0-9]+\.\.([0-9]+)')

def parse_subtest_plan(lines: List[str]) -> int:
	consume_non_diagnositic(lines)
	match = SUBTEST_PLAN.match(lines[0])
	if match:
		lines.pop(0)
		return int(match.group(1))
	else:
		return None

def max_status(left: TestStatus, right: TestStatus) -> TestStatus:
	if left == TestStatus.TEST_CRASHED or right == TestStatus.TEST_CRASHED:
		return TestStatus.TEST_CRASHED
	elif left == TestStatus.FAILURE or right == TestStatus.FAILURE:
		return TestStatus.FAILURE
	elif left != TestStatus.SUCCESS:
		return left
	elif right != TestStatus.SUCCESS:
		return right
	else:
		return TestStatus.SUCCESS

def parse_ok_not_ok_test_suite(lines: List[str], test_suite: TestSuite) -> bool:
	consume_non_diagnositic(lines)
	if not lines:
		test_suite.status = TestStatus.TEST_CRASHED
		return False
	line = lines[0]
	match = OK_NOT_OK_MODULE.match(line)
	if match:
		lines.pop(0)
		if match.group(1) == 'ok':
			test_suite.status = TestStatus.SUCCESS
		else:
			test_suite.status = TestStatus.FAILURE
		return True
	else:
		return False

def bubble_up_errors(to_status, status_container_list) -> TestStatus:
	status_list = map(to_status, status_container_list)
	return reduce(max_status, status_list, TestStatus.SUCCESS)

def bubble_up_test_case_errors(test_suite: TestSuite) -> TestStatus:
	max_test_case_status = bubble_up_errors(lambda x: x.status, test_suite.cases)
	return max_status(max_test_case_status, test_suite.status)

def parse_test_suite(lines: List[str]) -> TestSuite:
	if not lines:
		return None
	consume_non_diagnositic(lines)
	test_suite = TestSuite()
	test_suite.status = TestStatus.SUCCESS
	name = parse_subtest_header(lines)
	if not name:
		return None
	test_suite.name = name
	expected_test_case_num = parse_subtest_plan(lines)
	if not expected_test_case_num:
		return None
	while expected_test_case_num > 0:
		test_case = parse_test_case(lines)
		if not test_case:
			break
		test_suite.cases.append(test_case)
		expected_test_case_num -= 1
	if parse_ok_not_ok_test_suite(lines, test_suite):
		test_suite.status = bubble_up_test_case_errors(test_suite)
		return test_suite
	elif not lines:
		print_with_timestamp(red('[ERROR] ') + 'ran out of lines before end token')
		return test_suite
	else:
		print('failed to parse end of suite' + lines[0])
		return None

TAP_HEADER = re.compile(r'^TAP version 14$')

def parse_tap_header(lines: List[str]) -> bool:
	consume_non_diagnositic(lines)
	if TAP_HEADER.match(lines[0]):
		lines.pop(0)
		return True
	else:
		return False

def bubble_up_suite_errors(test_suite_list: List[TestSuite]) -> TestStatus:
	return bubble_up_errors(lambda x: x.status, test_suite_list)

def parse_test_result(lines: List[str]) -> TestResult:
	consume_non_diagnositic(lines)
	if not lines or not parse_tap_header(lines):
		return TestResult(TestStatus.NO_TESTS, [], lines)
	test_suites = []
	test_suite = parse_test_suite(lines)
	while test_suite:
		test_suites.append(test_suite)
		test_suite = parse_test_suite(lines)
	return TestResult(bubble_up_suite_errors(test_suites), test_suites, lines)

def parse_run_tests(kernel_output) -> TestResult:
	total_tests = 0
	failed_tests = 0
	crashed_tests = 0
	test_result = parse_test_result(list(isolate_kunit_output(kernel_output)))
	if test_result.status == TestStatus.NO_TESTS:
		print_with_timestamp(red('[ERROR] ') + 'no kunit output detected')
	for test_suite in test_result.suites:
		if test_suite.status == TestStatus.SUCCESS:
			print_suite_divider(green('[PASSED] ') + test_suite.name)
		elif test_suite.status == TestStatus.TEST_CRASHED:
			print_suite_divider(red('[CRASHED] ' + test_suite.name))
		else:
			print_suite_divider(red('[FAILED] ') + test_suite.name)
		for test_case in test_suite.cases:
			total_tests += 1
			if test_case.status == TestStatus.SUCCESS:
				print_with_timestamp(green('[PASSED] ') + test_case.name)
			elif test_case.status == TestStatus.TEST_CRASHED:
				crashed_tests += 1
				print_with_timestamp(red('[CRASHED] ' + test_case.name))
				print_log(map(yellow, test_case.log))
				print_with_timestamp('')
			else:
				failed_tests += 1
				print_with_timestamp(red('[FAILED] ') + test_case.name)
				print_log(map(yellow, test_case.log))
				print_with_timestamp('')
	print_with_timestamp(DIVIDER)
	fmt = green if test_result.status == TestStatus.SUCCESS else red
	print_with_timestamp(
		fmt('Testing complete. %d tests run. %d failed. %d crashed.' %
		    (total_tests, failed_tests, crashed_tests)))
	return test_result
