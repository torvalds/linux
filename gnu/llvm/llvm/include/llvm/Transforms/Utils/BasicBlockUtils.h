//===- Transform/Utils/BasicBlockUtils.h - BasicBlock Utils -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform manipulations on basic blocks, and
// instructions contained within basic blocks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_BASICBLOCKUTILS_H
#define LLVM_TRANSFORMS_UTILS_BASICBLOCKUTILS_H

// FIXME: Move to this file: BasicBlock::removePredecessor, BB::splitBasicBlock

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include <cassert>

namespace llvm {
class BranchInst;
class LandingPadInst;
class Loop;
class PHINode;
template <typename PtrType> class SmallPtrSetImpl;
class BlockFrequencyInfo;
class BranchProbabilityInfo;
class DomTreeUpdater;
class Function;
class IRBuilderBase;
class LoopInfo;
class MDNode;
class MemoryDependenceResults;
class MemorySSAUpdater;
class PostDominatorTree;
class ReturnInst;
class TargetLibraryInfo;
class Value;

/// Replace contents of every block in \p BBs with single unreachable
/// instruction. If \p Updates is specified, collect all necessary DT updates
/// into this vector. If \p KeepOneInputPHIs is true, one-input Phis in
/// successors of blocks being deleted will be preserved.
void detachDeadBlocks(ArrayRef <BasicBlock *> BBs,
                      SmallVectorImpl<DominatorTree::UpdateType> *Updates,
                      bool KeepOneInputPHIs = false);

/// Delete the specified block, which must have no predecessors.
void DeleteDeadBlock(BasicBlock *BB, DomTreeUpdater *DTU = nullptr,
                     bool KeepOneInputPHIs = false);

/// Delete the specified blocks from \p BB. The set of deleted blocks must have
/// no predecessors that are not being deleted themselves. \p BBs must have no
/// duplicating blocks. If there are loops among this set of blocks, all
/// relevant loop info updates should be done before this function is called.
/// If \p KeepOneInputPHIs is true, one-input Phis in successors of blocks
/// being deleted will be preserved.
void DeleteDeadBlocks(ArrayRef <BasicBlock *> BBs,
                      DomTreeUpdater *DTU = nullptr,
                      bool KeepOneInputPHIs = false);

/// Delete all basic blocks from \p F that are not reachable from its entry
/// node. If \p KeepOneInputPHIs is true, one-input Phis in successors of
/// blocks being deleted will be preserved.
bool EliminateUnreachableBlocks(Function &F, DomTreeUpdater *DTU = nullptr,
                                bool KeepOneInputPHIs = false);

/// We know that BB has one predecessor. If there are any single-entry PHI nodes
/// in it, fold them away. This handles the case when all entries to the PHI
/// nodes in a block are guaranteed equal, such as when the block has exactly
/// one predecessor.
bool FoldSingleEntryPHINodes(BasicBlock *BB,
                             MemoryDependenceResults *MemDep = nullptr);

/// Examine each PHI in the given block and delete it if it is dead. Also
/// recursively delete any operands that become dead as a result. This includes
/// tracing the def-use list from the PHI to see if it is ultimately unused or
/// if it reaches an unused cycle. Return true if any PHIs were deleted.
bool DeleteDeadPHIs(BasicBlock *BB, const TargetLibraryInfo *TLI = nullptr,
                    MemorySSAUpdater *MSSAU = nullptr);

/// Attempts to merge a block into its predecessor, if possible. The return
/// value indicates success or failure.
/// By default do not merge blocks if BB's predecessor has multiple successors.
/// If PredecessorWithTwoSuccessors = true, the blocks can only be merged
/// if BB's Pred has a branch to BB and to AnotherBB, and BB has a single
/// successor Sing. In this case the branch will be updated with Sing instead of
/// BB, and BB will still be merged into its predecessor and removed.
/// If \p DT is not nullptr, update it directly; in that case, DTU must be
/// nullptr.
bool MergeBlockIntoPredecessor(BasicBlock *BB, DomTreeUpdater *DTU = nullptr,
                               LoopInfo *LI = nullptr,
                               MemorySSAUpdater *MSSAU = nullptr,
                               MemoryDependenceResults *MemDep = nullptr,
                               bool PredecessorWithTwoSuccessors = false,
                               DominatorTree *DT = nullptr);

/// Merge block(s) sucessors, if possible. Return true if at least two
/// of the blocks were merged together.
/// In order to merge, each block must be terminated by an unconditional
/// branch. If L is provided, then the blocks merged into their predecessors
/// must be in L. In addition, This utility calls on another utility:
/// MergeBlockIntoPredecessor. Blocks are successfully merged when the call to
/// MergeBlockIntoPredecessor returns true.
bool MergeBlockSuccessorsIntoGivenBlocks(
    SmallPtrSetImpl<BasicBlock *> &MergeBlocks, Loop *L = nullptr,
    DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr);

/// Try to remove redundant dbg.value instructions from given basic block.
/// Returns true if at least one instruction was removed. Remove redundant
/// pseudo ops when RemovePseudoOp is true.
bool RemoveRedundantDbgInstrs(BasicBlock *BB);

/// Replace all uses of an instruction (specified by BI) with a value, then
/// remove and delete the original instruction.
void ReplaceInstWithValue(BasicBlock::iterator &BI, Value *V);

/// Replace the instruction specified by BI with the instruction specified by I.
/// Copies DebugLoc from BI to I, if I doesn't already have a DebugLoc. The
/// original instruction is deleted and BI is updated to point to the new
/// instruction.
void ReplaceInstWithInst(BasicBlock *BB, BasicBlock::iterator &BI,
                         Instruction *I);

/// Replace the instruction specified by From with the instruction specified by
/// To. Copies DebugLoc from BI to I, if I doesn't already have a DebugLoc.
void ReplaceInstWithInst(Instruction *From, Instruction *To);

/// Check if we can prove that all paths starting from this block converge
/// to a block that either has a @llvm.experimental.deoptimize call
/// prior to its terminating return instruction or is terminated by unreachable.
/// All blocks in the traversed sequence must have an unique successor, maybe
/// except for the last one.
bool IsBlockFollowedByDeoptOrUnreachable(const BasicBlock *BB);

/// Option class for critical edge splitting.
///
/// This provides a builder interface for overriding the default options used
/// during critical edge splitting.
struct CriticalEdgeSplittingOptions {
  DominatorTree *DT;
  PostDominatorTree *PDT;
  LoopInfo *LI;
  MemorySSAUpdater *MSSAU;
  bool MergeIdenticalEdges = false;
  bool KeepOneInputPHIs = false;
  bool PreserveLCSSA = false;
  bool IgnoreUnreachableDests = false;
  /// SplitCriticalEdge is guaranteed to preserve loop-simplify form if LI is
  /// provided. If it cannot be preserved, no splitting will take place. If it
  /// is not set, preserve loop-simplify form if possible.
  bool PreserveLoopSimplify = true;

