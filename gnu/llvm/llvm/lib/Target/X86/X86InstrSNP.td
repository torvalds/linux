//===-- X86InstrSNP.td - SNP Instruction Set Extension -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions that make up the AMD Secure Nested
// Paging (SNP) instruction set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SNP instructions

let SchedRW = [WriteSystem] in {
// F3 0F 01 FF
let Uses = [RAX], Defs = [EAX, EFLAGS] in
def PSMASH: I<0x01, MRM_FF, (outs), (ins), "psmash", []>, TB, XS,
            Requires<[In64BitMode]>;

// F2 0F 01 FF
let Uses = [RAX, RCX, RDX], Defs = [EAX, EFLAGS] in
def PVALIDATE64: I<0x01, MRM_FF, (outs), (ins), "pvalidate",[]>,
                 TB, XD, Requires<[In64BitMode]>;

let Uses = [EAX, ECX, EDX], Defs = [EAX, EFLAGS] in
def PVALIDATE32: I<0x01, MRM_FF, (outs), (ins), "pvalidate",[]>,
                 TB, XD, Requires<[Not64BitMode]>;

// F2 0F 01 FE
let Uses = [RAX, RCX], Defs = [EAX, EFLAGS] in
def RMPUPDATE: I<0x01, MRM_FE, (outs), (ins), "rmpupdate", []>, TB, XD,
               Requires<[In64BitMode]>;

// F3 0F 01 FE
let Uses = [RAX, RCX, RDX], Defs = [EAX, EFLAGS] in
def RMPADJUST: I<0x01, MRM_FE, (outs), (ins), "rmpadjust", []>, TB, XS,
               Requires<[In64BitMode]>;

// F3 0F 01 FD
let Uses = [RAX, RDX], Defs = [RAX, RCX, RDX, EFLAGS] in
def RMPQUERY: I<0x01, MRM_FD, (outs), (ins), "rmpquery", []>, TB, XS,
               Requires<[In64BitMode]>;
} // SchedRW

def : InstAlias<"psmash\t{%rax|rax}", (PSMASH)>, Requires<[In64BitMode]>;
def : InstAlias<"pvalidate\t{%rax, %rcx, %rdx|rdx, rcx, rax|}", (PVALIDATE64)>, Requires<[In64BitMode]>;
def : InstAlias<"pvalidate\t{%eax, %ecx, %edx|edx, ecx, eax|}", (PVALIDATE32)>, Requires<[Not64BitMode]>;
def : InstAlias<"rmpupdate\t{%rax, %rcx|rcx, rax|}", (RMPUPDATE)>, Requires<[In64BitMode]>;
def : InstAlias<"rmpadjust\t{%rax, %rcx, %rdx|rdx, rcx, rax|}", (RMPADJUST)>, Requires<[In64BitMode]>;
def : InstAlias<"rmpquery\t{%rax, %rdx|rdx, rax|}", (RMPQUERY)>, Requires<[In64BitMode]>;
