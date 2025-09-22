//===- BranchProbabilityInfo.h - Branch Probability Analysis ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is used to evaluate branch probabilties.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BRANCHPROBABILITYINFO_H
#define LLVM_ANALYSIS_BRANCHPROBABILITYINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>

namespace llvm {

class Function;
class Loop;
class LoopInfo;
class raw_ostream;
class DominatorTree;
class PostDominatorTree;
class TargetLibraryInfo;
class Value;

/// Analysis providing branch probability information.
///
/// This is a function analysis which provides information on the relative
/// probabilities of each "edge" in the function's CFG where such an edge is
/// defined by a pair (PredBlock and an index in the successors). The
/// probability of an edge from one block is always relative to the
/// probabilities of other edges from the block. The probabilites of all edges
/// from a block sum to exactly one (100%).
/// We use a pair (PredBlock and an index in the successors) to uniquely
/// identify an edge, since we can have multiple edges from Src to Dst.
/// As an example, we can have a switch which jumps to Dst with value 0 and
/// value 10.
///
/// Process of computing branch probabilities can be logically viewed as three
/// step process:
///
///   First, if there is a profile information associated with the branch then
/// it is trivially translated to branch probabilities. There is one exception
/// from this rule though. Probabilities for edges leading to "unreachable"
/// blocks (blocks with the estimated weight not greater than
/// UNREACHABLE_WEIGHT) are evaluated according to static estimation and
/// override profile information. If no branch probabilities were calculated
/// on this step then take the next one.
///
///   Second, estimate absolute execution weights for each block based on
/// statically known information. Roots of such information are "cold",
/// "unreachable", "noreturn" and "unwind" blocks. Those blocks get their
/// weights set to BlockExecWeight::COLD, BlockExecWeight::UNREACHABLE,
/// BlockExecWeight::NORETURN and BlockExecWeight::UNWIND respectively. Then the
/// weights are propagated to the other blocks up the domination line. In
/// addition, if all successors have estimated weights set then maximum of these
/// weights assigned to the block itself (while this is not ideal heuristic in
/// theory it's simple and works reasonably well in most cases) and the process
/// repeats. Once the process of weights propagation converges branch
/// probabilities are set for all such branches that have at least one successor
/// with the weight set. Default execution weight (BlockExecWeight::DEFAULT) is
/// used for any successors which doesn't have its weight set. For loop back
/// branches we use their weights scaled by loop trip count equal to
/// 'LBH_TAKEN_WEIGHT/LBH_NOTTAKEN_WEIGHT'.
///
/// Here is a simple example demonstrating how the described algorithm works.
///
///          BB1
///         /   \
///        v     v
///      BB2     BB3
///     /   \
///    v     v
///  ColdBB  UnreachBB
///
/// Initially, ColdBB is associated with COLD_WEIGHT and UnreachBB with
/// UNREACHABLE_WEIGHT. COLD_WEIGHT is set to BB2 as maximum between its
/// successors. BB1 and BB3 has no explicit estimated weights and assumed to
/// have DEFAULT_WEIGHT. Based on assigned weights branches will have the
/// following probabilities:
/// P(BB1->BB2) = COLD_WEIGHT/(COLD_WEIGHT + DEFAULT_WEIGHT) =
///   0xffff / (0xffff + 0xfffff) = 0.0588(5.9%)
/// P(BB1->BB3) = DEFAULT_WEIGHT_WEIGHT/(COLD_WEIGHT + DEFAULT_WEIGHT) =
///          0xfffff / (0xffff + 0xfffff) = 0.941(94.1%)
/// P(BB2->ColdBB) = COLD_WEIGHT/(COLD_WEIGHT + UNREACHABLE_WEIGHT) = 1(100%)
/// P(BB2->UnreachBB) =
///   UNREACHABLE_WEIGHT/(COLD_WEIGHT+UNREACHABLE_WEIGHT) = 0(0%)
///
/// If no branch probabilities were calculated on this step then take the next
/// one.
///
///   Third, apply different kinds of local heuristics for each individual
/// branch until first match. For example probability of a pointer to be null is
/// estimated as PH_TAKEN_WEIGHT/(PH_TAKEN_WEIGHT + PH_NONTAKEN_WEIGHT). If
/// no local heuristic has been matched then branch is left with no explicit
/// probability set and assumed to have default probability.
class BranchProbabilityInfo {
public:
  BranchProbabilityInfo() = default;

