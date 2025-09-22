//===-- RISCVCallingConv.td - Calling Conventions RISC-V ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the RISC-V architecture.
//
//===----------------------------------------------------------------------===//

// The RISC-V calling convention is handled with custom code in
// RISCVISelLowering.cpp (CC_RISCV).

def CSR_ILP32E_LP64E : CalleeSavedRegs<(add X1, X8, X9)>;

def CSR_ILP32_LP64
    : CalleeSavedRegs<(add CSR_ILP32E_LP64E, (sequence "X%u", 18, 27))>;

def CSR_ILP32F_LP64F
    : CalleeSavedRegs<(add CSR_ILP32_LP64,
                       F8_F, F9_F, (sequence "F%u_F", 18, 27))>;

def CSR_ILP32D_LP64D
    : CalleeSavedRegs<(add CSR_ILP32_LP64,
                       F8_D, F9_D, (sequence "F%u_D", 18, 27))>;

defvar CSR_V = (add (sequence "V%u", 1, 7), (sequence "V%u", 24, 31),
                     V2M2, V4M2, V6M2, V24M2, V26M2, V28M2, V30M2,
                     V4M4, V24M4, V28M4, V24M8);

def CSR_ILP32_LP64_V
    : CalleeSavedRegs<(add CSR_ILP32_LP64, CSR_V)>;

def CSR_ILP32F_LP64F_V
    : CalleeSavedRegs<(add CSR_ILP32F_LP64F, CSR_V)>;

def CSR_ILP32D_LP64D_V
    : CalleeSavedRegs<(add CSR_ILP32D_LP64D, CSR_V)>;

// Needed for implementation of RISCVRegisterInfo::getNoPreservedMask()
def CSR_NoRegs : CalleeSavedRegs<(add)>;

// Interrupt handler needs to save/restore all registers that are used,
// both Caller and Callee saved registers.
def CSR_Interrupt : CalleeSavedRegs<(add X1, (sequence "X%u", 5, 31))>;

// Same as CSR_Interrupt, but including all 32-bit FP registers.
def CSR_XLEN_F32_Interrupt: CalleeSavedRegs<(add CSR_Interrupt,
                                             (sequence "F%u_F", 0, 31))>;

// Same as CSR_Interrupt, but including all 64-bit FP registers.
def CSR_XLEN_F64_Interrupt: CalleeSavedRegs<(add CSR_Interrupt,
                                             (sequence "F%u_D", 0, 31))>;

// Same as CSR_Interrupt, but excluding X16-X31.
def CSR_Interrupt_RVE : CalleeSavedRegs<(sub CSR_Interrupt,
                                        (sequence "X%u", 16, 31))>;

// Same as CSR_XLEN_F32_Interrupt, but excluding X16-X31.
def CSR_XLEN_F32_Interrupt_RVE: CalleeSavedRegs<(sub CSR_XLEN_F32_Interrupt,
                                                (sequence "X%u", 16, 31))>;

// Same as CSR_XLEN_F64_Interrupt, but excluding X16-X31.
def CSR_XLEN_F64_Interrupt_RVE: CalleeSavedRegs<(sub CSR_XLEN_F64_Interrupt,
                                                (sequence "X%u", 16, 31))>;
