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
from typing import Iterable, Iterator, List, Optional, Tuple

TestResult = namedtuple('TestResult', ['status','suites','log'])

class TestSuite(object):
	def __init__(self) -> None:
		self.status = TestStatus.SUCCESS
		self.name = ''
		self.cases = []  # type: List[TestCase]

	def __str__(self) -> str:
		return 'TestSuite(' + str(self.status) + ',' + self.name + ',' + str(self.cases) + ')'

	def __repr__(self) -> str:
		return str(self)

class TestCase(object):
	def __init__(self) -> None:
		self.status = TestStatus.SUCCESS
		self.name = ''
		self.log = []  # type: List[str]

	def __str__(self) -> str:
		return 'TestCase(' + str(self.status) + ',' + self.name + ',' + str(self.log) + ')'

	def __repr__(self) -> str:
		return str(self)

class TestStatus(Enum):
	SUCCESS = auto()
	FAILURE = auto()
	SKIPPED = auto()
	TEST_CRASHED = auto()
	NO_TESTS = auto()
	FAILURE_TO_PARSE_TESTS = auto()

class LineStream:
	"""Provides a peek()/pop() interface over an iterator of (line#, text)."""
	_lines: Iterator[Tuple[int, str]]
	_next: Tuple[int, str]
	_done: bool

	def __init__(self, lines: Iterator[Tuple[int, str]]):
		self._lines = lines
		self._done = False
		self._next = (0, '')
		self._get_next()

	def _get_next(self) -> None:
		try:
			self._next = next(self._lines)
		except StopIteration:
			self._done = True

	def peek(self) -> str:
		return self._next[1]

	def pop(self) -> str:
		n = self._next
		self._get_next()
		return n[1]

	def __bool__(self) -> bool:
		return not self._done

	# Only used by kunit_tool_test.py.
	def __iter__(self) -> Iterator[str]:
		while bool(self):
			yield self.pop()

	def line_number(self) -> int:
		return self._next[0]

kunit_start_re = re.compile(r'TAP version [0-9]+$')
kunit_end_re = re.compile('(List of all partitions:|'
			  'Kernel panic - not syncing: VFS:|reboot: System halted)')

def extract_tap_lines(kernel_output: Iterable[str]) -> LineStream:
	def isolate_kunit_output(kernel_output: Iterable[str]) -> Iterator[Tuple[int, str]]:
		line_num = 0
		started = False
		for line in kernel_output:
			line_num += 1
			line = line.rstrip()  # line always has a trailing \n
			if kunit_start_re.search(line):
				prefix_len = len(line.split('TAP version')[0])
				started = True
				yield line_num, line[prefix_len:]
			elif kunit_end_re.search(line):
				break
			elif started:
				yield line_num, line[prefix_len:]
	return LineStream(lines=isolate_kunit_output(kernel_output))

def raw_output(kernel_output) -> None:
	for line in kernel_output:
		print(line.rstrip())

DIVIDER = '=' * 60

RESET = '\033[0;0m'

def red(text) -> str:
	return '\033[1;31m' + text + RESET

def yellow(text) -> str:
	return '\033[1;33m' + text + RESET

def green(text) -> str:
	return '\033[1;32m' + text + RESET

def print_with_timestamp(message) -> None:
	print('[%s] %s' % (datetime.now().strftime('%H:%M:%S'), message))

def format_suite_divider(message) -> str:
	return '======== ' + message + ' ========'

def print_suite_divider(message) -> None:
	print_with_timestamp(DIVIDER)
	print_with_timestamp(format_suite_divider(message))

def print_log(log) -> None:
	for m in log:
		print_with_timestamp(m)

TAP_ENTRIES = re.compile(r'^(TAP|[\s]*ok|[\s]*not ok|[\s]*[0-9]+\.\.[0-9]+|[\s]*#).*$')

def consume_non_diagnostic(lines: LineStream) -> None:
	while lines and not TAP_ENTRIES.match(lines.peek()):
		lines.pop()

def save_non_diagnostic(lines: LineStream, test_case: TestCase) -> None:
	while lines and not TAP_ENTRIES.match(lines.peek()):
		test_case.log.append(lines.peek())
		lines.pop()

OkNotOkResult = namedtuple('OkNotOkResult', ['is_ok','description', 'text'])

OK_NOT_OK_SKIP = re.compile(r'^[\s]*(ok|not ok) [0-9]+ - (.*) # SKIP(.*)$')

OK_NOT_OK_SUBTEST = re.compile(r'^[\s]+(ok|not ok) [0-9]+ - (.*)$')

OK_NOT_OK_MODULE = re.compile(r'^(ok|not ok) ([0-9]+) - (.*)$')

