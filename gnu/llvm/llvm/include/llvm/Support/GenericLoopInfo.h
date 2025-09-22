//===- GenericLoopInfo - Generic Loop Info for graphs -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LoopInfoBase class that is used to identify natural
// loops and determine the loop depth of various nodes in a generic graph of
// blocks.  A natural loop has exactly one entry-point, which is called the
// header. Note that natural loops may actually be several loops that share the
// same header node.
//
// This analysis calculates the nesting structure of loops in a function.  For
// each natural loop identified, this analysis identifies natural loops
// contained entirely within the loop and the basic blocks that make up the
// loop.
//
// It can calculate on the fly various bits of information, for example:
//
//  * whether there is a preheader for the loop
//  * the number of back edges to the header
//  * whether or not a particular block branches out of the loop
//  * the successor blocks of the loop
//  * the loop depth
//  * etc...
//
// Note that this analysis specifically identifies *Loops* not cycles or SCCs
// in the graph.  There can be strongly connected components in the graph which
// this analysis will not recognize and that will not be represented by a Loop
// instance.  In particular, a Loop might be inside such a non-loop SCC, or a
// non-loop SCC might contain a sub-SCC which is a Loop.
//
// For an overview of terminology used in this API (and thus all of our loop
// analyses or transforms), see docs/LoopTerminology.rst.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GENERICLOOPINFO_H
#define LLVM_SUPPORT_GENERICLOOPINFO_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/GenericDomTree.h"

namespace llvm {

template <class N, class M> class LoopInfoBase;
template <class N, class M> class LoopBase;

//===----------------------------------------------------------------------===//
/// Instances of this class are used to represent loops that are detected in the
/// flow graph.
///
template <class BlockT, class LoopT> class LoopBase {
  LoopT *ParentLoop;
  // Loops contained entirely within this one.
  std::vector<LoopT *> SubLoops;

  // The list of blocks in this loop. First entry is the header node.
  std::vector<BlockT *> Blocks;

  SmallPtrSet<const BlockT *, 8> DenseBlockSet;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// Indicator that this loop is no longer a valid loop.
  bool IsInvalid = false;
#endif

  LoopBase(const LoopBase<BlockT, LoopT> &) = delete;
  const LoopBase<BlockT, LoopT> &
  operator=(const LoopBase<BlockT, LoopT> &) = delete;

public:
  /// Return the nesting level of this loop.  An outer-most loop has depth 1,
  /// for consistency with loop depth values used for basic blocks, where depth
  /// 0 is used for blocks not inside any loops.
  unsigned getLoopDepth() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    unsigned D = 1;
    for (const LoopT *CurLoop = ParentLoop; CurLoop;
         CurLoop = CurLoop->ParentLoop)
      ++D;
    return D;
  }
  BlockT *getHeader() const { return getBlocks().front(); }
  /// Return the parent loop if it exists or nullptr for top
  /// level loops.

  /// A loop is either top-level in a function (that is, it is not
  /// contained in any other loop) or it is entirely enclosed in
  /// some other loop.
  /// If a loop is top-level, it has no parent, otherwise its
  /// parent is the innermost loop in which it is enclosed.
  LoopT *getParentLoop() const { return ParentLoop; }

  /// Get the outermost loop in which this loop is contained.
  /// This may be the loop itself, if it already is the outermost loop.
  const LoopT *getOutermostLoop() const {
    const LoopT *L = static_cast<const LoopT *>(this);
    while (L->ParentLoop)
      L = L->ParentLoop;
    return L;
  }

  LoopT *getOutermostLoop() {
    LoopT *L = static_cast<LoopT *>(this);
    while (L->ParentLoop)
      L = L->ParentLoop;
    return L;
  }

  /// This is a raw interface for bypassing addChildLoop.
  void setParentLoop(LoopT *L) {
    assert(!isInvalid() && "Loop not in a valid state!");
    ParentLoop = L;
  }

