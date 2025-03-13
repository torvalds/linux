#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# A collection of tests for tools/testing/kunit/kunit.py
#
# Copyright (C) 2019, Google LLC.
# Author: Brendan Higgins <brendanhiggins@google.com>

import unittest
from unittest import mock

import tempfile, shutil # Handling test_tmpdir

import itertools
import json
import os
import signal
import subprocess
from typing import Iterable

import kunit_config
import kunit_parser
import kunit_kernel
import kunit_json
import kunit
from kunit_printer import stdout

test_tmpdir = ''
abs_test_data_dir = ''

def setUpModule():
	global test_tmpdir, abs_test_data_dir
	test_tmpdir = tempfile.mkdtemp()
	abs_test_data_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), 'test_data'))

def tearDownModule():
	shutil.rmtree(test_tmpdir)

def test_data_path(path):
	return os.path.join(abs_test_data_dir, path)

class KconfigTest(unittest.TestCase):

	def test_is_subset_of(self):
		kconfig0 = kunit_config.Kconfig()
		self.assertTrue(kconfig0.is_subset_of(kconfig0))

		kconfig1 = kunit_config.Kconfig()
		kconfig1.add_entry('TEST', 'y')
		self.assertTrue(kconfig1.is_subset_of(kconfig1))
		self.assertTrue(kconfig0.is_subset_of(kconfig1))
		self.assertFalse(kconfig1.is_subset_of(kconfig0))

	def test_read_from_file(self):
		kconfig_path = test_data_path('test_read_from_file.kconfig')

		kconfig = kunit_config.parse_file(kconfig_path)

		expected_kconfig = kunit_config.Kconfig()
		expected_kconfig.add_entry('UML', 'y')
		expected_kconfig.add_entry('MMU', 'y')
		expected_kconfig.add_entry('TEST', 'y')
		expected_kconfig.add_entry('EXAMPLE_TEST', 'y')
		expected_kconfig.add_entry('MK8', 'n')

		self.assertEqual(kconfig, expected_kconfig)

	def test_write_to_file(self):
		kconfig_path = os.path.join(test_tmpdir, '.config')

		expected_kconfig = kunit_config.Kconfig()
		expected_kconfig.add_entry('UML', 'y')
		expected_kconfig.add_entry('MMU', 'y')
		expected_kconfig.add_entry('TEST', 'y')
		expected_kconfig.add_entry('EXAMPLE_TEST', 'y')
		expected_kconfig.add_entry('MK8', 'n')

		expected_kconfig.write_to_file(kconfig_path)

		actual_kconfig = kunit_config.parse_file(kconfig_path)
		self.assertEqual(actual_kconfig, expected_kconfig)

