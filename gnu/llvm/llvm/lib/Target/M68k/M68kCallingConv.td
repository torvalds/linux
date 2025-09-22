//===-- M68kCallingConv.td - Calling Conventions for M68k --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This describes the calling conventions for the M68k architectures. These
/// conventions assume Int to be 4 bytes and 4 byte aligned.
///
//===----------------------------------------------------------------------===//

// TODO Verify C convention follows SysV M68K ABI

class CCIfSubtarget<string F, CCAction A>
    : CCIf<!strconcat("static_cast<const M68kSubtarget &>"
                      "(State.getMachineFunction().getSubtarget()).", F), A>;

//===----------------------------------------------------------------------===//
// Return Value Calling Conventions
//===----------------------------------------------------------------------===//

/// M68k C return convention.
def RetCC_M68k_C : CallingConv<[
  CCIfPtr<CCAssignToReg<[A0]>>,
  CCIfType<[i1],   CCPromoteToType<i8>>,
  CCIfType<[i8],   CCAssignToReg<[BD0, BD1]>>,
  CCIfType<[i16],  CCAssignToReg<[WD0, WD1]>>,
  CCIfType<[i32],  CCAssignToReg<[D0, D1]>>,
]>;

/// M68k fastcc return convention.
/// This convention allows to return up to 16 bytes in registers which can be
/// split among 16 1-byte values or used for a single 16-byte value.
/// TODO: Verify its functionality and write tests
def RetCC_M68k_Fast : CallingConv<[
  CCIfPtr<CCAssignToReg<[A0]>>,
  CCIfType<[i1],   CCPromoteToType<i8>>,
  CCIfType<[i8],   CCAssignToReg<[BD0, BD1]>>,
  CCIfType<[i16],  CCAssignToReg<[WD0, WD1, WA0, WA1]>>,
  CCIfType<[i32],  CCAssignToReg<[D0, D1, A0, A1]>>,
]>;

/// This is the root return-value convention for the M68k backend.
def RetCC_M68k : CallingConv<[
  CCIfCC<"CallingConv::Fast", CCDelegateTo<RetCC_M68k_Fast>>,
  CCDelegateTo<RetCC_M68k_C>
]>;

//===----------------------------------------------------------------------===//
// M68k C Calling Convention
//===----------------------------------------------------------------------===//

/// CC_M68k_Common - In all M68k calling conventions, extra integers and FP
/// values are spilled on the stack.
def CC_M68k_Common : CallingConv<[
  /// Handles byval parameters.
  CCIfByVal<CCPassByVal<4, 4>>,

  /// Integer values get stored in stack slots that are 4 bytes in
  /// size and 4-byte aligned.
  CCIfType<[i32],  CCAssignToStack<4, 4>>
]>;

def CC_M68k_Fast : CallingConv<[
  /// Promote i1/i8/i16 arguments to i32.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  /// The 'nest' parameter, if any, is passed in A1.
  CCIfNest<CCAssignToReg<[A1]>>, // FIXME verify if this is correct

  /// Since M68k uses %An for pointers and we want them be passed in regs
  /// too we have to use custom function.
  CCIfType<[i32], CCCustom<"CC_M68k_Any_AssignToReg">>,

  /// Otherwise, same as everything else.
  CCDelegateTo<CC_M68k_Common>
]>;

def CC_M68k_C : CallingConv<[
  /// Promote i1/i8/i16 arguments to i32.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  /// The 'nest' parameter, if any, is passed in A1.
  CCIfNest<CCAssignToReg<[A1]>>, // FIXME verify if this is correct

  /// Use registers only if 'inreg' used and the call is not vararg
  CCIfNotVarArg<CCIfInReg<CCIfType<[i32], CCAssignToReg<[D0, D1]>>>>,

  // TODO: Support for 'sret'

  /// Otherwise, same as everything else.
  CCDelegateTo<CC_M68k_Common>
]>;

/// This is the root argument convention for the M68k backend.
def CC_M68k : CallingConv<[
  CCIfCC<"CallingConv::Fast", CCDelegateTo<CC_M68k_Fast>>,
  CCDelegateTo<CC_M68k_C>
]>;

//===----------------------------------------------------------------------===//
// Callee-saved Registers.
//===----------------------------------------------------------------------===//

def CSR_NoRegs : CalleeSavedRegs<(add)>;

// A5 - BP
// A6 - FP
def CSR_STD : CalleeSavedRegs<(add D2, D3, D4, D5, D6, D7,
                                   A2, A3, A4, A5, A6)>;

