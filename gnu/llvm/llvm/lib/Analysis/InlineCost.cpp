//===- InlineCost.cpp - Cost analysis for inliner -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements inline cost analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/InlineCost.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include <climits>
#include <limits>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "inline-cost"

STATISTIC(NumCallsAnalyzed, "Number of call sites analyzed");

static cl::opt<int>
    DefaultThreshold("inlinedefault-threshold", cl::Hidden, cl::init(225),
                     cl::desc("Default amount of inlining to perform"));

// We introduce this option since there is a minor compile-time win by avoiding
// addition of TTI attributes (target-features in particular) to inline
// candidates when they are guaranteed to be the same as top level methods in
// some use cases. If we avoid adding the attribute, we need an option to avoid
// checking these attributes.
static cl::opt<bool> IgnoreTTIInlineCompatible(
    "ignore-tti-inline-compatible", cl::Hidden, cl::init(false),
    cl::desc("Ignore TTI attributes compatibility check between callee/caller "
             "during inline cost calculation"));

static cl::opt<bool> PrintInstructionComments(
    "print-instruction-comments", cl::Hidden, cl::init(false),
    cl::desc("Prints comments for instruction based on inline cost analysis"));

static cl::opt<int> InlineThreshold(
    "inline-threshold", cl::Hidden, cl::init(225),
    cl::desc("Control the amount of inlining to perform (default = 225)"));

static cl::opt<int> HintThreshold(
    "inlinehint-threshold", cl::Hidden, cl::init(325),
    cl::desc("Threshold for inlining functions with inline hint"));

static cl::opt<int>
    ColdCallSiteThreshold("inline-cold-callsite-threshold", cl::Hidden,
                          cl::init(45),
                          cl::desc("Threshold for inlining cold callsites"));

static cl::opt<bool> InlineEnableCostBenefitAnalysis(
    "inline-enable-cost-benefit-analysis", cl::Hidden, cl::init(false),
    cl::desc("Enable the cost-benefit analysis for the inliner"));

// InlineSavingsMultiplier overrides per TTI multipliers iff it is
// specified explicitly in command line options. This option is exposed
// for tuning and testing.
static cl::opt<int> InlineSavingsMultiplier(
    "inline-savings-multiplier", cl::Hidden, cl::init(8),
    cl::desc("Multiplier to multiply cycle savings by during inlining"));

// InlineSavingsProfitableMultiplier overrides per TTI multipliers iff it is
// specified explicitly in command line options. This option is exposed
// for tuning and testing.
static cl::opt<int> InlineSavingsProfitableMultiplier(
    "inline-savings-profitable-multiplier", cl::Hidden, cl::init(4),
    cl::desc("A multiplier on top of cycle savings to decide whether the "
             "savings won't justify the cost"));

static cl::opt<int>
    InlineSizeAllowance("inline-size-allowance", cl::Hidden, cl::init(100),
                        cl::desc("The maximum size of a callee that get's "
                                 "inlined without sufficient cycle savings"));

// We introduce this threshold to help performance of instrumentation based
// PGO before we actually hook up inliner with analysis passes such as BPI and
// BFI.
static cl::opt<int> ColdThreshold(
    "inlinecold-threshold", cl::Hidden, cl::init(45),
    cl::desc("Threshold for inlining functions with cold attribute"));

static cl::opt<int>
    HotCallSiteThreshold("hot-callsite-threshold", cl::Hidden, cl::init(3000),
                         cl::desc("Threshold for hot callsites "));

static cl::opt<int> LocallyHotCallSiteThreshold(
    "locally-hot-callsite-threshold", cl::Hidden, cl::init(525),
    cl::desc("Threshold for locally hot callsites "));

static cl::opt<int> ColdCallSiteRelFreq(
    "cold-callsite-rel-freq", cl::Hidden, cl::init(2),
    cl::desc("Maximum block frequency, expressed as a percentage of caller's "
             "entry frequency, for a callsite to be cold in the absence of "
             "profile information."));

static cl::opt<uint64_t> HotCallSiteRelFreq(
    "hot-callsite-rel-freq", cl::Hidden, cl::init(60),
    cl::desc("Minimum block frequency, expressed as a multiple of caller's "
             "entry frequency, for a callsite to be hot in the absence of "
             "profile information."));

static cl::opt<int>
    InstrCost("inline-instr-cost", cl::Hidden, cl::init(5),
              cl::desc("Cost of a single instruction when inlining"));

static cl::opt<int>
    MemAccessCost("inline-memaccess-cost", cl::Hidden, cl::init(0),
                  cl::desc("Cost of load/store instruction when inlining"));

static cl::opt<int> CallPenalty(
    "inline-call-penalty", cl::Hidden, cl::init(25),
    cl::desc("Call penalty that is applied per callsite when inlining"));

static cl::opt<size_t>
    StackSizeThreshold("inline-max-stacksize", cl::Hidden,
                       cl::init(std::numeric_limits<size_t>::max()),
                       cl::desc("Do not inline functions with a stack size "
                                "that exceeds the specified limit"));

static cl::opt<size_t> RecurStackSizeThreshold(
    "recursive-inline-max-stacksize", cl::Hidden,
    cl::init(InlineConstants::TotalAllocaSizeRecursiveCaller),
    cl::desc("Do not inline recursive functions with a stack "
             "size that exceeds the specified limit"));

static cl::opt<bool> OptComputeFullInlineCost(
    "inline-cost-full", cl::Hidden,
    cl::desc("Compute the full inline cost of a call site even when the cost "
             "exceeds the threshold."));

static cl::opt<bool> InlineCallerSupersetNoBuiltin(
    "inline-caller-superset-nobuiltin", cl::Hidden, cl::init(true),
    cl::desc("Allow inlining when caller has a superset of callee's nobuiltin "
             "attributes."));

static cl::opt<bool> DisableGEPConstOperand(
    "disable-gep-const-evaluation", cl::Hidden, cl::init(false),
    cl::desc("Disables evaluation of GetElementPtr with constant operands"));

namespace llvm {
std::optional<int> getStringFnAttrAsInt(const Attribute &Attr) {
  if (Attr.isValid()) {
    int AttrValue = 0;
    if (!Attr.getValueAsString().getAsInteger(10, AttrValue))
      return AttrValue;
  }
  return std::nullopt;
}

std::optional<int> getStringFnAttrAsInt(CallBase &CB, StringRef AttrKind) {
  return getStringFnAttrAsInt(CB.getFnAttr(AttrKind));
}

std::optional<int> getStringFnAttrAsInt(Function *F, StringRef AttrKind) {
  return getStringFnAttrAsInt(F->getFnAttribute(AttrKind));
}

namespace InlineConstants {
int getInstrCost() { return InstrCost; }

} // namespace InlineConstants

} // namespace llvm

namespace {
class InlineCostCallAnalyzer;

// This struct is used to store information about inline cost of a
// particular instruction
struct InstructionCostDetail {
  int CostBefore = 0;
  int CostAfter = 0;
  int ThresholdBefore = 0;
  int ThresholdAfter = 0;

  int getThresholdDelta() const { return ThresholdAfter - ThresholdBefore; }

  int getCostDelta() const { return CostAfter - CostBefore; }

  bool hasThresholdChanged() const { return ThresholdAfter != ThresholdBefore; }
};

class InlineCostAnnotationWriter : public AssemblyAnnotationWriter {
private:
  InlineCostCallAnalyzer *const ICCA;

public:
  InlineCostAnnotationWriter(InlineCostCallAnalyzer *ICCA) : ICCA(ICCA) {}
  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &OS) override;
};

/// Carry out call site analysis, in order to evaluate inlinability.
/// NOTE: the type is currently used as implementation detail of functions such
/// as llvm::getInlineCost. Note the function_ref constructor parameters - the
/// expectation is that they come from the outer scope, from the wrapper
/// functions. If we want to support constructing CallAnalyzer objects where
/// lambdas are provided inline at construction, or where the object needs to
/// otherwise survive past the scope of the provided functions, we need to
/// revisit the argument types.
class CallAnalyzer : public InstVisitor<CallAnalyzer, bool> {
  typedef InstVisitor<CallAnalyzer, bool> Base;
  friend class InstVisitor<CallAnalyzer, bool>;

protected:
  virtual ~CallAnalyzer() = default;
  /// The TargetTransformInfo available for this compilation.
  const TargetTransformInfo &TTI;

  /// Getter for the cache of @llvm.assume intrinsics.
  function_ref<AssumptionCache &(Function &)> GetAssumptionCache;

  /// Getter for BlockFrequencyInfo
  function_ref<BlockFrequencyInfo &(Function &)> GetBFI;

  /// Profile summary information.
  ProfileSummaryInfo *PSI;

  /// The called function.
  Function &F;

  // Cache the DataLayout since we use it a lot.
  const DataLayout &DL;

  /// The OptimizationRemarkEmitter available for this compilation.
  OptimizationRemarkEmitter *ORE;

  /// The candidate callsite being analyzed. Please do not use this to do
  /// analysis in the caller function; we want the inline cost query to be
  /// easily cacheable. Instead, use the cover function paramHasAttr.
  CallBase &CandidateCall;

  /// Extension points for handling callsite features.
  // Called before a basic block was analyzed.
  virtual void onBlockStart(const BasicBlock *BB) {}

  /// Called after a basic block was analyzed.
  virtual void onBlockAnalyzed(const BasicBlock *BB) {}

  /// Called before an instruction was analyzed
  virtual void onInstructionAnalysisStart(const Instruction *I) {}

  /// Called after an instruction was analyzed
  virtual void onInstructionAnalysisFinish(const Instruction *I) {}

  /// Called at the end of the analysis of the callsite. Return the outcome of
  /// the analysis, i.e. 'InlineResult(true)' if the inlining may happen, or
  /// the reason it can't.
  virtual InlineResult finalizeAnalysis() { return InlineResult::success(); }
  /// Called when we're about to start processing a basic block, and every time
  /// we are done processing an instruction. Return true if there is no point in
  /// continuing the analysis (e.g. we've determined already the call site is
  /// too expensive to inline)
  virtual bool shouldStop() { return false; }

  /// Called before the analysis of the callee body starts (with callsite
  /// contexts propagated).  It checks callsite-specific information. Return a
  /// reason analysis can't continue if that's the case, or 'true' if it may
  /// continue.
  virtual InlineResult onAnalysisStart() { return InlineResult::success(); }
  /// Called if the analysis engine decides SROA cannot be done for the given
  /// alloca.
  virtual void onDisableSROA(AllocaInst *Arg) {}

  /// Called the analysis engine determines load elimination won't happen.
  virtual void onDisableLoadElimination() {}

  /// Called when we visit a CallBase, before the analysis starts. Return false
  /// to stop further processing of the instruction.
  virtual bool onCallBaseVisitStart(CallBase &Call) { return true; }

  /// Called to account for a call.
  virtual void onCallPenalty() {}

  /// Called to account for a load or store.
  virtual void onMemAccess(){};

  /// Called to account for the expectation the inlining would result in a load
  /// elimination.
  virtual void onLoadEliminationOpportunity() {}

  /// Called to account for the cost of argument setup for the Call in the
  /// callee's body (not the callsite currently under analysis).
  virtual void onCallArgumentSetup(const CallBase &Call) {}

  /// Called to account for a load relative intrinsic.
  virtual void onLoadRelativeIntrinsic() {}

  /// Called to account for a lowered call.
  virtual void onLoweredCall(Function *F, CallBase &Call, bool IsIndirectCall) {
  }

  /// Account for a jump table of given size. Return false to stop further
  /// processing the switch instruction
  virtual bool onJumpTable(unsigned JumpTableSize) { return true; }

  /// Account for a case cluster of given size. Return false to stop further
  /// processing of the instruction.
  virtual bool onCaseCluster(unsigned NumCaseCluster) { return true; }

  /// Called at the end of processing a switch instruction, with the given
  /// number of case clusters.
  virtual void onFinalizeSwitch(unsigned JumpTableSize, unsigned NumCaseCluster,
                                bool DefaultDestUndefined) {}

  /// Called to account for any other instruction not specifically accounted
  /// for.
  virtual void onMissedSimplification() {}

  /// Start accounting potential benefits due to SROA for the given alloca.
  virtual void onInitializeSROAArg(AllocaInst *Arg) {}

  /// Account SROA savings for the AllocaInst value.
  virtual void onAggregateSROAUse(AllocaInst *V) {}

  bool handleSROA(Value *V, bool DoNotDisable) {
    // Check for SROA candidates in comparisons.
    if (auto *SROAArg = getSROAArgForValueOrNull(V)) {
      if (DoNotDisable) {
        onAggregateSROAUse(SROAArg);
        return true;
      }
      disableSROAForArg(SROAArg);
    }
    return false;
  }

  bool IsCallerRecursive = false;
  bool IsRecursiveCall = false;
  bool ExposesReturnsTwice = false;
  bool HasDynamicAlloca = false;
  bool ContainsNoDuplicateCall = false;
  bool HasReturn = false;
  bool HasIndirectBr = false;
  bool HasUninlineableIntrinsic = false;
  bool InitsVargArgs = false;

  /// Number of bytes allocated statically by the callee.
  uint64_t AllocatedSize = 0;
  unsigned NumInstructions = 0;
  unsigned NumVectorInstructions = 0;

  /// While we walk the potentially-inlined instructions, we build up and
  /// maintain a mapping of simplified values specific to this callsite. The
  /// idea is to propagate any special information we have about arguments to
  /// this call through the inlinable section of the function, and account for
  /// likely simplifications post-inlining. The most important aspect we track
  /// is CFG altering simplifications -- when we prove a basic block dead, that
  /// can cause dramatic shifts in the cost of inlining a function.
  DenseMap<Value *, Constant *> SimplifiedValues;

  /// Keep track of the values which map back (through function arguments) to
  /// allocas on the caller stack which could be simplified through SROA.
  DenseMap<Value *, AllocaInst *> SROAArgValues;

  /// Keep track of Allocas for which we believe we may get SROA optimization.
  DenseSet<AllocaInst *> EnabledSROAAllocas;

  /// Keep track of values which map to a pointer base and constant offset.
  DenseMap<Value *, std::pair<Value *, APInt>> ConstantOffsetPtrs;

  /// Keep track of dead blocks due to the constant arguments.
  SmallPtrSet<BasicBlock *, 16> DeadBlocks;

  /// The mapping of the blocks to their known unique successors due to the
  /// constant arguments.
  DenseMap<BasicBlock *, BasicBlock *> KnownSuccessors;

  /// Model the elimination of repeated loads that is expected to happen
  /// whenever we simplify away the stores that would otherwise cause them to be
  /// loads.
  bool EnableLoadElimination = true;

  /// Whether we allow inlining for recursive call.
  bool AllowRecursiveCall = false;

  SmallPtrSet<Value *, 16> LoadAddrSet;

  AllocaInst *getSROAArgForValueOrNull(Value *V) const {
    auto It = SROAArgValues.find(V);
    if (It == SROAArgValues.end() || EnabledSROAAllocas.count(It->second) == 0)
      return nullptr;
    return It->second;
  }

  // Custom simplification helper routines.
  bool isAllocaDerivedArg(Value *V);
  void disableSROAForArg(AllocaInst *SROAArg);
  void disableSROA(Value *V);
  void findDeadBlocks(BasicBlock *CurrBB, BasicBlock *NextBB);
  void disableLoadElimination();
  bool isGEPFree(GetElementPtrInst &GEP);
  bool canFoldInboundsGEP(GetElementPtrInst &I);
  bool accumulateGEPOffset(GEPOperator &GEP, APInt &Offset);
  bool simplifyCallSite(Function *F, CallBase &Call);
  bool simplifyInstruction(Instruction &I);
  bool simplifyIntrinsicCallIsConstant(CallBase &CB);
  bool simplifyIntrinsicCallObjectSize(CallBase &CB);
  ConstantInt *stripAndComputeInBoundsConstantOffsets(Value *&V);

