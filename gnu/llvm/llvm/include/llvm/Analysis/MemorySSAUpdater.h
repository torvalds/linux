//===- MemorySSAUpdater.h - Memory SSA Updater-------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// An automatic updater for MemorySSA that handles arbitrary insertion,
// deletion, and moves.  It performs phi insertion where necessary, and
// automatically updates the MemorySSA IR to be correct.
// While updating loads or removing instructions is often easy enough to not
// need this, updating stores should generally not be attemped outside this
// API.
//
// Basic API usage:
// Create the memory access you want for the instruction (this is mainly so
// we know where it is, without having to duplicate the entire set of create
// functions MemorySSA supports).
// Call insertDef or insertUse depending on whether it's a MemoryUse or a
// MemoryDef.
// That's it.
//
// For moving, first, move the instruction itself using the normal SSA
// instruction moving API, then just call moveBefore, moveAfter,or moveTo with
// the right arguments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYSSAUPDATER_H
#define LLVM_ANALYSIS_MEMORYSSAUPDATER_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/CFGDiff.h"

namespace llvm {

class BasicBlock;
class DominatorTree;
class Instruction;
class LoopBlocksRPO;
template <typename T, unsigned int N> class SmallSetVector;

using ValueToValueMapTy = ValueMap<const Value *, WeakTrackingVH>;
using PhiToDefMap = SmallDenseMap<MemoryPhi *, MemoryAccess *>;
using CFGUpdate = cfg::Update<BasicBlock *>;

class MemorySSAUpdater {
private:
  MemorySSA *MSSA;

  /// We use WeakVH rather than a costly deletion to deal with dangling pointers.
  /// MemoryPhis are created eagerly and sometimes get zapped shortly afterwards.
  SmallVector<WeakVH, 16> InsertedPHIs;

  SmallPtrSet<BasicBlock *, 8> VisitedBlocks;
  SmallSet<AssertingVH<MemoryPhi>, 8> NonOptPhis;

public:
  MemorySSAUpdater(MemorySSA *MSSA) : MSSA(MSSA) {}

  /// Insert a definition into the MemorySSA IR.  RenameUses will rename any use
  /// below the new def block (and any inserted phis).  RenameUses should be set
  /// to true if the definition may cause new aliases for loads below it.  This
  /// is not the case for hoisting or sinking or other forms of code *movement*.
  /// It *is* the case for straight code insertion.
  /// For example:
  /// store a
  /// if (foo) { }
  /// load a
  ///
  /// Moving the store into the if block, and calling insertDef, does not
  /// require RenameUses.
  /// However, changing it to:
  /// store a
  /// if (foo) { store b }
  /// load a
  /// Where a mayalias b, *does* require RenameUses be set to true.
  void insertDef(MemoryDef *Def, bool RenameUses = false);
  void insertUse(MemoryUse *Use, bool RenameUses = false);
  /// Update the MemoryPhi in `To` following an edge deletion between `From` and
  /// `To`. If `To` becomes unreachable, a call to removeBlocks should be made.
  void removeEdge(BasicBlock *From, BasicBlock *To);
  /// Update the MemoryPhi in `To` to have a single incoming edge from `From`,
  /// following a CFG change that replaced multiple edges (switch) with a direct
  /// branch.
  void removeDuplicatePhiEdgesBetween(const BasicBlock *From,
                                      const BasicBlock *To);
  /// Update MemorySSA when inserting a unique backedge block for a loop.
  void updatePhisWhenInsertingUniqueBackedgeBlock(BasicBlock *LoopHeader,
                                                  BasicBlock *LoopPreheader,
                                                  BasicBlock *BackedgeBlock);
  /// Update MemorySSA after a loop was cloned, given the blocks in RPO order,
  /// the exit blocks and a 1:1 mapping of all blocks and instructions
  /// cloned. This involves duplicating all defs and uses in the cloned blocks
  /// Updating phi nodes in exit block successors is done separately.
  void updateForClonedLoop(const LoopBlocksRPO &LoopBlocks,
                           ArrayRef<BasicBlock *> ExitBlocks,
                           const ValueToValueMapTy &VM,
                           bool IgnoreIncomingWithNoClones = false);
  // Block BB was fully or partially cloned into its predecessor P1. Map
  // contains the 1:1 mapping of instructions cloned and VM[BB]=P1.
  void updateForClonedBlockIntoPred(BasicBlock *BB, BasicBlock *P1,
                                    const ValueToValueMapTy &VM);
  /// Update phi nodes in exit block successors following cloning. Exit blocks
  /// that were not cloned don't have additional predecessors added.
  void updateExitBlocksForClonedLoop(ArrayRef<BasicBlock *> ExitBlocks,
                                     const ValueToValueMapTy &VMap,
                                     DominatorTree &DT);
  void updateExitBlocksForClonedLoop(
      ArrayRef<BasicBlock *> ExitBlocks,
      ArrayRef<std::unique_ptr<ValueToValueMapTy>> VMaps, DominatorTree &DT);

