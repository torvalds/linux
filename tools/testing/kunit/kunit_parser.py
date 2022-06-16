# SPDX-License-Identifier: GPL-2.0
#
# Parses KTAP test results from a kernel dmesg log and incrementally prints
# results with reader-friendly format. Stores and returns test results in a
# Test object.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>
# Author: Rae Moar <rmoar@google.com>

from __future__ import annotations
import re
import sys

import datetime
from enum import Enum, auto
from typing import Iterable, Iterator, List, Optional, Tuple

class Test:
	"""
	A class to represent a test parsed from KTAP results. All KTAP
	results within a test log are stored in a main Test object as
	subtests.

	Attributes:
	status : TestStatus - status of the test
	name : str - name of the test
	expected_count : int - expected number of subtests (0 if single
		test case and None if unknown expected number of subtests)
	subtests : List[Test] - list of subtests
	log : List[str] - log of KTAP lines that correspond to the test
	counts : TestCounts - counts of the test statuses and errors of
		subtests or of the test itself if the test is a single
		test case.
	"""
	def __init__(self) -> None:
		"""Creates Test object with default attributes."""
		self.status = TestStatus.TEST_CRASHED
		self.name = ''
		self.expected_count = 0  # type: Optional[int]
		self.subtests = []  # type: List[Test]
		self.log = []  # type: List[str]
		self.counts = TestCounts()

	def __str__(self) -> str:
		"""Returns string representation of a Test class object."""
		return (f'Test({self.status}, {self.name}, {self.expected_count}, '
			f'{self.subtests}, {self.log}, {self.counts})')

	def __repr__(self) -> str:
		"""Returns string representation of a Test class object."""
		return str(self)

	def add_error(self, error_message: str) -> None:
		"""Records an error that occurred while parsing this test."""
		self.counts.errors += 1
		print_with_timestamp(red('[ERROR]') + f' Test: {self.name}: {error_message}')

class TestStatus(Enum):
	"""An enumeration class to represent the status of a test."""
	SUCCESS = auto()
	FAILURE = auto()
	SKIPPED = auto()
	TEST_CRASHED = auto()
	NO_TESTS = auto()
	FAILURE_TO_PARSE_TESTS = auto()

class TestCounts:
	"""
	Tracks the counts of statuses of all test cases and any errors within
	a Test.

	Attributes:
	passed : int - the number of tests that have passed
	failed : int - the number of tests that have failed
	crashed : int - the number of tests that have crashed
	skipped : int - the number of tests that have skipped
	errors : int - the number of errors in the test and subtests
	"""
	def __init__(self):
		"""Creates TestCounts object with counts of all test
		statuses and test errors set to 0.
		"""
		self.passed = 0
		self.failed = 0
		self.crashed = 0
		self.skipped = 0
		self.errors = 0

	def __str__(self) -> str:
		"""Returns the string representation of a TestCounts object."""
		statuses = [('passed', self.passed), ('failed', self.failed),
			('crashed', self.crashed), ('skipped', self.skipped),
			('errors', self.errors)]
		return f'Ran {self.total()} tests: ' + \
			', '.join(f'{s}: {n}' for s, n in statuses if n > 0)

	def total(self) -> int:
		"""Returns the total number of test cases within a test
		object, where a test case is a test with no subtests.
		"""
		return (self.passed + self.failed + self.crashed +
			self.skipped)

	def add_subtest_counts(self, counts: TestCounts) -> None:
		"""
		Adds the counts of another TestCounts object to the current
		TestCounts object. Used to add the counts of a subtest to the
		parent test.

		Parameters:
		counts - a different TestCounts object whose counts
			will be added to the counts of the TestCounts object
		"""
		self.passed += counts.passed
		self.failed += counts.failed
		self.crashed += counts.crashed
		self.skipped += counts.skipped
		self.errors += counts.errors

	def get_status(self) -> TestStatus:
		"""Returns the aggregated status of a Test using test
		counts.
		"""
		if self.total() == 0:
			return TestStatus.NO_TESTS
		if self.crashed:
			# Crashes should take priority.
			return TestStatus.TEST_CRASHED
		if self.failed:
			return TestStatus.FAILURE
		if self.passed:
			# No failures or crashes, looks good!
			return TestStatus.SUCCESS
		# We have only skipped tests.
		return TestStatus.SKIPPED

	def add_status(self, status: TestStatus) -> None:
		"""Increments the count for `status`."""
		if status == TestStatus.SUCCESS:
			self.passed += 1
		elif status == TestStatus.FAILURE:
			self.failed += 1
		elif status == TestStatus.SKIPPED:
			self.skipped += 1
		elif status != TestStatus.NO_TESTS:
			self.crashed += 1

