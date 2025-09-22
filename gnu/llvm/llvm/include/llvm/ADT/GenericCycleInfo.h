//===- GenericCycleInfo.h - Info for Cycles in any IR ------*- C++ -*------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Find all cycles in a control-flow graph, including irreducible loops.
///
/// See docs/CycleTerminology.rst for a formal definition of cycles.
///
/// Briefly:
/// - A cycle is a generalization of a loop which can represent
///   irreducible control flow.
/// - Cycles identified in a program are implementation defined,
///   depending on the DFS traversal chosen.
/// - Cycles are well-nested, and form a forest with a parent-child
///   relationship.
/// - In any choice of DFS, every natural loop L is represented by a
///   unique cycle C which is a superset of L.
/// - In the absence of irreducible control flow, the cycles are
///   exactly the natural loops in the program.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_GENERICCYCLEINFO_H
#define LLVM_ADT_GENERICCYCLEINFO_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/GenericSSAContext.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

template <typename ContextT> class GenericCycleInfo;
template <typename ContextT> class GenericCycleInfoCompute;

/// A possibly irreducible generalization of a \ref Loop.
template <typename ContextT> class GenericCycle {
public:
  using BlockT = typename ContextT::BlockT;
  using FunctionT = typename ContextT::FunctionT;
  template <typename> friend class GenericCycleInfo;
  template <typename> friend class GenericCycleInfoCompute;

private:
  /// The parent cycle. Is null for the root "cycle". Top-level cycles point
  /// at the root.
  GenericCycle *ParentCycle = nullptr;

  /// The entry block(s) of the cycle. The header is the only entry if
  /// this is a loop. Is empty for the root "cycle", to avoid
  /// unnecessary memory use.
  SmallVector<BlockT *, 1> Entries;

  /// Child cycles, if any.
  std::vector<std::unique_ptr<GenericCycle>> Children;

  /// Basic blocks that are contained in the cycle, including entry blocks,
  /// and including blocks that are part of a child cycle.
  using BlockSetVectorT = SetVector<BlockT *, SmallVector<BlockT *, 8>,
                                    DenseSet<const BlockT *>, 8>;
  BlockSetVectorT Blocks;

  /// Depth of the cycle in the tree. The root "cycle" is at depth 0.
  ///
  /// \note Depths are not necessarily contiguous. However, child loops always
  ///       have strictly greater depth than their parents, and sibling loops
  ///       always have the same depth.
  unsigned Depth = 0;

  void clear() {
    Entries.clear();
    Children.clear();
    Blocks.clear();
    Depth = 0;
    ParentCycle = nullptr;
  }

  void appendEntry(BlockT *Block) { Entries.push_back(Block); }
  void appendBlock(BlockT *Block) { Blocks.insert(Block); }

  GenericCycle(const GenericCycle &) = delete;
  GenericCycle &operator=(const GenericCycle &) = delete;
  GenericCycle(GenericCycle &&Rhs) = delete;
  GenericCycle &operator=(GenericCycle &&Rhs) = delete;

public:
  GenericCycle() = default;

  /// \brief Whether the cycle is a natural loop.
  bool isReducible() const { return Entries.size() == 1; }

  BlockT *getHeader() const { return Entries[0]; }

  const SmallVectorImpl<BlockT *> & getEntries() const {
    return Entries;
  }

  /// \brief Return whether \p Block is an entry block of the cycle.
  bool isEntry(const BlockT *Block) const {
    return is_contained(Entries, Block);
  }

  /// \brief Return whether \p Block is contained in the cycle.
  bool contains(const BlockT *Block) const { return Blocks.contains(Block); }

  /// \brief Returns true iff this cycle contains \p C.
  ///
  /// Note: Non-strict containment check, i.e. returns true if C is the
  /// same cycle.
  bool contains(const GenericCycle *C) const;

  const GenericCycle *getParentCycle() const { return ParentCycle; }
  GenericCycle *getParentCycle() { return ParentCycle; }
  unsigned getDepth() const { return Depth; }

  /// Return all of the successor blocks of this cycle.
  ///
  /// These are the blocks _outside of the current cycle_ which are
  /// branched to.
  void getExitBlocks(SmallVectorImpl<BlockT *> &TmpStorage) const;

  /// Return all blocks of this cycle that have successor outside of this cycle.
  /// These blocks have cycle exit branch.
  void getExitingBlocks(SmallVectorImpl<BlockT *> &TmpStorage) const;

  /// Return the preheader block for this cycle. Pre-header is well-defined for
  /// reducible cycle in docs/LoopTerminology.rst as: the only one entering
  /// block and its only edge is to the entry block. Return null for irreducible
  /// cycles.
  BlockT *getCyclePreheader() const;

  /// If the cycle has exactly one entry with exactly one predecessor, return
  /// it, otherwise return nullptr.
  BlockT *getCyclePredecessor() const;

  /// Iteration over child cycles.
  //@{
  using const_child_iterator_base =
      typename std::vector<std::unique_ptr<GenericCycle>>::const_iterator;
  struct const_child_iterator
      : iterator_adaptor_base<const_child_iterator, const_child_iterator_base> {
    using Base =
        iterator_adaptor_base<const_child_iterator, const_child_iterator_base>;

    const_child_iterator() = default;
    explicit const_child_iterator(const_child_iterator_base I) : Base(I) {}

    const const_child_iterator_base &wrapped() { return Base::wrapped(); }
    GenericCycle *operator*() const { return Base::I->get(); }
  };

  const_child_iterator child_begin() const {
    return const_child_iterator{Children.begin()};
  }
  const_child_iterator child_end() const {
    return const_child_iterator{Children.end()};
  }
  size_t getNumChildren() const { return Children.size(); }
  iterator_range<const_child_iterator> children() const {
    return llvm::make_range(const_child_iterator{Children.begin()},
                            const_child_iterator{Children.end()});
  }
  //@}

  /// Iteration over blocks in the cycle (including entry blocks).
  //@{
  using const_block_iterator = typename BlockSetVectorT::const_iterator;

  const_block_iterator block_begin() const {
    return const_block_iterator{Blocks.begin()};
  }
  const_block_iterator block_end() const {
    return const_block_iterator{Blocks.end()};
  }
  size_t getNumBlocks() const { return Blocks.size(); }
  iterator_range<const_block_iterator> blocks() const {
    return llvm::make_range(block_begin(), block_end());
  }
  //@}

  /// Iteration over entry blocks.
  //@{
  using const_entry_iterator =
      typename SmallVectorImpl<BlockT *>::const_iterator;

  size_t getNumEntries() const { return Entries.size(); }
  iterator_range<const_entry_iterator> entries() const {
    return llvm::make_range(Entries.begin(), Entries.end());
  }
  //@}

  Printable printEntries(const ContextT &Ctx) const {
    return Printable([this, &Ctx](raw_ostream &Out) {
      bool First = true;
      for (auto *Entry : Entries) {
        if (!First)
          Out << ' ';
        First = false;
        Out << Ctx.print(Entry);
      }
    });
  }

  Printable print(const ContextT &Ctx) const {
    return Printable([this, &Ctx](raw_ostream &Out) {
      Out << "depth=" << Depth << ": entries(" << printEntries(Ctx) << ')';

      for (auto *Block : Blocks) {
        if (isEntry(Block))
          continue;

        Out << ' ' << Ctx.print(Block);
      }
    });
  }
};