  BranchProbabilityInfo(const Function &F, const LoopInfo &LI,
                        const TargetLibraryInfo *TLI = nullptr,
                        DominatorTree *DT = nullptr,
                        PostDominatorTree *PDT = nullptr) {
    calculate(F, LI, TLI, DT, PDT);
  }

  BranchProbabilityInfo(BranchProbabilityInfo &&Arg)
      : Handles(std::move(Arg.Handles)), Probs(std::move(Arg.Probs)),
        LastF(Arg.LastF),
        EstimatedBlockWeight(std::move(Arg.EstimatedBlockWeight)) {
    for (auto &Handle : Handles)
      Handle.setBPI(this);
  }

  BranchProbabilityInfo(const BranchProbabilityInfo &) = delete;
  BranchProbabilityInfo &operator=(const BranchProbabilityInfo &) = delete;

  BranchProbabilityInfo &operator=(BranchProbabilityInfo &&RHS) {
    releaseMemory();
    Handles = std::move(RHS.Handles);
    Probs = std::move(RHS.Probs);
    EstimatedBlockWeight = std::move(RHS.EstimatedBlockWeight);
    for (auto &Handle : Handles)
      Handle.setBPI(this);
    return *this;
  }

  bool invalidate(Function &, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &);

  void releaseMemory();

  void print(raw_ostream &OS) const;

  /// Get an edge's probability, relative to other out-edges of the Src.
  ///
  /// This routine provides access to the fractional probability between zero
  /// (0%) and one (100%) of this edge executing, relative to other edges
  /// leaving the 'Src' block. The returned probability is never zero, and can
  /// only be one if the source block has only one successor.
  BranchProbability getEdgeProbability(const BasicBlock *Src,
                                       unsigned IndexInSuccessors) const;

  /// Get the probability of going from Src to Dst.
  ///
  /// It returns the sum of all probabilities for edges from Src to Dst.
  BranchProbability getEdgeProbability(const BasicBlock *Src,
                                       const BasicBlock *Dst) const;

  BranchProbability getEdgeProbability(const BasicBlock *Src,
                                       const_succ_iterator Dst) const;

  /// Test if an edge is hot relative to other out-edges of the Src.
  ///
  /// Check whether this edge out of the source block is 'hot'. We define hot
  /// as having a relative probability >= 80%.
  bool isEdgeHot(const BasicBlock *Src, const BasicBlock *Dst) const;

  /// Print an edge's probability.
  ///
  /// Retrieves an edge's probability similarly to \see getEdgeProbability, but
  /// then prints that probability to the provided stream. That stream is then
  /// returned.
  raw_ostream &printEdgeProbability(raw_ostream &OS, const BasicBlock *Src,
                                    const BasicBlock *Dst) const;

public:
  /// Set the raw probabilities for all edges from the given block.
  ///
  /// This allows a pass to explicitly set edge probabilities for a block. It
  /// can be used when updating the CFG to update the branch probability
  /// information.
  void setEdgeProbability(const BasicBlock *Src,
                          const SmallVectorImpl<BranchProbability> &Probs);

  /// Copy outgoing edge probabilities from \p Src to \p Dst.
  ///
  /// This allows to keep probabilities unset for the destination if they were
  /// unset for source.
  void copyEdgeProbabilities(BasicBlock *Src, BasicBlock *Dst);

  /// Swap outgoing edges probabilities for \p Src with branch terminator
  void swapSuccEdgesProbabilities(const BasicBlock *Src);

