//===--- SPIRVInlineAsmLowering.h - Inline Asm lowering ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file  describes how to lower LLVM inline asm calls to machine
// code calls for GlobalISel.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVINLINEASMLOWERING_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVINLINEASMLOWERING_H

#include "llvm/CodeGen/GlobalISel/InlineAsmLowering.h"

namespace llvm {

class SPIRVTargetLowering;

class SPIRVInlineAsmLowering : public InlineAsmLowering {
public:
  SPIRVInlineAsmLowering(const SPIRVTargetLowering &TLI);
  bool
  lowerAsmOperandForConstraint(Value *Val, StringRef Constraint,
                               std::vector<MachineOperand> &Ops,
                               MachineIRBuilder &MIRBuilder) const override;
};
} // end namespace llvm

#endif // LLVM_LIB_TARGET_SPIRV_SPIRVINLINEASMLOWERING_H