/// \brief Cycle information for a function.
template <typename ContextT> class GenericCycleInfo {
public:
  using BlockT = typename ContextT::BlockT;
  using CycleT = GenericCycle<ContextT>;
  using FunctionT = typename ContextT::FunctionT;
  template <typename> friend class GenericCycle;
  template <typename> friend class GenericCycleInfoCompute;

private:
  ContextT Context;

  /// Map basic blocks to their inner-most containing cycle.
  DenseMap<BlockT *, CycleT *> BlockMap;

  /// Map basic blocks to their top level containing cycle.
  DenseMap<BlockT *, CycleT *> BlockMapTopLevel;

  /// Top-level cycles discovered by any DFS.
  ///
  /// Note: The implementation treats the nullptr as the parent of
  /// every top-level cycle. See \ref contains for an example.
  std::vector<std::unique_ptr<CycleT>> TopLevelCycles;

  /// Move \p Child to \p NewParent by manipulating Children vectors.
  ///
  /// Note: This is an incomplete operation that does not update the depth of
  /// the subtree.
  void moveTopLevelCycleToNewParent(CycleT *NewParent, CycleT *Child);

  /// Assumes that \p Cycle is the innermost cycle containing \p Block.
  /// \p Block will be appended to \p Cycle and all of its parent cycles.
  /// \p Block will be added to BlockMap with \p Cycle and
  /// BlockMapTopLevel with \p Cycle's top level parent cycle.
  void addBlockToCycle(BlockT *Block, CycleT *Cycle);

public:
  GenericCycleInfo() = default;
  GenericCycleInfo(GenericCycleInfo &&) = default;
  GenericCycleInfo &operator=(GenericCycleInfo &&) = default;

  void clear();
  void compute(FunctionT &F);
  void splitCriticalEdge(BlockT *Pred, BlockT *Succ, BlockT *New);

  const FunctionT *getFunction() const { return Context.getFunction(); }
  const ContextT &getSSAContext() const { return Context; }

  CycleT *getCycle(const BlockT *Block) const;
  CycleT *getSmallestCommonCycle(CycleT *A, CycleT *B) const;
  unsigned getCycleDepth(const BlockT *Block) const;
  CycleT *getTopLevelParentCycle(BlockT *Block);

  /// Methods for debug and self-test.
  //@{
#ifndef NDEBUG
  bool validateTree() const;
#endif
  void print(raw_ostream &Out) const;
  void dump() const { print(dbgs()); }
  Printable print(const CycleT *Cycle) { return Cycle->print(Context); }
  //@}

  /// Iteration over top-level cycles.
  //@{
  using const_toplevel_iterator_base =
      typename std::vector<std::unique_ptr<CycleT>>::const_iterator;
  struct const_toplevel_iterator
      : iterator_adaptor_base<const_toplevel_iterator,
                              const_toplevel_iterator_base> {
    using Base = iterator_adaptor_base<const_toplevel_iterator,
                                       const_toplevel_iterator_base>;

    const_toplevel_iterator() = default;
    explicit const_toplevel_iterator(const_toplevel_iterator_base I)
        : Base(I) {}

    const const_toplevel_iterator_base &wrapped() { return Base::wrapped(); }
    CycleT *operator*() const { return Base::I->get(); }
  };

  const_toplevel_iterator toplevel_begin() const {
    return const_toplevel_iterator{TopLevelCycles.begin()};
  }
  const_toplevel_iterator toplevel_end() const {
    return const_toplevel_iterator{TopLevelCycles.end()};
  }

  iterator_range<const_toplevel_iterator> toplevel_cycles() const {
    return llvm::make_range(const_toplevel_iterator{TopLevelCycles.begin()},
                            const_toplevel_iterator{TopLevelCycles.end()});
  }
  //@}
};

