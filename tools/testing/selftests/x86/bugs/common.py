#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025 Intel Corporation
#
# This contains kselftest framework adapted common functions for testing
# mitigation for x86 bugs.

import os, sys, re, shutil

sys.path.insert(0, '../../kselftest')
import ksft

def read_file(path):
    if not os.path.exists(path):
        return None
    with open(path, 'r') as file:
        return file.read().strip()

def cpuinfo_has(arg):
    cpuinfo = read_file('/proc/cpuinfo')
    if arg in cpuinfo:
        return True
    return False

def cmdline_has(arg):
    cmdline = read_file('/proc/cmdline')
    if arg in cmdline:
        return True
    return False

def cmdline_has_either(args):
    cmdline = read_file('/proc/cmdline')
    for arg in args:
        if arg in cmdline:
            return True
    return False

def cmdline_has_none(args):
    return not cmdline_has_either(args)

def cmdline_has_all(args):
    cmdline = read_file('/proc/cmdline')
    for arg in args:
        if arg not in cmdline:
            return False
    return True

def get_sysfs(bug):
    return read_file("/sys/devices/system/cpu/vulnerabilities/" + bug)

def sysfs_has(bug, mitigation):
    status = get_sysfs(bug)
    if mitigation in status:
        return True
    return False

def sysfs_has_either(bugs, mitigations):
    for bug in bugs:
        for mitigation in mitigations:
            if sysfs_has(bug, mitigation):
                return True
    return False

def sysfs_has_none(bugs, mitigations):
    return not sysfs_has_either(bugs, mitigations)

def sysfs_has_all(bugs, mitigations):
    for bug in bugs:
        for mitigation in mitigations:
            if not sysfs_has(bug, mitigation):
                return False
    return True

def bug_check_pass(bug, found):
    ksft.print_msg(f"\nFound: {found}")
    # ksft.print_msg(f"\ncmdline: {read_file('/proc/cmdline')}")
    ksft.test_result_pass(f'{bug}: {found}')

def bug_check_fail(bug, found, expected):
    ksft.print_msg(f'\nFound:\t {found}')
    ksft.print_msg(f'Expected:\t {expected}')
    ksft.print_msg(f"\ncmdline: {read_file('/proc/cmdline')}")
    ksft.test_result_fail(f'{bug}: {found}')

def bug_status_unknown(bug, found):
    ksft.print_msg(f'\nUnknown status: {found}')
    ksft.print_msg(f"\ncmdline: {read_file('/proc/cmdline')}")
    ksft.test_result_fail(f'{bug}: {found}')

def basic_checks_sufficient(bug, mitigation):
    if not mitigation:
        bug_status_unknown(bug, "None")
        return True
    elif mitigation == "Not affected":
        ksft.test_result_pass(bug)
        return True
    elif mitigation == "Vulnerable":
        if cmdline_has_either([f'{bug}=off', 'mitigations=off']):
            bug_check_pass(bug, mitigation)
            return True
    return False

def get_section_info(vmlinux, section_name):
    from elftools.elf.elffile import ELFFile
    with open(vmlinux, 'rb') as f:
        elffile = ELFFile(f)
        section = elffile.get_section_by_name(section_name)
        if section is None:
            ksft.print_msg("Available sections in vmlinux:")
            for sec in elffile.iter_sections():
                ksft.print_msg(sec.name)
            raise ValueError(f"Section {section_name} not found in {vmlinux}")
        return section['sh_addr'], section['sh_offset'], section['sh_size']

def get_patch_sites(vmlinux, offset, size):
    import struct
    output = []
    with open(vmlinux, 'rb') as f:
        f.seek(offset)
        i = 0
        while i < size:
            data = f.read(4)  # s32
            if not data:
                break
            sym_offset = struct.unpack('<i', data)[0] + i
            i += 4
            output.append(sym_offset)
    return output

def get_instruction_from_vmlinux(elffile, section, virtual_address, target_address):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    section_start = section['sh_addr']
    section_end = section_start + section['sh_size']

    if not (section_start <= target_address < section_end):
        return None

    offset = target_address - section_start
    code = section.data()[offset:offset + 16]

    cap = init_capstone()
    for instruction in cap.disasm(code, target_address):
        if instruction.address == target_address:
            return instruction
    return None

def init_capstone():
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OPT_SYNTAX_ATT
    cap = Cs(CS_ARCH_X86, CS_MODE_64)
    cap.syntax = CS_OPT_SYNTAX_ATT
    return cap

def get_runtime_kernel():
    import drgn
    return drgn.program_from_kernel()

def check_dependencies_or_skip(modules, script_name="unknown test"):
    for mod in modules:
        try:
            __import__(mod)
        except ImportError:
            ksft.test_result_skip(f"Skipping {script_name}: missing module '{mod}'")
            ksft.finished()
