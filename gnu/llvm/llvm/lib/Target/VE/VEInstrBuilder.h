//===-- VEInstrBuilder.h - Aides for building VE insts ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes functions that may be used with BuildMI from the
// MachineInstrBuilder.h file to simplify generating frame and constant pool
// references.
//
// For reference, the order of operands for memory references is:
// (Operand), Dest Reg, Base Reg, and either Reg Index or Immediate
// Displacement.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VE_VEINSTRBUILDER_H
#define LLVM_LIB_TARGET_VE_VEINSTRBUILDER_H

#include "llvm/CodeGen/MachineInstrBuilder.h"

namespace llvm {

/// addFrameReference - This function is used to add a reference to the base of
/// an abstract object on the stack frame of the current function.  This
/// reference has base register as the FrameIndex offset until it is resolved.
/// This allows a constant offset to be specified as well...
///
static inline const MachineInstrBuilder &
addFrameReference(const MachineInstrBuilder &MIB, int FI, int Offset = 0,
                  bool ThreeOp = true) {
  if (ThreeOp)
    return MIB.addFrameIndex(FI).addImm(0).addImm(Offset);
  return MIB.addFrameIndex(FI).addImm(Offset);
}

} // namespace llvm

#endif