  /// Apply CFG updates, analogous with the DT edge updates. By default, the
  /// DT is assumed to be already up to date. If UpdateDTFirst is true, first
  /// update the DT with the same updates.
  void applyUpdates(ArrayRef<CFGUpdate> Updates, DominatorTree &DT,
                    bool UpdateDTFirst = false);
  /// Apply CFG insert updates, analogous with the DT edge updates.
  void applyInsertUpdates(ArrayRef<CFGUpdate> Updates, DominatorTree &DT);

  void moveBefore(MemoryUseOrDef *What, MemoryUseOrDef *Where);
  void moveAfter(MemoryUseOrDef *What, MemoryUseOrDef *Where);
  void moveToPlace(MemoryUseOrDef *What, BasicBlock *BB,
                   MemorySSA::InsertionPlace Where);
  /// `From` block was spliced into `From` and `To`. There is a CFG edge from
  /// `From` to `To`. Move all accesses from `From` to `To` starting at
  /// instruction `Start`. `To` is newly created BB, so empty of
  /// MemorySSA::MemoryAccesses. Edges are already updated, so successors of
  /// `To` with MPhi nodes need to update incoming block.
  /// |------|        |------|
  /// | From |        | From |
  /// |      |        |------|
  /// |      |           ||
  /// |      |   =>      \/
  /// |      |        |------|  <- Start
  /// |      |        |  To  |
  /// |------|        |------|
  void moveAllAfterSpliceBlocks(BasicBlock *From, BasicBlock *To,
                                Instruction *Start);
  /// `From` block was merged into `To`. There is a CFG edge from `To` to
  /// `From`.`To` still branches to `From`, but all instructions were moved and
  /// `From` is now an empty block; `From` is about to be deleted. Move all
  /// accesses from `From` to `To` starting at instruction `Start`. `To` may
  /// have multiple successors, `From` has a single predecessor. `From` may have
  /// successors with MPhi nodes, replace their incoming block with `To`.
  /// |------|        |------|
  /// |  To  |        |  To  |
  /// |------|        |      |
  ///    ||      =>   |      |
  ///    \/           |      |
  /// |------|        |      |  <- Start
  /// | From |        |      |
  /// |------|        |------|
  void moveAllAfterMergeBlocks(BasicBlock *From, BasicBlock *To,
                               Instruction *Start);
  /// A new empty BasicBlock (New) now branches directly to Old. Some of
  /// Old's predecessors (Preds) are now branching to New instead of Old.
  /// If New is the only predecessor, move Old's Phi, if present, to New.
  /// Otherwise, add a new Phi in New with appropriate incoming values, and
  /// update the incoming values in Old's Phi node too, if present.
  void wireOldPredecessorsToNewImmediatePredecessor(
      BasicBlock *Old, BasicBlock *New, ArrayRef<BasicBlock *> Preds,
      bool IdenticalEdgesWereMerged = true);
  // The below are utility functions. Other than creation of accesses to pass
  // to insertDef, and removeAccess to remove accesses, you should generally
  // not attempt to update memoryssa yourself. It is very non-trivial to get
  // the edge cases right, and the above calls already operate in near-optimal
  // time bounds.

  /// Create a MemoryAccess in MemorySSA at a specified point in a block.
  ///
  /// When used by itself, this method will only insert the new MemoryAccess
  /// into the access list, but not make any other changes, such as inserting
  /// MemoryPHI nodes, or updating users to point to the new MemoryAccess. You
  /// must specify a correct Definition in this case.
  ///
  /// Usually, this API is instead combined with insertUse() or insertDef(),
  /// which will perform all the necessary MSSA updates. If these APIs are used,
  /// then nullptr can be used as Definition, as the correct defining access
  /// will be automatically determined.
  ///
  /// Note: If a MemoryAccess already exists for I, this function will make it
  /// inaccessible and it *must* have removeMemoryAccess called on it.
  MemoryAccess *createMemoryAccessInBB(Instruction *I, MemoryAccess *Definition,
                                       const BasicBlock *BB,
                                       MemorySSA::InsertionPlace Point);

  MemoryAccess *createMemoryAccessInBB(Instruction *I, MemoryAccess *Definition,
                                       const BasicBlock *BB,
                                       MemorySSA::InsertionPlace Point,
                                       bool CreationMustSucceed);

  /// Create a MemoryAccess in MemorySSA before an existing MemoryAccess.
  ///
  /// See createMemoryAccessInBB() for usage details.
  MemoryUseOrDef *createMemoryAccessBefore(Instruction *I,
                                           MemoryAccess *Definition,
                                           MemoryUseOrDef *InsertPt);
  /// Create a MemoryAccess in MemorySSA after an existing MemoryAccess.
  ///
  /// See createMemoryAccessInBB() for usage details.
  MemoryUseOrDef *createMemoryAccessAfter(Instruction *I,
                                          MemoryAccess *Definition,
                                          MemoryAccess *InsertPt);

