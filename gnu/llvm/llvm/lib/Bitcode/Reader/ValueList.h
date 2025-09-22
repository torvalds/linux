//===-- Bitcode/Reader/ValueList.h - Number values --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class gives values and types Unique ID's.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_BITCODE_READER_VALUELIST_H
#define LLVM_LIB_BITCODE_READER_VALUELIST_H

#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <utility>
#include <vector>

namespace llvm {

class Error;
class Type;
class Value;

class BitcodeReaderValueList {
  /// Maps Value ID to pair of Value* and Type ID.
  std::vector<std::pair<WeakTrackingVH, unsigned>> ValuePtrs;

  /// Maximum number of valid references. Forward references exceeding the
  /// maximum must be invalid.
  unsigned RefsUpperBound;

  using MaterializeValueFnTy =
      std::function<Expected<Value *>(unsigned, BasicBlock *)>;
  MaterializeValueFnTy MaterializeValueFn;

public:
  BitcodeReaderValueList(size_t RefsUpperBound,
                         MaterializeValueFnTy MaterializeValueFn)
      : RefsUpperBound(std::min((size_t)std::numeric_limits<unsigned>::max(),
                                RefsUpperBound)),
        MaterializeValueFn(MaterializeValueFn) {}

  // vector compatibility methods
  unsigned size() const { return ValuePtrs.size(); }
  void resize(unsigned N) {
    ValuePtrs.resize(N);
  }
  void push_back(Value *V, unsigned TypeID) {
    ValuePtrs.emplace_back(V, TypeID);
  }

  void clear() {
    ValuePtrs.clear();
  }

  Value *operator[](unsigned i) const {
    assert(i < ValuePtrs.size());
    return ValuePtrs[i].first;
  }

  unsigned getTypeID(unsigned ValNo) const {
    assert(ValNo < ValuePtrs.size());
    return ValuePtrs[ValNo].second;
  }

  Value *back() const { return ValuePtrs.back().first; }
  void pop_back() {
    ValuePtrs.pop_back();
  }
  bool empty() const { return ValuePtrs.empty(); }

  void shrinkTo(unsigned N) {
    assert(N <= size() && "Invalid shrinkTo request!");
    ValuePtrs.resize(N);
  }

  void replaceValueWithoutRAUW(unsigned ValNo, Value *NewV) {
    assert(ValNo < ValuePtrs.size());
    ValuePtrs[ValNo].first = NewV;
  }

  Value *getValueFwdRef(unsigned Idx, Type *Ty, unsigned TyID,
                        BasicBlock *ConstExprInsertBB);

  Error assignValue(unsigned Idx, Value *V, unsigned TypeID);
};

} // end namespace llvm

#endif // LLVM_LIB_BITCODE_READER_VALUELIST_H
