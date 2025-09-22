//===-- BPFCallingConv.td - Calling Conventions BPF --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the BPF architecture.
//
//===----------------------------------------------------------------------===//

// BPF 64-bit C return-value convention.
def RetCC_BPF64 : CallingConv<[CCIfType<[i64], CCAssignToReg<[R0]>>]>;

// BPF 64-bit C Calling convention.
def CC_BPF64 : CallingConv<[
  // Promote i8/i16/i32 args to i64
  CCIfType<[ i8, i16, i32 ], CCPromoteToType<i64>>,

  // All arguments get passed in integer registers if there is space.
  CCIfType<[i64], CCAssignToReg<[ R1, R2, R3, R4, R5 ]>>,

  // Could be assigned to the stack in 8-byte aligned units, but unsupported
  CCAssignToStack<8, 8>
]>;

// Return-value convention when -mattr=+alu32 enabled
def RetCC_BPF32 : CallingConv<[
  CCIfType<[i32], CCAssignToRegWithShadow<[W0], [R0]>>,
  CCIfType<[i64], CCAssignToRegWithShadow<[R0], [W0]>>
]>;

// Calling convention when -mattr=+alu32 enabled
def CC_BPF32 : CallingConv<[
  // Promote i8/i16/i32 args to i64
  CCIfType<[i32], CCAssignToRegWithShadow<[W1, W2, W3, W4, W5],
                                          [R1, R2, R3, R4, R5]>>,

  // All arguments get passed in integer registers if there is space.
  CCIfType<[i64], CCAssignToRegWithShadow<[R1, R2, R3, R4, R5],
                                          [W1, W2, W3, W4, W5]>>,

  // Could be assigned to the stack in 8-byte aligned units, but unsupported
  CCAssignToStack<8, 8>
]>;

def CSR : CalleeSavedRegs<(add R6, R7, R8, R9, R10)>;
