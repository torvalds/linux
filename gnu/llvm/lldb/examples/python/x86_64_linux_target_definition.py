#!/usr/bin/env python
# ===-- x86_64_linux_target_definition.py -----------------------------*- C++ -*-===//
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
# (lldb) settings set plugin.process.gdb-remote.target-definition-file /path/to/x86_64_linux_target_definition.py
# (lldb) gdb-remote other.baz.com:1234
#
# The target definition file will get used if and only if the
# qRegisterInfo packets are not supported when connecting to a remote
# GDB server.
# ----------------------------------------------------------------------
from lldb import *

# Compiler and DWARF register numbers
name_to_gcc_dwarf_regnum = {
    "rax": 0,
    "rdx": 1,
    "rcx": 2,
    "rbx": 3,
    "rsi": 4,
    "rdi": 5,
    "rbp": 6,
    "rsp": 7,
    "r8": 8,
    "r9": 9,
    "r10": 10,
    "r11": 11,
    "r12": 12,
    "r13": 13,
    "r14": 14,
    "r15": 15,
    "rip": 16,
    "xmm0": 17,
    "xmm1": 18,
    "xmm2": 19,
    "xmm3": 20,
    "xmm4": 21,
    "xmm5": 22,
    "xmm6": 23,
    "xmm7": 24,
    "xmm8": 25,
    "xmm9": 26,
    "xmm10": 27,
    "xmm11": 28,
    "xmm12": 29,
    "xmm13": 30,
    "xmm14": 31,
    "xmm15": 32,
    "stmm0": 33,
    "stmm1": 34,
    "stmm2": 35,
    "stmm3": 36,
    "stmm4": 37,
    "stmm5": 38,
    "stmm6": 39,
    "stmm7": 30,
    "ymm0": 41,
    "ymm1": 42,
    "ymm2": 43,
    "ymm3": 44,
    "ymm4": 45,
    "ymm5": 46,
    "ymm6": 47,
    "ymm7": 48,
    "ymm8": 49,
    "ymm9": 40,
    "ymm10": 41,
    "ymm11": 42,
    "ymm12": 43,
    "ymm13": 44,
    "ymm14": 45,
    "ymm15": 46,
}

name_to_gdb_regnum = {
    "rax": 0,
    "rbx": 1,
    "rcx": 2,
    "rdx": 3,
    "rsi": 4,
    "rdi": 5,
    "rbp": 6,
    "rsp": 7,
    "r8": 8,
    "r9": 9,
    "r10": 10,
    "r11": 11,
    "r12": 12,
    "r13": 13,
    "r14": 14,
    "r15": 15,
    "rip": 16,
    "rflags": 17,
    "cs": 18,
    "ss": 19,
    "ds": 20,
    "es": 21,
    "fs": 22,
    "gs": 23,
    "stmm0": 24,
    "stmm1": 25,
    "stmm2": 26,
    "stmm3": 27,
    "stmm4": 28,
    "stmm5": 29,
    "stmm6": 30,
    "stmm7": 31,
    "fctrl": 32,
    "fstat": 33,
    "ftag": 34,
    "fiseg": 35,
    "fioff": 36,
    "foseg": 37,
    "fooff": 38,
    "fop": 39,
    "xmm0": 40,
    "xmm1": 41,
    "xmm2": 42,
    "xmm3": 43,
    "xmm4": 44,
    "xmm5": 45,
    "xmm6": 46,
    "xmm7": 47,
    "xmm8": 48,
    "xmm9": 49,
    "xmm10": 50,
    "xmm11": 51,
    "xmm12": 52,
    "xmm13": 53,
    "xmm14": 54,
    "xmm15": 55,
    "mxcsr": 56,
    "ymm0": 57,
    "ymm1": 58,
    "ymm2": 59,
    "ymm3": 60,
    "ymm4": 61,
    "ymm5": 62,
    "ymm6": 63,
    "ymm7": 64,
    "ymm8": 65,
    "ymm9": 66,
    "ymm10": 67,
    "ymm11": 68,
    "ymm12": 69,
    "ymm13": 70,
    "ymm14": 71,
    "ymm15": 72,
}

