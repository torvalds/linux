//===- PtrUseVisitor.cpp - InstVisitors over a pointers uses --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implementation of the pointer use visitors.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/PtrUseVisitor.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

void detail::PtrUseVisitorBase::enqueueUsers(Instruction &I) {
  for (Use &U : I.uses()) {
    if (VisitedUses.insert(&U).second) {
      UseToVisit NewU = {
        UseToVisit::UseAndIsOffsetKnownPair(&U, IsOffsetKnown),
        Offset
      };
      Worklist.push_back(std::move(NewU));
    }
  }
}

bool detail::PtrUseVisitorBase::adjustOffsetForGEP(GetElementPtrInst &GEPI) {
  if (!IsOffsetKnown)
    return false;

  APInt TmpOffset(DL.getIndexTypeSizeInBits(GEPI.getType()), 0);
  if (GEPI.accumulateConstantOffset(DL, TmpOffset)) {
    Offset += TmpOffset.sextOrTrunc(Offset.getBitWidth());
    return true;
  }

  return false;
}
