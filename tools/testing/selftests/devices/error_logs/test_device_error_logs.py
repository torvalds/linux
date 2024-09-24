#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2024 Collabora Ltd
#
# This test checks for the presence of error (or more critical) log messages
# coming from devices in the kernel log.
#
# One failed test case is reported for each device that has outputted error
# logs. Devices with no errors do not produce a passing test case to avoid
# polluting the results, therefore a successful run will list 0 tests run.
#

import glob
import os
import re
import sys

# Allow ksft module to be imported from different directory
this_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(this_dir, "../../kselftest/"))

import ksft

kmsg = "/dev/kmsg"

RE_log = re.compile(
    r"(?P<prefix>[0-9]+),(?P<sequence>[0-9]+),(?P<timestamp>[0-9]+),(?P<flag>[^;]*)(,[^;]*)*;(?P<message>.*)"
)
RE_tag = re.compile(r" (?P<key>[^=]+)=(?P<value>.*)")

PREFIX_ERROR = 3

logs = []
error_log_per_device = {}


def parse_kmsg():
    current_log = {}

    with open(kmsg) as f:
        os.set_blocking(f.fileno(), False)

        for line in f:
            tag_line = RE_tag.match(line)
            log_line = RE_log.match(line)

            if log_line:
                if current_log:
                    logs.append(current_log)  # Save last log

                current_log = {
                    "prefix": int(log_line.group("prefix")),
                    "sequence": int(log_line.group("sequence")),
                    "timestamp": int(log_line.group("timestamp")),
                    "flag": log_line.group("flag"),
                    "message": log_line.group("message"),
                }
            elif tag_line:
                current_log[tag_line.group("key")] = tag_line.group("value")


def generate_per_device_error_log():
    for log in logs:
        if log.get("DEVICE") and log["prefix"] <= PREFIX_ERROR:
            if not error_log_per_device.get(log["DEVICE"]):
                error_log_per_device[log["DEVICE"]] = []
            error_log_per_device[log["DEVICE"]].append(log)


parse_kmsg()

generate_per_device_error_log()
num_tests = len(error_log_per_device)

ksft.print_header()
ksft.set_plan(num_tests)

for device in error_log_per_device:
    for log in error_log_per_device[device]:
        ksft.print_msg(log["message"])
    ksft.test_result_fail(device)
if num_tests == 0:
    ksft.print_msg("No device error logs found")
ksft.finished()
