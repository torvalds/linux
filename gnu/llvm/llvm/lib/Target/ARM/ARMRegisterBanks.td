//=- ARMRegisterBank.td - Describe the AArch64 Banks ---------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

def GPRRegBank : RegisterBank<"GPRB", [GPR, GPRwithAPSR]>;
def FPRRegBank : RegisterBank<"FPRB", [HPR, SPR, DPR, QPR]>;