  CriticalEdgeSplittingOptions(DominatorTree *DT = nullptr,
                               LoopInfo *LI = nullptr,
                               MemorySSAUpdater *MSSAU = nullptr,
                               PostDominatorTree *PDT = nullptr)
      : DT(DT), PDT(PDT), LI(LI), MSSAU(MSSAU) {}

  CriticalEdgeSplittingOptions &setMergeIdenticalEdges() {
    MergeIdenticalEdges = true;
    return *this;
  }

  CriticalEdgeSplittingOptions &setKeepOneInputPHIs() {
    KeepOneInputPHIs = true;
    return *this;
  }

  CriticalEdgeSplittingOptions &setPreserveLCSSA() {
    PreserveLCSSA = true;
    return *this;
  }

  CriticalEdgeSplittingOptions &setIgnoreUnreachableDests() {
    IgnoreUnreachableDests = true;
    return *this;
  }

  CriticalEdgeSplittingOptions &unsetPreserveLoopSimplify() {
    PreserveLoopSimplify = false;
    return *this;
  }
};

/// When a loop exit edge is split, LCSSA form may require new PHIs in the new
/// exit block. This function inserts the new PHIs, as needed. Preds is a list
/// of preds inside the loop, SplitBB is the new loop exit block, and DestBB is
/// the old loop exit, now the successor of SplitBB.
void createPHIsForSplitLoopExit(ArrayRef<BasicBlock *> Preds,
                                BasicBlock *SplitBB, BasicBlock *DestBB);

/// If this edge is a critical edge, insert a new node to split the critical
/// edge. This will update the analyses passed in through the option struct.
/// This returns the new block if the edge was split, null otherwise.
///
/// If MergeIdenticalEdges in the options struct is true (not the default),
/// *all* edges from TI to the specified successor will be merged into the same
/// critical edge block. This is most commonly interesting with switch
/// instructions, which may have many edges to any one destination.  This
/// ensures that all edges to that dest go to one block instead of each going
/// to a different block, but isn't the standard definition of a "critical
/// edge".
///
/// It is invalid to call this function on a critical edge that starts at an
/// IndirectBrInst.  Splitting these edges will almost always create an invalid
/// program because the address of the new block won't be the one that is jumped
/// to.
BasicBlock *SplitCriticalEdge(Instruction *TI, unsigned SuccNum,
                              const CriticalEdgeSplittingOptions &Options =
                                  CriticalEdgeSplittingOptions(),
                              const Twine &BBName = "");

/// If it is known that an edge is critical, SplitKnownCriticalEdge can be
/// called directly, rather than calling SplitCriticalEdge first.
BasicBlock *SplitKnownCriticalEdge(Instruction *TI, unsigned SuccNum,
                                   const CriticalEdgeSplittingOptions &Options =
                                       CriticalEdgeSplittingOptions(),
                                   const Twine &BBName = "");

/// If an edge from Src to Dst is critical, split the edge and return true,
/// otherwise return false. This method requires that there be an edge between
/// the two blocks. It updates the analyses passed in the options struct
inline BasicBlock *
SplitCriticalEdge(BasicBlock *Src, BasicBlock *Dst,
                  const CriticalEdgeSplittingOptions &Options =
                      CriticalEdgeSplittingOptions()) {
  Instruction *TI = Src->getTerminator();
  unsigned i = 0;
  while (true) {
    assert(i != TI->getNumSuccessors() && "Edge doesn't exist!");
    if (TI->getSuccessor(i) == Dst)
      return SplitCriticalEdge(TI, i, Options);
    ++i;
  }
}

/// Loop over all of the edges in the CFG, breaking critical edges as they are
/// found. Returns the number of broken edges.
unsigned SplitAllCriticalEdges(Function &F,
                               const CriticalEdgeSplittingOptions &Options =
                                   CriticalEdgeSplittingOptions());

/// Split the edge connecting the specified blocks, and return the newly created
/// basic block between \p From and \p To.
BasicBlock *SplitEdge(BasicBlock *From, BasicBlock *To,
                      DominatorTree *DT = nullptr, LoopInfo *LI = nullptr,
                      MemorySSAUpdater *MSSAU = nullptr,
                      const Twine &BBName = "");

/// Sets the unwind edge of an instruction to a particular successor.
void setUnwindEdgeTo(Instruction *TI, BasicBlock *Succ);

/// Replaces all uses of OldPred with the NewPred block in all PHINodes in a
/// block.
void updatePhiNodes(BasicBlock *DestBB, BasicBlock *OldPred,
                    BasicBlock *NewPred, PHINode *Until = nullptr);

/// Split the edge connect the specficed blocks in the case that \p Succ is an
/// Exception Handling Block
BasicBlock *ehAwareSplitEdge(BasicBlock *BB, BasicBlock *Succ,
                             LandingPadInst *OriginalPad = nullptr,
                             PHINode *LandingPadReplacement = nullptr,
                             const CriticalEdgeSplittingOptions &Options =
                                 CriticalEdgeSplittingOptions(),
                             const Twine &BBName = "");

/// Split the specified block at the specified instruction.
///
/// If \p Before is true, splitBlockBefore handles the block
/// splitting. Otherwise, execution proceeds as described below.
///
/// Everything before \p SplitPt stays in \p Old and everything starting with \p
/// SplitPt moves to a new block. The two blocks are joined by an unconditional
/// branch. The new block with name \p BBName is returned.
///
/// FIXME: deprecated, switch to the DomTreeUpdater-based one.
BasicBlock *SplitBlock(BasicBlock *Old, BasicBlock::iterator SplitPt, DominatorTree *DT,
                       LoopInfo *LI = nullptr,
                       MemorySSAUpdater *MSSAU = nullptr,
                       const Twine &BBName = "", bool Before = false);
inline BasicBlock *SplitBlock(BasicBlock *Old, Instruction *SplitPt, DominatorTree *DT,
                       LoopInfo *LI = nullptr,
                       MemorySSAUpdater *MSSAU = nullptr,
                       const Twine &BBName = "", bool Before = false) {
  return SplitBlock(Old, SplitPt->getIterator(), DT, LI, MSSAU, BBName, Before);
}

/// Split the specified block at the specified instruction.
///
/// If \p Before is true, splitBlockBefore handles the block
/// splitting. Otherwise, execution proceeds as described below.
///
/// Everything before \p SplitPt stays in \p Old and everything starting with \p
/// SplitPt moves to a new block. The two blocks are joined by an unconditional
/// branch. The new block with name \p BBName is returned.
BasicBlock *SplitBlock(BasicBlock *Old, BasicBlock::iterator SplitPt,
                       DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr,
                       MemorySSAUpdater *MSSAU = nullptr,
                       const Twine &BBName = "", bool Before = false);
inline BasicBlock *SplitBlock(BasicBlock *Old, Instruction *SplitPt,
                       DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr,
                       MemorySSAUpdater *MSSAU = nullptr,
                       const Twine &BBName = "", bool Before = false) {
  return SplitBlock(Old, SplitPt->getIterator(), DTU, LI, MSSAU, BBName, Before);
}

/// Split the specified block at the specified instruction \p SplitPt.
/// All instructions before \p SplitPt are moved to a new block and all
/// instructions after \p SplitPt stay in the old block. The new block and the
/// old block are joined by inserting an unconditional branch to the end of the
/// new block. The new block with name \p BBName is returned.
BasicBlock *splitBlockBefore(BasicBlock *Old, BasicBlock::iterator SplitPt,
                             DomTreeUpdater *DTU, LoopInfo *LI,
                             MemorySSAUpdater *MSSAU, const Twine &BBName = "");
inline BasicBlock *splitBlockBefore(BasicBlock *Old, Instruction *SplitPt,
                             DomTreeUpdater *DTU, LoopInfo *LI,
                             MemorySSAUpdater *MSSAU, const Twine &BBName = "") {
  return splitBlockBefore(Old, SplitPt->getIterator(), DTU, LI, MSSAU, BBName);
}

/// This method introduces at least one new basic block into the function and
/// moves some of the predecessors of BB to be predecessors of the new block.
/// The new predecessors are indicated by the Preds array. The new block is
/// given a suffix of 'Suffix'. Returns new basic block to which predecessors
/// from Preds are now pointing.
///
/// If BB is a landingpad block then additional basicblock might be introduced.
/// It will have Suffix+".split_lp". See SplitLandingPadPredecessors for more
/// details on this case.
///
/// This currently updates the LLVM IR, DominatorTree, LoopInfo, and LCCSA but
/// no other analyses. In particular, it does not preserve LoopSimplify
/// (because it's complicated to handle the case where one of the edges being
/// split is an exit of a loop with other exits).
///
/// FIXME: deprecated, switch to the DomTreeUpdater-based one.
BasicBlock *SplitBlockPredecessors(BasicBlock *BB, ArrayRef<BasicBlock *> Preds,
                                   const char *Suffix, DominatorTree *DT,
                                   LoopInfo *LI = nullptr,
                                   MemorySSAUpdater *MSSAU = nullptr,
                                   bool PreserveLCSSA = false);

/// This method introduces at least one new basic block into the function and
/// moves some of the predecessors of BB to be predecessors of the new block.
/// The new predecessors are indicated by the Preds array. The new block is
/// given a suffix of 'Suffix'. Returns new basic block to which predecessors
/// from Preds are now pointing.
///
/// If BB is a landingpad block then additional basicblock might be introduced.
/// It will have Suffix+".split_lp". See SplitLandingPadPredecessors for more
/// details on this case.
///
/// This currently updates the LLVM IR, DominatorTree, LoopInfo, and LCCSA but
/// no other analyses. In particular, it does not preserve LoopSimplify
/// (because it's complicated to handle the case where one of the edges being
/// split is an exit of a loop with other exits).
BasicBlock *SplitBlockPredecessors(BasicBlock *BB, ArrayRef<BasicBlock *> Preds,
                                   const char *Suffix,
                                   DomTreeUpdater *DTU = nullptr,
                                   LoopInfo *LI = nullptr,
                                   MemorySSAUpdater *MSSAU = nullptr,
                                   bool PreserveLCSSA = false);

/// This method transforms the landing pad, OrigBB, by introducing two new basic
/// blocks into the function. One of those new basic blocks gets the
/// predecessors listed in Preds. The other basic block gets the remaining
/// predecessors of OrigBB. The landingpad instruction OrigBB is clone into both
/// of the new basic blocks. The new blocks are given the suffixes 'Suffix1' and
/// 'Suffix2', and are returned in the NewBBs vector.
///
/// This currently updates the LLVM IR, DominatorTree, LoopInfo, and LCCSA but
/// no other analyses. In particular, it does not preserve LoopSimplify
/// (because it's complicated to handle the case where one of the edges being
/// split is an exit of a loop with other exits).
void SplitLandingPadPredecessors(
    BasicBlock *OrigBB, ArrayRef<BasicBlock *> Preds, const char *Suffix,
    const char *Suffix2, SmallVectorImpl<BasicBlock *> &NewBBs,
    DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr,
    MemorySSAUpdater *MSSAU = nullptr, bool PreserveLCSSA = false);

/// This method duplicates the specified return instruction into a predecessor
/// which ends in an unconditional branch. If the return instruction returns a
/// value defined by a PHI, propagate the right value into the return. It
/// returns the new return instruction in the predecessor.
ReturnInst *FoldReturnIntoUncondBranch(ReturnInst *RI, BasicBlock *BB,
                                       BasicBlock *Pred,
                                       DomTreeUpdater *DTU = nullptr);

/// Split the containing block at the specified instruction - everything before
/// SplitBefore stays in the old basic block, and the rest of the instructions
/// in the BB are moved to a new block. The two blocks are connected by a
/// conditional branch (with value of Cmp being the condition).
/// Before:
///   Head
///   SplitBefore
///   Tail
/// After:
///   Head
///   if (Cond)
///     ThenBlock
///   SplitBefore
///   Tail
///
/// If \p ThenBlock is not specified, a new block will be created for it.
/// If \p Unreachable is true, the newly created block will end with
/// UnreachableInst, otherwise it branches to Tail.
/// Returns the NewBasicBlock's terminator.
///
/// Updates DTU and LI if given.
Instruction *SplitBlockAndInsertIfThen(Value *Cond, BasicBlock::iterator SplitBefore,
                                       bool Unreachable,
                                       MDNode *BranchWeights = nullptr,
                                       DomTreeUpdater *DTU = nullptr,
                                       LoopInfo *LI = nullptr,
                                       BasicBlock *ThenBlock = nullptr);

inline Instruction *SplitBlockAndInsertIfThen(Value *Cond, Instruction *SplitBefore,
                                       bool Unreachable,
                                       MDNode *BranchWeights = nullptr,
                                       DomTreeUpdater *DTU = nullptr,
                                       LoopInfo *LI = nullptr,
                                       BasicBlock *ThenBlock = nullptr) {
  return SplitBlockAndInsertIfThen(Cond, SplitBefore->getIterator(),
                                   Unreachable, BranchWeights, DTU, LI,
                                   ThenBlock);
}

/// Similar to SplitBlockAndInsertIfThen, but the inserted block is on the false
/// path of the branch.
Instruction *SplitBlockAndInsertIfElse(Value *Cond, BasicBlock::iterator SplitBefore,
                                       bool Unreachable,
                                       MDNode *BranchWeights = nullptr,
                                       DomTreeUpdater *DTU = nullptr,
                                       LoopInfo *LI = nullptr,
                                       BasicBlock *ElseBlock = nullptr);

inline Instruction *SplitBlockAndInsertIfElse(Value *Cond, Instruction *SplitBefore,
                                       bool Unreachable,
                                       MDNode *BranchWeights = nullptr,
                                       DomTreeUpdater *DTU = nullptr,
                                       LoopInfo *LI = nullptr,
                                       BasicBlock *ElseBlock = nullptr) {
  return SplitBlockAndInsertIfElse(Cond, SplitBefore->getIterator(),
                                   Unreachable, BranchWeights, DTU, LI,
                                   ElseBlock);
}

/// SplitBlockAndInsertIfThenElse is similar to SplitBlockAndInsertIfThen,
/// but also creates the ElseBlock.
/// Before:
///   Head
///   SplitBefore
///   Tail
/// After:
///   Head
///   if (Cond)
///     ThenBlock
///   else
///     ElseBlock
///   SplitBefore
///   Tail
///
/// Updates DT if given.
void SplitBlockAndInsertIfThenElse(Value *Cond,
                                   BasicBlock::iterator SplitBefore,
                                   Instruction **ThenTerm,
                                   Instruction **ElseTerm,
                                   MDNode *BranchWeights = nullptr,
                                   DomTreeUpdater *DTU = nullptr,
                                   LoopInfo *LI = nullptr);

inline void SplitBlockAndInsertIfThenElse(Value *Cond, Instruction *SplitBefore,
                                   Instruction **ThenTerm,
                                   Instruction **ElseTerm,
                                   MDNode *BranchWeights = nullptr,
                                   DomTreeUpdater *DTU = nullptr,
                                   LoopInfo *LI = nullptr)
{
  SplitBlockAndInsertIfThenElse(Cond, SplitBefore->getIterator(), ThenTerm,
                               ElseTerm, BranchWeights, DTU, LI);
}

/// Split the containing block at the specified instruction - everything before
/// SplitBefore stays in the old basic block, and the rest of the instructions
/// in the BB are moved to a new block. The two blocks are connected by a
/// conditional branch (with value of Cmp being the condition).
/// Before:
///   Head
///   SplitBefore
///   Tail
/// After:
///   Head
///   if (Cond)
///     TrueBlock
///   else
////    FalseBlock
///   SplitBefore
///   Tail
///
/// If \p ThenBlock is null, the resulting CFG won't contain the TrueBlock. If
/// \p ThenBlock is non-null and points to non-null BasicBlock pointer, that
/// block will be inserted as the TrueBlock. Otherwise a new block will be
/// created. Likewise for the \p ElseBlock parameter.
/// If \p UnreachableThen or \p UnreachableElse is true, the corresponding newly
/// created blocks will end with UnreachableInst, otherwise with branches to
/// Tail. The function will not modify existing basic blocks passed to it. The
/// caller must ensure that Tail is reachable from Head.
/// Returns the newly created blocks in \p ThenBlock and \p ElseBlock.
/// Updates DTU and LI if given.
void SplitBlockAndInsertIfThenElse(Value *Cond,
                                   BasicBlock::iterator SplitBefore,
                                   BasicBlock **ThenBlock,
                                   BasicBlock **ElseBlock,
                                   bool UnreachableThen = false,
                                   bool UnreachableElse = false,
                                   MDNode *BranchWeights = nullptr,
                                   DomTreeUpdater *DTU = nullptr,
                                   LoopInfo *LI = nullptr);

inline void SplitBlockAndInsertIfThenElse(Value *Cond, Instruction *SplitBefore,
                                   BasicBlock **ThenBlock,
                                   BasicBlock **ElseBlock,
                                   bool UnreachableThen = false,
                                   bool UnreachableElse = false,
                                   MDNode *BranchWeights = nullptr,
                                   DomTreeUpdater *DTU = nullptr,
                                   LoopInfo *LI = nullptr) {
  SplitBlockAndInsertIfThenElse(Cond, SplitBefore->getIterator(), ThenBlock,
    ElseBlock, UnreachableThen, UnreachableElse, BranchWeights, DTU, LI);
}

/// Insert a for (int i = 0; i < End; i++) loop structure (with the exception
/// that \p End is assumed > 0, and thus not checked on entry) at \p
/// SplitBefore.  Returns the first insert point in the loop body, and the
/// PHINode for the induction variable (i.e. "i" above).
std::pair<Instruction*, Value*>
SplitBlockAndInsertSimpleForLoop(Value *End, Instruction *SplitBefore);

/// Utility function for performing a given action on each lane of a vector
/// with \p EC elements.  To simplify porting legacy code, this defaults to
/// unrolling the implied loop for non-scalable element counts, but this is
/// not considered to be part of the contract of this routine, and is
/// expected to change in the future. The callback takes as arguments an
/// IRBuilder whose insert point is correctly set for instantiating the
/// given index, and a value which is (at runtime) the index to access.
/// This index *may* be a constant.
void SplitBlockAndInsertForEachLane(ElementCount EC, Type *IndexTy,
    Instruction *InsertBefore,
    std::function<void(IRBuilderBase&, Value*)> Func);

/// Utility function for performing a given action on each lane of a vector
/// with \p EVL effective length. EVL is assumed > 0. To simplify porting legacy
/// code, this defaults to unrolling the implied loop for non-scalable element
/// counts, but this is not considered to be part of the contract of this
/// routine, and is expected to change in the future. The callback takes as
/// arguments an IRBuilder whose insert point is correctly set for instantiating
/// the given index, and a value which is (at runtime) the index to access. This
/// index *may* be a constant.
void SplitBlockAndInsertForEachLane(
    Value *End, Instruction *InsertBefore,
    std::function<void(IRBuilderBase &, Value *)> Func);

/// Check whether BB is the merge point of a if-region.
/// If so, return the branch instruction that determines which entry into
/// BB will be taken.  Also, return by references the block that will be
/// entered from if the condition is true, and the block that will be
/// entered if the condition is false.
///
/// This does no checking to see if the true/false blocks have large or unsavory
/// instructions in them.
BranchInst *GetIfCondition(BasicBlock *BB, BasicBlock *&IfTrue,
                           BasicBlock *&IfFalse);

// Split critical edges where the source of the edge is an indirectbr
// instruction. This isn't always possible, but we can handle some easy cases.
// This is useful because MI is unable to split such critical edges,
// which means it will not be able to sink instructions along those edges.
// This is especially painful for indirect branches with many successors, where
// we end up having to prepare all outgoing values in the origin block.
//
// Our normal algorithm for splitting critical edges requires us to update
// the outgoing edges of the edge origin block, but for an indirectbr this
// is hard, since it would require finding and updating the block addresses
// the indirect branch uses. But if a block only has a single indirectbr
// predecessor, with the others being regular branches, we can do it in a
// different way.
// Say we have A -> D, B -> D, I -> D where only I -> D is an indirectbr.
// We can split D into D0 and D1, where D0 contains only the PHIs from D,
// and D1 is the D block body. We can then duplicate D0 as D0A and D0B, and
// create the following structure:
// A -> D0A, B -> D0A, I -> D0B, D0A -> D1, D0B -> D1
// If BPI and BFI aren't non-null, BPI/BFI will be updated accordingly.
// When `IgnoreBlocksWithoutPHI` is set to `true` critical edges leading to a
// block without phi-instructions will not be split.
bool SplitIndirectBrCriticalEdges(Function &F, bool IgnoreBlocksWithoutPHI,
                                  BranchProbabilityInfo *BPI = nullptr,
                                  BlockFrequencyInfo *BFI = nullptr);

/// Given a set of incoming and outgoing blocks, create a "hub" such that every
/// edge from an incoming block InBB to an outgoing block OutBB is now split
/// into two edges, one from InBB to the hub and another from the hub to
/// OutBB. The hub consists of a series of guard blocks, one for each outgoing
/// block. Each guard block conditionally branches to the corresponding outgoing
/// block, or the next guard block in the chain. These guard blocks are returned
/// in the argument vector.
///
/// Since the control flow edges from InBB to OutBB have now been replaced, the
/// function also updates any PHINodes in OutBB. For each such PHINode, the
/// operands corresponding to incoming blocks are moved to a new PHINode in the
/// hub, and the hub is made an operand of the original PHINode.
///
/// Input CFG:
/// ----------
///
///                    Def
///                     |
///                     v
///           In1      In2
///            |        |
///            |        |
///            v        v
///  Foo ---> Out1     Out2
///                     |
///                     v
///                    Use
///
///
/// Create hub: Incoming = {In1, In2}, Outgoing = {Out1, Out2}
/// ----------------------------------------------------------
///
///             Def
///              |
///              v
///  In1        In2          Foo
///   |    Hub   |            |
///   |    + - - | - - +      |
///   |    '     v     '      V
///   +------> Guard1 -----> Out1
///        '     |     '
///        '     v     '
///        '   Guard2 -----> Out2
///        '           '      |
///        + - - - - - +      |
///                           v
///                          Use
///
/// Limitations:
/// -----------
/// 1. This assumes that all terminators in the CFG are direct branches (the
///    "br" instruction). The presence of any other control flow such as
///    indirectbr, switch or callbr will cause an assert.
///
/// 2. The updates to the PHINodes are not sufficient to restore SSA
///    form. Consider a definition Def, its use Use, incoming block In2 and
///    outgoing block Out2, such that:
///    a. In2 is reachable from D or contains D.
///    b. U is reachable from Out2 or is contained in Out2.
///    c. U is not a PHINode if U is contained in Out2.
///
///    Clearly, Def dominates Out2 since the program is valid SSA. But when the
///    hub is introduced, there is a new path through the hub along which Use is
///    reachable from entry without passing through Def, and SSA is no longer
///    valid. To fix this, we need to look at all the blocks post-dominated by
///    the hub on the one hand, and dominated by Out2 on the other. This is left
///    for the caller to accomplish, since each specific use of this function
///    may have additional information which simplifies this fixup. For example,
///    see restoreSSA() in the UnifyLoopExits pass.
BasicBlock *CreateControlFlowHub(
    DomTreeUpdater *DTU, SmallVectorImpl<BasicBlock *> &GuardBlocks,
    const SetVector<BasicBlock *> &Predecessors,
    const SetVector<BasicBlock *> &Successors, const StringRef Prefix,
    std::optional<unsigned> MaxControlFlowBooleans = std::nullopt);

// Utility function for inverting branch condition and for swapping its
// successors
void InvertBranch(BranchInst *PBI, IRBuilderBase &Builder);

// Check whether the function only has simple terminator:
// br/brcond/unreachable/ret
bool hasOnlySimpleTerminator(const Function &F);

// Returns true if these basic blocks belong to a presplit coroutine and the
// edge corresponds to the 'default' case in the switch statement in the
// pattern:
//
// %0 = call i8 @llvm.coro.suspend(token none, i1 false)
// switch i8 %0, label %suspend [i8 0, label %resume
//                               i8 1, label %cleanup]
//
// i.e. the edge to the `%suspend` BB. This edge is special in that it will
// be elided by coroutine lowering (coro-split), and the `%suspend` BB needs
// to be kept as-is. It's not a real CFG edge - post-lowering, it will end
// up being a `ret`, and it must be thus lowerable to support symmetric
// transfer. For example:
//  - this edge is not a loop exit edge if encountered in a loop (and should
//    be ignored)
//  - must not be split for PGO instrumentation, for example.
bool isPresplitCoroSuspendExitEdge(const BasicBlock &Src,
                                   const BasicBlock &Dest);
} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_BASICBLOCKUTILS_H
