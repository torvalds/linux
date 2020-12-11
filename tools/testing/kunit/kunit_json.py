# SPDX-License-Identifier: GPL-2.0
#
# Generates JSON from KUnit results according to
# KernelCI spec: https://github.com/kernelci/kernelci-doc/wiki/Test-API
#
# Copyright (C) 2020, Google LLC.
# Author: Heidi Fahim <heidifahim@google.com>

import json
import os

import kunit_parser

from kunit_parser import TestStatus

def get_json_result(test_result, def_config, build_dir, json_path):
	sub_groups = []

	# Each test suite is mapped to a KernelCI sub_group
	for test_suite in test_result.suites:
		sub_group = {
			"name": test_suite.name,
			"arch": "UM",
			"defconfig": def_config,
			"build_environment": build_dir,
			"test_cases": [],
			"lab_name": None,
			"kernel": None,
			"job": None,
			"git_branch": "kselftest",
		}
		test_cases = []
		# TODO: Add attachments attribute in test_case with detailed
		#  failure message, see https://api.kernelci.org/schema-test-case.html#get
		for case in test_suite.cases:
			test_case = {"name": case.name, "status": "FAIL"}
			if case.status == TestStatus.SUCCESS:
				test_case["status"] = "PASS"
			elif case.status == TestStatus.TEST_CRASHED:
				test_case["status"] = "ERROR"
			test_cases.append(test_case)
		sub_group["test_cases"] = test_cases
		sub_groups.append(sub_group)
	test_group = {
		"name": "KUnit Test Group",
		"arch": "UM",
		"defconfig": def_config,
		"build_environment": build_dir,
		"sub_groups": sub_groups,
		"lab_name": None,
		"kernel": None,
		"job": None,
		"git_branch": "kselftest",
	}
	json_obj = json.dumps(test_group, indent=4)
	if json_path != 'stdout':
		with open(json_path, 'w') as result_path:
			result_path.write(json_obj)
		root = __file__.split('tools/testing/kunit/')[0]
		kunit_parser.print_with_timestamp(
			"Test results stored in %s" %
			os.path.join(root, result_path.name))
	return json_obj