  /// Return true if the specified loop is contained within in this loop.
  bool contains(const LoopT *L) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    if (L == this)
      return true;
    if (!L)
      return false;
    return contains(L->getParentLoop());
  }

  /// Return true if the specified basic block is in this loop.
  bool contains(const BlockT *BB) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return DenseBlockSet.count(BB);
  }

  /// Return true if the specified instruction is in this loop.
  template <class InstT> bool contains(const InstT *Inst) const {
    return contains(Inst->getParent());
  }

  /// Return the loops contained entirely within this loop.
  const std::vector<LoopT *> &getSubLoops() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return SubLoops;
  }
  std::vector<LoopT *> &getSubLoopsVector() {
    assert(!isInvalid() && "Loop not in a valid state!");
    return SubLoops;
  }
  typedef typename std::vector<LoopT *>::const_iterator iterator;
  typedef
      typename std::vector<LoopT *>::const_reverse_iterator reverse_iterator;
  iterator begin() const { return getSubLoops().begin(); }
  iterator end() const { return getSubLoops().end(); }
  reverse_iterator rbegin() const { return getSubLoops().rbegin(); }
  reverse_iterator rend() const { return getSubLoops().rend(); }

  // LoopInfo does not detect irreducible control flow, just natural
  // loops. That is, it is possible that there is cyclic control
  // flow within the "innermost loop" or around the "outermost
  // loop".

  /// Return true if the loop does not contain any (natural) loops.
  bool isInnermost() const { return getSubLoops().empty(); }
  /// Return true if the loop does not have a parent (natural) loop
  // (i.e. it is outermost, which is the same as top-level).
  bool isOutermost() const { return getParentLoop() == nullptr; }

  /// Get a list of the basic blocks which make up this loop.
  ArrayRef<BlockT *> getBlocks() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return Blocks;
  }
  typedef typename ArrayRef<BlockT *>::const_iterator block_iterator;
  block_iterator block_begin() const { return getBlocks().begin(); }
  block_iterator block_end() const { return getBlocks().end(); }
  inline iterator_range<block_iterator> blocks() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return make_range(block_begin(), block_end());
  }

  /// Get the number of blocks in this loop in constant time.
  /// Invalidate the loop, indicating that it is no longer a loop.
  unsigned getNumBlocks() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return Blocks.size();
  }

  /// Return a direct, mutable handle to the blocks vector so that we can
  /// mutate it efficiently with techniques like `std::remove`.
  std::vector<BlockT *> &getBlocksVector() {
    assert(!isInvalid() && "Loop not in a valid state!");
    return Blocks;
  }
  /// Return a direct, mutable handle to the blocks set so that we can
  /// mutate it efficiently.
  SmallPtrSetImpl<const BlockT *> &getBlocksSet() {
    assert(!isInvalid() && "Loop not in a valid state!");
    return DenseBlockSet;
  }

  /// Return a direct, immutable handle to the blocks set.
  const SmallPtrSetImpl<const BlockT *> &getBlocksSet() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return DenseBlockSet;
  }

  /// Return true if this loop is no longer valid.  The only valid use of this
  /// helper is "assert(L.isInvalid())" or equivalent, since IsInvalid is set to
  /// true by the destructor.  In other words, if this accessor returns true,
  /// the caller has already triggered UB by calling this accessor; and so it
  /// can only be called in a context where a return value of true indicates a
  /// programmer error.
  bool isInvalid() const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    return IsInvalid;
#else
    return false;
