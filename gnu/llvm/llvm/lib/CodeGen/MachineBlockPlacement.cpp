//===- MachineBlockPlacement.cpp - Basic Block Code Layout optimization ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements basic block placement transformations using the CFG
// structure and branch probability estimates.
//
// The pass strives to preserve the structure of the CFG (that is, retain
// a topological ordering of basic blocks) in the absence of a *strong* signal
// to the contrary from probabilities. However, within the CFG structure, it
// attempts to choose an ordering which favors placing more likely sequences of
// blocks adjacent to each other.
//
// The algorithm works from the inner-most loop within a function outward, and
// at each stage walks through the basic blocks, trying to coalesce them into
// sequential chains where allowed by the CFG (or demanded by heavy
// probabilities). Finally, it walks the blocks in topological order, and the
// first time it reaches a chain of basic blocks, it schedules them in the
// function in-order.
//
//===----------------------------------------------------------------------===//

#include "BranchFolding.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfoImpl.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/MBFIWrapper.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineSizeOpts.h"
#include "llvm/CodeGen/TailDuplicator.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/CodeLayout.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "block-placement"

STATISTIC(NumCondBranches, "Number of conditional branches");
STATISTIC(NumUncondBranches, "Number of unconditional branches");
STATISTIC(CondBranchTakenFreq,
          "Potential frequency of taking conditional branches");
STATISTIC(UncondBranchTakenFreq,
          "Potential frequency of taking unconditional branches");

static cl::opt<unsigned> AlignAllBlock(
    "align-all-blocks",
    cl::desc("Force the alignment of all blocks in the function in log2 format "
             "(e.g 4 means align on 16B boundaries)."),
    cl::init(0), cl::Hidden);

static cl::opt<unsigned> AlignAllNonFallThruBlocks(
    "align-all-nofallthru-blocks",
    cl::desc("Force the alignment of all blocks that have no fall-through "
             "predecessors (i.e. don't add nops that are executed). In log2 "
             "format (e.g 4 means align on 16B boundaries)."),
    cl::init(0), cl::Hidden);

static cl::opt<unsigned> MaxBytesForAlignmentOverride(
    "max-bytes-for-alignment",
    cl::desc("Forces the maximum bytes allowed to be emitted when padding for "
             "alignment"),
    cl::init(0), cl::Hidden);

// FIXME: Find a good default for this flag and remove the flag.
static cl::opt<unsigned> ExitBlockBias(
    "block-placement-exit-block-bias",
    cl::desc("Block frequency percentage a loop exit block needs "
             "over the original exit to be considered the new exit."),
    cl::init(0), cl::Hidden);

// Definition:
// - Outlining: placement of a basic block outside the chain or hot path.

static cl::opt<unsigned> LoopToColdBlockRatio(
    "loop-to-cold-block-ratio",
    cl::desc("Outline loop blocks from loop chain if (frequency of loop) / "
             "(frequency of block) is greater than this ratio"),
    cl::init(5), cl::Hidden);

static cl::opt<bool> ForceLoopColdBlock(
    "force-loop-cold-block",
    cl::desc("Force outlining cold blocks from loops."),
    cl::init(false), cl::Hidden);

static cl::opt<bool>
    PreciseRotationCost("precise-rotation-cost",
                        cl::desc("Model the cost of loop rotation more "
                                 "precisely by using profile data."),
                        cl::init(false), cl::Hidden);

static cl::opt<bool>
    ForcePreciseRotationCost("force-precise-rotation-cost",
                             cl::desc("Force the use of precise cost "
                                      "loop rotation strategy."),
                             cl::init(false), cl::Hidden);

static cl::opt<unsigned> MisfetchCost(
    "misfetch-cost",
    cl::desc("Cost that models the probabilistic risk of an instruction "
             "misfetch due to a jump comparing to falling through, whose cost "
             "is zero."),
    cl::init(1), cl::Hidden);

static cl::opt<unsigned> JumpInstCost("jump-inst-cost",
                                      cl::desc("Cost of jump instructions."),
                                      cl::init(1), cl::Hidden);
static cl::opt<bool>
TailDupPlacement("tail-dup-placement",
              cl::desc("Perform tail duplication during placement. "
                       "Creates more fallthrough opportunites in "
                       "outline branches."),
              cl::init(true), cl::Hidden);

static cl::opt<bool>
BranchFoldPlacement("branch-fold-placement",
              cl::desc("Perform branch folding during placement. "
                       "Reduces code size."),
              cl::init(true), cl::Hidden);

// Heuristic for tail duplication.
static cl::opt<unsigned> TailDupPlacementThreshold(
    "tail-dup-placement-threshold",
    cl::desc("Instruction cutoff for tail duplication during layout. "
             "Tail merging during layout is forced to have a threshold "
             "that won't conflict."), cl::init(2),
    cl::Hidden);

// Heuristic for aggressive tail duplication.
static cl::opt<unsigned> TailDupPlacementAggressiveThreshold(
    "tail-dup-placement-aggressive-threshold",
    cl::desc("Instruction cutoff for aggressive tail duplication during "
             "layout. Used at -O3. Tail merging during layout is forced to "
             "have a threshold that won't conflict."), cl::init(4),
    cl::Hidden);

// Heuristic for tail duplication.
static cl::opt<unsigned> TailDupPlacementPenalty(
    "tail-dup-placement-penalty",
    cl::desc("Cost penalty for blocks that can avoid breaking CFG by copying. "
             "Copying can increase fallthrough, but it also increases icache "
             "pressure. This parameter controls the penalty to account for that. "
             "Percent as integer."),
    cl::init(2),
    cl::Hidden);

// Heuristic for tail duplication if profile count is used in cost model.
static cl::opt<unsigned> TailDupProfilePercentThreshold(
    "tail-dup-profile-percent-threshold",
    cl::desc("If profile count information is used in tail duplication cost "
             "model, the gained fall through number from tail duplication "
             "should be at least this percent of hot count."),
    cl::init(50), cl::Hidden);

// Heuristic for triangle chains.
static cl::opt<unsigned> TriangleChainCount(
    "triangle-chain-count",
    cl::desc("Number of triangle-shaped-CFG's that need to be in a row for the "
             "triangle tail duplication heuristic to kick in. 0 to disable."),
    cl::init(2),
    cl::Hidden);

// Use case: When block layout is visualized after MBP pass, the basic blocks
// are labeled in layout order; meanwhile blocks could be numbered in a
// different order. It's hard to map between the graph and pass output.
// With this option on, the basic blocks are renumbered in function layout
// order. For debugging only.
static cl::opt<bool> RenumberBlocksBeforeView(
    "renumber-blocks-before-view",
    cl::desc(
        "If true, basic blocks are re-numbered before MBP layout is printed "
        "into a dot graph. Only used when a function is being printed."),
    cl::init(false), cl::Hidden);

namespace llvm {
extern cl::opt<bool> EnableExtTspBlockPlacement;
extern cl::opt<bool> ApplyExtTspWithoutProfile;
extern cl::opt<unsigned> StaticLikelyProb;
extern cl::opt<unsigned> ProfileLikelyProb;

// Internal option used to control BFI display only after MBP pass.
// Defined in CodeGen/MachineBlockFrequencyInfo.cpp:
// -view-block-layout-with-bfi=
extern cl::opt<GVDAGType> ViewBlockLayoutWithBFI;

// Command line option to specify the name of the function for CFG dump
// Defined in Analysis/BlockFrequencyInfo.cpp:  -view-bfi-func-name=
extern cl::opt<std::string> ViewBlockFreqFuncName;
} // namespace llvm

namespace {

class BlockChain;

/// Type for our function-wide basic block -> block chain mapping.
using BlockToChainMapType = DenseMap<const MachineBasicBlock *, BlockChain *>;

/// A chain of blocks which will be laid out contiguously.
///
/// This is the datastructure representing a chain of consecutive blocks that
/// are profitable to layout together in order to maximize fallthrough
/// probabilities and code locality. We also can use a block chain to represent
/// a sequence of basic blocks which have some external (correctness)
/// requirement for sequential layout.
///
/// Chains can be built around a single basic block and can be merged to grow
/// them. They participate in a block-to-chain mapping, which is updated
/// automatically as chains are merged together.
class BlockChain {
  /// The sequence of blocks belonging to this chain.
  ///
  /// This is the sequence of blocks for a particular chain. These will be laid
  /// out in-order within the function.
  SmallVector<MachineBasicBlock *, 4> Blocks;

  /// A handle to the function-wide basic block to block chain mapping.
  ///
  /// This is retained in each block chain to simplify the computation of child
  /// block chains for SCC-formation and iteration. We store the edges to child
  /// basic blocks, and map them back to their associated chains using this
  /// structure.
  BlockToChainMapType &BlockToChain;

public:
  /// Construct a new BlockChain.
  ///
  /// This builds a new block chain representing a single basic block in the
  /// function. It also registers itself as the chain that block participates
  /// in with the BlockToChain mapping.
  BlockChain(BlockToChainMapType &BlockToChain, MachineBasicBlock *BB)
      : Blocks(1, BB), BlockToChain(BlockToChain) {
    assert(BB && "Cannot create a chain with a null basic block");
    BlockToChain[BB] = this;
  }

  /// Iterator over blocks within the chain.
  using iterator = SmallVectorImpl<MachineBasicBlock *>::iterator;
  using const_iterator = SmallVectorImpl<MachineBasicBlock *>::const_iterator;

  /// Beginning of blocks within the chain.
  iterator begin() { return Blocks.begin(); }
  const_iterator begin() const { return Blocks.begin(); }

  /// End of blocks within the chain.
  iterator end() { return Blocks.end(); }
  const_iterator end() const { return Blocks.end(); }

  bool remove(MachineBasicBlock* BB) {
    for(iterator i = begin(); i != end(); ++i) {
      if (*i == BB) {
        Blocks.erase(i);
        return true;
      }
    }
    return false;
  }

  /// Merge a block chain into this one.
  ///
  /// This routine merges a block chain into this one. It takes care of forming
  /// a contiguous sequence of basic blocks, updating the edge list, and
  /// updating the block -> chain mapping. It does not free or tear down the
  /// old chain, but the old chain's block list is no longer valid.
  void merge(MachineBasicBlock *BB, BlockChain *Chain) {
    assert(BB && "Can't merge a null block.");
    assert(!Blocks.empty() && "Can't merge into an empty chain.");

    // Fast path in case we don't have a chain already.
    if (!Chain) {
      assert(!BlockToChain[BB] &&
             "Passed chain is null, but BB has entry in BlockToChain.");
      Blocks.push_back(BB);
      BlockToChain[BB] = this;
      return;
    }

    assert(BB == *Chain->begin() && "Passed BB is not head of Chain.");
    assert(Chain->begin() != Chain->end());

    // Update the incoming blocks to point to this chain, and add them to the
    // chain structure.
    for (MachineBasicBlock *ChainBB : *Chain) {
      Blocks.push_back(ChainBB);
      assert(BlockToChain[ChainBB] == Chain && "Incoming blocks not in chain.");
      BlockToChain[ChainBB] = this;
    }
  }

#ifndef NDEBUG
  /// Dump the blocks in this chain.
  LLVM_DUMP_METHOD void dump() {
    for (MachineBasicBlock *MBB : *this)
      MBB->dump();
  }
#endif // NDEBUG

  /// Count of predecessors of any block within the chain which have not
  /// yet been scheduled.  In general, we will delay scheduling this chain
  /// until those predecessors are scheduled (or we find a sufficiently good
  /// reason to override this heuristic.)  Note that when forming loop chains,
  /// blocks outside the loop are ignored and treated as if they were already
  /// scheduled.
  ///
  /// Note: This field is reinitialized multiple times - once for each loop,
  /// and then once for the function as a whole.
  unsigned UnscheduledPredecessors = 0;
};

class MachineBlockPlacement : public MachineFunctionPass {
  /// A type for a block filter set.
  using BlockFilterSet = SmallSetVector<const MachineBasicBlock *, 16>;

  /// Pair struct containing basic block and taildup profitability
  struct BlockAndTailDupResult {
    MachineBasicBlock *BB = nullptr;
    bool ShouldTailDup;
  };

  /// Triple struct containing edge weight and the edge.
  struct WeightedEdge {
    BlockFrequency Weight;
    MachineBasicBlock *Src = nullptr;
    MachineBasicBlock *Dest = nullptr;
  };

  /// work lists of blocks that are ready to be laid out
  SmallVector<MachineBasicBlock *, 16> BlockWorkList;
  SmallVector<MachineBasicBlock *, 16> EHPadWorkList;

  /// Edges that have already been computed as optimal.
  DenseMap<const MachineBasicBlock *, BlockAndTailDupResult> ComputedEdges;

  /// Machine Function
  MachineFunction *F = nullptr;

  /// A handle to the branch probability pass.
  const MachineBranchProbabilityInfo *MBPI = nullptr;

  /// A handle to the function-wide block frequency pass.
  std::unique_ptr<MBFIWrapper> MBFI;

  /// A handle to the loop info.
  MachineLoopInfo *MLI = nullptr;

  /// Preferred loop exit.
  /// Member variable for convenience. It may be removed by duplication deep
  /// in the call stack.
  MachineBasicBlock *PreferredLoopExit = nullptr;

  /// A handle to the target's instruction info.
  const TargetInstrInfo *TII = nullptr;

  /// A handle to the target's lowering info.
  const TargetLoweringBase *TLI = nullptr;

  /// A handle to the post dominator tree.
  MachinePostDominatorTree *MPDT = nullptr;

  ProfileSummaryInfo *PSI = nullptr;

  /// Duplicator used to duplicate tails during placement.
  ///
  /// Placement decisions can open up new tail duplication opportunities, but
  /// since tail duplication affects placement decisions of later blocks, it
  /// must be done inline.
  TailDuplicator TailDup;

  /// Partial tail duplication threshold.
  BlockFrequency DupThreshold;

  /// True:  use block profile count to compute tail duplication cost.
  /// False: use block frequency to compute tail duplication cost.
  bool UseProfileCount = false;

  /// Allocator and owner of BlockChain structures.
  ///
  /// We build BlockChains lazily while processing the loop structure of
  /// a function. To reduce malloc traffic, we allocate them using this
  /// slab-like allocator, and destroy them after the pass completes. An
  /// important guarantee is that this allocator produces stable pointers to
  /// the chains.
  SpecificBumpPtrAllocator<BlockChain> ChainAllocator;

  /// Function wide BasicBlock to BlockChain mapping.
  ///
  /// This mapping allows efficiently moving from any given basic block to the
  /// BlockChain it participates in, if any. We use it to, among other things,
  /// allow implicitly defining edges between chains as the existing edges
  /// between basic blocks.
  DenseMap<const MachineBasicBlock *, BlockChain *> BlockToChain;

#ifndef NDEBUG
  /// The set of basic blocks that have terminators that cannot be fully
  /// analyzed.  These basic blocks cannot be re-ordered safely by
  /// MachineBlockPlacement, and we must preserve physical layout of these
  /// blocks and their successors through the pass.
  SmallPtrSet<MachineBasicBlock *, 4> BlocksWithUnanalyzableExits;
#endif

  /// Get block profile count or frequency according to UseProfileCount.
  /// The return value is used to model tail duplication cost.
  BlockFrequency getBlockCountOrFrequency(const MachineBasicBlock *BB) {
    if (UseProfileCount) {
      auto Count = MBFI->getBlockProfileCount(BB);
      if (Count)
        return BlockFrequency(*Count);
      else
        return BlockFrequency(0);
    } else
      return MBFI->getBlockFreq(BB);
  }

  /// Scale the DupThreshold according to basic block size.
  BlockFrequency scaleThreshold(MachineBasicBlock *BB);
  void initDupThreshold();

  /// Decrease the UnscheduledPredecessors count for all blocks in chain, and
  /// if the count goes to 0, add them to the appropriate work list.
  void markChainSuccessors(
      const BlockChain &Chain, const MachineBasicBlock *LoopHeaderBB,
      const BlockFilterSet *BlockFilter = nullptr);

  /// Decrease the UnscheduledPredecessors count for a single block, and
  /// if the count goes to 0, add them to the appropriate work list.
  void markBlockSuccessors(
      const BlockChain &Chain, const MachineBasicBlock *BB,
      const MachineBasicBlock *LoopHeaderBB,
      const BlockFilterSet *BlockFilter = nullptr);

  BranchProbability
  collectViableSuccessors(
      const MachineBasicBlock *BB, const BlockChain &Chain,
      const BlockFilterSet *BlockFilter,
      SmallVector<MachineBasicBlock *, 4> &Successors);
  bool isBestSuccessor(MachineBasicBlock *BB, MachineBasicBlock *Pred,
                       BlockFilterSet *BlockFilter);
  void findDuplicateCandidates(SmallVectorImpl<MachineBasicBlock *> &Candidates,
                               MachineBasicBlock *BB,
                               BlockFilterSet *BlockFilter);
  bool repeatedlyTailDuplicateBlock(
      MachineBasicBlock *BB, MachineBasicBlock *&LPred,
      const MachineBasicBlock *LoopHeaderBB, BlockChain &Chain,
      BlockFilterSet *BlockFilter,
      MachineFunction::iterator &PrevUnplacedBlockIt,
      BlockFilterSet::iterator &PrevUnplacedBlockInFilterIt);
  bool
  maybeTailDuplicateBlock(MachineBasicBlock *BB, MachineBasicBlock *LPred,
                          BlockChain &Chain, BlockFilterSet *BlockFilter,
                          MachineFunction::iterator &PrevUnplacedBlockIt,
                          BlockFilterSet::iterator &PrevUnplacedBlockInFilterIt,
                          bool &DuplicatedToLPred);
  bool hasBetterLayoutPredecessor(
      const MachineBasicBlock *BB, const MachineBasicBlock *Succ,
      const BlockChain &SuccChain, BranchProbability SuccProb,
      BranchProbability RealSuccProb, const BlockChain &Chain,
      const BlockFilterSet *BlockFilter);
  BlockAndTailDupResult selectBestSuccessor(
      const MachineBasicBlock *BB, const BlockChain &Chain,
      const BlockFilterSet *BlockFilter);
  MachineBasicBlock *selectBestCandidateBlock(
      const BlockChain &Chain, SmallVectorImpl<MachineBasicBlock *> &WorkList);
  MachineBasicBlock *
  getFirstUnplacedBlock(const BlockChain &PlacedChain,
                        MachineFunction::iterator &PrevUnplacedBlockIt);
  MachineBasicBlock *
  getFirstUnplacedBlock(const BlockChain &PlacedChain,
                        BlockFilterSet::iterator &PrevUnplacedBlockInFilterIt,
                        const BlockFilterSet *BlockFilter);

  /// Add a basic block to the work list if it is appropriate.
  ///
  /// If the optional parameter BlockFilter is provided, only MBB
  /// present in the set will be added to the worklist. If nullptr
  /// is provided, no filtering occurs.
  void fillWorkLists(const MachineBasicBlock *MBB,
                     SmallPtrSetImpl<BlockChain *> &UpdatedPreds,
                     const BlockFilterSet *BlockFilter);