  /// Return true if the given argument to the function being considered for
  /// inlining has the given attribute set either at the call site or the
  /// function declaration.  Primarily used to inspect call site specific
  /// attributes since these can be more precise than the ones on the callee
  /// itself.
  bool paramHasAttr(Argument *A, Attribute::AttrKind Attr);

  /// Return true if the given value is known non null within the callee if
  /// inlined through this particular callsite.
  bool isKnownNonNullInCallee(Value *V);

  /// Return true if size growth is allowed when inlining the callee at \p Call.
  bool allowSizeGrowth(CallBase &Call);

  // Custom analysis routines.
  InlineResult analyzeBlock(BasicBlock *BB,
                            SmallPtrSetImpl<const Value *> &EphValues);

  // Disable several entry points to the visitor so we don't accidentally use
  // them by declaring but not defining them here.
  void visit(Module *);
  void visit(Module &);
  void visit(Function *);
  void visit(Function &);
  void visit(BasicBlock *);
  void visit(BasicBlock &);

  // Provide base case for our instruction visit.
  bool visitInstruction(Instruction &I);

  // Our visit overrides.
  bool visitAlloca(AllocaInst &I);
  bool visitPHI(PHINode &I);
  bool visitGetElementPtr(GetElementPtrInst &I);
  bool visitBitCast(BitCastInst &I);
  bool visitPtrToInt(PtrToIntInst &I);
  bool visitIntToPtr(IntToPtrInst &I);
  bool visitCastInst(CastInst &I);
  bool visitCmpInst(CmpInst &I);
  bool visitSub(BinaryOperator &I);
  bool visitBinaryOperator(BinaryOperator &I);
  bool visitFNeg(UnaryOperator &I);
  bool visitLoad(LoadInst &I);
  bool visitStore(StoreInst &I);
  bool visitExtractValue(ExtractValueInst &I);
  bool visitInsertValue(InsertValueInst &I);
  bool visitCallBase(CallBase &Call);
  bool visitReturnInst(ReturnInst &RI);
  bool visitBranchInst(BranchInst &BI);
  bool visitSelectInst(SelectInst &SI);
  bool visitSwitchInst(SwitchInst &SI);
  bool visitIndirectBrInst(IndirectBrInst &IBI);
  bool visitResumeInst(ResumeInst &RI);
  bool visitCleanupReturnInst(CleanupReturnInst &RI);
  bool visitCatchReturnInst(CatchReturnInst &RI);
  bool visitUnreachableInst(UnreachableInst &I);

public:
  CallAnalyzer(Function &Callee, CallBase &Call, const TargetTransformInfo &TTI,
               function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
               function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
               ProfileSummaryInfo *PSI = nullptr,
               OptimizationRemarkEmitter *ORE = nullptr)
      : TTI(TTI), GetAssumptionCache(GetAssumptionCache), GetBFI(GetBFI),
        PSI(PSI), F(Callee), DL(F.getDataLayout()), ORE(ORE),
        CandidateCall(Call) {}

  InlineResult analyze();

  std::optional<Constant *> getSimplifiedValue(Instruction *I) {
    if (SimplifiedValues.contains(I))
      return SimplifiedValues[I];
    return std::nullopt;
  }

  // Keep a bunch of stats about the cost savings found so we can print them
  // out when debugging.
  unsigned NumConstantArgs = 0;
  unsigned NumConstantOffsetPtrArgs = 0;
  unsigned NumAllocaArgs = 0;
  unsigned NumConstantPtrCmps = 0;
  unsigned NumConstantPtrDiffs = 0;
  unsigned NumInstructionsSimplified = 0;

  void dump();
};

// Considering forming a binary search, we should find the number of nodes
// which is same as the number of comparisons when lowered. For a given
// number of clusters, n, we can define a recursive function, f(n), to find
// the number of nodes in the tree. The recursion is :
// f(n) = 1 + f(n/2) + f (n - n/2), when n > 3,
// and f(n) = n, when n <= 3.
// This will lead a binary tree where the leaf should be either f(2) or f(3)
// when n > 3.  So, the number of comparisons from leaves should be n, while
// the number of non-leaf should be :
//   2^(log2(n) - 1) - 1
//   = 2^log2(n) * 2^-1 - 1
//   = n / 2 - 1.
// Considering comparisons from leaf and non-leaf nodes, we can estimate the
// number of comparisons in a simple closed form :
//   n + n / 2 - 1 = n * 3 / 2 - 1
int64_t getExpectedNumberOfCompare(int NumCaseCluster) {
  return 3 * static_cast<int64_t>(NumCaseCluster) / 2 - 1;
}

/// FIXME: if it is necessary to derive from InlineCostCallAnalyzer, note
/// the FIXME in onLoweredCall, when instantiating an InlineCostCallAnalyzer
class InlineCostCallAnalyzer final : public CallAnalyzer {
  const bool ComputeFullInlineCost;
  int LoadEliminationCost = 0;
  /// Bonus to be applied when percentage of vector instructions in callee is
  /// high (see more details in updateThreshold).
  int VectorBonus = 0;
  /// Bonus to be applied when the callee has only one reachable basic block.
  int SingleBBBonus = 0;

  /// Tunable parameters that control the analysis.
  const InlineParams &Params;

  // This DenseMap stores the delta change in cost and threshold after
  // accounting for the given instruction. The map is filled only with the
  // flag PrintInstructionComments on.
  DenseMap<const Instruction *, InstructionCostDetail> InstructionCostDetailMap;

  /// Upper bound for the inlining cost. Bonuses are being applied to account
  /// for speculative "expected profit" of the inlining decision.
  int Threshold = 0;

  /// The amount of StaticBonus applied.
  int StaticBonusApplied = 0;

  /// Attempt to evaluate indirect calls to boost its inline cost.
  const bool BoostIndirectCalls;

  /// Ignore the threshold when finalizing analysis.
  const bool IgnoreThreshold;

  // True if the cost-benefit-analysis-based inliner is enabled.
  const bool CostBenefitAnalysisEnabled;

  /// Inlining cost measured in abstract units, accounts for all the
  /// instructions expected to be executed for a given function invocation.
  /// Instructions that are statically proven to be dead based on call-site
  /// arguments are not counted here.
  int Cost = 0;

  // The cumulative cost at the beginning of the basic block being analyzed.  At
  // the end of analyzing each basic block, "Cost - CostAtBBStart" represents
  // the size of that basic block.
  int CostAtBBStart = 0;

  // The static size of live but cold basic blocks.  This is "static" in the
  // sense that it's not weighted by profile counts at all.
  int ColdSize = 0;

  // Whether inlining is decided by cost-threshold analysis.
  bool DecidedByCostThreshold = false;

  // Whether inlining is decided by cost-benefit analysis.
  bool DecidedByCostBenefit = false;

  // The cost-benefit pair computed by cost-benefit analysis.
  std::optional<CostBenefitPair> CostBenefit;

  bool SingleBB = true;

  unsigned SROACostSavings = 0;
  unsigned SROACostSavingsLost = 0;

  /// The mapping of caller Alloca values to their accumulated cost savings. If
  /// we have to disable SROA for one of the allocas, this tells us how much
  /// cost must be added.
  DenseMap<AllocaInst *, int> SROAArgCosts;

  /// Return true if \p Call is a cold callsite.
  bool isColdCallSite(CallBase &Call, BlockFrequencyInfo *CallerBFI);

  /// Update Threshold based on callsite properties such as callee
  /// attributes and callee hotness for PGO builds. The Callee is explicitly
  /// passed to support analyzing indirect calls whose target is inferred by
  /// analysis.
  void updateThreshold(CallBase &Call, Function &Callee);
  /// Return a higher threshold if \p Call is a hot callsite.
  std::optional<int> getHotCallSiteThreshold(CallBase &Call,
                                             BlockFrequencyInfo *CallerBFI);

  /// Handle a capped 'int' increment for Cost.
  void addCost(int64_t Inc) {
    Inc = std::clamp<int64_t>(Inc, INT_MIN, INT_MAX);
    Cost = std::clamp<int64_t>(Inc + Cost, INT_MIN, INT_MAX);
  }

  void onDisableSROA(AllocaInst *Arg) override {
    auto CostIt = SROAArgCosts.find(Arg);
    if (CostIt == SROAArgCosts.end())
      return;
    addCost(CostIt->second);
    SROACostSavings -= CostIt->second;
    SROACostSavingsLost += CostIt->second;
    SROAArgCosts.erase(CostIt);
  }

  void onDisableLoadElimination() override {
    addCost(LoadEliminationCost);
    LoadEliminationCost = 0;
  }

  bool onCallBaseVisitStart(CallBase &Call) override {
    if (std::optional<int> AttrCallThresholdBonus =
            getStringFnAttrAsInt(Call, "call-threshold-bonus"))
      Threshold += *AttrCallThresholdBonus;

    if (std::optional<int> AttrCallCost =
            getStringFnAttrAsInt(Call, "call-inline-cost")) {
      addCost(*AttrCallCost);
      // Prevent further processing of the call since we want to override its
      // inline cost, not just add to it.
      return false;
    }
    return true;
  }

  void onCallPenalty() override { addCost(CallPenalty); }

  void onMemAccess() override { addCost(MemAccessCost); }

  void onCallArgumentSetup(const CallBase &Call) override {
    // Pay the price of the argument setup. We account for the average 1
    // instruction per call argument setup here.
    addCost(Call.arg_size() * InstrCost);
  }
  void onLoadRelativeIntrinsic() override {
    // This is normally lowered to 4 LLVM instructions.
    addCost(3 * InstrCost);
  }
  void onLoweredCall(Function *F, CallBase &Call,
                     bool IsIndirectCall) override {
    // We account for the average 1 instruction per call argument setup here.
    addCost(Call.arg_size() * InstrCost);

    // If we have a constant that we are calling as a function, we can peer
    // through it and see the function target. This happens not infrequently
    // during devirtualization and so we want to give it a hefty bonus for
    // inlining, but cap that bonus in the event that inlining wouldn't pan out.
    // Pretend to inline the function, with a custom threshold.
    if (IsIndirectCall && BoostIndirectCalls) {
      auto IndirectCallParams = Params;
      IndirectCallParams.DefaultThreshold =
          InlineConstants::IndirectCallThreshold;
      /// FIXME: if InlineCostCallAnalyzer is derived from, this may need
      /// to instantiate the derived class.
      InlineCostCallAnalyzer CA(*F, Call, IndirectCallParams, TTI,
                                GetAssumptionCache, GetBFI, PSI, ORE, false);
      if (CA.analyze().isSuccess()) {
        // We were able to inline the indirect call! Subtract the cost from the
        // threshold to get the bonus we want to apply, but don't go below zero.
        Cost -= std::max(0, CA.getThreshold() - CA.getCost());
      }
    } else
      // Otherwise simply add the cost for merely making the call.
      addCost(TTI.getInlineCallPenalty(CandidateCall.getCaller(), Call,
                                       CallPenalty));
  }

  void onFinalizeSwitch(unsigned JumpTableSize, unsigned NumCaseCluster,
                        bool DefaultDestUndefined) override {
    // If suitable for a jump table, consider the cost for the table size and
    // branch to destination.
    // Maximum valid cost increased in this function.
    if (JumpTableSize) {
      // Suppose a default branch includes one compare and one conditional
      // branch if it's reachable.
      if (!DefaultDestUndefined)
        addCost(2 * InstrCost);
      // Suppose a jump table requires one load and one jump instruction.
      int64_t JTCost =
          static_cast<int64_t>(JumpTableSize) * InstrCost + 2 * InstrCost;
      addCost(JTCost);
      return;
    }

    if (NumCaseCluster <= 3) {
      // Suppose a comparison includes one compare and one conditional branch.
      // We can reduce a set of instructions if the default branch is
      // undefined.
      addCost((NumCaseCluster - DefaultDestUndefined) * 2 * InstrCost);
      return;
    }

    int64_t ExpectedNumberOfCompare =
        getExpectedNumberOfCompare(NumCaseCluster);
    int64_t SwitchCost = ExpectedNumberOfCompare * 2 * InstrCost;

    addCost(SwitchCost);
  }
  void onMissedSimplification() override { addCost(InstrCost); }

  void onInitializeSROAArg(AllocaInst *Arg) override {
    assert(Arg != nullptr &&
           "Should not initialize SROA costs for null value.");
    auto SROAArgCost = TTI.getCallerAllocaCost(&CandidateCall, Arg);
    SROACostSavings += SROAArgCost;
    SROAArgCosts[Arg] = SROAArgCost;
  }

  void onAggregateSROAUse(AllocaInst *SROAArg) override {
    auto CostIt = SROAArgCosts.find(SROAArg);
    assert(CostIt != SROAArgCosts.end() &&
           "expected this argument to have a cost");
    CostIt->second += InstrCost;
    SROACostSavings += InstrCost;
  }

  void onBlockStart(const BasicBlock *BB) override { CostAtBBStart = Cost; }

  void onBlockAnalyzed(const BasicBlock *BB) override {
    if (CostBenefitAnalysisEnabled) {
      // Keep track of the static size of live but cold basic blocks.  For now,
      // we define a cold basic block to be one that's never executed.
      assert(GetBFI && "GetBFI must be available");
      BlockFrequencyInfo *BFI = &(GetBFI(F));
      assert(BFI && "BFI must be available");
      auto ProfileCount = BFI->getBlockProfileCount(BB);
      if (*ProfileCount == 0)
        ColdSize += Cost - CostAtBBStart;
    }

    auto *TI = BB->getTerminator();
    // If we had any successors at this point, than post-inlining is likely to
    // have them as well. Note that we assume any basic blocks which existed
    // due to branches or switches which folded above will also fold after
    // inlining.
    if (SingleBB && TI->getNumSuccessors() > 1) {
      // Take off the bonus we applied to the threshold.
      Threshold -= SingleBBBonus;
      SingleBB = false;
    }
  }

  void onInstructionAnalysisStart(const Instruction *I) override {
    // This function is called to store the initial cost of inlining before
    // the given instruction was assessed.
    if (!PrintInstructionComments)
      return;
    InstructionCostDetailMap[I].CostBefore = Cost;
    InstructionCostDetailMap[I].ThresholdBefore = Threshold;
  }

  void onInstructionAnalysisFinish(const Instruction *I) override {
    // This function is called to find new values of cost and threshold after
    // the instruction has been assessed.
    if (!PrintInstructionComments)
      return;
    InstructionCostDetailMap[I].CostAfter = Cost;
    InstructionCostDetailMap[I].ThresholdAfter = Threshold;
  }

  bool isCostBenefitAnalysisEnabled() {
    if (!PSI || !PSI->hasProfileSummary())
      return false;

    if (!GetBFI)
      return false;

    if (InlineEnableCostBenefitAnalysis.getNumOccurrences()) {
      // Honor the explicit request from the user.
      if (!InlineEnableCostBenefitAnalysis)
        return false;
    } else {
      // Otherwise, require instrumentation profile.
      if (!PSI->hasInstrumentationProfile())
        return false;
    }

    auto *Caller = CandidateCall.getParent()->getParent();
    if (!Caller->getEntryCount())
      return false;

    BlockFrequencyInfo *CallerBFI = &(GetBFI(*Caller));
    if (!CallerBFI)
      return false;

    // For now, limit to hot call site.
    if (!PSI->isHotCallSite(CandidateCall, CallerBFI))
      return false;

    // Make sure we have a nonzero entry count.
    auto EntryCount = F.getEntryCount();
    if (!EntryCount || !EntryCount->getCount())
      return false;

    BlockFrequencyInfo *CalleeBFI = &(GetBFI(F));
    if (!CalleeBFI)
      return false;

    return true;
  }

  // A helper function to choose between command line override and default.
  unsigned getInliningCostBenefitAnalysisSavingsMultiplier() const {
    if (InlineSavingsMultiplier.getNumOccurrences())
      return InlineSavingsMultiplier;
    return TTI.getInliningCostBenefitAnalysisSavingsMultiplier();
  }

  // A helper function to choose between command line override and default.
  unsigned getInliningCostBenefitAnalysisProfitableMultiplier() const {
    if (InlineSavingsProfitableMultiplier.getNumOccurrences())
      return InlineSavingsProfitableMultiplier;
    return TTI.getInliningCostBenefitAnalysisProfitableMultiplier();
  }

