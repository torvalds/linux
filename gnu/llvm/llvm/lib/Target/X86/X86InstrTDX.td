//===- X86InstrTDX.td - TDX Instruction Set Extension -*- tablegen -*===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions that make up the Intel TDX instruction
// set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// TDX instructions

// 64-bit only instructions
let SchedRW = [WriteSystem], Predicates = [In64BitMode] in {
// SEAMCALL - Call to SEAM VMX-root Operation Module
def SEAMCALL : I<0x01, MRM_CF, (outs), (ins), "seamcall", []>, TB, PD;

// SEAMRET - Return to Legacy VMX-root Operation
def SEAMRET : I<0x01, MRM_CD, (outs), (ins), "seamret", []>, TB, PD;

// SEAMOPS - SEAM Operations
def SEAMOPS : I<0x01, MRM_CE, (outs), (ins), "seamops", []>, TB, PD;
} // SchedRW

// common instructions
let SchedRW = [WriteSystem] in {
// TDCALL - Call SEAM Module Functions
def TDCALL : I<0x01, MRM_CC, (outs), (ins), "tdcall", []>, TB, PD;
} // SchedRW
