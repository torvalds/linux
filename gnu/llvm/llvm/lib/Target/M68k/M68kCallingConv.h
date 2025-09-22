//===-- M68kCallingConv.h - M68k Custom CC Routines -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the custom routines for the M68k Calling Convention
/// that aren't done by tablegen.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_M68KCALLINGCONV_H
#define LLVM_LIB_TARGET_M68K_M68KCALLINGCONV_H

#include "MCTargetDesc/M68kMCTargetDesc.h"

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Function.h"

namespace llvm {

/// Custom state to propagate llvm type info to register CC assigner
struct M68kCCState : public CCState {
  ArrayRef<Type *> ArgTypeList;

  M68kCCState(ArrayRef<Type *> ArgTypes, CallingConv::ID CC, bool IsVarArg,
              MachineFunction &MF, SmallVectorImpl<CCValAssign> &Locs,
              LLVMContext &C)
      : CCState(CC, IsVarArg, MF, Locs, C), ArgTypeList(ArgTypes) {}
};

/// NOTE this function is used to select registers for formal arguments and call
/// FIXME: Handling on pointer arguments is not complete
inline bool CC_M68k_Any_AssignToReg(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                    CCValAssign::LocInfo &LocInfo,
                                    ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  const M68kCCState &CCInfo = static_cast<M68kCCState &>(State);

  static const MCPhysReg DataRegList[] = {M68k::D0, M68k::D1, M68k::A0,
                                          M68k::A1};

  // Address registers have %a register priority
  static const MCPhysReg AddrRegList[] = {
      M68k::A0,
      M68k::A1,
      M68k::D0,
      M68k::D1,
  };

  const auto &ArgTypes = CCInfo.ArgTypeList;
  auto I = ArgTypes.begin(), End = ArgTypes.end();
  int No = ValNo;
  while (No > 0 && I != End) {
    No -= (*I)->isIntegerTy(64) ? 2 : 1;
    ++I;
  }

  bool IsPtr = I != End && (*I)->isPointerTy();

  unsigned Reg =
      IsPtr ? State.AllocateReg(AddrRegList) : State.AllocateReg(DataRegList);

  if (Reg) {
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    return true;
  }

  return false;
}

} // namespace llvm

#endif // LLVM_LIB_TARGET_M68K_M68KCALLINGCONV_H
