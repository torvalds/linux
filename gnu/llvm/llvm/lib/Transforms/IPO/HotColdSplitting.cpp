//===- HotColdSplitting.cpp -- Outline Cold Regions -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The goal of hot/cold splitting is to improve the memory locality of code.
/// The splitting pass does this by identifying cold blocks and moving them into
/// separate functions.
///
/// When the splitting pass finds a cold block (referred to as "the sink"), it
/// grows a maximal cold region around that block. The maximal region contains
/// all blocks (post-)dominated by the sink [*]. In theory, these blocks are as
/// cold as the sink. Once a region is found, it's split out of the original
/// function provided it's profitable to do so.
///
/// [*] In practice, there is some added complexity because some blocks are not
/// safe to extract.
///
/// TODO: Use the PM to get domtrees, and preserve BFI/BPI.
/// TODO: Reorder outlined functions.
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/HotColdSplitting.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <string>

#define DEBUG_TYPE "hotcoldsplit"

STATISTIC(NumColdRegionsFound, "Number of cold regions found.");
STATISTIC(NumColdRegionsOutlined, "Number of cold regions outlined.");

using namespace llvm;

static cl::opt<bool> EnableStaticAnalysis("hot-cold-static-analysis",
                                          cl::init(true), cl::Hidden);

static cl::opt<int>
    SplittingThreshold("hotcoldsplit-threshold", cl::init(2), cl::Hidden,
                       cl::desc("Base penalty for splitting cold code (as a "
                                "multiple of TCC_Basic)"));

static cl::opt<bool> EnableColdSection(
    "enable-cold-section", cl::init(false), cl::Hidden,
    cl::desc("Enable placement of extracted cold functions"
             " into a separate section after hot-cold splitting."));

static cl::opt<std::string>
    ColdSectionName("hotcoldsplit-cold-section-name", cl::init("__llvm_cold"),
                    cl::Hidden,
                    cl::desc("Name for the section containing cold functions "
                             "extracted by hot-cold splitting."));

static cl::opt<int> MaxParametersForSplit(
    "hotcoldsplit-max-params", cl::init(4), cl::Hidden,
    cl::desc("Maximum number of parameters for a split function"));

static cl::opt<int> ColdBranchProbDenom(
    "hotcoldsplit-cold-probability-denom", cl::init(100), cl::Hidden,
    cl::desc("Divisor of cold branch probability."
             "BranchProbability = 1/ColdBranchProbDenom"));