name_to_generic_regnum = {
    "rip": LLDB_REGNUM_GENERIC_PC,
    "rsp": LLDB_REGNUM_GENERIC_SP,
    "rbp": LLDB_REGNUM_GENERIC_FP,
    "rdi": LLDB_REGNUM_GENERIC_ARG1,
    "rsi": LLDB_REGNUM_GENERIC_ARG2,
    "rdx": LLDB_REGNUM_GENERIC_ARG3,
    "rcx": LLDB_REGNUM_GENERIC_ARG4,
    "r8": LLDB_REGNUM_GENERIC_ARG5,
    "r9": LLDB_REGNUM_GENERIC_ARG6,
}


def get_reg_num(reg_num_dict, reg_name):
    if reg_name in reg_num_dict:
        return reg_num_dict[reg_name]
    return LLDB_INVALID_REGNUM


x86_64_register_infos = [
    {
        "name": "rax",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "rbx",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "rcx",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg4",
    },
    {
        "name": "rdx",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg3",
    },
    {
        "name": "rsi",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg2",
    },
    {
        "name": "rdi",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg1",
    },
    {
        "name": "rbp",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "fp",
    },
    {
        "name": "rsp",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "sp",
    },
    {
        "name": "r8",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg5",
    },
    {
        "name": "r9",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "arg6",
    },
    {
        "name": "r10",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r11",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r12",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r13",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r14",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "r15",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
    },
    {
        "name": "rip",
        "set": 0,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatAddressInfo,
        "alt-name": "pc",
    },
    {
        "name": "rflags",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "cs",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "ss",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "ds",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "es",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "fs",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "gs",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "stmm0",
        "set": 1,
        "bitsize": 80,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "stmm1",
        "set": 1,
        "bitsize": 80,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "stmm2",
        "set": 1,
        "bitsize": 80,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "stmm3",
        "set": 1,
        "bitsize": 80,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "stmm4",
        "set": 1,
        "bitsize": 80,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "stmm5",
        "set": 1,
        "bitsize": 80,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "stmm6",
        "set": 1,
        "bitsize": 80,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "stmm7",
        "set": 1,
        "bitsize": 80,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "fctrl",
        "set": 1,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "fstat",
        "set": 1,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "ftag",
        "set": 1,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "fiseg",
        "set": 1,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "fioff",
        "set": 1,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "foseg",
        "set": 1,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "fooff",
        "set": 1,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "fop",
        "set": 1,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "xmm0",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm1",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm2",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm3",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm4",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm5",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm6",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm7",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm8",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm9",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm10",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm11",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm12",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm13",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm14",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "xmm15",
        "set": 1,
        "bitsize": 128,
        "encoding": eEncodingVector,
        "format": eFormatVectorOfUInt8,
    },
    {
        "name": "mxcsr",
        "set": 1,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    {
        "name": "orig_rax",
        "set": 1,
        "bitsize": 64,
        "encoding": eEncodingUint,
        "format": eFormatHex,
    },
    # Registers that are contained in or composed of one of more other
    # registers
    {
        "name": "eax",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rax[31:0]",
    },
    {
        "name": "ebx",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rbx[31:0]",
    },
    {
        "name": "ecx",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rcx[31:0]",
    },
    {
        "name": "edx",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rdx[31:0]",
    },
    {
        "name": "edi",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rdi[31:0]",
    },
    {
        "name": "esi",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rsi[31:0]",
    },
    {
        "name": "ebp",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rbp[31:0]",
    },
    {
        "name": "esp",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rsp[31:0]",
    },
    {
        "name": "r8d",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r8[31:0]",
    },
    {
        "name": "r9d",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r9[31:0]",
    },
    {
        "name": "r10d",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r10[31:0]",
    },
    {
        "name": "r11d",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r11[31:0]",
    },
    {
        "name": "r12d",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r12[31:0]",
    },
    {
        "name": "r13d",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r13[31:0]",
    },
    {
        "name": "r14d",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r14[31:0]",
    },
    {
        "name": "r15d",
        "set": 0,
        "bitsize": 32,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r15[31:0]",
    },
    {
        "name": "ax",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rax[15:0]",
    },
    {
        "name": "bx",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rbx[15:0]",
    },
    {
        "name": "cx",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rcx[15:0]",
    },
    {
        "name": "dx",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rdx[15:0]",
    },
    {
        "name": "di",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rdi[15:0]",
    },
    {
        "name": "si",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rsi[15:0]",
    },
    {
        "name": "bp",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rbp[15:0]",
    },
    {
        "name": "sp",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rsp[15:0]",
    },
    {
        "name": "r8w",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r8[15:0]",
    },
    {
        "name": "r9w",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r9[15:0]",
    },
    {
        "name": "r10w",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r10[15:0]",
    },
    {
        "name": "r11w",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r11[15:0]",
    },
    {
        "name": "r12w",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r12[15:0]",
    },
    {
        "name": "r13w",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r13[15:0]",
    },
    {
        "name": "r14w",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r14[15:0]",
    },
    {
        "name": "r15w",
        "set": 0,
        "bitsize": 16,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r15[15:0]",
    },
    {
        "name": "ah",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rax[15:8]",
    },
    {
        "name": "bh",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rbx[15:8]",
    },
    {
        "name": "ch",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rcx[15:8]",
    },
    {
        "name": "dh",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rdx[15:8]",
    },
    {
        "name": "al",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rax[7:0]",
    },
    {
        "name": "bl",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rbx[7:0]",
    },
    {
        "name": "cl",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rcx[7:0]",
    },
    {
        "name": "dl",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rdx[7:0]",
    },
    {
        "name": "dil",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rdi[7:0]",
    },
    {
        "name": "sil",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rsi[7:0]",
    },
    {
        "name": "bpl",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rbp[7:0]",
    },
    {
        "name": "spl",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "rsp[7:0]",
    },
    {
        "name": "r8l",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r8[7:0]",
    },
    {
        "name": "r9l",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r9[7:0]",
    },
    {
        "name": "r10l",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r10[7:0]",
    },
    {
        "name": "r11l",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r11[7:0]",
    },
    {
        "name": "r12l",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r12[7:0]",
    },
    {
        "name": "r13l",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r13[7:0]",
    },
    {
        "name": "r14l",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r14[7:0]",
    },
    {
        "name": "r15l",
        "set": 0,
        "bitsize": 8,
        "encoding": eEncodingUint,
        "format": eFormatHex,
        "slice": "r15[7:0]",
    },
]

g_target_definition = None


def get_target_definition():
    global g_target_definition
    if g_target_definition is None:
        g_target_definition = {}
        offset = 0
        for reg_info in x86_64_register_infos:
            reg_name = reg_info["name"]

            # Only fill in the offset if there is no 'slice' in the register
            # info
            if "slice" not in reg_info and "composite" not in reg_info:
                reg_info["offset"] = offset
                offset += reg_info["bitsize"] // 8

            # Set the GCC/DWARF register number for this register if it has one
            reg_num = get_reg_num(name_to_gcc_dwarf_regnum, reg_name)
            if reg_num != LLDB_INVALID_REGNUM:
                reg_info["gcc"] = reg_num
                reg_info["dwarf"] = reg_num

            # Set the generic register number for this register if it has one
            reg_num = get_reg_num(name_to_generic_regnum, reg_name)
            if reg_num != LLDB_INVALID_REGNUM:
                reg_info["generic"] = reg_num

            # Set the GDB register number for this register if it has one
            reg_num = get_reg_num(name_to_gdb_regnum, reg_name)
            if reg_num != LLDB_INVALID_REGNUM:
                reg_info["gdb"] = reg_num

        g_target_definition["sets"] = [
            "General Purpose Registers",
            "Floating Point Registers",
        ]
        g_target_definition["registers"] = x86_64_register_infos
        g_target_definition["host-info"] = {
            "triple": "x86_64-*-linux",
            "endian": eByteOrderLittle,
        }
        g_target_definition["g-packet-size"] = offset
        g_target_definition["breakpoint-pc-offset"] = -1
    return g_target_definition


def get_dynamic_setting(target, setting_name):
    if setting_name == "gdb-server-target-definition":
        return get_target_definition()
