//===- JumpThreading.h - thread control through conditional BBs -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// See the comments on JumpThreadingPass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_JUMPTHREADING_H
#define LLVM_TRANSFORMS_SCALAR_JUMPTHREADING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <optional>
#include <utility>

namespace llvm {

class AAResults;
class BasicBlock;
class BinaryOperator;
class BranchInst;
class CmpInst;
class Constant;
class Function;
class Instruction;
class IntrinsicInst;
class LazyValueInfo;
class LoadInst;
class PHINode;
class SelectInst;
class SwitchInst;
class TargetLibraryInfo;
class TargetTransformInfo;
class Value;

/// A private "module" namespace for types and utilities used by
/// JumpThreading.
/// These are implementation details and should not be used by clients.
namespace jumpthreading {

// These are at global scope so static functions can use them too.
using PredValueInfo = SmallVectorImpl<std::pair<Constant *, BasicBlock *>>;
using PredValueInfoTy = SmallVector<std::pair<Constant *, BasicBlock *>, 8>;

// This is used to keep track of what kind of constant we're currently hoping
// to find.
enum ConstantPreference { WantInteger, WantBlockAddress };

} // end namespace jumpthreading

/// This pass performs 'jump threading', which looks at blocks that have
/// multiple predecessors and multiple successors.  If one or more of the
/// predecessors of the block can be proven to always jump to one of the
/// successors, we forward the edge from the predecessor to the successor by
/// duplicating the contents of this block.
///
/// An example of when this can occur is code like this:
///
///   if () { ...
///     X = 4;
///   }
///   if (X < 3) {
///
/// In this case, the unconditional branch at the end of the first if can be
/// revectored to the false side of the second if.
class JumpThreadingPass : public PassInfoMixin<JumpThreadingPass> {
  Function *F = nullptr;
  FunctionAnalysisManager *FAM = nullptr;
  TargetLibraryInfo *TLI = nullptr;
  TargetTransformInfo *TTI = nullptr;
  LazyValueInfo *LVI = nullptr;
  AAResults *AA = nullptr;
  std::unique_ptr<DomTreeUpdater> DTU;
  std::optional<BlockFrequencyInfo *> BFI;
  std::optional<BranchProbabilityInfo *> BPI;
  bool ChangedSinceLastAnalysisUpdate = false;
  bool HasGuards = false;
#ifndef LLVM_ENABLE_ABI_BREAKING_CHECKS
  SmallPtrSet<const BasicBlock *, 16> LoopHeaders;
#else
  SmallSet<AssertingVH<const BasicBlock>, 16> LoopHeaders;
#endif

  unsigned BBDupThreshold;
  unsigned DefaultBBDupThreshold;

public:
  JumpThreadingPass(int T = -1);

  // Glue for old PM.
  bool runImpl(Function &F, FunctionAnalysisManager *FAM,
               TargetLibraryInfo *TLI, TargetTransformInfo *TTI,
               LazyValueInfo *LVI, AAResults *AA,
               std::unique_ptr<DomTreeUpdater> DTU,
               std::optional<BlockFrequencyInfo *> BFI,
               std::optional<BranchProbabilityInfo *> BPI);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  DomTreeUpdater *getDomTreeUpdater() const { return DTU.get(); }
  void findLoopHeaders(Function &F);
  bool processBlock(BasicBlock *BB);
  bool maybeMergeBasicBlockIntoOnlyPred(BasicBlock *BB);
  void updateSSA(BasicBlock *BB, BasicBlock *NewBB,
                 ValueToValueMapTy &ValueMapping);
  void cloneInstructions(ValueToValueMapTy &ValueMapping,
                         BasicBlock::iterator BI, BasicBlock::iterator BE,
                         BasicBlock *NewBB, BasicBlock *PredBB);
  bool tryThreadEdge(BasicBlock *BB,
                     const SmallVectorImpl<BasicBlock *> &PredBBs,
                     BasicBlock *SuccBB);
  void threadEdge(BasicBlock *BB, const SmallVectorImpl<BasicBlock *> &PredBBs,
                  BasicBlock *SuccBB);
  bool duplicateCondBranchOnPHIIntoPred(
      BasicBlock *BB, const SmallVectorImpl<BasicBlock *> &PredBBs);