namespace {
// Same as blockEndsInUnreachable in CodeGen/BranchFolding.cpp. Do not modify
// this function unless you modify the MBB version as well.
//
/// A no successor, non-return block probably ends in unreachable and is cold.
/// Also consider a block that ends in an indirect branch to be a return block,
/// since many targets use plain indirect branches to return.
bool blockEndsInUnreachable(const BasicBlock &BB) {
  if (!succ_empty(&BB))
    return false;
  if (BB.empty())
    return true;
  const Instruction *I = BB.getTerminator();
  return !(isa<ReturnInst>(I) || isa<IndirectBrInst>(I));
}

void analyzeProfMetadata(BasicBlock *BB,
                         BranchProbability ColdProbThresh,
                         SmallPtrSetImpl<BasicBlock *> &AnnotatedColdBlocks) {
  // TODO: Handle branches with > 2 successors.
  BranchInst *CondBr = dyn_cast<BranchInst>(BB->getTerminator());
  if (!CondBr)
    return;

  uint64_t TrueWt, FalseWt;
  if (!extractBranchWeights(*CondBr, TrueWt, FalseWt))
    return;

  auto SumWt = TrueWt + FalseWt;
  if (SumWt == 0)
    return;

  auto TrueProb = BranchProbability::getBranchProbability(TrueWt, SumWt);
  auto FalseProb = BranchProbability::getBranchProbability(FalseWt, SumWt);

  if (TrueProb <= ColdProbThresh)
    AnnotatedColdBlocks.insert(CondBr->getSuccessor(0));

  if (FalseProb <= ColdProbThresh)
    AnnotatedColdBlocks.insert(CondBr->getSuccessor(1));
}

bool unlikelyExecuted(BasicBlock &BB) {
  // Exception handling blocks are unlikely executed.
  if (BB.isEHPad() || isa<ResumeInst>(BB.getTerminator()))
    return true;

  // The block is cold if it calls/invokes a cold function. However, do not
  // mark sanitizer traps as cold.
  for (Instruction &I : BB)
    if (auto *CB = dyn_cast<CallBase>(&I))
      if (CB->hasFnAttr(Attribute::Cold) &&
          !CB->getMetadata(LLVMContext::MD_nosanitize))
        return true;

  // The block is cold if it has an unreachable terminator, unless it's
  // preceded by a call to a (possibly warm) noreturn call (e.g. longjmp).
  if (blockEndsInUnreachable(BB)) {
    if (auto *CI =
            dyn_cast_or_null<CallInst>(BB.getTerminator()->getPrevNode()))
      if (CI->hasFnAttr(Attribute::NoReturn))
        return false;
    return true;
  }

  return false;
}

/// Check whether it's safe to outline \p BB.
static bool mayExtractBlock(const BasicBlock &BB) {
  // EH pads are unsafe to outline because doing so breaks EH type tables. It
  // follows that invoke instructions cannot be extracted, because CodeExtractor
  // requires unwind destinations to be within the extraction region.
  //
  // Resumes that are not reachable from a cleanup landing pad are considered to
  // be unreachable. It’s not safe to split them out either.

  if (BB.hasAddressTaken() || BB.isEHPad())
    return false;
  auto Term = BB.getTerminator();
  if (isa<InvokeInst>(Term) || isa<ResumeInst>(Term))
    return false;

  // Do not outline basic blocks that have token type instructions. e.g.,
  // exception:
  // %0 = cleanuppad within none []
  // call void @"?terminate@@YAXXZ"() [ "funclet"(token %0) ]
  // br label %continue-exception
  if (llvm::any_of(
          BB, [](const Instruction &I) { return I.getType()->isTokenTy(); })) {
    return false;
  }

  return true;
}

/// Mark \p F cold. Based on this assumption, also optimize it for minimum size.
/// If \p UpdateEntryCount is true (set when this is a new split function and
/// module has profile data), set entry count to 0 to ensure treated as cold.
/// Return true if the function is changed.
static bool markFunctionCold(Function &F, bool UpdateEntryCount = false) {
  assert(!F.hasOptNone() && "Can't mark this cold");
  bool Changed = false;
  if (!F.hasFnAttribute(Attribute::Cold)) {
    F.addFnAttr(Attribute::Cold);
    Changed = true;
  }
  if (!F.hasFnAttribute(Attribute::MinSize)) {
    F.addFnAttr(Attribute::MinSize);
    Changed = true;
  }
  if (UpdateEntryCount) {
    // Set the entry count to 0 to ensure it is placed in the unlikely text
    // section when function sections are enabled.
    F.setEntryCount(0);
    Changed = true;
  }

  return Changed;
}

} // end anonymous namespace

/// Check whether \p F is inherently cold.
bool HotColdSplitting::isFunctionCold(const Function &F) const {
  if (F.hasFnAttribute(Attribute::Cold))
    return true;

  if (F.getCallingConv() == CallingConv::Cold)
    return true;

  if (PSI->isFunctionEntryCold(&F))
    return true;

  return false;
}

bool HotColdSplitting::isBasicBlockCold(
    BasicBlock *BB, BranchProbability ColdProbThresh,
    SmallPtrSetImpl<BasicBlock *> &AnnotatedColdBlocks,
    BlockFrequencyInfo *BFI) const {
  if (BFI) {
    if (PSI->isColdBlock(BB, BFI))
      return true;
  } else {
    // Find cold blocks of successors of BB during a reverse postorder traversal.
    analyzeProfMetadata(BB, ColdProbThresh, AnnotatedColdBlocks);

    // A statically cold BB would be known before it is visited
    // because the prof-data of incoming edges are 'analyzed' as part of RPOT.
    if (AnnotatedColdBlocks.count(BB))
      return true;
  }

  if (EnableStaticAnalysis && unlikelyExecuted(*BB))
    return true;

  return false;
}

