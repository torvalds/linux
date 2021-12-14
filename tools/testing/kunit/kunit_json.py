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

from kunit_parser import Test, TestStatus
from typing import Any, Dict, Optional

JsonObj = Dict[str, Any]

def _get_group_json(test: Test, def_config: str,
		build_dir: Optional[str]) -> JsonObj:
	sub_groups = []  # List[JsonObj]
	test_cases = []  # List[JsonObj]

	for subtest in test.subtests:
		if len(subtest.subtests):
			sub_group = _get_group_json(subtest, def_config,
				build_dir)
			sub_groups.append(sub_group)
		else:
			test_case = {"name": subtest.name, "status": "FAIL"}
			if subtest.status == TestStatus.SUCCESS:
				test_case["status"] = "PASS"
			elif subtest.status == TestStatus.SKIPPED:
				test_case["status"] = "SKIP"
			elif subtest.status == TestStatus.TEST_CRASHED:
				test_case["status"] = "ERROR"
			test_cases.append(test_case)

	test_group = {
		"name": test.name,
		"arch": "UM",
		"defconfig": def_config,
		"build_environment": build_dir,
		"sub_groups": sub_groups,
		"test_cases": test_cases,
		"lab_name": None,
		"kernel": None,
		"job": None,
		"git_branch": "kselftest",
	}
	return test_group

def get_json_result(test: Test, def_config: str,
		build_dir: Optional[str], json_path: str) -> str:
	test_group = _get_group_json(test, def_config, build_dir)
	test_group["name"] = "KUnit Test Group"
	json_obj = json.dumps(test_group, indent=4)
	if json_path != 'stdout':
		with open(json_path, 'w') as result_path:
			result_path.write(json_obj)
		root = __file__.split('tools/testing/kunit/')[0]
		kunit_parser.print_with_timestamp(
			"Test results stored in %s" %
			os.path.join(root, result_path.name))
	return json_obj