  /// Remove a MemoryAccess from MemorySSA, including updating all
  /// definitions and uses.
  /// This should be called when a memory instruction that has a MemoryAccess
  /// associated with it is erased from the program.  For example, if a store or
  /// load is simply erased (not replaced), removeMemoryAccess should be called
  /// on the MemoryAccess for that store/load.
  void removeMemoryAccess(MemoryAccess *, bool OptimizePhis = false);

  /// Remove MemoryAccess for a given instruction, if a MemoryAccess exists.
  /// This should be called when an instruction (load/store) is deleted from
  /// the program.
  void removeMemoryAccess(const Instruction *I, bool OptimizePhis = false) {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(I))
      removeMemoryAccess(MA, OptimizePhis);
  }

  /// Remove all MemoryAcceses in a set of BasicBlocks about to be deleted.
  /// Assumption we make here: all uses of deleted defs and phi must either
  /// occur in blocks about to be deleted (thus will be deleted as well), or
  /// they occur in phis that will simply lose an incoming value.
  /// Deleted blocks still have successor info, but their predecessor edges and
  /// Phi nodes may already be updated. Instructions in DeadBlocks should be
  /// deleted after this call.
  void removeBlocks(const SmallSetVector<BasicBlock *, 8> &DeadBlocks);

  /// Instruction I will be changed to an unreachable. Remove all accesses in
  /// I's block that follow I (inclusive), and update the Phis in the blocks'
  /// successors.
  void changeToUnreachable(const Instruction *I);

  /// Get handle on MemorySSA.
  MemorySSA* getMemorySSA() const { return MSSA; }

private:
  // Move What before Where in the MemorySSA IR.
  template <class WhereType>
  void moveTo(MemoryUseOrDef *What, BasicBlock *BB, WhereType Where);
  // Move all memory accesses from `From` to `To` starting at `Start`.
  // Restrictions apply, see public wrappers of this method.
  void moveAllAccesses(BasicBlock *From, BasicBlock *To, Instruction *Start);
  MemoryAccess *getPreviousDef(MemoryAccess *);
  MemoryAccess *getPreviousDefInBlock(MemoryAccess *);
  MemoryAccess *
  getPreviousDefFromEnd(BasicBlock *,
                        DenseMap<BasicBlock *, TrackingVH<MemoryAccess>> &);
  MemoryAccess *
  getPreviousDefRecursive(BasicBlock *,
                          DenseMap<BasicBlock *, TrackingVH<MemoryAccess>> &);
  MemoryAccess *recursePhi(MemoryAccess *Phi);
  MemoryAccess *tryRemoveTrivialPhi(MemoryPhi *Phi);
  template <class RangeType>
  MemoryAccess *tryRemoveTrivialPhi(MemoryPhi *Phi, RangeType &Operands);
  void tryRemoveTrivialPhis(ArrayRef<WeakVH> UpdatedPHIs);
  void fixupDefs(const SmallVectorImpl<WeakVH> &);
  // Clone all uses and defs from BB to NewBB given a 1:1 map of all
  // instructions and blocks cloned, and a map of MemoryPhi : Definition
  // (MemoryAccess Phi or Def). VMap maps old instructions to cloned
  // instructions and old blocks to cloned blocks. MPhiMap, is created in the
  // caller of this private method, and maps existing MemoryPhis to new
  // definitions that new MemoryAccesses must point to. These definitions may
  // not necessarily be MemoryPhis themselves, they may be MemoryDefs. As such,
  // the map is between MemoryPhis and MemoryAccesses, where the MemoryAccesses
  // may be MemoryPhis or MemoryDefs and not MemoryUses.
  // If CloneWasSimplified = true, the clone was exact. Otherwise, assume that
  // the clone involved simplifications that may have: (1) turned a MemoryUse
  // into an instruction that MemorySSA has no representation for, or (2) turned
  // a MemoryDef into a MemoryUse or an instruction that MemorySSA has no
  // representation for. No other cases are supported.
  void cloneUsesAndDefs(BasicBlock *BB, BasicBlock *NewBB,
                        const ValueToValueMapTy &VMap, PhiToDefMap &MPhiMap,
                        bool CloneWasSimplified = false);
  template <typename Iter>
  void privateUpdateExitBlocksForClonedLoop(ArrayRef<BasicBlock *> ExitBlocks,
                                            Iter ValuesBegin, Iter ValuesEnd,
                                            DominatorTree &DT);
  void applyInsertUpdates(ArrayRef<CFGUpdate>, DominatorTree &DT,
                          const GraphDiff<BasicBlock *> *GD);
};
} // end namespace llvm

#endif // LLVM_ANALYSIS_MEMORYSSAUPDATER_H