// Returns false if the function should not be considered for hot-cold split
// optimization.
bool HotColdSplitting::shouldOutlineFrom(const Function &F) const {
  if (F.hasFnAttribute(Attribute::AlwaysInline))
    return false;

  if (F.hasFnAttribute(Attribute::NoInline))
    return false;

  // A function marked `noreturn` may contain unreachable terminators: these
  // should not be considered cold, as the function may be a trampoline.
  if (F.hasFnAttribute(Attribute::NoReturn))
    return false;

  if (F.hasFnAttribute(Attribute::SanitizeAddress) ||
      F.hasFnAttribute(Attribute::SanitizeHWAddress) ||
      F.hasFnAttribute(Attribute::SanitizeThread) ||
      F.hasFnAttribute(Attribute::SanitizeMemory))
    return false;

  // Do not outline scoped EH personality functions.
  if (F.hasPersonalityFn())
    if (isScopedEHPersonality(classifyEHPersonality(F.getPersonalityFn())))
      return false;

  return true;
}

/// Get the benefit score of outlining \p Region.
static InstructionCost getOutliningBenefit(ArrayRef<BasicBlock *> Region,
                                           TargetTransformInfo &TTI) {
  // Sum up the code size costs of non-terminator instructions. Tight coupling
  // with \ref getOutliningPenalty is needed to model the costs of terminators.
  InstructionCost Benefit = 0;
  for (BasicBlock *BB : Region)
    for (Instruction &I : BB->instructionsWithoutDebug())
      if (&I != BB->getTerminator())
        Benefit +=
            TTI.getInstructionCost(&I, TargetTransformInfo::TCK_CodeSize);

  return Benefit;
}

