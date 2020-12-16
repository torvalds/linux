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

from collections import namedtuple
from enum import Enum, auto

import kunit_config
import kunit_json
import kunit_kernel
import kunit_parser

KunitResult = namedtuple('KunitResult', ['status','result','elapsed_time'])

KunitConfigRequest = namedtuple('KunitConfigRequest',
				['build_dir', 'make_options'])
KunitBuildRequest = namedtuple('KunitBuildRequest',
			       ['jobs', 'build_dir', 'alltests',
				'make_options'])
KunitExecRequest = namedtuple('KunitExecRequest',
			      ['timeout', 'build_dir', 'alltests'])
KunitParseRequest = namedtuple('KunitParseRequest',
			       ['raw_output', 'input_data', 'build_dir', 'json'])
KunitRequest = namedtuple('KunitRequest', ['raw_output','timeout', 'jobs',
					   'build_dir', 'alltests', 'json',
					   'make_options'])

KernelDirectoryPath = sys.argv[0].split('tools/testing/kunit/')[0]

class KunitStatus(Enum):
	SUCCESS = auto()
	CONFIG_FAILURE = auto()
	BUILD_FAILURE = auto()
	TEST_FAILURE = auto()

def get_kernel_root_path():
	parts = sys.argv[0] if not __file__ else __file__
	parts = os.path.realpath(parts).split('tools/testing/kunit')
	if len(parts) != 2:
		sys.exit(1)
	return parts[0]

def config_tests(linux: kunit_kernel.LinuxSourceTree,
		 request: KunitConfigRequest) -> KunitResult:
	kunit_parser.print_with_timestamp('Configuring KUnit Kernel ...')

	config_start = time.time()
	success = linux.build_reconfig(request.build_dir, request.make_options)
	config_end = time.time()
	if not success:
		return KunitResult(KunitStatus.CONFIG_FAILURE,
				   'could not configure kernel',
				   config_end - config_start)
	return KunitResult(KunitStatus.SUCCESS,
			   'configured kernel successfully',
			   config_end - config_start)

def build_tests(linux: kunit_kernel.LinuxSourceTree,
		request: KunitBuildRequest) -> KunitResult:
	kunit_parser.print_with_timestamp('Building KUnit Kernel ...')

	build_start = time.time()
	success = linux.build_um_kernel(request.alltests,
					request.jobs,
					request.build_dir,
					request.make_options)
	build_end = time.time()
	if not success:
		return KunitResult(KunitStatus.BUILD_FAILURE,
				   'could not build kernel',
				   build_end - build_start)
	if not success:
		return KunitResult(KunitStatus.BUILD_FAILURE,
				   'could not build kernel',
				   build_end - build_start)
	return KunitResult(KunitStatus.SUCCESS,
			   'built kernel successfully',
			   build_end - build_start)

def exec_tests(linux: kunit_kernel.LinuxSourceTree,
	       request: KunitExecRequest) -> KunitResult:
	kunit_parser.print_with_timestamp('Starting KUnit Kernel ...')
	test_start = time.time()
	result = linux.run_kernel(
		timeout=None if request.alltests else request.timeout,
		build_dir=request.build_dir)

	test_end = time.time()

	return KunitResult(KunitStatus.SUCCESS,
			   result,
			   test_end - test_start)

def parse_tests(request: KunitParseRequest) -> KunitResult:
	parse_start = time.time()

	test_result = kunit_parser.TestResult(kunit_parser.TestStatus.SUCCESS,
					      [],
					      'Tests not Parsed.')

	if request.raw_output:
		kunit_parser.raw_output(request.input_data)
	else:
		test_result = kunit_parser.parse_run_tests(request.input_data)
	parse_end = time.time()

	if request.json:
		json_obj = kunit_json.get_json_result(
					test_result=test_result,
					def_config='kunit_defconfig',
					build_dir=request.build_dir,
					json_path=request.json)
		if request.json == 'stdout':
			print(json_obj)

	if test_result.status != kunit_parser.TestStatus.SUCCESS:
		return KunitResult(KunitStatus.TEST_FAILURE, test_result,
				   parse_end - parse_start)

	return KunitResult(KunitStatus.SUCCESS, test_result,
				parse_end - parse_start)


