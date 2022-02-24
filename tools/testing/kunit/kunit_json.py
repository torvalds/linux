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
from typing import Any, Dict

JsonObj = Dict[str, Any]

_status_map: Dict[TestStatus, str] = {
	TestStatus.SUCCESS: "PASS",
	TestStatus.SKIPPED: "SKIP",
	TestStatus.TEST_CRASHED: "ERROR",
}

def _get_group_json(test: Test, def_config: str, build_dir: str) -> JsonObj:
	sub_groups = []  # List[JsonObj]
	test_cases = []  # List[JsonObj]

	for subtest in test.subtests:
		if subtest.subtests:
			sub_group = _get_group_json(subtest, def_config,
				build_dir)
			sub_groups.append(sub_group)
			continue
		status = _status_map.get(subtest.status, "FAIL")
		test_cases.append({"name": subtest.name, "status": status})

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

def get_json_result(test: Test, def_config: str, build_dir: str) -> str:
	test_group = _get_group_json(test, def_config, build_dir)
	test_group["name"] = "KUnit Test Group"
	return json.dumps(test_group, indent=4)