  void OverrideCycleSavingsAndSizeForTesting(APInt &CycleSavings, int &Size) {
    if (std::optional<int> AttrCycleSavings = getStringFnAttrAsInt(
            CandidateCall, "inline-cycle-savings-for-test")) {
      CycleSavings = *AttrCycleSavings;
    }

    if (std::optional<int> AttrRuntimeCost = getStringFnAttrAsInt(
            CandidateCall, "inline-runtime-cost-for-test")) {
      Size = *AttrRuntimeCost;
    }
  }

  // Determine whether we should inline the given call site, taking into account
  // both the size cost and the cycle savings.  Return std::nullopt if we don't
  // have sufficient profiling information to determine.
  std::optional<bool> costBenefitAnalysis() {
    if (!CostBenefitAnalysisEnabled)
      return std::nullopt;

    // buildInlinerPipeline in the pass builder sets HotCallSiteThreshold to 0
    // for the prelink phase of the AutoFDO + ThinLTO build.  Honor the logic by
    // falling back to the cost-based metric.
    // TODO: Improve this hacky condition.
    if (Threshold == 0)
      return std::nullopt;

    assert(GetBFI);
    BlockFrequencyInfo *CalleeBFI = &(GetBFI(F));
    assert(CalleeBFI);

    // The cycle savings expressed as the sum of InstrCost
    // multiplied by the estimated dynamic count of each instruction we can
    // avoid.  Savings come from the call site cost, such as argument setup and
    // the call instruction, as well as the instructions that are folded.
    //
    // We use 128-bit APInt here to avoid potential overflow.  This variable
    // should stay well below 10^^24 (or 2^^80) in practice.  This "worst" case
    // assumes that we can avoid or fold a billion instructions, each with a
    // profile count of 10^^15 -- roughly the number of cycles for a 24-hour
    // period on a 4GHz machine.
    APInt CycleSavings(128, 0);

    for (auto &BB : F) {
      APInt CurrentSavings(128, 0);
      for (auto &I : BB) {
        if (BranchInst *BI = dyn_cast<BranchInst>(&I)) {
          // Count a conditional branch as savings if it becomes unconditional.
          if (BI->isConditional() &&
              isa_and_nonnull<ConstantInt>(
                  SimplifiedValues.lookup(BI->getCondition()))) {
            CurrentSavings += InstrCost;
          }
        } else if (SwitchInst *SI = dyn_cast<SwitchInst>(&I)) {
          if (isa_and_present<ConstantInt>(SimplifiedValues.lookup(SI->getCondition())))
            CurrentSavings += InstrCost;
        } else if (Value *V = dyn_cast<Value>(&I)) {
          // Count an instruction as savings if we can fold it.
          if (SimplifiedValues.count(V)) {
            CurrentSavings += InstrCost;
          }
        }
      }

      auto ProfileCount = CalleeBFI->getBlockProfileCount(&BB);
      CurrentSavings *= *ProfileCount;
      CycleSavings += CurrentSavings;
    }

    // Compute the cycle savings per call.
    auto EntryProfileCount = F.getEntryCount();
    assert(EntryProfileCount && EntryProfileCount->getCount());
    auto EntryCount = EntryProfileCount->getCount();
    CycleSavings += EntryCount / 2;
    CycleSavings = CycleSavings.udiv(EntryCount);

    // Compute the total savings for the call site.
    auto *CallerBB = CandidateCall.getParent();
    BlockFrequencyInfo *CallerBFI = &(GetBFI(*(CallerBB->getParent())));
    CycleSavings += getCallsiteCost(TTI, this->CandidateCall, DL);
    CycleSavings *= *CallerBFI->getBlockProfileCount(CallerBB);

    // Remove the cost of the cold basic blocks to model the runtime cost more
    // accurately. Both machine block placement and function splitting could
    // place cold blocks further from hot blocks.
    int Size = Cost - ColdSize;

    // Allow tiny callees to be inlined regardless of whether they meet the
    // savings threshold.
    Size = Size > InlineSizeAllowance ? Size - InlineSizeAllowance : 1;

    OverrideCycleSavingsAndSizeForTesting(CycleSavings, Size);
    CostBenefit.emplace(APInt(128, Size), CycleSavings);

    // Let R be the ratio of CycleSavings to Size.  We accept the inlining
    // opportunity if R is really high and reject if R is really low.  If R is
    // somewhere in the middle, we fall back to the cost-based analysis.
    //
    // Specifically, let R = CycleSavings / Size, we accept the inlining
    // opportunity if:
    //
    //             PSI->getOrCompHotCountThreshold()
    // R > -------------------------------------------------
    //     getInliningCostBenefitAnalysisSavingsMultiplier()
    //
    // and reject the inlining opportunity if:
    //
    //                PSI->getOrCompHotCountThreshold()
    // R <= ----------------------------------------------------
    //      getInliningCostBenefitAnalysisProfitableMultiplier()
    //
    // Otherwise, we fall back to the cost-based analysis.
    //
    // Implementation-wise, use multiplication (CycleSavings * Multiplier,
    // HotCountThreshold * Size) rather than division to avoid precision loss.
    APInt Threshold(128, PSI->getOrCompHotCountThreshold());
    Threshold *= Size;

    APInt UpperBoundCycleSavings = CycleSavings;
    UpperBoundCycleSavings *= getInliningCostBenefitAnalysisSavingsMultiplier();
    if (UpperBoundCycleSavings.uge(Threshold))
      return true;

    APInt LowerBoundCycleSavings = CycleSavings;
    LowerBoundCycleSavings *=
        getInliningCostBenefitAnalysisProfitableMultiplier();
    if (LowerBoundCycleSavings.ult(Threshold))
      return false;

    // Otherwise, fall back to the cost-based analysis.
    return std::nullopt;
  }

  InlineResult finalizeAnalysis() override {
    // Loops generally act a lot like calls in that they act like barriers to
    // movement, require a certain amount of setup, etc. So when optimising for
    // size, we penalise any call sites that perform loops. We do this after all
    // other costs here, so will likely only be dealing with relatively small
    // functions (and hence DT and LI will hopefully be cheap).
    auto *Caller = CandidateCall.getFunction();
    if (Caller->hasMinSize()) {
      DominatorTree DT(F);
      LoopInfo LI(DT);
      int NumLoops = 0;
      for (Loop *L : LI) {
        // Ignore loops that will not be executed
        if (DeadBlocks.count(L->getHeader()))
          continue;
        NumLoops++;
      }
      addCost(NumLoops * InlineConstants::LoopPenalty);
    }

    // We applied the maximum possible vector bonus at the beginning. Now,
    // subtract the excess bonus, if any, from the Threshold before
    // comparing against Cost.
    if (NumVectorInstructions <= NumInstructions / 10)
      Threshold -= VectorBonus;
    else if (NumVectorInstructions <= NumInstructions / 2)
      Threshold -= VectorBonus / 2;

    if (std::optional<int> AttrCost =
            getStringFnAttrAsInt(CandidateCall, "function-inline-cost"))
      Cost = *AttrCost;

    if (std::optional<int> AttrCostMult = getStringFnAttrAsInt(
            CandidateCall,
            InlineConstants::FunctionInlineCostMultiplierAttributeName))
      Cost *= *AttrCostMult;

    if (std::optional<int> AttrThreshold =
            getStringFnAttrAsInt(CandidateCall, "function-inline-threshold"))
      Threshold = *AttrThreshold;

    if (auto Result = costBenefitAnalysis()) {
      DecidedByCostBenefit = true;
      if (*Result)
        return InlineResult::success();
      else
        return InlineResult::failure("Cost over threshold.");
    }

    if (IgnoreThreshold)
      return InlineResult::success();

    DecidedByCostThreshold = true;
    return Cost < std::max(1, Threshold)
               ? InlineResult::success()
               : InlineResult::failure("Cost over threshold.");
  }

  bool shouldStop() override {
    if (IgnoreThreshold || ComputeFullInlineCost)
      return false;
    // Bail out the moment we cross the threshold. This means we'll under-count
    // the cost, but only when undercounting doesn't matter.
    if (Cost < Threshold)
      return false;
    DecidedByCostThreshold = true;
    return true;
  }

  void onLoadEliminationOpportunity() override {
    LoadEliminationCost += InstrCost;
  }

  InlineResult onAnalysisStart() override {
    // Perform some tweaks to the cost and threshold based on the direct
    // callsite information.

    // We want to more aggressively inline vector-dense kernels, so up the
    // threshold, and we'll lower it if the % of vector instructions gets too
    // low. Note that these bonuses are some what arbitrary and evolved over
    // time by accident as much as because they are principled bonuses.
    //
    // FIXME: It would be nice to remove all such bonuses. At least it would be
    // nice to base the bonus values on something more scientific.
    assert(NumInstructions == 0);
    assert(NumVectorInstructions == 0);

    // Update the threshold based on callsite properties
    updateThreshold(CandidateCall, F);

    // While Threshold depends on commandline options that can take negative
    // values, we want to enforce the invariant that the computed threshold and
    // bonuses are non-negative.
    assert(Threshold >= 0);
    assert(SingleBBBonus >= 0);
    assert(VectorBonus >= 0);

    // Speculatively apply all possible bonuses to Threshold. If cost exceeds
    // this Threshold any time, and cost cannot decrease, we can stop processing
    // the rest of the function body.
    Threshold += (SingleBBBonus + VectorBonus);

    // Give out bonuses for the callsite, as the instructions setting them up
    // will be gone after inlining.
    addCost(-getCallsiteCost(TTI, this->CandidateCall, DL));

    // If this function uses the coldcc calling convention, prefer not to inline
    // it.
    if (F.getCallingConv() == CallingConv::Cold)
      Cost += InlineConstants::ColdccPenalty;

    LLVM_DEBUG(dbgs() << "      Initial cost: " << Cost << "\n");

    // Check if we're done. This can happen due to bonuses and penalties.
    if (Cost >= Threshold && !ComputeFullInlineCost)
      return InlineResult::failure("high cost");

    return InlineResult::success();
  }

public:
  InlineCostCallAnalyzer(
      Function &Callee, CallBase &Call, const InlineParams &Params,
      const TargetTransformInfo &TTI,
      function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
      function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
      ProfileSummaryInfo *PSI = nullptr,
      OptimizationRemarkEmitter *ORE = nullptr, bool BoostIndirect = true,
      bool IgnoreThreshold = false)
      : CallAnalyzer(Callee, Call, TTI, GetAssumptionCache, GetBFI, PSI, ORE),
        ComputeFullInlineCost(OptComputeFullInlineCost ||
                              Params.ComputeFullInlineCost || ORE ||
                              isCostBenefitAnalysisEnabled()),
        Params(Params), Threshold(Params.DefaultThreshold),
        BoostIndirectCalls(BoostIndirect), IgnoreThreshold(IgnoreThreshold),
        CostBenefitAnalysisEnabled(isCostBenefitAnalysisEnabled()),
        Writer(this) {
    AllowRecursiveCall = *Params.AllowRecursiveCall;
  }

  /// Annotation Writer for instruction details
  InlineCostAnnotationWriter Writer;

  void dump();

  // Prints the same analysis as dump(), but its definition is not dependent
  // on the build.
  void print(raw_ostream &OS);

  std::optional<InstructionCostDetail> getCostDetails(const Instruction *I) {
    if (InstructionCostDetailMap.contains(I))
      return InstructionCostDetailMap[I];
    return std::nullopt;
  }

  virtual ~InlineCostCallAnalyzer() = default;
  int getThreshold() const { return Threshold; }
  int getCost() const { return Cost; }
  int getStaticBonusApplied() const { return StaticBonusApplied; }
  std::optional<CostBenefitPair> getCostBenefitPair() { return CostBenefit; }
  bool wasDecidedByCostBenefit() const { return DecidedByCostBenefit; }
  bool wasDecidedByCostThreshold() const { return DecidedByCostThreshold; }
};

// Return true if CB is the sole call to local function Callee.
static bool isSoleCallToLocalFunction(const CallBase &CB,
                                      const Function &Callee) {
  return Callee.hasLocalLinkage() && Callee.hasOneLiveUse() &&
         &Callee == CB.getCalledFunction();
}

class InlineCostFeaturesAnalyzer final : public CallAnalyzer {
private:
  InlineCostFeatures Cost = {};

  // FIXME: These constants are taken from the heuristic-based cost visitor.
  // These should be removed entirely in a later revision to avoid reliance on
  // heuristics in the ML inliner.
  static constexpr int JTCostMultiplier = 2;
  static constexpr int CaseClusterCostMultiplier = 2;
  static constexpr int SwitchDefaultDestCostMultiplier = 2;
  static constexpr int SwitchCostMultiplier = 2;

  // FIXME: These are taken from the heuristic-based cost visitor: we should
  // eventually abstract these to the CallAnalyzer to avoid duplication.
  unsigned SROACostSavingOpportunities = 0;
  int VectorBonus = 0;
  int SingleBBBonus = 0;
  int Threshold = 5;

  DenseMap<AllocaInst *, unsigned> SROACosts;

  void increment(InlineCostFeatureIndex Feature, int64_t Delta = 1) {
    Cost[static_cast<size_t>(Feature)] += Delta;
  }

  void set(InlineCostFeatureIndex Feature, int64_t Value) {
    Cost[static_cast<size_t>(Feature)] = Value;
  }

  void onDisableSROA(AllocaInst *Arg) override {
    auto CostIt = SROACosts.find(Arg);
    if (CostIt == SROACosts.end())
      return;

    increment(InlineCostFeatureIndex::sroa_losses, CostIt->second);
    SROACostSavingOpportunities -= CostIt->second;
    SROACosts.erase(CostIt);
  }

  void onDisableLoadElimination() override {
    set(InlineCostFeatureIndex::load_elimination, 1);
  }

  void onCallPenalty() override {
    increment(InlineCostFeatureIndex::call_penalty, CallPenalty);
  }

  void onCallArgumentSetup(const CallBase &Call) override {
    increment(InlineCostFeatureIndex::call_argument_setup,
              Call.arg_size() * InstrCost);
  }

  void onLoadRelativeIntrinsic() override {
    increment(InlineCostFeatureIndex::load_relative_intrinsic, 3 * InstrCost);
  }

  void onLoweredCall(Function *F, CallBase &Call,
                     bool IsIndirectCall) override {
    increment(InlineCostFeatureIndex::lowered_call_arg_setup,
              Call.arg_size() * InstrCost);

    if (IsIndirectCall) {
      InlineParams IndirectCallParams = {/* DefaultThreshold*/ 0,
                                         /*HintThreshold*/ {},
                                         /*ColdThreshold*/ {},
                                         /*OptSizeThreshold*/ {},
                                         /*OptMinSizeThreshold*/ {},
                                         /*HotCallSiteThreshold*/ {},
                                         /*LocallyHotCallSiteThreshold*/ {},
                                         /*ColdCallSiteThreshold*/ {},
                                         /*ComputeFullInlineCost*/ true,
                                         /*EnableDeferral*/ true};
      IndirectCallParams.DefaultThreshold =
          InlineConstants::IndirectCallThreshold;

      InlineCostCallAnalyzer CA(*F, Call, IndirectCallParams, TTI,
                                GetAssumptionCache, GetBFI, PSI, ORE, false,
                                true);
      if (CA.analyze().isSuccess()) {
        increment(InlineCostFeatureIndex::nested_inline_cost_estimate,
                  CA.getCost());
        increment(InlineCostFeatureIndex::nested_inlines, 1);
      }
    } else {
      onCallPenalty();
    }
  }