/// Get the penalty score for outlining \p Region.
static int getOutliningPenalty(ArrayRef<BasicBlock *> Region,
                               unsigned NumInputs, unsigned NumOutputs) {
  int Penalty = SplittingThreshold;
  LLVM_DEBUG(dbgs() << "Applying penalty for splitting: " << Penalty << "\n");

  // If the splitting threshold is set at or below zero, skip the usual
  // profitability check.
  if (SplittingThreshold <= 0)
    return Penalty;

  // Find the number of distinct exit blocks for the region. Use a conservative
  // check to determine whether control returns from the region.
  bool NoBlocksReturn = true;
  SmallPtrSet<BasicBlock *, 2> SuccsOutsideRegion;
  for (BasicBlock *BB : Region) {
    // If a block has no successors, only assume it does not return if it's
    // unreachable.
    if (succ_empty(BB)) {
      NoBlocksReturn &= isa<UnreachableInst>(BB->getTerminator());
      continue;
    }

    for (BasicBlock *SuccBB : successors(BB)) {
      if (!is_contained(Region, SuccBB)) {
        NoBlocksReturn = false;
        SuccsOutsideRegion.insert(SuccBB);
      }
    }
  }

  // Count the number of phis in exit blocks with >= 2 incoming values from the
  // outlining region. These phis are split (\ref severSplitPHINodesOfExits),
  // and new outputs are created to supply the split phis. CodeExtractor can't
  // report these new outputs until extraction begins, but it's important to
  // factor the cost of the outputs into the cost calculation.
  unsigned NumSplitExitPhis = 0;
  for (BasicBlock *ExitBB : SuccsOutsideRegion) {
    for (PHINode &PN : ExitBB->phis()) {
      // Find all incoming values from the outlining region.
      int NumIncomingVals = 0;
      for (unsigned i = 0; i < PN.getNumIncomingValues(); ++i)
        if (llvm::is_contained(Region, PN.getIncomingBlock(i))) {
          ++NumIncomingVals;
          if (NumIncomingVals > 1) {
            ++NumSplitExitPhis;
            break;
          }
        }
    }
  }

  // Apply a penalty for calling the split function. Factor in the cost of
  // materializing all of the parameters.
  int NumOutputsAndSplitPhis = NumOutputs + NumSplitExitPhis;
  int NumParams = NumInputs + NumOutputsAndSplitPhis;
  if (NumParams > MaxParametersForSplit) {
    LLVM_DEBUG(dbgs() << NumInputs << " inputs and " << NumOutputsAndSplitPhis
                      << " outputs exceeds parameter limit ("
                      << MaxParametersForSplit << ")\n");
    return std::numeric_limits<int>::max();
  }
  const int CostForArgMaterialization = 2 * TargetTransformInfo::TCC_Basic;
  LLVM_DEBUG(dbgs() << "Applying penalty for: " << NumParams << " params\n");
  Penalty += CostForArgMaterialization * NumParams;

  // Apply the typical code size cost for an output alloca and its associated
  // reload in the caller. Also penalize the associated store in the callee.
  LLVM_DEBUG(dbgs() << "Applying penalty for: " << NumOutputsAndSplitPhis
                    << " outputs/split phis\n");
  const int CostForRegionOutput = 3 * TargetTransformInfo::TCC_Basic;
  Penalty += CostForRegionOutput * NumOutputsAndSplitPhis;

  // Apply a `noreturn` bonus.
  if (NoBlocksReturn) {
    LLVM_DEBUG(dbgs() << "Applying bonus for: " << Region.size()
                      << " non-returning terminators\n");
    Penalty -= Region.size();
  }

  // Apply a penalty for having more than one successor outside of the region.
  // This penalty accounts for the switch needed in the caller.
  if (SuccsOutsideRegion.size() > 1) {
    LLVM_DEBUG(dbgs() << "Applying penalty for: " << SuccsOutsideRegion.size()
                      << " non-region successors\n");
    Penalty += (SuccsOutsideRegion.size() - 1) * TargetTransformInfo::TCC_Basic;
  }

  return Penalty;
}

// Determine if it is beneficial to split the \p Region.
bool HotColdSplitting::isSplittingBeneficial(CodeExtractor &CE,
                                             const BlockSequence &Region,
                                             TargetTransformInfo &TTI) {
  assert(!Region.empty());

  // Perform a simple cost/benefit analysis to decide whether or not to permit
  // splitting.
  SetVector<Value *> Inputs, Outputs, Sinks;
  CE.findInputsOutputs(Inputs, Outputs, Sinks);
  InstructionCost OutliningBenefit = getOutliningBenefit(Region, TTI);
  int OutliningPenalty =
      getOutliningPenalty(Region, Inputs.size(), Outputs.size());
  LLVM_DEBUG(dbgs() << "Split profitability: benefit = " << OutliningBenefit
                    << ", penalty = " << OutliningPenalty << "\n");
  if (!OutliningBenefit.isValid() || OutliningBenefit <= OutliningPenalty)
    return false;

  return true;
}

