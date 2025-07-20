#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025 Intel Corporation
#
# Test for indirect target selection (ITS) cmdline permutations with other bugs
# like spectre_v2 and retbleed.

import os, sys, subprocess, itertools, re, shutil

test_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, test_dir + '/../../kselftest')
import ksft
import common as c

bug = "indirect_target_selection"
mitigation = c.get_sysfs(bug)

if not mitigation or "Not affected" in mitigation:
    ksft.test_result_skip("Skipping its_permutations.py: not applicable")
    ksft.finished()

if shutil.which('vng') is None:
    ksft.test_result_skip("Skipping its_permutations.py: virtme-ng ('vng') not found in PATH.")
    ksft.finished()

TEST = f"{test_dir}/its_sysfs.py"
default_kparam = ['clearcpuid=hypervisor', 'panic=5', 'panic_on_warn=1', 'oops=panic', 'nmi_watchdog=1', 'hung_task_panic=1']

DEBUG = " -v "

# Install dependencies
# https://github.com/arighi/virtme-ng
# apt install virtme-ng
BOOT_CMD = f"vng --run {test_dir}/../../../../../arch/x86/boot/bzImage "
#BOOT_CMD += DEBUG

bug = "indirect_target_selection"

input_options = {
    'indirect_target_selection'     : ['off', 'on', 'stuff', 'vmexit'],
    'retbleed'                      : ['off', 'stuff', 'auto'],
    'spectre_v2'                    : ['off', 'on', 'eibrs', 'retpoline', 'ibrs', 'eibrs,retpoline'],
}

def pretty_print(output):
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

    # Define patterns and their corresponding colors
    patterns = {
        r"^ok \d+": OKGREEN,
        r"^not ok \d+": FAIL,
        r"^# Testing .*": OKBLUE,
        r"^# Found: .*": WARNING,
        r"^# Totals: .*": BOLD,
        r"pass:([1-9]\d*)": OKGREEN,
        r"fail:([1-9]\d*)": FAIL,
        r"skip:([1-9]\d*)": WARNING,
    }

    # Apply colors based on patterns
    for pattern, color in patterns.items():
        output = re.sub(pattern, lambda match: f"{color}{match.group(0)}{ENDC}", output, flags=re.MULTILINE)

    print(output)

combinations = list(itertools.product(*input_options.values()))
ksft.print_header()
ksft.set_plan(len(combinations))

logs = ""

for combination in combinations:
    append = ""
    log = ""
    for p in default_kparam:
        append += f' --append={p}'
    command = BOOT_CMD + append
    test_params = ""
    for i, key in enumerate(input_options.keys()):
        param = f'{key}={combination[i]}'
        test_params += f' {param}'
        command += f" --append={param}"
    command += f" -- {TEST}"
    test_name = f"{bug} {test_params}"
    pretty_print(f'# Testing {test_name}')
    t =  subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    t.wait()
    output, _ = t.communicate()
    if t.returncode == 0:
        ksft.test_result_pass(test_name)
    else:
        ksft.test_result_fail(test_name)
    output = output.decode()
    log += f" {output}"
    pretty_print(log)
    logs += output + "\n"

# Optionally use tappy to parse the output
# apt install python3-tappy
with open("logs.txt", "w") as f:
    f.write(logs)

ksft.finished()