  void onFinalizeSwitch(unsigned JumpTableSize, unsigned NumCaseCluster,
                        bool DefaultDestUndefined) override {
    if (JumpTableSize) {
      if (!DefaultDestUndefined)
        increment(InlineCostFeatureIndex::switch_default_dest_penalty,
                  SwitchDefaultDestCostMultiplier * InstrCost);
      int64_t JTCost = static_cast<int64_t>(JumpTableSize) * InstrCost +
                       JTCostMultiplier * InstrCost;
      increment(InlineCostFeatureIndex::jump_table_penalty, JTCost);
      return;
    }

    if (NumCaseCluster <= 3) {
      increment(InlineCostFeatureIndex::case_cluster_penalty,
                (NumCaseCluster - DefaultDestUndefined) *
                    CaseClusterCostMultiplier * InstrCost);
      return;
    }

    int64_t ExpectedNumberOfCompare =
        getExpectedNumberOfCompare(NumCaseCluster);

    int64_t SwitchCost =
        ExpectedNumberOfCompare * SwitchCostMultiplier * InstrCost;
    increment(InlineCostFeatureIndex::switch_penalty, SwitchCost);
  }

  void onMissedSimplification() override {
    increment(InlineCostFeatureIndex::unsimplified_common_instructions,
              InstrCost);
  }

  void onInitializeSROAArg(AllocaInst *Arg) override {
    auto SROAArgCost = TTI.getCallerAllocaCost(&CandidateCall, Arg);
    SROACosts[Arg] = SROAArgCost;
    SROACostSavingOpportunities += SROAArgCost;
  }

  void onAggregateSROAUse(AllocaInst *Arg) override {
    SROACosts.find(Arg)->second += InstrCost;
    SROACostSavingOpportunities += InstrCost;
  }

  void onBlockAnalyzed(const BasicBlock *BB) override {
    if (BB->getTerminator()->getNumSuccessors() > 1)
      set(InlineCostFeatureIndex::is_multiple_blocks, 1);
    Threshold -= SingleBBBonus;
  }

  InlineResult finalizeAnalysis() override {
    auto *Caller = CandidateCall.getFunction();
    if (Caller->hasMinSize()) {
      DominatorTree DT(F);
      LoopInfo LI(DT);
      for (Loop *L : LI) {
        // Ignore loops that will not be executed
        if (DeadBlocks.count(L->getHeader()))
          continue;
        increment(InlineCostFeatureIndex::num_loops,
                  InlineConstants::LoopPenalty);
      }
    }
    set(InlineCostFeatureIndex::dead_blocks, DeadBlocks.size());
    set(InlineCostFeatureIndex::simplified_instructions,
        NumInstructionsSimplified);
    set(InlineCostFeatureIndex::constant_args, NumConstantArgs);
    set(InlineCostFeatureIndex::constant_offset_ptr_args,
        NumConstantOffsetPtrArgs);
    set(InlineCostFeatureIndex::sroa_savings, SROACostSavingOpportunities);

    if (NumVectorInstructions <= NumInstructions / 10)
      Threshold -= VectorBonus;
    else if (NumVectorInstructions <= NumInstructions / 2)
      Threshold -= VectorBonus / 2;

    set(InlineCostFeatureIndex::threshold, Threshold);

    return InlineResult::success();
  }

  bool shouldStop() override { return false; }

  void onLoadEliminationOpportunity() override {
    increment(InlineCostFeatureIndex::load_elimination, 1);
  }

  InlineResult onAnalysisStart() override {
    increment(InlineCostFeatureIndex::callsite_cost,
              -1 * getCallsiteCost(TTI, this->CandidateCall, DL));

    set(InlineCostFeatureIndex::cold_cc_penalty,
        (F.getCallingConv() == CallingConv::Cold));

    set(InlineCostFeatureIndex::last_call_to_static_bonus,
        isSoleCallToLocalFunction(CandidateCall, F));

    // FIXME: we shouldn't repeat this logic in both the Features and Cost
    // analyzer - instead, we should abstract it to a common method in the
    // CallAnalyzer
    int SingleBBBonusPercent = 50;
    int VectorBonusPercent = TTI.getInlinerVectorBonusPercent();
    Threshold += TTI.adjustInliningThreshold(&CandidateCall);
    Threshold *= TTI.getInliningThresholdMultiplier();
    SingleBBBonus = Threshold * SingleBBBonusPercent / 100;
    VectorBonus = Threshold * VectorBonusPercent / 100;
    Threshold += (SingleBBBonus + VectorBonus);

    return InlineResult::success();
  }

public:
  InlineCostFeaturesAnalyzer(
      const TargetTransformInfo &TTI,
      function_ref<AssumptionCache &(Function &)> &GetAssumptionCache,
      function_ref<BlockFrequencyInfo &(Function &)> GetBFI,
      ProfileSummaryInfo *PSI, OptimizationRemarkEmitter *ORE, Function &Callee,
      CallBase &Call)
      : CallAnalyzer(Callee, Call, TTI, GetAssumptionCache, GetBFI, PSI) {}

  const InlineCostFeatures &features() const { return Cost; }
};

} // namespace

/// Test whether the given value is an Alloca-derived function argument.
bool CallAnalyzer::isAllocaDerivedArg(Value *V) {
  return SROAArgValues.count(V);
}

void CallAnalyzer::disableSROAForArg(AllocaInst *SROAArg) {
  onDisableSROA(SROAArg);
  EnabledSROAAllocas.erase(SROAArg);
  disableLoadElimination();
}

void InlineCostAnnotationWriter::emitInstructionAnnot(
    const Instruction *I, formatted_raw_ostream &OS) {
  // The cost of inlining of the given instruction is printed always.
  // The threshold delta is printed only when it is non-zero. It happens
  // when we decided to give a bonus at a particular instruction.
  std::optional<InstructionCostDetail> Record = ICCA->getCostDetails(I);
  if (!Record)
    OS << "; No analysis for the instruction";
  else {
    OS << "; cost before = " << Record->CostBefore
       << ", cost after = " << Record->CostAfter
       << ", threshold before = " << Record->ThresholdBefore
       << ", threshold after = " << Record->ThresholdAfter << ", ";
    OS << "cost delta = " << Record->getCostDelta();
    if (Record->hasThresholdChanged())
      OS << ", threshold delta = " << Record->getThresholdDelta();
  }
  auto C = ICCA->getSimplifiedValue(const_cast<Instruction *>(I));
  if (C) {
    OS << ", simplified to ";
    (*C)->print(OS, true);
  }
  OS << "\n";
}

/// If 'V' maps to a SROA candidate, disable SROA for it.
void CallAnalyzer::disableSROA(Value *V) {
  if (auto *SROAArg = getSROAArgForValueOrNull(V)) {
    disableSROAForArg(SROAArg);
  }
}

void CallAnalyzer::disableLoadElimination() {
  if (EnableLoadElimination) {
    onDisableLoadElimination();
    EnableLoadElimination = false;
  }
}

/// Accumulate a constant GEP offset into an APInt if possible.
///
/// Returns false if unable to compute the offset for any reason. Respects any
/// simplified values known during the analysis of this callsite.
bool CallAnalyzer::accumulateGEPOffset(GEPOperator &GEP, APInt &Offset) {
  unsigned IntPtrWidth = DL.getIndexTypeSizeInBits(GEP.getType());
  assert(IntPtrWidth == Offset.getBitWidth());

  for (gep_type_iterator GTI = gep_type_begin(GEP), GTE = gep_type_end(GEP);
       GTI != GTE; ++GTI) {
    ConstantInt *OpC = dyn_cast<ConstantInt>(GTI.getOperand());
    if (!OpC)
      if (Constant *SimpleOp = SimplifiedValues.lookup(GTI.getOperand()))
        OpC = dyn_cast<ConstantInt>(SimpleOp);
    if (!OpC)
      return false;
    if (OpC->isZero())
      continue;

    // Handle a struct index, which adds its field offset to the pointer.
    if (StructType *STy = GTI.getStructTypeOrNull()) {
      unsigned ElementIdx = OpC->getZExtValue();
      const StructLayout *SL = DL.getStructLayout(STy);
      Offset += APInt(IntPtrWidth, SL->getElementOffset(ElementIdx));
      continue;
    }

    APInt TypeSize(IntPtrWidth, GTI.getSequentialElementStride(DL));
    Offset += OpC->getValue().sextOrTrunc(IntPtrWidth) * TypeSize;
  }
  return true;
}

/// Use TTI to check whether a GEP is free.
///
/// Respects any simplified values known during the analysis of this callsite.
bool CallAnalyzer::isGEPFree(GetElementPtrInst &GEP) {
  SmallVector<Value *, 4> Operands;
  Operands.push_back(GEP.getOperand(0));
  for (const Use &Op : GEP.indices())
    if (Constant *SimpleOp = SimplifiedValues.lookup(Op))
      Operands.push_back(SimpleOp);
    else
      Operands.push_back(Op);
  return TTI.getInstructionCost(&GEP, Operands,
                                TargetTransformInfo::TCK_SizeAndLatency) ==
         TargetTransformInfo::TCC_Free;
}

bool CallAnalyzer::visitAlloca(AllocaInst &I) {
  disableSROA(I.getOperand(0));

  // Check whether inlining will turn a dynamic alloca into a static
  // alloca and handle that case.
  if (I.isArrayAllocation()) {
    Constant *Size = SimplifiedValues.lookup(I.getArraySize());
    if (auto *AllocSize = dyn_cast_or_null<ConstantInt>(Size)) {
      // Sometimes a dynamic alloca could be converted into a static alloca
      // after this constant prop, and become a huge static alloca on an
      // unconditional CFG path. Avoid inlining if this is going to happen above
      // a threshold.
      // FIXME: If the threshold is removed or lowered too much, we could end up
      // being too pessimistic and prevent inlining non-problematic code. This
      // could result in unintended perf regressions. A better overall strategy
      // is needed to track stack usage during inlining.
      Type *Ty = I.getAllocatedType();
      AllocatedSize = SaturatingMultiplyAdd(
          AllocSize->getLimitedValue(),
          DL.getTypeAllocSize(Ty).getKnownMinValue(), AllocatedSize);
      if (AllocatedSize > InlineConstants::MaxSimplifiedDynamicAllocaToInline)
        HasDynamicAlloca = true;
      return false;
    }
  }

  // Accumulate the allocated size.
  if (I.isStaticAlloca()) {
    Type *Ty = I.getAllocatedType();
    AllocatedSize = SaturatingAdd(DL.getTypeAllocSize(Ty).getKnownMinValue(),
                                  AllocatedSize);
  }

  // FIXME: This is overly conservative. Dynamic allocas are inefficient for
  // a variety of reasons, and so we would like to not inline them into
  // functions which don't currently have a dynamic alloca. This simply
  // disables inlining altogether in the presence of a dynamic alloca.
  if (!I.isStaticAlloca())
    HasDynamicAlloca = true;

  return false;
}

bool CallAnalyzer::visitPHI(PHINode &I) {
  // FIXME: We need to propagate SROA *disabling* through phi nodes, even
  // though we don't want to propagate it's bonuses. The idea is to disable
  // SROA if it *might* be used in an inappropriate manner.

  // Phi nodes are always zero-cost.
  // FIXME: Pointer sizes may differ between different address spaces, so do we
  // need to use correct address space in the call to getPointerSizeInBits here?
  // Or could we skip the getPointerSizeInBits call completely? As far as I can
  // see the ZeroOffset is used as a dummy value, so we can probably use any
  // bit width for the ZeroOffset?
  APInt ZeroOffset = APInt::getZero(DL.getPointerSizeInBits(0));
  bool CheckSROA = I.getType()->isPointerTy();

  // Track the constant or pointer with constant offset we've seen so far.
  Constant *FirstC = nullptr;
  std::pair<Value *, APInt> FirstBaseAndOffset = {nullptr, ZeroOffset};
  Value *FirstV = nullptr;

  for (unsigned i = 0, e = I.getNumIncomingValues(); i != e; ++i) {
    BasicBlock *Pred = I.getIncomingBlock(i);
    // If the incoming block is dead, skip the incoming block.
    if (DeadBlocks.count(Pred))
      continue;
    // If the parent block of phi is not the known successor of the incoming
    // block, skip the incoming block.
    BasicBlock *KnownSuccessor = KnownSuccessors[Pred];
    if (KnownSuccessor && KnownSuccessor != I.getParent())
      continue;

    Value *V = I.getIncomingValue(i);
    // If the incoming value is this phi itself, skip the incoming value.
    if (&I == V)
      continue;

    Constant *C = dyn_cast<Constant>(V);
    if (!C)
      C = SimplifiedValues.lookup(V);

    std::pair<Value *, APInt> BaseAndOffset = {nullptr, ZeroOffset};
    if (!C && CheckSROA)
      BaseAndOffset = ConstantOffsetPtrs.lookup(V);

    if (!C && !BaseAndOffset.first)
      // The incoming value is neither a constant nor a pointer with constant
      // offset, exit early.
      return true;

    if (FirstC) {
      if (FirstC == C)
        // If we've seen a constant incoming value before and it is the same
        // constant we see this time, continue checking the next incoming value.
        continue;
      // Otherwise early exit because we either see a different constant or saw
      // a constant before but we have a pointer with constant offset this time.
      return true;
    }

    if (FirstV) {
      // The same logic as above, but check pointer with constant offset here.
      if (FirstBaseAndOffset == BaseAndOffset)
        continue;
      return true;
    }

    if (C) {
      // This is the 1st time we've seen a constant, record it.
      FirstC = C;
      continue;
    }

    // The remaining case is that this is the 1st time we've seen a pointer with
    // constant offset, record it.
    FirstV = V;
    FirstBaseAndOffset = BaseAndOffset;
  }

  // Check if we can map phi to a constant.
  if (FirstC) {
    SimplifiedValues[&I] = FirstC;
    return true;
  }

  // Check if we can map phi to a pointer with constant offset.
  if (FirstBaseAndOffset.first) {
    ConstantOffsetPtrs[&I] = FirstBaseAndOffset;

    if (auto *SROAArg = getSROAArgForValueOrNull(FirstV))
      SROAArgValues[&I] = SROAArg;
  }

  return true;
}

/// Check we can fold GEPs of constant-offset call site argument pointers.
/// This requires target data and inbounds GEPs.
///
/// \return true if the specified GEP can be folded.
bool CallAnalyzer::canFoldInboundsGEP(GetElementPtrInst &I) {
  // Check if we have a base + offset for the pointer.
  std::pair<Value *, APInt> BaseAndOffset =
      ConstantOffsetPtrs.lookup(I.getPointerOperand());
  if (!BaseAndOffset.first)
    return false;

  // Check if the offset of this GEP is constant, and if so accumulate it
  // into Offset.
  if (!accumulateGEPOffset(cast<GEPOperator>(I), BaseAndOffset.second))
    return false;

  // Add the result as a new mapping to Base + Offset.
  ConstantOffsetPtrs[&I] = BaseAndOffset;

  return true;
}

bool CallAnalyzer::visitGetElementPtr(GetElementPtrInst &I) {
  auto *SROAArg = getSROAArgForValueOrNull(I.getPointerOperand());

  // Lambda to check whether a GEP's indices are all constant.
  auto IsGEPOffsetConstant = [&](GetElementPtrInst &GEP) {
    for (const Use &Op : GEP.indices())
      if (!isa<Constant>(Op) && !SimplifiedValues.lookup(Op))
        return false;
    return true;
  };

  if (!DisableGEPConstOperand)
    if (simplifyInstruction(I))
      return true;

  if ((I.isInBounds() && canFoldInboundsGEP(I)) || IsGEPOffsetConstant(I)) {
    if (SROAArg)
      SROAArgValues[&I] = SROAArg;

    // Constant GEPs are modeled as free.
    return true;
  }

  // Variable GEPs will require math and will disable SROA.
  if (SROAArg)
    disableSROAForArg(SROAArg);
  return isGEPFree(I);
}

/// Simplify \p I if its operands are constants and update SimplifiedValues.
bool CallAnalyzer::simplifyInstruction(Instruction &I) {
  SmallVector<Constant *> COps;
  for (Value *Op : I.operands()) {
    Constant *COp = dyn_cast<Constant>(Op);
    if (!COp)
      COp = SimplifiedValues.lookup(Op);
    if (!COp)
      return false;
    COps.push_back(COp);
  }
  auto *C = ConstantFoldInstOperands(&I, COps, DL);
  if (!C)
    return false;
  SimplifiedValues[&I] = C;
  return true;
}