  static BranchProbability getBranchProbStackProtector(bool IsLikely) {
    static const BranchProbability LikelyProb((1u << 20) - 1, 1u << 20);
    return IsLikely ? LikelyProb : LikelyProb.getCompl();
  }

  void calculate(const Function &F, const LoopInfo &LI,
                 const TargetLibraryInfo *TLI, DominatorTree *DT,
                 PostDominatorTree *PDT);

  /// Forget analysis results for the given basic block.
  void eraseBlock(const BasicBlock *BB);

  // Data structure to track SCCs for handling irreducible loops.
  class SccInfo {
    // Enum of types to classify basic blocks in SCC. Basic block belonging to
    // SCC is 'Inner' until it is either 'Header' or 'Exiting'. Note that a
    // basic block can be 'Header' and 'Exiting' at the same time.
    enum SccBlockType {
      Inner = 0x0,
      Header = 0x1,
      Exiting = 0x2,
    };
    // Map of basic blocks to SCC IDs they belong to. If basic block doesn't
    // belong to any SCC it is not in the map.
    using SccMap = DenseMap<const BasicBlock *, int>;
    // Each basic block in SCC is attributed with one or several types from
    // SccBlockType. Map value has uint32_t type (instead of SccBlockType)
    // since basic block may be for example "Header" and "Exiting" at the same
    // time and we need to be able to keep more than one value from
    // SccBlockType.
    using SccBlockTypeMap = DenseMap<const BasicBlock *, uint32_t>;
    // Vector containing classification of basic blocks for all  SCCs where i'th
    // vector element corresponds to SCC with ID equal to i.
    using SccBlockTypeMaps = std::vector<SccBlockTypeMap>;

    SccMap SccNums;
    SccBlockTypeMaps SccBlocks;

  public:
    explicit SccInfo(const Function &F);

    /// If \p BB belongs to some SCC then ID of that SCC is returned, otherwise
    /// -1 is returned. If \p BB belongs to more than one SCC at the same time
    /// result is undefined.
    int getSCCNum(const BasicBlock *BB) const;
    /// Returns true if \p BB is a 'header' block in SCC with \p SccNum ID,
    /// false otherwise.
    bool isSCCHeader(const BasicBlock *BB, int SccNum) const {
      return getSccBlockType(BB, SccNum) & Header;
    }
    /// Returns true if \p BB is an 'exiting' block in SCC with \p SccNum ID,
    /// false otherwise.
    bool isSCCExitingBlock(const BasicBlock *BB, int SccNum) const {
      return getSccBlockType(BB, SccNum) & Exiting;
    }
    /// Fills in \p Enters vector with all such blocks that don't belong to
    /// SCC with \p SccNum ID but there is an edge to a block belonging to the
    /// SCC.
    void getSccEnterBlocks(int SccNum,
                           SmallVectorImpl<BasicBlock *> &Enters) const;
    /// Fills in \p Exits vector with all such blocks that don't belong to
    /// SCC with \p SccNum ID but there is an edge from a block belonging to the
    /// SCC.
    void getSccExitBlocks(int SccNum,
                          SmallVectorImpl<BasicBlock *> &Exits) const;

  private:
    /// Returns \p BB's type according to classification given by SccBlockType
    /// enum. Please note that \p BB must belong to SSC with \p SccNum ID.
    uint32_t getSccBlockType(const BasicBlock *BB, int SccNum) const;
    /// Calculates \p BB's type and stores it in internal data structures for
    /// future use. Please note that \p BB must belong to SSC with \p SccNum ID.
    void calculateSccBlockType(const BasicBlock *BB, int SccNum);
  };

private:
  // We need to store CallbackVH's in order to correctly handle basic block
  // removal.
  class BasicBlockCallbackVH final : public CallbackVH {
    BranchProbabilityInfo *BPI;

    void deleted() override {
      assert(BPI != nullptr);
      BPI->eraseBlock(cast<BasicBlock>(getValPtr()));
    }

