//===-- M68kInstrBuilder.h - Functions to build M68k insts ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file exposes functions that may be used with BuildMI from the
/// MachineInstrBuilder.h file to handle M68k'isms in a clean way.
///
/// TODO The BuildMem function may be used with the BuildMI function to add
/// entire memory references in a single, typed, function call.  M68k memory
/// references can be very complex expressions (described in the README), so
/// wrapping them up behind an easier to use interface makes sense.
/// Descriptions of the functions are included below.
///
/// For reference, the order of operands for memory references is:
/// (Operand), Base, Scale, Index, Displacement.
///
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_LIB_TARGET_M68K_M68KINSTRBUILDER_H
#define LLVM_LIB_TARGET_M68K_M68KINSTRBUILDER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCInstrDesc.h"

#include <cassert>

namespace llvm {
namespace M68k {
static inline const MachineInstrBuilder &
addOffset(const MachineInstrBuilder &MIB, int Offset) {
  return MIB.addImm(Offset);
}

/// addRegIndirectWithDisp - This function is used to add a memory reference
/// of the form (Offset, Base), i.e., one with no scale or index, but with a
/// displacement. An example is: (4,D0).
static inline const MachineInstrBuilder &
addRegIndirectWithDisp(const MachineInstrBuilder &MIB, Register Reg,
                       bool IsKill, int Offset) {
  return MIB.addImm(Offset).addReg(Reg, getKillRegState(IsKill));
}

/// addFrameReference - This function is used to add a reference to the base of
/// an abstract object on the stack frame of the current function.  This
/// reference has base register as the FrameIndex offset until it is resolved.
/// This allows a constant offset to be specified as well...
static inline const MachineInstrBuilder &
addFrameReference(const MachineInstrBuilder &MIB, int FI, int Offset = 0) {
  MachineInstr *MI = MIB;
  MachineFunction &MF = *MI->getParent()->getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCInstrDesc &MCID = MI->getDesc();
  auto Flags = MachineMemOperand::MONone;
  if (MCID.mayLoad())
    Flags |= MachineMemOperand::MOLoad;
  if (MCID.mayStore())
    Flags |= MachineMemOperand::MOStore;
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI, Offset), Flags,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  return MIB.addImm(Offset).addFrameIndex(FI).addMemOperand(MMO);
}

static inline const MachineInstrBuilder &
addMemOperand(const MachineInstrBuilder &MIB, int FI, int Offset = 0) {
  MachineInstr *MI = MIB;
  MachineFunction &MF = *MI->getParent()->getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCInstrDesc &MCID = MI->getDesc();
  auto Flags = MachineMemOperand::MONone;
  if (MCID.mayLoad())
    Flags |= MachineMemOperand::MOLoad;
  if (MCID.mayStore())
    Flags |= MachineMemOperand::MOStore;
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI, Offset), Flags,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  return MIB.addMemOperand(MMO);
}
} // end namespace M68k
} // end namespace llvm

#endif // LLVM_LIB_TARGET_M68K_M68KINSTRBUILDER_H