def run_tests(linux: kunit_kernel.LinuxSourceTree,
	      request: KunitRequest) -> KunitResult:
	run_start = time.time()

	config_request = KunitConfigRequest(request.build_dir,
					    request.make_options)
	config_result = config_tests(linux, config_request)
	if config_result.status != KunitStatus.SUCCESS:
		return config_result

	build_request = KunitBuildRequest(request.jobs, request.build_dir,
					  request.alltests,
					  request.make_options)
	build_result = build_tests(linux, build_request)
	if build_result.status != KunitStatus.SUCCESS:
		return build_result

	exec_request = KunitExecRequest(request.timeout, request.build_dir,
					request.alltests)
	exec_result = exec_tests(linux, exec_request)
	if exec_result.status != KunitStatus.SUCCESS:
		return exec_result

	parse_request = KunitParseRequest(request.raw_output,
					  exec_result.result,
					  request.build_dir,
					  request.json)
	parse_result = parse_tests(parse_request)

	run_end = time.time()

	kunit_parser.print_with_timestamp((
		'Elapsed time: %.3fs total, %.3fs configuring, %.3fs ' +
		'building, %.3fs running\n') % (
				run_end - run_start,
				config_result.elapsed_time,
				build_result.elapsed_time,
				exec_result.elapsed_time))
	return parse_result

def add_common_opts(parser):
	parser.add_argument('--build_dir',
			    help='As in the make command, it specifies the build '
			    'directory.',
                            type=str, default='.kunit', metavar='build_dir')
	parser.add_argument('--make_options',
			    help='X=Y make option, can be repeated.',
			    action='append')
	parser.add_argument('--alltests',
			    help='Run all KUnit tests through allyesconfig',
			    action='store_true')

def add_build_opts(parser):
	parser.add_argument('--jobs',
			    help='As in the make command, "Specifies  the number of '
			    'jobs (commands) to run simultaneously."',
			    type=int, default=8, metavar='jobs')

def add_exec_opts(parser):
	parser.add_argument('--timeout',
			    help='maximum number of seconds to allow for all tests '
			    'to run. This does not include time taken to build the '
			    'tests.',
			    type=int,
			    default=300,
			    metavar='timeout')

def add_parse_opts(parser):
	parser.add_argument('--raw_output', help='don\'t format output from kernel',
			    action='store_true')
	parser.add_argument('--json',
			    nargs='?',
			    help='Stores test results in a JSON, and either '
			    'prints to stdout or saves to file if a '
			    'filename is specified',
			    type=str, const='stdout', default=None)