/// \brief GraphTraits for iterating over a sub-tree of the CycleT tree.
template <typename CycleRefT, typename ChildIteratorT> struct CycleGraphTraits {
  using NodeRef = CycleRefT;

  using nodes_iterator = ChildIteratorT;
  using ChildIteratorType = nodes_iterator;

  static NodeRef getEntryNode(NodeRef Graph) { return Graph; }

  static ChildIteratorType child_begin(NodeRef Ref) {
    return Ref->child_begin();
  }
  static ChildIteratorType child_end(NodeRef Ref) { return Ref->child_end(); }

  // Not implemented:
  // static nodes_iterator nodes_begin(GraphType *G)
  // static nodes_iterator nodes_end  (GraphType *G)
  //    nodes_iterator/begin/end - Allow iteration over all nodes in the graph

  // typedef EdgeRef           - Type of Edge token in the graph, which should
  //                             be cheap to copy.
  // typedef ChildEdgeIteratorType - Type used to iterate over children edges in
  //                             graph, dereference to a EdgeRef.

  // static ChildEdgeIteratorType child_edge_begin(NodeRef)
  // static ChildEdgeIteratorType child_edge_end(NodeRef)
  //     Return iterators that point to the beginning and ending of the
  //     edge list for the given callgraph node.
  //
  // static NodeRef edge_dest(EdgeRef)
  //     Return the destination node of an edge.
  // static unsigned       size       (GraphType *G)
  //    Return total number of nodes in the graph
};

template <typename BlockT>
struct GraphTraits<const GenericCycle<BlockT> *>
    : CycleGraphTraits<const GenericCycle<BlockT> *,
                       typename GenericCycle<BlockT>::const_child_iterator> {};
template <typename BlockT>
struct GraphTraits<GenericCycle<BlockT> *>
    : CycleGraphTraits<GenericCycle<BlockT> *,
                       typename GenericCycle<BlockT>::const_child_iterator> {};

} // namespace llvm

#endif // LLVM_ADT_GENERICCYCLEINFO_H
