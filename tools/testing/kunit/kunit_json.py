# SPDX-License-Identifier: GPL-2.0
#
# Generates JSON from KUnit results according to
# KernelCI spec: https://github.com/kernelci/kernelci-doc/wiki/Test-API
#
# Copyright (C) 2020, Google LLC.
# Author: Heidi Fahim <heidifahim@google.com>

from dataclasses import dataclass
import json
from typing import Any, Dict

from kunit_parser import Test, TestStatus

@dataclass
class Metadata:
	"""Stores metadata about this run to include in get_json_result()."""
	arch: str = ''
	def_config: str = ''
	build_dir: str = ''

JsonObj = Dict[str, Any]

_status_map: Dict[TestStatus, str] = {
	TestStatus.SUCCESS: "PASS",
	TestStatus.SKIPPED: "SKIP",
	TestStatus.TEST_CRASHED: "ERROR",
}

def _get_group_json(test: Test, common_fields: JsonObj) -> JsonObj:
	sub_groups = []  # List[JsonObj]
	test_cases = []  # List[JsonObj]

	for subtest in test.subtests:
		if subtest.subtests:
			sub_group = _get_group_json(subtest, common_fields)
			sub_groups.append(sub_group)
			continue
		status = _status_map.get(subtest.status, "FAIL")
		test_cases.append({"name": subtest.name, "status": status})

	test_counts = test.counts
	counts_json = {
		"tests": test_counts.total(),
		"passed": test_counts.passed,
		"failed": test_counts.failed,
		"crashed": test_counts.crashed,
		"skipped": test_counts.skipped,
		"errors": test_counts.errors,
	}
	test_group = {
		"name": test.name,
		"sub_groups": sub_groups,
		"test_cases": test_cases,
		"misc": counts_json
	}
	test_group.update(common_fields)
	return test_group

def get_json_result(test: Test, metadata: Metadata) -> str:
	common_fields = {
		"arch": metadata.arch,
		"defconfig": metadata.def_config,
		"build_environment": metadata.build_dir,
		"lab_name": None,
		"kernel": None,
		"job": None,
		"git_branch": "kselftest",
	}

	test_group = _get_group_json(test, common_fields)
	test_group["name"] = "KUnit Test Group"
	return json.dumps(test_group, indent=4)