def parse_ok_not_ok_test_case(lines: LineStream, test_case: TestCase) -> bool:
	save_non_diagnostic(lines, test_case)
	if not lines:
		test_case.status = TestStatus.TEST_CRASHED
		return True
	line = lines.peek()
	match = OK_NOT_OK_SUBTEST.match(line)
	while not match and lines:
		line = lines.pop()
		match = OK_NOT_OK_SUBTEST.match(line)
	if match:
		test_case.log.append(lines.pop())
		test_case.name = match.group(2)
		skip_match = OK_NOT_OK_SKIP.match(line)
		if skip_match:
			test_case.status = TestStatus.SKIPPED
			return True
		if test_case.status == TestStatus.TEST_CRASHED:
			return True
		if match.group(1) == 'ok':
			test_case.status = TestStatus.SUCCESS
		else:
			test_case.status = TestStatus.FAILURE
		return True
	else:
		return False

SUBTEST_DIAGNOSTIC = re.compile(r'^[\s]+# (.*)$')
DIAGNOSTIC_CRASH_MESSAGE = re.compile(r'^[\s]+# .*?: kunit test case crashed!$')

def parse_diagnostic(lines: LineStream, test_case: TestCase) -> bool:
	save_non_diagnostic(lines, test_case)
	if not lines:
		return False
	line = lines.peek()
	match = SUBTEST_DIAGNOSTIC.match(line)
	if match:
		test_case.log.append(lines.pop())
		crash_match = DIAGNOSTIC_CRASH_MESSAGE.match(line)
		if crash_match:
			test_case.status = TestStatus.TEST_CRASHED
		return True
	else:
		return False

def parse_test_case(lines: LineStream) -> Optional[TestCase]:
	test_case = TestCase()
	save_non_diagnostic(lines, test_case)
	while parse_diagnostic(lines, test_case):
		pass
	if parse_ok_not_ok_test_case(lines, test_case):
		return test_case
	else:
		return None

SUBTEST_HEADER = re.compile(r'^[\s]+# Subtest: (.*)$')

def parse_subtest_header(lines: LineStream) -> Optional[str]:
	consume_non_diagnostic(lines)
	if not lines:
		return None
	match = SUBTEST_HEADER.match(lines.peek())
	if match:
		lines.pop()
		return match.group(1)
	else:
		return None

SUBTEST_PLAN = re.compile(r'[\s]+[0-9]+\.\.([0-9]+)')

def parse_subtest_plan(lines: LineStream) -> Optional[int]:
	consume_non_diagnostic(lines)
	match = SUBTEST_PLAN.match(lines.peek())
	if match:
		lines.pop()
		return int(match.group(1))
	else:
		return None

def max_status(left: TestStatus, right: TestStatus) -> TestStatus:
	if left == right:
		return left
	elif left == TestStatus.TEST_CRASHED or right == TestStatus.TEST_CRASHED:
		return TestStatus.TEST_CRASHED
	elif left == TestStatus.FAILURE or right == TestStatus.FAILURE:
		return TestStatus.FAILURE
	elif left == TestStatus.SKIPPED:
		return right
	else:
		return left

def parse_ok_not_ok_test_suite(lines: LineStream,
			       test_suite: TestSuite,
			       expected_suite_index: int) -> bool:
	consume_non_diagnostic(lines)
	if not lines:
		test_suite.status = TestStatus.TEST_CRASHED
		return False
	line = lines.peek()
	match = OK_NOT_OK_MODULE.match(line)
	if match:
		lines.pop()
		if match.group(1) == 'ok':
			test_suite.status = TestStatus.SUCCESS
		else:
			test_suite.status = TestStatus.FAILURE
		skip_match = OK_NOT_OK_SKIP.match(line)
		if skip_match:
			test_suite.status = TestStatus.SKIPPED
		suite_index = int(match.group(2))
		if suite_index != expected_suite_index:
			print_with_timestamp(
				red('[ERROR] ') + 'expected_suite_index ' +
				str(expected_suite_index) + ', but got ' +
				str(suite_index))
		return True
	else:
		return False

def bubble_up_errors(status_list: Iterable[TestStatus]) -> TestStatus:
	return reduce(max_status, status_list, TestStatus.SKIPPED)

def bubble_up_test_case_errors(test_suite: TestSuite) -> TestStatus:
	max_test_case_status = bubble_up_errors(x.status for x in test_suite.cases)
	return max_status(max_test_case_status, test_suite.status)

def parse_test_suite(lines: LineStream, expected_suite_index: int) -> Optional[TestSuite]:
	if not lines:
		return None
	consume_non_diagnostic(lines)
	test_suite = TestSuite()
	test_suite.status = TestStatus.SUCCESS
	name = parse_subtest_header(lines)
	if not name:
		return None
	test_suite.name = name
	expected_test_case_num = parse_subtest_plan(lines)
	if expected_test_case_num is None:
		return None
	while expected_test_case_num > 0:
		test_case = parse_test_case(lines)
		if not test_case:
			break
		test_suite.cases.append(test_case)
		expected_test_case_num -= 1
	if parse_ok_not_ok_test_suite(lines, test_suite, expected_suite_index):
		test_suite.status = bubble_up_test_case_errors(test_suite)
		return test_suite
	elif not lines:
		print_with_timestamp(red('[ERROR] ') + 'ran out of lines before end token')
		return test_suite
	else:
		print(f'failed to parse end of suite "{name}", at line {lines.line_number()}: {lines.peek()}')
		return None

