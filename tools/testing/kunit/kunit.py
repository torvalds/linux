#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0
#
# A thin wrapper on top of the KUnit Kernel
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>

import argparse
import sys
import os
import time
import shutil

from collections import namedtuple
from enum import Enum, auto

import kunit_config
import kunit_kernel
import kunit_parser

KunitResult = namedtuple('KunitResult', ['status','result'])

KunitRequest = namedtuple('KunitRequest', ['raw_output','timeout', 'jobs', 'build_dir', 'defconfig'])

KernelDirectoryPath = sys.argv[0].split('tools/testing/kunit/')[0]

class KunitStatus(Enum):
	SUCCESS = auto()
	CONFIG_FAILURE = auto()
	BUILD_FAILURE = auto()
	TEST_FAILURE = auto()

def create_default_kunitconfig():
	if not os.path.exists(kunit_kernel.kunitconfig_path):
		shutil.copyfile('arch/um/configs/kunit_defconfig',
				kunit_kernel.kunitconfig_path)

def get_kernel_root_path():
	parts = sys.argv[0] if not __file__ else __file__
	parts = os.path.realpath(parts).split('tools/testing/kunit')
	if len(parts) != 2:
		sys.exit(1)
	return parts[0]

def run_tests(linux: kunit_kernel.LinuxSourceTree,
	      request: KunitRequest) -> KunitResult:
	config_start = time.time()
	success = linux.build_reconfig(request.build_dir)
	config_end = time.time()
	if not success:
		return KunitResult(KunitStatus.CONFIG_FAILURE, 'could not configure kernel')

	kunit_parser.print_with_timestamp('Building KUnit Kernel ...')

	build_start = time.time()
	success = linux.build_um_kernel(request.jobs, request.build_dir)
	build_end = time.time()
	if not success:
		return KunitResult(KunitStatus.BUILD_FAILURE, 'could not build kernel')

	kunit_parser.print_with_timestamp('Starting KUnit Kernel ...')
	test_start = time.time()

	test_result = kunit_parser.TestResult(kunit_parser.TestStatus.SUCCESS,
					      [],
					      'Tests not Parsed.')
	if request.raw_output:
		kunit_parser.raw_output(
			linux.run_kernel(timeout=request.timeout,
					 build_dir=request.build_dir))
	else:
		kunit_output = linux.run_kernel(timeout=request.timeout,
						build_dir=request.build_dir)
		test_result = kunit_parser.parse_run_tests(kunit_output)
	test_end = time.time()

	kunit_parser.print_with_timestamp((
		'Elapsed time: %.3fs total, %.3fs configuring, %.3fs ' +
		'building, %.3fs running\n') % (
				test_end - config_start,
				config_end - config_start,
				build_end - build_start,
				test_end - test_start))

	if test_result.status != kunit_parser.TestStatus.SUCCESS:
		return KunitResult(KunitStatus.TEST_FAILURE, test_result)
	else:
		return KunitResult(KunitStatus.SUCCESS, test_result)

def main(argv, linux=None):
	parser = argparse.ArgumentParser(
			description='Helps writing and running KUnit tests.')
	subparser = parser.add_subparsers(dest='subcommand')

	run_parser = subparser.add_parser('run', help='Runs KUnit tests.')
	run_parser.add_argument('--raw_output', help='don\'t format output from kernel',
				action='store_true')

	run_parser.add_argument('--timeout',
				help='maximum number of seconds to allow for all tests '
				'to run. This does not include time taken to build the '
				'tests.',
				type=int,
				default=300,
				metavar='timeout')

	run_parser.add_argument('--jobs',
				help='As in the make command, "Specifies  the number of '
				'jobs (commands) to run simultaneously."',
				type=int, default=8, metavar='jobs')

	run_parser.add_argument('--build_dir',
				help='As in the make command, it specifies the build '
				'directory.',
				type=str, default='', metavar='build_dir')

	run_parser.add_argument('--defconfig',
				help='Uses a default .kunitconfig.',
				action='store_true')

	cli_args = parser.parse_args(argv)

	if cli_args.subcommand == 'run':
		if get_kernel_root_path():
			os.chdir(get_kernel_root_path())

		if cli_args.build_dir:
			if not os.path.exists(cli_args.build_dir):
				os.mkdir(cli_args.build_dir)
			kunit_kernel.kunitconfig_path = os.path.join(
				cli_args.build_dir,
				kunit_kernel.kunitconfig_path)

		if cli_args.defconfig:
			create_default_kunitconfig()

		if not linux:
			linux = kunit_kernel.LinuxSourceTree()

		request = KunitRequest(cli_args.raw_output,
				       cli_args.timeout,
				       cli_args.jobs,
				       cli_args.build_dir,
				       cli_args.defconfig)
		result = run_tests(linux, request)
		if result.status != KunitStatus.SUCCESS:
			sys.exit(1)
	else:
		parser.print_help()

if __name__ == '__main__':
	main(sys.argv[1:])