class LineStream:
	"""
	A class to represent the lines of kernel output.
	Provides a lazy peek()/pop() interface over an iterator of
	(line#, text).
	"""
	_lines: Iterator[Tuple[int, str]]
	_next: Tuple[int, str]
	_need_next: bool
	_done: bool

	def __init__(self, lines: Iterator[Tuple[int, str]]):
		"""Creates a new LineStream that wraps the given iterator."""
		self._lines = lines
		self._done = False
		self._need_next = True
		self._next = (0, '')

	def _get_next(self) -> None:
		"""Advances the LineSteam to the next line, if necessary."""
		if not self._need_next:
			return
		try:
			self._next = next(self._lines)
		except StopIteration:
			self._done = True
		finally:
			self._need_next = False

	def peek(self) -> str:
		"""Returns the current line, without advancing the LineStream.
		"""
		self._get_next()
		return self._next[1]

	def pop(self) -> str:
		"""Returns the current line and advances the LineStream to
		the next line.
		"""
		s = self.peek()
		if self._done:
			raise ValueError(f'LineStream: going past EOF, last line was {s}')
		self._need_next = True
		return s

	def __bool__(self) -> bool:
		"""Returns True if stream has more lines."""
		self._get_next()
		return not self._done

	# Only used by kunit_tool_test.py.
	def __iter__(self) -> Iterator[str]:
		"""Empties all lines stored in LineStream object into
		Iterator object and returns the Iterator object.
		"""
		while bool(self):
			yield self.pop()

	def line_number(self) -> int:
		"""Returns the line number of the current line."""
		self._get_next()
		return self._next[0]

# Parsing helper methods:

KTAP_START = re.compile(r'KTAP version ([0-9]+)$')
TAP_START = re.compile(r'TAP version ([0-9]+)$')
KTAP_END = re.compile('(List of all partitions:|'
	'Kernel panic - not syncing: VFS:|reboot: System halted)')

def extract_tap_lines(kernel_output: Iterable[str]) -> LineStream:
	"""Extracts KTAP lines from the kernel output."""
	def isolate_ktap_output(kernel_output: Iterable[str]) \
			-> Iterator[Tuple[int, str]]:
		line_num = 0
		started = False
		for line in kernel_output:
			line_num += 1
			line = line.rstrip()  # remove trailing \n
			if not started and KTAP_START.search(line):
				# start extracting KTAP lines and set prefix
				# to number of characters before version line
				prefix_len = len(
					line.split('KTAP version')[0])
				started = True
				yield line_num, line[prefix_len:]
			elif not started and TAP_START.search(line):
				# start extracting KTAP lines and set prefix
				# to number of characters before version line
				prefix_len = len(line.split('TAP version')[0])
				started = True
				yield line_num, line[prefix_len:]
			elif started and KTAP_END.search(line):
				# stop extracting KTAP lines
				break
			elif started:
				# remove prefix and any indention and yield
				# line with line number
				line = line[prefix_len:].lstrip()
				yield line_num, line
	return LineStream(lines=isolate_ktap_output(kernel_output))

KTAP_VERSIONS = [1]
TAP_VERSIONS = [13, 14]

def check_version(version_num: int, accepted_versions: List[int],
			version_type: str, test: Test) -> None:
	"""
	Adds error to test object if version number is too high or too
	low.

	Parameters:
	version_num - The inputted version number from the parsed KTAP or TAP
		header line
	accepted_version - List of accepted KTAP or TAP versions
	version_type - 'KTAP' or 'TAP' depending on the type of
		version line.
	test - Test object for current test being parsed
	"""
	if version_num < min(accepted_versions):
		test.add_error(f'{version_type} version lower than expected!')
	elif version_num > max(accepted_versions):
		test.add_error(f'{version_type} version higer than expected!')

def parse_ktap_header(lines: LineStream, test: Test) -> bool:
	"""
	Parses KTAP/TAP header line and checks version number.
	Returns False if fails to parse KTAP/TAP header line.

	Accepted formats:
	- 'KTAP version [version number]'
	- 'TAP version [version number]'

	Parameters:
	lines - LineStream of KTAP output to parse
	test - Test object for current test being parsed

	Return:
	True if successfully parsed KTAP/TAP header line
	"""
	ktap_match = KTAP_START.match(lines.peek())
	tap_match = TAP_START.match(lines.peek())
	if ktap_match:
		version_num = int(ktap_match.group(1))
		check_version(version_num, KTAP_VERSIONS, 'KTAP', test)
	elif tap_match:
		version_num = int(tap_match.group(1))
		check_version(version_num, TAP_VERSIONS, 'TAP', test)
	else:
		return False
	test.log.append(lines.pop())
	return True