// Split the single \p EntryPoint cold region. \p CE is the region code
// extractor.
Function *HotColdSplitting::extractColdRegion(
    BasicBlock &EntryPoint, CodeExtractor &CE,
    const CodeExtractorAnalysisCache &CEAC, BlockFrequencyInfo *BFI,
    TargetTransformInfo &TTI, OptimizationRemarkEmitter &ORE) {
  Function *OrigF = EntryPoint.getParent();
  if (Function *OutF = CE.extractCodeRegion(CEAC)) {
    User *U = *OutF->user_begin();
    CallInst *CI = cast<CallInst>(U);
    NumColdRegionsOutlined++;
    if (TTI.useColdCCForColdCall(*OutF)) {
      OutF->setCallingConv(CallingConv::Cold);
      CI->setCallingConv(CallingConv::Cold);
    }
    CI->setIsNoInline();

    if (EnableColdSection)
      OutF->setSection(ColdSectionName);
    else {
      if (OrigF->hasSection())
        OutF->setSection(OrigF->getSection());
    }

    markFunctionCold(*OutF, BFI != nullptr);

    LLVM_DEBUG(llvm::dbgs() << "Outlined Region: " << *OutF);
    ORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "HotColdSplit",
                                &*EntryPoint.begin())
             << ore::NV("Original", OrigF) << " split cold code into "
             << ore::NV("Split", OutF);
    });
    return OutF;
  }

  ORE.emit([&]() {
    return OptimizationRemarkMissed(DEBUG_TYPE, "ExtractFailed",
                                    &*EntryPoint.begin())
           << "Failed to extract region at block "
           << ore::NV("Block", &EntryPoint);
  });
  return nullptr;
}

/// A pair of (basic block, score).
using BlockTy = std::pair<BasicBlock *, unsigned>;

namespace {
/// A maximal outlining region. This contains all blocks post-dominated by a
/// sink block, the sink block itself, and all blocks dominated by the sink.
/// If sink-predecessors and sink-successors cannot be extracted in one region,
/// the static constructor returns a list of suitable extraction regions.
class OutliningRegion {
  /// A list of (block, score) pairs. A block's score is non-zero iff it's a
  /// viable sub-region entry point. Blocks with higher scores are better entry
  /// points (i.e. they are more distant ancestors of the sink block).
  SmallVector<BlockTy, 0> Blocks = {};

  /// The suggested entry point into the region. If the region has multiple
  /// entry points, all blocks within the region may not be reachable from this
  /// entry point.
  BasicBlock *SuggestedEntryPoint = nullptr;

  /// Whether the entire function is cold.
  bool EntireFunctionCold = false;

  /// If \p BB is a viable entry point, return \p Score. Return 0 otherwise.
  static unsigned getEntryPointScore(BasicBlock &BB, unsigned Score) {
    return mayExtractBlock(BB) ? Score : 0;
  }

  /// These scores should be lower than the score for predecessor blocks,
  /// because regions starting at predecessor blocks are typically larger.
  static constexpr unsigned ScoreForSuccBlock = 1;
  static constexpr unsigned ScoreForSinkBlock = 1;

  OutliningRegion(const OutliningRegion &) = delete;
  OutliningRegion &operator=(const OutliningRegion &) = delete;

public:
  OutliningRegion() = default;
  OutliningRegion(OutliningRegion &&) = default;
  OutliningRegion &operator=(OutliningRegion &&) = default;