#endif
  }

  /// True if terminator in the block can branch to another block that is
  /// outside of the current loop. \p BB must be inside the loop.
  bool isLoopExiting(const BlockT *BB) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(contains(BB) && "Exiting block must be part of the loop");
    for (const auto *Succ : children<const BlockT *>(BB)) {
      if (!contains(Succ))
        return true;
    }
    return false;
  }

  /// Returns true if \p BB is a loop-latch.
  /// A latch block is a block that contains a branch back to the header.
  /// This function is useful when there are multiple latches in a loop
  /// because \fn getLoopLatch will return nullptr in that case.
  bool isLoopLatch(const BlockT *BB) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(contains(BB) && "block does not belong to the loop");
    return llvm::is_contained(inverse_children<BlockT *>(getHeader()), BB);
  }

  /// Calculate the number of back edges to the loop header.
  unsigned getNumBackEdges() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return llvm::count_if(inverse_children<BlockT *>(getHeader()),
                          [&](BlockT *Pred) { return contains(Pred); });
  }

  //===--------------------------------------------------------------------===//
  // APIs for simple analysis of the loop.
  //
  // Note that all of these methods can fail on general loops (ie, there may not
  // be a preheader, etc).  For best success, the loop simplification and
  // induction variable canonicalization pass should be used to normalize loops
  // for easy analysis.  These methods assume canonical loops.

  /// Return all blocks inside the loop that have successors outside of the
  /// loop. These are the blocks _inside of the current loop_ which branch out.
  /// The returned list is always unique.
  void getExitingBlocks(SmallVectorImpl<BlockT *> &ExitingBlocks) const;

  /// If getExitingBlocks would return exactly one block, return that block.
  /// Otherwise return null.
  BlockT *getExitingBlock() const;

  /// Return all of the successor blocks of this loop. These are the blocks
  /// _outside of the current loop_ which are branched to.
  void getExitBlocks(SmallVectorImpl<BlockT *> &ExitBlocks) const;

  /// If getExitBlocks would return exactly one block, return that block.
  /// Otherwise return null.
  BlockT *getExitBlock() const;

  /// Return true if no exit block for the loop has a predecessor that is
  /// outside the loop.
  bool hasDedicatedExits() const;

  /// Return all unique successor blocks of this loop.
  /// These are the blocks _outside of the current loop_ which are branched to.
  void getUniqueExitBlocks(SmallVectorImpl<BlockT *> &ExitBlocks) const;

  /// Return all unique successor blocks of this loop except successors from
  /// Latch block are not considered. If the exit comes from Latch has also
  /// non Latch predecessor in a loop it will be added to ExitBlocks.
  /// These are the blocks _outside of the current loop_ which are branched to.
  void getUniqueNonLatchExitBlocks(SmallVectorImpl<BlockT *> &ExitBlocks) const;

  /// If getUniqueExitBlocks would return exactly one block, return that block.
  /// Otherwise return null.
  BlockT *getUniqueExitBlock() const;

  /// Return true if this loop does not have any exit blocks.
  bool hasNoExitBlocks() const;

  /// Edge type.
  typedef std::pair<BlockT *, BlockT *> Edge;

  /// Return all pairs of (_inside_block_,_outside_block_).
  void getExitEdges(SmallVectorImpl<Edge> &ExitEdges) const;

  /// If there is a preheader for this loop, return it. A loop has a preheader
  /// if there is only one edge to the header of the loop from outside of the
  /// loop. If this is the case, the block branching to the header of the loop
  /// is the preheader node.
  ///
  /// This method returns null if there is no preheader for the loop.
  BlockT *getLoopPreheader() const;

  /// If the given loop's header has exactly one unique predecessor outside the
  /// loop, return it. Otherwise return null.
  ///  This is less strict that the loop "preheader" concept, which requires
  /// the predecessor to have exactly one successor.
  BlockT *getLoopPredecessor() const;

  /// If there is a single latch block for this loop, return it.
  /// A latch block is a block that contains a branch back to the header.
  BlockT *getLoopLatch() const;

  /// Return all loop latch blocks of this loop. A latch block is a block that
  /// contains a branch back to the header.
  void getLoopLatches(SmallVectorImpl<BlockT *> &LoopLatches) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    BlockT *H = getHeader();
    for (const auto Pred : inverse_children<BlockT *>(H))
      if (contains(Pred))
        LoopLatches.push_back(Pred);
  }

  /// Return all inner loops in the loop nest rooted by the loop in preorder,
  /// with siblings in forward program order.
  template <class Type>
  static void getInnerLoopsInPreorder(const LoopT &L,
                                      SmallVectorImpl<Type> &PreOrderLoops) {
    SmallVector<LoopT *, 4> PreOrderWorklist;
    PreOrderWorklist.append(L.rbegin(), L.rend());

    while (!PreOrderWorklist.empty()) {
      LoopT *L = PreOrderWorklist.pop_back_val();
      // Sub-loops are stored in forward program order, but will process the
      // worklist backwards so append them in reverse order.
      PreOrderWorklist.append(L->rbegin(), L->rend());
      PreOrderLoops.push_back(L);
    }
  }

  /// Return all loops in the loop nest rooted by the loop in preorder, with
  /// siblings in forward program order.
  SmallVector<const LoopT *, 4> getLoopsInPreorder() const {
    SmallVector<const LoopT *, 4> PreOrderLoops;
    const LoopT *CurLoop = static_cast<const LoopT *>(this);
    PreOrderLoops.push_back(CurLoop);
    getInnerLoopsInPreorder(*CurLoop, PreOrderLoops);
    return PreOrderLoops;
  }
  SmallVector<LoopT *, 4> getLoopsInPreorder() {
    SmallVector<LoopT *, 4> PreOrderLoops;
    LoopT *CurLoop = static_cast<LoopT *>(this);
    PreOrderLoops.push_back(CurLoop);
    getInnerLoopsInPreorder(*CurLoop, PreOrderLoops);
    return PreOrderLoops;
  }

  //===--------------------------------------------------------------------===//
  // APIs for updating loop information after changing the CFG
  //

  /// This method is used by other analyses to update loop information.
  /// NewBB is set to be a new member of the current loop.
  /// Because of this, it is added as a member of all parent loops, and is added
  /// to the specified LoopInfo object as being in the current basic block.  It
  /// is not valid to replace the loop header with this method.
  void addBasicBlockToLoop(BlockT *NewBB, LoopInfoBase<BlockT, LoopT> &LI);

  /// This is used when splitting loops up. It replaces the OldChild entry in
  /// our children list with NewChild, and updates the parent pointer of
  /// OldChild to be null and the NewChild to be this loop.
  /// This updates the loop depth of the new child.
  void replaceChildLoopWith(LoopT *OldChild, LoopT *NewChild);

  /// Add the specified loop to be a child of this loop.
  /// This updates the loop depth of the new child.
  void addChildLoop(LoopT *NewChild) {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(!NewChild->ParentLoop && "NewChild already has a parent!");
    NewChild->ParentLoop = static_cast<LoopT *>(this);
    SubLoops.push_back(NewChild);
  }

  /// This removes the specified child from being a subloop of this loop. The
  /// loop is not deleted, as it will presumably be inserted into another loop.
  LoopT *removeChildLoop(iterator I) {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(I != SubLoops.end() && "Cannot remove end iterator!");
    LoopT *Child = *I;
    assert(Child->ParentLoop == this && "Child is not a child of this loop!");
    SubLoops.erase(SubLoops.begin() + (I - begin()));
    Child->ParentLoop = nullptr;
    return Child;
  }

  /// This removes the specified child from being a subloop of this loop. The
  /// loop is not deleted, as it will presumably be inserted into another loop.
  LoopT *removeChildLoop(LoopT *Child) {
    return removeChildLoop(llvm::find(*this, Child));
  }

  /// This adds a basic block directly to the basic block list.
  /// This should only be used by transformations that create new loops.  Other
  /// transformations should use addBasicBlockToLoop.
  void addBlockEntry(BlockT *BB) {
    assert(!isInvalid() && "Loop not in a valid state!");
    Blocks.push_back(BB);
    DenseBlockSet.insert(BB);
  }

  /// interface to reverse Blocks[from, end of loop] in this loop
  void reverseBlock(unsigned from) {
    assert(!isInvalid() && "Loop not in a valid state!");
    std::reverse(Blocks.begin() + from, Blocks.end());
  }

  /// interface to do reserve() for Blocks
  void reserveBlocks(unsigned size) {
    assert(!isInvalid() && "Loop not in a valid state!");
    Blocks.reserve(size);
  }

  /// This method is used to move BB (which must be part of this loop) to be the
  /// loop header of the loop (the block that dominates all others).
  void moveToHeader(BlockT *BB) {
    assert(!isInvalid() && "Loop not in a valid state!");
    if (Blocks[0] == BB)
      return;
    for (unsigned i = 0;; ++i) {
      assert(i != Blocks.size() && "Loop does not contain BB!");
      if (Blocks[i] == BB) {
        Blocks[i] = Blocks[0];
        Blocks[0] = BB;
        return;
      }
    }
  }

  /// This removes the specified basic block from the current loop, updating the
  /// Blocks as appropriate. This does not update the mapping in the LoopInfo
  /// class.
  void removeBlockFromLoop(BlockT *BB) {
    assert(!isInvalid() && "Loop not in a valid state!");
    auto I = find(Blocks, BB);
    assert(I != Blocks.end() && "N is not in this list!");
    Blocks.erase(I);

    DenseBlockSet.erase(BB);
  }

  /// Verify loop structure
  void verifyLoop() const;

  /// Verify loop structure of this loop and all nested loops.
  void verifyLoopNest(DenseSet<const LoopT *> *Loops) const;

  /// Returns true if the loop is annotated parallel.
  ///
  /// Derived classes can override this method using static template
  /// polymorphism.
  bool isAnnotatedParallel() const { return false; }

  /// Print loop with all the BBs inside it.
  void print(raw_ostream &OS, bool Verbose = false, bool PrintNested = true,
             unsigned Depth = 0) const;