  void buildChain(const MachineBasicBlock *BB, BlockChain &Chain,
                  BlockFilterSet *BlockFilter = nullptr);
  bool canMoveBottomBlockToTop(const MachineBasicBlock *BottomBlock,
                               const MachineBasicBlock *OldTop);
  bool hasViableTopFallthrough(const MachineBasicBlock *Top,
                               const BlockFilterSet &LoopBlockSet);
  BlockFrequency TopFallThroughFreq(const MachineBasicBlock *Top,
                                    const BlockFilterSet &LoopBlockSet);
  BlockFrequency FallThroughGains(const MachineBasicBlock *NewTop,
                                  const MachineBasicBlock *OldTop,
                                  const MachineBasicBlock *ExitBB,
                                  const BlockFilterSet &LoopBlockSet);
  MachineBasicBlock *findBestLoopTopHelper(MachineBasicBlock *OldTop,
      const MachineLoop &L, const BlockFilterSet &LoopBlockSet);
  MachineBasicBlock *findBestLoopTop(
      const MachineLoop &L, const BlockFilterSet &LoopBlockSet);
  MachineBasicBlock *findBestLoopExit(
      const MachineLoop &L, const BlockFilterSet &LoopBlockSet,
      BlockFrequency &ExitFreq);
  BlockFilterSet collectLoopBlockSet(const MachineLoop &L);
  void buildLoopChains(const MachineLoop &L);
  void rotateLoop(
      BlockChain &LoopChain, const MachineBasicBlock *ExitingBB,
      BlockFrequency ExitFreq, const BlockFilterSet &LoopBlockSet);
  void rotateLoopWithProfile(
      BlockChain &LoopChain, const MachineLoop &L,
      const BlockFilterSet &LoopBlockSet);
  void buildCFGChains();
  void optimizeBranches();
  void alignBlocks();
  /// Returns true if a block should be tail-duplicated to increase fallthrough
  /// opportunities.
  bool shouldTailDuplicate(MachineBasicBlock *BB);
  /// Check the edge frequencies to see if tail duplication will increase
  /// fallthroughs.
  bool isProfitableToTailDup(
    const MachineBasicBlock *BB, const MachineBasicBlock *Succ,
    BranchProbability QProb,
    const BlockChain &Chain, const BlockFilterSet *BlockFilter);

  /// Check for a trellis layout.
  bool isTrellis(const MachineBasicBlock *BB,
                 const SmallVectorImpl<MachineBasicBlock *> &ViableSuccs,
                 const BlockChain &Chain, const BlockFilterSet *BlockFilter);

  /// Get the best successor given a trellis layout.
  BlockAndTailDupResult getBestTrellisSuccessor(
      const MachineBasicBlock *BB,
      const SmallVectorImpl<MachineBasicBlock *> &ViableSuccs,
      BranchProbability AdjustedSumProb, const BlockChain &Chain,
      const BlockFilterSet *BlockFilter);

  /// Get the best pair of non-conflicting edges.
  static std::pair<WeightedEdge, WeightedEdge> getBestNonConflictingEdges(
      const MachineBasicBlock *BB,
      MutableArrayRef<SmallVector<WeightedEdge, 8>> Edges);

  /// Returns true if a block can tail duplicate into all unplaced
  /// predecessors. Filters based on loop.
  bool canTailDuplicateUnplacedPreds(
      const MachineBasicBlock *BB, MachineBasicBlock *Succ,
      const BlockChain &Chain, const BlockFilterSet *BlockFilter);

  /// Find chains of triangles to tail-duplicate where a global analysis works,
  /// but a local analysis would not find them.
  void precomputeTriangleChains();

  /// Apply a post-processing step optimizing block placement.
  void applyExtTsp();

  /// Modify the existing block placement in the function and adjust all jumps.
  void assignBlockOrder(const std::vector<const MachineBasicBlock *> &NewOrder);

  /// Create a single CFG chain from the current block order.
  void createCFGChainExtTsp();

public:
  static char ID; // Pass identification, replacement for typeid

