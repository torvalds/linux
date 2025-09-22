//===- GenericUniformityImpl.h -----------------------*- C++ -*------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This template implementation resides in a separate file so that it
// does not get injected into every .cpp file that includes the
// generic header.
//
// DO NOT INCLUDE THIS FILE WHEN MERELY USING UNIFORMITYINFO.
//
// This file should only be included by files that implement a
// specialization of the relvant templates. Currently these are:
// - UniformityAnalysis.cpp
//
// Note: The DEBUG_TYPE macro should be defined before using this
// file so that any use of LLVM_DEBUG is associated with the
// including file rather than this file.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implementation of uniformity analysis.
///
/// The algorithm is a fixed point iteration that starts with the assumption
/// that all control flow and all values are uniform. Starting from sources of
/// divergence (whose discovery must be implemented by a CFG- or even
/// target-specific derived class), divergence of values is propagated from
/// definition to uses in a straight-forward way. The main complexity lies in
/// the propagation of the impact of divergent control flow on the divergence of
/// values (sync dependencies).
///
/// NOTE: In general, no interface exists for a transform to update
/// (Machine)UniformityInfo. Additionally, (Machine)CycleAnalysis is a
/// transitive dependence, but it also does not provide an interface for
/// updating itself. Given that, transforms should not preserve uniformity in
/// their getAnalysisUsage() callback.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_GENERICUNIFORMITYIMPL_H
#define LLVM_ADT_GENERICUNIFORMITYIMPL_H

#include "llvm/ADT/GenericUniformityInfo.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "uniformity"