  static std::vector<OutliningRegion> create(BasicBlock &SinkBB,
                                             const DominatorTree &DT,
                                             const PostDominatorTree &PDT) {
    std::vector<OutliningRegion> Regions;
    SmallPtrSet<BasicBlock *, 4> RegionBlocks;

    Regions.emplace_back();
    OutliningRegion *ColdRegion = &Regions.back();

    auto addBlockToRegion = [&](BasicBlock *BB, unsigned Score) {
      RegionBlocks.insert(BB);
      ColdRegion->Blocks.emplace_back(BB, Score);
    };

    // The ancestor farthest-away from SinkBB, and also post-dominated by it.
    unsigned SinkScore = getEntryPointScore(SinkBB, ScoreForSinkBlock);
    ColdRegion->SuggestedEntryPoint = (SinkScore > 0) ? &SinkBB : nullptr;
    unsigned BestScore = SinkScore;

    // Visit SinkBB's ancestors using inverse DFS.
    auto PredIt = ++idf_begin(&SinkBB);
    auto PredEnd = idf_end(&SinkBB);
    while (PredIt != PredEnd) {
      BasicBlock &PredBB = **PredIt;
      bool SinkPostDom = PDT.dominates(&SinkBB, &PredBB);

      // If the predecessor is cold and has no predecessors, the entire
      // function must be cold.
      if (SinkPostDom && pred_empty(&PredBB)) {
        ColdRegion->EntireFunctionCold = true;
        return Regions;
      }

      // If SinkBB does not post-dominate a predecessor, do not mark the
      // predecessor (or any of its predecessors) cold.
      if (!SinkPostDom || !mayExtractBlock(PredBB)) {
        PredIt.skipChildren();
        continue;
      }

      // Keep track of the post-dominated ancestor farthest away from the sink.
      // The path length is always >= 2, ensuring that predecessor blocks are
      // considered as entry points before the sink block.
      unsigned PredScore = getEntryPointScore(PredBB, PredIt.getPathLength());
      if (PredScore > BestScore) {
        ColdRegion->SuggestedEntryPoint = &PredBB;
        BestScore = PredScore;
      }

      addBlockToRegion(&PredBB, PredScore);
      ++PredIt;
    }

    // If the sink can be added to the cold region, do so. It's considered as
    // an entry point before any sink-successor blocks.
    //
    // Otherwise, split cold sink-successor blocks using a separate region.
    // This satisfies the requirement that all extraction blocks other than the
    // first have predecessors within the extraction region.
    if (mayExtractBlock(SinkBB)) {
      addBlockToRegion(&SinkBB, SinkScore);
      if (pred_empty(&SinkBB)) {
        ColdRegion->EntireFunctionCold = true;
        return Regions;
      }
    } else {
      Regions.emplace_back();
      ColdRegion = &Regions.back();
      BestScore = 0;
    }

    // Find all successors of SinkBB dominated by SinkBB using DFS.
    auto SuccIt = ++df_begin(&SinkBB);
    auto SuccEnd = df_end(&SinkBB);
    while (SuccIt != SuccEnd) {
      BasicBlock &SuccBB = **SuccIt;
      bool SinkDom = DT.dominates(&SinkBB, &SuccBB);

      // Don't allow the backwards & forwards DFSes to mark the same block.
      bool DuplicateBlock = RegionBlocks.count(&SuccBB);

      // If SinkBB does not dominate a successor, do not mark the successor (or
      // any of its successors) cold.
      if (DuplicateBlock || !SinkDom || !mayExtractBlock(SuccBB)) {
        SuccIt.skipChildren();
        continue;
      }

      unsigned SuccScore = getEntryPointScore(SuccBB, ScoreForSuccBlock);
      if (SuccScore > BestScore) {
        ColdRegion->SuggestedEntryPoint = &SuccBB;
        BestScore = SuccScore;
      }

      addBlockToRegion(&SuccBB, SuccScore);
      ++SuccIt;
    }

    return Regions;
  }

  /// Whether this region has nothing to extract.
  bool empty() const { return !SuggestedEntryPoint; }

  /// The blocks in this region.
  ArrayRef<std::pair<BasicBlock *, unsigned>> blocks() const { return Blocks; }

  /// Whether the entire function containing this region is cold.
  bool isEntireFunctionCold() const { return EntireFunctionCold; }

  /// Remove a sub-region from this region and return it as a block sequence.
  BlockSequence takeSingleEntrySubRegion(DominatorTree &DT) {
    assert(!empty() && !isEntireFunctionCold() && "Nothing to extract");

    // Remove blocks dominated by the suggested entry point from this region.
    // During the removal, identify the next best entry point into the region.
    // Ensure that the first extracted block is the suggested entry point.
    BlockSequence SubRegion = {SuggestedEntryPoint};
    BasicBlock *NextEntryPoint = nullptr;
    unsigned NextScore = 0;
    auto RegionEndIt = Blocks.end();
    auto RegionStartIt = remove_if(Blocks, [&](const BlockTy &Block) {
      BasicBlock *BB = Block.first;
      unsigned Score = Block.second;
      bool InSubRegion =
          BB == SuggestedEntryPoint || DT.dominates(SuggestedEntryPoint, BB);
      if (!InSubRegion && Score > NextScore) {
        NextEntryPoint = BB;
        NextScore = Score;
      }
      if (InSubRegion && BB != SuggestedEntryPoint)
        SubRegion.push_back(BB);
      return InSubRegion;
    });
    Blocks.erase(RegionStartIt, RegionEndIt);

    // Update the suggested entry point.
    SuggestedEntryPoint = NextEntryPoint;

    return SubRegion;
  }
};
} // namespace

