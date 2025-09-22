//===- AggressiveInstCombineInternal.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the instruction pattern combiner classes.
// Currently, it handles pattern expressions for:
//  * Truncate instruction
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_AGGRESSIVEINSTCOMBINE_COMBINEINTERNAL_H
#define LLVM_LIB_TRANSFORMS_AGGRESSIVEINSTCOMBINE_COMBINEINTERNAL_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// TruncInstCombine - looks for expression graphs dominated by trunc
// instructions and for each eligible graph, it will create a reduced bit-width
// expression and replace the old expression with this new one and remove the
// old one. Eligible expression graph is such that:
//   1. Contains only supported instructions.
//   2. Supported leaves: ZExtInst, SExtInst, TruncInst and Constant value.
//   3. Can be evaluated into type with reduced legal bit-width (or Trunc type).
//   4. All instructions in the graph must not have users outside the graph.
//      Only exception is for {ZExt, SExt}Inst with operand type equal to the
//      new reduced type chosen in (3).
//
// The motivation for this optimization is that evaluating and expression using
// smaller bit-width is preferable, especially for vectorization where we can
// fit more values in one vectorized instruction. In addition, this optimization
// may decrease the number of cast instructions, but will not increase it.
//===----------------------------------------------------------------------===//

namespace llvm {
class AssumptionCache;
class DataLayout;
class DominatorTree;
class Function;
class Instruction;
class TargetLibraryInfo;
class TruncInst;
class Type;
class Value;

class TruncInstCombine {
  AssumptionCache &AC;
  TargetLibraryInfo &TLI;
  const DataLayout &DL;
  const DominatorTree &DT;

  /// List of all TruncInst instructions to be processed.
  SmallVector<TruncInst *, 4> Worklist;

  /// Current processed TruncInst instruction.
  TruncInst *CurrentTruncInst = nullptr;

  /// Information per each instruction in the expression graph.
  struct Info {
    /// Number of LSBs that are needed to generate a valid expression.
    unsigned ValidBitWidth = 0;
    /// Minimum number of LSBs needed to generate the ValidBitWidth.
    unsigned MinBitWidth = 0;
    /// The reduced value generated to replace the old instruction.
    Value *NewValue = nullptr;
  };
  /// An ordered map representing expression graph post-dominated by current
  /// processed TruncInst. It maps each instruction in the graph to its Info
  /// structure. The map is ordered such that each instruction appears before
  /// all other instructions in the graph that uses it.
  MapVector<Instruction *, Info> InstInfoMap;

public:
  TruncInstCombine(AssumptionCache &AC, TargetLibraryInfo &TLI,
                   const DataLayout &DL, const DominatorTree &DT)
      : AC(AC), TLI(TLI), DL(DL), DT(DT) {}

  /// Perform TruncInst pattern optimization on given function.
  bool run(Function &F);

private:
  /// Build expression graph dominated by the /p CurrentTruncInst and append it
  /// to the InstInfoMap container.
  ///
  /// \return true only if succeed to generate an eligible sub expression graph.
  bool buildTruncExpressionGraph();

  /// Calculate the minimal allowed bit-width of the chain ending with the
  /// currently visited truncate's operand.
  ///
  /// \return minimum number of bits to which the chain ending with the
  /// truncate's operand can be shrunk to.
  unsigned getMinBitWidth();

  /// Build an expression graph dominated by the current processed TruncInst and
  /// Check if it is eligible to be reduced to a smaller type.
  ///
  /// \return the scalar version of the new type to be used for the reduced
  ///         expression graph, or nullptr if the expression graph is not
  ///         eligible to be reduced.
  Type *getBestTruncatedType();

  KnownBits computeKnownBits(const Value *V) const {
    return llvm::computeKnownBits(V, DL, /*Depth=*/0, &AC,
                                  /*CtxI=*/cast<Instruction>(CurrentTruncInst),
                                  &DT);
  }

  unsigned ComputeNumSignBits(const Value *V) const {
    return llvm::ComputeNumSignBits(
        V, DL, /*Depth=*/0, &AC, /*CtxI=*/cast<Instruction>(CurrentTruncInst),
        &DT);
  }

  /// Given a \p V value and a \p SclTy scalar type return the generated reduced
  /// value of \p V based on the type \p SclTy.
  ///
  /// \param V value to be reduced.
  /// \param SclTy scalar version of new type to reduce to.
  /// \return the new reduced value.
  Value *getReducedOperand(Value *V, Type *SclTy);

  /// Create a new expression graph using the reduced /p SclTy type and replace
  /// the old expression graph with it. Also erase all instructions in the old
  /// graph, except those that are still needed outside the graph.
  ///
  /// \param SclTy scalar version of new type to reduce expression graph into.
  void ReduceExpressionGraph(Type *SclTy);
};
} // end namespace llvm.

#endif