class KUnitParserTest(unittest.TestCase):
	def setUp(self):
		self.print_mock = mock.patch('kunit_printer.Printer.print').start()
		self.addCleanup(mock.patch.stopall)

	def noPrintCallContains(self, substr: str):
		for call in self.print_mock.mock_calls:
			self.assertNotIn(substr, call.args[0])

	def assertContains(self, needle: str, haystack: kunit_parser.LineStream):
		# Clone the iterator so we can print the contents on failure.
		copy, backup = itertools.tee(haystack)
		for line in copy:
			if needle in line:
				return
		raise AssertionError(f'"{needle}" not found in {list(backup)}!')

	def test_output_isolated_correctly(self):
		log_path = test_data_path('test_output_isolated_correctly.log')
		with open(log_path) as file:
			result = kunit_parser.extract_tap_lines(file.readlines())
		self.assertContains('TAP version 14', result)
		self.assertContains('# Subtest: example', result)
		self.assertContains('1..2', result)
		self.assertContains('ok 1 - example_simple_test', result)
		self.assertContains('ok 2 - example_mock_test', result)
		self.assertContains('ok 1 - example', result)

	def test_output_with_prefix_isolated_correctly(self):
		log_path = test_data_path('test_pound_sign.log')
		with open(log_path) as file:
			result = kunit_parser.extract_tap_lines(file.readlines())
		self.assertContains('TAP version 14', result)
		self.assertContains('# Subtest: kunit-resource-test', result)
		self.assertContains('1..5', result)
		self.assertContains('ok 1 - kunit_resource_test_init_resources', result)
		self.assertContains('ok 2 - kunit_resource_test_alloc_resource', result)
		self.assertContains('ok 3 - kunit_resource_test_destroy_resource', result)
		self.assertContains('foo bar 	#', result)
		self.assertContains('ok 4 - kunit_resource_test_cleanup_resources', result)
		self.assertContains('ok 5 - kunit_resource_test_proper_free_ordering', result)
		self.assertContains('ok 1 - kunit-resource-test', result)
		self.assertContains('foo bar 	# non-kunit output', result)
		self.assertContains('# Subtest: kunit-try-catch-test', result)
		self.assertContains('1..2', result)
		self.assertContains('ok 1 - kunit_test_try_catch_successful_try_no_catch',
				    result)
		self.assertContains('ok 2 - kunit_test_try_catch_unsuccessful_try_does_catch',
				    result)
		self.assertContains('ok 2 - kunit-try-catch-test', result)
		self.assertContains('# Subtest: string-stream-test', result)
		self.assertContains('1..3', result)
		self.assertContains('ok 1 - string_stream_test_empty_on_creation', result)
		self.assertContains('ok 2 - string_stream_test_not_empty_after_add', result)
		self.assertContains('ok 3 - string_stream_test_get_string', result)
		self.assertContains('ok 3 - string-stream-test', result)

	def test_parse_successful_test_log(self):
		all_passed_log = test_data_path('test_is_test_passed-all_passed.log')
		with open(all_passed_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual(result.counts.errors, 0)

	def test_parse_successful_nested_tests_log(self):
		all_passed_log = test_data_path('test_is_test_passed-all_passed_nested.log')
		with open(all_passed_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual(result.counts.errors, 0)

	def test_kselftest_nested(self):
		kselftest_log = test_data_path('test_is_test_passed-kselftest.log')
		with open(kselftest_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual(result.counts.errors, 0)

	def test_parse_failed_test_log(self):
		failed_log = test_data_path('test_is_test_passed-failure.log')
		with open(failed_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.FAILURE, result.status)
		self.assertEqual(result.counts.errors, 0)

	def test_no_header(self):
		empty_log = test_data_path('test_is_test_passed-no_tests_run_no_header.log')
		with open(empty_log) as file:
			result = kunit_parser.parse_run_tests(
				kunit_parser.extract_tap_lines(file.readlines()), stdout)
		self.assertEqual(0, len(result.subtests))
		self.assertEqual(kunit_parser.TestStatus.FAILURE_TO_PARSE_TESTS, result.status)
		self.assertEqual(result.counts.errors, 1)

	def test_missing_test_plan(self):
		missing_plan_log = test_data_path('test_is_test_passed-'
			'missing_plan.log')
		with open(missing_plan_log) as file:
			result = kunit_parser.parse_run_tests(
				kunit_parser.extract_tap_lines(
				file.readlines()), stdout)
		# A missing test plan is not an error.
		self.assertEqual(result.counts, kunit_parser.TestCounts(passed=10, errors=0))
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)

	def test_no_tests(self):
		header_log = test_data_path('test_is_test_passed-no_tests_run_with_header.log')
		with open(header_log) as file:
			result = kunit_parser.parse_run_tests(
				kunit_parser.extract_tap_lines(file.readlines()), stdout)
		self.assertEqual(0, len(result.subtests))
		self.assertEqual(kunit_parser.TestStatus.NO_TESTS, result.status)
		self.assertEqual(result.counts.errors, 1)

	def test_no_tests_no_plan(self):
		no_plan_log = test_data_path('test_is_test_passed-no_tests_no_plan.log')
		with open(no_plan_log) as file:
			result = kunit_parser.parse_run_tests(
				kunit_parser.extract_tap_lines(file.readlines()), stdout)
		self.assertEqual(0, len(result.subtests[0].subtests[0].subtests))
		self.assertEqual(
			kunit_parser.TestStatus.NO_TESTS,
			result.subtests[0].subtests[0].status)
		self.assertEqual(result.counts, kunit_parser.TestCounts(passed=1, errors=1))


	def test_no_kunit_output(self):
		crash_log = test_data_path('test_insufficient_memory.log')
		print_mock = mock.patch('kunit_printer.Printer.print').start()
		with open(crash_log) as file:
			result = kunit_parser.parse_run_tests(
				kunit_parser.extract_tap_lines(file.readlines()), stdout)
		print_mock.assert_any_call(StrContains('Could not find any KTAP output.'))
		print_mock.stop()
		self.assertEqual(0, len(result.subtests))
		self.assertEqual(result.counts.errors, 1)

	def test_skipped_test(self):
		skipped_log = test_data_path('test_skip_tests.log')
		with open(skipped_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)

		# A skipped test does not fail the whole suite.
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual(result.counts, kunit_parser.TestCounts(passed=4, skipped=1))

	def test_skipped_all_tests(self):
		skipped_log = test_data_path('test_skip_all_tests.log')
		with open(skipped_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)

		self.assertEqual(kunit_parser.TestStatus.SKIPPED, result.status)
		self.assertEqual(result.counts, kunit_parser.TestCounts(skipped=5))

	def test_ignores_hyphen(self):
		hyphen_log = test_data_path('test_strip_hyphen.log')
		with open(hyphen_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)

		# A skipped test does not fail the whole suite.
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual(
			"sysctl_test",
			result.subtests[0].name)
		self.assertEqual(
			"example",
			result.subtests[1].name)

	def test_ignores_prefix_printk_time(self):
		prefix_log = test_data_path('test_config_printk_time.log')
		with open(prefix_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual('kunit-resource-test', result.subtests[0].name)
		self.assertEqual(result.counts.errors, 0)

	def test_ignores_multiple_prefixes(self):
		prefix_log = test_data_path('test_multiple_prefixes.log')
		with open(prefix_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual('kunit-resource-test', result.subtests[0].name)
		self.assertEqual(result.counts.errors, 0)

	def test_prefix_mixed_kernel_output(self):
		mixed_prefix_log = test_data_path('test_interrupted_tap_output.log')
		with open(mixed_prefix_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual('kunit-resource-test', result.subtests[0].name)
		self.assertEqual(result.counts.errors, 0)

	def test_prefix_poundsign(self):
		pound_log = test_data_path('test_pound_sign.log')
		with open(pound_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual('kunit-resource-test', result.subtests[0].name)
		self.assertEqual(result.counts.errors, 0)

	def test_kernel_panic_end(self):
		panic_log = test_data_path('test_kernel_panic_interrupt.log')
		with open(panic_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.TEST_CRASHED, result.status)
		self.assertEqual('kunit-resource-test', result.subtests[0].name)
		self.assertGreaterEqual(result.counts.errors, 1)

	def test_pound_no_prefix(self):
		pound_log = test_data_path('test_pound_no_prefix.log')
		with open(pound_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)
		self.assertEqual('kunit-resource-test', result.subtests[0].name)
		self.assertEqual(result.counts.errors, 0)

	def test_summarize_failures(self):
		output = """
		KTAP version 1
		1..2
			# Subtest: all_failed_suite
			1..2
			not ok 1 - test1
			not ok 2 - test2
		not ok 1 - all_failed_suite
			# Subtest: some_failed_suite
			1..2
			ok 1 - test1
			not ok 2 - test2
		not ok 1 - some_failed_suite
		"""
		result = kunit_parser.parse_run_tests(output.splitlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.FAILURE, result.status)

		self.assertEqual(kunit_parser._summarize_failed_tests(result),
			'Failures: all_failed_suite, some_failed_suite.test2')

	def test_ktap_format(self):
		ktap_log = test_data_path('test_parse_ktap_output.log')
		with open(ktap_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.assertEqual(result.counts, kunit_parser.TestCounts(passed=3))
		self.assertEqual('suite', result.subtests[0].name)
		self.assertEqual('case_1', result.subtests[0].subtests[0].name)
		self.assertEqual('case_2', result.subtests[0].subtests[1].name)

	def test_parse_subtest_header(self):
		ktap_log = test_data_path('test_parse_subtest_header.log')
		with open(ktap_log) as file:
			kunit_parser.parse_run_tests(file.readlines(), stdout)
		self.print_mock.assert_any_call(StrContains('suite (1 subtest)'))

	def test_parse_attributes(self):
		ktap_log = test_data_path('test_parse_attributes.log')
		with open(ktap_log) as file:
			result = kunit_parser.parse_run_tests(file.readlines(), stdout)

		# Test should pass with no errors
		self.assertEqual(result.counts, kunit_parser.TestCounts(passed=1, errors=0))
		self.assertEqual(kunit_parser.TestStatus.SUCCESS, result.status)

		# Ensure suite header is parsed correctly
		self.print_mock.assert_any_call(StrContains('suite (1 subtest)'))

		# Ensure attributes in correct test log
		self.assertContains('# module: example', result.subtests[0].log)
		self.assertContains('# test.speed: slow', result.subtests[0].subtests[0].log)

	def test_show_test_output_on_failure(self):
		output = """
		KTAP version 1
		1..1
		  Test output.
		    Indented more.
		not ok 1 test1
		"""
		result = kunit_parser.parse_run_tests(output.splitlines(), stdout)
		self.assertEqual(kunit_parser.TestStatus.FAILURE, result.status)

		self.print_mock.assert_any_call(StrContains('Test output.'))
		self.print_mock.assert_any_call(StrContains('  Indented more.'))
		self.noPrintCallContains('not ok 1 test1')

	def test_parse_late_test_plan(self):
		output = """
		TAP version 13
		ok 4 test4
		1..4
		"""
		result = kunit_parser.parse_run_tests(output.splitlines(), stdout)
		# Missing test results after test plan should alert a suspected test crash.
		self.assertEqual(kunit_parser.TestStatus.TEST_CRASHED, result.status)
		self.assertEqual(result.counts, kunit_parser.TestCounts(passed=1, crashed=1, errors=1))

def line_stream_from_strs(strs: Iterable[str]) -> kunit_parser.LineStream:
	return kunit_parser.LineStream(enumerate(strs, start=1))

class LineStreamTest(unittest.TestCase):

	def test_basic(self):
		stream = line_stream_from_strs(['hello', 'world'])

		self.assertTrue(stream, msg='Should be more input')
		self.assertEqual(stream.line_number(), 1)
		self.assertEqual(stream.peek(), 'hello')
		self.assertEqual(stream.pop(), 'hello')

		self.assertTrue(stream, msg='Should be more input')
		self.assertEqual(stream.line_number(), 2)
		self.assertEqual(stream.peek(), 'world')
		self.assertEqual(stream.pop(), 'world')

		self.assertFalse(stream, msg='Should be no more input')
		with self.assertRaisesRegex(ValueError, 'LineStream: going past EOF'):
			stream.pop()

	def test_is_lazy(self):
		called_times = 0
		def generator():
			nonlocal called_times
			for _ in range(1,5):
				called_times += 1
				yield called_times, str(called_times)

		stream = kunit_parser.LineStream(generator())
		self.assertEqual(called_times, 0)

		self.assertEqual(stream.pop(), '1')
		self.assertEqual(called_times, 1)

		self.assertEqual(stream.pop(), '2')
		self.assertEqual(called_times, 2)

class LinuxSourceTreeTest(unittest.TestCase):

	def setUp(self):
		mock.patch.object(signal, 'signal').start()
		self.addCleanup(mock.patch.stopall)

	def test_invalid_kunitconfig(self):
		with self.assertRaisesRegex(kunit_kernel.ConfigError, 'nonexistent.* does not exist'):
			kunit_kernel.LinuxSourceTree('', kunitconfig_paths=['/nonexistent_file'])

	def test_valid_kunitconfig(self):
		with tempfile.NamedTemporaryFile('wt') as kunitconfig:
			kunit_kernel.LinuxSourceTree('', kunitconfig_paths=[kunitconfig.name])

	def test_dir_kunitconfig(self):
		with tempfile.TemporaryDirectory('') as dir:
			with open(os.path.join(dir, '.kunitconfig'), 'w'):
				pass
			kunit_kernel.LinuxSourceTree('', kunitconfig_paths=[dir])

	def test_multiple_kunitconfig(self):
		want_kconfig = kunit_config.Kconfig()
		want_kconfig.add_entry('KUNIT', 'y')
		want_kconfig.add_entry('KUNIT_TEST', 'm')

		with tempfile.TemporaryDirectory('') as dir:
			other = os.path.join(dir, 'otherkunitconfig')
			with open(os.path.join(dir, '.kunitconfig'), 'w') as f:
				f.write('CONFIG_KUNIT=y')
			with open(other, 'w') as f:
				f.write('CONFIG_KUNIT_TEST=m')
				pass

			tree = kunit_kernel.LinuxSourceTree('', kunitconfig_paths=[dir, other])
			self.assertTrue(want_kconfig.is_subset_of(tree._kconfig), msg=tree._kconfig)


	def test_multiple_kunitconfig_invalid(self):
		with tempfile.TemporaryDirectory('') as dir:
			other = os.path.join(dir, 'otherkunitconfig')
			with open(os.path.join(dir, '.kunitconfig'), 'w') as f:
				f.write('CONFIG_KUNIT=y')
			with open(other, 'w') as f:
				f.write('CONFIG_KUNIT=m')

			with self.assertRaisesRegex(kunit_kernel.ConfigError, '(?s)Multiple values.*CONFIG_KUNIT'):
				kunit_kernel.LinuxSourceTree('', kunitconfig_paths=[dir, other])


	def test_kconfig_add(self):
		want_kconfig = kunit_config.Kconfig()
		want_kconfig.add_entry('NOT_REAL', 'y')

		tree = kunit_kernel.LinuxSourceTree('', kconfig_add=['CONFIG_NOT_REAL=y'])
		self.assertTrue(want_kconfig.is_subset_of(tree._kconfig), msg=tree._kconfig)

	def test_invalid_arch(self):
		with self.assertRaisesRegex(kunit_kernel.ConfigError, 'not a valid arch, options are.*x86_64'):
			kunit_kernel.LinuxSourceTree('', arch='invalid')

	def test_run_kernel_hits_exception(self):
		def fake_start(unused_args, unused_build_dir):
			return subprocess.Popen(['echo "hi\nbye"'], shell=True, text=True, stdout=subprocess.PIPE)

		with tempfile.TemporaryDirectory('') as build_dir:
			tree = kunit_kernel.LinuxSourceTree(build_dir)
			mock.patch.object(tree._ops, 'start', side_effect=fake_start).start()

			with self.assertRaises(ValueError):
				for line in tree.run_kernel(build_dir=build_dir):
					self.assertEqual(line, 'hi\n')
					raise ValueError('uh oh, did not read all output')

			with open(kunit_kernel.get_outfile_path(build_dir), 'rt') as outfile:
				self.assertEqual(outfile.read(), 'hi\nbye\n', msg='Missing some output')

	def test_build_reconfig_no_config(self):
		with tempfile.TemporaryDirectory('') as build_dir:
			with open(kunit_kernel.get_kunitconfig_path(build_dir), 'w') as f:
				f.write('CONFIG_KUNIT=y')

			tree = kunit_kernel.LinuxSourceTree(build_dir)
			# Stub out the source tree operations, so we don't have
			# the defaults for any given architecture get in the
			# way.
			tree._ops = kunit_kernel.LinuxSourceTreeOperations('none', None)
			mock_build_config = mock.patch.object(tree, 'build_config').start()

			# Should generate the .config
			self.assertTrue(tree.build_reconfig(build_dir, make_options=[]))
			mock_build_config.assert_called_once_with(build_dir, [])

	def test_build_reconfig_existing_config(self):
		with tempfile.TemporaryDirectory('') as build_dir:
			# Existing .config is a superset, should not touch it
			with open(kunit_kernel.get_kunitconfig_path(build_dir), 'w') as f:
				f.write('CONFIG_KUNIT=y')
			with open(kunit_kernel.get_old_kunitconfig_path(build_dir), 'w') as f:
				f.write('CONFIG_KUNIT=y')
			with open(kunit_kernel.get_kconfig_path(build_dir), 'w') as f:
				f.write('CONFIG_KUNIT=y\nCONFIG_KUNIT_TEST=y')

			tree = kunit_kernel.LinuxSourceTree(build_dir)
			# Stub out the source tree operations, so we don't have
			# the defaults for any given architecture get in the
			# way.
			tree._ops = kunit_kernel.LinuxSourceTreeOperations('none', None)
			mock_build_config = mock.patch.object(tree, 'build_config').start()

			self.assertTrue(tree.build_reconfig(build_dir, make_options=[]))
			self.assertEqual(mock_build_config.call_count, 0)

	def test_build_reconfig_remove_option(self):
		with tempfile.TemporaryDirectory('') as build_dir:
			# We removed CONFIG_KUNIT_TEST=y from our .kunitconfig...
			with open(kunit_kernel.get_kunitconfig_path(build_dir), 'w') as f:
				f.write('CONFIG_KUNIT=y')
			with open(kunit_kernel.get_old_kunitconfig_path(build_dir), 'w') as f:
				f.write('CONFIG_KUNIT=y\nCONFIG_KUNIT_TEST=y')
			with open(kunit_kernel.get_kconfig_path(build_dir), 'w') as f:
				f.write('CONFIG_KUNIT=y\nCONFIG_KUNIT_TEST=y')

			tree = kunit_kernel.LinuxSourceTree(build_dir)
			# Stub out the source tree operations, so we don't have
			# the defaults for any given architecture get in the
			# way.
			tree._ops = kunit_kernel.LinuxSourceTreeOperations('none', None)
			mock_build_config = mock.patch.object(tree, 'build_config').start()

			# ... so we should trigger a call to build_config()
			self.assertTrue(tree.build_reconfig(build_dir, make_options=[]))
			mock_build_config.assert_called_once_with(build_dir, [])

	# TODO: add more test cases.


class KUnitJsonTest(unittest.TestCase):
	def setUp(self):
		self.print_mock = mock.patch('kunit_printer.Printer.print').start()
		self.addCleanup(mock.patch.stopall)

	def _json_for(self, log_file):
		with open(test_data_path(log_file)) as file:
			test_result = kunit_parser.parse_run_tests(file, stdout)
			json_obj = kunit_json.get_json_result(
				test=test_result,
				metadata=kunit_json.Metadata())
		return json.loads(json_obj)

	def test_failed_test_json(self):
		result = self._json_for('test_is_test_passed-failure.log')
		self.assertEqual(
			{'name': 'example_simple_test', 'status': 'FAIL'},
			result["sub_groups"][1]["test_cases"][0])

	def test_crashed_test_json(self):
		result = self._json_for('test_kernel_panic_interrupt.log')
		self.assertEqual(
			{'name': '', 'status': 'ERROR'},
			result["sub_groups"][2]["test_cases"][1])

	def test_skipped_test_json(self):
		result = self._json_for('test_skip_tests.log')
		self.assertEqual(
			{'name': 'example_skip_test', 'status': 'SKIP'},
			result["sub_groups"][1]["test_cases"][1])

	def test_no_tests_json(self):
		result = self._json_for('test_is_test_passed-no_tests_run_with_header.log')
		self.assertEqual(0, len(result['sub_groups']))

	def test_nested_json(self):
		result = self._json_for('test_is_test_passed-all_passed_nested.log')
		self.assertEqual(
			{'name': 'example_simple_test', 'status': 'PASS'},
			result["sub_groups"][0]["sub_groups"][0]["test_cases"][0])

class StrContains(str):
	def __eq__(self, other):
		return self in other

class KUnitMainTest(unittest.TestCase):
	def setUp(self):
		path = test_data_path('test_is_test_passed-all_passed.log')
		with open(path) as file:
			all_passed_log = file.readlines()

		self.print_mock = mock.patch('kunit_printer.Printer.print').start()
		self.addCleanup(mock.patch.stopall)

		self.mock_linux_init = mock.patch.object(kunit_kernel, 'LinuxSourceTree').start()
		self.linux_source_mock = self.mock_linux_init.return_value
		self.linux_source_mock.build_reconfig.return_value = True
		self.linux_source_mock.build_kernel.return_value = True
		self.linux_source_mock.run_kernel.return_value = all_passed_log

	def test_config_passes_args_pass(self):
		kunit.main(['config', '--build_dir=.kunit'])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)
		self.assertEqual(self.linux_source_mock.run_kernel.call_count, 0)

	def test_build_passes_args_pass(self):
		kunit.main(['build'])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)
		self.linux_source_mock.build_kernel.assert_called_once_with(kunit.get_default_jobs(), '.kunit', None)
		self.assertEqual(self.linux_source_mock.run_kernel.call_count, 0)

	def test_exec_passes_args_pass(self):
		kunit.main(['exec'])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 0)
		self.assertEqual(self.linux_source_mock.run_kernel.call_count, 1)
		self.linux_source_mock.run_kernel.assert_called_once_with(
			args=None, build_dir='.kunit', filter_glob='', filter='', filter_action=None, timeout=300)
		self.print_mock.assert_any_call(StrContains('Testing complete.'))

	def test_run_passes_args_pass(self):
		kunit.main(['run'])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)
		self.assertEqual(self.linux_source_mock.run_kernel.call_count, 1)
		self.linux_source_mock.run_kernel.assert_called_once_with(
			args=None, build_dir='.kunit', filter_glob='', filter='', filter_action=None, timeout=300)
		self.print_mock.assert_any_call(StrContains('Testing complete.'))

	def test_exec_passes_args_fail(self):
		self.linux_source_mock.run_kernel = mock.Mock(return_value=[])
		with self.assertRaises(SystemExit) as e:
			kunit.main(['exec'])
		self.assertEqual(e.exception.code, 1)

	def test_run_passes_args_fail(self):
		self.linux_source_mock.run_kernel = mock.Mock(return_value=[])
		with self.assertRaises(SystemExit) as e:
			kunit.main(['run'])
		self.assertEqual(e.exception.code, 1)
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)
		self.assertEqual(self.linux_source_mock.run_kernel.call_count, 1)
		self.print_mock.assert_any_call(StrContains('Could not find any KTAP output.'))

	def test_exec_no_tests(self):
		self.linux_source_mock.run_kernel = mock.Mock(return_value=['TAP version 14', '1..0'])
		with self.assertRaises(SystemExit) as e:
			kunit.main(['run'])
		self.assertEqual(e.exception.code, 1)
		self.linux_source_mock.run_kernel.assert_called_once_with(
			args=None, build_dir='.kunit', filter_glob='', filter='', filter_action=None, timeout=300)
		self.print_mock.assert_any_call(StrContains(' 0 tests run!'))

	def test_exec_raw_output(self):
		self.linux_source_mock.run_kernel = mock.Mock(return_value=[])
		kunit.main(['exec', '--raw_output'])
		self.assertEqual(self.linux_source_mock.run_kernel.call_count, 1)
		for call in self.print_mock.call_args_list:
			self.assertNotEqual(call, mock.call(StrContains('Testing complete.')))
			self.assertNotEqual(call, mock.call(StrContains(' 0 tests run!')))

	def test_run_raw_output(self):
		self.linux_source_mock.run_kernel = mock.Mock(return_value=[])
		kunit.main(['run', '--raw_output'])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)
		self.assertEqual(self.linux_source_mock.run_kernel.call_count, 1)
		for call in self.print_mock.call_args_list:
			self.assertNotEqual(call, mock.call(StrContains('Testing complete.')))
			self.assertNotEqual(call, mock.call(StrContains(' 0 tests run!')))

	def test_run_raw_output_kunit(self):
		self.linux_source_mock.run_kernel = mock.Mock(return_value=[])
		kunit.main(['run', '--raw_output=kunit'])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)
		self.assertEqual(self.linux_source_mock.run_kernel.call_count, 1)
		for call in self.print_mock.call_args_list:
			self.assertNotEqual(call, mock.call(StrContains('Testing complete.')))
			self.assertNotEqual(call, mock.call(StrContains(' 0 tests run')))

	def test_run_raw_output_invalid(self):
		self.linux_source_mock.run_kernel = mock.Mock(return_value=[])
		with self.assertRaises(SystemExit) as e:
			kunit.main(['run', '--raw_output=invalid'])
		self.assertNotEqual(e.exception.code, 0)

	def test_run_raw_output_does_not_take_positional_args(self):
		# --raw_output is a string flag, but we don't want it to consume
		# any positional arguments, only ones after an '='
		self.linux_source_mock.run_kernel = mock.Mock(return_value=[])
		kunit.main(['run', '--raw_output', 'filter_glob'])
		self.linux_source_mock.run_kernel.assert_called_once_with(
			args=None, build_dir='.kunit', filter_glob='filter_glob', filter='', filter_action=None, timeout=300)

	def test_exec_timeout(self):
		timeout = 3453
		kunit.main(['exec', '--timeout', str(timeout)])
		self.linux_source_mock.run_kernel.assert_called_once_with(
			args=None, build_dir='.kunit', filter_glob='', filter='', filter_action=None, timeout=timeout)
		self.print_mock.assert_any_call(StrContains('Testing complete.'))

	def test_run_timeout(self):
		timeout = 3453
		kunit.main(['run', '--timeout', str(timeout)])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)
		self.linux_source_mock.run_kernel.assert_called_once_with(
			args=None, build_dir='.kunit', filter_glob='', filter='', filter_action=None, timeout=timeout)
		self.print_mock.assert_any_call(StrContains('Testing complete.'))

	def test_run_builddir(self):
		build_dir = '.kunit'
		kunit.main(['run', '--build_dir=.kunit'])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)
		self.linux_source_mock.run_kernel.assert_called_once_with(
			args=None, build_dir=build_dir, filter_glob='', filter='', filter_action=None, timeout=300)
		self.print_mock.assert_any_call(StrContains('Testing complete.'))

	def test_config_builddir(self):
		build_dir = '.kunit'
		kunit.main(['config', '--build_dir', build_dir])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)

	def test_build_builddir(self):
		build_dir = '.kunit'
		jobs = kunit.get_default_jobs()
		kunit.main(['build', '--build_dir', build_dir])
		self.linux_source_mock.build_kernel.assert_called_once_with(jobs, build_dir, None)

	def test_exec_builddir(self):
		build_dir = '.kunit'
		kunit.main(['exec', '--build_dir', build_dir])
		self.linux_source_mock.run_kernel.assert_called_once_with(
			args=None, build_dir=build_dir, filter_glob='', filter='', filter_action=None, timeout=300)
		self.print_mock.assert_any_call(StrContains('Testing complete.'))

	def test_run_kunitconfig(self):
		kunit.main(['run', '--kunitconfig=mykunitconfig'])
		# Just verify that we parsed and initialized it correctly here.
		self.mock_linux_init.assert_called_once_with('.kunit',
						kunitconfig_paths=['mykunitconfig'],
						kconfig_add=None,
						arch='um',
						cross_compile=None,
						qemu_config_path=None,
						extra_qemu_args=[])

	def test_config_kunitconfig(self):
		kunit.main(['config', '--kunitconfig=mykunitconfig'])
		# Just verify that we parsed and initialized it correctly here.
		self.mock_linux_init.assert_called_once_with('.kunit',
						kunitconfig_paths=['mykunitconfig'],
						kconfig_add=None,
						arch='um',
						cross_compile=None,
						qemu_config_path=None,
						extra_qemu_args=[])

	def test_config_alltests(self):
		kunit.main(['config', '--kunitconfig=mykunitconfig', '--alltests'])
		# Just verify that we parsed and initialized it correctly here.
		self.mock_linux_init.assert_called_once_with('.kunit',
						kunitconfig_paths=[kunit_kernel.ALL_TESTS_CONFIG_PATH, 'mykunitconfig'],
						kconfig_add=None,
						arch='um',
						cross_compile=None,
						qemu_config_path=None,
						extra_qemu_args=[])


	@mock.patch.object(kunit_kernel, 'LinuxSourceTree')
	def test_run_multiple_kunitconfig(self, mock_linux_init):
		mock_linux_init.return_value = self.linux_source_mock
		kunit.main(['run', '--kunitconfig=mykunitconfig', '--kunitconfig=other'])
		# Just verify that we parsed and initialized it correctly here.
		mock_linux_init.assert_called_once_with('.kunit',
							kunitconfig_paths=['mykunitconfig', 'other'],
							kconfig_add=None,
							arch='um',
							cross_compile=None,
							qemu_config_path=None,
							extra_qemu_args=[])

	def test_run_kconfig_add(self):
		kunit.main(['run', '--kconfig_add=CONFIG_KASAN=y', '--kconfig_add=CONFIG_KCSAN=y'])
		# Just verify that we parsed and initialized it correctly here.
		self.mock_linux_init.assert_called_once_with('.kunit',
						kunitconfig_paths=[],
						kconfig_add=['CONFIG_KASAN=y', 'CONFIG_KCSAN=y'],
						arch='um',
						cross_compile=None,
						qemu_config_path=None,
						extra_qemu_args=[])

	def test_run_qemu_args(self):
		kunit.main(['run', '--arch=x86_64', '--qemu_args', '-m 2048'])
		# Just verify that we parsed and initialized it correctly here.
		self.mock_linux_init.assert_called_once_with('.kunit',
						kunitconfig_paths=[],
						kconfig_add=None,
						arch='x86_64',
						cross_compile=None,
						qemu_config_path=None,
						extra_qemu_args=['-m', '2048'])

	def test_run_kernel_args(self):
		kunit.main(['run', '--kernel_args=a=1', '--kernel_args=b=2'])
		self.assertEqual(self.linux_source_mock.build_reconfig.call_count, 1)
		self.linux_source_mock.run_kernel.assert_called_once_with(
		      args=['a=1','b=2'], build_dir='.kunit', filter_glob='', filter='', filter_action=None, timeout=300)
		self.print_mock.assert_any_call(StrContains('Testing complete.'))

	def test_list_tests(self):
		want = ['suite.test1', 'suite.test2', 'suite2.test1']
		self.linux_source_mock.run_kernel.return_value = ['TAP version 14', 'init: random output'] + want

		got = kunit._list_tests(self.linux_source_mock,
				     kunit.KunitExecRequest(None, None, False, False, '.kunit', 300, 'suite*', '', None, None, 'suite', False, False))
		self.assertEqual(got, want)
		# Should respect the user's filter glob when listing tests.
		self.linux_source_mock.run_kernel.assert_called_once_with(
			args=['kunit.action=list'], build_dir='.kunit', filter_glob='suite*', filter='', filter_action=None, timeout=300)

	@mock.patch.object(kunit, '_list_tests')
	def test_run_isolated_by_suite(self, mock_tests):
		mock_tests.return_value = ['suite.test1', 'suite.test2', 'suite2.test1']
		kunit.main(['exec', '--run_isolated=suite', 'suite*.test*'])

		# Should respect the user's filter glob when listing tests.
		mock_tests.assert_called_once_with(mock.ANY,
				     kunit.KunitExecRequest(None, None, False, False, '.kunit', 300, 'suite*.test*', '', None, None, 'suite', False, False))
		self.linux_source_mock.run_kernel.assert_has_calls([
			mock.call(args=None, build_dir='.kunit', filter_glob='suite.test*', filter='', filter_action=None, timeout=300),
			mock.call(args=None, build_dir='.kunit', filter_glob='suite2.test*', filter='', filter_action=None, timeout=300),
		])

	@mock.patch.object(kunit, '_list_tests')
	def test_run_isolated_by_test(self, mock_tests):
		mock_tests.return_value = ['suite.test1', 'suite.test2', 'suite2.test1']
		kunit.main(['exec', '--run_isolated=test', 'suite*'])

		# Should respect the user's filter glob when listing tests.
		mock_tests.assert_called_once_with(mock.ANY,
				     kunit.KunitExecRequest(None, None, False, False, '.kunit', 300, 'suite*', '', None, None, 'test', False, False))
		self.linux_source_mock.run_kernel.assert_has_calls([
			mock.call(args=None, build_dir='.kunit', filter_glob='suite.test1', filter='', filter_action=None, timeout=300),
			mock.call(args=None, build_dir='.kunit', filter_glob='suite.test2', filter='', filter_action=None, timeout=300),
			mock.call(args=None, build_dir='.kunit', filter_glob='suite2.test1', filter='', filter_action=None, timeout=300),
		])

if __name__ == '__main__':
	unittest.main()