namespace llvm {

/// Construct a specially modified post-order traversal of cycles.
///
/// The ModifiedPO is contructed using a virtually modified CFG as follows:
///
/// 1. The successors of pre-entry nodes (predecessors of an cycle
///    entry that are outside the cycle) are replaced by the
///    successors of the successors of the header.
/// 2. Successors of the cycle header are replaced by the exit blocks
///    of the cycle.
///
/// Effectively, we produce a depth-first numbering with the following
/// properties:
///
/// 1. Nodes after a cycle are numbered earlier than the cycle header.
/// 2. The header is numbered earlier than the nodes in the cycle.
/// 3. The numbering of the nodes within the cycle forms an interval
///    starting with the header.
///
/// Effectively, the virtual modification arranges the nodes in a
/// cycle as a DAG with the header as the sole leaf, and successors of
/// the header as the roots. A reverse traversal of this numbering has
/// the following invariant on the unmodified original CFG:
///
///    Each node is visited after all its predecessors, except if that
///    predecessor is the cycle header.
///
template <typename ContextT> class ModifiedPostOrder {
public:
  using BlockT = typename ContextT::BlockT;
  using FunctionT = typename ContextT::FunctionT;
  using DominatorTreeT = typename ContextT::DominatorTreeT;

  using CycleInfoT = GenericCycleInfo<ContextT>;
  using CycleT = typename CycleInfoT::CycleT;
  using const_iterator = typename std::vector<BlockT *>::const_iterator;

  ModifiedPostOrder(const ContextT &C) : Context(C) {}

  bool empty() const { return m_order.empty(); }
  size_t size() const { return m_order.size(); }

  void clear() { m_order.clear(); }
  void compute(const CycleInfoT &CI);

  unsigned count(BlockT *BB) const { return POIndex.count(BB); }
  const BlockT *operator[](size_t idx) const { return m_order[idx]; }

  void appendBlock(const BlockT &BB, bool isReducibleCycleHeader = false) {
    POIndex[&BB] = m_order.size();
    m_order.push_back(&BB);
    LLVM_DEBUG(dbgs() << "ModifiedPO(" << POIndex[&BB]
                      << "): " << Context.print(&BB) << "\n");
    if (isReducibleCycleHeader)
      ReducibleCycleHeaders.insert(&BB);
  }

  unsigned getIndex(const BlockT *BB) const {
    assert(POIndex.count(BB));
    return POIndex.lookup(BB);
  }

  bool isReducibleCycleHeader(const BlockT *BB) const {
    return ReducibleCycleHeaders.contains(BB);
  }

private:
  SmallVector<const BlockT *> m_order;
  DenseMap<const BlockT *, unsigned> POIndex;
  SmallPtrSet<const BlockT *, 32> ReducibleCycleHeaders;
  const ContextT &Context;

  void computeCyclePO(const CycleInfoT &CI, const CycleT *Cycle,
                      SmallPtrSetImpl<const BlockT *> &Finalized);

  void computeStackPO(SmallVectorImpl<const BlockT *> &Stack,
                      const CycleInfoT &CI, const CycleT *Cycle,
                      SmallPtrSetImpl<const BlockT *> &Finalized);
};

template <typename> class DivergencePropagator;

/// \class GenericSyncDependenceAnalysis
///
/// \brief Locate join blocks for disjoint paths starting at a divergent branch.
///
/// An analysis per divergent branch that returns the set of basic
/// blocks whose phi nodes become divergent due to divergent control.
/// These are the blocks that are reachable by two disjoint paths from
/// the branch, or cycle exits reachable along a path that is disjoint
/// from a path to the cycle latch.

// --- Above line is not a doxygen comment; intentionally left blank ---
//
// Originally implemented in SyncDependenceAnalysis.cpp for DivergenceAnalysis.
//
// The SyncDependenceAnalysis is used in the UniformityAnalysis to model
// control-induced divergence in phi nodes.
//
// -- Reference --
// The algorithm is an extension of Section 5 of
//
//   An abstract interpretation for SPMD divergence
//       on reducible control flow graphs.
//   Julian Rosemann, Simon Moll and Sebastian Hack
//   POPL '21
//
//
// -- Sync dependence --
// Sync dependence characterizes the control flow aspect of the
// propagation of branch divergence. For example,
//
//   %cond = icmp slt i32 %tid, 10
//   br i1 %cond, label %then, label %else
// then:
//   br label %merge
// else:
//   br label %merge
// merge:
//   %a = phi i32 [ 0, %then ], [ 1, %else ]
//
// Suppose %tid holds the thread ID. Although %a is not data dependent on %tid
// because %tid is not on its use-def chains, %a is sync dependent on %tid
// because the branch "br i1 %cond" depends on %tid and affects which value %a
// is assigned to.
//
//
// -- Reduction to SSA construction --
// There are two disjoint paths from A to X, if a certain variant of SSA
// construction places a phi node in X under the following set-up scheme.
//
// This variant of SSA construction ignores incoming undef values.
// That is paths from the entry without a definition do not result in
// phi nodes.
//
//       entry
//     /      \
//    A        \
//  /   \       Y
// B     C     /
//  \   /  \  /
//    D     E
//     \   /
//       F
//
// Assume that A contains a divergent branch. We are interested
// in the set of all blocks where each block is reachable from A
// via two disjoint paths. This would be the set {D, F} in this
// case.
// To generally reduce this query to SSA construction we introduce
// a virtual variable x and assign to x different values in each
// successor block of A.
//
//           entry
//         /      \
//        A        \
//      /   \       Y
// x = 0   x = 1   /
//      \  /   \  /
//        D     E
//         \   /
//           F
//
// Our flavor of SSA construction for x will construct the following
//
//            entry
//          /      \
//         A        \
//       /   \       Y
// x0 = 0   x1 = 1  /
//       \   /   \ /
//     x2 = phi   E
//         \     /
//         x3 = phi
//
// The blocks D and F contain phi nodes and are thus each reachable
// by two disjoins paths from A.
//
// -- Remarks --
// * In case of cycle exits we need to check for temporal divergence.
//   To this end, we check whether the definition of x differs between the
//   cycle exit and the cycle header (_after_ SSA construction).
//
// * In the presence of irreducible control flow, the fixed point is
//   reached only after multiple iterations. This is because labels
//   reaching the header of a cycle must be repropagated through the
//   cycle. This is true even in a reducible cycle, since the labels
//   may have been produced by a nested irreducible cycle.
//
// * Note that SyncDependenceAnalysis is not concerned with the points
//   of convergence in an irreducible cycle. It's only purpose is to
//   identify join blocks. The "diverged entry" criterion is
//   separately applied on join blocks to determine if an entire
//   irreducible cycle is assumed to be divergent.
//
// * Relevant related work:
//     A simple algorithm for global data flow analysis problems.
//     Matthew S. Hecht and Jeffrey D. Ullman.
//     SIAM Journal on Computing, 4(4):519–532, December 1975.
//
template <typename ContextT> class GenericSyncDependenceAnalysis {
public:
  using BlockT = typename ContextT::BlockT;
  using DominatorTreeT = typename ContextT::DominatorTreeT;
  using FunctionT = typename ContextT::FunctionT;
  using ValueRefT = typename ContextT::ValueRefT;
  using InstructionT = typename ContextT::InstructionT;

  using CycleInfoT = GenericCycleInfo<ContextT>;
  using CycleT = typename CycleInfoT::CycleT;

  using ConstBlockSet = SmallPtrSet<const BlockT *, 4>;
  using ModifiedPO = ModifiedPostOrder<ContextT>;

  // * if BlockLabels[B] == C then C is the dominating definition at
  //   block B
  // * if BlockLabels[B] == nullptr then we haven't seen B yet
  // * if BlockLabels[B] == B then:
  //   - B is a join point of disjoint paths from X, or,
  //   - B is an immediate successor of X (initial value), or,
  //   - B is X
  using BlockLabelMap = DenseMap<const BlockT *, const BlockT *>;

  /// Information discovered by the sync dependence analysis for each
  /// divergent branch.
  struct DivergenceDescriptor {
    // Join points of diverged paths.
    ConstBlockSet JoinDivBlocks;
    // Divergent cycle exits
    ConstBlockSet CycleDivBlocks;
    // Labels assigned to blocks on diverged paths.
    BlockLabelMap BlockLabels;
  };

  using DivergencePropagatorT = DivergencePropagator<ContextT>;

  GenericSyncDependenceAnalysis(const ContextT &Context,
                                const DominatorTreeT &DT, const CycleInfoT &CI);

  /// \brief Computes divergent join points and cycle exits caused by branch
  /// divergence in \p Term.
  ///
  /// This returns a pair of sets:
  /// * The set of blocks which are reachable by disjoint paths from
  ///   \p Term.
  /// * The set also contains cycle exits if there two disjoint paths:
  ///   one from \p Term to the cycle exit and another from \p Term to
  ///   the cycle header.
  const DivergenceDescriptor &getJoinBlocks(const BlockT *DivTermBlock);

private:
  static DivergenceDescriptor EmptyDivergenceDesc;

  ModifiedPO CyclePO;

  const DominatorTreeT &DT;
  const CycleInfoT &CI;

  DenseMap<const BlockT *, std::unique_ptr<DivergenceDescriptor>>
      CachedControlDivDescs;
};

/// \brief Analysis that identifies uniform values in a data-parallel
/// execution.
///
/// This analysis propagates divergence in a data-parallel context
/// from sources of divergence to all users. It can be instantiated
/// for an IR that provides a suitable SSAContext.
template <typename ContextT> class GenericUniformityAnalysisImpl {
public:
  using BlockT = typename ContextT::BlockT;
  using FunctionT = typename ContextT::FunctionT;
  using ValueRefT = typename ContextT::ValueRefT;
  using ConstValueRefT = typename ContextT::ConstValueRefT;
  using UseT = typename ContextT::UseT;
  using InstructionT = typename ContextT::InstructionT;
  using DominatorTreeT = typename ContextT::DominatorTreeT;

  using CycleInfoT = GenericCycleInfo<ContextT>;
  using CycleT = typename CycleInfoT::CycleT;

  using SyncDependenceAnalysisT = GenericSyncDependenceAnalysis<ContextT>;
  using DivergenceDescriptorT =
      typename SyncDependenceAnalysisT::DivergenceDescriptor;
  using BlockLabelMapT = typename SyncDependenceAnalysisT::BlockLabelMap;

  GenericUniformityAnalysisImpl(const DominatorTreeT &DT, const CycleInfoT &CI,
                                const TargetTransformInfo *TTI)
      : Context(CI.getSSAContext()), F(*Context.getFunction()), CI(CI),
        TTI(TTI), DT(DT), SDA(Context, DT, CI) {}

  void initialize();

  const FunctionT &getFunction() const { return F; }

  /// \brief Mark \p UniVal as a value that is always uniform.
  void addUniformOverride(const InstructionT &Instr);

  /// \brief Examine \p I for divergent outputs and add to the worklist.
  void markDivergent(const InstructionT &I);

  /// \brief Mark \p DivVal as a divergent value.
  /// \returns Whether the tracked divergence state of \p DivVal changed.
  bool markDivergent(ConstValueRefT DivVal);

  /// \brief Mark outputs of \p Instr as divergent.
  /// \returns Whether the tracked divergence state of any output has changed.
  bool markDefsDivergent(const InstructionT &Instr);

  /// \brief Propagate divergence to all instructions in the region.
  /// Divergence is seeded by calls to \p markDivergent.
  void compute();

  /// \brief Whether any value was marked or analyzed to be divergent.
  bool hasDivergence() const { return !DivergentValues.empty(); }

  /// \brief Whether \p Val will always return a uniform value regardless of its
  /// operands
  bool isAlwaysUniform(const InstructionT &Instr) const;

  bool hasDivergentDefs(const InstructionT &I) const;

  bool isDivergent(const InstructionT &I) const {
    if (I.isTerminator()) {
      return DivergentTermBlocks.contains(I.getParent());
    }
    return hasDivergentDefs(I);
  };

  /// \brief Whether \p Val is divergent at its definition.
  bool isDivergent(ConstValueRefT V) const { return DivergentValues.count(V); }

  bool isDivergentUse(const UseT &U) const;

  bool hasDivergentTerminator(const BlockT &B) const {
    return DivergentTermBlocks.contains(&B);
  }

  void print(raw_ostream &out) const;

protected:
  /// \brief Value/block pair representing a single phi input.
  struct PhiInput {
    ConstValueRefT value;
    BlockT *predBlock;

    PhiInput(ConstValueRefT value, BlockT *predBlock)
        : value(value), predBlock(predBlock) {}
  };

  const ContextT &Context;
  const FunctionT &F;
  const CycleInfoT &CI;
  const TargetTransformInfo *TTI = nullptr;

  // Detected/marked divergent values.
  DenseSet<ConstValueRefT> DivergentValues;
  SmallPtrSet<const BlockT *, 32> DivergentTermBlocks;

  // Internal worklist for divergence propagation.
  std::vector<const InstructionT *> Worklist;

  /// \brief Mark \p Term as divergent and push all Instructions that become
  /// divergent as a result on the worklist.
  void analyzeControlDivergence(const InstructionT &Term);

private:
  const DominatorTreeT &DT;

  // Recognized cycles with divergent exits.
  SmallPtrSet<const CycleT *, 16> DivergentExitCycles;

  // Cycles assumed to be divergent.
  //
  // We don't use a set here because every insertion needs an explicit
  // traversal of all existing members.
  SmallVector<const CycleT *> AssumedDivergent;

  // The SDA links divergent branches to divergent control-flow joins.
  SyncDependenceAnalysisT SDA;

  // Set of known-uniform values.
  SmallPtrSet<const InstructionT *, 32> UniformOverrides;

  /// \brief Mark all nodes in \p JoinBlock as divergent and push them on
  /// the worklist.
  void taintAndPushAllDefs(const BlockT &JoinBlock);

  /// \brief Mark all phi nodes in \p JoinBlock as divergent and push them on
  /// the worklist.
  void taintAndPushPhiNodes(const BlockT &JoinBlock);

  /// \brief Identify all Instructions that become divergent because \p DivExit
  /// is a divergent cycle exit of \p DivCycle. Mark those instructions as
  /// divergent and push them on the worklist.
  void propagateCycleExitDivergence(const BlockT &DivExit,
                                    const CycleT &DivCycle);

  /// Mark as divergent all external uses of values defined in \p DefCycle.
  void analyzeCycleExitDivergence(const CycleT &DefCycle);

  /// \brief Mark as divergent all uses of \p I that are outside \p DefCycle.
  void propagateTemporalDivergence(const InstructionT &I,
                                   const CycleT &DefCycle);

  /// \brief Push all users of \p Val (in the region) to the worklist.
  void pushUsers(const InstructionT &I);
  void pushUsers(ConstValueRefT V);

  bool usesValueFromCycle(const InstructionT &I, const CycleT &DefCycle) const;

  /// \brief Whether \p Def is divergent when read in \p ObservingBlock.
  bool isTemporalDivergent(const BlockT &ObservingBlock,
                           const InstructionT &Def) const;
};

template <typename ImplT>
void GenericUniformityAnalysisImplDeleter<ImplT>::operator()(ImplT *Impl) {
  delete Impl;
}

/// Compute divergence starting with a divergent branch.
template <typename ContextT> class DivergencePropagator {
public:
  using BlockT = typename ContextT::BlockT;
  using DominatorTreeT = typename ContextT::DominatorTreeT;
  using FunctionT = typename ContextT::FunctionT;
  using ValueRefT = typename ContextT::ValueRefT;

  using CycleInfoT = GenericCycleInfo<ContextT>;
  using CycleT = typename CycleInfoT::CycleT;

  using ModifiedPO = ModifiedPostOrder<ContextT>;
  using SyncDependenceAnalysisT = GenericSyncDependenceAnalysis<ContextT>;
  using DivergenceDescriptorT =
      typename SyncDependenceAnalysisT::DivergenceDescriptor;
  using BlockLabelMapT = typename SyncDependenceAnalysisT::BlockLabelMap;

  const ModifiedPO &CyclePOT;
  const DominatorTreeT &DT;
  const CycleInfoT &CI;
  const BlockT &DivTermBlock;
  const ContextT &Context;

  // Track blocks that receive a new label. Every time we relabel a
  // cycle header, we another pass over the modified post-order in
  // order to propagate the header label. The bit vector also allows
  // us to skip labels that have not changed.
  SparseBitVector<> FreshLabels;

  // divergent join and cycle exit descriptor.
  std::unique_ptr<DivergenceDescriptorT> DivDesc;
  BlockLabelMapT &BlockLabels;

  DivergencePropagator(const ModifiedPO &CyclePOT, const DominatorTreeT &DT,
                       const CycleInfoT &CI, const BlockT &DivTermBlock)
      : CyclePOT(CyclePOT), DT(DT), CI(CI), DivTermBlock(DivTermBlock),
        Context(CI.getSSAContext()), DivDesc(new DivergenceDescriptorT),
        BlockLabels(DivDesc->BlockLabels) {}

  void printDefs(raw_ostream &Out) {
    Out << "Propagator::BlockLabels {\n";
    for (int BlockIdx = (int)CyclePOT.size() - 1; BlockIdx >= 0; --BlockIdx) {
      const auto *Block = CyclePOT[BlockIdx];
      const auto *Label = BlockLabels[Block];
      Out << Context.print(Block) << "(" << BlockIdx << ") : ";
      if (!Label) {
        Out << "<null>\n";
      } else {
        Out << Context.print(Label) << "\n";
      }
    }
    Out << "}\n";
  }

  // Push a definition (\p PushedLabel) to \p SuccBlock and return whether this
  // causes a divergent join.
  bool computeJoin(const BlockT &SuccBlock, const BlockT &PushedLabel) {
    const auto *OldLabel = BlockLabels[&SuccBlock];

    LLVM_DEBUG(dbgs() << "labeling " << Context.print(&SuccBlock) << ":\n"
                      << "\tpushed label: " << Context.print(&PushedLabel)
                      << "\n"
                      << "\told label: " << Context.print(OldLabel) << "\n");

    // Early exit if there is no change in the label.
    if (OldLabel == &PushedLabel)
      return false;

    if (OldLabel != &SuccBlock) {
      auto SuccIdx = CyclePOT.getIndex(&SuccBlock);
      // Assigning a new label, mark this in FreshLabels.
      LLVM_DEBUG(dbgs() << "\tfresh label: " << SuccIdx << "\n");
      FreshLabels.set(SuccIdx);
    }

    // This is not a join if the succ was previously unlabeled.
    if (!OldLabel) {
      LLVM_DEBUG(dbgs() << "\tnew label: " << Context.print(&PushedLabel)
                        << "\n");
      BlockLabels[&SuccBlock] = &PushedLabel;
      return false;
    }

    // This is a new join. Label the join block as itself, and not as
    // the pushed label.
    LLVM_DEBUG(dbgs() << "\tnew label: " << Context.print(&SuccBlock) << "\n");
    BlockLabels[&SuccBlock] = &SuccBlock;

    return true;
  }

  // visiting a virtual cycle exit edge from the cycle header --> temporal
  // divergence on join
  bool visitCycleExitEdge(const BlockT &ExitBlock, const BlockT &Label) {
    if (!computeJoin(ExitBlock, Label))
      return false;

    // Identified a divergent cycle exit
    DivDesc->CycleDivBlocks.insert(&ExitBlock);
    LLVM_DEBUG(dbgs() << "\tDivergent cycle exit: " << Context.print(&ExitBlock)
                      << "\n");
    return true;
  }

  // process \p SuccBlock with reaching definition \p Label
  bool visitEdge(const BlockT &SuccBlock, const BlockT &Label) {
    if (!computeJoin(SuccBlock, Label))
      return false;

    // Divergent, disjoint paths join.
    DivDesc->JoinDivBlocks.insert(&SuccBlock);
    LLVM_DEBUG(dbgs() << "\tDivergent join: " << Context.print(&SuccBlock)
                      << "\n");
    return true;
  }

  std::unique_ptr<DivergenceDescriptorT> computeJoinPoints() {
    assert(DivDesc);

    LLVM_DEBUG(dbgs() << "SDA:computeJoinPoints: "
                      << Context.print(&DivTermBlock) << "\n");

    // Early stopping criterion
    int FloorIdx = CyclePOT.size() - 1;
    const BlockT *FloorLabel = nullptr;
    int DivTermIdx = CyclePOT.getIndex(&DivTermBlock);

    // Bootstrap with branch targets
    auto const *DivTermCycle = CI.getCycle(&DivTermBlock);
    for (const auto *SuccBlock : successors(&DivTermBlock)) {
      if (DivTermCycle && !DivTermCycle->contains(SuccBlock)) {
        // If DivTerm exits the cycle immediately, computeJoin() might
        // not reach SuccBlock with a different label. We need to
        // check for this exit now.
        DivDesc->CycleDivBlocks.insert(SuccBlock);
        LLVM_DEBUG(dbgs() << "\tImmediate divergent cycle exit: "
                          << Context.print(SuccBlock) << "\n");
      }
      auto SuccIdx = CyclePOT.getIndex(SuccBlock);
      visitEdge(*SuccBlock, *SuccBlock);
      FloorIdx = std::min<int>(FloorIdx, SuccIdx);
    }

    while (true) {
      auto BlockIdx = FreshLabels.find_last();
      if (BlockIdx == -1 || BlockIdx < FloorIdx)
        break;

      LLVM_DEBUG(dbgs() << "Current labels:\n"; printDefs(dbgs()));

      FreshLabels.reset(BlockIdx);
      if (BlockIdx == DivTermIdx) {
        LLVM_DEBUG(dbgs() << "Skipping DivTermBlock\n");
        continue;
      }

      const auto *Block = CyclePOT[BlockIdx];
      LLVM_DEBUG(dbgs() << "visiting " << Context.print(Block) << " at index "
                        << BlockIdx << "\n");

      const auto *Label = BlockLabels[Block];
      assert(Label);

      bool CausedJoin = false;
      int LoweredFloorIdx = FloorIdx;

      // If the current block is the header of a reducible cycle that
      // contains the divergent branch, then the label should be
      // propagated to the cycle exits. Such a header is the "last
      // possible join" of any disjoint paths within this cycle. This
      // prevents detection of spurious joins at the entries of any
      // irreducible child cycles.
      //
      // This conclusion about the header is true for any choice of DFS:
      //
      //   If some DFS has a reducible cycle C with header H, then for
      //   any other DFS, H is the header of a cycle C' that is a
      //   superset of C. For a divergent branch inside the subgraph
      //   C, any join node inside C is either H, or some node
      //   encountered without passing through H.
      //
      auto getReducibleParent = [&](const BlockT *Block) -> const CycleT * {
        if (!CyclePOT.isReducibleCycleHeader(Block))
          return nullptr;
        const auto *BlockCycle = CI.getCycle(Block);
        if (BlockCycle->contains(&DivTermBlock))
          return BlockCycle;
        return nullptr;
      };

      if (const auto *BlockCycle = getReducibleParent(Block)) {
        SmallVector<BlockT *, 4> BlockCycleExits;
        BlockCycle->getExitBlocks(BlockCycleExits);
        for (auto *BlockCycleExit : BlockCycleExits) {
          CausedJoin |= visitCycleExitEdge(*BlockCycleExit, *Label);
          LoweredFloorIdx =
              std::min<int>(LoweredFloorIdx, CyclePOT.getIndex(BlockCycleExit));
        }
      } else {
        for (const auto *SuccBlock : successors(Block)) {
          CausedJoin |= visitEdge(*SuccBlock, *Label);
          LoweredFloorIdx =
              std::min<int>(LoweredFloorIdx, CyclePOT.getIndex(SuccBlock));
        }
      }

      // Floor update
      if (CausedJoin) {
        // 1. Different labels pushed to successors
        FloorIdx = LoweredFloorIdx;
      } else if (FloorLabel != Label) {
        // 2. No join caused BUT we pushed a label that is different than the
        // last pushed label
        FloorIdx = LoweredFloorIdx;
        FloorLabel = Label;
      }
    }

    LLVM_DEBUG(dbgs() << "Final labeling:\n"; printDefs(dbgs()));

    // Check every cycle containing DivTermBlock for exit divergence.
    // A cycle has exit divergence if the label of an exit block does
    // not match the label of its header.
    for (const auto *Cycle = CI.getCycle(&DivTermBlock); Cycle;
         Cycle = Cycle->getParentCycle()) {
      if (Cycle->isReducible()) {
        // The exit divergence of a reducible cycle is recorded while
        // propagating labels.
        continue;
      }
      SmallVector<BlockT *> Exits;
      Cycle->getExitBlocks(Exits);
      auto *Header = Cycle->getHeader();
      auto *HeaderLabel = BlockLabels[Header];
      for (const auto *Exit : Exits) {
        if (BlockLabels[Exit] != HeaderLabel) {
          // Identified a divergent cycle exit
          DivDesc->CycleDivBlocks.insert(Exit);
          LLVM_DEBUG(dbgs() << "\tDivergent cycle exit: " << Context.print(Exit)
                            << "\n");
        }
      }
    }

    return std::move(DivDesc);
  }
};

template <typename ContextT>
typename llvm::GenericSyncDependenceAnalysis<ContextT>::DivergenceDescriptor
    llvm::GenericSyncDependenceAnalysis<ContextT>::EmptyDivergenceDesc;

template <typename ContextT>
llvm::GenericSyncDependenceAnalysis<ContextT>::GenericSyncDependenceAnalysis(
    const ContextT &Context, const DominatorTreeT &DT, const CycleInfoT &CI)
    : CyclePO(Context), DT(DT), CI(CI) {
  CyclePO.compute(CI);
}

template <typename ContextT>
auto llvm::GenericSyncDependenceAnalysis<ContextT>::getJoinBlocks(
    const BlockT *DivTermBlock) -> const DivergenceDescriptor & {
  // trivial case
  if (succ_size(DivTermBlock) <= 1) {
    return EmptyDivergenceDesc;
  }

  // already available in cache?
  auto ItCached = CachedControlDivDescs.find(DivTermBlock);
  if (ItCached != CachedControlDivDescs.end())
    return *ItCached->second;

  // compute all join points
  DivergencePropagatorT Propagator(CyclePO, DT, CI, *DivTermBlock);
  auto DivDesc = Propagator.computeJoinPoints();

  auto printBlockSet = [&](ConstBlockSet &Blocks) {
    return Printable([&](raw_ostream &Out) {
      Out << "[";
      ListSeparator LS;
      for (const auto *BB : Blocks) {
        Out << LS << CI.getSSAContext().print(BB);
      }
      Out << "]\n";
    });
  };

  LLVM_DEBUG(
      dbgs() << "\nResult (" << CI.getSSAContext().print(DivTermBlock)
             << "):\n  JoinDivBlocks: " << printBlockSet(DivDesc->JoinDivBlocks)
             << "  CycleDivBlocks: " << printBlockSet(DivDesc->CycleDivBlocks)
             << "\n");
  (void)printBlockSet;

  auto ItInserted =
      CachedControlDivDescs.try_emplace(DivTermBlock, std::move(DivDesc));
  assert(ItInserted.second);
  return *ItInserted.first->second;
}

template <typename ContextT>
void GenericUniformityAnalysisImpl<ContextT>::markDivergent(
    const InstructionT &I) {
  if (isAlwaysUniform(I))
    return;
  bool Marked = false;
  if (I.isTerminator()) {
    Marked = DivergentTermBlocks.insert(I.getParent()).second;
    if (Marked) {
      LLVM_DEBUG(dbgs() << "marked divergent term block: "
                        << Context.print(I.getParent()) << "\n");
    }
  } else {
    Marked = markDefsDivergent(I);
  }

  if (Marked)
    Worklist.push_back(&I);
}

template <typename ContextT>
bool GenericUniformityAnalysisImpl<ContextT>::markDivergent(
    ConstValueRefT Val) {
  if (DivergentValues.insert(Val).second) {
    LLVM_DEBUG(dbgs() << "marked divergent: " << Context.print(Val) << "\n");
    return true;
  }
  return false;
}

template <typename ContextT>
void GenericUniformityAnalysisImpl<ContextT>::addUniformOverride(
    const InstructionT &Instr) {
  UniformOverrides.insert(&Instr);
}

// Mark as divergent all external uses of values defined in \p DefCycle.
//
// A value V defined by a block B inside \p DefCycle may be used outside the
// cycle only if the use is a PHI in some exit block, or B dominates some exit
// block. Thus, we check uses as follows:
//
// - Check all PHIs in all exit blocks for inputs defined inside \p DefCycle.
// - For every block B inside \p DefCycle that dominates at least one exit
//   block, check all uses outside \p DefCycle.
//
// FIXME: This function does not distinguish between divergent and uniform
// exits. For each divergent exit, only the values that are live at that exit
// need to be propagated as divergent at their use outside the cycle.
template <typename ContextT>
void GenericUniformityAnalysisImpl<ContextT>::analyzeCycleExitDivergence(
    const CycleT &DefCycle) {
  SmallVector<BlockT *> Exits;
  DefCycle.getExitBlocks(Exits);
  for (auto *Exit : Exits) {
    for (auto &Phi : Exit->phis()) {
      if (usesValueFromCycle(Phi, DefCycle)) {
        markDivergent(Phi);
      }
    }
  }

  for (auto *BB : DefCycle.blocks()) {
    if (!llvm::any_of(Exits,
                     [&](BlockT *Exit) { return DT.dominates(BB, Exit); }))
      continue;
    for (auto &II : *BB) {
      propagateTemporalDivergence(II, DefCycle);
    }
  }
}

template <typename ContextT>
void GenericUniformityAnalysisImpl<ContextT>::propagateCycleExitDivergence(
    const BlockT &DivExit, const CycleT &InnerDivCycle) {
  LLVM_DEBUG(dbgs() << "\tpropCycleExitDiv " << Context.print(&DivExit)
                    << "\n");
  auto *DivCycle = &InnerDivCycle;
  auto *OuterDivCycle = DivCycle;
  auto *ExitLevelCycle = CI.getCycle(&DivExit);
  const unsigned CycleExitDepth =
      ExitLevelCycle ? ExitLevelCycle->getDepth() : 0;

  // Find outer-most cycle that does not contain \p DivExit
  while (DivCycle && DivCycle->getDepth() > CycleExitDepth) {
    LLVM_DEBUG(dbgs() << "  Found exiting cycle: "
                      << Context.print(DivCycle->getHeader()) << "\n");
    OuterDivCycle = DivCycle;
    DivCycle = DivCycle->getParentCycle();
  }
  LLVM_DEBUG(dbgs() << "\tOuter-most exiting cycle: "
                    << Context.print(OuterDivCycle->getHeader()) << "\n");

  if (!DivergentExitCycles.insert(OuterDivCycle).second)
    return;

  // Exit divergence does not matter if the cycle itself is assumed to
  // be divergent.
  for (const auto *C : AssumedDivergent) {
    if (C->contains(OuterDivCycle))
      return;
  }

  analyzeCycleExitDivergence(*OuterDivCycle);
}

template <typename ContextT>
void GenericUniformityAnalysisImpl<ContextT>::taintAndPushAllDefs(
    const BlockT &BB) {
  LLVM_DEBUG(dbgs() << "taintAndPushAllDefs " << Context.print(&BB) << "\n");
  for (const auto &I : instrs(BB)) {
    // Terminators do not produce values; they are divergent only if
    // the condition is divergent. That is handled when the divergent
    // condition is placed in the worklist.
    if (I.isTerminator())
      break;

    markDivergent(I);
  }
}

/// Mark divergent phi nodes in a join block
template <typename ContextT>
void GenericUniformityAnalysisImpl<ContextT>::taintAndPushPhiNodes(
    const BlockT &JoinBlock) {
  LLVM_DEBUG(dbgs() << "taintAndPushPhiNodes in " << Context.print(&JoinBlock)
                    << "\n");
  for (const auto &Phi : JoinBlock.phis()) {
    // FIXME: The non-undef value is not constant per se; it just happens to be
    // uniform and may not dominate this PHI. So assuming that the same value
    // reaches along all incoming edges may itself be undefined behaviour. This
    // particular interpretation of the undef value was added to
    // DivergenceAnalysis in the following review:
    //
    // https://reviews.llvm.org/D19013
    if (ContextT::isConstantOrUndefValuePhi(Phi))
      continue;
    markDivergent(Phi);
  }
}

/// Add \p Candidate to \p Cycles if it is not already contained in \p Cycles.
///
/// \return true iff \p Candidate was added to \p Cycles.
template <typename CycleT>
static bool insertIfNotContained(SmallVector<CycleT *> &Cycles,
                                 CycleT *Candidate) {
  if (llvm::any_of(Cycles,
                   [Candidate](CycleT *C) { return C->contains(Candidate); }))
    return false;
  Cycles.push_back(Candidate);
  return true;
}

/// Return the outermost cycle made divergent by branch outside it.
///
/// If two paths that diverged outside an irreducible cycle join
/// inside that cycle, then that whole cycle is assumed to be
/// divergent. This does not apply if the cycle is reducible.
template <typename CycleT, typename BlockT>
static const CycleT *getExtDivCycle(const CycleT *Cycle,
                                    const BlockT *DivTermBlock,
                                    const BlockT *JoinBlock) {
  assert(Cycle);
  assert(Cycle->contains(JoinBlock));

  if (Cycle->contains(DivTermBlock))
    return nullptr;

  const auto *OriginalCycle = Cycle;
  const auto *Parent = Cycle->getParentCycle();
  while (Parent && !Parent->contains(DivTermBlock)) {
    Cycle = Parent;
    Parent = Cycle->getParentCycle();
  }

  // If the original cycle is not the outermost cycle, then the outermost cycle
  // is irreducible. If the outermost cycle were reducible, then external
  // diverged paths would not reach the original inner cycle.
  (void)OriginalCycle;
  assert(Cycle == OriginalCycle || !Cycle->isReducible());

  if (Cycle->isReducible()) {
    assert(Cycle->getHeader() == JoinBlock);
    return nullptr;
  }

  LLVM_DEBUG(dbgs() << "cycle made divergent by external branch\n");
  return Cycle;
}

/// Return the outermost cycle made divergent by branch inside it.
///
/// This checks the "diverged entry" criterion defined in the
/// docs/ConvergenceAnalysis.html.
template <typename ContextT, typename CycleT, typename BlockT,
          typename DominatorTreeT>
static const CycleT *
getIntDivCycle(const CycleT *Cycle, const BlockT *DivTermBlock,
               const BlockT *JoinBlock, const DominatorTreeT &DT,
               ContextT &Context) {
  LLVM_DEBUG(dbgs() << "examine join " << Context.print(JoinBlock)
                    << " for internal branch " << Context.print(DivTermBlock)
                    << "\n");
  if (DT.properlyDominates(DivTermBlock, JoinBlock))
    return nullptr;

  // Find the smallest common cycle, if one exists.
  assert(Cycle && Cycle->contains(JoinBlock));
  while (Cycle && !Cycle->contains(DivTermBlock)) {
    Cycle = Cycle->getParentCycle();
  }
  if (!Cycle || Cycle->isReducible())
    return nullptr;

  if (DT.properlyDominates(Cycle->getHeader(), JoinBlock))
    return nullptr;

  LLVM_DEBUG(dbgs() << "  header " << Context.print(Cycle->getHeader())
                    << " does not dominate join\n");

  const auto *Parent = Cycle->getParentCycle();
  while (Parent && !DT.properlyDominates(Parent->getHeader(), JoinBlock)) {
    LLVM_DEBUG(dbgs() << "  header " << Context.print(Parent->getHeader())
                      << " does not dominate join\n");
    Cycle = Parent;
    Parent = Parent->getParentCycle();
  }

  LLVM_DEBUG(dbgs() << "  cycle made divergent by internal branch\n");
  return Cycle;
}

template <typename ContextT, typename CycleT, typename BlockT,
          typename DominatorTreeT>
static const CycleT *
getOutermostDivergentCycle(const CycleT *Cycle, const BlockT *DivTermBlock,
                           const BlockT *JoinBlock, const DominatorTreeT &DT,
                           ContextT &Context) {
  if (!Cycle)
    return nullptr;

  // First try to expand Cycle to the largest that contains JoinBlock
  // but not DivTermBlock.
  const auto *Ext = getExtDivCycle(Cycle, DivTermBlock, JoinBlock);

  // Continue expanding to the largest cycle that contains both.
  const auto *Int = getIntDivCycle(Cycle, DivTermBlock, JoinBlock, DT, Context);

  if (Int)
    return Int;
  return Ext;
}

template <typename ContextT>
bool GenericUniformityAnalysisImpl<ContextT>::isTemporalDivergent(
    const BlockT &ObservingBlock, const InstructionT &Def) const {
  const BlockT *DefBlock = Def.getParent();
  for (const CycleT *Cycle = CI.getCycle(DefBlock);
       Cycle && !Cycle->contains(&ObservingBlock);
       Cycle = Cycle->getParentCycle()) {
    if (DivergentExitCycles.contains(Cycle)) {
      return true;
    }
  }
  return false;
}

template <typename ContextT>
void GenericUniformityAnalysisImpl<ContextT>::analyzeControlDivergence(
    const InstructionT &Term) {
  const auto *DivTermBlock = Term.getParent();
  DivergentTermBlocks.insert(DivTermBlock);
  LLVM_DEBUG(dbgs() << "analyzeControlDiv " << Context.print(DivTermBlock)
                    << "\n");

  // Don't propagate divergence from unreachable blocks.
  if (!DT.isReachableFromEntry(DivTermBlock))
    return;

  const auto &DivDesc = SDA.getJoinBlocks(DivTermBlock);
  SmallVector<const CycleT *> DivCycles;

  // Iterate over all blocks now reachable by a disjoint path join
  for (const auto *JoinBlock : DivDesc.JoinDivBlocks) {
    const auto *Cycle = CI.getCycle(JoinBlock);
    LLVM_DEBUG(dbgs() << "visiting join block " << Context.print(JoinBlock)
                      << "\n");
    if (const auto *Outermost = getOutermostDivergentCycle(
            Cycle, DivTermBlock, JoinBlock, DT, Context)) {
      LLVM_DEBUG(dbgs() << "found divergent cycle\n");
      DivCycles.push_back(Outermost);
      continue;
    }
    taintAndPushPhiNodes(*JoinBlock);
  }

  // Sort by order of decreasing depth. This allows later cycles to be skipped
  // because they are already contained in earlier ones.
  llvm::sort(DivCycles, [](const CycleT *A, const CycleT *B) {
    return A->getDepth() > B->getDepth();
  });

  // Cycles that are assumed divergent due to the diverged entry
  // criterion potentially contain temporal divergence depending on
  // the DFS chosen. Conservatively, all values produced in such a
  // cycle are assumed divergent. "Cycle invariant" values may be
  // assumed uniform, but that requires further analysis.
  for (auto *C : DivCycles) {
    if (!insertIfNotContained(AssumedDivergent, C))
      continue;
    LLVM_DEBUG(dbgs() << "process divergent cycle\n");
    for (const BlockT *BB : C->blocks()) {
      taintAndPushAllDefs(*BB);
    }
  }

  const auto *BranchCycle = CI.getCycle(DivTermBlock);
  assert(DivDesc.CycleDivBlocks.empty() || BranchCycle);
  for (const auto *DivExitBlock : DivDesc.CycleDivBlocks) {
    propagateCycleExitDivergence(*DivExitBlock, *BranchCycle);
  }
}

template <typename ContextT>
void GenericUniformityAnalysisImpl<ContextT>::compute() {
  // Initialize worklist.
  auto DivValuesCopy = DivergentValues;
  for (const auto DivVal : DivValuesCopy) {
    assert(isDivergent(DivVal) && "Worklist invariant violated!");
    pushUsers(DivVal);
  }

  // All values on the Worklist are divergent.
  // Their users may not have been updated yet.
  while (!Worklist.empty()) {
    const InstructionT *I = Worklist.back();
    Worklist.pop_back();

    LLVM_DEBUG(dbgs() << "worklist pop: " << Context.print(I) << "\n");

    if (I->isTerminator()) {
      analyzeControlDivergence(*I);
      continue;
    }

    // propagate value divergence to users
    assert(isDivergent(*I) && "Worklist invariant violated!");
    pushUsers(*I);
  }
}

template <typename ContextT>
bool GenericUniformityAnalysisImpl<ContextT>::isAlwaysUniform(
    const InstructionT &Instr) const {
  return UniformOverrides.contains(&Instr);
}

template <typename ContextT>
GenericUniformityInfo<ContextT>::GenericUniformityInfo(
    const DominatorTreeT &DT, const CycleInfoT &CI,
    const TargetTransformInfo *TTI) {
  DA.reset(new ImplT{DT, CI, TTI});
}

template <typename ContextT>
void GenericUniformityAnalysisImpl<ContextT>::print(raw_ostream &OS) const {
  bool haveDivergentArgs = false;

  // Control flow instructions may be divergent even if their inputs are
  // uniform. Thus, although exceedingly rare, it is possible to have a program
  // with no divergent values but with divergent control structures.
  if (DivergentValues.empty() && DivergentTermBlocks.empty() &&
      DivergentExitCycles.empty()) {
    OS << "ALL VALUES UNIFORM\n";
    return;
  }

  for (const auto &entry : DivergentValues) {
    const BlockT *parent = Context.getDefBlock(entry);
    if (!parent) {
      if (!haveDivergentArgs) {
        OS << "DIVERGENT ARGUMENTS:\n";
        haveDivergentArgs = true;
      }
      OS << "  DIVERGENT: " << Context.print(entry) << '\n';
    }
  }

  if (!AssumedDivergent.empty()) {
    OS << "CYCLES ASSSUMED DIVERGENT:\n";
    for (const CycleT *cycle : AssumedDivergent) {
      OS << "  " << cycle->print(Context) << '\n';
    }
  }

  if (!DivergentExitCycles.empty()) {
    OS << "CYCLES WITH DIVERGENT EXIT:\n";
    for (const CycleT *cycle : DivergentExitCycles) {
      OS << "  " << cycle->print(Context) << '\n';
    }
  }

  for (auto &block : F) {
    OS << "\nBLOCK " << Context.print(&block) << '\n';

    OS << "DEFINITIONS\n";
    SmallVector<ConstValueRefT, 16> defs;
    Context.appendBlockDefs(defs, block);
    for (auto value : defs) {
      if (isDivergent(value))
        OS << "  DIVERGENT: ";
      else
        OS << "             ";
      OS << Context.print(value) << '\n';
    }

    OS << "TERMINATORS\n";
    SmallVector<const InstructionT *, 8> terms;
    Context.appendBlockTerms(terms, block);
    bool divergentTerminators = hasDivergentTerminator(block);
    for (auto *T : terms) {
      if (divergentTerminators)
        OS << "  DIVERGENT: ";
      else
        OS << "             ";
      OS << Context.print(T) << '\n';
    }

    OS << "END BLOCK\n";
  }
}

template <typename ContextT>
bool GenericUniformityInfo<ContextT>::hasDivergence() const {
  return DA->hasDivergence();
}

template <typename ContextT>
const typename ContextT::FunctionT &
GenericUniformityInfo<ContextT>::getFunction() const {
  return DA->getFunction();
}

/// Whether \p V is divergent at its definition.
template <typename ContextT>
bool GenericUniformityInfo<ContextT>::isDivergent(ConstValueRefT V) const {
  return DA->isDivergent(V);
}

template <typename ContextT>
bool GenericUniformityInfo<ContextT>::isDivergent(const InstructionT *I) const {
  return DA->isDivergent(*I);
}

template <typename ContextT>
bool GenericUniformityInfo<ContextT>::isDivergentUse(const UseT &U) const {
  return DA->isDivergentUse(U);
}

template <typename ContextT>
bool GenericUniformityInfo<ContextT>::hasDivergentTerminator(const BlockT &B) {
  return DA->hasDivergentTerminator(B);
}

/// \brief T helper function for printing.
template <typename ContextT>
void GenericUniformityInfo<ContextT>::print(raw_ostream &out) const {
  DA->print(out);
}

template <typename ContextT>
void llvm::ModifiedPostOrder<ContextT>::computeStackPO(
    SmallVectorImpl<const BlockT *> &Stack, const CycleInfoT &CI,
    const CycleT *Cycle, SmallPtrSetImpl<const BlockT *> &Finalized) {
  LLVM_DEBUG(dbgs() << "inside computeStackPO\n");
  while (!Stack.empty()) {
    auto *NextBB = Stack.back();
    if (Finalized.count(NextBB)) {
      Stack.pop_back();
      continue;
    }
    LLVM_DEBUG(dbgs() << "  visiting " << CI.getSSAContext().print(NextBB)
                      << "\n");
    auto *NestedCycle = CI.getCycle(NextBB);
    if (Cycle != NestedCycle && (!Cycle || Cycle->contains(NestedCycle))) {
      LLVM_DEBUG(dbgs() << "  found a cycle\n");
      while (NestedCycle->getParentCycle() != Cycle)
        NestedCycle = NestedCycle->getParentCycle();

      SmallVector<BlockT *, 3> NestedExits;
      NestedCycle->getExitBlocks(NestedExits);
      bool PushedNodes = false;
      for (auto *NestedExitBB : NestedExits) {
        LLVM_DEBUG(dbgs() << "  examine exit: "
                          << CI.getSSAContext().print(NestedExitBB) << "\n");
        if (Cycle && !Cycle->contains(NestedExitBB))
          continue;
        if (Finalized.count(NestedExitBB))
          continue;
        PushedNodes = true;
        Stack.push_back(NestedExitBB);
        LLVM_DEBUG(dbgs() << "  pushed exit: "
                          << CI.getSSAContext().print(NestedExitBB) << "\n");
      }
      if (!PushedNodes) {
        // All loop exits finalized -> finish this node
        Stack.pop_back();
        computeCyclePO(CI, NestedCycle, Finalized);
      }
      continue;
    }

    LLVM_DEBUG(dbgs() << "  no nested cycle, going into DAG\n");
    // DAG-style
    bool PushedNodes = false;
    for (auto *SuccBB : successors(NextBB)) {
      LLVM_DEBUG(dbgs() << "  examine succ: "
                        << CI.getSSAContext().print(SuccBB) << "\n");
      if (Cycle && !Cycle->contains(SuccBB))
        continue;
      if (Finalized.count(SuccBB))
        continue;
      PushedNodes = true;
      Stack.push_back(SuccBB);
      LLVM_DEBUG(dbgs() << "  pushed succ: " << CI.getSSAContext().print(SuccBB)
                        << "\n");
    }
    if (!PushedNodes) {
      // Never push nodes twice
      LLVM_DEBUG(dbgs() << "  finishing node: "
                        << CI.getSSAContext().print(NextBB) << "\n");
      Stack.pop_back();
      Finalized.insert(NextBB);
      appendBlock(*NextBB);
    }
  }
  LLVM_DEBUG(dbgs() << "exited computeStackPO\n");
}

template <typename ContextT>
void ModifiedPostOrder<ContextT>::computeCyclePO(
    const CycleInfoT &CI, const CycleT *Cycle,
    SmallPtrSetImpl<const BlockT *> &Finalized) {
  LLVM_DEBUG(dbgs() << "inside computeCyclePO\n");
  SmallVector<const BlockT *> Stack;
  auto *CycleHeader = Cycle->getHeader();

  LLVM_DEBUG(dbgs() << "  noted header: "
                    << CI.getSSAContext().print(CycleHeader) << "\n");
  assert(!Finalized.count(CycleHeader));
  Finalized.insert(CycleHeader);

  // Visit the header last
  LLVM_DEBUG(dbgs() << "  finishing header: "
                    << CI.getSSAContext().print(CycleHeader) << "\n");
  appendBlock(*CycleHeader, Cycle->isReducible());

  // Initialize with immediate successors
  for (auto *BB : successors(CycleHeader)) {
    LLVM_DEBUG(dbgs() << "  examine succ: " << CI.getSSAContext().print(BB)
                      << "\n");
    if (!Cycle->contains(BB))
      continue;
    if (BB == CycleHeader)
      continue;
    if (!Finalized.count(BB)) {
      LLVM_DEBUG(dbgs() << "  pushed succ: " << CI.getSSAContext().print(BB)
                        << "\n");
      Stack.push_back(BB);
    }
  }

  // Compute PO inside region
  computeStackPO(Stack, CI, Cycle, Finalized);

  LLVM_DEBUG(dbgs() << "exited computeCyclePO\n");
}

/// \brief Generically compute the modified post order.
template <typename ContextT>
void llvm::ModifiedPostOrder<ContextT>::compute(const CycleInfoT &CI) {
  SmallPtrSet<const BlockT *, 32> Finalized;
  SmallVector<const BlockT *> Stack;
  auto *F = CI.getFunction();
  Stack.reserve(24); // FIXME made-up number
  Stack.push_back(&F->front());
  computeStackPO(Stack, CI, nullptr, Finalized);
}

} // namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_ADT_GENERICUNIFORMITYIMPL_H
