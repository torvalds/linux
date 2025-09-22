//===-- AVRCallingConv.td - Calling Conventions for AVR ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for AVR architecture.
// Normal functions use a special calling convention, solved in code.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// AVR Return Value Calling Convention
//===----------------------------------------------------------------------===//

// Special return value calling convention for runtime functions.
def RetCC_AVR_BUILTIN : CallingConv<[
  CCIfType<[i8], CCAssignToReg<[R24, R25]>>,
  CCIfType<[i16], CCAssignToReg<[R23R22, R25R24]>>
]>;

//===----------------------------------------------------------------------===//
// AVR Argument Calling Conventions
//===----------------------------------------------------------------------===//

// The calling conventions are implemented in custom C++ code

// Calling convention for variadic functions.
def ArgCC_AVR_Vararg : CallingConv<[
  // i8 are always passed through the stack with a byte slot and byte alignment.
  CCIfType<[i8], CCAssignToStack<1, 1>>,
  // i16 are always passed through the stack with an alignment of 1.
  CCAssignToStack<2, 1>
]>;

//===----------------------------------------------------------------------===//
// Callee-saved register lists.
//===----------------------------------------------------------------------===//

def CSR_Normal : CalleeSavedRegs<(add R29, R28, (sequence "R%u", 17, 2))>;
def CSR_NormalTiny : CalleeSavedRegs<(add R29, R28, R19, R18)>;
def CSR_Interrupts : CalleeSavedRegs<(add(sequence "R%u", 31, 2))>;
def CSR_InterruptsTiny : CalleeSavedRegs<(add(sequence "R%u", 31, 18))>;