/// Try to simplify a call to llvm.is.constant.
///
/// Duplicate the argument checking from CallAnalyzer::simplifyCallSite since
/// we expect calls of this specific intrinsic to be infrequent.
///
/// FIXME: Given that we know CB's parent (F) caller
/// (CandidateCall->getParent()->getParent()), we might be able to determine
/// whether inlining F into F's caller would change how the call to
/// llvm.is.constant would evaluate.
bool CallAnalyzer::simplifyIntrinsicCallIsConstant(CallBase &CB) {
  Value *Arg = CB.getArgOperand(0);
  auto *C = dyn_cast<Constant>(Arg);

  if (!C)
    C = dyn_cast_or_null<Constant>(SimplifiedValues.lookup(Arg));

  Type *RT = CB.getFunctionType()->getReturnType();
  SimplifiedValues[&CB] = ConstantInt::get(RT, C ? 1 : 0);
  return true;
}

bool CallAnalyzer::simplifyIntrinsicCallObjectSize(CallBase &CB) {
  // As per the langref, "The fourth argument to llvm.objectsize determines if
  // the value should be evaluated at runtime."
  if (cast<ConstantInt>(CB.getArgOperand(3))->isOne())
    return false;

  Value *V = lowerObjectSizeCall(&cast<IntrinsicInst>(CB), DL, nullptr,
                                 /*MustSucceed=*/true);
  Constant *C = dyn_cast_or_null<Constant>(V);
  if (C)
    SimplifiedValues[&CB] = C;
  return C;
}

bool CallAnalyzer::visitBitCast(BitCastInst &I) {
  // Propagate constants through bitcasts.
  if (simplifyInstruction(I))
    return true;

  // Track base/offsets through casts
  std::pair<Value *, APInt> BaseAndOffset =
      ConstantOffsetPtrs.lookup(I.getOperand(0));
  // Casts don't change the offset, just wrap it up.
  if (BaseAndOffset.first)
    ConstantOffsetPtrs[&I] = BaseAndOffset;

  // Also look for SROA candidates here.
  if (auto *SROAArg = getSROAArgForValueOrNull(I.getOperand(0)))
    SROAArgValues[&I] = SROAArg;

  // Bitcasts are always zero cost.
  return true;
}

bool CallAnalyzer::visitPtrToInt(PtrToIntInst &I) {
  // Propagate constants through ptrtoint.
  if (simplifyInstruction(I))
    return true;

  // Track base/offset pairs when converted to a plain integer provided the
  // integer is large enough to represent the pointer.
  unsigned IntegerSize = I.getType()->getScalarSizeInBits();
  unsigned AS = I.getOperand(0)->getType()->getPointerAddressSpace();
  if (IntegerSize == DL.getPointerSizeInBits(AS)) {
    std::pair<Value *, APInt> BaseAndOffset =
        ConstantOffsetPtrs.lookup(I.getOperand(0));
    if (BaseAndOffset.first)
      ConstantOffsetPtrs[&I] = BaseAndOffset;
  }

  // This is really weird. Technically, ptrtoint will disable SROA. However,
  // unless that ptrtoint is *used* somewhere in the live basic blocks after
  // inlining, it will be nuked, and SROA should proceed. All of the uses which
  // would block SROA would also block SROA if applied directly to a pointer,
  // and so we can just add the integer in here. The only places where SROA is
  // preserved either cannot fire on an integer, or won't in-and-of themselves
  // disable SROA (ext) w/o some later use that we would see and disable.
  if (auto *SROAArg = getSROAArgForValueOrNull(I.getOperand(0)))
    SROAArgValues[&I] = SROAArg;

  return TTI.getInstructionCost(&I, TargetTransformInfo::TCK_SizeAndLatency) ==
         TargetTransformInfo::TCC_Free;
}

bool CallAnalyzer::visitIntToPtr(IntToPtrInst &I) {
  // Propagate constants through ptrtoint.
  if (simplifyInstruction(I))
    return true;

  // Track base/offset pairs when round-tripped through a pointer without
  // modifications provided the integer is not too large.
  Value *Op = I.getOperand(0);
  unsigned IntegerSize = Op->getType()->getScalarSizeInBits();
  if (IntegerSize <= DL.getPointerTypeSizeInBits(I.getType())) {
    std::pair<Value *, APInt> BaseAndOffset = ConstantOffsetPtrs.lookup(Op);
    if (BaseAndOffset.first)
      ConstantOffsetPtrs[&I] = BaseAndOffset;
  }

  // "Propagate" SROA here in the same manner as we do for ptrtoint above.
  if (auto *SROAArg = getSROAArgForValueOrNull(Op))
    SROAArgValues[&I] = SROAArg;

  return TTI.getInstructionCost(&I, TargetTransformInfo::TCK_SizeAndLatency) ==
         TargetTransformInfo::TCC_Free;
}

bool CallAnalyzer::visitCastInst(CastInst &I) {
  // Propagate constants through casts.
  if (simplifyInstruction(I))
    return true;

  // Disable SROA in the face of arbitrary casts we don't explicitly list
  // elsewhere.
  disableSROA(I.getOperand(0));

  // If this is a floating-point cast, and the target says this operation
  // is expensive, this may eventually become a library call. Treat the cost
  // as such.
  switch (I.getOpcode()) {
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
    if (TTI.getFPOpCost(I.getType()) == TargetTransformInfo::TCC_Expensive)
      onCallPenalty();
    break;
  default:
    break;
  }

  return TTI.getInstructionCost(&I, TargetTransformInfo::TCK_SizeAndLatency) ==
         TargetTransformInfo::TCC_Free;
}

bool CallAnalyzer::paramHasAttr(Argument *A, Attribute::AttrKind Attr) {
  return CandidateCall.paramHasAttr(A->getArgNo(), Attr);
}

bool CallAnalyzer::isKnownNonNullInCallee(Value *V) {
  // Does the *call site* have the NonNull attribute set on an argument?  We
  // use the attribute on the call site to memoize any analysis done in the
  // caller. This will also trip if the callee function has a non-null
  // parameter attribute, but that's a less interesting case because hopefully
  // the callee would already have been simplified based on that.
  if (Argument *A = dyn_cast<Argument>(V))
    if (paramHasAttr(A, Attribute::NonNull))
      return true;

  // Is this an alloca in the caller?  This is distinct from the attribute case
  // above because attributes aren't updated within the inliner itself and we
  // always want to catch the alloca derived case.
  if (isAllocaDerivedArg(V))
    // We can actually predict the result of comparisons between an
    // alloca-derived value and null. Note that this fires regardless of
    // SROA firing.
    return true;

  return false;
}

bool CallAnalyzer::allowSizeGrowth(CallBase &Call) {
  // If the normal destination of the invoke or the parent block of the call
  // site is unreachable-terminated, there is little point in inlining this
  // unless there is literally zero cost.
  // FIXME: Note that it is possible that an unreachable-terminated block has a
  // hot entry. For example, in below scenario inlining hot_call_X() may be
  // beneficial :
  // main() {
  //   hot_call_1();
  //   ...
  //   hot_call_N()
  //   exit(0);
  // }
  // For now, we are not handling this corner case here as it is rare in real
  // code. In future, we should elaborate this based on BPI and BFI in more
  // general threshold adjusting heuristics in updateThreshold().
  if (InvokeInst *II = dyn_cast<InvokeInst>(&Call)) {
    if (isa<UnreachableInst>(II->getNormalDest()->getTerminator()))
      return false;
  } else if (isa<UnreachableInst>(Call.getParent()->getTerminator()))
    return false;

  return true;
}

bool InlineCostCallAnalyzer::isColdCallSite(CallBase &Call,
                                            BlockFrequencyInfo *CallerBFI) {
  // If global profile summary is available, then callsite's coldness is
  // determined based on that.
  if (PSI && PSI->hasProfileSummary())
    return PSI->isColdCallSite(Call, CallerBFI);

  // Otherwise we need BFI to be available.
  if (!CallerBFI)
    return false;

  // Determine if the callsite is cold relative to caller's entry. We could
  // potentially cache the computation of scaled entry frequency, but the added
  // complexity is not worth it unless this scaling shows up high in the
  // profiles.
  const BranchProbability ColdProb(ColdCallSiteRelFreq, 100);
  auto CallSiteBB = Call.getParent();
  auto CallSiteFreq = CallerBFI->getBlockFreq(CallSiteBB);
  auto CallerEntryFreq =
      CallerBFI->getBlockFreq(&(Call.getCaller()->getEntryBlock()));
  return CallSiteFreq < CallerEntryFreq * ColdProb;
}

std::optional<int>
InlineCostCallAnalyzer::getHotCallSiteThreshold(CallBase &Call,
                                                BlockFrequencyInfo *CallerBFI) {

  // If global profile summary is available, then callsite's hotness is
  // determined based on that.
  if (PSI && PSI->hasProfileSummary() && PSI->isHotCallSite(Call, CallerBFI))
    return Params.HotCallSiteThreshold;

  // Otherwise we need BFI to be available and to have a locally hot callsite
  // threshold.
  if (!CallerBFI || !Params.LocallyHotCallSiteThreshold)
    return std::nullopt;

  // Determine if the callsite is hot relative to caller's entry. We could
  // potentially cache the computation of scaled entry frequency, but the added
  // complexity is not worth it unless this scaling shows up high in the
  // profiles.
  const BasicBlock *CallSiteBB = Call.getParent();
  BlockFrequency CallSiteFreq = CallerBFI->getBlockFreq(CallSiteBB);
  BlockFrequency CallerEntryFreq = CallerBFI->getEntryFreq();
  std::optional<BlockFrequency> Limit = CallerEntryFreq.mul(HotCallSiteRelFreq);
  if (Limit && CallSiteFreq >= *Limit)
    return Params.LocallyHotCallSiteThreshold;

  // Otherwise treat it normally.
  return std::nullopt;
}

void InlineCostCallAnalyzer::updateThreshold(CallBase &Call, Function &Callee) {
  // If no size growth is allowed for this inlining, set Threshold to 0.
  if (!allowSizeGrowth(Call)) {
    Threshold = 0;
    return;
  }

  Function *Caller = Call.getCaller();

  // return min(A, B) if B is valid.
  auto MinIfValid = [](int A, std::optional<int> B) {
    return B ? std::min(A, *B) : A;
  };

  // return max(A, B) if B is valid.
  auto MaxIfValid = [](int A, std::optional<int> B) {
    return B ? std::max(A, *B) : A;
  };

  // Various bonus percentages. These are multiplied by Threshold to get the
  // bonus values.
  // SingleBBBonus: This bonus is applied if the callee has a single reachable
  // basic block at the given callsite context. This is speculatively applied
  // and withdrawn if more than one basic block is seen.
  //
  // LstCallToStaticBonus: This large bonus is applied to ensure the inlining
  // of the last call to a static function as inlining such functions is
  // guaranteed to reduce code size.
  //
  // These bonus percentages may be set to 0 based on properties of the caller
  // and the callsite.
  int SingleBBBonusPercent = 50;
  int VectorBonusPercent = TTI.getInlinerVectorBonusPercent();
  int LastCallToStaticBonus = InlineConstants::LastCallToStaticBonus;

  // Lambda to set all the above bonus and bonus percentages to 0.
  auto DisallowAllBonuses = [&]() {
    SingleBBBonusPercent = 0;
    VectorBonusPercent = 0;
    LastCallToStaticBonus = 0;
  };

  // Use the OptMinSizeThreshold or OptSizeThreshold knob if they are available
  // and reduce the threshold if the caller has the necessary attribute.
  if (Caller->hasMinSize()) {
    Threshold = MinIfValid(Threshold, Params.OptMinSizeThreshold);
    // For minsize, we want to disable the single BB bonus and the vector
    // bonuses, but not the last-call-to-static bonus. Inlining the last call to
    // a static function will, at the minimum, eliminate the parameter setup and
    // call/return instructions.
    SingleBBBonusPercent = 0;
    VectorBonusPercent = 0;
  } else if (Caller->hasOptSize())
    Threshold = MinIfValid(Threshold, Params.OptSizeThreshold);

  // Adjust the threshold based on inlinehint attribute and profile based
  // hotness information if the caller does not have MinSize attribute.
  if (!Caller->hasMinSize()) {
    if (Callee.hasFnAttribute(Attribute::InlineHint))
      Threshold = MaxIfValid(Threshold, Params.HintThreshold);

    // FIXME: After switching to the new passmanager, simplify the logic below
    // by checking only the callsite hotness/coldness as we will reliably
    // have local profile information.
    //
    // Callsite hotness and coldness can be determined if sample profile is
    // used (which adds hotness metadata to calls) or if caller's
    // BlockFrequencyInfo is available.
    BlockFrequencyInfo *CallerBFI = GetBFI ? &(GetBFI(*Caller)) : nullptr;
    auto HotCallSiteThreshold = getHotCallSiteThreshold(Call, CallerBFI);
    if (!Caller->hasOptSize() && HotCallSiteThreshold) {
      LLVM_DEBUG(dbgs() << "Hot callsite.\n");
      // FIXME: This should update the threshold only if it exceeds the
      // current threshold, but AutoFDO + ThinLTO currently relies on this
      // behavior to prevent inlining of hot callsites during ThinLTO
      // compile phase.
      Threshold = *HotCallSiteThreshold;
    } else if (isColdCallSite(Call, CallerBFI)) {
      LLVM_DEBUG(dbgs() << "Cold callsite.\n");
      // Do not apply bonuses for a cold callsite including the
      // LastCallToStatic bonus. While this bonus might result in code size
      // reduction, it can cause the size of a non-cold caller to increase
      // preventing it from being inlined.
      DisallowAllBonuses();
      Threshold = MinIfValid(Threshold, Params.ColdCallSiteThreshold);
    } else if (PSI) {
      // Use callee's global profile information only if we have no way of
      // determining this via callsite information.
      if (PSI->isFunctionEntryHot(&Callee)) {
        LLVM_DEBUG(dbgs() << "Hot callee.\n");
        // If callsite hotness can not be determined, we may still know
        // that the callee is hot and treat it as a weaker hint for threshold
        // increase.
        Threshold = MaxIfValid(Threshold, Params.HintThreshold);
      } else if (PSI->isFunctionEntryCold(&Callee)) {
        LLVM_DEBUG(dbgs() << "Cold callee.\n");
        // Do not apply bonuses for a cold callee including the
        // LastCallToStatic bonus. While this bonus might result in code size
        // reduction, it can cause the size of a non-cold caller to increase
        // preventing it from being inlined.
        DisallowAllBonuses();
        Threshold = MinIfValid(Threshold, Params.ColdThreshold);
      }
    }
  }

  Threshold += TTI.adjustInliningThreshold(&Call);

  // Finally, take the target-specific inlining threshold multiplier into
  // account.
  Threshold *= TTI.getInliningThresholdMultiplier();

  SingleBBBonus = Threshold * SingleBBBonusPercent / 100;
  VectorBonus = Threshold * VectorBonusPercent / 100;

  // If there is only one call of the function, and it has internal linkage,
  // the cost of inlining it drops dramatically. It may seem odd to update
  // Cost in updateThreshold, but the bonus depends on the logic in this method.
  if (isSoleCallToLocalFunction(Call, F)) {
    Cost -= LastCallToStaticBonus;
    StaticBonusApplied = LastCallToStaticBonus;
  }
}