def main(argv, linux=None):
	parser = argparse.ArgumentParser(
			description='Helps writing and running KUnit tests.')
	subparser = parser.add_subparsers(dest='subcommand')

	# The 'run' command will config, build, exec, and parse in one go.
	run_parser = subparser.add_parser('run', help='Runs KUnit tests.')
	add_common_opts(run_parser)
	add_build_opts(run_parser)
	add_exec_opts(run_parser)
	add_parse_opts(run_parser)

	config_parser = subparser.add_parser('config',
						help='Ensures that .config contains all of '
						'the options in .kunitconfig')
	add_common_opts(config_parser)

	build_parser = subparser.add_parser('build', help='Builds a kernel with KUnit tests')
	add_common_opts(build_parser)
	add_build_opts(build_parser)

	exec_parser = subparser.add_parser('exec', help='Run a kernel with KUnit tests')
	add_common_opts(exec_parser)
	add_exec_opts(exec_parser)
	add_parse_opts(exec_parser)

	# The 'parse' option is special, as it doesn't need the kernel source
	# (therefore there is no need for a build_dir, hence no add_common_opts)
	# and the '--file' argument is not relevant to 'run', so isn't in
	# add_parse_opts()
	parse_parser = subparser.add_parser('parse',
					    help='Parses KUnit results from a file, '
					    'and parses formatted results.')
	add_parse_opts(parse_parser)
	parse_parser.add_argument('file',
				  help='Specifies the file to read results from.',
				  type=str, nargs='?', metavar='input_file')

	cli_args = parser.parse_args(argv)

	if get_kernel_root_path():
		os.chdir(get_kernel_root_path())

	if cli_args.subcommand == 'run':
		if not os.path.exists(cli_args.build_dir):
			os.mkdir(cli_args.build_dir)

		if not linux:
			linux = kunit_kernel.LinuxSourceTree()

		linux.create_kunitconfig(cli_args.build_dir)
		linux.read_kunitconfig(cli_args.build_dir)

		request = KunitRequest(cli_args.raw_output,
				       cli_args.timeout,
				       cli_args.jobs,
				       cli_args.build_dir,
				       cli_args.alltests,
				       cli_args.json,
				       cli_args.make_options)
		result = run_tests(linux, request)
		if result.status != KunitStatus.SUCCESS:
			sys.exit(1)
	elif cli_args.subcommand == 'config':
		if cli_args.build_dir and (
				not os.path.exists(cli_args.build_dir)):
			os.mkdir(cli_args.build_dir)

		if not linux:
			linux = kunit_kernel.LinuxSourceTree()

		linux.create_kunitconfig(cli_args.build_dir)
		linux.read_kunitconfig(cli_args.build_dir)

		request = KunitConfigRequest(cli_args.build_dir,
					     cli_args.make_options)
		result = config_tests(linux, request)
		kunit_parser.print_with_timestamp((
			'Elapsed time: %.3fs\n') % (
				result.elapsed_time))
		if result.status != KunitStatus.SUCCESS:
			sys.exit(1)
	elif cli_args.subcommand == 'build':
		if not linux:
			linux = kunit_kernel.LinuxSourceTree()

		linux.create_kunitconfig(cli_args.build_dir)
		linux.read_kunitconfig(cli_args.build_dir)

		request = KunitBuildRequest(cli_args.jobs,
					    cli_args.build_dir,
					    cli_args.alltests,
					    cli_args.make_options)
		result = build_tests(linux, request)
		kunit_parser.print_with_timestamp((
			'Elapsed time: %.3fs\n') % (
				result.elapsed_time))
		if result.status != KunitStatus.SUCCESS:
			sys.exit(1)
	elif cli_args.subcommand == 'exec':
		if not linux:
			linux = kunit_kernel.LinuxSourceTree()

		linux.create_kunitconfig(cli_args.build_dir)
		linux.read_kunitconfig(cli_args.build_dir)

		exec_request = KunitExecRequest(cli_args.timeout,
						cli_args.build_dir,
						cli_args.alltests)
		exec_result = exec_tests(linux, exec_request)
		parse_request = KunitParseRequest(cli_args.raw_output,
						  exec_result.result,
						  cli_args.build_dir,
						  cli_args.json)
		result = parse_tests(parse_request)
		kunit_parser.print_with_timestamp((
			'Elapsed time: %.3fs\n') % (
				exec_result.elapsed_time))
		if result.status != KunitStatus.SUCCESS:
			sys.exit(1)
	elif cli_args.subcommand == 'parse':
		if cli_args.file == None:
			kunit_output = sys.stdin
		else:
			with open(cli_args.file, 'r') as f:
				kunit_output = f.read().splitlines()
		request = KunitParseRequest(cli_args.raw_output,
					    kunit_output,
					    None,
					    cli_args.json)
		result = parse_tests(request)
		if result.status != KunitStatus.SUCCESS:
			sys.exit(1)
	else:
		parser.print_help()

if __name__ == '__main__':
	main(sys.argv[1:])
