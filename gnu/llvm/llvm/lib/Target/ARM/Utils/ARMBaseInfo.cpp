//===-- ARMBaseInfo.cpp - ARM Base encoding information------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides basic encoding and assembly information for ARM.
//
//===----------------------------------------------------------------------===//
#include "ARMBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

using namespace llvm;
namespace llvm {
ARM::PredBlockMask expandPredBlockMask(ARM::PredBlockMask BlockMask,
                                       ARMVCC::VPTCodes Kind) {
  using PredBlockMask = ARM::PredBlockMask;
  assert(Kind != ARMVCC::None && "Cannot expand a mask with None!");
  assert(llvm::countr_zero((unsigned)BlockMask) != 0 && "Mask is already full");

  auto ChooseMask = [&](PredBlockMask AddedThen, PredBlockMask AddedElse) {
    return Kind == ARMVCC::Then ? AddedThen : AddedElse;
  };

  switch (BlockMask) {
  case PredBlockMask::T:
    return ChooseMask(PredBlockMask::TT, PredBlockMask::TE);
  case PredBlockMask::TT:
    return ChooseMask(PredBlockMask::TTT, PredBlockMask::TTE);
  case PredBlockMask::TE:
    return ChooseMask(PredBlockMask::TET, PredBlockMask::TEE);
  case PredBlockMask::TTT:
    return ChooseMask(PredBlockMask::TTTT, PredBlockMask::TTTE);
  case PredBlockMask::TTE:
    return ChooseMask(PredBlockMask::TTET, PredBlockMask::TTEE);
  case PredBlockMask::TET:
    return ChooseMask(PredBlockMask::TETT, PredBlockMask::TETE);
  case PredBlockMask::TEE:
    return ChooseMask(PredBlockMask::TEET, PredBlockMask::TEEE);
  default:
    llvm_unreachable("Unknown Mask");
  }
}

namespace ARMSysReg {

// lookup system register using 12-bit SYSm value.
// Note: the search is uniqued using M1 mask
const MClassSysReg *lookupMClassSysRegBy12bitSYSmValue(unsigned SYSm) {
  return lookupMClassSysRegByM1Encoding12(SYSm);
}

// returns APSR with _<bits> qualifier.
// Note: ARMv7-M deprecates using MSR APSR without a _<bits> qualifier
const MClassSysReg *lookupMClassSysRegAPSRNonDeprecated(unsigned SYSm) {
  return lookupMClassSysRegByM2M3Encoding8((1<<9)|(SYSm & 0xFF));
}

// lookup system registers using 8-bit SYSm value
const MClassSysReg *lookupMClassSysRegBy8bitSYSmValue(unsigned SYSm) {
  return ARMSysReg::lookupMClassSysRegByM2M3Encoding8((1<<8)|(SYSm & 0xFF));
}

#define GET_MCLASSSYSREG_IMPL
#include "ARMGenSystemRegister.inc"

} // end namespace ARMSysReg

namespace ARMBankedReg {
#define GET_BANKEDREG_IMPL
#include "ARMGenSystemRegister.inc"
} // end namespce ARMSysReg
} // end namespace llvm