bool CallAnalyzer::visitCmpInst(CmpInst &I) {
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  // First try to handle simplified comparisons.
  if (simplifyInstruction(I))
    return true;

  if (I.getOpcode() == Instruction::FCmp)
    return false;

  // Otherwise look for a comparison between constant offset pointers with
  // a common base.
  Value *LHSBase, *RHSBase;
  APInt LHSOffset, RHSOffset;
  std::tie(LHSBase, LHSOffset) = ConstantOffsetPtrs.lookup(LHS);
  if (LHSBase) {
    std::tie(RHSBase, RHSOffset) = ConstantOffsetPtrs.lookup(RHS);
    if (RHSBase && LHSBase == RHSBase) {
      // We have common bases, fold the icmp to a constant based on the
      // offsets.
      SimplifiedValues[&I] = ConstantInt::getBool(
          I.getType(),
          ICmpInst::compare(LHSOffset, RHSOffset, I.getPredicate()));
      ++NumConstantPtrCmps;
      return true;
    }
  }

  auto isImplicitNullCheckCmp = [](const CmpInst &I) {
    for (auto *User : I.users())
      if (auto *Instr = dyn_cast<Instruction>(User))
        if (!Instr->getMetadata(LLVMContext::MD_make_implicit))
          return false;
    return true;
  };

  // If the comparison is an equality comparison with null, we can simplify it
  // if we know the value (argument) can't be null
  if (I.isEquality() && isa<ConstantPointerNull>(I.getOperand(1))) {
    if (isKnownNonNullInCallee(I.getOperand(0))) {
      bool IsNotEqual = I.getPredicate() == CmpInst::ICMP_NE;
      SimplifiedValues[&I] = IsNotEqual ? ConstantInt::getTrue(I.getType())
                                        : ConstantInt::getFalse(I.getType());
      return true;
    }
    // Implicit null checks act as unconditional branches and their comparisons
    // should be treated as simplified and free of cost.
    if (isImplicitNullCheckCmp(I))
      return true;
  }
  return handleSROA(I.getOperand(0), isa<ConstantPointerNull>(I.getOperand(1)));
}

bool CallAnalyzer::visitSub(BinaryOperator &I) {
  // Try to handle a special case: we can fold computing the difference of two
  // constant-related pointers.
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  Value *LHSBase, *RHSBase;
  APInt LHSOffset, RHSOffset;
  std::tie(LHSBase, LHSOffset) = ConstantOffsetPtrs.lookup(LHS);
  if (LHSBase) {
    std::tie(RHSBase, RHSOffset) = ConstantOffsetPtrs.lookup(RHS);
    if (RHSBase && LHSBase == RHSBase) {
      // We have common bases, fold the subtract to a constant based on the
      // offsets.
      Constant *CLHS = ConstantInt::get(LHS->getContext(), LHSOffset);
      Constant *CRHS = ConstantInt::get(RHS->getContext(), RHSOffset);
      if (Constant *C = ConstantExpr::getSub(CLHS, CRHS)) {
        SimplifiedValues[&I] = C;
        ++NumConstantPtrDiffs;
        return true;
      }
    }
  }

  // Otherwise, fall back to the generic logic for simplifying and handling
  // instructions.
  return Base::visitSub(I);
}

bool CallAnalyzer::visitBinaryOperator(BinaryOperator &I) {
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  Constant *CLHS = dyn_cast<Constant>(LHS);
  if (!CLHS)
    CLHS = SimplifiedValues.lookup(LHS);
  Constant *CRHS = dyn_cast<Constant>(RHS);
  if (!CRHS)
    CRHS = SimplifiedValues.lookup(RHS);

  Value *SimpleV = nullptr;
  if (auto FI = dyn_cast<FPMathOperator>(&I))
    SimpleV = simplifyBinOp(I.getOpcode(), CLHS ? CLHS : LHS, CRHS ? CRHS : RHS,
                            FI->getFastMathFlags(), DL);
  else
    SimpleV =
        simplifyBinOp(I.getOpcode(), CLHS ? CLHS : LHS, CRHS ? CRHS : RHS, DL);

  if (Constant *C = dyn_cast_or_null<Constant>(SimpleV))
    SimplifiedValues[&I] = C;

  if (SimpleV)
    return true;

  // Disable any SROA on arguments to arbitrary, unsimplified binary operators.
  disableSROA(LHS);
  disableSROA(RHS);

  // If the instruction is floating point, and the target says this operation
  // is expensive, this may eventually become a library call. Treat the cost
  // as such. Unless it's fneg which can be implemented with an xor.
  using namespace llvm::PatternMatch;
  if (I.getType()->isFloatingPointTy() &&
      TTI.getFPOpCost(I.getType()) == TargetTransformInfo::TCC_Expensive &&
      !match(&I, m_FNeg(m_Value())))
    onCallPenalty();

  return false;
}

bool CallAnalyzer::visitFNeg(UnaryOperator &I) {
  Value *Op = I.getOperand(0);
  Constant *COp = dyn_cast<Constant>(Op);
  if (!COp)
    COp = SimplifiedValues.lookup(Op);

  Value *SimpleV = simplifyFNegInst(
      COp ? COp : Op, cast<FPMathOperator>(I).getFastMathFlags(), DL);

  if (Constant *C = dyn_cast_or_null<Constant>(SimpleV))
    SimplifiedValues[&I] = C;

  if (SimpleV)
    return true;

  // Disable any SROA on arguments to arbitrary, unsimplified fneg.
  disableSROA(Op);

  return false;
}

bool CallAnalyzer::visitLoad(LoadInst &I) {
  if (handleSROA(I.getPointerOperand(), I.isSimple()))
    return true;

  // If the data is already loaded from this address and hasn't been clobbered
  // by any stores or calls, this load is likely to be redundant and can be
  // eliminated.
  if (EnableLoadElimination &&
      !LoadAddrSet.insert(I.getPointerOperand()).second && I.isUnordered()) {
    onLoadEliminationOpportunity();
    return true;
  }

  onMemAccess();
  return false;
}

bool CallAnalyzer::visitStore(StoreInst &I) {
  if (handleSROA(I.getPointerOperand(), I.isSimple()))
    return true;

  // The store can potentially clobber loads and prevent repeated loads from
  // being eliminated.
  // FIXME:
  // 1. We can probably keep an initial set of eliminatable loads substracted
  // from the cost even when we finally see a store. We just need to disable
  // *further* accumulation of elimination savings.
  // 2. We should probably at some point thread MemorySSA for the callee into
  // this and then use that to actually compute *really* precise savings.
  disableLoadElimination();

  onMemAccess();
  return false;
}

bool CallAnalyzer::visitExtractValue(ExtractValueInst &I) {
  // Constant folding for extract value is trivial.
  if (simplifyInstruction(I))
    return true;

  // SROA can't look through these, but they may be free.
  return Base::visitExtractValue(I);
}

bool CallAnalyzer::visitInsertValue(InsertValueInst &I) {
  // Constant folding for insert value is trivial.
  if (simplifyInstruction(I))
    return true;

  // SROA can't look through these, but they may be free.
  return Base::visitInsertValue(I);
}

/// Try to simplify a call site.
///
/// Takes a concrete function and callsite and tries to actually simplify it by
/// analyzing the arguments and call itself with instsimplify. Returns true if
/// it has simplified the callsite to some other entity (a constant), making it
/// free.
bool CallAnalyzer::simplifyCallSite(Function *F, CallBase &Call) {
  // FIXME: Using the instsimplify logic directly for this is inefficient
  // because we have to continually rebuild the argument list even when no
  // simplifications can be performed. Until that is fixed with remapping
  // inside of instsimplify, directly constant fold calls here.
  if (!canConstantFoldCallTo(&Call, F))
    return false;

  // Try to re-map the arguments to constants.
  SmallVector<Constant *, 4> ConstantArgs;
  ConstantArgs.reserve(Call.arg_size());
  for (Value *I : Call.args()) {
    Constant *C = dyn_cast<Constant>(I);
    if (!C)
      C = dyn_cast_or_null<Constant>(SimplifiedValues.lookup(I));
    if (!C)
      return false; // This argument doesn't map to a constant.

    ConstantArgs.push_back(C);
  }
  if (Constant *C = ConstantFoldCall(&Call, F, ConstantArgs)) {
    SimplifiedValues[&Call] = C;
    return true;
  }

  return false;
}

bool CallAnalyzer::visitCallBase(CallBase &Call) {
  if (!onCallBaseVisitStart(Call))
    return true;

  if (Call.hasFnAttr(Attribute::ReturnsTwice) &&
      !F.hasFnAttribute(Attribute::ReturnsTwice)) {
    // This aborts the entire analysis.
    ExposesReturnsTwice = true;
    return false;
  }
  if (isa<CallInst>(Call) && cast<CallInst>(Call).cannotDuplicate())
    ContainsNoDuplicateCall = true;

  Function *F = Call.getCalledFunction();
  bool IsIndirectCall = !F;
  if (IsIndirectCall) {
    // Check if this happens to be an indirect function call to a known function
    // in this inline context. If not, we've done all we can.
    Value *Callee = Call.getCalledOperand();
    F = dyn_cast_or_null<Function>(SimplifiedValues.lookup(Callee));
    if (!F || F->getFunctionType() != Call.getFunctionType()) {
      onCallArgumentSetup(Call);

      if (!Call.onlyReadsMemory())
        disableLoadElimination();
      return Base::visitCallBase(Call);
    }
  }

  assert(F && "Expected a call to a known function");

  // When we have a concrete function, first try to simplify it directly.
  if (simplifyCallSite(F, Call))
    return true;

  // Next check if it is an intrinsic we know about.
  // FIXME: Lift this into part of the InstVisitor.
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&Call)) {
    switch (II->getIntrinsicID()) {
    default:
      if (!Call.onlyReadsMemory() && !isAssumeLikeIntrinsic(II))
        disableLoadElimination();
      return Base::visitCallBase(Call);

    case Intrinsic::load_relative:
      onLoadRelativeIntrinsic();
      return false;

    case Intrinsic::memset:
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
      disableLoadElimination();
      // SROA can usually chew through these intrinsics, but they aren't free.
      return false;
    case Intrinsic::icall_branch_funnel:
    case Intrinsic::localescape:
      HasUninlineableIntrinsic = true;
      return false;
    case Intrinsic::vastart:
      InitsVargArgs = true;
      return false;
    case Intrinsic::launder_invariant_group:
    case Intrinsic::strip_invariant_group:
      if (auto *SROAArg = getSROAArgForValueOrNull(II->getOperand(0)))
        SROAArgValues[II] = SROAArg;
      return true;
    case Intrinsic::is_constant:
      return simplifyIntrinsicCallIsConstant(Call);
    case Intrinsic::objectsize:
      return simplifyIntrinsicCallObjectSize(Call);
    }
  }

  if (F == Call.getFunction()) {
    // This flag will fully abort the analysis, so don't bother with anything
    // else.
    IsRecursiveCall = true;
    if (!AllowRecursiveCall)
      return false;
  }

  if (TTI.isLoweredToCall(F)) {
    onLoweredCall(F, Call, IsIndirectCall);
  }

  if (!(Call.onlyReadsMemory() || (IsIndirectCall && F->onlyReadsMemory())))
    disableLoadElimination();
  return Base::visitCallBase(Call);
}

bool CallAnalyzer::visitReturnInst(ReturnInst &RI) {
  // At least one return instruction will be free after inlining.
  bool Free = !HasReturn;
  HasReturn = true;
  return Free;
}

bool CallAnalyzer::visitBranchInst(BranchInst &BI) {
  // We model unconditional branches as essentially free -- they really
  // shouldn't exist at all, but handling them makes the behavior of the
  // inliner more regular and predictable. Interestingly, conditional branches
  // which will fold away are also free.
  return BI.isUnconditional() || isa<ConstantInt>(BI.getCondition()) ||
         BI.getMetadata(LLVMContext::MD_make_implicit) ||
         isa_and_nonnull<ConstantInt>(
             SimplifiedValues.lookup(BI.getCondition()));
}

bool CallAnalyzer::visitSelectInst(SelectInst &SI) {
  bool CheckSROA = SI.getType()->isPointerTy();
  Value *TrueVal = SI.getTrueValue();
  Value *FalseVal = SI.getFalseValue();

  Constant *TrueC = dyn_cast<Constant>(TrueVal);
  if (!TrueC)
    TrueC = SimplifiedValues.lookup(TrueVal);
  Constant *FalseC = dyn_cast<Constant>(FalseVal);
  if (!FalseC)
    FalseC = SimplifiedValues.lookup(FalseVal);
  Constant *CondC =
      dyn_cast_or_null<Constant>(SimplifiedValues.lookup(SI.getCondition()));

  if (!CondC) {
    // Select C, X, X => X
    if (TrueC == FalseC && TrueC) {
      SimplifiedValues[&SI] = TrueC;
      return true;
    }

    if (!CheckSROA)
      return Base::visitSelectInst(SI);

    std::pair<Value *, APInt> TrueBaseAndOffset =
        ConstantOffsetPtrs.lookup(TrueVal);
    std::pair<Value *, APInt> FalseBaseAndOffset =
        ConstantOffsetPtrs.lookup(FalseVal);
    if (TrueBaseAndOffset == FalseBaseAndOffset && TrueBaseAndOffset.first) {
      ConstantOffsetPtrs[&SI] = TrueBaseAndOffset;

      if (auto *SROAArg = getSROAArgForValueOrNull(TrueVal))
        SROAArgValues[&SI] = SROAArg;
      return true;
    }

    return Base::visitSelectInst(SI);
  }

  // Select condition is a constant.
  Value *SelectedV = CondC->isAllOnesValue()  ? TrueVal
                     : (CondC->isNullValue()) ? FalseVal
                                              : nullptr;
  if (!SelectedV) {
    // Condition is a vector constant that is not all 1s or all 0s.  If all
    // operands are constants, ConstantFoldSelectInstruction() can handle the
    // cases such as select vectors.
    if (TrueC && FalseC) {
      if (auto *C = ConstantFoldSelectInstruction(CondC, TrueC, FalseC)) {
        SimplifiedValues[&SI] = C;
        return true;
      }
    }
    return Base::visitSelectInst(SI);
  }

  // Condition is either all 1s or all 0s. SI can be simplified.
  if (Constant *SelectedC = dyn_cast<Constant>(SelectedV)) {
    SimplifiedValues[&SI] = SelectedC;
    return true;
  }

  if (!CheckSROA)
    return true;

  std::pair<Value *, APInt> BaseAndOffset =
      ConstantOffsetPtrs.lookup(SelectedV);
  if (BaseAndOffset.first) {
    ConstantOffsetPtrs[&SI] = BaseAndOffset;

    if (auto *SROAArg = getSROAArgForValueOrNull(SelectedV))
      SROAArgValues[&SI] = SROAArg;
  }

  return true;
}

bool CallAnalyzer::visitSwitchInst(SwitchInst &SI) {
  // We model unconditional switches as free, see the comments on handling
  // branches.
  if (isa<ConstantInt>(SI.getCondition()))
    return true;
  if (Value *V = SimplifiedValues.lookup(SI.getCondition()))
    if (isa<ConstantInt>(V))
      return true;

  // Assume the most general case where the switch is lowered into
  // either a jump table, bit test, or a balanced binary tree consisting of
  // case clusters without merging adjacent clusters with the same
  // destination. We do not consider the switches that are lowered with a mix
  // of jump table/bit test/binary search tree. The cost of the switch is
  // proportional to the size of the tree or the size of jump table range.
  //
  // NB: We convert large switches which are just used to initialize large phi
  // nodes to lookup tables instead in simplifycfg, so this shouldn't prevent
  // inlining those. It will prevent inlining in cases where the optimization
  // does not (yet) fire.

  unsigned JumpTableSize = 0;
  BlockFrequencyInfo *BFI = GetBFI ? &(GetBFI(F)) : nullptr;
  unsigned NumCaseCluster =
      TTI.getEstimatedNumberOfCaseClusters(SI, JumpTableSize, PSI, BFI);

  onFinalizeSwitch(JumpTableSize, NumCaseCluster, SI.defaultDestUndefined());
  return false;
}

bool CallAnalyzer::visitIndirectBrInst(IndirectBrInst &IBI) {
  // We never want to inline functions that contain an indirectbr.  This is
  // incorrect because all the blockaddress's (in static global initializers
  // for example) would be referring to the original function, and this
  // indirect jump would jump from the inlined copy of the function into the
  // original function which is extremely undefined behavior.
  // FIXME: This logic isn't really right; we can safely inline functions with
  // indirectbr's as long as no other function or global references the
  // blockaddress of a block within the current function.
  HasIndirectBr = true;
  return false;
}