protected:
  friend class LoopInfoBase<BlockT, LoopT>;

  /// This creates an empty loop.
  LoopBase() : ParentLoop(nullptr) {}

  explicit LoopBase(BlockT *BB) : ParentLoop(nullptr) {
    Blocks.push_back(BB);
    DenseBlockSet.insert(BB);
  }

  // Since loop passes like SCEV are allowed to key analysis results off of
  // `Loop` pointers, we cannot re-use pointers within a loop pass manager.
  // This means loop passes should not be `delete` ing `Loop` objects directly
  // (and risk a later `Loop` allocation re-using the address of a previous one)
  // but should be using LoopInfo::markAsRemoved, which keeps around the `Loop`
  // pointer till the end of the lifetime of the `LoopInfo` object.
  //
  // To make it easier to follow this rule, we mark the destructor as
  // non-public.
  ~LoopBase() {
    for (auto *SubLoop : SubLoops)
      SubLoop->~LoopT();

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    IsInvalid = true;
#endif
    SubLoops.clear();
    Blocks.clear();
    DenseBlockSet.clear();
    ParentLoop = nullptr;
  }
};

template <class BlockT, class LoopT>
raw_ostream &operator<<(raw_ostream &OS, const LoopBase<BlockT, LoopT> &Loop) {
  Loop.print(OS);
  return OS;
}

