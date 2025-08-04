#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025 Intel Corporation
#
# Test for indirect target selection (ITS) mitigation.
#
# Test if indirect CALL/JMP are correctly patched by evaluating
# the vmlinux .retpoline_sites in /proc/kcore.

# Install dependencies
# add-apt-repository ppa:michel-slm/kernel-utils
# apt update
# apt install -y python3-drgn python3-pyelftools python3-capstone
#
# Best to copy the vmlinux at a standard location:
# mkdir -p /usr/lib/debug/lib/modules/$(uname -r)
# cp $VMLINUX /usr/lib/debug/lib/modules/$(uname -r)/vmlinux
#
# Usage: ./its_indirect_alignment.py [vmlinux]

import os, sys, argparse
from pathlib import Path

this_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, this_dir + '/../../kselftest')
import ksft
import common as c

bug = "indirect_target_selection"

mitigation = c.get_sysfs(bug)
if not mitigation or "Aligned branch/return thunks" not in mitigation:
    ksft.test_result_skip("Skipping its_indirect_alignment.py: Aligned branch/return thunks not enabled")
    ksft.finished()

if c.sysfs_has("spectre_v2", "Retpolines"):
    ksft.test_result_skip("Skipping its_indirect_alignment.py: Retpolines deployed")
    ksft.finished()

c.check_dependencies_or_skip(['drgn', 'elftools', 'capstone'], script_name="its_indirect_alignment.py")

from elftools.elf.elffile import ELFFile
from drgn.helpers.common.memory import identify_address

cap = c.init_capstone()

if len(os.sys.argv) > 1:
    arg_vmlinux = os.sys.argv[1]
    if not os.path.exists(arg_vmlinux):
        ksft.test_result_fail(f"its_indirect_alignment.py: vmlinux not found at argument path: {arg_vmlinux}")
        ksft.exit_fail()
    os.makedirs(f"/usr/lib/debug/lib/modules/{os.uname().release}", exist_ok=True)
    os.system(f'cp {arg_vmlinux} /usr/lib/debug/lib/modules/$(uname -r)/vmlinux')

vmlinux = f"/usr/lib/debug/lib/modules/{os.uname().release}/vmlinux"
if not os.path.exists(vmlinux):
    ksft.test_result_fail(f"its_indirect_alignment.py: vmlinux not found at {vmlinux}")
    ksft.exit_fail()

ksft.print_msg(f"Using vmlinux: {vmlinux}")

retpolines_start_vmlinux, retpolines_sec_offset, size = c.get_section_info(vmlinux, '.retpoline_sites')
ksft.print_msg(f"vmlinux: Section .retpoline_sites (0x{retpolines_start_vmlinux:x}) found at 0x{retpolines_sec_offset:x} with size 0x{size:x}")

sites_offset = c.get_patch_sites(vmlinux, retpolines_sec_offset, size)
total_retpoline_tests = len(sites_offset)
ksft.print_msg(f"Found {total_retpoline_tests} retpoline sites")

prog = c.get_runtime_kernel()
retpolines_start_kcore = prog.symbol('__retpoline_sites').address
ksft.print_msg(f'kcore: __retpoline_sites: 0x{retpolines_start_kcore:x}')

x86_indirect_its_thunk_r15 = prog.symbol('__x86_indirect_its_thunk_r15').address
ksft.print_msg(f'kcore: __x86_indirect_its_thunk_r15: 0x{x86_indirect_its_thunk_r15:x}')

tests_passed = 0
tests_failed = 0
tests_unknown = 0

with open(vmlinux, 'rb') as f:
    elffile = ELFFile(f)
    text_section = elffile.get_section_by_name('.text')

    for i in range(0, len(sites_offset)):
        site = retpolines_start_kcore + sites_offset[i]
        vmlinux_site = retpolines_start_vmlinux + sites_offset[i]
        passed = unknown = failed = False
        try:
            vmlinux_insn = c.get_instruction_from_vmlinux(elffile, text_section, text_section['sh_addr'], vmlinux_site)
            kcore_insn = list(cap.disasm(prog.read(site, 16), site))[0]
            operand = kcore_insn.op_str
            insn_end = site + kcore_insn.size - 1 # TODO handle Jcc.32 __x86_indirect_thunk_\reg
            safe_site = insn_end & 0x20
            site_status = "" if safe_site else "(unsafe)"

            ksft.print_msg(f"\nSite {i}: {identify_address(prog, site)} <0x{site:x}> {site_status}")
            ksft.print_msg(f"\tvmlinux: 0x{vmlinux_insn.address:x}:\t{vmlinux_insn.mnemonic}\t{vmlinux_insn.op_str}")
            ksft.print_msg(f"\tkcore:   0x{kcore_insn.address:x}:\t{kcore_insn.mnemonic}\t{kcore_insn.op_str}")

            if (site & 0x20) ^ (insn_end & 0x20):
                ksft.print_msg(f"\tSite at safe/unsafe boundary: {str(kcore_insn.bytes)} {kcore_insn.mnemonic} {operand}")
            if safe_site:
                tests_passed += 1
                passed = True
                ksft.print_msg(f"\tPASSED: At safe address")
                continue

            if operand.startswith('0xffffffff'):
                thunk = int(operand, 16)
                if thunk > x86_indirect_its_thunk_r15:
                    insn_at_thunk = list(cap.disasm(prog.read(thunk, 16), thunk))[0]
                    operand += ' -> ' + insn_at_thunk.mnemonic + ' ' + insn_at_thunk.op_str + ' <dynamic-thunk?>'
                    if 'jmp' in insn_at_thunk.mnemonic and thunk & 0x20:
                        ksft.print_msg(f"\tPASSED: Found {operand} at safe address")
                        passed = True
                if not passed:
                    if kcore_insn.operands[0].type == capstone.CS_OP_IMM:
                        operand += ' <' + prog.symbol(int(operand, 16)) + '>'
                        if '__x86_indirect_its_thunk_' in operand:
                            ksft.print_msg(f"\tPASSED: Found {operand}")
                        else:
                            ksft.print_msg(f"\tPASSED: Found direct branch: {kcore_insn}, ITS thunk not required.")
                        passed = True
                    else:
                        unknown = True
            if passed:
                tests_passed += 1
            elif unknown:
                ksft.print_msg(f"UNKNOWN: unexpected operand: {kcore_insn}")
                tests_unknown += 1
            else:
                ksft.print_msg(f'\t************* FAILED *************')
                ksft.print_msg(f"\tFound {kcore_insn.bytes} {kcore_insn.mnemonic} {operand}")
                ksft.print_msg(f'\t**********************************')
                tests_failed += 1
        except Exception as e:
            ksft.print_msg(f"UNKNOWN: An unexpected error occurred: {e}")
            tests_unknown += 1

ksft.print_msg(f"\n\nSummary:")
ksft.print_msg(f"PASS:    \t{tests_passed} \t/ {total_retpoline_tests}")
ksft.print_msg(f"FAIL:    \t{tests_failed} \t/ {total_retpoline_tests}")
ksft.print_msg(f"UNKNOWN: \t{tests_unknown} \t/ {total_retpoline_tests}")

if tests_failed == 0:
    ksft.test_result_pass("All ITS return thunk sites passed")
else:
    ksft.test_result_fail(f"{tests_failed} ITS return thunk sites failed")
ksft.finished()
