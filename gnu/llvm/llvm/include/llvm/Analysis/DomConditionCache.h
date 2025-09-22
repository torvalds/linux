//===- llvm/Analysis/DomConditionCache.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Cache for branch conditions that affect a certain value for use by
// ValueTracking. Unlike AssumptionCache, this class does not perform any
// automatic analysis or invalidation. The caller is responsible for registering
// all relevant branches (and re-registering them if they change), and for
// removing invalidated values from the cache.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOMCONDITIONCACHE_H
#define LLVM_ANALYSIS_DOMCONDITIONCACHE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {

class Value;
class BranchInst;

class DomConditionCache {
private:
  /// A map of values about which a branch might be providing information.
  using AffectedValuesMap = DenseMap<Value *, SmallVector<BranchInst *, 1>>;
  AffectedValuesMap AffectedValues;

public:
  /// Add a branch condition to the cache.
  void registerBranch(BranchInst *BI);

  /// Remove a value from the cache, e.g. because it will be erased.
  void removeValue(Value *V) { AffectedValues.erase(V); }

  /// Access the list of branches which affect this value.
  ArrayRef<BranchInst *> conditionsFor(const Value *V) const {
    auto AVI = AffectedValues.find_as(const_cast<Value *>(V));
    if (AVI == AffectedValues.end())
      return ArrayRef<BranchInst *>();

    return AVI->second;
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_DOMCONDITIONCACHE_H