//===----------------------------------------------------------------------===//
/// This class builds and contains all of the top-level loop
/// structures in the specified function.
///

template <class BlockT, class LoopT> class LoopInfoBase {
  // BBMap - Mapping of basic blocks to the inner most loop they occur in
  DenseMap<const BlockT *, LoopT *> BBMap;
  std::vector<LoopT *> TopLevelLoops;
  BumpPtrAllocator LoopAllocator;

  friend class LoopBase<BlockT, LoopT>;
  friend class LoopInfo;

  void operator=(const LoopInfoBase &) = delete;
  LoopInfoBase(const LoopInfoBase &) = delete;

public:
  LoopInfoBase() = default;
  ~LoopInfoBase() { releaseMemory(); }

  LoopInfoBase(LoopInfoBase &&Arg)
      : BBMap(std::move(Arg.BBMap)),
        TopLevelLoops(std::move(Arg.TopLevelLoops)),
        LoopAllocator(std::move(Arg.LoopAllocator)) {
    // We have to clear the arguments top level loops as we've taken ownership.
    Arg.TopLevelLoops.clear();
  }
  LoopInfoBase &operator=(LoopInfoBase &&RHS) {
    BBMap = std::move(RHS.BBMap);

    for (auto *L : TopLevelLoops)
      L->~LoopT();

    TopLevelLoops = std::move(RHS.TopLevelLoops);
    LoopAllocator = std::move(RHS.LoopAllocator);
    RHS.TopLevelLoops.clear();
    return *this;
  }

  void releaseMemory() {
    BBMap.clear();

    for (auto *L : TopLevelLoops)
      L->~LoopT();
    TopLevelLoops.clear();
    LoopAllocator.Reset();
  }

  template <typename... ArgsTy> LoopT *AllocateLoop(ArgsTy &&...Args) {
    LoopT *Storage = LoopAllocator.Allocate<LoopT>();
    return new (Storage) LoopT(std::forward<ArgsTy>(Args)...);
  }

  /// iterator/begin/end - The interface to the top-level loops in the current
  /// function.
  ///
  typedef typename std::vector<LoopT *>::const_iterator iterator;
  typedef
      typename std::vector<LoopT *>::const_reverse_iterator reverse_iterator;
  iterator begin() const { return TopLevelLoops.begin(); }
  iterator end() const { return TopLevelLoops.end(); }
  reverse_iterator rbegin() const { return TopLevelLoops.rbegin(); }
  reverse_iterator rend() const { return TopLevelLoops.rend(); }
  bool empty() const { return TopLevelLoops.empty(); }

  /// Return all of the loops in the function in preorder across the loop
  /// nests, with siblings in forward program order.
  ///
  /// Note that because loops form a forest of trees, preorder is equivalent to
  /// reverse postorder.
  SmallVector<LoopT *, 4> getLoopsInPreorder() const;

  /// Return all of the loops in the function in preorder across the loop
  /// nests, with siblings in *reverse* program order.
  ///
  /// Note that because loops form a forest of trees, preorder is equivalent to
  /// reverse postorder.
  ///
  /// Also note that this is *not* a reverse preorder. Only the siblings are in
  /// reverse program order.
  SmallVector<LoopT *, 4> getLoopsInReverseSiblingPreorder() const;

  /// Return the inner most loop that BB lives in. If a basic block is in no
  /// loop (for example the entry node), null is returned.
  LoopT *getLoopFor(const BlockT *BB) const { return BBMap.lookup(BB); }

  /// Same as getLoopFor.
  const LoopT *operator[](const BlockT *BB) const { return getLoopFor(BB); }

  /// Return the loop nesting level of the specified block. A depth of 0 means
  /// the block is not inside any loop.
  unsigned getLoopDepth(const BlockT *BB) const {
    const LoopT *L = getLoopFor(BB);
    return L ? L->getLoopDepth() : 0;
  }

  // True if the block is a loop header node
  bool isLoopHeader(const BlockT *BB) const {
    const LoopT *L = getLoopFor(BB);
    return L && L->getHeader() == BB;
  }

  /// Return the top-level loops.
  const std::vector<LoopT *> &getTopLevelLoops() const { return TopLevelLoops; }

  /// Return the top-level loops.
  std::vector<LoopT *> &getTopLevelLoopsVector() { return TopLevelLoops; }

  /// This removes the specified top-level loop from this loop info object.
  /// The loop is not deleted, as it will presumably be inserted into
  /// another loop.
  LoopT *removeLoop(iterator I) {
    assert(I != end() && "Cannot remove end iterator!");
    LoopT *L = *I;
    assert(L->isOutermost() && "Not a top-level loop!");
    TopLevelLoops.erase(TopLevelLoops.begin() + (I - begin()));
    return L;
  }

  /// Change the top-level loop that contains BB to the specified loop.
  /// This should be used by transformations that restructure the loop hierarchy
  /// tree.
  void changeLoopFor(BlockT *BB, LoopT *L) {
    if (!L) {
      BBMap.erase(BB);
      return;
    }
    BBMap[BB] = L;
  }

  /// Replace the specified loop in the top-level loops list with the indicated
  /// loop.
  void changeTopLevelLoop(LoopT *OldLoop, LoopT *NewLoop) {
    auto I = find(TopLevelLoops, OldLoop);
    assert(I != TopLevelLoops.end() && "Old loop not at top level!");
    *I = NewLoop;
    assert(!NewLoop->ParentLoop && !OldLoop->ParentLoop &&
           "Loops already embedded into a subloop!");
  }

  /// This adds the specified loop to the collection of top-level loops.
  void addTopLevelLoop(LoopT *New) {
    assert(New->isOutermost() && "Loop already in subloop!");
    TopLevelLoops.push_back(New);
  }

  /// This method completely removes BB from all data structures,
  /// including all of the Loop objects it is nested in and our mapping from
  /// BasicBlocks to loops.
  void removeBlock(BlockT *BB) {
    auto I = BBMap.find(BB);
    if (I != BBMap.end()) {
      for (LoopT *L = I->second; L; L = L->getParentLoop())
        L->removeBlockFromLoop(BB);

      BBMap.erase(I);
    }
  }

  // Internals

  static bool isNotAlreadyContainedIn(const LoopT *SubLoop,
                                      const LoopT *ParentLoop) {
    if (!SubLoop)
      return true;
    if (SubLoop == ParentLoop)
      return false;
    return isNotAlreadyContainedIn(SubLoop->getParentLoop(), ParentLoop);
  }

  /// Create the loop forest using a stable algorithm.
  void analyze(const DominatorTreeBase<BlockT, false> &DomTree);

  // Debugging
  void print(raw_ostream &OS) const;

  void verify(const DominatorTreeBase<BlockT, false> &DomTree) const;

  /// Destroy a loop that has been removed from the `LoopInfo` nest.
  ///
  /// This runs the destructor of the loop object making it invalid to
  /// reference afterward. The memory is retained so that the *pointer* to the
  /// loop remains valid.
  ///
  /// The caller is responsible for removing this loop from the loop nest and
  /// otherwise disconnecting it from the broader `LoopInfo` data structures.
  /// Callers that don't naturally handle this themselves should probably call
  /// `erase' instead.
  void destroy(LoopT *L) {
    L->~LoopT();

    // Since LoopAllocator is a BumpPtrAllocator, this Deallocate only poisons
    // \c L, but the pointer remains valid for non-dereferencing uses.
    LoopAllocator.Deallocate(L);
  }
};

} // namespace llvm

#endif // LLVM_SUPPORT_GENERICLOOPINFO_H
