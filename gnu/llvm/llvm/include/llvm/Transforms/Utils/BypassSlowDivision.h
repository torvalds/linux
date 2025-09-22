//===- llvm/Transforms/Utils/BypassSlowDivision.h ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains an optimization for div and rem on architectures that
// execute short instructions significantly faster than longer instructions.
// For example, on Intel Atom 32-bit divides are slow enough that during
// runtime it is profitable to check the value of the operands, and if they are
// positive and less than 256 use an unsigned 8-bit divide.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_BYPASSSLOWDIVISION_H
#define LLVM_TRANSFORMS_UTILS_BYPASSSLOWDIVISION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/IR/ValueHandle.h"
#include <cstdint>

namespace llvm {

class BasicBlock;
class Value;

struct DivRemMapKey {
  bool SignedOp;
  AssertingVH<Value> Dividend;
  AssertingVH<Value> Divisor;

  DivRemMapKey() = default;

  DivRemMapKey(bool InSignedOp, Value *InDividend, Value *InDivisor)
      : SignedOp(InSignedOp), Dividend(InDividend), Divisor(InDivisor) {}
};

template <> struct DenseMapInfo<DivRemMapKey> {
  static bool isEqual(const DivRemMapKey &Val1, const DivRemMapKey &Val2) {
    return Val1.SignedOp == Val2.SignedOp && Val1.Dividend == Val2.Dividend &&
           Val1.Divisor == Val2.Divisor;
  }

  static DivRemMapKey getEmptyKey() {
    return DivRemMapKey(false, nullptr, nullptr);
  }

  static DivRemMapKey getTombstoneKey() {
    return DivRemMapKey(true, nullptr, nullptr);
  }

  static unsigned getHashValue(const DivRemMapKey &Val) {
    return (unsigned)(reinterpret_cast<uintptr_t>(
                          static_cast<Value *>(Val.Dividend)) ^
                      reinterpret_cast<uintptr_t>(
                          static_cast<Value *>(Val.Divisor))) ^
           (unsigned)Val.SignedOp;
  }
};

/// This optimization identifies DIV instructions in a BB that can be
/// profitably bypassed and carried out with a shorter, faster divide.
///
/// This optimization may add basic blocks immediately after BB; for obvious
/// reasons, you shouldn't pass those blocks to bypassSlowDivision.
bool bypassSlowDivision(
    BasicBlock *BB, const DenseMap<unsigned int, unsigned int> &BypassWidth);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_BYPASSSLOWDIVISION_H