TAP_HEADER = re.compile(r'^TAP version 14$')

def parse_tap_header(lines: LineStream) -> bool:
	consume_non_diagnostic(lines)
	if TAP_HEADER.match(lines.peek()):
		lines.pop()
		return True
	else:
		return False

TEST_PLAN = re.compile(r'[0-9]+\.\.([0-9]+)')

def parse_test_plan(lines: LineStream) -> Optional[int]:
	consume_non_diagnostic(lines)
	match = TEST_PLAN.match(lines.peek())
	if match:
		lines.pop()
		return int(match.group(1))
	else:
		return None

def bubble_up_suite_errors(test_suites: Iterable[TestSuite]) -> TestStatus:
	return bubble_up_errors(x.status for x in test_suites)

def parse_test_result(lines: LineStream) -> TestResult:
	consume_non_diagnostic(lines)
	if not lines or not parse_tap_header(lines):
		return TestResult(TestStatus.NO_TESTS, [], lines)
	expected_test_suite_num = parse_test_plan(lines)
	if not expected_test_suite_num:
		return TestResult(TestStatus.FAILURE_TO_PARSE_TESTS, [], lines)
	test_suites = []
	for i in range(1, expected_test_suite_num + 1):
		test_suite = parse_test_suite(lines, i)
		if test_suite:
			test_suites.append(test_suite)
		else:
			print_with_timestamp(
				red('[ERROR] ') + ' expected ' +
				str(expected_test_suite_num) +
				' test suites, but got ' + str(i - 2))
			break
	test_suite = parse_test_suite(lines, -1)
	if test_suite:
		print_with_timestamp(red('[ERROR] ') +
			'got unexpected test suite: ' + test_suite.name)
	if test_suites:
		return TestResult(bubble_up_suite_errors(test_suites), test_suites, lines)
	else:
		return TestResult(TestStatus.NO_TESTS, [], lines)

class TestCounts:
	passed: int
	failed: int
	crashed: int
	skipped: int

	def __init__(self):
		self.passed = 0
		self.failed = 0
		self.crashed = 0
		self.skipped = 0

	def total(self) -> int:
		return self.passed + self.failed + self.crashed + self.skipped

def print_and_count_results(test_result: TestResult) -> TestCounts:
	counts = TestCounts()
	for test_suite in test_result.suites:
		if test_suite.status == TestStatus.SUCCESS:
			print_suite_divider(green('[PASSED] ') + test_suite.name)
		elif test_suite.status == TestStatus.SKIPPED:
			print_suite_divider(yellow('[SKIPPED] ') + test_suite.name)
		elif test_suite.status == TestStatus.TEST_CRASHED:
			print_suite_divider(red('[CRASHED] ' + test_suite.name))
		else:
			print_suite_divider(red('[FAILED] ') + test_suite.name)
		for test_case in test_suite.cases:
			if test_case.status == TestStatus.SUCCESS:
				counts.passed += 1
				print_with_timestamp(green('[PASSED] ') + test_case.name)
			elif test_case.status == TestStatus.SKIPPED:
				counts.skipped += 1
				print_with_timestamp(yellow('[SKIPPED] ') + test_case.name)
			elif test_case.status == TestStatus.TEST_CRASHED:
				counts.crashed += 1
				print_with_timestamp(red('[CRASHED] ' + test_case.name))
				print_log(map(yellow, test_case.log))
				print_with_timestamp('')
			else:
				counts.failed += 1
				print_with_timestamp(red('[FAILED] ') + test_case.name)
				print_log(map(yellow, test_case.log))
				print_with_timestamp('')
	return counts

def parse_run_tests(kernel_output: Iterable[str]) -> TestResult:
	counts = TestCounts()
	lines = extract_tap_lines(kernel_output)
	test_result = parse_test_result(lines)
	if test_result.status == TestStatus.NO_TESTS:
		print(red('[ERROR] ') + yellow('no tests run!'))
	elif test_result.status == TestStatus.FAILURE_TO_PARSE_TESTS:
		print(red('[ERROR] ') + yellow('could not parse test results!'))
	else:
		counts = print_and_count_results(test_result)
	print_with_timestamp(DIVIDER)
	if test_result.status == TestStatus.SUCCESS:
		fmt = green
	elif test_result.status == TestStatus.SKIPPED:
		fmt = yellow
	else:
		fmt =red
	print_with_timestamp(
		fmt('Testing complete. %d tests run. %d failed. %d crashed. %d skipped.' %
		    (counts.total(), counts.failed, counts.crashed, counts.skipped)))
	return test_result
