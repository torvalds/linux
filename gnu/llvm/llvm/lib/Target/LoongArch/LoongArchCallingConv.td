//=- LoongArchCallingConv.td - Calling Conventions LoongArch -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the LoongArch architecture.
//
//===----------------------------------------------------------------------===//

def CSR_ILP32S_LP64S
    : CalleeSavedRegs<(add R1, (sequence "R%u", 22, 31))>;

def CSR_ILP32F_LP64F
    : CalleeSavedRegs<(add CSR_ILP32S_LP64S, (sequence "F%u", 24, 31))>;

def CSR_ILP32D_LP64D
    : CalleeSavedRegs<(add CSR_ILP32S_LP64S, (sequence "F%u_64", 24, 31))>;

// Needed for implementation of LoongArchRegisterInfo::getNoPreservedMask()
def CSR_NoRegs : CalleeSavedRegs<(add)>;