  MachineBlockPlacement() : MachineFunctionPass(ID) {
    initializeMachineBlockPlacementPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  bool allowTailDupPlacement() const {
    assert(F);
    return TailDupPlacement && !F->getTarget().requiresStructuredCFG();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    if (TailDupPlacement)
      AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char MachineBlockPlacement::ID = 0;

char &llvm::MachineBlockPlacementID = MachineBlockPlacement::ID;

INITIALIZE_PASS_BEGIN(MachineBlockPlacement, DEBUG_TYPE,
                      "Branch Probability Basic Block Placement", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_END(MachineBlockPlacement, DEBUG_TYPE,
                    "Branch Probability Basic Block Placement", false, false)

#ifndef NDEBUG
/// Helper to print the name of a MBB.
///
/// Only used by debug logging.
static std::string getBlockName(const MachineBasicBlock *BB) {
  std::string Result;
  raw_string_ostream OS(Result);
  OS << printMBBReference(*BB);
  OS << " ('" << BB->getName() << "')";
  OS.flush();
  return Result;
}
#endif

/// Mark a chain's successors as having one fewer preds.
///
/// When a chain is being merged into the "placed" chain, this routine will
/// quickly walk the successors of each block in the chain and mark them as
/// having one fewer active predecessor. It also adds any successors of this
/// chain which reach the zero-predecessor state to the appropriate worklist.
void MachineBlockPlacement::markChainSuccessors(
    const BlockChain &Chain, const MachineBasicBlock *LoopHeaderBB,
    const BlockFilterSet *BlockFilter) {
  // Walk all the blocks in this chain, marking their successors as having
  // a predecessor placed.
  for (MachineBasicBlock *MBB : Chain) {
    markBlockSuccessors(Chain, MBB, LoopHeaderBB, BlockFilter);
  }
}

/// Mark a single block's successors as having one fewer preds.
///
/// Under normal circumstances, this is only called by markChainSuccessors,
/// but if a block that was to be placed is completely tail-duplicated away,
/// and was duplicated into the chain end, we need to redo markBlockSuccessors
/// for just that block.
void MachineBlockPlacement::markBlockSuccessors(
    const BlockChain &Chain, const MachineBasicBlock *MBB,
    const MachineBasicBlock *LoopHeaderBB, const BlockFilterSet *BlockFilter) {
  // Add any successors for which this is the only un-placed in-loop
  // predecessor to the worklist as a viable candidate for CFG-neutral
  // placement. No subsequent placement of this block will violate the CFG
  // shape, so we get to use heuristics to choose a favorable placement.
  for (MachineBasicBlock *Succ : MBB->successors()) {
    if (BlockFilter && !BlockFilter->count(Succ))
      continue;
    BlockChain &SuccChain = *BlockToChain[Succ];
    // Disregard edges within a fixed chain, or edges to the loop header.
    if (&Chain == &SuccChain || Succ == LoopHeaderBB)
      continue;

    // This is a cross-chain edge that is within the loop, so decrement the
    // loop predecessor count of the destination chain.
    if (SuccChain.UnscheduledPredecessors == 0 ||
        --SuccChain.UnscheduledPredecessors > 0)
      continue;

    auto *NewBB = *SuccChain.begin();
    if (NewBB->isEHPad())
      EHPadWorkList.push_back(NewBB);
    else
      BlockWorkList.push_back(NewBB);
  }
}

/// This helper function collects the set of successors of block
/// \p BB that are allowed to be its layout successors, and return
/// the total branch probability of edges from \p BB to those
/// blocks.
BranchProbability MachineBlockPlacement::collectViableSuccessors(
    const MachineBasicBlock *BB, const BlockChain &Chain,
    const BlockFilterSet *BlockFilter,
    SmallVector<MachineBasicBlock *, 4> &Successors) {
  // Adjust edge probabilities by excluding edges pointing to blocks that is
  // either not in BlockFilter or is already in the current chain. Consider the
  // following CFG:
  //
  //     --->A
  //     |  / \
  //     | B   C
  //     |  \ / \
  //     ----D   E
  //
  // Assume A->C is very hot (>90%), and C->D has a 50% probability, then after
  // A->C is chosen as a fall-through, D won't be selected as a successor of C
  // due to CFG constraint (the probability of C->D is not greater than
  // HotProb to break topo-order). If we exclude E that is not in BlockFilter
  // when calculating the probability of C->D, D will be selected and we
  // will get A C D B as the layout of this loop.
  auto AdjustedSumProb = BranchProbability::getOne();
  for (MachineBasicBlock *Succ : BB->successors()) {
    bool SkipSucc = false;
    if (Succ->isEHPad() || (BlockFilter && !BlockFilter->count(Succ))) {
      SkipSucc = true;
    } else {
      BlockChain *SuccChain = BlockToChain[Succ];
      if (SuccChain == &Chain) {
        SkipSucc = true;
      } else if (Succ != *SuccChain->begin()) {
        LLVM_DEBUG(dbgs() << "    " << getBlockName(Succ)
                          << " -> Mid chain!\n");
        continue;
      }
    }
    if (SkipSucc)
      AdjustedSumProb -= MBPI->getEdgeProbability(BB, Succ);
    else
      Successors.push_back(Succ);
  }

  return AdjustedSumProb;
}

/// The helper function returns the branch probability that is adjusted
/// or normalized over the new total \p AdjustedSumProb.
static BranchProbability
getAdjustedProbability(BranchProbability OrigProb,
                       BranchProbability AdjustedSumProb) {
  BranchProbability SuccProb;
  uint32_t SuccProbN = OrigProb.getNumerator();
  uint32_t SuccProbD = AdjustedSumProb.getNumerator();
  if (SuccProbN >= SuccProbD)
    SuccProb = BranchProbability::getOne();
  else
    SuccProb = BranchProbability(SuccProbN, SuccProbD);

  return SuccProb;
}

/// Check if \p BB has exactly the successors in \p Successors.
static bool
hasSameSuccessors(MachineBasicBlock &BB,
                  SmallPtrSetImpl<const MachineBasicBlock *> &Successors) {
  if (BB.succ_size() != Successors.size())
    return false;
  // We don't want to count self-loops
  if (Successors.count(&BB))
    return false;
  for (MachineBasicBlock *Succ : BB.successors())
    if (!Successors.count(Succ))
      return false;
  return true;
}

/// Check if a block should be tail duplicated to increase fallthrough
/// opportunities.
/// \p BB Block to check.
bool MachineBlockPlacement::shouldTailDuplicate(MachineBasicBlock *BB) {
  // Blocks with single successors don't create additional fallthrough
  // opportunities. Don't duplicate them. TODO: When conditional exits are
  // analyzable, allow them to be duplicated.
  bool IsSimple = TailDup.isSimpleBB(BB);

  if (BB->succ_size() == 1)
    return false;
  return TailDup.shouldTailDuplicate(IsSimple, *BB);
}

/// Compare 2 BlockFrequency's with a small penalty for \p A.
/// In order to be conservative, we apply a X% penalty to account for
/// increased icache pressure and static heuristics. For small frequencies
/// we use only the numerators to improve accuracy. For simplicity, we assume the
/// penalty is less than 100%
/// TODO(iteratee): Use 64-bit fixed point edge frequencies everywhere.
static bool greaterWithBias(BlockFrequency A, BlockFrequency B,
                            BlockFrequency EntryFreq) {
  BranchProbability ThresholdProb(TailDupPlacementPenalty, 100);
  BlockFrequency Gain = A - B;
  return (Gain / ThresholdProb) >= EntryFreq;
}

/// Check the edge frequencies to see if tail duplication will increase
/// fallthroughs. It only makes sense to call this function when
/// \p Succ would not be chosen otherwise. Tail duplication of \p Succ is
/// always locally profitable if we would have picked \p Succ without
/// considering duplication.
bool MachineBlockPlacement::isProfitableToTailDup(
    const MachineBasicBlock *BB, const MachineBasicBlock *Succ,
    BranchProbability QProb,
    const BlockChain &Chain, const BlockFilterSet *BlockFilter) {
  // We need to do a probability calculation to make sure this is profitable.
  // First: does succ have a successor that post-dominates? This affects the
  // calculation. The 2 relevant cases are:
  //    BB         BB
  //    | \Qout    | \Qout
  //   P|  C       |P C
  //    =   C'     =   C'
  //    |  /Qin    |  /Qin
  //    | /        | /
  //    Succ       Succ
  //    / \        | \  V
  //  U/   =V      |U \
  //  /     \      =   D
  //  D      E     |  /
  //               | /
  //               |/
  //               PDom
  //  '=' : Branch taken for that CFG edge
  // In the second case, Placing Succ while duplicating it into C prevents the
  // fallthrough of Succ into either D or PDom, because they now have C as an
  // unplaced predecessor

  // Start by figuring out which case we fall into
  MachineBasicBlock *PDom = nullptr;
  SmallVector<MachineBasicBlock *, 4> SuccSuccs;
  // Only scan the relevant successors
  auto AdjustedSuccSumProb =
      collectViableSuccessors(Succ, Chain, BlockFilter, SuccSuccs);
  BranchProbability PProb = MBPI->getEdgeProbability(BB, Succ);
  auto BBFreq = MBFI->getBlockFreq(BB);
  auto SuccFreq = MBFI->getBlockFreq(Succ);
  BlockFrequency P = BBFreq * PProb;
  BlockFrequency Qout = BBFreq * QProb;
  BlockFrequency EntryFreq = MBFI->getEntryFreq();
  // If there are no more successors, it is profitable to copy, as it strictly
  // increases fallthrough.
  if (SuccSuccs.size() == 0)
    return greaterWithBias(P, Qout, EntryFreq);

  auto BestSuccSucc = BranchProbability::getZero();
  // Find the PDom or the best Succ if no PDom exists.
  for (MachineBasicBlock *SuccSucc : SuccSuccs) {
    auto Prob = MBPI->getEdgeProbability(Succ, SuccSucc);
    if (Prob > BestSuccSucc)
      BestSuccSucc = Prob;
    if (PDom == nullptr)
      if (MPDT->dominates(SuccSucc, Succ)) {
        PDom = SuccSucc;
        break;
      }
  }
  // For the comparisons, we need to know Succ's best incoming edge that isn't
  // from BB.
  auto SuccBestPred = BlockFrequency(0);
  for (MachineBasicBlock *SuccPred : Succ->predecessors()) {
    if (SuccPred == Succ || SuccPred == BB
        || BlockToChain[SuccPred] == &Chain
        || (BlockFilter && !BlockFilter->count(SuccPred)))
      continue;
    auto Freq = MBFI->getBlockFreq(SuccPred)
        * MBPI->getEdgeProbability(SuccPred, Succ);
    if (Freq > SuccBestPred)
      SuccBestPred = Freq;
  }
  // Qin is Succ's best unplaced incoming edge that isn't BB
  BlockFrequency Qin = SuccBestPred;
  // If it doesn't have a post-dominating successor, here is the calculation:
  //    BB        BB
  //    | \Qout   |  \
  //   P|  C      |   =
  //    =   C'    |    C
  //    |  /Qin   |     |
  //    | /       |     C' (+Succ)
  //    Succ      Succ /|
  //    / \       |  \/ |
  //  U/   =V     |  == |
  //  /     \     | /  \|
  //  D      E    D     E
  //  '=' : Branch taken for that CFG edge
  //  Cost in the first case is: P + V
  //  For this calculation, we always assume P > Qout. If Qout > P
  //  The result of this function will be ignored at the caller.
  //  Let F = SuccFreq - Qin
  //  Cost in the second case is: Qout + min(Qin, F) * U + max(Qin, F) * V

  if (PDom == nullptr || !Succ->isSuccessor(PDom)) {
    BranchProbability UProb = BestSuccSucc;
    BranchProbability VProb = AdjustedSuccSumProb - UProb;
    BlockFrequency F = SuccFreq - Qin;
    BlockFrequency V = SuccFreq * VProb;
    BlockFrequency QinU = std::min(Qin, F) * UProb;
    BlockFrequency BaseCost = P + V;
    BlockFrequency DupCost = Qout + QinU + std::max(Qin, F) * VProb;
    return greaterWithBias(BaseCost, DupCost, EntryFreq);
  }
  BranchProbability UProb = MBPI->getEdgeProbability(Succ, PDom);
  BranchProbability VProb = AdjustedSuccSumProb - UProb;
  BlockFrequency U = SuccFreq * UProb;
  BlockFrequency V = SuccFreq * VProb;
  BlockFrequency F = SuccFreq - Qin;
  // If there is a post-dominating successor, here is the calculation:
  // BB         BB                 BB          BB
  // | \Qout    |   \               | \Qout     |  \
  // |P C       |    =              |P C        |   =
  // =   C'     |P    C             =   C'      |P   C
  // |  /Qin    |      |            |  /Qin     |     |
  // | /        |      C' (+Succ)   | /         |     C' (+Succ)
  // Succ       Succ  /|            Succ        Succ /|
  // | \  V     |   \/ |            | \  V      |  \/ |
  // |U \       |U  /\ =?           |U =        |U /\ |
  // =   D      = =  =?|            |   D       | =  =|
  // |  /       |/     D            |  /        |/    D
  // | /        |     /             | =         |    /
  // |/         |    /              |/          |   =
  // Dom         Dom                Dom         Dom
  //  '=' : Branch taken for that CFG edge
  // The cost for taken branches in the first case is P + U
  // Let F = SuccFreq - Qin
  // The cost in the second case (assuming independence), given the layout:
  // BB, Succ, (C+Succ), D, Dom or the layout:
  // BB, Succ, D, Dom, (C+Succ)
  // is Qout + max(F, Qin) * U + min(F, Qin)
  // compare P + U vs Qout + P * U + Qin.
  //
  // The 3rd and 4th cases cover when Dom would be chosen to follow Succ.
  //
  // For the 3rd case, the cost is P + 2 * V
  // For the 4th case, the cost is Qout + min(Qin, F) * U + max(Qin, F) * V + V
  // We choose 4 over 3 when (P + V) > Qout + min(Qin, F) * U + max(Qin, F) * V
  if (UProb > AdjustedSuccSumProb / 2 &&
      !hasBetterLayoutPredecessor(Succ, PDom, *BlockToChain[PDom], UProb, UProb,
                                  Chain, BlockFilter))
    // Cases 3 & 4
    return greaterWithBias(
        (P + V), (Qout + std::max(Qin, F) * VProb + std::min(Qin, F) * UProb),
        EntryFreq);
  // Cases 1 & 2
  return greaterWithBias((P + U),
                         (Qout + std::min(Qin, F) * AdjustedSuccSumProb +
                          std::max(Qin, F) * UProb),
                         EntryFreq);
}

/// Check for a trellis layout. \p BB is the upper part of a trellis if its
/// successors form the lower part of a trellis. A successor set S forms the
/// lower part of a trellis if all of the predecessors of S are either in S or
/// have all of S as successors. We ignore trellises where BB doesn't have 2
/// successors because for fewer than 2, it's trivial, and for 3 or greater they
/// are very uncommon and complex to compute optimally. Allowing edges within S
/// is not strictly a trellis, but the same algorithm works, so we allow it.
bool MachineBlockPlacement::isTrellis(
    const MachineBasicBlock *BB,
    const SmallVectorImpl<MachineBasicBlock *> &ViableSuccs,
    const BlockChain &Chain, const BlockFilterSet *BlockFilter) {
  // Technically BB could form a trellis with branching factor higher than 2.
  // But that's extremely uncommon.
  if (BB->succ_size() != 2 || ViableSuccs.size() != 2)
    return false;

  SmallPtrSet<const MachineBasicBlock *, 2> Successors(BB->succ_begin(),
                                                       BB->succ_end());
  // To avoid reviewing the same predecessors twice.
  SmallPtrSet<const MachineBasicBlock *, 8> SeenPreds;

  for (MachineBasicBlock *Succ : ViableSuccs) {
    int PredCount = 0;
    for (auto *SuccPred : Succ->predecessors()) {
      // Allow triangle successors, but don't count them.
      if (Successors.count(SuccPred)) {
        // Make sure that it is actually a triangle.
        for (MachineBasicBlock *CheckSucc : SuccPred->successors())
          if (!Successors.count(CheckSucc))
            return false;
        continue;
      }
      const BlockChain *PredChain = BlockToChain[SuccPred];
      if (SuccPred == BB || (BlockFilter && !BlockFilter->count(SuccPred)) ||
          PredChain == &Chain || PredChain == BlockToChain[Succ])
        continue;
      ++PredCount;
      // Perform the successor check only once.
      if (!SeenPreds.insert(SuccPred).second)
        continue;
      if (!hasSameSuccessors(*SuccPred, Successors))
        return false;
    }
    // If one of the successors has only BB as a predecessor, it is not a
    // trellis.
    if (PredCount < 1)
      return false;
  }
  return true;
}

/// Pick the highest total weight pair of edges that can both be laid out.
/// The edges in \p Edges[0] are assumed to have a different destination than
/// the edges in \p Edges[1]. Simple counting shows that the best pair is either
/// the individual highest weight edges to the 2 different destinations, or in
/// case of a conflict, one of them should be replaced with a 2nd best edge.
std::pair<MachineBlockPlacement::WeightedEdge,
          MachineBlockPlacement::WeightedEdge>
MachineBlockPlacement::getBestNonConflictingEdges(
    const MachineBasicBlock *BB,
    MutableArrayRef<SmallVector<MachineBlockPlacement::WeightedEdge, 8>>
        Edges) {
  // Sort the edges, and then for each successor, find the best incoming
  // predecessor. If the best incoming predecessors aren't the same,
  // then that is clearly the best layout. If there is a conflict, one of the
  // successors will have to fallthrough from the second best predecessor. We
  // compare which combination is better overall.

  // Sort for highest frequency.
  auto Cmp = [](WeightedEdge A, WeightedEdge B) { return A.Weight > B.Weight; };

  llvm::stable_sort(Edges[0], Cmp);
  llvm::stable_sort(Edges[1], Cmp);
  auto BestA = Edges[0].begin();
  auto BestB = Edges[1].begin();
  // Arrange for the correct answer to be in BestA and BestB
  // If the 2 best edges don't conflict, the answer is already there.
  if (BestA->Src == BestB->Src) {
    // Compare the total fallthrough of (Best + Second Best) for both pairs
    auto SecondBestA = std::next(BestA);
    auto SecondBestB = std::next(BestB);
    BlockFrequency BestAScore = BestA->Weight + SecondBestB->Weight;
    BlockFrequency BestBScore = BestB->Weight + SecondBestA->Weight;
    if (BestAScore < BestBScore)
      BestA = SecondBestA;
    else
      BestB = SecondBestB;
  }
  // Arrange for the BB edge to be in BestA if it exists.
  if (BestB->Src == BB)
    std::swap(BestA, BestB);
  return std::make_pair(*BestA, *BestB);
}

/// Get the best successor from \p BB based on \p BB being part of a trellis.
/// We only handle trellises with 2 successors, so the algorithm is
/// straightforward: Find the best pair of edges that don't conflict. We find
/// the best incoming edge for each successor in the trellis. If those conflict,
/// we consider which of them should be replaced with the second best.
/// Upon return the two best edges will be in \p BestEdges. If one of the edges
/// comes from \p BB, it will be in \p BestEdges[0]
MachineBlockPlacement::BlockAndTailDupResult
MachineBlockPlacement::getBestTrellisSuccessor(
    const MachineBasicBlock *BB,
    const SmallVectorImpl<MachineBasicBlock *> &ViableSuccs,
    BranchProbability AdjustedSumProb, const BlockChain &Chain,
    const BlockFilterSet *BlockFilter) {

  BlockAndTailDupResult Result = {nullptr, false};
  SmallPtrSet<const MachineBasicBlock *, 4> Successors(BB->succ_begin(),
                                                       BB->succ_end());

  // We assume size 2 because it's common. For general n, we would have to do
  // the Hungarian algorithm, but it's not worth the complexity because more
  // than 2 successors is fairly uncommon, and a trellis even more so.
  if (Successors.size() != 2 || ViableSuccs.size() != 2)
    return Result;

  // Collect the edge frequencies of all edges that form the trellis.
  SmallVector<WeightedEdge, 8> Edges[2];
  int SuccIndex = 0;
  for (auto *Succ : ViableSuccs) {
    for (MachineBasicBlock *SuccPred : Succ->predecessors()) {
      // Skip any placed predecessors that are not BB
      if (SuccPred != BB)
        if ((BlockFilter && !BlockFilter->count(SuccPred)) ||
            BlockToChain[SuccPred] == &Chain ||
            BlockToChain[SuccPred] == BlockToChain[Succ])
          continue;
      BlockFrequency EdgeFreq = MBFI->getBlockFreq(SuccPred) *
                                MBPI->getEdgeProbability(SuccPred, Succ);
      Edges[SuccIndex].push_back({EdgeFreq, SuccPred, Succ});
    }
    ++SuccIndex;
  }

  // Pick the best combination of 2 edges from all the edges in the trellis.
  WeightedEdge BestA, BestB;
  std::tie(BestA, BestB) = getBestNonConflictingEdges(BB, Edges);

  if (BestA.Src != BB) {
    // If we have a trellis, and BB doesn't have the best fallthrough edges,
    // we shouldn't choose any successor. We've already looked and there's a
    // better fallthrough edge for all the successors.
    LLVM_DEBUG(dbgs() << "Trellis, but not one of the chosen edges.\n");
    return Result;
  }

  // Did we pick the triangle edge? If tail-duplication is profitable, do
  // that instead. Otherwise merge the triangle edge now while we know it is
  // optimal.
  if (BestA.Dest == BestB.Src) {
    // The edges are BB->Succ1->Succ2, and we're looking to see if BB->Succ2
    // would be better.
    MachineBasicBlock *Succ1 = BestA.Dest;
    MachineBasicBlock *Succ2 = BestB.Dest;
    // Check to see if tail-duplication would be profitable.
    if (allowTailDupPlacement() && shouldTailDuplicate(Succ2) &&
        canTailDuplicateUnplacedPreds(BB, Succ2, Chain, BlockFilter) &&
        isProfitableToTailDup(BB, Succ2, MBPI->getEdgeProbability(BB, Succ1),
                              Chain, BlockFilter)) {
      LLVM_DEBUG(BranchProbability Succ2Prob = getAdjustedProbability(
                     MBPI->getEdgeProbability(BB, Succ2), AdjustedSumProb);
                 dbgs() << "    Selected: " << getBlockName(Succ2)
                        << ", probability: " << Succ2Prob
                        << " (Tail Duplicate)\n");
      Result.BB = Succ2;
      Result.ShouldTailDup = true;
      return Result;
    }
  }
  // We have already computed the optimal edge for the other side of the
  // trellis.
  ComputedEdges[BestB.Src] = { BestB.Dest, false };

  auto TrellisSucc = BestA.Dest;
  LLVM_DEBUG(BranchProbability SuccProb = getAdjustedProbability(
                 MBPI->getEdgeProbability(BB, TrellisSucc), AdjustedSumProb);
             dbgs() << "    Selected: " << getBlockName(TrellisSucc)
                    << ", probability: " << SuccProb << " (Trellis)\n");
  Result.BB = TrellisSucc;
  return Result;
}

/// When the option allowTailDupPlacement() is on, this method checks if the
/// fallthrough candidate block \p Succ (of block \p BB) can be tail-duplicated
/// into all of its unplaced, unfiltered predecessors, that are not BB.
bool MachineBlockPlacement::canTailDuplicateUnplacedPreds(
    const MachineBasicBlock *BB, MachineBasicBlock *Succ,
    const BlockChain &Chain, const BlockFilterSet *BlockFilter) {
  if (!shouldTailDuplicate(Succ))
    return false;

  // The result of canTailDuplicate.
  bool Duplicate = true;
  // Number of possible duplication.
  unsigned int NumDup = 0;

  // For CFG checking.
  SmallPtrSet<const MachineBasicBlock *, 4> Successors(BB->succ_begin(),
                                                       BB->succ_end());
  for (MachineBasicBlock *Pred : Succ->predecessors()) {
    // Make sure all unplaced and unfiltered predecessors can be
    // tail-duplicated into.
    // Skip any blocks that are already placed or not in this loop.
    if (Pred == BB || (BlockFilter && !BlockFilter->count(Pred))
        || (BlockToChain[Pred] == &Chain && !Succ->succ_empty()))
      continue;
    if (!TailDup.canTailDuplicate(Succ, Pred)) {
      if (Successors.size() > 1 && hasSameSuccessors(*Pred, Successors))
        // This will result in a trellis after tail duplication, so we don't
        // need to copy Succ into this predecessor. In the presence
        // of a trellis tail duplication can continue to be profitable.
        // For example:
        // A            A
        // |\           |\
        // | \          | \
        // |  C         |  C+BB
        // | /          |  |
        // |/           |  |
        // BB    =>     BB |
        // |\           |\/|
        // | \          |/\|
        // |  D         |  D
        // | /          | /
        // |/           |/
        // Succ         Succ
        //
        // After BB was duplicated into C, the layout looks like the one on the
        // right. BB and C now have the same successors. When considering
        // whether Succ can be duplicated into all its unplaced predecessors, we
        // ignore C.
        // We can do this because C already has a profitable fallthrough, namely
        // D. TODO(iteratee): ignore sufficiently cold predecessors for
        // duplication and for this test.
        //
        // This allows trellises to be laid out in 2 separate chains
        // (A,B,Succ,...) and later (C,D,...) This is a reasonable heuristic
        // because it allows the creation of 2 fallthrough paths with links
        // between them, and we correctly identify the best layout for these
        // CFGs. We want to extend trellises that the user created in addition
        // to trellises created by tail-duplication, so we just look for the
        // CFG.
        continue;
      Duplicate = false;
      continue;
    }
    NumDup++;
  }

  // No possible duplication in current filter set.
  if (NumDup == 0)
    return false;

  // If profile information is available, findDuplicateCandidates can do more
  // precise benefit analysis.
  if (F->getFunction().hasProfileData())
    return true;

  // This is mainly for function exit BB.
  // The integrated tail duplication is really designed for increasing
  // fallthrough from predecessors from Succ to its successors. We may need
  // other machanism to handle different cases.
  if (Succ->succ_empty())
    return true;

  // Plus the already placed predecessor.
  NumDup++;

  // If the duplication candidate has more unplaced predecessors than
  // successors, the extra duplication can't bring more fallthrough.
  //
  //     Pred1 Pred2 Pred3
  //         \   |   /
  //          \  |  /
  //           \ | /
  //            Dup
  //            / \
  //           /   \
  //       Succ1  Succ2
  //
  // In this example Dup has 2 successors and 3 predecessors, duplication of Dup
  // can increase the fallthrough from Pred1 to Succ1 and from Pred2 to Succ2,
  // but the duplication into Pred3 can't increase fallthrough.
  //
  // A small number of extra duplication may not hurt too much. We need a better
  // heuristic to handle it.
  if ((NumDup > Succ->succ_size()) || !Duplicate)
    return false;

  return true;
}

/// Find chains of triangles where we believe it would be profitable to
/// tail-duplicate them all, but a local analysis would not find them.
/// There are 3 ways this can be profitable:
/// 1) The post-dominators marked 50% are actually taken 55% (This shrinks with
///    longer chains)
/// 2) The chains are statically correlated. Branch probabilities have a very
///    U-shaped distribution.
///    [http://nrs.harvard.edu/urn-3:HUL.InstRepos:24015805]
///    If the branches in a chain are likely to be from the same side of the
///    distribution as their predecessor, but are independent at runtime, this
///    transformation is profitable. (Because the cost of being wrong is a small
///    fixed cost, unlike the standard triangle layout where the cost of being
///    wrong scales with the # of triangles.)
/// 3) The chains are dynamically correlated. If the probability that a previous
///    branch was taken positively influences whether the next branch will be
///    taken
/// We believe that 2 and 3 are common enough to justify the small margin in 1.
void MachineBlockPlacement::precomputeTriangleChains() {
  struct TriangleChain {
    std::vector<MachineBasicBlock *> Edges;

    TriangleChain(MachineBasicBlock *src, MachineBasicBlock *dst)
        : Edges({src, dst}) {}

    void append(MachineBasicBlock *dst) {
      assert(getKey()->isSuccessor(dst) &&
             "Attempting to append a block that is not a successor.");
      Edges.push_back(dst);
    }

    unsigned count() const { return Edges.size() - 1; }

    MachineBasicBlock *getKey() const {
      return Edges.back();
    }
  };

  if (TriangleChainCount == 0)
    return;

  LLVM_DEBUG(dbgs() << "Pre-computing triangle chains.\n");
  // Map from last block to the chain that contains it. This allows us to extend
  // chains as we find new triangles.
  DenseMap<const MachineBasicBlock *, TriangleChain> TriangleChainMap;
  for (MachineBasicBlock &BB : *F) {
    // If BB doesn't have 2 successors, it doesn't start a triangle.
    if (BB.succ_size() != 2)
      continue;
    MachineBasicBlock *PDom = nullptr;
    for (MachineBasicBlock *Succ : BB.successors()) {
      if (!MPDT->dominates(Succ, &BB))
        continue;
      PDom = Succ;
      break;
    }
    // If BB doesn't have a post-dominating successor, it doesn't form a
    // triangle.
    if (PDom == nullptr)
      continue;
    // If PDom has a hint that it is low probability, skip this triangle.
    if (MBPI->getEdgeProbability(&BB, PDom) < BranchProbability(50, 100))
      continue;
    // If PDom isn't eligible for duplication, this isn't the kind of triangle
    // we're looking for.
    if (!shouldTailDuplicate(PDom))
      continue;
    bool CanTailDuplicate = true;
    // If PDom can't tail-duplicate into it's non-BB predecessors, then this
    // isn't the kind of triangle we're looking for.
    for (MachineBasicBlock* Pred : PDom->predecessors()) {
      if (Pred == &BB)
        continue;
      if (!TailDup.canTailDuplicate(PDom, Pred)) {
        CanTailDuplicate = false;
        break;
      }
    }
    // If we can't tail-duplicate PDom to its predecessors, then skip this
    // triangle.
    if (!CanTailDuplicate)
      continue;

    // Now we have an interesting triangle. Insert it if it's not part of an
    // existing chain.
    // Note: This cannot be replaced with a call insert() or emplace() because
    // the find key is BB, but the insert/emplace key is PDom.
    auto Found = TriangleChainMap.find(&BB);
    // If it is, remove the chain from the map, grow it, and put it back in the
    // map with the end as the new key.
    if (Found != TriangleChainMap.end()) {
      TriangleChain Chain = std::move(Found->second);
      TriangleChainMap.erase(Found);
      Chain.append(PDom);
      TriangleChainMap.insert(std::make_pair(Chain.getKey(), std::move(Chain)));
    } else {
      auto InsertResult = TriangleChainMap.try_emplace(PDom, &BB, PDom);
      assert(InsertResult.second && "Block seen twice.");
      (void)InsertResult;
    }
  }

  // Iterating over a DenseMap is safe here, because the only thing in the body
  // of the loop is inserting into another DenseMap (ComputedEdges).
  // ComputedEdges is never iterated, so this doesn't lead to non-determinism.
  for (auto &ChainPair : TriangleChainMap) {
    TriangleChain &Chain = ChainPair.second;
    // Benchmarking has shown that due to branch correlation duplicating 2 or
    // more triangles is profitable, despite the calculations assuming
    // independence.
    if (Chain.count() < TriangleChainCount)
      continue;
    MachineBasicBlock *dst = Chain.Edges.back();
    Chain.Edges.pop_back();
    for (MachineBasicBlock *src : reverse(Chain.Edges)) {
      LLVM_DEBUG(dbgs() << "Marking edge: " << getBlockName(src) << "->"
                        << getBlockName(dst)
                        << " as pre-computed based on triangles.\n");

      auto InsertResult = ComputedEdges.insert({src, {dst, true}});
      assert(InsertResult.second && "Block seen twice.");
      (void)InsertResult;

      dst = src;
    }
  }
}

// When profile is not present, return the StaticLikelyProb.
// When profile is available, we need to handle the triangle-shape CFG.
static BranchProbability getLayoutSuccessorProbThreshold(
      const MachineBasicBlock *BB) {
  if (!BB->getParent()->getFunction().hasProfileData())
    return BranchProbability(StaticLikelyProb, 100);
  if (BB->succ_size() == 2) {
    const MachineBasicBlock *Succ1 = *BB->succ_begin();
    const MachineBasicBlock *Succ2 = *(BB->succ_begin() + 1);
    if (Succ1->isSuccessor(Succ2) || Succ2->isSuccessor(Succ1)) {
      /* See case 1 below for the cost analysis. For BB->Succ to
       * be taken with smaller cost, the following needs to hold:
       *   Prob(BB->Succ) > 2 * Prob(BB->Pred)
       *   So the threshold T in the calculation below
       *   (1-T) * Prob(BB->Succ) > T * Prob(BB->Pred)
       *   So T / (1 - T) = 2, Yielding T = 2/3
       * Also adding user specified branch bias, we have
       *   T = (2/3)*(ProfileLikelyProb/50)
       *     = (2*ProfileLikelyProb)/150)
       */
      return BranchProbability(2 * ProfileLikelyProb, 150);
    }
  }
  return BranchProbability(ProfileLikelyProb, 100);
}

/// Checks to see if the layout candidate block \p Succ has a better layout
/// predecessor than \c BB. If yes, returns true.
/// \p SuccProb: The probability adjusted for only remaining blocks.
///   Only used for logging
/// \p RealSuccProb: The un-adjusted probability.
/// \p Chain: The chain that BB belongs to and Succ is being considered for.
/// \p BlockFilter: if non-null, the set of blocks that make up the loop being
///    considered
bool MachineBlockPlacement::hasBetterLayoutPredecessor(
    const MachineBasicBlock *BB, const MachineBasicBlock *Succ,
    const BlockChain &SuccChain, BranchProbability SuccProb,
    BranchProbability RealSuccProb, const BlockChain &Chain,
    const BlockFilterSet *BlockFilter) {

  // There isn't a better layout when there are no unscheduled predecessors.
  if (SuccChain.UnscheduledPredecessors == 0)
    return false;

  // There are two basic scenarios here:
  // -------------------------------------
  // Case 1: triangular shape CFG (if-then):
  //     BB
  //     | \
  //     |  \
  //     |   Pred
  //     |   /
  //     Succ
  // In this case, we are evaluating whether to select edge -> Succ, e.g.
  // set Succ as the layout successor of BB. Picking Succ as BB's
  // successor breaks the CFG constraints (FIXME: define these constraints).
  // With this layout, Pred BB
  // is forced to be outlined, so the overall cost will be cost of the
  // branch taken from BB to Pred, plus the cost of back taken branch
  // from Pred to Succ, as well as the additional cost associated
  // with the needed unconditional jump instruction from Pred To Succ.

  // The cost of the topological order layout is the taken branch cost
  // from BB to Succ, so to make BB->Succ a viable candidate, the following
  // must hold:
  //     2 * freq(BB->Pred) * taken_branch_cost + unconditional_jump_cost
  //      < freq(BB->Succ) *  taken_branch_cost.
  // Ignoring unconditional jump cost, we get
  //    freq(BB->Succ) > 2 * freq(BB->Pred), i.e.,
  //    prob(BB->Succ) > 2 * prob(BB->Pred)
  //
  // When real profile data is available, we can precisely compute the
  // probability threshold that is needed for edge BB->Succ to be considered.
  // Without profile data, the heuristic requires the branch bias to be
  // a lot larger to make sure the signal is very strong (e.g. 80% default).
  // -----------------------------------------------------------------
  // Case 2: diamond like CFG (if-then-else):
  //     S
  //    / \
  //   |   \
  //  BB    Pred
  //   \    /
  //    Succ
  //    ..
  //
  // The current block is BB and edge BB->Succ is now being evaluated.
  // Note that edge S->BB was previously already selected because
  // prob(S->BB) > prob(S->Pred).
  // At this point, 2 blocks can be placed after BB: Pred or Succ. If we
  // choose Pred, we will have a topological ordering as shown on the left
  // in the picture below. If we choose Succ, we have the solution as shown
  // on the right:
  //
  //   topo-order:
  //
  //       S-----                             ---S
  //       |    |                             |  |
  //    ---BB   |                             |  BB
  //    |       |                             |  |
  //    |  Pred--                             |  Succ--
  //    |  |                                  |       |
  //    ---Succ                               ---Pred--
  //
  // cost = freq(S->Pred) + freq(BB->Succ)    cost = 2 * freq (S->Pred)
  //      = freq(S->Pred) + freq(S->BB)
  //
  // If we have profile data (i.e, branch probabilities can be trusted), the
  // cost (number of taken branches) with layout S->BB->Succ->Pred is 2 *
  // freq(S->Pred) while the cost of topo order is freq(S->Pred) + freq(S->BB).
  // We know Prob(S->BB) > Prob(S->Pred), so freq(S->BB) > freq(S->Pred), which
  // means the cost of topological order is greater.
  // When profile data is not available, however, we need to be more
  // conservative. If the branch prediction is wrong, breaking the topo-order
  // will actually yield a layout with large cost. For this reason, we need
  // strong biased branch at block S with Prob(S->BB) in order to select
  // BB->Succ. This is equivalent to looking the CFG backward with backward
  // edge: Prob(Succ->BB) needs to >= HotProb in order to be selected (without
  // profile data).
  // --------------------------------------------------------------------------
  // Case 3: forked diamond
  //       S
  //      / \
  //     /   \
  //   BB    Pred
  //   | \   / |
  //   |  \ /  |
  //   |   X   |
  //   |  / \  |
  //   | /   \ |
  //   S1     S2
  //
  // The current block is BB and edge BB->S1 is now being evaluated.
  // As above S->BB was already selected because
  // prob(S->BB) > prob(S->Pred). Assume that prob(BB->S1) >= prob(BB->S2).
  //
  // topo-order:
  //
  //     S-------|                     ---S
  //     |       |                     |  |
  //  ---BB      |                     |  BB
  //  |          |                     |  |
  //  |  Pred----|                     |  S1----
  //  |  |                             |       |
  //  --(S1 or S2)                     ---Pred--
  //                                        |
  //                                       S2
  //
  // topo-cost = freq(S->Pred) + freq(BB->S1) + freq(BB->S2)
  //    + min(freq(Pred->S1), freq(Pred->S2))
  // Non-topo-order cost:
  // non-topo-cost = 2 * freq(S->Pred) + freq(BB->S2).
  // To be conservative, we can assume that min(freq(Pred->S1), freq(Pred->S2))
  // is 0. Then the non topo layout is better when
  // freq(S->Pred) < freq(BB->S1).
  // This is exactly what is checked below.
  // Note there are other shapes that apply (Pred may not be a single block,
  // but they all fit this general pattern.)
  BranchProbability HotProb = getLayoutSuccessorProbThreshold(BB);

  // Make sure that a hot successor doesn't have a globally more
  // important predecessor.
  BlockFrequency CandidateEdgeFreq = MBFI->getBlockFreq(BB) * RealSuccProb;
  bool BadCFGConflict = false;

  for (MachineBasicBlock *Pred : Succ->predecessors()) {
    BlockChain *PredChain = BlockToChain[Pred];
    if (Pred == Succ || PredChain == &SuccChain ||
        (BlockFilter && !BlockFilter->count(Pred)) ||
        PredChain == &Chain || Pred != *std::prev(PredChain->end()) ||
        // This check is redundant except for look ahead. This function is
        // called for lookahead by isProfitableToTailDup when BB hasn't been
        // placed yet.
        (Pred == BB))
      continue;
    // Do backward checking.
    // For all cases above, we need a backward checking to filter out edges that
    // are not 'strongly' biased.
    // BB  Pred
    //  \ /
    //  Succ
    // We select edge BB->Succ if
    //      freq(BB->Succ) > freq(Succ) * HotProb
    //      i.e. freq(BB->Succ) > freq(BB->Succ) * HotProb + freq(Pred->Succ) *
    //      HotProb
    //      i.e. freq((BB->Succ) * (1 - HotProb) > freq(Pred->Succ) * HotProb
    // Case 1 is covered too, because the first equation reduces to:
    // prob(BB->Succ) > HotProb. (freq(Succ) = freq(BB) for a triangle)
    BlockFrequency PredEdgeFreq =
        MBFI->getBlockFreq(Pred) * MBPI->getEdgeProbability(Pred, Succ);
    if (PredEdgeFreq * HotProb >= CandidateEdgeFreq * HotProb.getCompl()) {
      BadCFGConflict = true;
      break;
    }
  }

  if (BadCFGConflict) {
    LLVM_DEBUG(dbgs() << "    Not a candidate: " << getBlockName(Succ) << " -> "
                      << SuccProb << " (prob) (non-cold CFG conflict)\n");
    return true;
  }

  return false;
}

/// Select the best successor for a block.
///
/// This looks across all successors of a particular block and attempts to
/// select the "best" one to be the layout successor. It only considers direct
/// successors which also pass the block filter. It will attempt to avoid
/// breaking CFG structure, but cave and break such structures in the case of
/// very hot successor edges.
///
/// \returns The best successor block found, or null if none are viable, along
/// with a boolean indicating if tail duplication is necessary.
MachineBlockPlacement::BlockAndTailDupResult
MachineBlockPlacement::selectBestSuccessor(
    const MachineBasicBlock *BB, const BlockChain &Chain,
    const BlockFilterSet *BlockFilter) {
  const BranchProbability HotProb(StaticLikelyProb, 100);

  BlockAndTailDupResult BestSucc = { nullptr, false };
  auto BestProb = BranchProbability::getZero();

  SmallVector<MachineBasicBlock *, 4> Successors;
  auto AdjustedSumProb =
      collectViableSuccessors(BB, Chain, BlockFilter, Successors);

  LLVM_DEBUG(dbgs() << "Selecting best successor for: " << getBlockName(BB)
                    << "\n");

  // if we already precomputed the best successor for BB, return that if still
  // applicable.
  auto FoundEdge = ComputedEdges.find(BB);
  if (FoundEdge != ComputedEdges.end()) {
    MachineBasicBlock *Succ = FoundEdge->second.BB;
    ComputedEdges.erase(FoundEdge);
    BlockChain *SuccChain = BlockToChain[Succ];
    if (BB->isSuccessor(Succ) && (!BlockFilter || BlockFilter->count(Succ)) &&
        SuccChain != &Chain && Succ == *SuccChain->begin())
      return FoundEdge->second;
  }

  // if BB is part of a trellis, Use the trellis to determine the optimal
  // fallthrough edges
  if (isTrellis(BB, Successors, Chain, BlockFilter))
    return getBestTrellisSuccessor(BB, Successors, AdjustedSumProb, Chain,
                                   BlockFilter);

  // For blocks with CFG violations, we may be able to lay them out anyway with
  // tail-duplication. We keep this vector so we can perform the probability
  // calculations the minimum number of times.
  SmallVector<std::pair<BranchProbability, MachineBasicBlock *>, 4>
      DupCandidates;
  for (MachineBasicBlock *Succ : Successors) {
    auto RealSuccProb = MBPI->getEdgeProbability(BB, Succ);
    BranchProbability SuccProb =
        getAdjustedProbability(RealSuccProb, AdjustedSumProb);

    BlockChain &SuccChain = *BlockToChain[Succ];
    // Skip the edge \c BB->Succ if block \c Succ has a better layout
    // predecessor that yields lower global cost.
    if (hasBetterLayoutPredecessor(BB, Succ, SuccChain, SuccProb, RealSuccProb,
                                   Chain, BlockFilter)) {
      // If tail duplication would make Succ profitable, place it.
      if (allowTailDupPlacement() && shouldTailDuplicate(Succ))
        DupCandidates.emplace_back(SuccProb, Succ);
      continue;
    }

    LLVM_DEBUG(
        dbgs() << "    Candidate: " << getBlockName(Succ)
               << ", probability: " << SuccProb
               << (SuccChain.UnscheduledPredecessors != 0 ? " (CFG break)" : "")
               << "\n");

    if (BestSucc.BB && BestProb >= SuccProb) {
      LLVM_DEBUG(dbgs() << "    Not the best candidate, continuing\n");
      continue;
    }

    LLVM_DEBUG(dbgs() << "    Setting it as best candidate\n");
    BestSucc.BB = Succ;
    BestProb = SuccProb;
  }
  // Handle the tail duplication candidates in order of decreasing probability.
  // Stop at the first one that is profitable. Also stop if they are less
  // profitable than BestSucc. Position is important because we preserve it and
  // prefer first best match. Here we aren't comparing in order, so we capture
  // the position instead.
  llvm::stable_sort(DupCandidates,
                    [](std::tuple<BranchProbability, MachineBasicBlock *> L,
                       std::tuple<BranchProbability, MachineBasicBlock *> R) {
                      return std::get<0>(L) > std::get<0>(R);
                    });
  for (auto &Tup : DupCandidates) {
    BranchProbability DupProb;
    MachineBasicBlock *Succ;
    std::tie(DupProb, Succ) = Tup;
    if (DupProb < BestProb)
      break;
    if (canTailDuplicateUnplacedPreds(BB, Succ, Chain, BlockFilter)
        && (isProfitableToTailDup(BB, Succ, BestProb, Chain, BlockFilter))) {
      LLVM_DEBUG(dbgs() << "    Candidate: " << getBlockName(Succ)
                        << ", probability: " << DupProb
                        << " (Tail Duplicate)\n");
      BestSucc.BB = Succ;
      BestSucc.ShouldTailDup = true;
      break;
    }
  }

  if (BestSucc.BB)
    LLVM_DEBUG(dbgs() << "    Selected: " << getBlockName(BestSucc.BB) << "\n");

  return BestSucc;
}

/// Select the best block from a worklist.
///
/// This looks through the provided worklist as a list of candidate basic
/// blocks and select the most profitable one to place. The definition of
/// profitable only really makes sense in the context of a loop. This returns
/// the most frequently visited block in the worklist, which in the case of
/// a loop, is the one most desirable to be physically close to the rest of the
/// loop body in order to improve i-cache behavior.
///
/// \returns The best block found, or null if none are viable.
MachineBasicBlock *MachineBlockPlacement::selectBestCandidateBlock(
    const BlockChain &Chain, SmallVectorImpl<MachineBasicBlock *> &WorkList) {
  // Once we need to walk the worklist looking for a candidate, cleanup the
  // worklist of already placed entries.
  // FIXME: If this shows up on profiles, it could be folded (at the cost of
  // some code complexity) into the loop below.
  llvm::erase_if(WorkList, [&](MachineBasicBlock *BB) {
    return BlockToChain.lookup(BB) == &Chain;
  });

  if (WorkList.empty())
    return nullptr;

  bool IsEHPad = WorkList[0]->isEHPad();

  MachineBasicBlock *BestBlock = nullptr;
  BlockFrequency BestFreq;
  for (MachineBasicBlock *MBB : WorkList) {
    assert(MBB->isEHPad() == IsEHPad &&
           "EHPad mismatch between block and work list.");

    BlockChain &SuccChain = *BlockToChain[MBB];
    if (&SuccChain == &Chain)
      continue;

    assert(SuccChain.UnscheduledPredecessors == 0 &&
           "Found CFG-violating block");

    BlockFrequency CandidateFreq = MBFI->getBlockFreq(MBB);
    LLVM_DEBUG(dbgs() << "    " << getBlockName(MBB) << " -> "
                      << printBlockFreq(MBFI->getMBFI(), CandidateFreq)
                      << " (freq)\n");

    // For ehpad, we layout the least probable first as to avoid jumping back
    // from least probable landingpads to more probable ones.
    //
    // FIXME: Using probability is probably (!) not the best way to achieve
    // this. We should probably have a more principled approach to layout
    // cleanup code.
    //
    // The goal is to get:
    //
    //                 +--------------------------+
    //                 |                          V
    // InnerLp -> InnerCleanup    OuterLp -> OuterCleanup -> Resume
    //
    // Rather than:
    //
    //                 +-------------------------------------+
    //                 V                                     |
    // OuterLp -> OuterCleanup -> Resume     InnerLp -> InnerCleanup
    if (BestBlock && (IsEHPad ^ (BestFreq >= CandidateFreq)))
      continue;

    BestBlock = MBB;
    BestFreq = CandidateFreq;
  }

  return BestBlock;
}

/// Retrieve the first unplaced basic block in the entire function.
///
/// This routine is called when we are unable to use the CFG to walk through
/// all of the basic blocks and form a chain due to unnatural loops in the CFG.
/// We walk through the function's blocks in order, starting from the
/// LastUnplacedBlockIt. We update this iterator on each call to avoid
/// re-scanning the entire sequence on repeated calls to this routine.
MachineBasicBlock *MachineBlockPlacement::getFirstUnplacedBlock(
    const BlockChain &PlacedChain,
    MachineFunction::iterator &PrevUnplacedBlockIt) {

  for (MachineFunction::iterator I = PrevUnplacedBlockIt, E = F->end(); I != E;
       ++I) {
    if (BlockToChain[&*I] != &PlacedChain) {
      PrevUnplacedBlockIt = I;
      // Now select the head of the chain to which the unplaced block belongs
      // as the block to place. This will force the entire chain to be placed,
      // and satisfies the requirements of merging chains.
      return *BlockToChain[&*I]->begin();
    }
  }
  return nullptr;
}

/// Retrieve the first unplaced basic block among the blocks in BlockFilter.
///
/// This is similar to getFirstUnplacedBlock for the entire function, but since
/// the size of BlockFilter is typically far less than the number of blocks in
/// the entire function, iterating through the BlockFilter is more efficient.
/// When processing the entire funciton, using the version without BlockFilter
/// has a complexity of #(loops in function) * #(blocks in function), while this
/// version has a complexity of sum(#(loops in block) foreach block in function)
/// which is always smaller. For long function mostly sequential in structure,
/// the complexity is amortized to 1 * #(blocks in function).
MachineBasicBlock *MachineBlockPlacement::getFirstUnplacedBlock(
    const BlockChain &PlacedChain,
    BlockFilterSet::iterator &PrevUnplacedBlockInFilterIt,
    const BlockFilterSet *BlockFilter) {
  assert(BlockFilter);
  for (; PrevUnplacedBlockInFilterIt != BlockFilter->end();
       ++PrevUnplacedBlockInFilterIt) {
    BlockChain *C = BlockToChain[*PrevUnplacedBlockInFilterIt];
    if (C != &PlacedChain) {
      return *C->begin();
    }
  }
  return nullptr;
}

void MachineBlockPlacement::fillWorkLists(
    const MachineBasicBlock *MBB,
    SmallPtrSetImpl<BlockChain *> &UpdatedPreds,
    const BlockFilterSet *BlockFilter = nullptr) {
  BlockChain &Chain = *BlockToChain[MBB];
  if (!UpdatedPreds.insert(&Chain).second)
    return;

  assert(
      Chain.UnscheduledPredecessors == 0 &&
      "Attempting to place block with unscheduled predecessors in worklist.");
  for (MachineBasicBlock *ChainBB : Chain) {
    assert(BlockToChain[ChainBB] == &Chain &&
           "Block in chain doesn't match BlockToChain map.");
    for (MachineBasicBlock *Pred : ChainBB->predecessors()) {
      if (BlockFilter && !BlockFilter->count(Pred))
        continue;
      if (BlockToChain[Pred] == &Chain)
        continue;
      ++Chain.UnscheduledPredecessors;
    }
  }

  if (Chain.UnscheduledPredecessors != 0)
    return;

  MachineBasicBlock *BB = *Chain.begin();
  if (BB->isEHPad())
    EHPadWorkList.push_back(BB);
  else
    BlockWorkList.push_back(BB);
}

void MachineBlockPlacement::buildChain(
    const MachineBasicBlock *HeadBB, BlockChain &Chain,
    BlockFilterSet *BlockFilter) {
  assert(HeadBB && "BB must not be null.\n");
  assert(BlockToChain[HeadBB] == &Chain && "BlockToChainMap mis-match.\n");
  MachineFunction::iterator PrevUnplacedBlockIt = F->begin();
  BlockFilterSet::iterator PrevUnplacedBlockInFilterIt;
  if (BlockFilter)
    PrevUnplacedBlockInFilterIt = BlockFilter->begin();

  const MachineBasicBlock *LoopHeaderBB = HeadBB;
  markChainSuccessors(Chain, LoopHeaderBB, BlockFilter);
  MachineBasicBlock *BB = *std::prev(Chain.end());
  while (true) {
    assert(BB && "null block found at end of chain in loop.");
    assert(BlockToChain[BB] == &Chain && "BlockToChainMap mis-match in loop.");
    assert(*std::prev(Chain.end()) == BB && "BB Not found at end of chain.");


    // Look for the best viable successor if there is one to place immediately
    // after this block.
    auto Result = selectBestSuccessor(BB, Chain, BlockFilter);
    MachineBasicBlock* BestSucc = Result.BB;
    bool ShouldTailDup = Result.ShouldTailDup;
    if (allowTailDupPlacement())
      ShouldTailDup |= (BestSucc && canTailDuplicateUnplacedPreds(BB, BestSucc,
                                                                  Chain,
                                                                  BlockFilter));

    // If an immediate successor isn't available, look for the best viable
    // block among those we've identified as not violating the loop's CFG at
    // this point. This won't be a fallthrough, but it will increase locality.
    if (!BestSucc)
      BestSucc = selectBestCandidateBlock(Chain, BlockWorkList);
    if (!BestSucc)
      BestSucc = selectBestCandidateBlock(Chain, EHPadWorkList);

    if (!BestSucc) {
      if (BlockFilter)
        BestSucc = getFirstUnplacedBlock(Chain, PrevUnplacedBlockInFilterIt,
                                         BlockFilter);
      else
        BestSucc = getFirstUnplacedBlock(Chain, PrevUnplacedBlockIt);
      if (!BestSucc)
        break;

      LLVM_DEBUG(dbgs() << "Unnatural loop CFG detected, forcibly merging the "
                           "layout successor until the CFG reduces\n");
    }

    // Placement may have changed tail duplication opportunities.
    // Check for that now.
    if (allowTailDupPlacement() && BestSucc && ShouldTailDup) {
      repeatedlyTailDuplicateBlock(BestSucc, BB, LoopHeaderBB, Chain,
                                   BlockFilter, PrevUnplacedBlockIt,
                                   PrevUnplacedBlockInFilterIt);
      // If the chosen successor was duplicated into BB, don't bother laying
      // it out, just go round the loop again with BB as the chain end.
      if (!BB->isSuccessor(BestSucc))
        continue;
    }

    // Place this block, updating the datastructures to reflect its placement.
    BlockChain &SuccChain = *BlockToChain[BestSucc];
    // Zero out UnscheduledPredecessors for the successor we're about to merge in case
    // we selected a successor that didn't fit naturally into the CFG.
    SuccChain.UnscheduledPredecessors = 0;
    LLVM_DEBUG(dbgs() << "Merging from " << getBlockName(BB) << " to "
                      << getBlockName(BestSucc) << "\n");
    markChainSuccessors(SuccChain, LoopHeaderBB, BlockFilter);
    Chain.merge(BestSucc, &SuccChain);
    BB = *std::prev(Chain.end());
  }

  LLVM_DEBUG(dbgs() << "Finished forming chain for header block "
                    << getBlockName(*Chain.begin()) << "\n");
}

// If bottom of block BB has only one successor OldTop, in most cases it is
// profitable to move it before OldTop, except the following case:
//
//     -->OldTop<-
//     |    .    |
//     |    .    |
//     |    .    |
//     ---Pred   |
//          |    |
//         BB-----
//
// If BB is moved before OldTop, Pred needs a taken branch to BB, and it can't
// layout the other successor below it, so it can't reduce taken branch.
// In this case we keep its original layout.
bool
MachineBlockPlacement::canMoveBottomBlockToTop(
    const MachineBasicBlock *BottomBlock,
    const MachineBasicBlock *OldTop) {
  if (BottomBlock->pred_size() != 1)
    return true;
  MachineBasicBlock *Pred = *BottomBlock->pred_begin();
  if (Pred->succ_size() != 2)
    return true;

  MachineBasicBlock *OtherBB = *Pred->succ_begin();
  if (OtherBB == BottomBlock)
    OtherBB = *Pred->succ_rbegin();
  if (OtherBB == OldTop)
    return false;

  return true;
}

// Find out the possible fall through frequence to the top of a loop.
BlockFrequency
MachineBlockPlacement::TopFallThroughFreq(
    const MachineBasicBlock *Top,
    const BlockFilterSet &LoopBlockSet) {
  BlockFrequency MaxFreq = BlockFrequency(0);
  for (MachineBasicBlock *Pred : Top->predecessors()) {
    BlockChain *PredChain = BlockToChain[Pred];
    if (!LoopBlockSet.count(Pred) &&
        (!PredChain || Pred == *std::prev(PredChain->end()))) {
      // Found a Pred block can be placed before Top.
      // Check if Top is the best successor of Pred.
      auto TopProb = MBPI->getEdgeProbability(Pred, Top);
      bool TopOK = true;
      for (MachineBasicBlock *Succ : Pred->successors()) {
        auto SuccProb = MBPI->getEdgeProbability(Pred, Succ);
        BlockChain *SuccChain = BlockToChain[Succ];
        // Check if Succ can be placed after Pred.
        // Succ should not be in any chain, or it is the head of some chain.
        if (!LoopBlockSet.count(Succ) && (SuccProb > TopProb) &&
            (!SuccChain || Succ == *SuccChain->begin())) {
          TopOK = false;
          break;
        }
      }
      if (TopOK) {
        BlockFrequency EdgeFreq = MBFI->getBlockFreq(Pred) *
                                  MBPI->getEdgeProbability(Pred, Top);
        if (EdgeFreq > MaxFreq)
          MaxFreq = EdgeFreq;
      }
    }
  }
  return MaxFreq;
}

// Compute the fall through gains when move NewTop before OldTop.
//
// In following diagram, edges marked as "-" are reduced fallthrough, edges
// marked as "+" are increased fallthrough, this function computes
//
//      SUM(increased fallthrough) - SUM(decreased fallthrough)
//
//              |
//              | -
//              V
//        --->OldTop
//        |     .
//        |     .
//       +|     .    +
//        |   Pred --->
//        |     |-
//        |     V
//        --- NewTop <---
//              |-
//              V
//
BlockFrequency
MachineBlockPlacement::FallThroughGains(
    const MachineBasicBlock *NewTop,
    const MachineBasicBlock *OldTop,
    const MachineBasicBlock *ExitBB,
    const BlockFilterSet &LoopBlockSet) {
  BlockFrequency FallThrough2Top = TopFallThroughFreq(OldTop, LoopBlockSet);
  BlockFrequency FallThrough2Exit = BlockFrequency(0);
  if (ExitBB)
    FallThrough2Exit = MBFI->getBlockFreq(NewTop) *
        MBPI->getEdgeProbability(NewTop, ExitBB);
  BlockFrequency BackEdgeFreq = MBFI->getBlockFreq(NewTop) *
      MBPI->getEdgeProbability(NewTop, OldTop);

  // Find the best Pred of NewTop.
  MachineBasicBlock *BestPred = nullptr;
  BlockFrequency FallThroughFromPred = BlockFrequency(0);
  for (MachineBasicBlock *Pred : NewTop->predecessors()) {
    if (!LoopBlockSet.count(Pred))
      continue;
    BlockChain *PredChain = BlockToChain[Pred];
    if (!PredChain || Pred == *std::prev(PredChain->end())) {
      BlockFrequency EdgeFreq =
          MBFI->getBlockFreq(Pred) * MBPI->getEdgeProbability(Pred, NewTop);
      if (EdgeFreq > FallThroughFromPred) {
        FallThroughFromPred = EdgeFreq;
        BestPred = Pred;
      }
    }
  }

  // If NewTop is not placed after Pred, another successor can be placed
  // after Pred.
  BlockFrequency NewFreq = BlockFrequency(0);
  if (BestPred) {
    for (MachineBasicBlock *Succ : BestPred->successors()) {
      if ((Succ == NewTop) || (Succ == BestPred) || !LoopBlockSet.count(Succ))
        continue;
      if (ComputedEdges.contains(Succ))
        continue;
      BlockChain *SuccChain = BlockToChain[Succ];
      if ((SuccChain && (Succ != *SuccChain->begin())) ||
          (SuccChain == BlockToChain[BestPred]))
        continue;
      BlockFrequency EdgeFreq = MBFI->getBlockFreq(BestPred) *
                                MBPI->getEdgeProbability(BestPred, Succ);
      if (EdgeFreq > NewFreq)
        NewFreq = EdgeFreq;
    }
    BlockFrequency OrigEdgeFreq = MBFI->getBlockFreq(BestPred) *
                                  MBPI->getEdgeProbability(BestPred, NewTop);
    if (NewFreq > OrigEdgeFreq) {
      // If NewTop is not the best successor of Pred, then Pred doesn't
      // fallthrough to NewTop. So there is no FallThroughFromPred and
      // NewFreq.
      NewFreq = BlockFrequency(0);
      FallThroughFromPred = BlockFrequency(0);
    }
  }

  BlockFrequency Result = BlockFrequency(0);
  BlockFrequency Gains = BackEdgeFreq + NewFreq;
  BlockFrequency Lost =
      FallThrough2Top + FallThrough2Exit + FallThroughFromPred;
  if (Gains > Lost)
    Result = Gains - Lost;
  return Result;
}

/// Helper function of findBestLoopTop. Find the best loop top block
/// from predecessors of old top.
///
/// Look for a block which is strictly better than the old top for laying
/// out before the old top of the loop. This looks for only two patterns:
///
///     1. a block has only one successor, the old loop top
///
///        Because such a block will always result in an unconditional jump,
///        rotating it in front of the old top is always profitable.
///
///     2. a block has two successors, one is old top, another is exit
///        and it has more than one predecessors
///
///        If it is below one of its predecessors P, only P can fall through to
///        it, all other predecessors need a jump to it, and another conditional
///        jump to loop header. If it is moved before loop header, all its
///        predecessors jump to it, then fall through to loop header. So all its
///        predecessors except P can reduce one taken branch.
///        At the same time, move it before old top increases the taken branch
///        to loop exit block, so the reduced taken branch will be compared with
///        the increased taken branch to the loop exit block.
MachineBasicBlock *
MachineBlockPlacement::findBestLoopTopHelper(
    MachineBasicBlock *OldTop,
    const MachineLoop &L,
    const BlockFilterSet &LoopBlockSet) {
  // Check that the header hasn't been fused with a preheader block due to
  // crazy branches. If it has, we need to start with the header at the top to
  // prevent pulling the preheader into the loop body.
  BlockChain &HeaderChain = *BlockToChain[OldTop];
  if (!LoopBlockSet.count(*HeaderChain.begin()))
    return OldTop;
  if (OldTop != *HeaderChain.begin())
    return OldTop;

  LLVM_DEBUG(dbgs() << "Finding best loop top for: " << getBlockName(OldTop)
                    << "\n");

  BlockFrequency BestGains = BlockFrequency(0);
  MachineBasicBlock *BestPred = nullptr;
  for (MachineBasicBlock *Pred : OldTop->predecessors()) {
    if (!LoopBlockSet.count(Pred))
      continue;
    if (Pred == L.getHeader())
      continue;
    LLVM_DEBUG(dbgs() << "   old top pred: " << getBlockName(Pred) << ", has "
                      << Pred->succ_size() << " successors, "
                      << printBlockFreq(MBFI->getMBFI(), *Pred) << " freq\n");
    if (Pred->succ_size() > 2)
      continue;

    MachineBasicBlock *OtherBB = nullptr;
    if (Pred->succ_size() == 2) {
      OtherBB = *Pred->succ_begin();
      if (OtherBB == OldTop)
        OtherBB = *Pred->succ_rbegin();
    }

    if (!canMoveBottomBlockToTop(Pred, OldTop))
      continue;

    BlockFrequency Gains = FallThroughGains(Pred, OldTop, OtherBB,
                                            LoopBlockSet);
    if ((Gains > BlockFrequency(0)) &&
        (Gains > BestGains ||
         ((Gains == BestGains) && Pred->isLayoutSuccessor(OldTop)))) {
      BestPred = Pred;
      BestGains = Gains;
    }
  }

  // If no direct predecessor is fine, just use the loop header.
  if (!BestPred) {
    LLVM_DEBUG(dbgs() << "    final top unchanged\n");
    return OldTop;
  }

  // Walk backwards through any straight line of predecessors.
  while (BestPred->pred_size() == 1 &&
         (*BestPred->pred_begin())->succ_size() == 1 &&
         *BestPred->pred_begin() != L.getHeader())
    BestPred = *BestPred->pred_begin();

  LLVM_DEBUG(dbgs() << "    final top: " << getBlockName(BestPred) << "\n");
  return BestPred;
}

/// Find the best loop top block for layout.
///
/// This function iteratively calls findBestLoopTopHelper, until no new better
/// BB can be found.
MachineBasicBlock *
MachineBlockPlacement::findBestLoopTop(const MachineLoop &L,
                                       const BlockFilterSet &LoopBlockSet) {
  // Placing the latch block before the header may introduce an extra branch
  // that skips this block the first time the loop is executed, which we want
  // to avoid when optimising for size.
  // FIXME: in theory there is a case that does not introduce a new branch,
  // i.e. when the layout predecessor does not fallthrough to the loop header.
  // In practice this never happens though: there always seems to be a preheader
  // that can fallthrough and that is also placed before the header.
  bool OptForSize = F->getFunction().hasOptSize() ||
                    llvm::shouldOptimizeForSize(L.getHeader(), PSI, MBFI.get());
  if (OptForSize)
    return L.getHeader();

  MachineBasicBlock *OldTop = nullptr;
  MachineBasicBlock *NewTop = L.getHeader();
  while (NewTop != OldTop) {
    OldTop = NewTop;
    NewTop = findBestLoopTopHelper(OldTop, L, LoopBlockSet);
    if (NewTop != OldTop)
      ComputedEdges[NewTop] = { OldTop, false };
  }
  return NewTop;
}

/// Find the best loop exiting block for layout.
///
/// This routine implements the logic to analyze the loop looking for the best
/// block to layout at the top of the loop. Typically this is done to maximize
/// fallthrough opportunities.
MachineBasicBlock *
MachineBlockPlacement::findBestLoopExit(const MachineLoop &L,
                                        const BlockFilterSet &LoopBlockSet,
                                        BlockFrequency &ExitFreq) {
  // We don't want to layout the loop linearly in all cases. If the loop header
  // is just a normal basic block in the loop, we want to look for what block
  // within the loop is the best one to layout at the top. However, if the loop
  // header has be pre-merged into a chain due to predecessors not having
  // analyzable branches, *and* the predecessor it is merged with is *not* part
  // of the loop, rotating the header into the middle of the loop will create
  // a non-contiguous range of blocks which is Very Bad. So start with the
  // header and only rotate if safe.
  BlockChain &HeaderChain = *BlockToChain[L.getHeader()];
  if (!LoopBlockSet.count(*HeaderChain.begin()))
    return nullptr;

  BlockFrequency BestExitEdgeFreq;
  unsigned BestExitLoopDepth = 0;
  MachineBasicBlock *ExitingBB = nullptr;
  // If there are exits to outer loops, loop rotation can severely limit
  // fallthrough opportunities unless it selects such an exit. Keep a set of
  // blocks where rotating to exit with that block will reach an outer loop.
  SmallPtrSet<MachineBasicBlock *, 4> BlocksExitingToOuterLoop;

  LLVM_DEBUG(dbgs() << "Finding best loop exit for: "
                    << getBlockName(L.getHeader()) << "\n");
  for (MachineBasicBlock *MBB : L.getBlocks()) {
    BlockChain &Chain = *BlockToChain[MBB];
    // Ensure that this block is at the end of a chain; otherwise it could be
    // mid-way through an inner loop or a successor of an unanalyzable branch.
    if (MBB != *std::prev(Chain.end()))
      continue;

    // Now walk the successors. We need to establish whether this has a viable
    // exiting successor and whether it has a viable non-exiting successor.
    // We store the old exiting state and restore it if a viable looping
    // successor isn't found.
    MachineBasicBlock *OldExitingBB = ExitingBB;
    BlockFrequency OldBestExitEdgeFreq = BestExitEdgeFreq;
    bool HasLoopingSucc = false;
    for (MachineBasicBlock *Succ : MBB->successors()) {
      if (Succ->isEHPad())
        continue;
      if (Succ == MBB)
        continue;
      BlockChain &SuccChain = *BlockToChain[Succ];
      // Don't split chains, either this chain or the successor's chain.
      if (&Chain == &SuccChain) {
        LLVM_DEBUG(dbgs() << "    exiting: " << getBlockName(MBB) << " -> "
                          << getBlockName(Succ) << " (chain conflict)\n");
        continue;
      }

      auto SuccProb = MBPI->getEdgeProbability(MBB, Succ);
      if (LoopBlockSet.count(Succ)) {
        LLVM_DEBUG(dbgs() << "    looping: " << getBlockName(MBB) << " -> "
                          << getBlockName(Succ) << " (" << SuccProb << ")\n");
        HasLoopingSucc = true;
        continue;
      }

      unsigned SuccLoopDepth = 0;
      if (MachineLoop *ExitLoop = MLI->getLoopFor(Succ)) {
        SuccLoopDepth = ExitLoop->getLoopDepth();
        if (ExitLoop->contains(&L))
          BlocksExitingToOuterLoop.insert(MBB);
      }

      BlockFrequency ExitEdgeFreq = MBFI->getBlockFreq(MBB) * SuccProb;
      LLVM_DEBUG(
          dbgs() << "    exiting: " << getBlockName(MBB) << " -> "
                 << getBlockName(Succ) << " [L:" << SuccLoopDepth << "] ("
                 << printBlockFreq(MBFI->getMBFI(), ExitEdgeFreq) << ")\n");
      // Note that we bias this toward an existing layout successor to retain
      // incoming order in the absence of better information. The exit must have
      // a frequency higher than the current exit before we consider breaking
      // the layout.
      BranchProbability Bias(100 - ExitBlockBias, 100);
      if (!ExitingBB || SuccLoopDepth > BestExitLoopDepth ||
          ExitEdgeFreq > BestExitEdgeFreq ||
          (MBB->isLayoutSuccessor(Succ) &&
           !(ExitEdgeFreq < BestExitEdgeFreq * Bias))) {
        BestExitEdgeFreq = ExitEdgeFreq;
        ExitingBB = MBB;
      }
    }

    if (!HasLoopingSucc) {
      // Restore the old exiting state, no viable looping successor was found.
      ExitingBB = OldExitingBB;
      BestExitEdgeFreq = OldBestExitEdgeFreq;
    }
  }
  // Without a candidate exiting block or with only a single block in the
  // loop, just use the loop header to layout the loop.
  if (!ExitingBB) {
    LLVM_DEBUG(
        dbgs() << "    No other candidate exit blocks, using loop header\n");
    return nullptr;
  }
  if (L.getNumBlocks() == 1) {
    LLVM_DEBUG(dbgs() << "    Loop has 1 block, using loop header as exit\n");
    return nullptr;
  }

  // Also, if we have exit blocks which lead to outer loops but didn't select
  // one of them as the exiting block we are rotating toward, disable loop
  // rotation altogether.
  if (!BlocksExitingToOuterLoop.empty() &&
      !BlocksExitingToOuterLoop.count(ExitingBB))
    return nullptr;

  LLVM_DEBUG(dbgs() << "  Best exiting block: " << getBlockName(ExitingBB)
                    << "\n");
  ExitFreq = BestExitEdgeFreq;
  return ExitingBB;
}

/// Check if there is a fallthrough to loop header Top.
///
///   1. Look for a Pred that can be layout before Top.
///   2. Check if Top is the most possible successor of Pred.
bool
MachineBlockPlacement::hasViableTopFallthrough(
    const MachineBasicBlock *Top,
    const BlockFilterSet &LoopBlockSet) {
  for (MachineBasicBlock *Pred : Top->predecessors()) {
    BlockChain *PredChain = BlockToChain[Pred];
    if (!LoopBlockSet.count(Pred) &&
        (!PredChain || Pred == *std::prev(PredChain->end()))) {
      // Found a Pred block can be placed before Top.
      // Check if Top is the best successor of Pred.
      auto TopProb = MBPI->getEdgeProbability(Pred, Top);
      bool TopOK = true;
      for (MachineBasicBlock *Succ : Pred->successors()) {
        auto SuccProb = MBPI->getEdgeProbability(Pred, Succ);
        BlockChain *SuccChain = BlockToChain[Succ];
        // Check if Succ can be placed after Pred.
        // Succ should not be in any chain, or it is the head of some chain.
        if ((!SuccChain || Succ == *SuccChain->begin()) && SuccProb > TopProb) {
          TopOK = false;
          break;
        }
      }
      if (TopOK)
        return true;
    }
  }
  return false;
}

/// Attempt to rotate an exiting block to the bottom of the loop.
///
/// Once we have built a chain, try to rotate it to line up the hot exit block
/// with fallthrough out of the loop if doing so doesn't introduce unnecessary
/// branches. For example, if the loop has fallthrough into its header and out
/// of its bottom already, don't rotate it.
void MachineBlockPlacement::rotateLoop(BlockChain &LoopChain,
                                       const MachineBasicBlock *ExitingBB,
                                       BlockFrequency ExitFreq,
                                       const BlockFilterSet &LoopBlockSet) {
  if (!ExitingBB)
    return;

  MachineBasicBlock *Top = *LoopChain.begin();
  MachineBasicBlock *Bottom = *std::prev(LoopChain.end());

  // If ExitingBB is already the last one in a chain then nothing to do.
  if (Bottom == ExitingBB)
    return;

  // The entry block should always be the first BB in a function.
  if (Top->isEntryBlock())
    return;

  bool ViableTopFallthrough = hasViableTopFallthrough(Top, LoopBlockSet);

  // If the header has viable fallthrough, check whether the current loop
  // bottom is a viable exiting block. If so, bail out as rotating will
  // introduce an unnecessary branch.
  if (ViableTopFallthrough) {
    for (MachineBasicBlock *Succ : Bottom->successors()) {
      BlockChain *SuccChain = BlockToChain[Succ];
      if (!LoopBlockSet.count(Succ) &&
          (!SuccChain || Succ == *SuccChain->begin()))
        return;
    }

    // Rotate will destroy the top fallthrough, we need to ensure the new exit
    // frequency is larger than top fallthrough.
    BlockFrequency FallThrough2Top = TopFallThroughFreq(Top, LoopBlockSet);
    if (FallThrough2Top >= ExitFreq)
      return;
  }

  BlockChain::iterator ExitIt = llvm::find(LoopChain, ExitingBB);
  if (ExitIt == LoopChain.end())
    return;

  // Rotating a loop exit to the bottom when there is a fallthrough to top
  // trades the entry fallthrough for an exit fallthrough.
  // If there is no bottom->top edge, but the chosen exit block does have
  // a fallthrough, we break that fallthrough for nothing in return.

  // Let's consider an example. We have a built chain of basic blocks
  // B1, B2, ..., Bn, where Bk is a ExitingBB - chosen exit block.
  // By doing a rotation we get
  // Bk+1, ..., Bn, B1, ..., Bk
  // Break of fallthrough to B1 is compensated by a fallthrough from Bk.
  // If we had a fallthrough Bk -> Bk+1 it is broken now.
  // It might be compensated by fallthrough Bn -> B1.
  // So we have a condition to avoid creation of extra branch by loop rotation.
  // All below must be true to avoid loop rotation:
  //   If there is a fallthrough to top (B1)
  //   There was fallthrough from chosen exit block (Bk) to next one (Bk+1)
  //   There is no fallthrough from bottom (Bn) to top (B1).
  // Please note that there is no exit fallthrough from Bn because we checked it
  // above.
  if (ViableTopFallthrough) {
    assert(std::next(ExitIt) != LoopChain.end() &&
           "Exit should not be last BB");
    MachineBasicBlock *NextBlockInChain = *std::next(ExitIt);
    if (ExitingBB->isSuccessor(NextBlockInChain))
      if (!Bottom->isSuccessor(Top))
        return;
  }

  LLVM_DEBUG(dbgs() << "Rotating loop to put exit " << getBlockName(ExitingBB)
                    << " at bottom\n");
  std::rotate(LoopChain.begin(), std::next(ExitIt), LoopChain.end());
}

/// Attempt to rotate a loop based on profile data to reduce branch cost.
///
/// With profile data, we can determine the cost in terms of missed fall through
/// opportunities when rotating a loop chain and select the best rotation.
/// Basically, there are three kinds of cost to consider for each rotation:
///    1. The possibly missed fall through edge (if it exists) from BB out of
///    the loop to the loop header.
///    2. The possibly missed fall through edges (if they exist) from the loop
///    exits to BB out of the loop.
///    3. The missed fall through edge (if it exists) from the last BB to the
///    first BB in the loop chain.
///  Therefore, the cost for a given rotation is the sum of costs listed above.
///  We select the best rotation with the smallest cost.
void MachineBlockPlacement::rotateLoopWithProfile(
    BlockChain &LoopChain, const MachineLoop &L,
    const BlockFilterSet &LoopBlockSet) {
  auto RotationPos = LoopChain.end();
  MachineBasicBlock *ChainHeaderBB = *LoopChain.begin();

  // The entry block should always be the first BB in a function.
  if (ChainHeaderBB->isEntryBlock())
    return;

  BlockFrequency SmallestRotationCost = BlockFrequency::max();

  // A utility lambda that scales up a block frequency by dividing it by a
  // branch probability which is the reciprocal of the scale.
  auto ScaleBlockFrequency = [](BlockFrequency Freq,
                                unsigned Scale) -> BlockFrequency {
    if (Scale == 0)
      return BlockFrequency(0);
    // Use operator / between BlockFrequency and BranchProbability to implement
    // saturating multiplication.
    return Freq / BranchProbability(1, Scale);
  };

  // Compute the cost of the missed fall-through edge to the loop header if the
  // chain head is not the loop header. As we only consider natural loops with
  // single header, this computation can be done only once.
  BlockFrequency HeaderFallThroughCost(0);
  for (auto *Pred : ChainHeaderBB->predecessors()) {
    BlockChain *PredChain = BlockToChain[Pred];
    if (!LoopBlockSet.count(Pred) &&
        (!PredChain || Pred == *std::prev(PredChain->end()))) {
      auto EdgeFreq = MBFI->getBlockFreq(Pred) *
          MBPI->getEdgeProbability(Pred, ChainHeaderBB);
      auto FallThruCost = ScaleBlockFrequency(EdgeFreq, MisfetchCost);
      // If the predecessor has only an unconditional jump to the header, we
      // need to consider the cost of this jump.
      if (Pred->succ_size() == 1)
        FallThruCost += ScaleBlockFrequency(EdgeFreq, JumpInstCost);
      HeaderFallThroughCost = std::max(HeaderFallThroughCost, FallThruCost);
    }
  }

  // Here we collect all exit blocks in the loop, and for each exit we find out
  // its hottest exit edge. For each loop rotation, we define the loop exit cost
  // as the sum of frequencies of exit edges we collect here, excluding the exit
  // edge from the tail of the loop chain.
  SmallVector<std::pair<MachineBasicBlock *, BlockFrequency>, 4> ExitsWithFreq;
  for (auto *BB : LoopChain) {
    auto LargestExitEdgeProb = BranchProbability::getZero();
    for (auto *Succ : BB->successors()) {
      BlockChain *SuccChain = BlockToChain[Succ];
      if (!LoopBlockSet.count(Succ) &&
          (!SuccChain || Succ == *SuccChain->begin())) {
        auto SuccProb = MBPI->getEdgeProbability(BB, Succ);
        LargestExitEdgeProb = std::max(LargestExitEdgeProb, SuccProb);
      }
    }
    if (LargestExitEdgeProb > BranchProbability::getZero()) {
      auto ExitFreq = MBFI->getBlockFreq(BB) * LargestExitEdgeProb;
      ExitsWithFreq.emplace_back(BB, ExitFreq);
    }
  }

  // In this loop we iterate every block in the loop chain and calculate the
  // cost assuming the block is the head of the loop chain. When the loop ends,
  // we should have found the best candidate as the loop chain's head.
  for (auto Iter = LoopChain.begin(), TailIter = std::prev(LoopChain.end()),
            EndIter = LoopChain.end();
       Iter != EndIter; Iter++, TailIter++) {
    // TailIter is used to track the tail of the loop chain if the block we are
    // checking (pointed by Iter) is the head of the chain.
    if (TailIter == LoopChain.end())
      TailIter = LoopChain.begin();

    auto TailBB = *TailIter;

    // Calculate the cost by putting this BB to the top.
    BlockFrequency Cost = BlockFrequency(0);

    // If the current BB is the loop header, we need to take into account the
    // cost of the missed fall through edge from outside of the loop to the
    // header.
    if (Iter != LoopChain.begin())
      Cost += HeaderFallThroughCost;

    // Collect the loop exit cost by summing up frequencies of all exit edges
    // except the one from the chain tail.
    for (auto &ExitWithFreq : ExitsWithFreq)
      if (TailBB != ExitWithFreq.first)
        Cost += ExitWithFreq.second;

    // The cost of breaking the once fall-through edge from the tail to the top
    // of the loop chain. Here we need to consider three cases:
    // 1. If the tail node has only one successor, then we will get an
    //    additional jmp instruction. So the cost here is (MisfetchCost +
    //    JumpInstCost) * tail node frequency.
    // 2. If the tail node has two successors, then we may still get an
    //    additional jmp instruction if the layout successor after the loop
    //    chain is not its CFG successor. Note that the more frequently executed
    //    jmp instruction will be put ahead of the other one. Assume the
    //    frequency of those two branches are x and y, where x is the frequency
    //    of the edge to the chain head, then the cost will be
    //    (x * MisfetechCost + min(x, y) * JumpInstCost) * tail node frequency.
    // 3. If the tail node has more than two successors (this rarely happens),
    //    we won't consider any additional cost.
    if (TailBB->isSuccessor(*Iter)) {
      auto TailBBFreq = MBFI->getBlockFreq(TailBB);
      if (TailBB->succ_size() == 1)
        Cost += ScaleBlockFrequency(TailBBFreq, MisfetchCost + JumpInstCost);
      else if (TailBB->succ_size() == 2) {
        auto TailToHeadProb = MBPI->getEdgeProbability(TailBB, *Iter);
        auto TailToHeadFreq = TailBBFreq * TailToHeadProb;
        auto ColderEdgeFreq = TailToHeadProb > BranchProbability(1, 2)
                                  ? TailBBFreq * TailToHeadProb.getCompl()
                                  : TailToHeadFreq;
        Cost += ScaleBlockFrequency(TailToHeadFreq, MisfetchCost) +
                ScaleBlockFrequency(ColderEdgeFreq, JumpInstCost);
      }
    }

    LLVM_DEBUG(dbgs() << "The cost of loop rotation by making "
                      << getBlockName(*Iter) << " to the top: "
                      << printBlockFreq(MBFI->getMBFI(), Cost) << "\n");

    if (Cost < SmallestRotationCost) {
      SmallestRotationCost = Cost;
      RotationPos = Iter;
    }
  }

  if (RotationPos != LoopChain.end()) {
    LLVM_DEBUG(dbgs() << "Rotate loop by making " << getBlockName(*RotationPos)
                      << " to the top\n");
    std::rotate(LoopChain.begin(), RotationPos, LoopChain.end());
  }
}

/// Collect blocks in the given loop that are to be placed.
///
/// When profile data is available, exclude cold blocks from the returned set;
/// otherwise, collect all blocks in the loop.
MachineBlockPlacement::BlockFilterSet
MachineBlockPlacement::collectLoopBlockSet(const MachineLoop &L) {
  BlockFilterSet LoopBlockSet;

  // Filter cold blocks off from LoopBlockSet when profile data is available.
  // Collect the sum of frequencies of incoming edges to the loop header from
  // outside. If we treat the loop as a super block, this is the frequency of
  // the loop. Then for each block in the loop, we calculate the ratio between
  // its frequency and the frequency of the loop block. When it is too small,
  // don't add it to the loop chain. If there are outer loops, then this block
  // will be merged into the first outer loop chain for which this block is not
  // cold anymore. This needs precise profile data and we only do this when
  // profile data is available.
  if (F->getFunction().hasProfileData() || ForceLoopColdBlock) {
    BlockFrequency LoopFreq(0);
    for (auto *LoopPred : L.getHeader()->predecessors())
      if (!L.contains(LoopPred))
        LoopFreq += MBFI->getBlockFreq(LoopPred) *
                    MBPI->getEdgeProbability(LoopPred, L.getHeader());

    for (MachineBasicBlock *LoopBB : L.getBlocks()) {
      if (LoopBlockSet.count(LoopBB))
        continue;
      auto Freq = MBFI->getBlockFreq(LoopBB).getFrequency();
      if (Freq == 0 || LoopFreq.getFrequency() / Freq > LoopToColdBlockRatio)
        continue;
      BlockChain *Chain = BlockToChain[LoopBB];
      for (MachineBasicBlock *ChainBB : *Chain)
        LoopBlockSet.insert(ChainBB);
    }
  } else
    LoopBlockSet.insert(L.block_begin(), L.block_end());

  return LoopBlockSet;
}

/// Forms basic block chains from the natural loop structures.
///
/// These chains are designed to preserve the existing *structure* of the code
/// as much as possible. We can then stitch the chains together in a way which
/// both preserves the topological structure and minimizes taken conditional
/// branches.
void MachineBlockPlacement::buildLoopChains(const MachineLoop &L) {
  // First recurse through any nested loops, building chains for those inner
  // loops.
  for (const MachineLoop *InnerLoop : L)
    buildLoopChains(*InnerLoop);

  assert(BlockWorkList.empty() &&
         "BlockWorkList not empty when starting to build loop chains.");
  assert(EHPadWorkList.empty() &&
         "EHPadWorkList not empty when starting to build loop chains.");
  BlockFilterSet LoopBlockSet = collectLoopBlockSet(L);

  // Check if we have profile data for this function. If yes, we will rotate
  // this loop by modeling costs more precisely which requires the profile data
  // for better layout.
  bool RotateLoopWithProfile =
      ForcePreciseRotationCost ||
      (PreciseRotationCost && F->getFunction().hasProfileData());

  // First check to see if there is an obviously preferable top block for the
  // loop. This will default to the header, but may end up as one of the
  // predecessors to the header if there is one which will result in strictly
  // fewer branches in the loop body.
  MachineBasicBlock *LoopTop = findBestLoopTop(L, LoopBlockSet);

  // If we selected just the header for the loop top, look for a potentially
  // profitable exit block in the event that rotating the loop can eliminate
  // branches by placing an exit edge at the bottom.
  //
  // Loops are processed innermost to uttermost, make sure we clear
  // PreferredLoopExit before processing a new loop.
  PreferredLoopExit = nullptr;
  BlockFrequency ExitFreq;
  if (!RotateLoopWithProfile && LoopTop == L.getHeader())
    PreferredLoopExit = findBestLoopExit(L, LoopBlockSet, ExitFreq);

  BlockChain &LoopChain = *BlockToChain[LoopTop];

  // FIXME: This is a really lame way of walking the chains in the loop: we
  // walk the blocks, and use a set to prevent visiting a particular chain
  // twice.
  SmallPtrSet<BlockChain *, 4> UpdatedPreds;
  assert(LoopChain.UnscheduledPredecessors == 0 &&
         "LoopChain should not have unscheduled predecessors.");
  UpdatedPreds.insert(&LoopChain);

  for (const MachineBasicBlock *LoopBB : LoopBlockSet)
    fillWorkLists(LoopBB, UpdatedPreds, &LoopBlockSet);

  buildChain(LoopTop, LoopChain, &LoopBlockSet);

  if (RotateLoopWithProfile)
    rotateLoopWithProfile(LoopChain, L, LoopBlockSet);
  else
    rotateLoop(LoopChain, PreferredLoopExit, ExitFreq, LoopBlockSet);

  LLVM_DEBUG({
    // Crash at the end so we get all of the debugging output first.
    bool BadLoop = false;
    if (LoopChain.UnscheduledPredecessors) {
      BadLoop = true;
      dbgs() << "Loop chain contains a block without its preds placed!\n"
             << "  Loop header:  " << getBlockName(*L.block_begin()) << "\n"
             << "  Chain header: " << getBlockName(*LoopChain.begin()) << "\n";
    }
    for (MachineBasicBlock *ChainBB : LoopChain) {
      dbgs() << "          ... " << getBlockName(ChainBB) << "\n";
      if (!LoopBlockSet.remove(ChainBB)) {
        // We don't mark the loop as bad here because there are real situations
        // where this can occur. For example, with an unanalyzable fallthrough
        // from a loop block to a non-loop block or vice versa.
        dbgs() << "Loop chain contains a block not contained by the loop!\n"
               << "  Loop header:  " << getBlockName(*L.block_begin()) << "\n"
               << "  Chain header: " << getBlockName(*LoopChain.begin()) << "\n"
               << "  Bad block:    " << getBlockName(ChainBB) << "\n";
      }
    }

    if (!LoopBlockSet.empty()) {
      BadLoop = true;
      for (const MachineBasicBlock *LoopBB : LoopBlockSet)
        dbgs() << "Loop contains blocks never placed into a chain!\n"
               << "  Loop header:  " << getBlockName(*L.block_begin()) << "\n"
               << "  Chain header: " << getBlockName(*LoopChain.begin()) << "\n"
               << "  Bad block:    " << getBlockName(LoopBB) << "\n";
    }
    assert(!BadLoop && "Detected problems with the placement of this loop.");
  });

  BlockWorkList.clear();
  EHPadWorkList.clear();
}

void MachineBlockPlacement::buildCFGChains() {
  // Ensure that every BB in the function has an associated chain to simplify
  // the assumptions of the remaining algorithm.
  SmallVector<MachineOperand, 4> Cond; // For analyzeBranch.
  for (MachineFunction::iterator FI = F->begin(), FE = F->end(); FI != FE;
       ++FI) {
    MachineBasicBlock *BB = &*FI;
    BlockChain *Chain =
        new (ChainAllocator.Allocate()) BlockChain(BlockToChain, BB);
    // Also, merge any blocks which we cannot reason about and must preserve
    // the exact fallthrough behavior for.
    while (true) {
      Cond.clear();
      MachineBasicBlock *TBB = nullptr, *FBB = nullptr; // For analyzeBranch.
      if (!TII->analyzeBranch(*BB, TBB, FBB, Cond) || !FI->canFallThrough())
        break;

      MachineFunction::iterator NextFI = std::next(FI);
      MachineBasicBlock *NextBB = &*NextFI;
      // Ensure that the layout successor is a viable block, as we know that
      // fallthrough is a possibility.
      assert(NextFI != FE && "Can't fallthrough past the last block.");
      LLVM_DEBUG(dbgs() << "Pre-merging due to unanalyzable fallthrough: "
                        << getBlockName(BB) << " -> " << getBlockName(NextBB)
                        << "\n");
      Chain->merge(NextBB, nullptr);
#ifndef NDEBUG
      BlocksWithUnanalyzableExits.insert(&*BB);
#endif
      FI = NextFI;
      BB = NextBB;
    }
  }

  // Build any loop-based chains.
  PreferredLoopExit = nullptr;
  for (MachineLoop *L : *MLI)
    buildLoopChains(*L);

  assert(BlockWorkList.empty() &&
         "BlockWorkList should be empty before building final chain.");
  assert(EHPadWorkList.empty() &&
         "EHPadWorkList should be empty before building final chain.");

  SmallPtrSet<BlockChain *, 4> UpdatedPreds;
  for (MachineBasicBlock &MBB : *F)
    fillWorkLists(&MBB, UpdatedPreds);

  BlockChain &FunctionChain = *BlockToChain[&F->front()];
  buildChain(&F->front(), FunctionChain);

#ifndef NDEBUG
  using FunctionBlockSetType = SmallPtrSet<MachineBasicBlock *, 16>;
#endif
  LLVM_DEBUG({
    // Crash at the end so we get all of the debugging output first.
    bool BadFunc = false;
    FunctionBlockSetType FunctionBlockSet;
    for (MachineBasicBlock &MBB : *F)
      FunctionBlockSet.insert(&MBB);

    for (MachineBasicBlock *ChainBB : FunctionChain)
      if (!FunctionBlockSet.erase(ChainBB)) {
        BadFunc = true;
        dbgs() << "Function chain contains a block not in the function!\n"
               << "  Bad block:    " << getBlockName(ChainBB) << "\n";
      }

    if (!FunctionBlockSet.empty()) {
      BadFunc = true;
      for (MachineBasicBlock *RemainingBB : FunctionBlockSet)
        dbgs() << "Function contains blocks never placed into a chain!\n"
               << "  Bad block:    " << getBlockName(RemainingBB) << "\n";
    }
    assert(!BadFunc && "Detected problems with the block placement.");
  });

  // Remember original layout ordering, so we can update terminators after
  // reordering to point to the original layout successor.
  SmallVector<MachineBasicBlock *, 4> OriginalLayoutSuccessors(
      F->getNumBlockIDs());
  {
    MachineBasicBlock *LastMBB = nullptr;
    for (auto &MBB : *F) {
      if (LastMBB != nullptr)
        OriginalLayoutSuccessors[LastMBB->getNumber()] = &MBB;
      LastMBB = &MBB;
    }
    OriginalLayoutSuccessors[F->back().getNumber()] = nullptr;
  }

  // Splice the blocks into place.
  MachineFunction::iterator InsertPos = F->begin();
  LLVM_DEBUG(dbgs() << "[MBP] Function: " << F->getName() << "\n");
  for (MachineBasicBlock *ChainBB : FunctionChain) {
    LLVM_DEBUG(dbgs() << (ChainBB == *FunctionChain.begin() ? "Placing chain "
                                                            : "          ... ")
                      << getBlockName(ChainBB) << "\n");
    if (InsertPos != MachineFunction::iterator(ChainBB))
      F->splice(InsertPos, ChainBB);
    else
      ++InsertPos;

    // Update the terminator of the previous block.
    if (ChainBB == *FunctionChain.begin())
      continue;
    MachineBasicBlock *PrevBB = &*std::prev(MachineFunction::iterator(ChainBB));

    // FIXME: It would be awesome of updateTerminator would just return rather
    // than assert when the branch cannot be analyzed in order to remove this
    // boiler plate.
    Cond.clear();
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr; // For analyzeBranch.

#ifndef NDEBUG
    if (!BlocksWithUnanalyzableExits.count(PrevBB)) {
      // Given the exact block placement we chose, we may actually not _need_ to
      // be able to edit PrevBB's terminator sequence, but not being _able_ to
      // do that at this point is a bug.
      assert((!TII->analyzeBranch(*PrevBB, TBB, FBB, Cond) ||
              !PrevBB->canFallThrough()) &&
             "Unexpected block with un-analyzable fallthrough!");
      Cond.clear();
      TBB = FBB = nullptr;
    }
#endif

    // The "PrevBB" is not yet updated to reflect current code layout, so,
    //   o. it may fall-through to a block without explicit "goto" instruction
    //      before layout, and no longer fall-through it after layout; or
    //   o. just opposite.
    //
    // analyzeBranch() may return erroneous value for FBB when these two
    // situations take place. For the first scenario FBB is mistakenly set NULL;
    // for the 2nd scenario, the FBB, which is expected to be NULL, is
    // mistakenly pointing to "*BI".
    // Thus, if the future change needs to use FBB before the layout is set, it
    // has to correct FBB first by using the code similar to the following:
    //
    // if (!Cond.empty() && (!FBB || FBB == ChainBB)) {
    //   PrevBB->updateTerminator();
    //   Cond.clear();
    //   TBB = FBB = nullptr;
    //   if (TII->analyzeBranch(*PrevBB, TBB, FBB, Cond)) {
    //     // FIXME: This should never take place.
    //     TBB = FBB = nullptr;
    //   }
    // }
    if (!TII->analyzeBranch(*PrevBB, TBB, FBB, Cond)) {
      PrevBB->updateTerminator(OriginalLayoutSuccessors[PrevBB->getNumber()]);
    }
  }

  // Fixup the last block.
  Cond.clear();
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr; // For analyzeBranch.
  if (!TII->analyzeBranch(F->back(), TBB, FBB, Cond)) {
    MachineBasicBlock *PrevBB = &F->back();
    PrevBB->updateTerminator(OriginalLayoutSuccessors[PrevBB->getNumber()]);
  }

  BlockWorkList.clear();
  EHPadWorkList.clear();
}

void MachineBlockPlacement::optimizeBranches() {
  BlockChain &FunctionChain = *BlockToChain[&F->front()];
  SmallVector<MachineOperand, 4> Cond; // For analyzeBranch.

  // Now that all the basic blocks in the chain have the proper layout,
  // make a final call to analyzeBranch with AllowModify set.
  // Indeed, the target may be able to optimize the branches in a way we
  // cannot because all branches may not be analyzable.
  // E.g., the target may be able to remove an unconditional branch to
  // a fallthrough when it occurs after predicated terminators.
  for (MachineBasicBlock *ChainBB : FunctionChain) {
    Cond.clear();
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr; // For analyzeBranch.
    if (!TII->analyzeBranch(*ChainBB, TBB, FBB, Cond, /*AllowModify*/ true)) {
      // If PrevBB has a two-way branch, try to re-order the branches
      // such that we branch to the successor with higher probability first.
      if (TBB && !Cond.empty() && FBB &&
          MBPI->getEdgeProbability(ChainBB, FBB) >
              MBPI->getEdgeProbability(ChainBB, TBB) &&
          !TII->reverseBranchCondition(Cond)) {
        LLVM_DEBUG(dbgs() << "Reverse order of the two branches: "
                          << getBlockName(ChainBB) << "\n");
        LLVM_DEBUG(dbgs() << "    Edge probability: "
                          << MBPI->getEdgeProbability(ChainBB, FBB) << " vs "
                          << MBPI->getEdgeProbability(ChainBB, TBB) << "\n");
        DebugLoc dl; // FIXME: this is nowhere
        TII->removeBranch(*ChainBB);
        TII->insertBranch(*ChainBB, FBB, TBB, Cond, dl);
      }
    }
  }
}

void MachineBlockPlacement::alignBlocks() {
  // Walk through the backedges of the function now that we have fully laid out
  // the basic blocks and align the destination of each backedge. We don't rely
  // exclusively on the loop info here so that we can align backedges in
  // unnatural CFGs and backedges that were introduced purely because of the
  // loop rotations done during this layout pass.
  if (F->getFunction().hasMinSize() ||
      (F->getFunction().hasOptSize() && !TLI->alignLoopsWithOptSize()))
    return;
  BlockChain &FunctionChain = *BlockToChain[&F->front()];
  if (FunctionChain.begin() == FunctionChain.end())
    return; // Empty chain.

  const BranchProbability ColdProb(1, 5); // 20%
  BlockFrequency EntryFreq = MBFI->getBlockFreq(&F->front());
  BlockFrequency WeightedEntryFreq = EntryFreq * ColdProb;
  for (MachineBasicBlock *ChainBB : FunctionChain) {
    if (ChainBB == *FunctionChain.begin())
      continue;

    // Don't align non-looping basic blocks. These are unlikely to execute
    // enough times to matter in practice. Note that we'll still handle
    // unnatural CFGs inside of a natural outer loop (the common case) and
    // rotated loops.
    MachineLoop *L = MLI->getLoopFor(ChainBB);
    if (!L)
      continue;

    const Align TLIAlign = TLI->getPrefLoopAlignment(L);
    unsigned MDAlign = 1;
    MDNode *LoopID = L->getLoopID();
    if (LoopID) {
      for (const MDOperand &MDO : llvm::drop_begin(LoopID->operands())) {
        MDNode *MD = dyn_cast<MDNode>(MDO);
        if (MD == nullptr)
          continue;
        MDString *S = dyn_cast<MDString>(MD->getOperand(0));
        if (S == nullptr)
          continue;
        if (S->getString() == "llvm.loop.align") {
          assert(MD->getNumOperands() == 2 &&
                 "per-loop align metadata should have two operands.");
          MDAlign =
              mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue();
          assert(MDAlign >= 1 && "per-loop align value must be positive.");
        }
      }
    }

    // Use max of the TLIAlign and MDAlign
    const Align LoopAlign = std::max(TLIAlign, Align(MDAlign));
    if (LoopAlign == 1)
      continue; // Don't care about loop alignment.

    // If the block is cold relative to the function entry don't waste space
    // aligning it.
    BlockFrequency Freq = MBFI->getBlockFreq(ChainBB);
    if (Freq < WeightedEntryFreq)
      continue;

    // If the block is cold relative to its loop header, don't align it
    // regardless of what edges into the block exist.
    MachineBasicBlock *LoopHeader = L->getHeader();
    BlockFrequency LoopHeaderFreq = MBFI->getBlockFreq(LoopHeader);
    if (Freq < (LoopHeaderFreq * ColdProb))
      continue;

    // If the global profiles indicates so, don't align it.
    if (llvm::shouldOptimizeForSize(ChainBB, PSI, MBFI.get()) &&
        !TLI->alignLoopsWithOptSize())
      continue;

    // Check for the existence of a non-layout predecessor which would benefit
    // from aligning this block.
    MachineBasicBlock *LayoutPred =
        &*std::prev(MachineFunction::iterator(ChainBB));

    auto DetermineMaxAlignmentPadding = [&]() {
      // Set the maximum bytes allowed to be emitted for alignment.
      unsigned MaxBytes;
      if (MaxBytesForAlignmentOverride.getNumOccurrences() > 0)
        MaxBytes = MaxBytesForAlignmentOverride;
      else
        MaxBytes = TLI->getMaxPermittedBytesForAlignment(ChainBB);
      ChainBB->setMaxBytesForAlignment(MaxBytes);
    };

    // Force alignment if all the predecessors are jumps. We already checked
    // that the block isn't cold above.
    if (!LayoutPred->isSuccessor(ChainBB)) {
      ChainBB->setAlignment(LoopAlign);
      DetermineMaxAlignmentPadding();
      continue;
    }

    // Align this block if the layout predecessor's edge into this block is
    // cold relative to the block. When this is true, other predecessors make up
    // all of the hot entries into the block and thus alignment is likely to be
    // important.
    BranchProbability LayoutProb =
        MBPI->getEdgeProbability(LayoutPred, ChainBB);
    BlockFrequency LayoutEdgeFreq = MBFI->getBlockFreq(LayoutPred) * LayoutProb;
    if (LayoutEdgeFreq <= (Freq * ColdProb)) {
      ChainBB->setAlignment(LoopAlign);
      DetermineMaxAlignmentPadding();
    }
  }
}

/// Tail duplicate \p BB into (some) predecessors if profitable, repeating if
/// it was duplicated into its chain predecessor and removed.
/// \p BB    - Basic block that may be duplicated.
///
/// \p LPred - Chosen layout predecessor of \p BB.
///            Updated to be the chain end if LPred is removed.
/// \p Chain - Chain to which \p LPred belongs, and \p BB will belong.
/// \p BlockFilter - Set of blocks that belong to the loop being laid out.
///                  Used to identify which blocks to update predecessor
///                  counts.
/// \p PrevUnplacedBlockIt - Iterator pointing to the last block that was
///                          chosen in the given order due to unnatural CFG
///                          only needed if \p BB is removed and
///                          \p PrevUnplacedBlockIt pointed to \p BB.
/// @return true if \p BB was removed.
bool MachineBlockPlacement::repeatedlyTailDuplicateBlock(
    MachineBasicBlock *BB, MachineBasicBlock *&LPred,
    const MachineBasicBlock *LoopHeaderBB, BlockChain &Chain,
    BlockFilterSet *BlockFilter, MachineFunction::iterator &PrevUnplacedBlockIt,
    BlockFilterSet::iterator &PrevUnplacedBlockInFilterIt) {
  bool Removed, DuplicatedToLPred;
  bool DuplicatedToOriginalLPred;
  Removed = maybeTailDuplicateBlock(
      BB, LPred, Chain, BlockFilter, PrevUnplacedBlockIt,
      PrevUnplacedBlockInFilterIt, DuplicatedToLPred);
  if (!Removed)
    return false;
  DuplicatedToOriginalLPred = DuplicatedToLPred;
  // Iteratively try to duplicate again. It can happen that a block that is
  // duplicated into is still small enough to be duplicated again.
  // No need to call markBlockSuccessors in this case, as the blocks being
  // duplicated from here on are already scheduled.
  while (DuplicatedToLPred && Removed) {
    MachineBasicBlock *DupBB, *DupPred;
    // The removal callback causes Chain.end() to be updated when a block is
    // removed. On the first pass through the loop, the chain end should be the
    // same as it was on function entry. On subsequent passes, because we are
    // duplicating the block at the end of the chain, if it is removed the
    // chain will have shrunk by one block.
    BlockChain::iterator ChainEnd = Chain.end();
    DupBB = *(--ChainEnd);
    // Now try to duplicate again.
    if (ChainEnd == Chain.begin())
      break;
    DupPred = *std::prev(ChainEnd);
    Removed = maybeTailDuplicateBlock(
        DupBB, DupPred, Chain, BlockFilter, PrevUnplacedBlockIt,
        PrevUnplacedBlockInFilterIt, DuplicatedToLPred);
  }
  // If BB was duplicated into LPred, it is now scheduled. But because it was
  // removed, markChainSuccessors won't be called for its chain. Instead we
  // call markBlockSuccessors for LPred to achieve the same effect. This must go
  // at the end because repeating the tail duplication can increase the number
  // of unscheduled predecessors.
  LPred = *std::prev(Chain.end());
  if (DuplicatedToOriginalLPred)
    markBlockSuccessors(Chain, LPred, LoopHeaderBB, BlockFilter);
  return true;
}

/// Tail duplicate \p BB into (some) predecessors if profitable.
/// \p BB    - Basic block that may be duplicated
/// \p LPred - Chosen layout predecessor of \p BB
/// \p Chain - Chain to which \p LPred belongs, and \p BB will belong.
/// \p BlockFilter - Set of blocks that belong to the loop being laid out.
///                  Used to identify which blocks to update predecessor
///                  counts.
/// \p PrevUnplacedBlockIt - Iterator pointing to the last block that was
///                          chosen in the given order due to unnatural CFG
///                          only needed if \p BB is removed and
///                          \p PrevUnplacedBlockIt pointed to \p BB.
/// \p DuplicatedToLPred - True if the block was duplicated into LPred.
/// \return  - True if the block was duplicated into all preds and removed.
bool MachineBlockPlacement::maybeTailDuplicateBlock(
    MachineBasicBlock *BB, MachineBasicBlock *LPred, BlockChain &Chain,
    BlockFilterSet *BlockFilter, MachineFunction::iterator &PrevUnplacedBlockIt,
    BlockFilterSet::iterator &PrevUnplacedBlockInFilterIt,
    bool &DuplicatedToLPred) {
  DuplicatedToLPred = false;
  if (!shouldTailDuplicate(BB))
    return false;

  LLVM_DEBUG(dbgs() << "Redoing tail duplication for Succ#" << BB->getNumber()
                    << "\n");

  // This has to be a callback because none of it can be done after
  // BB is deleted.
  bool Removed = false;
  auto RemovalCallback =
      [&](MachineBasicBlock *RemBB) {
        // Signal to outer function
        Removed = true;

        // Conservative default.
        bool InWorkList = true;
        // Remove from the Chain and Chain Map
        if (BlockToChain.count(RemBB)) {
          BlockChain *Chain = BlockToChain[RemBB];
          InWorkList = Chain->UnscheduledPredecessors == 0;
          Chain->remove(RemBB);
          BlockToChain.erase(RemBB);
        }

        // Handle the unplaced block iterator
        if (&(*PrevUnplacedBlockIt) == RemBB) {
          PrevUnplacedBlockIt++;
        }

        // Handle the Work Lists
        if (InWorkList) {
          SmallVectorImpl<MachineBasicBlock *> &RemoveList = BlockWorkList;
          if (RemBB->isEHPad())
            RemoveList = EHPadWorkList;
          llvm::erase(RemoveList, RemBB);
        }

        // Handle the filter set
        if (BlockFilter) {
          auto It = llvm::find(*BlockFilter, RemBB);
          // Erase RemBB from BlockFilter, and keep PrevUnplacedBlockInFilterIt
          // pointing to the same element as before.
          if (It != BlockFilter->end()) {
            if (It < PrevUnplacedBlockInFilterIt) {
              const MachineBasicBlock *PrevBB = *PrevUnplacedBlockInFilterIt;
              // BlockFilter is a SmallVector so all elements after RemBB are
              // shifted to the front by 1 after its deletion.
              auto Distance = PrevUnplacedBlockInFilterIt - It - 1;
              PrevUnplacedBlockInFilterIt = BlockFilter->erase(It) + Distance;
              assert(*PrevUnplacedBlockInFilterIt == PrevBB);
              (void)PrevBB;
            } else if (It == PrevUnplacedBlockInFilterIt)
              // The block pointed by PrevUnplacedBlockInFilterIt is erased, we
              // have to set it to the next element.
              PrevUnplacedBlockInFilterIt = BlockFilter->erase(It);
            else
              BlockFilter->erase(It);
          }
        }

        // Remove the block from loop info.
        MLI->removeBlock(RemBB);
        if (RemBB == PreferredLoopExit)
          PreferredLoopExit = nullptr;

        LLVM_DEBUG(dbgs() << "TailDuplicator deleted block: "
                          << getBlockName(RemBB) << "\n");
      };
  auto RemovalCallbackRef =
      function_ref<void(MachineBasicBlock*)>(RemovalCallback);

  SmallVector<MachineBasicBlock *, 8> DuplicatedPreds;
  bool IsSimple = TailDup.isSimpleBB(BB);
  SmallVector<MachineBasicBlock *, 8> CandidatePreds;
  SmallVectorImpl<MachineBasicBlock *> *CandidatePtr = nullptr;
  if (F->getFunction().hasProfileData()) {
    // We can do partial duplication with precise profile information.
    findDuplicateCandidates(CandidatePreds, BB, BlockFilter);
    if (CandidatePreds.size() == 0)
      return false;
    if (CandidatePreds.size() < BB->pred_size())
      CandidatePtr = &CandidatePreds;
  }
  TailDup.tailDuplicateAndUpdate(IsSimple, BB, LPred, &DuplicatedPreds,
                                 &RemovalCallbackRef, CandidatePtr);

  // Update UnscheduledPredecessors to reflect tail-duplication.
  DuplicatedToLPred = false;
  for (MachineBasicBlock *Pred : DuplicatedPreds) {
    // We're only looking for unscheduled predecessors that match the filter.
    BlockChain* PredChain = BlockToChain[Pred];
    if (Pred == LPred)
      DuplicatedToLPred = true;
    if (Pred == LPred || (BlockFilter && !BlockFilter->count(Pred))
        || PredChain == &Chain)
      continue;
    for (MachineBasicBlock *NewSucc : Pred->successors()) {
      if (BlockFilter && !BlockFilter->count(NewSucc))
        continue;
      BlockChain *NewChain = BlockToChain[NewSucc];
      if (NewChain != &Chain && NewChain != PredChain)
        NewChain->UnscheduledPredecessors++;
    }
  }
  return Removed;
}

// Count the number of actual machine instructions.
static uint64_t countMBBInstruction(MachineBasicBlock *MBB) {
  uint64_t InstrCount = 0;
  for (MachineInstr &MI : *MBB) {
    if (!MI.isPHI() && !MI.isMetaInstruction())
      InstrCount += 1;
  }
  return InstrCount;
}

// The size cost of duplication is the instruction size of the duplicated block.
// So we should scale the threshold accordingly. But the instruction size is not
// available on all targets, so we use the number of instructions instead.
BlockFrequency MachineBlockPlacement::scaleThreshold(MachineBasicBlock *BB) {
  return BlockFrequency(DupThreshold.getFrequency() * countMBBInstruction(BB));
}

// Returns true if BB is Pred's best successor.
bool MachineBlockPlacement::isBestSuccessor(MachineBasicBlock *BB,
                                            MachineBasicBlock *Pred,
                                            BlockFilterSet *BlockFilter) {
  if (BB == Pred)
    return false;
  if (BlockFilter && !BlockFilter->count(Pred))
    return false;
  BlockChain *PredChain = BlockToChain[Pred];
  if (PredChain && (Pred != *std::prev(PredChain->end())))
    return false;

  // Find the successor with largest probability excluding BB.
  BranchProbability BestProb = BranchProbability::getZero();
  for (MachineBasicBlock *Succ : Pred->successors())
    if (Succ != BB) {
      if (BlockFilter && !BlockFilter->count(Succ))
        continue;
      BlockChain *SuccChain = BlockToChain[Succ];
      if (SuccChain && (Succ != *SuccChain->begin()))
        continue;
      BranchProbability SuccProb = MBPI->getEdgeProbability(Pred, Succ);
      if (SuccProb > BestProb)
        BestProb = SuccProb;
    }

  BranchProbability BBProb = MBPI->getEdgeProbability(Pred, BB);
  if (BBProb <= BestProb)
    return false;

  // Compute the number of reduced taken branches if Pred falls through to BB
  // instead of another successor. Then compare it with threshold.
  BlockFrequency PredFreq = getBlockCountOrFrequency(Pred);
  BlockFrequency Gain = PredFreq * (BBProb - BestProb);
  return Gain > scaleThreshold(BB);
}

// Find out the predecessors of BB and BB can be beneficially duplicated into
// them.
void MachineBlockPlacement::findDuplicateCandidates(
    SmallVectorImpl<MachineBasicBlock *> &Candidates,
    MachineBasicBlock *BB,
    BlockFilterSet *BlockFilter) {
  MachineBasicBlock *Fallthrough = nullptr;
  BranchProbability DefaultBranchProb = BranchProbability::getZero();
  BlockFrequency BBDupThreshold(scaleThreshold(BB));
  SmallVector<MachineBasicBlock *, 8> Preds(BB->predecessors());
  SmallVector<MachineBasicBlock *, 8> Succs(BB->successors());

  // Sort for highest frequency.
  auto CmpSucc = [&](MachineBasicBlock *A, MachineBasicBlock *B) {
    return MBPI->getEdgeProbability(BB, A) > MBPI->getEdgeProbability(BB, B);
  };
  auto CmpPred = [&](MachineBasicBlock *A, MachineBasicBlock *B) {
    return MBFI->getBlockFreq(A) > MBFI->getBlockFreq(B);
  };
  llvm::stable_sort(Succs, CmpSucc);
  llvm::stable_sort(Preds, CmpPred);

  auto SuccIt = Succs.begin();
  if (SuccIt != Succs.end()) {
    DefaultBranchProb = MBPI->getEdgeProbability(BB, *SuccIt).getCompl();
  }

  // For each predecessors of BB, compute the benefit of duplicating BB,
  // if it is larger than the threshold, add it into Candidates.
  //
  // If we have following control flow.
  //
  //     PB1 PB2 PB3 PB4
  //      \   |  /    /\
  //       \  | /    /  \
  //        \ |/    /    \
  //         BB----/     OB
  //         /\
  //        /  \
  //      SB1 SB2
  //
  // And it can be partially duplicated as
  //
  //   PB2+BB
  //      |  PB1 PB3 PB4
  //      |   |  /    /\
  //      |   | /    /  \
  //      |   |/    /    \
  //      |  BB----/     OB
  //      |\ /|
  //      | X |
  //      |/ \|
  //     SB2 SB1
  //
  // The benefit of duplicating into a predecessor is defined as
  //         Orig_taken_branch - Duplicated_taken_branch
  //
  // The Orig_taken_branch is computed with the assumption that predecessor
  // jumps to BB and the most possible successor is laid out after BB.
  //
  // The Duplicated_taken_branch is computed with the assumption that BB is
  // duplicated into PB, and one successor is layout after it (SB1 for PB1 and
  // SB2 for PB2 in our case). If there is no available successor, the combined
  // block jumps to all BB's successor, like PB3 in this example.
  //
  // If a predecessor has multiple successors, so BB can't be duplicated into
  // it. But it can beneficially fall through to BB, and duplicate BB into other
  // predecessors.
  for (MachineBasicBlock *Pred : Preds) {
    BlockFrequency PredFreq = getBlockCountOrFrequency(Pred);

    if (!TailDup.canTailDuplicate(BB, Pred)) {
      // BB can't be duplicated into Pred, but it is possible to be layout
      // below Pred.
      if (!Fallthrough && isBestSuccessor(BB, Pred, BlockFilter)) {
        Fallthrough = Pred;
        if (SuccIt != Succs.end())
          SuccIt++;
      }
      continue;
    }

    BlockFrequency OrigCost = PredFreq + PredFreq * DefaultBranchProb;
    BlockFrequency DupCost;
    if (SuccIt == Succs.end()) {
      // Jump to all successors;
      if (Succs.size() > 0)
        DupCost += PredFreq;
    } else {
      // Fallthrough to *SuccIt, jump to all other successors;
      DupCost += PredFreq;
      DupCost -= PredFreq * MBPI->getEdgeProbability(BB, *SuccIt);
    }

    assert(OrigCost >= DupCost);
    OrigCost -= DupCost;
    if (OrigCost > BBDupThreshold) {
      Candidates.push_back(Pred);
      if (SuccIt != Succs.end())
        SuccIt++;
    }
  }

  // No predecessors can optimally fallthrough to BB.
  // So we can change one duplication into fallthrough.
  if (!Fallthrough) {
    if ((Candidates.size() < Preds.size()) && (Candidates.size() > 0)) {
      Candidates[0] = Candidates.back();
      Candidates.pop_back();
    }
  }
}

void MachineBlockPlacement::initDupThreshold() {
  DupThreshold = BlockFrequency(0);
  if (!F->getFunction().hasProfileData())
    return;

  // We prefer to use prifile count.
  uint64_t HotThreshold = PSI->getOrCompHotCountThreshold();
  if (HotThreshold != UINT64_MAX) {
    UseProfileCount = true;
    DupThreshold =
        BlockFrequency(HotThreshold * TailDupProfilePercentThreshold / 100);
    return;
  }

  // Profile count is not available, we can use block frequency instead.
  BlockFrequency MaxFreq = BlockFrequency(0);
  for (MachineBasicBlock &MBB : *F) {
    BlockFrequency Freq = MBFI->getBlockFreq(&MBB);
    if (Freq > MaxFreq)
      MaxFreq = Freq;
  }

  BranchProbability ThresholdProb(TailDupPlacementPenalty, 100);
  DupThreshold = BlockFrequency(MaxFreq * ThresholdProb);
  UseProfileCount = false;
}

bool MachineBlockPlacement::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  // Check for single-block functions and skip them.
  if (std::next(MF.begin()) == MF.end())
    return false;

  F = &MF;
  MBPI = &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  MBFI = std::make_unique<MBFIWrapper>(
      getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI());
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  TII = MF.getSubtarget().getInstrInfo();
  TLI = MF.getSubtarget().getTargetLowering();
  MPDT = nullptr;
  PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();

  initDupThreshold();

  // Initialize PreferredLoopExit to nullptr here since it may never be set if
  // there are no MachineLoops.
  PreferredLoopExit = nullptr;

  assert(BlockToChain.empty() &&
         "BlockToChain map should be empty before starting placement.");
  assert(ComputedEdges.empty() &&
         "Computed Edge map should be empty before starting placement.");

  unsigned TailDupSize = TailDupPlacementThreshold;
  // If only the aggressive threshold is explicitly set, use it.
  if (TailDupPlacementAggressiveThreshold.getNumOccurrences() != 0 &&
      TailDupPlacementThreshold.getNumOccurrences() == 0)
    TailDupSize = TailDupPlacementAggressiveThreshold;

  TargetPassConfig *PassConfig = &getAnalysis<TargetPassConfig>();
  // For aggressive optimization, we can adjust some thresholds to be less
  // conservative.
  if (PassConfig->getOptLevel() >= CodeGenOptLevel::Aggressive) {
    // At O3 we should be more willing to copy blocks for tail duplication. This
    // increases size pressure, so we only do it at O3
    // Do this unless only the regular threshold is explicitly set.
    if (TailDupPlacementThreshold.getNumOccurrences() == 0 ||
        TailDupPlacementAggressiveThreshold.getNumOccurrences() != 0)
      TailDupSize = TailDupPlacementAggressiveThreshold;
  }

  // If there's no threshold provided through options, query the target
  // information for a threshold instead.
  if (TailDupPlacementThreshold.getNumOccurrences() == 0 &&
      (PassConfig->getOptLevel() < CodeGenOptLevel::Aggressive ||
       TailDupPlacementAggressiveThreshold.getNumOccurrences() == 0))
    TailDupSize = TII->getTailDuplicateSize(PassConfig->getOptLevel());

  if (allowTailDupPlacement()) {
    MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
    bool OptForSize = MF.getFunction().hasOptSize() ||
                      llvm::shouldOptimizeForSize(&MF, PSI, &MBFI->getMBFI());
    if (OptForSize)
      TailDupSize = 1;
    bool PreRegAlloc = false;
    TailDup.initMF(MF, PreRegAlloc, MBPI, MBFI.get(), PSI,
                   /* LayoutMode */ true, TailDupSize);
    precomputeTriangleChains();
  }

  buildCFGChains();

  // Changing the layout can create new tail merging opportunities.
  // TailMerge can create jump into if branches that make CFG irreducible for
  // HW that requires structured CFG.
  bool EnableTailMerge = !MF.getTarget().requiresStructuredCFG() &&
                         PassConfig->getEnableTailMerge() &&
                         BranchFoldPlacement;
  // No tail merging opportunities if the block number is less than four.
  if (MF.size() > 3 && EnableTailMerge) {
    unsigned TailMergeSize = TailDupSize + 1;
    BranchFolder BF(/*DefaultEnableTailMerge=*/true, /*CommonHoist=*/false,
                    *MBFI, *MBPI, PSI, TailMergeSize);

    if (BF.OptimizeFunction(MF, TII, MF.getSubtarget().getRegisterInfo(), MLI,
                            /*AfterPlacement=*/true)) {
      // Redo the layout if tail merging creates/removes/moves blocks.
      BlockToChain.clear();
      ComputedEdges.clear();
      // Must redo the post-dominator tree if blocks were changed.
      if (MPDT)
        MPDT->recalculate(MF);
      ChainAllocator.DestroyAll();
      buildCFGChains();
    }
  }

  // Apply a post-processing optimizing block placement.
  if (MF.size() >= 3 && EnableExtTspBlockPlacement &&
      (ApplyExtTspWithoutProfile || MF.getFunction().hasProfileData())) {
    // Find a new placement and modify the layout of the blocks in the function.
    applyExtTsp();

    // Re-create CFG chain so that we can optimizeBranches and alignBlocks.
    createCFGChainExtTsp();
  }

  optimizeBranches();
  alignBlocks();

  BlockToChain.clear();
  ComputedEdges.clear();
  ChainAllocator.DestroyAll();

  bool HasMaxBytesOverride =
      MaxBytesForAlignmentOverride.getNumOccurrences() > 0;

  if (AlignAllBlock)
    // Align all of the blocks in the function to a specific alignment.
    for (MachineBasicBlock &MBB : MF) {
      if (HasMaxBytesOverride)
        MBB.setAlignment(Align(1ULL << AlignAllBlock),
                         MaxBytesForAlignmentOverride);
      else
        MBB.setAlignment(Align(1ULL << AlignAllBlock));
    }
  else if (AlignAllNonFallThruBlocks) {
    // Align all of the blocks that have no fall-through predecessors to a
    // specific alignment.
    for (auto MBI = std::next(MF.begin()), MBE = MF.end(); MBI != MBE; ++MBI) {
      auto LayoutPred = std::prev(MBI);
      if (!LayoutPred->isSuccessor(&*MBI)) {
        if (HasMaxBytesOverride)
          MBI->setAlignment(Align(1ULL << AlignAllNonFallThruBlocks),
                            MaxBytesForAlignmentOverride);
        else
          MBI->setAlignment(Align(1ULL << AlignAllNonFallThruBlocks));
      }
    }
  }
  if (ViewBlockLayoutWithBFI != GVDT_None &&
      (ViewBlockFreqFuncName.empty() ||
       F->getFunction().getName() == ViewBlockFreqFuncName)) {
    if (RenumberBlocksBeforeView)
      MF.RenumberBlocks();
    MBFI->view("MBP." + MF.getName(), false);
  }

  // We always return true as we have no way to track whether the final order
  // differs from the original order.
  return true;
}

void MachineBlockPlacement::applyExtTsp() {
  // Prepare data; blocks are indexed by their index in the current ordering.
  DenseMap<const MachineBasicBlock *, uint64_t> BlockIndex;
  BlockIndex.reserve(F->size());
  std::vector<const MachineBasicBlock *> CurrentBlockOrder;
  CurrentBlockOrder.reserve(F->size());
  size_t NumBlocks = 0;
  for (const MachineBasicBlock &MBB : *F) {
    BlockIndex[&MBB] = NumBlocks++;
    CurrentBlockOrder.push_back(&MBB);
  }

  auto BlockSizes = std::vector<uint64_t>(F->size());
  auto BlockCounts = std::vector<uint64_t>(F->size());
  std::vector<codelayout::EdgeCount> JumpCounts;
  for (MachineBasicBlock &MBB : *F) {
    // Getting the block frequency.
    BlockFrequency BlockFreq = MBFI->getBlockFreq(&MBB);
    BlockCounts[BlockIndex[&MBB]] = BlockFreq.getFrequency();
    // Getting the block size:
    // - approximate the size of an instruction by 4 bytes, and
    // - ignore debug instructions.
    // Note: getting the exact size of each block is target-dependent and can be
    // done by extending the interface of MCCodeEmitter. Experimentally we do
    // not see a perf improvement with the exact block sizes.
    auto NonDbgInsts =
        instructionsWithoutDebug(MBB.instr_begin(), MBB.instr_end());
    int NumInsts = std::distance(NonDbgInsts.begin(), NonDbgInsts.end());
    BlockSizes[BlockIndex[&MBB]] = 4 * NumInsts;
    // Getting jump frequencies.
    for (MachineBasicBlock *Succ : MBB.successors()) {
      auto EP = MBPI->getEdgeProbability(&MBB, Succ);
      BlockFrequency JumpFreq = BlockFreq * EP;
      JumpCounts.push_back(
          {BlockIndex[&MBB], BlockIndex[Succ], JumpFreq.getFrequency()});
    }
  }

  LLVM_DEBUG(dbgs() << "Applying ext-tsp layout for |V| = " << F->size()
                    << " with profile = " << F->getFunction().hasProfileData()
                    << " (" << F->getName().str() << ")"
                    << "\n");
  LLVM_DEBUG(
      dbgs() << format("  original  layout score: %0.2f\n",
                       calcExtTspScore(BlockSizes, BlockCounts, JumpCounts)));

  // Run the layout algorithm.
  auto NewOrder = computeExtTspLayout(BlockSizes, BlockCounts, JumpCounts);
  std::vector<const MachineBasicBlock *> NewBlockOrder;
  NewBlockOrder.reserve(F->size());
  for (uint64_t Node : NewOrder) {
    NewBlockOrder.push_back(CurrentBlockOrder[Node]);
  }
  LLVM_DEBUG(dbgs() << format("  optimized layout score: %0.2f\n",
                              calcExtTspScore(NewOrder, BlockSizes, BlockCounts,
                                              JumpCounts)));

  // Assign new block order.
  assignBlockOrder(NewBlockOrder);
}

void MachineBlockPlacement::assignBlockOrder(
    const std::vector<const MachineBasicBlock *> &NewBlockOrder) {
  assert(F->size() == NewBlockOrder.size() && "Incorrect size of block order");
  F->RenumberBlocks();

  bool HasChanges = false;
  for (size_t I = 0; I < NewBlockOrder.size(); I++) {
    if (NewBlockOrder[I] != F->getBlockNumbered(I)) {
      HasChanges = true;
      break;
    }
  }
  // Stop early if the new block order is identical to the existing one.
  if (!HasChanges)
    return;

  SmallVector<MachineBasicBlock *, 4> PrevFallThroughs(F->getNumBlockIDs());
  for (auto &MBB : *F) {
    PrevFallThroughs[MBB.getNumber()] = MBB.getFallThrough();
  }

  // Sort basic blocks in the function according to the computed order.
  DenseMap<const MachineBasicBlock *, size_t> NewIndex;
  for (const MachineBasicBlock *MBB : NewBlockOrder) {
    NewIndex[MBB] = NewIndex.size();
  }
  F->sort([&](MachineBasicBlock &L, MachineBasicBlock &R) {
    return NewIndex[&L] < NewIndex[&R];
  });

  // Update basic block branches by inserting explicit fallthrough branches
  // when required and re-optimize branches when possible.
  const TargetInstrInfo *TII = F->getSubtarget().getInstrInfo();
  SmallVector<MachineOperand, 4> Cond;
  for (auto &MBB : *F) {
    MachineFunction::iterator NextMBB = std::next(MBB.getIterator());
    MachineFunction::iterator EndIt = MBB.getParent()->end();
    auto *FTMBB = PrevFallThroughs[MBB.getNumber()];
    // If this block had a fallthrough before we need an explicit unconditional
    // branch to that block if the fallthrough block is not adjacent to the
    // block in the new order.
    if (FTMBB && (NextMBB == EndIt || &*NextMBB != FTMBB)) {
      TII->insertUnconditionalBranch(MBB, FTMBB, MBB.findBranchDebugLoc());
    }

    // It might be possible to optimize branches by flipping the condition.
    Cond.clear();
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    if (TII->analyzeBranch(MBB, TBB, FBB, Cond))
      continue;
    MBB.updateTerminator(FTMBB);
  }

#ifndef NDEBUG
  // Make sure we correctly constructed all branches.
  F->verify(this, "After optimized block reordering");
#endif
}

void MachineBlockPlacement::createCFGChainExtTsp() {
  BlockToChain.clear();
  ComputedEdges.clear();
  ChainAllocator.DestroyAll();

  MachineBasicBlock *HeadBB = &F->front();
  BlockChain *FunctionChain =
      new (ChainAllocator.Allocate()) BlockChain(BlockToChain, HeadBB);

  for (MachineBasicBlock &MBB : *F) {
    if (HeadBB == &MBB)
      continue; // Ignore head of the chain
    FunctionChain->merge(&MBB, nullptr);
  }
}

namespace {

/// A pass to compute block placement statistics.
///
/// A separate pass to compute interesting statistics for evaluating block
/// placement. This is separate from the actual placement pass so that they can
/// be computed in the absence of any placement transformations or when using
/// alternative placement strategies.
class MachineBlockPlacementStats : public MachineFunctionPass {
  /// A handle to the branch probability pass.
  const MachineBranchProbabilityInfo *MBPI;

  /// A handle to the function-wide block frequency pass.
  const MachineBlockFrequencyInfo *MBFI;

public:
  static char ID; // Pass identification, replacement for typeid

  MachineBlockPlacementStats() : MachineFunctionPass(ID) {
    initializeMachineBlockPlacementStatsPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char MachineBlockPlacementStats::ID = 0;

char &llvm::MachineBlockPlacementStatsID = MachineBlockPlacementStats::ID;

INITIALIZE_PASS_BEGIN(MachineBlockPlacementStats, "block-placement-stats",
                      "Basic Block Placement Stats", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_END(MachineBlockPlacementStats, "block-placement-stats",
                    "Basic Block Placement Stats", false, false)

bool MachineBlockPlacementStats::runOnMachineFunction(MachineFunction &F) {
  // Check for single-block functions and skip them.
  if (std::next(F.begin()) == F.end())
    return false;

  if (!isFunctionInPrintList(F.getName()))
    return false;

  MBPI = &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  MBFI = &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();

  for (MachineBasicBlock &MBB : F) {
    BlockFrequency BlockFreq = MBFI->getBlockFreq(&MBB);
    Statistic &NumBranches =
        (MBB.succ_size() > 1) ? NumCondBranches : NumUncondBranches;
    Statistic &BranchTakenFreq =
        (MBB.succ_size() > 1) ? CondBranchTakenFreq : UncondBranchTakenFreq;
    for (MachineBasicBlock *Succ : MBB.successors()) {
      // Skip if this successor is a fallthrough.
      if (MBB.isLayoutSuccessor(Succ))
        continue;

      BlockFrequency EdgeFreq =
          BlockFreq * MBPI->getEdgeProbability(&MBB, Succ);
      ++NumBranches;
      BranchTakenFreq += EdgeFreq.getFrequency();
    }
  }

  return false;
}
