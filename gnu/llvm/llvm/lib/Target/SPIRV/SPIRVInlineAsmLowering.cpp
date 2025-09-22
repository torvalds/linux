//===--- SPIRVInlineAsmLowering.cpp - Inline Asm lowering -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the lowering of LLVM inline asm calls to machine code
// calls for GlobalISel.
//
//===----------------------------------------------------------------------===//

#include "SPIRVInlineAsmLowering.h"
#include "SPIRVSubtarget.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsSPIRV.h"

using namespace llvm;

SPIRVInlineAsmLowering::SPIRVInlineAsmLowering(const SPIRVTargetLowering &TLI)
    : InlineAsmLowering(&TLI) {}

bool SPIRVInlineAsmLowering::lowerAsmOperandForConstraint(
    Value *Val, StringRef Constraint, std::vector<MachineOperand> &Ops,
    MachineIRBuilder &MIRBuilder) const {
  Value *ValOp = nullptr;
  if (isa<ConstantInt>(Val)) {
    ValOp = Val;
  } else if (ConstantFP *CFP = dyn_cast<ConstantFP>(Val)) {
    Ops.push_back(MachineOperand::CreateFPImm(CFP));
    return true;
  } else if (auto *II = dyn_cast<IntrinsicInst>(Val)) {
    if (II->getIntrinsicID() == Intrinsic::spv_track_constant) {
      if (isa<ConstantInt>(II->getOperand(0))) {
        ValOp = II->getOperand(0);
      } else if (ConstantFP *CFP = dyn_cast<ConstantFP>(II->getOperand(0))) {
        Ops.push_back(MachineOperand::CreateFPImm(CFP));
        return true;
      }
    }
  }
  return ValOp ? InlineAsmLowering::lowerAsmOperandForConstraint(
                     ValOp, Constraint, Ops, MIRBuilder)
               : false;
}
