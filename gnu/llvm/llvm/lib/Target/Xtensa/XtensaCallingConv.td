//===- XtensaCallingConv.td - Xtensa Calling Conventions -*- tablegen ---*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for the Xtensa ABI.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Xtensa return value calling convention
//===----------------------------------------------------------------------===//
def RetCC_Xtensa : CallingConv<[
  // First two return values go in a2, a3, a4, a5
  CCIfType<[i32], CCAssignToReg<[A2, A3, A4, A5]>>,
  CCIfType<[i64], CCAssignToRegWithShadow<[A2, A4], [A3, A5]>>
]>;

//===----------------------------------------------------------------------===//
// Callee-saved register lists.
//===----------------------------------------------------------------------===//

def CSR_Xtensa : CalleeSavedRegs<(add A0, A12, A13, A14, A15)>;