bool CallAnalyzer::visitResumeInst(ResumeInst &RI) {
  // FIXME: It's not clear that a single instruction is an accurate model for
  // the inline cost of a resume instruction.
  return false;
}

bool CallAnalyzer::visitCleanupReturnInst(CleanupReturnInst &CRI) {
  // FIXME: It's not clear that a single instruction is an accurate model for
  // the inline cost of a cleanupret instruction.
  return false;
}

bool CallAnalyzer::visitCatchReturnInst(CatchReturnInst &CRI) {
  // FIXME: It's not clear that a single instruction is an accurate model for
  // the inline cost of a catchret instruction.
  return false;
}

bool CallAnalyzer::visitUnreachableInst(UnreachableInst &I) {
  // FIXME: It might be reasonably to discount the cost of instructions leading
  // to unreachable as they have the lowest possible impact on both runtime and
  // code size.
  return true; // No actual code is needed for unreachable.
}

bool CallAnalyzer::visitInstruction(Instruction &I) {
  // Some instructions are free. All of the free intrinsics can also be
  // handled by SROA, etc.
  if (TTI.getInstructionCost(&I, TargetTransformInfo::TCK_SizeAndLatency) ==
      TargetTransformInfo::TCC_Free)
    return true;

  // We found something we don't understand or can't handle. Mark any SROA-able
  // values in the operand list as no longer viable.
  for (const Use &Op : I.operands())
    disableSROA(Op);

  return false;
}

/// Analyze a basic block for its contribution to the inline cost.
///
/// This method walks the analyzer over every instruction in the given basic
/// block and accounts for their cost during inlining at this callsite. It
/// aborts early if the threshold has been exceeded or an impossible to inline
/// construct has been detected. It returns false if inlining is no longer
/// viable, and true if inlining remains viable.
InlineResult
CallAnalyzer::analyzeBlock(BasicBlock *BB,
                           SmallPtrSetImpl<const Value *> &EphValues) {
  for (Instruction &I : *BB) {
    // FIXME: Currently, the number of instructions in a function regardless of
    // our ability to simplify them during inline to constants or dead code,
    // are actually used by the vector bonus heuristic. As long as that's true,
    // we have to special case debug intrinsics here to prevent differences in
    // inlining due to debug symbols. Eventually, the number of unsimplified
    // instructions shouldn't factor into the cost computation, but until then,
    // hack around it here.
    // Similarly, skip pseudo-probes.
    if (I.isDebugOrPseudoInst())
      continue;

    // Skip ephemeral values.
    if (EphValues.count(&I))
      continue;

    ++NumInstructions;
    if (isa<ExtractElementInst>(I) || I.getType()->isVectorTy())
      ++NumVectorInstructions;

    // If the instruction simplified to a constant, there is no cost to this
    // instruction. Visit the instructions using our InstVisitor to account for
    // all of the per-instruction logic. The visit tree returns true if we
    // consumed the instruction in any way, and false if the instruction's base
    // cost should count against inlining.
    onInstructionAnalysisStart(&I);

    if (Base::visit(&I))
      ++NumInstructionsSimplified;
    else
      onMissedSimplification();

    onInstructionAnalysisFinish(&I);
    using namespace ore;
    // If the visit this instruction detected an uninlinable pattern, abort.
    InlineResult IR = InlineResult::success();
    if (IsRecursiveCall && !AllowRecursiveCall)
      IR = InlineResult::failure("recursive");
    else if (ExposesReturnsTwice)
      IR = InlineResult::failure("exposes returns twice");
    else if (HasDynamicAlloca)
      IR = InlineResult::failure("dynamic alloca");
    else if (HasIndirectBr)
      IR = InlineResult::failure("indirect branch");
    else if (HasUninlineableIntrinsic)
      IR = InlineResult::failure("uninlinable intrinsic");
    else if (InitsVargArgs)
      IR = InlineResult::failure("varargs");
    if (!IR.isSuccess()) {
      if (ORE)
        ORE->emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE, "NeverInline",
                                          &CandidateCall)
                 << NV("Callee", &F) << " has uninlinable pattern ("
                 << NV("InlineResult", IR.getFailureReason())
                 << ") and cost is not fully computed";
        });
      return IR;
    }

    // If the caller is a recursive function then we don't want to inline
    // functions which allocate a lot of stack space because it would increase
    // the caller stack usage dramatically.
    if (IsCallerRecursive && AllocatedSize > RecurStackSizeThreshold) {
      auto IR =
          InlineResult::failure("recursive and allocates too much stack space");
      if (ORE)
        ORE->emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE, "NeverInline",
                                          &CandidateCall)
                 << NV("Callee", &F) << " is "
                 << NV("InlineResult", IR.getFailureReason())
                 << ". Cost is not fully computed";
        });
      return IR;
    }

    if (shouldStop())
      return InlineResult::failure(
          "Call site analysis is not favorable to inlining.");
  }

  return InlineResult::success();
}

/// Compute the base pointer and cumulative constant offsets for V.
///
/// This strips all constant offsets off of V, leaving it the base pointer, and
/// accumulates the total constant offset applied in the returned constant. It
/// returns 0 if V is not a pointer, and returns the constant '0' if there are
/// no constant offsets applied.
ConstantInt *CallAnalyzer::stripAndComputeInBoundsConstantOffsets(Value *&V) {
  if (!V->getType()->isPointerTy())
    return nullptr;

  unsigned AS = V->getType()->getPointerAddressSpace();
  unsigned IntPtrWidth = DL.getIndexSizeInBits(AS);
  APInt Offset = APInt::getZero(IntPtrWidth);

  // Even though we don't look through PHI nodes, we could be called on an
  // instruction in an unreachable block, which may be on a cycle.
  SmallPtrSet<Value *, 4> Visited;
  Visited.insert(V);
  do {
    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
      if (!GEP->isInBounds() || !accumulateGEPOffset(*GEP, Offset))
        return nullptr;
      V = GEP->getPointerOperand();
    } else if (GlobalAlias *GA = dyn_cast<GlobalAlias>(V)) {
      if (GA->isInterposable())
        break;
      V = GA->getAliasee();
    } else {
      break;
    }
    assert(V->getType()->isPointerTy() && "Unexpected operand type!");
  } while (Visited.insert(V).second);

  Type *IdxPtrTy = DL.getIndexType(V->getType());
  return cast<ConstantInt>(ConstantInt::get(IdxPtrTy, Offset));
}

/// Find dead blocks due to deleted CFG edges during inlining.
///
/// If we know the successor of the current block, \p CurrBB, has to be \p
/// NextBB, the other successors of \p CurrBB are dead if these successors have
/// no live incoming CFG edges.  If one block is found to be dead, we can
/// continue growing the dead block list by checking the successors of the dead
/// blocks to see if all their incoming edges are dead or not.
void CallAnalyzer::findDeadBlocks(BasicBlock *CurrBB, BasicBlock *NextBB) {
  auto IsEdgeDead = [&](BasicBlock *Pred, BasicBlock *Succ) {
    // A CFG edge is dead if the predecessor is dead or the predecessor has a
    // known successor which is not the one under exam.
    return (DeadBlocks.count(Pred) ||
            (KnownSuccessors[Pred] && KnownSuccessors[Pred] != Succ));
  };

  auto IsNewlyDead = [&](BasicBlock *BB) {
    // If all the edges to a block are dead, the block is also dead.
    return (!DeadBlocks.count(BB) &&
            llvm::all_of(predecessors(BB),
                         [&](BasicBlock *P) { return IsEdgeDead(P, BB); }));
  };

  for (BasicBlock *Succ : successors(CurrBB)) {
    if (Succ == NextBB || !IsNewlyDead(Succ))
      continue;
    SmallVector<BasicBlock *, 4> NewDead;
    NewDead.push_back(Succ);
    while (!NewDead.empty()) {
      BasicBlock *Dead = NewDead.pop_back_val();
      if (DeadBlocks.insert(Dead).second)
        // Continue growing the dead block lists.
        for (BasicBlock *S : successors(Dead))
          if (IsNewlyDead(S))
            NewDead.push_back(S);
    }
  }
}

/// Analyze a call site for potential inlining.
///
/// Returns true if inlining this call is viable, and false if it is not
/// viable. It computes the cost and adjusts the threshold based on numerous
/// factors and heuristics. If this method returns false but the computed cost
/// is below the computed threshold, then inlining was forcibly disabled by
/// some artifact of the routine.
InlineResult CallAnalyzer::analyze() {
  ++NumCallsAnalyzed;

  auto Result = onAnalysisStart();
  if (!Result.isSuccess())
    return Result;

  if (F.empty())
    return InlineResult::success();

  Function *Caller = CandidateCall.getFunction();
  // Check if the caller function is recursive itself.
  for (User *U : Caller->users()) {
    CallBase *Call = dyn_cast<CallBase>(U);
    if (Call && Call->getFunction() == Caller) {
      IsCallerRecursive = true;
      break;
    }
  }

  // Populate our simplified values by mapping from function arguments to call
  // arguments with known important simplifications.
  auto CAI = CandidateCall.arg_begin();
  for (Argument &FAI : F.args()) {
    assert(CAI != CandidateCall.arg_end());
    if (Constant *C = dyn_cast<Constant>(CAI))
      SimplifiedValues[&FAI] = C;

    Value *PtrArg = *CAI;
    if (ConstantInt *C = stripAndComputeInBoundsConstantOffsets(PtrArg)) {
      ConstantOffsetPtrs[&FAI] = std::make_pair(PtrArg, C->getValue());

      // We can SROA any pointer arguments derived from alloca instructions.
      if (auto *SROAArg = dyn_cast<AllocaInst>(PtrArg)) {
        SROAArgValues[&FAI] = SROAArg;
        onInitializeSROAArg(SROAArg);
        EnabledSROAAllocas.insert(SROAArg);
      }
    }
    ++CAI;
  }
  NumConstantArgs = SimplifiedValues.size();
  NumConstantOffsetPtrArgs = ConstantOffsetPtrs.size();
  NumAllocaArgs = SROAArgValues.size();

  // FIXME: If a caller has multiple calls to a callee, we end up recomputing
  // the ephemeral values multiple times (and they're completely determined by
  // the callee, so this is purely duplicate work).
  SmallPtrSet<const Value *, 32> EphValues;
  CodeMetrics::collectEphemeralValues(&F, &GetAssumptionCache(F), EphValues);

  // The worklist of live basic blocks in the callee *after* inlining. We avoid
  // adding basic blocks of the callee which can be proven to be dead for this
  // particular call site in order to get more accurate cost estimates. This
  // requires a somewhat heavyweight iteration pattern: we need to walk the
  // basic blocks in a breadth-first order as we insert live successors. To
  // accomplish this, prioritizing for small iterations because we exit after
  // crossing our threshold, we use a small-size optimized SetVector.
  typedef SmallSetVector<BasicBlock *, 16> BBSetVector;
  BBSetVector BBWorklist;
  BBWorklist.insert(&F.getEntryBlock());

  // Note that we *must not* cache the size, this loop grows the worklist.
  for (unsigned Idx = 0; Idx != BBWorklist.size(); ++Idx) {
    if (shouldStop())
      break;

    BasicBlock *BB = BBWorklist[Idx];
    if (BB->empty())
      continue;

    onBlockStart(BB);

    // Disallow inlining a blockaddress with uses other than strictly callbr.
    // A blockaddress only has defined behavior for an indirect branch in the
    // same function, and we do not currently support inlining indirect
    // branches.  But, the inliner may not see an indirect branch that ends up
    // being dead code at a particular call site. If the blockaddress escapes
    // the function, e.g., via a global variable, inlining may lead to an
    // invalid cross-function reference.
    // FIXME: pr/39560: continue relaxing this overt restriction.
    if (BB->hasAddressTaken())
      for (User *U : BlockAddress::get(&*BB)->users())
        if (!isa<CallBrInst>(*U))
          return InlineResult::failure("blockaddress used outside of callbr");

    // Analyze the cost of this block. If we blow through the threshold, this
    // returns false, and we can bail on out.
    InlineResult IR = analyzeBlock(BB, EphValues);
    if (!IR.isSuccess())
      return IR;

    Instruction *TI = BB->getTerminator();

    // Add in the live successors by first checking whether we have terminator
    // that may be simplified based on the values simplified by this call.
    if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
      if (BI->isConditional()) {
        Value *Cond = BI->getCondition();
        if (ConstantInt *SimpleCond =
                dyn_cast_or_null<ConstantInt>(SimplifiedValues.lookup(Cond))) {
          BasicBlock *NextBB = BI->getSuccessor(SimpleCond->isZero() ? 1 : 0);
          BBWorklist.insert(NextBB);
          KnownSuccessors[BB] = NextBB;
          findDeadBlocks(BB, NextBB);
          continue;
        }
      }
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(TI)) {
      Value *Cond = SI->getCondition();
      if (ConstantInt *SimpleCond =
              dyn_cast_or_null<ConstantInt>(SimplifiedValues.lookup(Cond))) {
        BasicBlock *NextBB = SI->findCaseValue(SimpleCond)->getCaseSuccessor();
        BBWorklist.insert(NextBB);
        KnownSuccessors[BB] = NextBB;
        findDeadBlocks(BB, NextBB);
        continue;
      }
    }

    // If we're unable to select a particular successor, just count all of
    // them.
    for (BasicBlock *Succ : successors(BB))
      BBWorklist.insert(Succ);

    onBlockAnalyzed(BB);
  }

  // If this is a noduplicate call, we can still inline as long as
  // inlining this would cause the removal of the caller (so the instruction
  // is not actually duplicated, just moved).
  if (!isSoleCallToLocalFunction(CandidateCall, F) && ContainsNoDuplicateCall)
    return InlineResult::failure("noduplicate");

  // If the callee's stack size exceeds the user-specified threshold,
  // do not let it be inlined.
  // The command line option overrides a limit set in the function attributes.
  size_t FinalStackSizeThreshold = StackSizeThreshold;
  if (!StackSizeThreshold.getNumOccurrences())
    if (std::optional<int> AttrMaxStackSize = getStringFnAttrAsInt(
            Caller, InlineConstants::MaxInlineStackSizeAttributeName))
      FinalStackSizeThreshold = *AttrMaxStackSize;
  if (AllocatedSize > FinalStackSizeThreshold)
    return InlineResult::failure("stacksize");

  return finalizeAnalysis();
}