  public:
    void setBPI(BranchProbabilityInfo *BPI) { this->BPI = BPI; }

    BasicBlockCallbackVH(const Value *V, BranchProbabilityInfo *BPI = nullptr)
        : CallbackVH(const_cast<Value *>(V)), BPI(BPI) {}
  };

  /// Pair of Loop and SCC ID number. Used to unify handling of normal and
  /// SCC based loop representations.
  using LoopData = std::pair<Loop *, int>;
  /// Helper class to keep basic block along with its loop data information.
  class LoopBlock {
  public:
    explicit LoopBlock(const BasicBlock *BB, const LoopInfo &LI,
                       const SccInfo &SccI);

    const BasicBlock *getBlock() const { return BB; }
    BasicBlock *getBlock() { return const_cast<BasicBlock *>(BB); }
    LoopData getLoopData() const { return LD; }
    Loop *getLoop() const { return LD.first; }
    int getSccNum() const { return LD.second; }

    bool belongsToLoop() const { return getLoop() || getSccNum() != -1; }
    bool belongsToSameLoop(const LoopBlock &LB) const {
      return (LB.getLoop() && getLoop() == LB.getLoop()) ||
             (LB.getSccNum() != -1 && getSccNum() == LB.getSccNum());
    }

  private:
    const BasicBlock *const BB = nullptr;
    LoopData LD = {nullptr, -1};
  };

  // Pair of LoopBlocks representing an edge from first to second block.
  using LoopEdge = std::pair<const LoopBlock &, const LoopBlock &>;

  DenseSet<BasicBlockCallbackVH, DenseMapInfo<Value*>> Handles;

  // Since we allow duplicate edges from one basic block to another, we use
  // a pair (PredBlock and an index in the successors) to specify an edge.
  using Edge = std::pair<const BasicBlock *, unsigned>;

  DenseMap<Edge, BranchProbability> Probs;

  /// Track the last function we run over for printing.
  const Function *LastF = nullptr;

  const LoopInfo *LI = nullptr;

  /// Keeps information about all SCCs in a function.
  std::unique_ptr<const SccInfo> SccI;

  /// Keeps mapping of a basic block to its estimated weight.
  SmallDenseMap<const BasicBlock *, uint32_t> EstimatedBlockWeight;

  /// Keeps mapping of a loop to estimated weight to enter the loop.
  SmallDenseMap<LoopData, uint32_t> EstimatedLoopWeight;

  /// Helper to construct LoopBlock for \p BB.
  LoopBlock getLoopBlock(const BasicBlock *BB) const {
    return LoopBlock(BB, *LI, *SccI);
  }

  /// Returns true if destination block belongs to some loop and source block is
  /// either doesn't belong to any loop or belongs to a loop which is not inner
  /// relative to the destination block.
  bool isLoopEnteringEdge(const LoopEdge &Edge) const;
  /// Returns true if source block belongs to some loop and destination block is
  /// either doesn't belong to any loop or belongs to a loop which is not inner
  /// relative to the source block.
  bool isLoopExitingEdge(const LoopEdge &Edge) const;
  /// Returns true if \p Edge is either enters to or exits from some loop, false
  /// in all other cases.
  bool isLoopEnteringExitingEdge(const LoopEdge &Edge) const;
  /// Returns true if source and destination blocks belongs to the same loop and
  /// destination block is loop header.
  bool isLoopBackEdge(const LoopEdge &Edge) const;
  // Fills in \p Enters vector with all "enter" blocks to a loop \LB belongs to.
  void getLoopEnterBlocks(const LoopBlock &LB,
                          SmallVectorImpl<BasicBlock *> &Enters) const;
  // Fills in \p Exits vector with all "exit" blocks from a loop \LB belongs to.
  void getLoopExitBlocks(const LoopBlock &LB,
                         SmallVectorImpl<BasicBlock *> &Exits) const;

