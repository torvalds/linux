#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025 Intel Corporation
#
# Test for indirect target selection (ITS) mitigation.
#
# Tests if the RETs are correctly patched by evaluating the
# vmlinux .return_sites in /proc/kcore.
#
# Install dependencies
# add-apt-repository ppa:michel-slm/kernel-utils
# apt update
# apt install -y python3-drgn python3-pyelftools python3-capstone
#
# Run on target machine
# mkdir -p /usr/lib/debug/lib/modules/$(uname -r)
# cp $VMLINUX /usr/lib/debug/lib/modules/$(uname -r)/vmlinux
#
# Usage: ./its_ret_alignment.py

import os, sys, argparse
from pathlib import Path

this_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, this_dir + '/../../kselftest')
import ksft
import common as c

bug = "indirect_target_selection"
mitigation = c.get_sysfs(bug)
if not mitigation or "Aligned branch/return thunks" not in mitigation:
    ksft.test_result_skip("Skipping its_ret_alignment.py: Aligned branch/return thunks not enabled")
    ksft.finished()

c.check_dependencies_or_skip(['drgn', 'elftools', 'capstone'], script_name="its_ret_alignment.py")

from elftools.elf.elffile import ELFFile
from drgn.helpers.common.memory import identify_address

cap = c.init_capstone()

if len(os.sys.argv) > 1:
    arg_vmlinux = os.sys.argv[1]
    if not os.path.exists(arg_vmlinux):
        ksft.test_result_fail(f"its_ret_alignment.py: vmlinux not found at user-supplied path: {arg_vmlinux}")
        ksft.exit_fail()
    os.makedirs(f"/usr/lib/debug/lib/modules/{os.uname().release}", exist_ok=True)
    os.system(f'cp {arg_vmlinux} /usr/lib/debug/lib/modules/$(uname -r)/vmlinux')

vmlinux = f"/usr/lib/debug/lib/modules/{os.uname().release}/vmlinux"
if not os.path.exists(vmlinux):
    ksft.test_result_fail(f"its_ret_alignment.py: vmlinux not found at {vmlinux}")
    ksft.exit_fail()

ksft.print_msg(f"Using vmlinux: {vmlinux}")

rethunks_start_vmlinux, rethunks_sec_offset, size = c.get_section_info(vmlinux, '.return_sites')
ksft.print_msg(f"vmlinux: Section .return_sites (0x{rethunks_start_vmlinux:x}) found at 0x{rethunks_sec_offset:x} with size 0x{size:x}")

sites_offset = c.get_patch_sites(vmlinux, rethunks_sec_offset, size)
total_rethunk_tests = len(sites_offset)
ksft.print_msg(f"Found {total_rethunk_tests} rethunk sites")

prog = c.get_runtime_kernel()
rethunks_start_kcore = prog.symbol('__return_sites').address
ksft.print_msg(f'kcore: __rethunk_sites: 0x{rethunks_start_kcore:x}')

its_return_thunk = prog.symbol('its_return_thunk').address
ksft.print_msg(f'kcore: its_return_thunk: 0x{its_return_thunk:x}')

tests_passed = 0
tests_failed = 0
tests_unknown = 0
tests_skipped = 0

with open(vmlinux, 'rb') as f:
    elffile = ELFFile(f)
    text_section = elffile.get_section_by_name('.text')

    for i in range(len(sites_offset)):
        site = rethunks_start_kcore + sites_offset[i]
        vmlinux_site = rethunks_start_vmlinux + sites_offset[i]
        try:
            passed = unknown = failed = skipped = False

            symbol = identify_address(prog, site)
            vmlinux_insn = c.get_instruction_from_vmlinux(elffile, text_section, text_section['sh_addr'], vmlinux_site)
            kcore_insn = list(cap.disasm(prog.read(site, 16), site))[0]

            insn_end = site + kcore_insn.size - 1

            safe_site = insn_end & 0x20
            site_status = "" if safe_site else "(unsafe)"

            ksft.print_msg(f"\nSite {i}: {symbol} <0x{site:x}> {site_status}")
            ksft.print_msg(f"\tvmlinux: 0x{vmlinux_insn.address:x}:\t{vmlinux_insn.mnemonic}\t{vmlinux_insn.op_str}")
            ksft.print_msg(f"\tkcore:   0x{kcore_insn.address:x}:\t{kcore_insn.mnemonic}\t{kcore_insn.op_str}")

            if safe_site:
                tests_passed += 1
                passed = True
                ksft.print_msg(f"\tPASSED: At safe address")
                continue

            if "jmp" in kcore_insn.mnemonic:
                passed = True
            elif "ret" not in kcore_insn.mnemonic:
                skipped = True

            if passed:
                ksft.print_msg(f"\tPASSED: Found {kcore_insn.mnemonic} {kcore_insn.op_str}")
                tests_passed += 1
            elif skipped:
                ksft.print_msg(f"\tSKIPPED: Found '{kcore_insn.mnemonic}'")
                tests_skipped += 1
            elif unknown:
                ksft.print_msg(f"UNKNOWN: An unknown instruction: {kcore_insn}")
                tests_unknown += 1
            else:
                ksft.print_msg(f'\t************* FAILED *************')
                ksft.print_msg(f"\tFound {kcore_insn.mnemonic} {kcore_insn.op_str}")
                ksft.print_msg(f'\t**********************************')
                tests_failed += 1
        except Exception as e:
            ksft.print_msg(f"UNKNOWN: An unexpected error occurred: {e}")
            tests_unknown += 1

ksft.print_msg(f"\n\nSummary:")
ksft.print_msg(f"PASSED: \t{tests_passed} \t/ {total_rethunk_tests}")
ksft.print_msg(f"FAILED: \t{tests_failed} \t/ {total_rethunk_tests}")
ksft.print_msg(f"SKIPPED: \t{tests_skipped} \t/ {total_rethunk_tests}")
ksft.print_msg(f"UNKNOWN: \t{tests_unknown} \t/ {total_rethunk_tests}")

if tests_failed == 0:
    ksft.test_result_pass("All ITS return thunk sites passed.")
else:
    ksft.test_result_fail(f"{tests_failed} failed sites need ITS return thunks.")
ksft.finished()
