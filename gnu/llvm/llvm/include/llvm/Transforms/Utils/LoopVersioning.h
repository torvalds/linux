//===- LoopVersioning.h - Utility to version a loop -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a utility class to perform loop versioning.  The versioned
// loop speculates that otherwise may-aliasing memory accesses don't overlap and
// emits checks to prove this.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOOPVERSIONING_H
#define LLVM_TRANSFORMS_UTILS_LOOPVERSIONING_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

namespace llvm {

class Loop;
class SCEVPredicate;
class ScalarEvolution;
class LoopAccessInfo;
class LoopInfo;
struct RuntimeCheckingPtrGroup;
typedef std::pair<const RuntimeCheckingPtrGroup *,
                  const RuntimeCheckingPtrGroup *>
    RuntimePointerCheck;

template <typename T> class ArrayRef;

/// This class emits a version of the loop where run-time checks ensure
/// that may-alias pointers can't overlap.
///
/// It currently only supports single-exit loops and assumes that the loop
/// already has a preheader.
class LoopVersioning {
public:
  /// Expects LoopAccessInfo, Loop, LoopInfo, DominatorTree as input.
  /// It uses runtime check provided by the user. If \p UseLAIChecks is true,
  /// we will retain the default checks made by LAI. Otherwise, construct an
  /// object having no checks and we expect the user to add them.
  LoopVersioning(const LoopAccessInfo &LAI,
                 ArrayRef<RuntimePointerCheck> Checks, Loop *L, LoopInfo *LI,
                 DominatorTree *DT, ScalarEvolution *SE);

  /// Performs the CFG manipulation part of versioning the loop including
  /// the DominatorTree and LoopInfo updates.
  ///
  /// The loop that was used to construct the class will be the "versioned" loop
  /// i.e. the loop that will receive control if all the memchecks pass.
  ///
  /// This allows the loop transform pass to operate on the same loop regardless
  /// of whether versioning was necessary or not:
  ///
  ///    for each loop L:
  ///        analyze L
  ///        if versioning is necessary version L
  ///        transform L
  void versionLoop() { versionLoop(findDefsUsedOutsideOfLoop(VersionedLoop)); }

  /// Same but if the client has already precomputed the set of values
  /// used outside the loop, this API will allows passing that.
  void versionLoop(const SmallVectorImpl<Instruction *> &DefsUsedOutside);

  /// Returns the versioned loop.  Control flows here if pointers in the
  /// loop don't alias (i.e. all memchecks passed).  (This loop is actually the
  /// same as the original loop that we got constructed with.)
  Loop *getVersionedLoop() { return VersionedLoop; }

  /// Returns the fall-back loop.  Control flows here if pointers in the
  /// loop may alias (i.e. one of the memchecks failed).
  Loop *getNonVersionedLoop() { return NonVersionedLoop; }

  /// Annotate memory instructions in the versioned loop with no-alias
  /// metadata based on the memchecks issued.
  ///
  /// This is just wrapper that calls prepareNoAliasMetadata and
  /// annotateInstWithNoAlias on the instructions of the versioned loop.
  void annotateLoopWithNoAlias();

  /// Set up the aliasing scopes based on the memchecks.  This needs to
  /// be called before the first call to annotateInstWithNoAlias.
  void prepareNoAliasMetadata();

  /// Add the noalias annotations to \p VersionedInst.
  ///
  /// \p OrigInst is the instruction corresponding to \p VersionedInst in the
  /// original loop.  Initialize the aliasing scopes with
  /// prepareNoAliasMetadata once before this can be called.
  void annotateInstWithNoAlias(Instruction *VersionedInst,
                               const Instruction *OrigInst);

private:
  /// Adds the necessary PHI nodes for the versioned loops based on the
  /// loop-defined values used outside of the loop.
  ///
  /// This needs to be called after versionLoop if there are defs in the loop
  /// that are used outside the loop.
  void addPHINodes(const SmallVectorImpl<Instruction *> &DefsUsedOutside);

  /// Add the noalias annotations to \p I.  Initialize the aliasing
  /// scopes with prepareNoAliasMetadata once before this can be called.
  void annotateInstWithNoAlias(Instruction *I) {
    annotateInstWithNoAlias(I, I);
  }

  /// The original loop.  This becomes the "versioned" one.  I.e.,
  /// control flows here if pointers in the loop don't alias.
  Loop *VersionedLoop;
  /// The fall-back loop.  I.e. control flows here if pointers in the
  /// loop may alias (memchecks failed).
  Loop *NonVersionedLoop = nullptr;

  /// This maps the instructions from VersionedLoop to their counterpart
  /// in NonVersionedLoop.
  ValueToValueMapTy VMap;

  /// The set of alias checks that we are versioning for.
  SmallVector<RuntimePointerCheck, 4> AliasChecks;

  /// The set of SCEV checks that we are versioning for.
  const SCEVPredicate &Preds;

  /// Maps a pointer to the pointer checking group that the pointer
  /// belongs to.
  DenseMap<const Value *, const RuntimeCheckingPtrGroup *> PtrToGroup;

  /// The alias scope corresponding to a pointer checking group.
  DenseMap<const RuntimeCheckingPtrGroup *, MDNode *> GroupToScope;

  /// The list of alias scopes that a pointer checking group can't alias.
  DenseMap<const RuntimeCheckingPtrGroup *, MDNode *>
      GroupToNonAliasingScopeList;

  /// Analyses used.
  const LoopAccessInfo &LAI;
  LoopInfo *LI;
  DominatorTree *DT;
  ScalarEvolution *SE;
};

/// Expose LoopVersioning as a pass.  Currently this is only used for
/// unit-testing.  It adds all memchecks necessary to remove all may-aliasing
/// array accesses from the loop.
class LoopVersioningPass : public PassInfoMixin<LoopVersioningPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
}

#endif