bool HotColdSplitting::outlineColdRegions(Function &F, bool HasProfileSummary) {
  // The set of cold blocks outlined.
  SmallPtrSet<BasicBlock *, 4> ColdBlocks;

  // The set of cold blocks cannot be outlined.
  SmallPtrSet<BasicBlock *, 4> CannotBeOutlinedColdBlocks;

  // Set of cold blocks obtained with RPOT.
  SmallPtrSet<BasicBlock *, 4> AnnotatedColdBlocks;

  // The worklist of non-intersecting regions left to outline. The first member
  // of the pair is the entry point into the region to be outlined.
  SmallVector<std::pair<BasicBlock *, CodeExtractor>, 2> OutliningWorklist;

  // Set up an RPO traversal. Experimentally, this performs better (outlines
  // more) than a PO traversal, because we prevent region overlap by keeping
  // the first region to contain a block.
  ReversePostOrderTraversal<Function *> RPOT(&F);

  // Calculate domtrees lazily. This reduces compile-time significantly.
  std::unique_ptr<DominatorTree> DT;
  std::unique_ptr<PostDominatorTree> PDT;

  // Calculate BFI lazily (it's only used to query ProfileSummaryInfo). This
  // reduces compile-time significantly. TODO: When we *do* use BFI, we should
  // be able to salvage its domtrees instead of recomputing them.
  BlockFrequencyInfo *BFI = nullptr;
  if (HasProfileSummary)
    BFI = GetBFI(F);

  TargetTransformInfo &TTI = GetTTI(F);
  OptimizationRemarkEmitter &ORE = (*GetORE)(F);
  AssumptionCache *AC = LookupAC(F);
  auto ColdProbThresh = TTI.getPredictableBranchThreshold().getCompl();

  if (ColdBranchProbDenom.getNumOccurrences())
    ColdProbThresh = BranchProbability(1, ColdBranchProbDenom.getValue());

  unsigned OutlinedFunctionID = 1;
  // Find all cold regions.
  for (BasicBlock *BB : RPOT) {
    // This block is already part of some outlining region.
    if (ColdBlocks.count(BB))
      continue;

    // This block is already part of some region cannot be outlined.
    if (CannotBeOutlinedColdBlocks.count(BB))
      continue;

    if (!isBasicBlockCold(BB, ColdProbThresh, AnnotatedColdBlocks, BFI))
      continue;

    LLVM_DEBUG({
      dbgs() << "Found a cold block:\n";
      BB->dump();
    });

    if (!DT)
      DT = std::make_unique<DominatorTree>(F);
    if (!PDT)
      PDT = std::make_unique<PostDominatorTree>(F);

    auto Regions = OutliningRegion::create(*BB, *DT, *PDT);
    for (OutliningRegion &Region : Regions) {
      if (Region.empty())
        continue;

      if (Region.isEntireFunctionCold()) {
        LLVM_DEBUG(dbgs() << "Entire function is cold\n");
        return markFunctionCold(F);
      }

      do {
        BlockSequence SubRegion = Region.takeSingleEntrySubRegion(*DT);
        LLVM_DEBUG({
          dbgs() << "Hot/cold splitting attempting to outline these blocks:\n";
          for (BasicBlock *BB : SubRegion)
            BB->dump();
        });

        // TODO: Pass BFI and BPI to update profile information.
        CodeExtractor CE(
            SubRegion, &*DT, /* AggregateArgs */ false, /* BFI */ nullptr,
            /* BPI */ nullptr, AC, /* AllowVarArgs */ false,
            /* AllowAlloca */ false, /* AllocaBlock */ nullptr,
            /* Suffix */ "cold." + std::to_string(OutlinedFunctionID));

        if (CE.isEligible() && isSplittingBeneficial(CE, SubRegion, TTI) &&
            // If this outlining region intersects with another, drop the new
            // region.
            //
            // TODO: It's theoretically possible to outline more by only keeping
            // the largest region which contains a block, but the extra
            // bookkeeping to do this is tricky/expensive.
            none_of(SubRegion, [&](BasicBlock *Block) {
              return ColdBlocks.contains(Block);
            })) {
          ColdBlocks.insert(SubRegion.begin(), SubRegion.end());

          LLVM_DEBUG({
            for (auto *Block : SubRegion)
              dbgs() << "  contains cold block:" << Block->getName() << "\n";
          });

          OutliningWorklist.emplace_back(
              std::make_pair(SubRegion[0], std::move(CE)));
          ++OutlinedFunctionID;
        } else {
          // The cold block region cannot be outlined.
          for (auto *Block : SubRegion)
            if ((DT->dominates(BB, Block) && PDT->dominates(Block, BB)) ||
                (PDT->dominates(BB, Block) && DT->dominates(Block, BB)))
              // Will skip this cold block in the loop to save the compile time
              CannotBeOutlinedColdBlocks.insert(Block);
        }
      } while (!Region.empty());

      ++NumColdRegionsFound;
    }
  }

  if (OutliningWorklist.empty())
    return false;

  // Outline single-entry cold regions, splitting up larger regions as needed.
  // Cache and recycle the CodeExtractor analysis to avoid O(n^2) compile-time.
  CodeExtractorAnalysisCache CEAC(F);
  for (auto &BCE : OutliningWorklist) {
    Function *Outlined =
        extractColdRegion(*BCE.first, BCE.second, CEAC, BFI, TTI, ORE);
    assert(Outlined && "Should be outlined");
    (void)Outlined;
  }

  return true;
}

