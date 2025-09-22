//===- CodeMetrics.h - Code cost measurements -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements various weight measurements for code, helping
// the Inliner and other passes decide whether to duplicate its contents.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CODEMETRICS_H
#define LLVM_ANALYSIS_CODEMETRICS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/InstructionCost.h"

namespace llvm {
class AssumptionCache;
class BasicBlock;
class Instruction;
class Loop;
class Function;
template <class T> class SmallPtrSetImpl;
class TargetTransformInfo;
class Value;

enum struct ConvergenceKind { None, Controlled, ExtendedLoop, Uncontrolled };

/// Utility to calculate the size and a few similar metrics for a set
/// of basic blocks.
struct CodeMetrics {
  /// True if this function contains a call to setjmp or other functions
  /// with attribute "returns twice" without having the attribute itself.
  bool exposesReturnsTwice = false;

  /// True if this function calls itself.
  bool isRecursive = false;

  /// True if this function cannot be duplicated.
  ///
  /// True if this function contains one or more indirect branches, or it contains
  /// one or more 'noduplicate' instructions.
  bool notDuplicatable = false;

  /// The kind of convergence specified in this function.
  ConvergenceKind Convergence = ConvergenceKind::None;

  /// True if this function calls alloca (in the C sense).
  bool usesDynamicAlloca = false;

  /// Code size cost of the analyzed blocks.
  InstructionCost NumInsts = 0;

  /// Number of analyzed blocks.
  unsigned NumBlocks = false;

  /// Keeps track of basic block code size estimates.
  DenseMap<const BasicBlock *, InstructionCost> NumBBInsts;

  /// Keep track of the number of calls to 'big' functions.
  unsigned NumCalls = false;

  /// The number of calls to internal functions with a single caller.
  ///
  /// These are likely targets for future inlining, likely exposed by
  /// interleaved devirtualization.
  unsigned NumInlineCandidates = 0;

  /// How many instructions produce vector values.
  ///
  /// The inliner is more aggressive with inlining vector kernels.
  unsigned NumVectorInsts = 0;

  /// How many 'ret' instructions the blocks contain.
  unsigned NumRets = 0;

  /// Add information about a block to the current state.
  void analyzeBasicBlock(const BasicBlock *BB, const TargetTransformInfo &TTI,
                         const SmallPtrSetImpl<const Value *> &EphValues,
                         bool PrepareForLTO = false, const Loop *L = nullptr);

  /// Collect a loop's ephemeral values (those used only by an assume
  /// or similar intrinsics in the loop).
  static void collectEphemeralValues(const Loop *L, AssumptionCache *AC,
                                     SmallPtrSetImpl<const Value *> &EphValues);

  /// Collect a functions's ephemeral values (those used only by an
  /// assume or similar intrinsics in the function).
  static void collectEphemeralValues(const Function *L, AssumptionCache *AC,
                                     SmallPtrSetImpl<const Value *> &EphValues);
};

}

#endif
