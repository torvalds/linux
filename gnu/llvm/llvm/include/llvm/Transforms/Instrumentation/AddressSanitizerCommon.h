//===--------- Definition of the AddressSanitizer class ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares common infrastructure for AddressSanitizer and
// HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZERCOMMON_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZERCOMMON_H

#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"

namespace llvm {

class InterestingMemoryOperand {
public:
  Use *PtrUse;
  bool IsWrite;
  Type *OpType;
  TypeSize TypeStoreSize = TypeSize::getFixed(0);
  MaybeAlign Alignment;
  // The mask Value, if we're looking at a masked load/store.
  Value *MaybeMask;
  // The EVL Value, if we're looking at a vp intrinsic.
  Value *MaybeEVL;
  // The Stride Value, if we're looking at a strided load/store.
  Value *MaybeStride;

  InterestingMemoryOperand(Instruction *I, unsigned OperandNo, bool IsWrite,
                           class Type *OpType, MaybeAlign Alignment,
                           Value *MaybeMask = nullptr,
                           Value *MaybeEVL = nullptr,
                           Value *MaybeStride = nullptr)
      : IsWrite(IsWrite), OpType(OpType), Alignment(Alignment),
        MaybeMask(MaybeMask), MaybeEVL(MaybeEVL), MaybeStride(MaybeStride) {
    const DataLayout &DL = I->getDataLayout();
    TypeStoreSize = DL.getTypeStoreSizeInBits(OpType);
    PtrUse = &I->getOperandUse(OperandNo);
  }

  Instruction *getInsn() { return cast<Instruction>(PtrUse->getUser()); }

  Value *getPtr() { return PtrUse->get(); }
};

// Get AddressSanitizer parameters.
void getAddressSanitizerParams(const Triple &TargetTriple, int LongSize,
                               bool IsKasan, uint64_t *ShadowBase,
                               int *MappingScale, bool *OrShadowOffset);

} // namespace llvm

#endif
