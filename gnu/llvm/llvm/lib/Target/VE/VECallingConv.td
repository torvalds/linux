//===-- VECallingConv.td - Calling Conventions VE ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the VE architectures.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Aurora VE
//===----------------------------------------------------------------------===//
def CC_VE_C_Stack: CallingConv<[
  // F128 are assigned to the stack in 16-byte aligned units
  CCIfType<[f128], CCAssignToStackWithShadow<16, 16, [SX7]>>,

  // All of the rest are assigned to the stack in 8-byte aligned units.
  CCAssignToStack<0, 8>
]>;

///// C Calling Convention (VE ABI v2.1) /////
//
// Reference: https://www.nec.com/en/global/prod/hpc/aurora/document/VE-ABI_v2.1.pdf
//
def CC_VE_C : CallingConv<[
  // All arguments get passed in generic registers if there is space.

  // Promote i1/i8/i16/i32 arguments to i64.
  CCIfType<[i1, i8, i16, i32], CCPromoteToType<i64>>,

  // Convert float arguments to i64 with padding.
  //     63     31   0
  //    +------+------+
  //    | float|   0  |
  //    +------+------+
  CCIfType<[f32], CCBitConvertToType<i64>>,

  // bool, char, int, enum, long, long long, float, double
  //     --> generic 64 bit registers
  CCIfType<[i64, f64],
           CCAssignToReg<[SX0, SX1, SX2, SX3, SX4, SX5, SX6, SX7]>>,

  // long double --> pair of generic 64 bit registers
  //
  // NOTE: If Q1 is allocated while SX1 is free, llvm tries to allocate SX1 for
  //       following operands, this masks SX1 to avoid such behavior.
  CCIfType<[f128],
           CCAssignToRegWithShadow<[Q0, Q1, Q2, Q3],
                                   [SX0, SX1, SX3, SX5]>>,

  // Alternatively, they are assigned to the stack in 8-byte aligned units.
  CCDelegateTo<CC_VE_C_Stack>
]>;

///// Standard vararg C Calling Convention (VE ABI v2.1) /////
// All arguments get passed in stack for varargs function or non-prototyped
// function.
def CC_VE2 : CallingConv<[
  // Promote i1/i8/i16/i32 arguments to i64.
  CCIfType<[i1, i8, i16, i32], CCPromoteToType<i64>>,

  // Convert float arguments to i64 with padding.
  //     63     31   0
  //    +------+------+
  //    | float|   0  |
  //    +------+------+
  CCIfType<[f32], CCBitConvertToType<i64>>,

  // F128 are assigned to the stack in 16-byte aligned units
  CCIfType<[f128], CCAssignToStack<16, 16>>,

  CCAssignToStack<0, 8>
]>;

def RetCC_VE_C : CallingConv<[
  // Promote i1/i8/i16/i32 return values to i64.
  CCIfType<[i1, i8, i16, i32], CCPromoteToType<i64>>,

  // Convert float return values to i64 with padding.
  //     63     31   0
  //    +------+------+
  //    | float|   0  |
  //    +------+------+
  CCIfType<[f32], CCBitConvertToType<i64>>,

  // bool, char, int, enum, long, long long, float, double
  //     --> generic 64 bit registers
  CCIfType<[i64, f64],
           CCAssignToReg<[SX0, SX1, SX2, SX3, SX4, SX5, SX6, SX7]>>,

  // long double --> pair of generic 64 bit registers
  CCIfType<[f128],
           CCAssignToRegWithShadow<[Q0, Q1, Q2, Q3],
                                   [SX0, SX1, SX3, SX5]>>,
]>;

///// Custom fastcc /////
//
// This passes vector params and return values in registers.  Scalar values are
// handled conforming to the standard cc.
def CC_VE_Fast : CallingConv<[
  // vector --> generic vector registers
  CCIfType<[v256i32, v256f32, v256i64, v256f64],
           CCAssignToReg<[V0, V1, V2, V3, V4, V5, V6, V7]>>,
  // TODO: make this conditional on packed mode
  CCIfType<[v512i32, v512f32],
           CCAssignToReg<[V0, V1, V2, V3, V4, V5, V6, V7]>>,

  // vector mask --> generic vector mask registers
  CCIfType<[v256i1],
           CCAssignToReg<[VM1, VM2, VM3, VM4, VM5, VM6, VM7]>>,

  // pair of vector mask --> generic vector mask registers
  CCIfType<[v512i1],
           CCAssignToRegWithShadow<[VMP1, VMP2, VMP3],
                                   [VM1, VM3, VM5]>>,

  // Follow the standard C CC for scalars.
  CCDelegateTo<CC_VE_C>
]>;

def RetCC_VE_Fast : CallingConv<[
  // vector --> generic vector registers
  CCIfType<[v256i32, v256f32, v256i64, v256f64],
           CCAssignToReg<[V0, V1, V2, V3, V4, V5, V6, V7]>>,
  // TODO: make this conditional on packed mode
  CCIfType<[v512i32, v512f32],
           CCAssignToReg<[V0, V1, V2, V3, V4, V5, V6, V7]>>,

  // vector mask --> generic vector mask registers
  CCIfType<[v256i1],
           CCAssignToReg<[VM1, VM2, VM3, VM4, VM5, VM6, VM7]>>,

  // pair of vector mask --> generic vector mask registers
  CCIfType<[v512i1],
           CCAssignToRegWithShadow<[VMP1, VMP2, VMP3],
                                   [VM1, VM3, VM5]>>,

  // Follow the standard C CC for scalars.
  CCDelegateTo<RetCC_VE_C>
]>;

// Callee-saved registers
def CSR : CalleeSavedRegs<(add (sequence "SX%u", 18, 33))>;
def CSR_NoRegs : CalleeSavedRegs<(add)>;

// PreserveAll (clobbers s62,s63) - used for ve_grow_stack
def CSR_preserve_all : CalleeSavedRegs<(add (sequence "SX%u", 0, 61),
                                            (sequence "V%u", 0, 63),
                                            (sequence "VM%u", 1, 15))>;