bool HotColdSplitting::run(Module &M) {
  bool Changed = false;
  bool HasProfileSummary = (M.getProfileSummary(/* IsCS */ false) != nullptr);
  for (Function &F : M) {
    // Do not touch declarations.
    if (F.isDeclaration())
      continue;

    // Do not modify `optnone` functions.
    if (F.hasOptNone())
      continue;

    // Detect inherently cold functions and mark them as such.
    if (isFunctionCold(F)) {
      Changed |= markFunctionCold(F);
      continue;
    }

    if (!shouldOutlineFrom(F)) {
      LLVM_DEBUG(llvm::dbgs() << "Skipping " << F.getName() << "\n");
      continue;
    }

    LLVM_DEBUG(llvm::dbgs() << "Outlining in " << F.getName() << "\n");
    Changed |= outlineColdRegions(F, HasProfileSummary);
  }
  return Changed;
}

PreservedAnalyses
HotColdSplittingPass::run(Module &M, ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto LookupAC = [&FAM](Function &F) -> AssumptionCache * {
    return FAM.getCachedResult<AssumptionAnalysis>(F);
  };

  auto GBFI = [&FAM](Function &F) {
    return &FAM.getResult<BlockFrequencyAnalysis>(F);
  };

  std::function<TargetTransformInfo &(Function &)> GTTI =
      [&FAM](Function &F) -> TargetTransformInfo & {
    return FAM.getResult<TargetIRAnalysis>(F);
  };

  std::unique_ptr<OptimizationRemarkEmitter> ORE;
  std::function<OptimizationRemarkEmitter &(Function &)> GetORE =
      [&ORE](Function &F) -> OptimizationRemarkEmitter & {
    ORE.reset(new OptimizationRemarkEmitter(&F));
    return *ORE;
  };

  ProfileSummaryInfo *PSI = &AM.getResult<ProfileSummaryAnalysis>(M);

  if (HotColdSplitting(PSI, GBFI, GTTI, &GetORE, LookupAC).run(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