TEST_HEADER = re.compile(r'^# Subtest: (.*)$')

def parse_test_header(lines: LineStream, test: Test) -> bool:
	"""
	Parses test header and stores test name in test object.
	Returns False if fails to parse test header line.

	Accepted format:
	- '# Subtest: [test name]'

	Parameters:
	lines - LineStream of KTAP output to parse
	test - Test object for current test being parsed

	Return:
	True if successfully parsed test header line
	"""
	match = TEST_HEADER.match(lines.peek())
	if not match:
		return False
	test.log.append(lines.pop())
	test.name = match.group(1)
	return True

TEST_PLAN = re.compile(r'1\.\.([0-9]+)')

def parse_test_plan(lines: LineStream, test: Test) -> bool:
	"""
	Parses test plan line and stores the expected number of subtests in
	test object. Reports an error if expected count is 0.
	Returns False and sets expected_count to None if there is no valid test
	plan.

	Accepted format:
	- '1..[number of subtests]'

	Parameters:
	lines - LineStream of KTAP output to parse
	test - Test object for current test being parsed

	Return:
	True if successfully parsed test plan line
	"""
	match = TEST_PLAN.match(lines.peek())
	if not match:
		test.expected_count = None
		return False
	test.log.append(lines.pop())
	expected_count = int(match.group(1))
	test.expected_count = expected_count
	return True

TEST_RESULT = re.compile(r'^(ok|not ok) ([0-9]+) (- )?([^#]*)( # .*)?$')

TEST_RESULT_SKIP = re.compile(r'^(ok|not ok) ([0-9]+) (- )?(.*) # SKIP(.*)$')

def peek_test_name_match(lines: LineStream, test: Test) -> bool:
	"""
	Matches current line with the format of a test result line and checks
	if the name matches the name of the current test.
	Returns False if fails to match format or name.

	Accepted format:
	- '[ok|not ok] [test number] [-] [test name] [optional skip
		directive]'

	Parameters:
	lines - LineStream of KTAP output to parse
	test - Test object for current test being parsed

	Return:
	True if matched a test result line and the name matching the
		expected test name
	"""
	line = lines.peek()
	match = TEST_RESULT.match(line)
	if not match:
		return False
	name = match.group(4)
	return name == test.name

def parse_test_result(lines: LineStream, test: Test,
			expected_num: int) -> bool:
	"""
	Parses test result line and stores the status and name in the test
	object. Reports an error if the test number does not match expected
	test number.
	Returns False if fails to parse test result line.

	Note that the SKIP directive is the only direction that causes a
	change in status.

	Accepted format:
	- '[ok|not ok] [test number] [-] [test name] [optional skip
		directive]'

	Parameters:
	lines - LineStream of KTAP output to parse
	test - Test object for current test being parsed
	expected_num - expected test number for current test

	Return:
	True if successfully parsed a test result line.
	"""
	line = lines.peek()
	match = TEST_RESULT.match(line)
	skip_match = TEST_RESULT_SKIP.match(line)

	# Check if line matches test result line format
	if not match:
		return False
	test.log.append(lines.pop())

	# Set name of test object
	if skip_match:
		test.name = skip_match.group(4)
	else:
		test.name = match.group(4)

	# Check test num
	num = int(match.group(2))
	if num != expected_num:
		test.add_error(f'Expected test number {expected_num} but found {num}')

	# Set status of test object
	status = match.group(1)
	if skip_match:
		test.status = TestStatus.SKIPPED
	elif status == 'ok':
		test.status = TestStatus.SUCCESS
	else:
		test.status = TestStatus.FAILURE
	return True

def parse_diagnostic(lines: LineStream) -> List[str]:
	"""
	Parse lines that do not match the format of a test result line or
	test header line and returns them in list.

	Line formats that are not parsed:
	- '# Subtest: [test name]'
	- '[ok|not ok] [test number] [-] [test name] [optional skip
		directive]'

	Parameters:
	lines - LineStream of KTAP output to parse

	Return:
	Log of diagnostic lines
	"""
	log = []  # type: List[str]
	while lines and not TEST_RESULT.match(lines.peek()) and not \
			TEST_HEADER.match(lines.peek()):
		log.append(lines.pop())
	return log


# Printing helper methods:

DIVIDER = '=' * 60

RESET = '\033[0;0m'

def red(text: str) -> str:
	"""Returns inputted string with red color code."""
	if not sys.stdout.isatty():
		return text
	return '\033[1;31m' + text + RESET

def yellow(text: str) -> str:
	"""Returns inputted string with yellow color code."""
	if not sys.stdout.isatty():
		return text
	return '\033[1;33m' + text + RESET

def green(text: str) -> str:
	"""Returns inputted string with green color code."""
	if not sys.stdout.isatty():
		return text
	return '\033[1;32m' + text + RESET

ANSI_LEN = len(red(''))

def print_with_timestamp(message: str) -> None:
	"""Prints message with timestamp at beginning."""
	print('[%s] %s' % (datetime.datetime.now().strftime('%H:%M:%S'), message))

def format_test_divider(message: str, len_message: int) -> str:
	"""
	Returns string with message centered in fixed width divider.

	Example:
	'===================== message example ====================='

	Parameters:
	message - message to be centered in divider line
	len_message - length of the message to be printed such that
		any characters of the color codes are not counted

	Return:
	String containing message centered in fixed width divider
	"""
	default_count = 3  # default number of dashes
	len_1 = default_count
	len_2 = default_count
	difference = len(DIVIDER) - len_message - 2  # 2 spaces added
	if difference > 0:
		# calculate number of dashes for each side of the divider
		len_1 = int(difference / 2)
		len_2 = difference - len_1
	return ('=' * len_1) + f' {message} ' + ('=' * len_2)

def print_test_header(test: Test) -> None:
	"""
	Prints test header with test name and optionally the expected number
	of subtests.

	Example:
	'=================== example (2 subtests) ==================='

	Parameters:
	test - Test object representing current test being printed
	"""
	message = test.name
	if test.expected_count:
		if test.expected_count == 1:
			message += ' (1 subtest)'
		else:
			message += f' ({test.expected_count} subtests)'
	print_with_timestamp(format_test_divider(message, len(message)))

def print_log(log: Iterable[str]) -> None:
	"""Prints all strings in saved log for test in yellow."""
	for m in log:
		print_with_timestamp(yellow(m))

def format_test_result(test: Test) -> str:
	"""
	Returns string with formatted test result with colored status and test
	name.

	Example:
	'[PASSED] example'

	Parameters:
	test - Test object representing current test being printed

	Return:
	String containing formatted test result
	"""
	if test.status == TestStatus.SUCCESS:
		return green('[PASSED] ') + test.name
	if test.status == TestStatus.SKIPPED:
		return yellow('[SKIPPED] ') + test.name
	if test.status == TestStatus.NO_TESTS:
		return yellow('[NO TESTS RUN] ') + test.name
	if test.status == TestStatus.TEST_CRASHED:
		print_log(test.log)
		return red('[CRASHED] ') + test.name
	print_log(test.log)
	return red('[FAILED] ') + test.name

def print_test_result(test: Test) -> None:
	"""
	Prints result line with status of test.

	Example:
	'[PASSED] example'

	Parameters:
	test - Test object representing current test being printed
	"""
	print_with_timestamp(format_test_result(test))

def print_test_footer(test: Test) -> None:
	"""
	Prints test footer with status of test.

	Example:
	'===================== [PASSED] example ====================='

	Parameters:
	test - Test object representing current test being printed
	"""
	message = format_test_result(test)
	print_with_timestamp(format_test_divider(message,
		len(message) - ANSI_LEN))

def print_summary_line(test: Test) -> None:
	"""
	Prints summary line of test object. Color of line is dependent on
	status of test. Color is green if test passes, yellow if test is
	skipped, and red if the test fails or crashes. Summary line contains
	counts of the statuses of the tests subtests or the test itself if it
	has no subtests.

	Example:
	"Testing complete. Passed: 2, Failed: 0, Crashed: 0, Skipped: 0,
	Errors: 0"

	test - Test object representing current test being printed
	"""
	if test.status == TestStatus.SUCCESS:
		color = green
	elif test.status in (TestStatus.SKIPPED, TestStatus.NO_TESTS):
		color = yellow
	else:
		color = red
	print_with_timestamp(color(f'Testing complete. {test.counts}'))

# Other methods:

def bubble_up_test_results(test: Test) -> None:
	"""
	If the test has subtests, add the test counts of the subtests to the
	test and check if any of the tests crashed and if so set the test
	status to crashed. Otherwise if the test has no subtests add the
	status of the test to the test counts.

	Parameters:
	test - Test object for current test being parsed
	"""
	subtests = test.subtests
	counts = test.counts
	status = test.status
	for t in subtests:
		counts.add_subtest_counts(t.counts)
	if counts.total() == 0:
		counts.add_status(status)
	elif test.counts.get_status() == TestStatus.TEST_CRASHED:
		test.status = TestStatus.TEST_CRASHED

def parse_test(lines: LineStream, expected_num: int, log: List[str]) -> Test:
	"""
	Finds next test to parse in LineStream, creates new Test object,
	parses any subtests of the test, populates Test object with all
	information (status, name) about the test and the Test objects for
	any subtests, and then returns the Test object. The method accepts
	three formats of tests:

	Accepted test formats:

	- Main KTAP/TAP header

	Example:

	KTAP version 1
	1..4
	[subtests]

	- Subtest header line

	Example:

	# Subtest: name
	1..3
	[subtests]
	ok 1 name

	- Test result line

	Example:

	ok 1 - test

	Parameters:
	lines - LineStream of KTAP output to parse
	expected_num - expected test number for test to be parsed
	log - list of strings containing any preceding diagnostic lines
		corresponding to the current test

	Return:
	Test object populated with characteristics and any subtests
	"""
	test = Test()
	test.log.extend(log)
	parent_test = False
	main = parse_ktap_header(lines, test)
	if main:
		# If KTAP/TAP header is found, attempt to parse
		# test plan
		test.name = "main"
		parse_test_plan(lines, test)
		parent_test = True
	else:
		# If KTAP/TAP header is not found, test must be subtest
		# header or test result line so parse attempt to parser
		# subtest header
		parent_test = parse_test_header(lines, test)
		if parent_test:
			# If subtest header is found, attempt to parse
			# test plan and print header
			parse_test_plan(lines, test)
			print_test_header(test)
	expected_count = test.expected_count
	subtests = []
	test_num = 1
	while parent_test and (expected_count is None or test_num <= expected_count):
		# Loop to parse any subtests.
		# Break after parsing expected number of tests or
		# if expected number of tests is unknown break when test
		# result line with matching name to subtest header is found
		# or no more lines in stream.
		sub_log = parse_diagnostic(lines)
		sub_test = Test()
		if not lines or (peek_test_name_match(lines, test) and
				not main):
			if expected_count and test_num <= expected_count:
				# If parser reaches end of test before
				# parsing expected number of subtests, print
				# crashed subtest and record error
				test.add_error('missing expected subtest!')
				sub_test.log.extend(sub_log)
				test.counts.add_status(
					TestStatus.TEST_CRASHED)
				print_test_result(sub_test)
			else:
				test.log.extend(sub_log)
				break
		else:
			sub_test = parse_test(lines, test_num, sub_log)
		subtests.append(sub_test)
		test_num += 1
	test.subtests = subtests
	if not main:
		# If not main test, look for test result line
		test.log.extend(parse_diagnostic(lines))
		if (parent_test and peek_test_name_match(lines, test)) or \
				not parent_test:
			parse_test_result(lines, test, expected_num)
		else:
			test.add_error('missing subtest result line!')

	# Check for there being no tests
	if parent_test and len(subtests) == 0:
		# Don't override a bad status if this test had one reported.
		# Assumption: no subtests means CRASHED is from Test.__init__()
		if test.status in (TestStatus.TEST_CRASHED, TestStatus.SUCCESS):
			test.status = TestStatus.NO_TESTS
			test.add_error('0 tests run!')

	# Add statuses to TestCounts attribute in Test object
	bubble_up_test_results(test)
	if parent_test and not main:
		# If test has subtests and is not the main test object, print
		# footer.
		print_test_footer(test)
	elif not main:
		print_test_result(test)
	return test

def parse_run_tests(kernel_output: Iterable[str]) -> Test:
	"""
	Using kernel output, extract KTAP lines, parse the lines for test
	results and print condensed test results and summary line.

	Parameters:
	kernel_output - Iterable object contains lines of kernel output

	Return:
	Test - the main test object with all subtests.
	"""
	print_with_timestamp(DIVIDER)
	lines = extract_tap_lines(kernel_output)
	test = Test()
	if not lines:
		test.name = '<missing>'
		test.add_error('could not find any KTAP output!')
		test.status = TestStatus.FAILURE_TO_PARSE_TESTS
	else:
		test = parse_test(lines, 0, [])
		if test.status != TestStatus.NO_TESTS:
			test.status = test.counts.get_status()
	print_with_timestamp(DIVIDER)
	print_summary_line(test)
	return test