  /// Returns estimated weight for \p BB. std::nullopt if \p BB has no estimated
  /// weight.
  std::optional<uint32_t> getEstimatedBlockWeight(const BasicBlock *BB) const;

  /// Returns estimated weight to enter \p L. In other words it is weight of
  /// loop's header block not scaled by trip count. Returns std::nullopt if \p L
  /// has no no estimated weight.
  std::optional<uint32_t> getEstimatedLoopWeight(const LoopData &L) const;

  /// Return estimated weight for \p Edge. Returns std::nullopt if estimated
  /// weight is unknown.
  std::optional<uint32_t> getEstimatedEdgeWeight(const LoopEdge &Edge) const;

  /// Iterates over all edges leading from \p SrcBB to \p Successors and
  /// returns maximum of all estimated weights. If at least one edge has unknown
  /// estimated weight std::nullopt is returned.
  template <class IterT>
  std::optional<uint32_t>
  getMaxEstimatedEdgeWeight(const LoopBlock &SrcBB,
                            iterator_range<IterT> Successors) const;

  /// If \p LoopBB has no estimated weight then set it to \p BBWeight and
  /// return true. Otherwise \p BB's weight remains unchanged and false is
  /// returned. In addition all blocks/loops that might need their weight to be
  /// re-estimated are put into BlockWorkList/LoopWorkList.
  bool updateEstimatedBlockWeight(LoopBlock &LoopBB, uint32_t BBWeight,
                                  SmallVectorImpl<BasicBlock *> &BlockWorkList,
                                  SmallVectorImpl<LoopBlock> &LoopWorkList);

  /// Starting from \p LoopBB (including \p LoopBB itself) propagate \p BBWeight
  /// up the domination tree.
  void propagateEstimatedBlockWeight(const LoopBlock &LoopBB, DominatorTree *DT,
                                     PostDominatorTree *PDT, uint32_t BBWeight,
                                     SmallVectorImpl<BasicBlock *> &WorkList,
                                     SmallVectorImpl<LoopBlock> &LoopWorkList);

  /// Returns block's weight encoded in the IR.
  std::optional<uint32_t> getInitialEstimatedBlockWeight(const BasicBlock *BB);

  // Computes estimated weights for all blocks in \p F.
  void computeEestimateBlockWeight(const Function &F, DominatorTree *DT,
                                   PostDominatorTree *PDT);

  /// Based on computed weights by \p computeEstimatedBlockWeight set
  /// probabilities on branches.
  bool calcEstimatedHeuristics(const BasicBlock *BB);
  bool calcMetadataWeights(const BasicBlock *BB);
  bool calcPointerHeuristics(const BasicBlock *BB);
  bool calcZeroHeuristics(const BasicBlock *BB, const TargetLibraryInfo *TLI);
  bool calcFloatingPointHeuristics(const BasicBlock *BB);
};

/// Analysis pass which computes \c BranchProbabilityInfo.
class BranchProbabilityAnalysis
    : public AnalysisInfoMixin<BranchProbabilityAnalysis> {
  friend AnalysisInfoMixin<BranchProbabilityAnalysis>;

  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = BranchProbabilityInfo;

  /// Run the analysis pass over a function and produce BPI.
  BranchProbabilityInfo run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c BranchProbabilityAnalysis results.
class BranchProbabilityPrinterPass
    : public PassInfoMixin<BranchProbabilityPrinterPass> {
  raw_ostream &OS;

public:
  explicit BranchProbabilityPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

/// Legacy analysis pass which computes \c BranchProbabilityInfo.
class BranchProbabilityInfoWrapperPass : public FunctionPass {
  BranchProbabilityInfo BPI;

public:
  static char ID;

  BranchProbabilityInfoWrapperPass();

  BranchProbabilityInfo &getBPI() { return BPI; }
  const BranchProbabilityInfo &getBPI() const { return BPI; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
  void releaseMemory() override;
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_BRANCHPROBABILITYINFO_H
