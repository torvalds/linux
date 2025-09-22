//===-- CSKYCallingConv.td - Calling Conventions CSKY ----*- tablegen -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the CSKY architecture.
//
//===----------------------------------------------------------------------===//

def CSR_I32 : CalleeSavedRegs<(add R8, R15, (sequence "R%u", 4, 7),
  (sequence "R%u", 9, 11), (sequence "R%u", 16, 17), R28)>;
def CSR_GPR_FPR32 : CalleeSavedRegs<(add CSR_I32, (sequence "F%u_32", 8, 15))>;
def CSR_GPR_FPR64 : CalleeSavedRegs<(add CSR_I32,
                                    (sequence "F%u_64", 8, 15))>;

// Interrupt handler needs to save/restore all registers that are used,
// both Caller and Callee saved registers.
def CSR_GPR_ISR : CalleeSavedRegs<(add R8, R15,
    (sequence "R%u", 0, 3),
    (sequence "R%u", 4, 7),
    (sequence "R%u", 9, 13),
    (sequence "R%u", 16, 31))>;

def CSR_GPR_FPR32_ISR: CalleeSavedRegs<(add CSR_GPR_ISR,
    (sequence "F%u_32", 0, 15))>;
def CSR_GPR_FPR64_ISR: CalleeSavedRegs<(add CSR_GPR_ISR,
    (sequence "F%u_64", 0, 15))>;

def CSR_GPR_FPR32v3_ISR: CalleeSavedRegs<(add CSR_GPR_FPR32_ISR,
    (sequence "F%u_32", 16, 31))>;
def CSR_GPR_FPR64v3_ISR: CalleeSavedRegs<(add CSR_GPR_FPR64_ISR,
    (sequence "F%u_64", 16, 31))>;

// Needed for implementation of CSKYRegisterInfo::getNoPreservedMask()
def CSR_NoRegs : CalleeSavedRegs<(add)>;

def CC_CSKY_ABIV2_SOFT : CallingConv<[
  // DSP types
  CCIfType<[v2i16, v4i8], CCAssignToReg<[R0, R1, R2, R3]>>,
  CCIfType<[v2i16, v4i8], CCAssignToStack<4, 4>>,
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f32], CCAssignToReg<[R0, R1, R2, R3]>>,
  CCIfType<[f32], CCAssignToStack<4, 4>>,
  CCIfType<[i32], CCAssignToReg<[R0, R1, R2, R3]>>,
  CCIfType<[i32], CCAssignToStack<4, 4>>,
  CCIfType<[f64], CCCustom<"CC_CSKY_ABIV2_SOFT_64">>,
  CCIfType<[f64], CCAssignToStack<8, 4>>
]>;

def RetCC_CSKY_ABIV2_SOFT : CallingConv<[
  // DSP types
  CCIfType<[v2i16, v4i8], CCAssignToReg<[R0, R1]>>,
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[R0, R1]>>,
  CCIfType<[f64], CCCustom<"Ret_CSKY_ABIV2_SOFT_64">>
]>;

def CC_CSKY_ABIV2_FP : CallingConv<[
  // DSP types
  CCIfType<[v2i16, v4i8], CCAssignToReg<[R0, R1, R2, R3]>>,
  CCIfType<[v2i16, v4i8], CCAssignToStack<4, 4>>,
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[R0, R1, R2, R3]>>,
  CCIfType<[i32], CCAssignToStack<4, 4>>,
  CCIfType<[f32], CCAssignToReg<[F0_32, F1_32, F2_32, F3_32]>>,
  CCIfType<[f32], CCAssignToStack<4, 4>>,
  CCIfType<[f64], CCAssignToReg<[F0_64, F1_64, F2_64, F3_64]>>,
  CCIfType<[f64], CCAssignToStack<8, 4>>
]>;

def RetCC_CSKY_ABIV2_FP : CallingConv<[
  // DSP types
  CCIfType<[v2i16, v4i8], CCAssignToReg<[R0, R1]>>,
  CCIfType<[i8, i16], CCPromoteToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[R0, R1]>>,
  CCIfType<[f32], CCAssignToReg<[F0_32]>>,
  CCIfType<[f64], CCAssignToReg<[F0_64]>>
]>;
