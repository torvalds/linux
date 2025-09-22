//===- LanaiCallingConv.td - Calling Conventions Lanai -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the Lanai architectures.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Return Value Calling Conventions
//===----------------------------------------------------------------------===//

// Lanai 32-bit C Calling convention.
def CC_Lanai32 : CallingConv<[
  // Promote i8/i16 args to i32
  CCIfType<[i8, i16], CCPromoteToType<i32>>,

  // Put argument in registers if marked 'inreg' and not a vararg call.
  CCIfNotVarArg<CCIfInReg<CCIfType<[i32],
                                   CCAssignToReg<[R6, R7, R18, R19]>>>>,

  // Otherwise they are assigned to the stack in 4-byte aligned units.
  CCAssignToStack<4, 4>
]>;

// Lanai 32-bit Fast Calling convention.
def CC_Lanai32_Fast : CallingConv<[
  // Promote i8/i16 args to i32
  CCIfType<[ i8, i16 ], CCPromoteToType<i32>>,

  // Put arguments in registers.
  CCIfNotVarArg<CCIfType<[i32], CCAssignToReg<[ R6, R7, R18, R19 ]>>>,

  // Otherwise they are assigned to the stack in 4-byte aligned units.
  CCAssignToStack<4, 4>
]>;

// Lanai 32-bit C return-value convention.
def RetCC_Lanai32 : CallingConv<[
  // Specify two registers to allow returning 64-bit results that have already
  // been lowered to 2 32-bit values.
  CCIfType<[i32], CCAssignToReg<[RV, R9]>>
]>;

def CSR: CalleeSavedRegs<(add)>;