void InlineCostCallAnalyzer::print(raw_ostream &OS) {
#define DEBUG_PRINT_STAT(x) OS << "      " #x ": " << x << "\n"
  if (PrintInstructionComments)
    F.print(OS, &Writer);
  DEBUG_PRINT_STAT(NumConstantArgs);
  DEBUG_PRINT_STAT(NumConstantOffsetPtrArgs);
  DEBUG_PRINT_STAT(NumAllocaArgs);
  DEBUG_PRINT_STAT(NumConstantPtrCmps);
  DEBUG_PRINT_STAT(NumConstantPtrDiffs);
  DEBUG_PRINT_STAT(NumInstructionsSimplified);
  DEBUG_PRINT_STAT(NumInstructions);
  DEBUG_PRINT_STAT(SROACostSavings);
  DEBUG_PRINT_STAT(SROACostSavingsLost);
  DEBUG_PRINT_STAT(LoadEliminationCost);
  DEBUG_PRINT_STAT(ContainsNoDuplicateCall);
  DEBUG_PRINT_STAT(Cost);
  DEBUG_PRINT_STAT(Threshold);
#undef DEBUG_PRINT_STAT
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// Dump stats about this call's analysis.
LLVM_DUMP_METHOD void InlineCostCallAnalyzer::dump() { print(dbgs()); }
#endif

/// Test that there are no attribute conflicts between Caller and Callee
///        that prevent inlining.
static bool functionsHaveCompatibleAttributes(
    Function *Caller, Function *Callee, TargetTransformInfo &TTI,
    function_ref<const TargetLibraryInfo &(Function &)> &GetTLI) {
  // Note that CalleeTLI must be a copy not a reference. The legacy pass manager
  // caches the most recently created TLI in the TargetLibraryInfoWrapperPass
  // object, and always returns the same object (which is overwritten on each
  // GetTLI call). Therefore we copy the first result.
  auto CalleeTLI = GetTLI(*Callee);
  return (IgnoreTTIInlineCompatible ||
          TTI.areInlineCompatible(Caller, Callee)) &&
         GetTLI(*Caller).areInlineCompatible(CalleeTLI,
                                             InlineCallerSupersetNoBuiltin) &&
         AttributeFuncs::areInlineCompatible(*Caller, *Callee);
}

int llvm::getCallsiteCost(const TargetTransformInfo &TTI, const CallBase &Call,
                          const DataLayout &DL) {
  int64_t Cost = 0;
  for (unsigned I = 0, E = Call.arg_size(); I != E; ++I) {
    if (Call.isByValArgument(I)) {
      // We approximate the number of loads and stores needed by dividing the
      // size of the byval type by the target's pointer size.
      PointerType *PTy = cast<PointerType>(Call.getArgOperand(I)->getType());
      unsigned TypeSize = DL.getTypeSizeInBits(Call.getParamByValType(I));
      unsigned AS = PTy->getAddressSpace();
      unsigned PointerSize = DL.getPointerSizeInBits(AS);
      // Ceiling division.
      unsigned NumStores = (TypeSize + PointerSize - 1) / PointerSize;

      // If it generates more than 8 stores it is likely to be expanded as an
      // inline memcpy so we take that as an upper bound. Otherwise we assume
      // one load and one store per word copied.
      // FIXME: The maxStoresPerMemcpy setting from the target should be used
      // here instead of a magic number of 8, but it's not available via
      // DataLayout.
      NumStores = std::min(NumStores, 8U);

      Cost += 2 * NumStores * InstrCost;
    } else {
      // For non-byval arguments subtract off one instruction per call
      // argument.
      Cost += InstrCost;
    }
  }
  // The call instruction also disappears after inlining.
  Cost += InstrCost;
  Cost += TTI.getInlineCallPenalty(Call.getCaller(), Call, CallPenalty);

  return std::min<int64_t>(Cost, INT_MAX);
}

InlineCost llvm::getInlineCost(
    CallBase &Call, const InlineParams &Params, TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<const TargetLibraryInfo &(Function &)> GetTLI,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI,
    ProfileSummaryInfo *PSI, OptimizationRemarkEmitter *ORE) {
  return getInlineCost(Call, Call.getCalledFunction(), Params, CalleeTTI,
                       GetAssumptionCache, GetTLI, GetBFI, PSI, ORE);
}

std::optional<int> llvm::getInliningCostEstimate(
    CallBase &Call, TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI,
    ProfileSummaryInfo *PSI, OptimizationRemarkEmitter *ORE) {
  const InlineParams Params = {/* DefaultThreshold*/ 0,
                               /*HintThreshold*/ {},
                               /*ColdThreshold*/ {},
                               /*OptSizeThreshold*/ {},
                               /*OptMinSizeThreshold*/ {},
                               /*HotCallSiteThreshold*/ {},
                               /*LocallyHotCallSiteThreshold*/ {},
                               /*ColdCallSiteThreshold*/ {},
                               /*ComputeFullInlineCost*/ true,
                               /*EnableDeferral*/ true};

  InlineCostCallAnalyzer CA(*Call.getCalledFunction(), Call, Params, CalleeTTI,
                            GetAssumptionCache, GetBFI, PSI, ORE, true,
                            /*IgnoreThreshold*/ true);
  auto R = CA.analyze();
  if (!R.isSuccess())
    return std::nullopt;
  return CA.getCost();
}

std::optional<InlineCostFeatures> llvm::getInliningCostFeatures(
    CallBase &Call, TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI,
    ProfileSummaryInfo *PSI, OptimizationRemarkEmitter *ORE) {
  InlineCostFeaturesAnalyzer CFA(CalleeTTI, GetAssumptionCache, GetBFI, PSI,
                                 ORE, *Call.getCalledFunction(), Call);
  auto R = CFA.analyze();
  if (!R.isSuccess())
    return std::nullopt;
  return CFA.features();
}

std::optional<InlineResult> llvm::getAttributeBasedInliningDecision(
    CallBase &Call, Function *Callee, TargetTransformInfo &CalleeTTI,
    function_ref<const TargetLibraryInfo &(Function &)> GetTLI) {

  // Cannot inline indirect calls.
  if (!Callee)
    return InlineResult::failure("indirect call");

  // When callee coroutine function is inlined into caller coroutine function
  // before coro-split pass,
  // coro-early pass can not handle this quiet well.
  // So we won't inline the coroutine function if it have not been unsplited
  if (Callee->isPresplitCoroutine())
    return InlineResult::failure("unsplited coroutine call");

  // Never inline calls with byval arguments that does not have the alloca
  // address space. Since byval arguments can be replaced with a copy to an
  // alloca, the inlined code would need to be adjusted to handle that the
  // argument is in the alloca address space (so it is a little bit complicated
  // to solve).
  unsigned AllocaAS = Callee->getDataLayout().getAllocaAddrSpace();
  for (unsigned I = 0, E = Call.arg_size(); I != E; ++I)
    if (Call.isByValArgument(I)) {
      PointerType *PTy = cast<PointerType>(Call.getArgOperand(I)->getType());
      if (PTy->getAddressSpace() != AllocaAS)
        return InlineResult::failure("byval arguments without alloca"
                                     " address space");
    }

  // Calls to functions with always-inline attributes should be inlined
  // whenever possible.
  if (Call.hasFnAttr(Attribute::AlwaysInline)) {
    if (Call.getAttributes().hasFnAttr(Attribute::NoInline))
      return InlineResult::failure("noinline call site attribute");

    auto IsViable = isInlineViable(*Callee);
    if (IsViable.isSuccess())
      return InlineResult::success();
    return InlineResult::failure(IsViable.getFailureReason());
  }

  // Never inline functions with conflicting attributes (unless callee has
  // always-inline attribute).
  Function *Caller = Call.getCaller();
  if (!functionsHaveCompatibleAttributes(Caller, Callee, CalleeTTI, GetTLI))
    return InlineResult::failure("conflicting attributes");

  // Don't inline this call if the caller has the optnone attribute.
  if (Caller->hasOptNone())
    return InlineResult::failure("optnone attribute");

  // Don't inline a function that treats null pointer as valid into a caller
  // that does not have this attribute.
  if (!Caller->nullPointerIsDefined() && Callee->nullPointerIsDefined())
    return InlineResult::failure("nullptr definitions incompatible");

  // Don't inline functions which can be interposed at link-time.
  if (Callee->isInterposable())
    return InlineResult::failure("interposable");

  // Don't inline functions marked noinline.
  if (Callee->hasFnAttribute(Attribute::NoInline))
    return InlineResult::failure("noinline function attribute");

  // Don't inline call sites marked noinline.
  if (Call.isNoInline())
    return InlineResult::failure("noinline call site attribute");

  return std::nullopt;
}

InlineCost llvm::getInlineCost(
    CallBase &Call, Function *Callee, const InlineParams &Params,
    TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<const TargetLibraryInfo &(Function &)> GetTLI,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI,
    ProfileSummaryInfo *PSI, OptimizationRemarkEmitter *ORE) {

  auto UserDecision =
      llvm::getAttributeBasedInliningDecision(Call, Callee, CalleeTTI, GetTLI);

  if (UserDecision) {
    if (UserDecision->isSuccess())
      return llvm::InlineCost::getAlways("always inline attribute");
    return llvm::InlineCost::getNever(UserDecision->getFailureReason());
  }

  LLVM_DEBUG(llvm::dbgs() << "      Analyzing call of " << Callee->getName()
                          << "... (caller:" << Call.getCaller()->getName()
                          << ")\n");

  InlineCostCallAnalyzer CA(*Callee, Call, Params, CalleeTTI,
                            GetAssumptionCache, GetBFI, PSI, ORE);
  InlineResult ShouldInline = CA.analyze();

  LLVM_DEBUG(CA.dump());

  // Always make cost benefit based decision explicit.
  // We use always/never here since threshold is not meaningful,
  // as it's not what drives cost-benefit analysis.
  if (CA.wasDecidedByCostBenefit()) {
    if (ShouldInline.isSuccess())
      return InlineCost::getAlways("benefit over cost",
                                   CA.getCostBenefitPair());
    else
      return InlineCost::getNever("cost over benefit", CA.getCostBenefitPair());
  }

  if (CA.wasDecidedByCostThreshold())
    return InlineCost::get(CA.getCost(), CA.getThreshold(),
                           CA.getStaticBonusApplied());

  // No details on how the decision was made, simply return always or never.
  return ShouldInline.isSuccess()
             ? InlineCost::getAlways("empty function")
             : InlineCost::getNever(ShouldInline.getFailureReason());
}

InlineResult llvm::isInlineViable(Function &F) {
  bool ReturnsTwice = F.hasFnAttribute(Attribute::ReturnsTwice);
  for (BasicBlock &BB : F) {
    // Disallow inlining of functions which contain indirect branches.
    if (isa<IndirectBrInst>(BB.getTerminator()))
      return InlineResult::failure("contains indirect branches");

    // Disallow inlining of blockaddresses which are used by non-callbr
    // instructions.
    if (BB.hasAddressTaken())
      for (User *U : BlockAddress::get(&BB)->users())
        if (!isa<CallBrInst>(*U))
          return InlineResult::failure("blockaddress used outside of callbr");

    for (auto &II : BB) {
      CallBase *Call = dyn_cast<CallBase>(&II);
      if (!Call)
        continue;

      // Disallow recursive calls.
      Function *Callee = Call->getCalledFunction();
      if (&F == Callee)
        return InlineResult::failure("recursive call");

      // Disallow calls which expose returns-twice to a function not previously
      // attributed as such.
      if (!ReturnsTwice && isa<CallInst>(Call) &&
          cast<CallInst>(Call)->canReturnTwice())
        return InlineResult::failure("exposes returns-twice attribute");

      if (Callee)
        switch (Callee->getIntrinsicID()) {
        default:
          break;
        case llvm::Intrinsic::icall_branch_funnel:
          // Disallow inlining of @llvm.icall.branch.funnel because current
          // backend can't separate call targets from call arguments.
          return InlineResult::failure(
              "disallowed inlining of @llvm.icall.branch.funnel");
        case llvm::Intrinsic::localescape:
          // Disallow inlining functions that call @llvm.localescape. Doing this
          // correctly would require major changes to the inliner.
          return InlineResult::failure(
              "disallowed inlining of @llvm.localescape");
        case llvm::Intrinsic::vastart:
          // Disallow inlining of functions that initialize VarArgs with
          // va_start.
          return InlineResult::failure(
              "contains VarArgs initialized with va_start");
        }
    }
  }

  return InlineResult::success();
}

// APIs to create InlineParams based on command line flags and/or other
// parameters.

InlineParams llvm::getInlineParams(int Threshold) {
  InlineParams Params;

  // This field is the threshold to use for a callee by default. This is
  // derived from one or more of:
  //  * optimization or size-optimization levels,
  //  * a value passed to createFunctionInliningPass function, or
  //  * the -inline-threshold flag.
  //  If the -inline-threshold flag is explicitly specified, that is used
  //  irrespective of anything else.
  if (InlineThreshold.getNumOccurrences() > 0)
    Params.DefaultThreshold = InlineThreshold;
  else
    Params.DefaultThreshold = Threshold;

  // Set the HintThreshold knob from the -inlinehint-threshold.
  Params.HintThreshold = HintThreshold;

  // Set the HotCallSiteThreshold knob from the -hot-callsite-threshold.
  Params.HotCallSiteThreshold = HotCallSiteThreshold;

  // If the -locally-hot-callsite-threshold is explicitly specified, use it to
  // populate LocallyHotCallSiteThreshold. Later, we populate
  // Params.LocallyHotCallSiteThreshold from -locally-hot-callsite-threshold if
  // we know that optimization level is O3 (in the getInlineParams variant that
  // takes the opt and size levels).
  // FIXME: Remove this check (and make the assignment unconditional) after
  // addressing size regression issues at O2.
  if (LocallyHotCallSiteThreshold.getNumOccurrences() > 0)
    Params.LocallyHotCallSiteThreshold = LocallyHotCallSiteThreshold;

  // Set the ColdCallSiteThreshold knob from the
  // -inline-cold-callsite-threshold.
  Params.ColdCallSiteThreshold = ColdCallSiteThreshold;

  // Set the OptMinSizeThreshold and OptSizeThreshold params only if the
  // -inlinehint-threshold commandline option is not explicitly given. If that
  // option is present, then its value applies even for callees with size and
  // minsize attributes.
  // If the -inline-threshold is not specified, set the ColdThreshold from the
  // -inlinecold-threshold even if it is not explicitly passed. If
  // -inline-threshold is specified, then -inlinecold-threshold needs to be
  // explicitly specified to set the ColdThreshold knob
  if (InlineThreshold.getNumOccurrences() == 0) {
    Params.OptMinSizeThreshold = InlineConstants::OptMinSizeThreshold;
    Params.OptSizeThreshold = InlineConstants::OptSizeThreshold;
    Params.ColdThreshold = ColdThreshold;
  } else if (ColdThreshold.getNumOccurrences() > 0) {
    Params.ColdThreshold = ColdThreshold;
  }
  return Params;
}

InlineParams llvm::getInlineParams() {
  return getInlineParams(DefaultThreshold);
}

// Compute the default threshold for inlining based on the opt level and the
// size opt level.
static int computeThresholdFromOptLevels(unsigned OptLevel,
                                         unsigned SizeOptLevel) {
  if (OptLevel > 2)
    return InlineConstants::OptAggressiveThreshold;
  if (SizeOptLevel == 1) // -Os
    return InlineConstants::OptSizeThreshold;
  if (SizeOptLevel == 2) // -Oz
    return InlineConstants::OptMinSizeThreshold;
  return DefaultThreshold;
}

InlineParams llvm::getInlineParams(unsigned OptLevel, unsigned SizeOptLevel) {
  auto Params =
      getInlineParams(computeThresholdFromOptLevels(OptLevel, SizeOptLevel));
  // At O3, use the value of -locally-hot-callsite-threshold option to populate
  // Params.LocallyHotCallSiteThreshold. Below O3, this flag has effect only
  // when it is specified explicitly.
  if (OptLevel > 2)
    Params.LocallyHotCallSiteThreshold = LocallyHotCallSiteThreshold;
  return Params;
}

PreservedAnalyses
InlineCostAnnotationPrinterPass::run(Function &F,
                                     FunctionAnalysisManager &FAM) {
  PrintInstructionComments = true;
  std::function<AssumptionCache &(Function &)> GetAssumptionCache =
      [&](Function &F) -> AssumptionCache & {
    return FAM.getResult<AssumptionAnalysis>(F);
  };
  Module *M = F.getParent();
  ProfileSummaryInfo PSI(*M);
  DataLayout DL(M);
  TargetTransformInfo TTI(DL);
  // FIXME: Redesign the usage of InlineParams to expand the scope of this pass.
  // In the current implementation, the type of InlineParams doesn't matter as
  // the pass serves only for verification of inliner's decisions.
  // We can add a flag which determines InlineParams for this run. Right now,
  // the default InlineParams are used.
  const InlineParams Params = llvm::getInlineParams();
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (CallInst *CI = dyn_cast<CallInst>(&I)) {
        Function *CalledFunction = CI->getCalledFunction();
        if (!CalledFunction || CalledFunction->isDeclaration())
          continue;
        OptimizationRemarkEmitter ORE(CalledFunction);
        InlineCostCallAnalyzer ICCA(*CalledFunction, *CI, Params, TTI,
                                    GetAssumptionCache, nullptr, &PSI, &ORE);
        ICCA.analyze();
        OS << "      Analyzing call of " << CalledFunction->getName()
           << "... (caller:" << CI->getCaller()->getName() << ")\n";
        ICCA.print(OS);
        OS << "\n";
      }
    }
  }
  return PreservedAnalyses::all();
}
