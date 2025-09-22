#!/usr/bin/env python
# ===-- armv7_cortex_m_target_definition.py.py ------------------*- C++ -*-===//
#
#                     The LLVM Compiler Infrastructure
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===//

# ----------------------------------------------------------------------
# DESCRIPTION
#
# This file can be used with the following setting:
#   plugin.process.gdb-remote.target-definition-file
# This setting should be used when you are trying to connect to a
# remote GDB server that doesn't support any of the register discovery
# packets that LLDB normally uses.
#
# Why is this necessary? LLDB doesn't require a new build of LLDB that
# targets each new architecture you will debug with. Instead, all
# architectures are supported and LLDB relies on extra GDB server
# packets to discover the target we are connecting to so that is can
# show the right registers for each target. This allows the GDB server
# to change and add new registers without requiring a new LLDB build
# just so we can see new registers.
#
# This file implements the x86_64 registers for the darwin version of
# GDB and allows you to connect to servers that use this register set.
#
# USAGE
#
# (lldb) settings set plugin.process.gdb-remote.target-definition-file /path/to/armv7_cortex_m_target_definition.py
# (lldb) gdb-remote other.baz.com:1234
#
# The target definition file will get used if and only if the
# qRegisterInfo packets are not supported when connecting to a remote
# GDB server.
# ----------------------------------------------------------------------

from lldb import *

# DWARF register numbers
name_to_dwarf_regnum = {
    "r0": 0,
    "r1": 1,
    "r2": 2,
    "r3": 3,
    "r4": 4,
    "r5": 5,
    "r6": 6,
    "r7": 7,
    "r9": 8,
    "r10": 9,
    "r11": 10,
    "r12": 11,
    "sp": 12,
    "lr": 13,
    "pc": 14,
    "r15": 15,
    "xpsr": 16,
}

name_to_generic_regnum = {
    "pc": LLDB_REGNUM_GENERIC_PC,
    "sp": LLDB_REGNUM_GENERIC_SP,
    "r7": LLDB_REGNUM_GENERIC_FP,
    "lr": LLDB_REGNUM_GENERIC_RA,
    "r0": LLDB_REGNUM_GENERIC_ARG1,
    "r1": LLDB_REGNUM_GENERIC_ARG2,
    "r2": LLDB_REGNUM_GENERIC_ARG3,
    "r3": LLDB_REGNUM_GENERIC_ARG4,
}


def get_reg_num(reg_num_dict, reg_name):
    if reg_name in reg_num_dict:
        return reg_num_dict[reg_name]
    return LLDB_INVALID_REGNUM


def get_reg_num(reg_num_dict, reg_name):
    if reg_name in reg_num_dict:
        return reg_num_dict[reg_name]
    return LLDB_INVALID_REGNUM


armv7_register_infos = [
    {
        "name": "r0",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg1",
    },
    {
        "name": "r1",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg2",
    },
    {
        "name": "r2",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg3",
    },
    {
        "name": "r3",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg4",
    },
    {
        "name": "r4",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r5",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r6",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r7",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "fp",
    },
    {
        "name": "r8",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r9",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r10",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r11",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r12",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "sp",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "r13",
    },
    {
        "name": "lr",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "r14",
    },
    {
        "name": "pc",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "r15",
    },
    {
        "name": "xpsr",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "cpsr",
    },
]

g_target_definition = None


def get_target_definition():
    global g_target_definition
    if g_target_definition is None:
        g_target_definition = {}
        offset = 0
        for reg_info in armv7_register_infos:
            reg_name = reg_info["name"]

            if "slice" not in reg_info and "composite" not in reg_info:
                reg_info["offset"] = offset
                offset += reg_info["bitsize"] / 8

            # Set the DWARF/eh_frame register number for this register if it has one
            reg_num = get_reg_num(name_to_dwarf_regnum, reg_name)
            if reg_num != LLDB_INVALID_REGNUM:
                reg_info["gcc"] = reg_num
                reg_info["ehframe"] = reg_num

            # Set the generic register number for this register if it has one
            reg_num = get_reg_num(name_to_generic_regnum, reg_name)
            if reg_num != LLDB_INVALID_REGNUM:
                reg_info["generic"] = reg_num

        g_target_definition["sets"] = ["General Purpose Registers"]
        g_target_definition["registers"] = armv7_register_infos
        g_target_definition["host-info"] = {
            "triple": "armv7em--",
            "endian": eByteOrderLittle,
        }
        g_target_definition["g-packet-size"] = offset
    return g_target_definition


def get_dynamic_setting(target, setting_name):
    if setting_name == "gdb-server-target-definition":
        return get_target_definition()
