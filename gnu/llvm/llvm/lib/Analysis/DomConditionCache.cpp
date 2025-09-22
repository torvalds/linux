//===- DomConditionCache.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DomConditionCache.h"
#include "llvm/Analysis/ValueTracking.h"
using namespace llvm;

static void findAffectedValues(Value *Cond,
                               SmallVectorImpl<Value *> &Affected) {
  auto InsertAffected = [&Affected](Value *V) { Affected.push_back(V); };
  findValuesAffectedByCondition(Cond, /*IsAssume=*/false, InsertAffected);
}

void DomConditionCache::registerBranch(BranchInst *BI) {
  assert(BI->isConditional() && "Must be conditional branch");
  SmallVector<Value *, 16> Affected;
  findAffectedValues(BI->getCondition(), Affected);
  for (Value *V : Affected) {
    auto &AV = AffectedValues[V];
    if (!is_contained(AV, BI))
      AV.push_back(BI);
  }
}
