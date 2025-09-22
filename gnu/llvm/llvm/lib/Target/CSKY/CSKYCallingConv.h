//=== CSKYCallingConv.h - CSKY Custom Calling Convention Routines -*-C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom routines for the CSKY Calling Convention that
// aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKYCALLINGCONV_H
#define LLVM_LIB_TARGET_CSKY_CSKYCALLINGCONV_H

#include "CSKY.h"
#include "CSKYSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {

static bool CC_CSKY_ABIV2_SOFT_64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                  CCValAssign::LocInfo &LocInfo,
                                  ISD::ArgFlagsTy &ArgFlags, CCState &State) {

  static const MCPhysReg ArgGPRs[] = {CSKY::R0, CSKY::R1, CSKY::R2, CSKY::R3};
  Register Reg = State.AllocateReg(ArgGPRs);
  LocVT = MVT::i32;
  if (!Reg) {
    unsigned StackOffset = State.AllocateStack(8, Align(4));
    State.addLoc(
        CCValAssign::getMem(ValNo, ValVT, StackOffset, LocVT, LocInfo));
    return true;
  }
  if (!State.AllocateReg(ArgGPRs))
    State.AllocateStack(4, Align(4));
  State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  return true;
}

static bool Ret_CSKY_ABIV2_SOFT_64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                   CCValAssign::LocInfo &LocInfo,
                                   ISD::ArgFlagsTy &ArgFlags, CCState &State) {

  static const MCPhysReg ArgGPRs[] = {CSKY::R0, CSKY::R1};
  Register Reg = State.AllocateReg(ArgGPRs);
  LocVT = MVT::i32;
  if (!Reg)
    return false;

  if (!State.AllocateReg(ArgGPRs))
    return false;

  State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  return true;
}

} // namespace llvm

#endif
