//===- SelectionDAGAddressAnalysis.h - DAG Address Analysis -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAGADDRESSANALYSIS_H
#define LLVM_CODEGEN_SELECTIONDAGADDRESSANALYSIS_H

#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include <cstdint>

namespace llvm {

class SelectionDAG;

/// Helper struct to parse and store a memory address as base + index + offset.
/// We ignore sign extensions when it is safe to do so.
/// The following two expressions are not equivalent. To differentiate we need
/// to store whether there was a sign extension involved in the index
/// computation.
///  (load (i64 add (i64 copyfromreg %c)
///                 (i64 signextend (add (i8 load %index)
///                                      (i8 1))))
/// vs
///
/// (load (i64 add (i64 copyfromreg %c)
///                (i64 signextend (i32 add (i32 signextend (i8 load %index))
///                                         (i32 1)))))
class BaseIndexOffset {
private:
  SDValue Base;
  SDValue Index;
  std::optional<int64_t> Offset;
  bool IsIndexSignExt = false;

public:
  BaseIndexOffset() = default;
  BaseIndexOffset(SDValue Base, SDValue Index, bool IsIndexSignExt)
      : Base(Base), Index(Index), IsIndexSignExt(IsIndexSignExt) {}
  BaseIndexOffset(SDValue Base, SDValue Index, int64_t Offset,
                  bool IsIndexSignExt)
      : Base(Base), Index(Index), Offset(Offset),
        IsIndexSignExt(IsIndexSignExt) {}

  SDValue getBase() { return Base; }
  SDValue getBase() const { return Base; }
  SDValue getIndex() { return Index; }
  SDValue getIndex() const { return Index; }
  void addToOffset(int64_t VectorOff) {
    Offset = Offset.value_or(0) + VectorOff;
  }
  bool hasValidOffset() const { return Offset.has_value(); }
  int64_t getOffset() const { return *Offset; }

  // Returns true if `Other` and `*this` are both some offset from the same base
  // pointer. In that case, `Off` is set to the offset between `*this` and
  // `Other` (negative if `Other` is before `*this`).
  bool equalBaseIndex(const BaseIndexOffset &Other, const SelectionDAG &DAG,
                      int64_t &Off) const;

  bool equalBaseIndex(const BaseIndexOffset &Other,
                      const SelectionDAG &DAG) const {
    int64_t Off;
    return equalBaseIndex(Other, DAG, Off);
  }

  // Returns true if `Other` (with size `OtherSize`) can be proven to be fully
  // contained in `*this` (with size `Size`).
  bool contains(const SelectionDAG &DAG, int64_t BitSize,
                const BaseIndexOffset &Other, int64_t OtherBitSize,
                int64_t &BitOffset) const;

  bool contains(const SelectionDAG &DAG, int64_t BitSize,
                const BaseIndexOffset &Other, int64_t OtherBitSize) const {
    int64_t BitOffset;
    return contains(DAG, BitSize, Other, OtherBitSize, BitOffset);
  }

  // Returns true `Op0` and `Op1` can be proven to alias/not alias, in
  // which case `IsAlias` is set to true/false.
  static bool computeAliasing(const SDNode *Op0, const LocationSize NumBytes0,
                              const SDNode *Op1, const LocationSize NumBytes1,
                              const SelectionDAG &DAG, bool &IsAlias);

  /// Parses tree in N for base, index, offset addresses.
  static BaseIndexOffset match(const SDNode *N, const SelectionDAG &DAG);

  void print(raw_ostream& OS) const;
  void dump() const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SELECTIONDAGADDRESSANALYSIS_H
