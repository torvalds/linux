# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import libear
import unittest

import os.path
import subprocess
import json


def run(source_dir, target_dir):
    def execute(cmd):
        return subprocess.check_call(
            cmd, cwd=target_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )

    execute(["cmake", source_dir])
    execute(["make"])

    result_file = os.path.join(target_dir, "result.json")
    expected_file = os.path.join(target_dir, "expected.json")
    execute(["intercept-build", "--cdb", result_file, "./exec", expected_file])
    return (expected_file, result_file)


class ExecAnatomyTest(unittest.TestCase):
    def assertEqualJson(self, expected, result):
        def read_json(filename):
            with open(filename) as handler:
                return json.load(handler)

        lhs = read_json(expected)
        rhs = read_json(result)
        for item in lhs:
            self.assertTrue(rhs.count(item))
        for item in rhs:
            self.assertTrue(lhs.count(item))

    def test_all_exec_calls(self):
        this_dir, _ = os.path.split(__file__)
        source_dir = os.path.abspath(os.path.join(this_dir, "..", "exec"))
        with libear.TemporaryDirectory() as tmp_dir:
            expected, result = run(source_dir, tmp_dir)
            self.assertEqualJson(expected, result)
