#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025 Intel Corporation
#
# Test for Indirect Target Selection(ITS) mitigation sysfs status.

import sys, os, re
this_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, this_dir + '/../../kselftest')
import ksft

from common import *

bug = "indirect_target_selection"
mitigation = get_sysfs(bug)

ITS_MITIGATION_ALIGNED_THUNKS	= "Mitigation: Aligned branch/return thunks"
ITS_MITIGATION_RETPOLINE_STUFF	= "Mitigation: Retpolines, Stuffing RSB"
ITS_MITIGATION_VMEXIT_ONLY		= "Mitigation: Vulnerable, KVM: Not affected"
ITS_MITIGATION_VULNERABLE       = "Vulnerable"

def check_mitigation():
    if mitigation == ITS_MITIGATION_ALIGNED_THUNKS:
        if cmdline_has(f'{bug}=stuff') and sysfs_has("spectre_v2", "Retpolines"):
            bug_check_fail(bug, ITS_MITIGATION_ALIGNED_THUNKS, ITS_MITIGATION_RETPOLINE_STUFF)
            return
        if cmdline_has(f'{bug}=vmexit') and cpuinfo_has('its_native_only'):
            bug_check_fail(bug, ITS_MITIGATION_ALIGNED_THUNKS, ITS_MITIGATION_VMEXIT_ONLY)
            return
        bug_check_pass(bug, ITS_MITIGATION_ALIGNED_THUNKS)
        return

    if mitigation == ITS_MITIGATION_RETPOLINE_STUFF:
        if cmdline_has(f'{bug}=stuff') and sysfs_has("spectre_v2", "Retpolines"):
            bug_check_pass(bug, ITS_MITIGATION_RETPOLINE_STUFF)
            return
        if sysfs_has('retbleed', 'Stuffing'):
            bug_check_pass(bug, ITS_MITIGATION_RETPOLINE_STUFF)
            return
        bug_check_fail(bug, ITS_MITIGATION_RETPOLINE_STUFF, ITS_MITIGATION_ALIGNED_THUNKS)

    if mitigation == ITS_MITIGATION_VMEXIT_ONLY:
        if cmdline_has(f'{bug}=vmexit') and cpuinfo_has('its_native_only'):
            bug_check_pass(bug, ITS_MITIGATION_VMEXIT_ONLY)
            return
        bug_check_fail(bug, ITS_MITIGATION_VMEXIT_ONLY, ITS_MITIGATION_ALIGNED_THUNKS)

    if mitigation == ITS_MITIGATION_VULNERABLE:
        if sysfs_has("spectre_v2", "Vulnerable"):
            bug_check_pass(bug, ITS_MITIGATION_VULNERABLE)
        else:
            bug_check_fail(bug, "Mitigation", ITS_MITIGATION_VULNERABLE)

    bug_status_unknown(bug, mitigation)
    return

ksft.print_header()
ksft.set_plan(1)
ksft.print_msg(f'{bug}: {mitigation} ...')

if not basic_checks_sufficient(bug, mitigation):
    check_mitigation()

ksft.finished()
