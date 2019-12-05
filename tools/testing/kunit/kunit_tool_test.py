#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0
#
# A collection of tests for tools/testing/kunit/kunit.py
#
# Copyright (C) 2019, Google LLC.
# Author: Brendan Higgins <brendanhiggins@google.com>

import unittest
from unittest import mock

import tempfile, shutil # Handling test_tmpdir

import os

import kunit_config
import kunit_parser
import kunit_kernel
import kunit

test_tmpdir = ''

def setUpModule():
	global test_tmpdir
	test_tmpdir = tempfile.mkdtemp()

def tearDownModule():
	shutil.rmtree(test_tmpdir)

def get_absolute_path(path):
	return os.path.join(os.path.dirname(__file__), path)

class KconfigTest(unittest.TestCase):

	def test_is_subset_of(self):
		kconfig0 = kunit_config.Kconfig()
		self.assertTrue(kconfig0.is_subset_of(kconfig0))

		kconfig1 = kunit_config.Kconfig()
		kconfig1.add_entry(kunit_config.KconfigEntry('CONFIG_TEST=y'))
		self.assertTrue(kconfig1.is_subset_of(kconfig1))
		self.assertTrue(kconfig0.is_subset_of(kconfig1))
		self.assertFalse(kconfig1.is_subset_of(kconfig0))

	def test_read_from_file(self):
		kconfig = kunit_config.Kconfig()
		kconfig_path = get_absolute_path(
			'test_data/test_read_from_file.kconfig')

		kconfig.read_from_file(kconfig_path)

		expected_kconfig = kunit_config.Kconfig()
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('CONFIG_UML=y'))
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('CONFIG_MMU=y'))
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('CONFIG_TEST=y'))
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('CONFIG_EXAMPLE_TEST=y'))
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('# CONFIG_MK8 is not set'))

		self.assertEqual(kconfig.entries(), expected_kconfig.entries())

	def test_write_to_file(self):
		kconfig_path = os.path.join(test_tmpdir, '.config')

		expected_kconfig = kunit_config.Kconfig()
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('CONFIG_UML=y'))
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('CONFIG_MMU=y'))
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('CONFIG_TEST=y'))
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('CONFIG_EXAMPLE_TEST=y'))
		expected_kconfig.add_entry(
			kunit_config.KconfigEntry('# CONFIG_MK8 is not set'))

		expected_kconfig.write_to_file(kconfig_path)

		actual_kconfig = kunit_config.Kconfig()
		actual_kconfig.read_from_file(kconfig_path)

		self.assertEqual(actual_kconfig.entries(),
				 expected_kconfig.entries())

class KUnitParserTest(unittest.TestCase):

	def assertContains(self, needle, haystack):
		for line in haystack:
			if needle in line:
				return
		raise AssertionError('"' +
			str(needle) + '" not found in "' + str(haystack) + '"!')

	def test_output_isolated_correctly(self):
		log_path = get_absolute_path(
			'test_data/test_output_isolated_correctly.log')
		file = open(log_path)
		result = kunit_parser.isolate_kunit_output(file.readlines())
		self.assertContains('TAP version 14\n', result)
		self.assertContains('	# Subtest: example', result)
		self.assertContains('	1..2', result)
		self.assertContains('	ok 1 - example_simple_test', result)
		self.assertContains('	ok 2 - example_mock_test', result)
		self.assertContains('ok 1 - example', result)
		file.close()

	def test_parse_successful_test_log(self):
		all_passed_log = get_absolute_path(
			'test_data/test_is_test_passed-all_passed.log')
		file = open(all_passed_log)
		result = kunit_parser.parse_run_tests(file.readlines())
		self.assertEqual(
			kunit_parser.TestStatus.SUCCESS,
			result.status)
		file.close()

	def test_parse_failed_test_log(self):
		failed_log = get_absolute_path(
			'test_data/test_is_test_passed-failure.log')
		file = open(failed_log)
		result = kunit_parser.parse_run_tests(file.readlines())
		self.assertEqual(
			kunit_parser.TestStatus.FAILURE,
			result.status)
		file.close()

	def test_no_tests(self):
		empty_log = get_absolute_path(
			'test_data/test_is_test_passed-no_tests_run.log')
		file = open(empty_log)
		result = kunit_parser.parse_run_tests(
			kunit_parser.isolate_kunit_output(file.readlines()))
		self.assertEqual(0, len(result.suites))
		self.assertEqual(
			kunit_parser.TestStatus.NO_TESTS,
			result.status)
		file.close()

	def test_crashed_test(self):
		crashed_log = get_absolute_path(
			'test_data/test_is_test_passed-crash.log')
		file = open(crashed_log)
		result = kunit_parser.parse_run_tests(file.readlines())
		self.assertEqual(
			kunit_parser.TestStatus.TEST_CRASHED,
			result.status)
		file.close()

class StrContains(str):
	def __eq__(self, other):
		return self in other

class KUnitMainTest(unittest.TestCase):
	def setUp(self):
		path = get_absolute_path('test_data/test_is_test_passed-all_passed.log')
		file = open(path)
		all_passed_log = file.readlines()
		self.print_patch = mock.patch('builtins.print')
		self.print_mock = self.print_patch.start()
		self.linux_source_mock = mock.Mock()
		self.linux_source_mock.build_reconfig = mock.Mock(return_value=True)
		self.linux_source_mock.build_um_kernel = mock.Mock(return_value=True)
		self.linux_source_mock.run_kernel = mock.Mock(return_value=all_passed_log)

	def tearDown(self):
		self.print_patch.stop()
		pass

	def test_run_passes_args_pass(self):
		kunit.main(['run'], self.linux_source_mock)
		assert self.linux_source_mock.build_reconfig.call_count == 1
		assert self.linux_source_mock.run_kernel.call_count == 1
		self.print_mock.assert_any_call(StrContains('Testing complete.'))

	def test_run_passes_args_fail(self):
		self.linux_source_mock.run_kernel = mock.Mock(return_value=[])
		with self.assertRaises(SystemExit) as e:
			kunit.main(['run'], self.linux_source_mock)
		assert type(e.exception) == SystemExit
		assert e.exception.code == 1
		assert self.linux_source_mock.build_reconfig.call_count == 1
		assert self.linux_source_mock.run_kernel.call_count == 1
		self.print_mock.assert_any_call(StrContains(' 0 tests run'))

	def test_run_raw_output(self):
		self.linux_source_mock.run_kernel = mock.Mock(return_value=[])
		kunit.main(['run', '--raw_output'], self.linux_source_mock)
		assert self.linux_source_mock.build_reconfig.call_count == 1
		assert self.linux_source_mock.run_kernel.call_count == 1
		for kall in self.print_mock.call_args_list:
			assert kall != mock.call(StrContains('Testing complete.'))
			assert kall != mock.call(StrContains(' 0 tests run'))

	def test_run_timeout(self):
		timeout = 3453
		kunit.main(['run', '--timeout', str(timeout)], self.linux_source_mock)
		assert self.linux_source_mock.build_reconfig.call_count == 1
		self.linux_source_mock.run_kernel.assert_called_once_with(timeout=timeout)
		self.print_mock.assert_any_call(StrContains('Testing complete.'))

if __name__ == '__main__':
	unittest.main()
