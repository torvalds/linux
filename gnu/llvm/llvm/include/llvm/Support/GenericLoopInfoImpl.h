//===- GenericLoopInfoImp.h - Generic Loop Info Implementation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This fle contains the implementation of GenericLoopInfo. It should only be
// included in files that explicitly instantiate a GenericLoopInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GENERICLOOPINFOIMPL_H
#define LLVM_SUPPORT_GENERICLOOPINFOIMPL_H

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/Support/GenericLoopInfo.h"

namespace llvm {

//===----------------------------------------------------------------------===//
// APIs for simple analysis of the loop. See header notes.

/// getExitingBlocks - Return all blocks inside the loop that have successors
/// outside of the loop.  These are the blocks _inside of the current loop_
/// which branch out.  The returned list is always unique.
///
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::getExitingBlocks(
    SmallVectorImpl<BlockT *> &ExitingBlocks) const {
  assert(!isInvalid() && "Loop not in a valid state!");
  for (const auto BB : blocks())
    for (auto *Succ : children<BlockT *>(BB))
      if (!contains(Succ)) {
        // Not in current loop? It must be an exit block.
        ExitingBlocks.push_back(BB);
        break;
      }
}

/// getExitingBlock - If getExitingBlocks would return exactly one block,
/// return that block. Otherwise return null.
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getExitingBlock() const {
  assert(!isInvalid() && "Loop not in a valid state!");
  auto notInLoop = [&](BlockT *BB) { return !contains(BB); };
  auto isExitBlock = [&](BlockT *BB, bool AllowRepeats) -> BlockT * {
    assert(!AllowRepeats && "Unexpected parameter value.");
    // Child not in current loop?  It must be an exit block.
    return any_of(children<BlockT *>(BB), notInLoop) ? BB : nullptr;
  };

  return find_singleton<BlockT>(blocks(), isExitBlock);
}

/// getExitBlocks - Return all of the successor blocks of this loop.  These
/// are the blocks _outside of the current loop_ which are branched to.
///
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::getExitBlocks(
    SmallVectorImpl<BlockT *> &ExitBlocks) const {
  assert(!isInvalid() && "Loop not in a valid state!");
  for (const auto BB : blocks())
    for (auto *Succ : children<BlockT *>(BB))
      if (!contains(Succ))
        // Not in current loop? It must be an exit block.
        ExitBlocks.push_back(Succ);
}

/// getExitBlock - If getExitBlocks would return exactly one block,
/// return that block. Otherwise return null.
template <class BlockT, class LoopT>
std::pair<BlockT *, bool> getExitBlockHelper(const LoopBase<BlockT, LoopT> *L,
                                             bool Unique) {
  assert(!L->isInvalid() && "Loop not in a valid state!");
  auto notInLoop = [&](BlockT *BB,
                       bool AllowRepeats) -> std::pair<BlockT *, bool> {
    assert(AllowRepeats == Unique && "Unexpected parameter value.");
    return {!L->contains(BB) ? BB : nullptr, false};
  };
  auto singleExitBlock = [&](BlockT *BB,
                             bool AllowRepeats) -> std::pair<BlockT *, bool> {
    assert(AllowRepeats == Unique && "Unexpected parameter value.");
    return find_singleton_nested<BlockT>(children<BlockT *>(BB), notInLoop,
                                         AllowRepeats);
  };
  return find_singleton_nested<BlockT>(L->blocks(), singleExitBlock, Unique);
}

template <class BlockT, class LoopT>
bool LoopBase<BlockT, LoopT>::hasNoExitBlocks() const {
  auto RC = getExitBlockHelper(this, false);
  if (RC.second)
    // found multiple exit blocks
    return false;
  // return true if there is no exit block
  return !RC.first;
}

/// getExitBlock - If getExitBlocks would return exactly one block,
/// return that block. Otherwise return null.
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getExitBlock() const {
  return getExitBlockHelper(this, false).first;
}

template <class BlockT, class LoopT>
bool LoopBase<BlockT, LoopT>::hasDedicatedExits() const {
  // Each predecessor of each exit block of a normal loop is contained
  // within the loop.
  SmallVector<BlockT *, 4> UniqueExitBlocks;
  getUniqueExitBlocks(UniqueExitBlocks);
  for (BlockT *EB : UniqueExitBlocks)
    for (BlockT *Predecessor : inverse_children<BlockT *>(EB))
      if (!contains(Predecessor))
        return false;
  // All the requirements are met.
  return true;
}

// Helper function to get unique loop exits. Pred is a predicate pointing to
// BasicBlocks in a loop which should be considered to find loop exits.
template <class BlockT, class LoopT, typename PredicateT>
void getUniqueExitBlocksHelper(const LoopT *L,
                               SmallVectorImpl<BlockT *> &ExitBlocks,
                               PredicateT Pred) {
  assert(!L->isInvalid() && "Loop not in a valid state!");
  SmallPtrSet<BlockT *, 32> Visited;
  auto Filtered = make_filter_range(L->blocks(), Pred);
  for (BlockT *BB : Filtered)
    for (BlockT *Successor : children<BlockT *>(BB))
      if (!L->contains(Successor))
        if (Visited.insert(Successor).second)
          ExitBlocks.push_back(Successor);
}

template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::getUniqueExitBlocks(
    SmallVectorImpl<BlockT *> &ExitBlocks) const {
  getUniqueExitBlocksHelper(this, ExitBlocks,
                            [](const BlockT *BB) { return true; });
}

template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::getUniqueNonLatchExitBlocks(
    SmallVectorImpl<BlockT *> &ExitBlocks) const {
  const BlockT *Latch = getLoopLatch();
  assert(Latch && "Latch block must exists");
  getUniqueExitBlocksHelper(this, ExitBlocks,
                            [Latch](const BlockT *BB) { return BB != Latch; });
}

template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getUniqueExitBlock() const {
  return getExitBlockHelper(this, true).first;
}

/// getExitEdges - Return all pairs of (_inside_block_,_outside_block_).
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::getExitEdges(
    SmallVectorImpl<Edge> &ExitEdges) const {
  assert(!isInvalid() && "Loop not in a valid state!");
  for (const auto BB : blocks())
    for (auto *Succ : children<BlockT *>(BB))
      if (!contains(Succ))
        // Not in current loop? It must be an exit block.
        ExitEdges.emplace_back(BB, Succ);
}

namespace detail {
template <class BlockT>
using has_hoist_check = decltype(&BlockT::isLegalToHoistInto);

template <class BlockT>
using detect_has_hoist_check = llvm::is_detected<has_hoist_check, BlockT>;

/// SFINAE functions that dispatch to the isLegalToHoistInto member function or
/// return false, if it doesn't exist.
template <class BlockT> bool isLegalToHoistInto(BlockT *Block) {
  if constexpr (detect_has_hoist_check<BlockT>::value)
    return Block->isLegalToHoistInto();
  return false;
}
} // namespace detail

/// getLoopPreheader - If there is a preheader for this loop, return it.  A
/// loop has a preheader if there is only one edge to the header of the loop
/// from outside of the loop and it is legal to hoist instructions into the
/// predecessor. If this is the case, the block branching to the header of the
/// loop is the preheader node.
///
/// This method returns null if there is no preheader for the loop.
///
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getLoopPreheader() const {
  assert(!isInvalid() && "Loop not in a valid state!");
  // Keep track of nodes outside the loop branching to the header...
  BlockT *Out = getLoopPredecessor();
  if (!Out)
    return nullptr;

  // Make sure we are allowed to hoist instructions into the predecessor.
  if (!detail::isLegalToHoistInto(Out))
    return nullptr;

  // Make sure there is only one exit out of the preheader.
  if (!llvm::hasSingleElement(llvm::children<BlockT *>(Out)))
    return nullptr; // Multiple exits from the block, must not be a preheader.

  // The predecessor has exactly one successor, so it is a preheader.
  return Out;
}

/// getLoopPredecessor - If the given loop's header has exactly one unique
/// predecessor outside the loop, return it. Otherwise return null.
/// This is less strict that the loop "preheader" concept, which requires
/// the predecessor to have exactly one successor.
///
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getLoopPredecessor() const {
  assert(!isInvalid() && "Loop not in a valid state!");
  // Keep track of nodes outside the loop branching to the header...
  BlockT *Out = nullptr;

  // Loop over the predecessors of the header node...
  BlockT *Header = getHeader();
  for (const auto Pred : inverse_children<BlockT *>(Header)) {
    if (!contains(Pred)) { // If the block is not in the loop...
      if (Out && Out != Pred)
        return nullptr; // Multiple predecessors outside the loop
      Out = Pred;
    }
  }

  return Out;
}

/// getLoopLatch - If there is a single latch block for this loop, return it.
/// A latch block is a block that contains a branch back to the header.
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getLoopLatch() const {
  assert(!isInvalid() && "Loop not in a valid state!");
  BlockT *Header = getHeader();
  BlockT *Latch = nullptr;
  for (const auto Pred : inverse_children<BlockT *>(Header)) {
    if (contains(Pred)) {
      if (Latch)
        return nullptr;
      Latch = Pred;
    }
  }

  return Latch;
}

//===----------------------------------------------------------------------===//
// APIs for updating loop information after changing the CFG
//

/// addBasicBlockToLoop - This method is used by other analyses to update loop
/// information.  NewBB is set to be a new member of the current loop.
/// Because of this, it is added as a member of all parent loops, and is added
/// to the specified LoopInfo object as being in the current basic block.  It
/// is not valid to replace the loop header with this method.
///
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::addBasicBlockToLoop(
    BlockT *NewBB, LoopInfoBase<BlockT, LoopT> &LIB) {
  assert(!isInvalid() && "Loop not in a valid state!");
#ifndef NDEBUG
  if (!Blocks.empty()) {
    auto SameHeader = LIB[getHeader()];
    assert(contains(SameHeader) && getHeader() == SameHeader->getHeader() &&
           "Incorrect LI specified for this loop!");
  }
#endif
  assert(NewBB && "Cannot add a null basic block to the loop!");
  assert(!LIB[NewBB] && "BasicBlock already in the loop!");

  LoopT *L = static_cast<LoopT *>(this);

  // Add the loop mapping to the LoopInfo object...
  LIB.BBMap[NewBB] = L;

  // Add the basic block to this loop and all parent loops...
  while (L) {
    L->addBlockEntry(NewBB);
    L = L->getParentLoop();
  }
}

/// replaceChildLoopWith - This is used when splitting loops up.  It replaces
/// the OldChild entry in our children list with NewChild, and updates the
/// parent pointer of OldChild to be null and the NewChild to be this loop.
/// This updates the loop depth of the new child.
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::replaceChildLoopWith(LoopT *OldChild,
                                                   LoopT *NewChild) {
  assert(!isInvalid() && "Loop not in a valid state!");
  assert(OldChild->ParentLoop == this && "This loop is already broken!");
  assert(!NewChild->ParentLoop && "NewChild already has a parent!");
  typename std::vector<LoopT *>::iterator I = find(SubLoops, OldChild);
  assert(I != SubLoops.end() && "OldChild not in loop!");
  *I = NewChild;
  OldChild->ParentLoop = nullptr;
  NewChild->ParentLoop = static_cast<LoopT *>(this);
}

/// verifyLoop - Verify loop structure
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::verifyLoop() const {
  assert(!isInvalid() && "Loop not in a valid state!");
#ifndef NDEBUG
  assert(!Blocks.empty() && "Loop header is missing");

  // Setup for using a depth-first iterator to visit every block in the loop.
  SmallVector<BlockT *, 8> ExitBBs;
  getExitBlocks(ExitBBs);
  df_iterator_default_set<BlockT *> VisitSet;
  VisitSet.insert(ExitBBs.begin(), ExitBBs.end());

  // Keep track of the BBs visited.
  SmallPtrSet<BlockT *, 8> VisitedBBs;

  // Check the individual blocks.
  for (BlockT *BB : depth_first_ext(getHeader(), VisitSet)) {
    assert(llvm::any_of(children<BlockT *>(BB),
                        [&](BlockT *B) { return contains(B); }) &&
           "Loop block has no in-loop successors!");

    assert(llvm::any_of(inverse_children<BlockT *>(BB),
                        [&](BlockT *B) { return contains(B); }) &&
           "Loop block has no in-loop predecessors!");

    SmallVector<BlockT *, 2> OutsideLoopPreds;
    for (BlockT *B : inverse_children<BlockT *>(BB))
      if (!contains(B))
        OutsideLoopPreds.push_back(B);

    if (BB == getHeader()) {
      assert(!OutsideLoopPreds.empty() && "Loop is unreachable!");
    } else if (!OutsideLoopPreds.empty()) {
      // A non-header loop shouldn't be reachable from outside the loop,
      // though it is permitted if the predecessor is not itself actually
      // reachable.
      BlockT *EntryBB = &BB->getParent()->front();
      for (BlockT *CB : depth_first(EntryBB))
        for (unsigned i = 0, e = OutsideLoopPreds.size(); i != e; ++i)
          assert(CB != OutsideLoopPreds[i] &&
                 "Loop has multiple entry points!");
    }
    assert(BB != &getHeader()->getParent()->front() &&
           "Loop contains function entry block!");

    VisitedBBs.insert(BB);
  }

  if (VisitedBBs.size() != getNumBlocks()) {
    dbgs() << "The following blocks are unreachable in the loop: ";
    for (auto *BB : Blocks) {
      if (!VisitedBBs.count(BB)) {
        dbgs() << *BB << "\n";
      }
    }
    assert(false && "Unreachable block in loop");
  }

  // Check the subloops.
  for (iterator I = begin(), E = end(); I != E; ++I)
    // Each block in each subloop should be contained within this loop.
    for (block_iterator BI = (*I)->block_begin(), BE = (*I)->block_end();
         BI != BE; ++BI) {
      assert(contains(*BI) &&
             "Loop does not contain all the blocks of a subloop!");
    }

  // Check the parent loop pointer.
  if (ParentLoop) {
    assert(is_contained(ParentLoop->getSubLoops(), this) &&
           "Loop is not a subloop of its parent!");
  }
#endif
}

/// verifyLoop - Verify loop structure of this loop and all nested loops.
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::verifyLoopNest(
    DenseSet<const LoopT *> *Loops) const {
  assert(!isInvalid() && "Loop not in a valid state!");
  Loops->insert(static_cast<const LoopT *>(this));
  // Verify this loop.
  verifyLoop();
  // Verify the subloops.
  for (iterator I = begin(), E = end(); I != E; ++I)
    (*I)->verifyLoopNest(Loops);
}

template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::print(raw_ostream &OS, bool Verbose,
                                    bool PrintNested, unsigned Depth) const {
  OS.indent(Depth * 2);
  if (static_cast<const LoopT *>(this)->isAnnotatedParallel())
    OS << "Parallel ";
  OS << "Loop at depth " << getLoopDepth() << " containing: ";

  BlockT *H = getHeader();
  for (unsigned i = 0; i < getBlocks().size(); ++i) {
    BlockT *BB = getBlocks()[i];
    if (!Verbose) {
      if (i)
        OS << ",";
      BB->printAsOperand(OS, false);
    } else
      OS << "\n";

    if (BB == H)
      OS << "<header>";
    if (isLoopLatch(BB))
      OS << "<latch>";
    if (isLoopExiting(BB))
      OS << "<exiting>";
    if (Verbose)
      BB->print(OS);
  }

  if (PrintNested) {
    OS << "\n";

    for (iterator I = begin(), E = end(); I != E; ++I)
      (*I)->print(OS, /*Verbose*/ false, PrintNested, Depth + 2);
  }
}

//===----------------------------------------------------------------------===//
/// Stable LoopInfo Analysis - Build a loop tree using stable iterators so the
/// result does / not depend on use list (block predecessor) order.
///

/// Discover a subloop with the specified backedges such that: All blocks within
/// this loop are mapped to this loop or a subloop. And all subloops within this
/// loop have their parent loop set to this loop or a subloop.
template <class BlockT, class LoopT>
static void discoverAndMapSubloop(LoopT *L, ArrayRef<BlockT *> Backedges,
                                  LoopInfoBase<BlockT, LoopT> *LI,
                                  const DomTreeBase<BlockT> &DomTree) {
  typedef GraphTraits<Inverse<BlockT *>> InvBlockTraits;

  unsigned NumBlocks = 0;
  unsigned NumSubloops = 0;

  // Perform a backward CFG traversal using a worklist.
  std::vector<BlockT *> ReverseCFGWorklist(Backedges.begin(), Backedges.end());
  while (!ReverseCFGWorklist.empty()) {
    BlockT *PredBB = ReverseCFGWorklist.back();
    ReverseCFGWorklist.pop_back();

    LoopT *Subloop = LI->getLoopFor(PredBB);
    if (!Subloop) {
      if (!DomTree.isReachableFromEntry(PredBB))
        continue;

      // This is an undiscovered block. Map it to the current loop.
      LI->changeLoopFor(PredBB, L);
      ++NumBlocks;
      if (PredBB == L->getHeader())
        continue;
      // Push all block predecessors on the worklist.
      ReverseCFGWorklist.insert(ReverseCFGWorklist.end(),
                                InvBlockTraits::child_begin(PredBB),
                                InvBlockTraits::child_end(PredBB));
    } else {
      // This is a discovered block. Find its outermost discovered loop.
      Subloop = Subloop->getOutermostLoop();

      // If it is already discovered to be a subloop of this loop, continue.
      if (Subloop == L)
        continue;

      // Discover a subloop of this loop.
      Subloop->setParentLoop(L);
      ++NumSubloops;
      NumBlocks += Subloop->getBlocksVector().capacity();
      PredBB = Subloop->getHeader();
      // Continue traversal along predecessors that are not loop-back edges from
      // within this subloop tree itself. Note that a predecessor may directly
      // reach another subloop that is not yet discovered to be a subloop of
      // this loop, which we must traverse.
      for (const auto Pred : inverse_children<BlockT *>(PredBB)) {
        if (LI->getLoopFor(Pred) != Subloop)
          ReverseCFGWorklist.push_back(Pred);
      }
    }
  }
  L->getSubLoopsVector().reserve(NumSubloops);
  L->reserveBlocks(NumBlocks);
}

/// Populate all loop data in a stable order during a single forward DFS.
template <class BlockT, class LoopT> class PopulateLoopsDFS {
  typedef GraphTraits<BlockT *> BlockTraits;
  typedef typename BlockTraits::ChildIteratorType SuccIterTy;

  LoopInfoBase<BlockT, LoopT> *LI;

public:
  PopulateLoopsDFS(LoopInfoBase<BlockT, LoopT> *li) : LI(li) {}

  void traverse(BlockT *EntryBlock);

protected:
  void insertIntoLoop(BlockT *Block);
};

/// Top-level driver for the forward DFS within the loop.
template <class BlockT, class LoopT>
void PopulateLoopsDFS<BlockT, LoopT>::traverse(BlockT *EntryBlock) {
  for (BlockT *BB : post_order(EntryBlock))
    insertIntoLoop(BB);
}

/// Add a single Block to its ancestor loops in PostOrder. If the block is a
/// subloop header, add the subloop to its parent in PostOrder, then reverse the
/// Block and Subloop vectors of the now complete subloop to achieve RPO.
template <class BlockT, class LoopT>
void PopulateLoopsDFS<BlockT, LoopT>::insertIntoLoop(BlockT *Block) {
  LoopT *Subloop = LI->getLoopFor(Block);
  if (Subloop && Block == Subloop->getHeader()) {
    // We reach this point once per subloop after processing all the blocks in
    // the subloop.
    if (!Subloop->isOutermost())
      Subloop->getParentLoop()->getSubLoopsVector().push_back(Subloop);
    else
      LI->addTopLevelLoop(Subloop);

    // For convenience, Blocks and Subloops are inserted in postorder. Reverse
    // the lists, except for the loop header, which is always at the beginning.
    Subloop->reverseBlock(1);
    std::reverse(Subloop->getSubLoopsVector().begin(),
                 Subloop->getSubLoopsVector().end());

    Subloop = Subloop->getParentLoop();
  }
  for (; Subloop; Subloop = Subloop->getParentLoop())
    Subloop->addBlockEntry(Block);
}

/// Analyze LoopInfo discovers loops during a postorder DominatorTree traversal
/// interleaved with backward CFG traversals within each subloop
/// (discoverAndMapSubloop). The backward traversal skips inner subloops, so
/// this part of the algorithm is linear in the number of CFG edges. Subloop and
/// Block vectors are then populated during a single forward CFG traversal
/// (PopulateLoopDFS).
///
/// During the two CFG traversals each block is seen three times:
/// 1) Discovered and mapped by a reverse CFG traversal.
/// 2) Visited during a forward DFS CFG traversal.
/// 3) Reverse-inserted in the loop in postorder following forward DFS.
///
/// The Block vectors are inclusive, so step 3 requires loop-depth number of
/// insertions per block.
template <class BlockT, class LoopT>
void LoopInfoBase<BlockT, LoopT>::analyze(const DomTreeBase<BlockT> &DomTree) {
  // Postorder traversal of the dominator tree.
  const DomTreeNodeBase<BlockT> *DomRoot = DomTree.getRootNode();
  for (auto DomNode : post_order(DomRoot)) {

    BlockT *Header = DomNode->getBlock();
    SmallVector<BlockT *, 4> Backedges;

    // Check each predecessor of the potential loop header.
    for (const auto Backedge : inverse_children<BlockT *>(Header)) {
      // If Header dominates predBB, this is a new loop. Collect the backedges.
      const DomTreeNodeBase<BlockT> *BackedgeNode = DomTree.getNode(Backedge);
      if (BackedgeNode && DomTree.dominates(DomNode, BackedgeNode))
        Backedges.push_back(Backedge);
    }
    // Perform a backward CFG traversal to discover and map blocks in this loop.
    if (!Backedges.empty()) {
      LoopT *L = AllocateLoop(Header);
      discoverAndMapSubloop(L, ArrayRef<BlockT *>(Backedges), this, DomTree);
    }
  }
  // Perform a single forward CFG traversal to populate block and subloop
  // vectors for all loops.
  PopulateLoopsDFS<BlockT, LoopT> DFS(this);
  DFS.traverse(DomRoot->getBlock());
}

template <class BlockT, class LoopT>
SmallVector<LoopT *, 4>
LoopInfoBase<BlockT, LoopT>::getLoopsInPreorder() const {
  SmallVector<LoopT *, 4> PreOrderLoops, PreOrderWorklist;
  // The outer-most loop actually goes into the result in the same relative
  // order as we walk it. But LoopInfo stores the top level loops in reverse
  // program order so for here we reverse it to get forward program order.
  // FIXME: If we change the order of LoopInfo we will want to remove the
  // reverse here.
  for (LoopT *RootL : reverse(*this)) {
    auto PreOrderLoopsInRootL = RootL->getLoopsInPreorder();
    PreOrderLoops.append(PreOrderLoopsInRootL.begin(),
                         PreOrderLoopsInRootL.end());
  }

  return PreOrderLoops;
}

template <class BlockT, class LoopT>
SmallVector<LoopT *, 4>
LoopInfoBase<BlockT, LoopT>::getLoopsInReverseSiblingPreorder() const {
  SmallVector<LoopT *, 4> PreOrderLoops, PreOrderWorklist;
  // The outer-most loop actually goes into the result in the same relative
  // order as we walk it. LoopInfo stores the top level loops in reverse
  // program order so we walk in order here.
  // FIXME: If we change the order of LoopInfo we will want to add a reverse
  // here.
  for (LoopT *RootL : *this) {
    assert(PreOrderWorklist.empty() &&
           "Must start with an empty preorder walk worklist.");
    PreOrderWorklist.push_back(RootL);
    do {
      LoopT *L = PreOrderWorklist.pop_back_val();
      // Sub-loops are stored in forward program order, but will process the
      // worklist backwards so we can just append them in order.
      PreOrderWorklist.append(L->begin(), L->end());
      PreOrderLoops.push_back(L);
    } while (!PreOrderWorklist.empty());
  }

  return PreOrderLoops;
}

// Debugging
template <class BlockT, class LoopT>
void LoopInfoBase<BlockT, LoopT>::print(raw_ostream &OS) const {
  for (unsigned i = 0; i < TopLevelLoops.size(); ++i)
    TopLevelLoops[i]->print(OS);
#if 0
  for (DenseMap<BasicBlock*, LoopT*>::const_iterator I = BBMap.begin(),
         E = BBMap.end(); I != E; ++I)
    OS << "BB '" << I->first->getName() << "' level = "
       << I->second->getLoopDepth() << "\n";
#endif
}

template <typename T>
bool compareVectors(std::vector<T> &BB1, std::vector<T> &BB2) {
  llvm::sort(BB1);
  llvm::sort(BB2);
  return BB1 == BB2;
}

template <class BlockT, class LoopT>
void addInnerLoopsToHeadersMap(DenseMap<BlockT *, const LoopT *> &LoopHeaders,
                               const LoopInfoBase<BlockT, LoopT> &LI,
                               const LoopT &L) {
  LoopHeaders[L.getHeader()] = &L;
  for (LoopT *SL : L)
    addInnerLoopsToHeadersMap(LoopHeaders, LI, *SL);
}

#ifndef NDEBUG
template <class BlockT, class LoopT>
static void compareLoops(const LoopT *L, const LoopT *OtherL,
                         DenseMap<BlockT *, const LoopT *> &OtherLoopHeaders) {
  BlockT *H = L->getHeader();
  BlockT *OtherH = OtherL->getHeader();
  assert(H == OtherH &&
         "Mismatched headers even though found in the same map entry!");

  assert(L->getLoopDepth() == OtherL->getLoopDepth() &&
         "Mismatched loop depth!");
  const LoopT *ParentL = L, *OtherParentL = OtherL;
  do {
    assert(ParentL->getHeader() == OtherParentL->getHeader() &&
           "Mismatched parent loop headers!");
    ParentL = ParentL->getParentLoop();
    OtherParentL = OtherParentL->getParentLoop();
  } while (ParentL);

  for (const LoopT *SubL : *L) {
    BlockT *SubH = SubL->getHeader();
    const LoopT *OtherSubL = OtherLoopHeaders.lookup(SubH);
    assert(OtherSubL && "Inner loop is missing in computed loop info!");
    OtherLoopHeaders.erase(SubH);
    compareLoops(SubL, OtherSubL, OtherLoopHeaders);
  }

  std::vector<BlockT *> BBs = L->getBlocks();
  std::vector<BlockT *> OtherBBs = OtherL->getBlocks();
  assert(compareVectors(BBs, OtherBBs) &&
         "Mismatched basic blocks in the loops!");

  const SmallPtrSetImpl<const BlockT *> &BlocksSet = L->getBlocksSet();
  const SmallPtrSetImpl<const BlockT *> &OtherBlocksSet =
      OtherL->getBlocksSet();
  assert(BlocksSet.size() == OtherBlocksSet.size() &&
         llvm::set_is_subset(BlocksSet, OtherBlocksSet) &&
         "Mismatched basic blocks in BlocksSets!");
}
#endif

template <class BlockT, class LoopT>
void LoopInfoBase<BlockT, LoopT>::verify(
    const DomTreeBase<BlockT> &DomTree) const {
  DenseSet<const LoopT *> Loops;
  for (iterator I = begin(), E = end(); I != E; ++I) {
    assert((*I)->isOutermost() && "Top-level loop has a parent!");
    (*I)->verifyLoopNest(&Loops);
  }

// Verify that blocks are mapped to valid loops.
#ifndef NDEBUG
  for (auto &Entry : BBMap) {
    const BlockT *BB = Entry.first;
    LoopT *L = Entry.second;
    assert(Loops.count(L) && "orphaned loop");
    assert(L->contains(BB) && "orphaned block");
    for (LoopT *ChildLoop : *L)
      assert(!ChildLoop->contains(BB) &&
             "BBMap should point to the innermost loop containing BB");
  }

  // Recompute LoopInfo to verify loops structure.
  LoopInfoBase<BlockT, LoopT> OtherLI;
  OtherLI.analyze(DomTree);

  // Build a map we can use to move from our LI to the computed one. This
  // allows us to ignore the particular order in any layer of the loop forest
  // while still comparing the structure.
  DenseMap<BlockT *, const LoopT *> OtherLoopHeaders;
  for (LoopT *L : OtherLI)
    addInnerLoopsToHeadersMap(OtherLoopHeaders, OtherLI, *L);

  // Walk the top level loops and ensure there is a corresponding top-level
  // loop in the computed version and then recursively compare those loop
  // nests.
  for (LoopT *L : *this) {
    BlockT *Header = L->getHeader();
    const LoopT *OtherL = OtherLoopHeaders.lookup(Header);
    assert(OtherL && "Top level loop is missing in computed loop info!");
    // Now that we've matched this loop, erase its header from the map.
    OtherLoopHeaders.erase(Header);
    // And recursively compare these loops.
    compareLoops(L, OtherL, OtherLoopHeaders);
  }

  // Any remaining entries in the map are loops which were found when computing
  // a fresh LoopInfo but not present in the current one.
  if (!OtherLoopHeaders.empty()) {
    for (const auto &HeaderAndLoop : OtherLoopHeaders)
      dbgs() << "Found new loop: " << *HeaderAndLoop.second << "\n";
    llvm_unreachable("Found new loops when recomputing LoopInfo!");
  }
#endif
}

} // namespace llvm

#endif // LLVM_SUPPORT_GENERICLOOPINFOIMPL_H