  bool computeValueKnownInPredecessorsImpl(
      Value *V, BasicBlock *BB, jumpthreading::PredValueInfo &Result,
      jumpthreading::ConstantPreference Preference,
      SmallPtrSet<Value *, 4> &RecursionSet, Instruction *CxtI = nullptr);
  bool
  computeValueKnownInPredecessors(Value *V, BasicBlock *BB,
                                  jumpthreading::PredValueInfo &Result,
                                  jumpthreading::ConstantPreference Preference,
                                  Instruction *CxtI = nullptr) {
    SmallPtrSet<Value *, 4> RecursionSet;
    return computeValueKnownInPredecessorsImpl(V, BB, Result, Preference,
                                               RecursionSet, CxtI);
  }

  Constant *evaluateOnPredecessorEdge(BasicBlock *BB, BasicBlock *PredPredBB,
                                      Value *cond, const DataLayout &DL);
  bool maybethreadThroughTwoBasicBlocks(BasicBlock *BB, Value *Cond);
  void threadThroughTwoBasicBlocks(BasicBlock *PredPredBB, BasicBlock *PredBB,
                                   BasicBlock *BB, BasicBlock *SuccBB);
  bool processThreadableEdges(Value *Cond, BasicBlock *BB,
                              jumpthreading::ConstantPreference Preference,
                              Instruction *CxtI = nullptr);

  bool processBranchOnPHI(PHINode *PN);
  bool processBranchOnXOR(BinaryOperator *BO);
  bool processImpliedCondition(BasicBlock *BB);

  bool simplifyPartiallyRedundantLoad(LoadInst *LI);
  void unfoldSelectInstr(BasicBlock *Pred, BasicBlock *BB, SelectInst *SI,
                         PHINode *SIUse, unsigned Idx);

  bool tryToUnfoldSelect(CmpInst *CondCmp, BasicBlock *BB);
  bool tryToUnfoldSelect(SwitchInst *SI, BasicBlock *BB);
  bool tryToUnfoldSelectInCurrBB(BasicBlock *BB);

  bool processGuards(BasicBlock *BB);
  bool threadGuard(BasicBlock *BB, IntrinsicInst *Guard, BranchInst *BI);

private:
  BasicBlock *splitBlockPreds(BasicBlock *BB, ArrayRef<BasicBlock *> Preds,
                              const char *Suffix);
  void updateBlockFreqAndEdgeWeight(BasicBlock *PredBB, BasicBlock *BB,
                                    BasicBlock *NewBB, BasicBlock *SuccBB,
                                    BlockFrequencyInfo *BFI,
                                    BranchProbabilityInfo *BPI,
                                    bool HasProfile);
  /// Check if the block has profile metadata for its outgoing edges.
  bool doesBlockHaveProfileData(BasicBlock *BB);

  /// Returns analysis preserved by the pass.
  PreservedAnalyses getPreservedAnalysis() const;

  /// Helper function to run "external" analysis in the middle of JumpThreading.
  /// It takes care of updating/invalidating other existing analysis
  /// before/after  running the "external" one.
  template <typename AnalysisT>
  typename AnalysisT::Result *runExternalAnalysis();

  /// Returns an existing instance of BPI if any, otherwise nullptr. By
  /// "existing" we mean either cached result provided by FunctionAnalysisManger
  /// or created by preceding call to 'getOrCreateBPI'.
  BranchProbabilityInfo *getBPI();

  /// Returns an existing instance of BFI if any, otherwise nullptr. By
  /// "existing" we mean either cached result provided by FunctionAnalysisManger
  /// or created by preceding call to 'getOrCreateBFI'.
  BlockFrequencyInfo *getBFI();

  /// Returns an existing instance of BPI if any, otherwise:
  ///   if 'HasProfile' is true creates new instance through
  ///   FunctionAnalysisManager, otherwise nullptr.
  BranchProbabilityInfo *getOrCreateBPI(bool Force = false);

  /// Returns an existing instance of BFI if any, otherwise:
  ///   if 'HasProfile' is true creates new instance through
  ///   FunctionAnalysisManager, otherwise nullptr.
  BlockFrequencyInfo *getOrCreateBFI(bool Force = false);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_JUMPTHREADING_H
