//===--- SelectOptimize.cpp - Convert select to branches if profitable ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass converts selects to conditional jumps when profitable.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/SelectOptimize.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/SizeOpts.h"
#include <algorithm>
#include <memory>
#include <queue>
#include <stack>

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "select-optimize"

STATISTIC(NumSelectOptAnalyzed,
          "Number of select groups considered for conversion to branch");
STATISTIC(NumSelectConvertedExpColdOperand,
          "Number of select groups converted due to expensive cold operand");
STATISTIC(NumSelectConvertedHighPred,
          "Number of select groups converted due to high-predictability");
STATISTIC(NumSelectUnPred,
          "Number of select groups not converted due to unpredictability");
STATISTIC(NumSelectColdBB,
          "Number of select groups not converted due to cold basic block");
STATISTIC(NumSelectConvertedLoop,
          "Number of select groups converted due to loop-level analysis");
STATISTIC(NumSelectsConverted, "Number of selects converted");

static cl::opt<unsigned> ColdOperandThreshold(
    "cold-operand-threshold",
    cl::desc("Maximum frequency of path for an operand to be considered cold."),
    cl::init(20), cl::Hidden);

static cl::opt<unsigned> ColdOperandMaxCostMultiplier(
    "cold-operand-max-cost-multiplier",
    cl::desc("Maximum cost multiplier of TCC_expensive for the dependence "
             "slice of a cold operand to be considered inexpensive."),
    cl::init(1), cl::Hidden);

static cl::opt<unsigned>
    GainGradientThreshold("select-opti-loop-gradient-gain-threshold",
                          cl::desc("Gradient gain threshold (%)."),
                          cl::init(25), cl::Hidden);

static cl::opt<unsigned>
    GainCycleThreshold("select-opti-loop-cycle-gain-threshold",
                       cl::desc("Minimum gain per loop (in cycles) threshold."),
                       cl::init(4), cl::Hidden);

static cl::opt<unsigned> GainRelativeThreshold(
    "select-opti-loop-relative-gain-threshold",
    cl::desc(
        "Minimum relative gain per loop threshold (1/X). Defaults to 12.5%"),
    cl::init(8), cl::Hidden);

static cl::opt<unsigned> MispredictDefaultRate(
    "mispredict-default-rate", cl::Hidden, cl::init(25),
    cl::desc("Default mispredict rate (initialized to 25%)."));

static cl::opt<bool>
    DisableLoopLevelHeuristics("disable-loop-level-heuristics", cl::Hidden,
                               cl::init(false),
                               cl::desc("Disable loop-level heuristics."));

namespace {

class SelectOptimizeImpl {
  const TargetMachine *TM = nullptr;
  const TargetSubtargetInfo *TSI = nullptr;
  const TargetLowering *TLI = nullptr;
  const TargetTransformInfo *TTI = nullptr;
  const LoopInfo *LI = nullptr;
  BlockFrequencyInfo *BFI;
  ProfileSummaryInfo *PSI = nullptr;
  OptimizationRemarkEmitter *ORE = nullptr;
  TargetSchedModel TSchedModel;

public:
  SelectOptimizeImpl() = default;
  SelectOptimizeImpl(const TargetMachine *TM) : TM(TM){};
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  bool runOnFunction(Function &F, Pass &P);

  using Scaled64 = ScaledNumber<uint64_t>;

  struct CostInfo {
    /// Predicated cost (with selects as conditional moves).
    Scaled64 PredCost;
    /// Non-predicated cost (with selects converted to branches).
    Scaled64 NonPredCost;
  };

  /// SelectLike is an abstraction over SelectInst and other operations that can
  /// act like selects. For example Or(Zext(icmp), X) can be treated like
  /// select(icmp, X|1, X).
  class SelectLike {
    SelectLike(Instruction *I) : I(I) {}

    /// The select (/or) instruction.
    Instruction *I;
    /// Whether this select is inverted, "not(cond), FalseVal, TrueVal", as
    /// opposed to the original condition.
    bool Inverted = false;

  public:
    /// Match a select or select-like instruction, returning a SelectLike.
    static SelectLike match(Instruction *I) {
      // Select instruction are what we are usually looking for.
      if (isa<SelectInst>(I))
        return SelectLike(I);

      // An Or(zext(i1 X), Y) can also be treated like a select, with condition
      // C and values Y|1 and Y.
      Value *X;
      if (PatternMatch::match(
              I, m_c_Or(m_OneUse(m_ZExt(m_Value(X))), m_Value())) &&
          X->getType()->isIntegerTy(1))
        return SelectLike(I);

      return SelectLike(nullptr);
    }

    bool isValid() { return I; }
    operator bool() { return isValid(); }

    /// Invert the select by inverting the condition and switching the operands.
    void setInverted() {
      assert(!Inverted && "Trying to invert an inverted SelectLike");
      assert(isa<Instruction>(getCondition()) &&
             cast<Instruction>(getCondition())->getOpcode() ==
                 Instruction::Xor);
      Inverted = true;
    }
    bool isInverted() const { return Inverted; }

    Instruction *getI() { return I; }
    const Instruction *getI() const { return I; }

    Type *getType() const { return I->getType(); }

    Value *getNonInvertedCondition() const {
      if (auto *Sel = dyn_cast<SelectInst>(I))
        return Sel->getCondition();
      // Or(zext) case
      if (auto *BO = dyn_cast<BinaryOperator>(I)) {
        Value *X;
        if (PatternMatch::match(BO->getOperand(0),
                                m_OneUse(m_ZExt(m_Value(X)))))
          return X;
        if (PatternMatch::match(BO->getOperand(1),
                                m_OneUse(m_ZExt(m_Value(X)))))
          return X;
      }

      llvm_unreachable("Unhandled case in getCondition");
    }

    /// Return the condition for the SelectLike instruction. For example the
    /// condition of a select or c in `or(zext(c), x)`
    Value *getCondition() const {
      Value *CC = getNonInvertedCondition();
      // For inverted conditions the CC is checked when created to be a not
      // (xor) instruction.
      if (Inverted)
        return cast<Instruction>(CC)->getOperand(0);
      return CC;
    }

    /// Return the true value for the SelectLike instruction. Note this may not
    /// exist for all SelectLike instructions. For example, for `or(zext(c), x)`
    /// the true value would be `or(x,1)`. As this value does not exist, nullptr
    /// is returned.
    Value *getTrueValue(bool HonorInverts = true) const {
      if (Inverted && HonorInverts)
        return getFalseValue(/*HonorInverts=*/false);
      if (auto *Sel = dyn_cast<SelectInst>(I))
        return Sel->getTrueValue();
      // Or(zext) case - The true value is Or(X), so return nullptr as the value
      // does not yet exist.
      if (isa<BinaryOperator>(I))
        return nullptr;

      llvm_unreachable("Unhandled case in getTrueValue");
    }

    /// Return the false value for the SelectLike instruction. For example the
    /// getFalseValue of a select or `x` in `or(zext(c), x)` (which is
    /// `select(c, x|1, x)`)
    Value *getFalseValue(bool HonorInverts = true) const {
      if (Inverted && HonorInverts)
        return getTrueValue(/*HonorInverts=*/false);
      if (auto *Sel = dyn_cast<SelectInst>(I))
        return Sel->getFalseValue();
      // Or(zext) case - return the operand which is not the zext.
      if (auto *BO = dyn_cast<BinaryOperator>(I)) {
        Value *X;
        if (PatternMatch::match(BO->getOperand(0),
                                m_OneUse(m_ZExt(m_Value(X)))))
          return BO->getOperand(1);
        if (PatternMatch::match(BO->getOperand(1),
                                m_OneUse(m_ZExt(m_Value(X)))))
          return BO->getOperand(0);
      }

      llvm_unreachable("Unhandled case in getFalseValue");
    }

    /// Return the NonPredCost cost of the true op, given the costs in
    /// InstCostMap. This may need to be generated for select-like instructions.
    Scaled64 getTrueOpCost(DenseMap<const Instruction *, CostInfo> &InstCostMap,
                           const TargetTransformInfo *TTI) {
      if (isa<SelectInst>(I))
        if (auto *I = dyn_cast<Instruction>(getTrueValue()))
          return InstCostMap.contains(I) ? InstCostMap[I].NonPredCost
                                         : Scaled64::getZero();

      // Or case - add the cost of an extra Or to the cost of the False case.
      if (isa<BinaryOperator>(I))
        if (auto I = dyn_cast<Instruction>(getFalseValue()))
          if (InstCostMap.contains(I)) {
            InstructionCost OrCost = TTI->getArithmeticInstrCost(
                Instruction::Or, I->getType(), TargetTransformInfo::TCK_Latency,
                {TargetTransformInfo::OK_AnyValue,
                 TargetTransformInfo::OP_None},
                {TTI::OK_UniformConstantValue, TTI::OP_PowerOf2});
            return InstCostMap[I].NonPredCost +
                   Scaled64::get(*OrCost.getValue());
          }

      return Scaled64::getZero();
    }

    /// Return the NonPredCost cost of the false op, given the costs in
    /// InstCostMap. This may need to be generated for select-like instructions.
    Scaled64
    getFalseOpCost(DenseMap<const Instruction *, CostInfo> &InstCostMap,
                   const TargetTransformInfo *TTI) {
      if (isa<SelectInst>(I))
        if (auto *I = dyn_cast<Instruction>(getFalseValue()))
          return InstCostMap.contains(I) ? InstCostMap[I].NonPredCost
                                         : Scaled64::getZero();

      // Or case - return the cost of the false case
      if (isa<BinaryOperator>(I))
        if (auto I = dyn_cast<Instruction>(getFalseValue()))
          if (InstCostMap.contains(I))
            return InstCostMap[I].NonPredCost;

      return Scaled64::getZero();
    }
  };

private:
  // Select groups consist of consecutive select instructions with the same
  // condition.
  using SelectGroup = SmallVector<SelectLike, 2>;
  using SelectGroups = SmallVector<SelectGroup, 2>;

  // Converts select instructions of a function to conditional jumps when deemed
  // profitable. Returns true if at least one select was converted.
  bool optimizeSelects(Function &F);

  // Heuristics for determining which select instructions can be profitably
  // conveted to branches. Separate heuristics for selects in inner-most loops
  // and the rest of code regions (base heuristics for non-inner-most loop
  // regions).
  void optimizeSelectsBase(Function &F, SelectGroups &ProfSIGroups);
  void optimizeSelectsInnerLoops(Function &F, SelectGroups &ProfSIGroups);

  // Converts to branches the select groups that were deemed
  // profitable-to-convert.
  void convertProfitableSIGroups(SelectGroups &ProfSIGroups);

  // Splits selects of a given basic block into select groups.
  void collectSelectGroups(BasicBlock &BB, SelectGroups &SIGroups);

  // Determines for which select groups it is profitable converting to branches
  // (base and inner-most-loop heuristics).
  void findProfitableSIGroupsBase(SelectGroups &SIGroups,
                                  SelectGroups &ProfSIGroups);
  void findProfitableSIGroupsInnerLoops(const Loop *L, SelectGroups &SIGroups,
                                        SelectGroups &ProfSIGroups);

  // Determines if a select group should be converted to a branch (base
  // heuristics).
  bool isConvertToBranchProfitableBase(const SelectGroup &ASI);

  // Returns true if there are expensive instructions in the cold value
  // operand's (if any) dependence slice of any of the selects of the given
  // group.
  bool hasExpensiveColdOperand(const SelectGroup &ASI);

  // For a given source instruction, collect its backwards dependence slice
  // consisting of instructions exclusively computed for producing the operands
  // of the source instruction.
  void getExclBackwardsSlice(Instruction *I, std::stack<Instruction *> &Slice,
                             Instruction *SI, bool ForSinking = false);

  // Returns true if the condition of the select is highly predictable.
  bool isSelectHighlyPredictable(const SelectLike SI);

  // Loop-level checks to determine if a non-predicated version (with branches)
  // of the given loop is more profitable than its predicated version.
  bool checkLoopHeuristics(const Loop *L, const CostInfo LoopDepth[2]);

  // Computes instruction and loop-critical-path costs for both the predicated
  // and non-predicated version of the given loop.
  bool computeLoopCosts(const Loop *L, const SelectGroups &SIGroups,
                        DenseMap<const Instruction *, CostInfo> &InstCostMap,
                        CostInfo *LoopCost);

  // Returns a set of all the select instructions in the given select groups.
  SmallDenseMap<const Instruction *, SelectLike, 2>
  getSImap(const SelectGroups &SIGroups);

  // Returns the latency cost of a given instruction.
  std::optional<uint64_t> computeInstCost(const Instruction *I);

  // Returns the misprediction cost of a given select when converted to branch.
  Scaled64 getMispredictionCost(const SelectLike SI, const Scaled64 CondCost);

  // Returns the cost of a branch when the prediction is correct.
  Scaled64 getPredictedPathCost(Scaled64 TrueCost, Scaled64 FalseCost,
                                const SelectLike SI);

  // Returns true if the target architecture supports lowering a given select.
  bool isSelectKindSupported(const SelectLike SI);
};

class SelectOptimize : public FunctionPass {
  SelectOptimizeImpl Impl;

public:
  static char ID;

  SelectOptimize() : FunctionPass(ID) {
    initializeSelectOptimizePass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    return Impl.runOnFunction(F, *this);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<BlockFrequencyInfoWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
  }
};

} // namespace

PreservedAnalyses SelectOptimizePass::run(Function &F,
                                          FunctionAnalysisManager &FAM) {
  SelectOptimizeImpl Impl(TM);
  return Impl.run(F, FAM);
}

char SelectOptimize::ID = 0;

INITIALIZE_PASS_BEGIN(SelectOptimize, DEBUG_TYPE, "Optimize selects", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_END(SelectOptimize, DEBUG_TYPE, "Optimize selects", false,
                    false)

FunctionPass *llvm::createSelectOptimizePass() { return new SelectOptimize(); }

PreservedAnalyses SelectOptimizeImpl::run(Function &F,
                                          FunctionAnalysisManager &FAM) {
  TSI = TM->getSubtargetImpl(F);
  TLI = TSI->getTargetLowering();

  // If none of the select types are supported then skip this pass.
  // This is an optimization pass. Legality issues will be handled by
  // instruction selection.
  if (!TLI->isSelectSupported(TargetLowering::ScalarValSelect) &&
      !TLI->isSelectSupported(TargetLowering::ScalarCondVectorVal) &&
      !TLI->isSelectSupported(TargetLowering::VectorMaskSelect))
    return PreservedAnalyses::all();

  TTI = &FAM.getResult<TargetIRAnalysis>(F);
  if (!TTI->enableSelectOptimize())
    return PreservedAnalyses::all();

  PSI = FAM.getResult<ModuleAnalysisManagerFunctionProxy>(F)
            .getCachedResult<ProfileSummaryAnalysis>(*F.getParent());
  assert(PSI && "This pass requires module analysis pass `profile-summary`!");
  BFI = &FAM.getResult<BlockFrequencyAnalysis>(F);

  // When optimizing for size, selects are preferable over branches.
  if (F.hasOptSize() || llvm::shouldOptimizeForSize(&F, PSI, BFI))
    return PreservedAnalyses::all();

  LI = &FAM.getResult<LoopAnalysis>(F);
  ORE = &FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  TSchedModel.init(TSI);

  bool Changed = optimizeSelects(F);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool SelectOptimizeImpl::runOnFunction(Function &F, Pass &P) {
  TM = &P.getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
  TSI = TM->getSubtargetImpl(F);
  TLI = TSI->getTargetLowering();

  // If none of the select types are supported then skip this pass.
  // This is an optimization pass. Legality issues will be handled by
  // instruction selection.
  if (!TLI->isSelectSupported(TargetLowering::ScalarValSelect) &&
      !TLI->isSelectSupported(TargetLowering::ScalarCondVectorVal) &&
      !TLI->isSelectSupported(TargetLowering::VectorMaskSelect))
    return false;

  TTI = &P.getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);

  if (!TTI->enableSelectOptimize())
    return false;

  LI = &P.getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  BFI = &P.getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
  PSI = &P.getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  ORE = &P.getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE();
  TSchedModel.init(TSI);

  // When optimizing for size, selects are preferable over branches.
  if (F.hasOptSize() || llvm::shouldOptimizeForSize(&F, PSI, BFI))
    return false;

  return optimizeSelects(F);
}

bool SelectOptimizeImpl::optimizeSelects(Function &F) {
  // Determine for which select groups it is profitable converting to branches.
  SelectGroups ProfSIGroups;
  // Base heuristics apply only to non-loops and outer loops.
  optimizeSelectsBase(F, ProfSIGroups);
  // Separate heuristics for inner-most loops.
  optimizeSelectsInnerLoops(F, ProfSIGroups);

  // Convert to branches the select groups that were deemed
  // profitable-to-convert.
  convertProfitableSIGroups(ProfSIGroups);

  // Code modified if at least one select group was converted.
  return !ProfSIGroups.empty();
}

void SelectOptimizeImpl::optimizeSelectsBase(Function &F,
                                             SelectGroups &ProfSIGroups) {
  // Collect all the select groups.
  SelectGroups SIGroups;
  for (BasicBlock &BB : F) {
    // Base heuristics apply only to non-loops and outer loops.
    Loop *L = LI->getLoopFor(&BB);
    if (L && L->isInnermost())
      continue;
    collectSelectGroups(BB, SIGroups);
  }

  // Determine for which select groups it is profitable converting to branches.
  findProfitableSIGroupsBase(SIGroups, ProfSIGroups);
}

void SelectOptimizeImpl::optimizeSelectsInnerLoops(Function &F,
                                                   SelectGroups &ProfSIGroups) {
  SmallVector<Loop *, 4> Loops(LI->begin(), LI->end());
  // Need to check size on each iteration as we accumulate child loops.
  for (unsigned long i = 0; i < Loops.size(); ++i)
    for (Loop *ChildL : Loops[i]->getSubLoops())
      Loops.push_back(ChildL);

  for (Loop *L : Loops) {
    if (!L->isInnermost())
      continue;

    SelectGroups SIGroups;
    for (BasicBlock *BB : L->getBlocks())
      collectSelectGroups(*BB, SIGroups);

    findProfitableSIGroupsInnerLoops(L, SIGroups, ProfSIGroups);
  }
}

/// If \p isTrue is true, return the true value of \p SI, otherwise return
/// false value of \p SI. If the true/false value of \p SI is defined by any
/// select instructions in \p Selects, look through the defining select
/// instruction until the true/false value is not defined in \p Selects.
static Value *
getTrueOrFalseValue(SelectOptimizeImpl::SelectLike SI, bool isTrue,
                    const SmallPtrSet<const Instruction *, 2> &Selects,
                    IRBuilder<> &IB) {
  Value *V = nullptr;
  for (SelectInst *DefSI = dyn_cast<SelectInst>(SI.getI());
       DefSI != nullptr && Selects.count(DefSI);
       DefSI = dyn_cast<SelectInst>(V)) {
    if (DefSI->getCondition() == SI.getCondition())
      V = (isTrue ? DefSI->getTrueValue() : DefSI->getFalseValue());
    else // Handle inverted SI
      V = (!isTrue ? DefSI->getTrueValue() : DefSI->getFalseValue());
  }

  if (isa<BinaryOperator>(SI.getI())) {
    assert(SI.getI()->getOpcode() == Instruction::Or &&
           "Only currently handling Or instructions.");
    V = SI.getFalseValue();
    if (isTrue)
      V = IB.CreateOr(V, ConstantInt::get(V->getType(), 1));
  }

  assert(V && "Failed to get select true/false value");
  return V;
}

void SelectOptimizeImpl::convertProfitableSIGroups(SelectGroups &ProfSIGroups) {
  for (SelectGroup &ASI : ProfSIGroups) {
    // The code transformation here is a modified version of the sinking
    // transformation in CodeGenPrepare::optimizeSelectInst with a more
    // aggressive strategy of which instructions to sink.
    //
    // TODO: eliminate the redundancy of logic transforming selects to branches
    // by removing CodeGenPrepare::optimizeSelectInst and optimizing here
    // selects for all cases (with and without profile information).

    // Transform a sequence like this:
    //    start:
    //       %cmp = cmp uge i32 %a, %b
    //       %sel = select i1 %cmp, i32 %c, i32 %d
    //
    // Into:
    //    start:
    //       %cmp = cmp uge i32 %a, %b
    //       %cmp.frozen = freeze %cmp
    //       br i1 %cmp.frozen, label %select.true, label %select.false
    //    select.true:
    //       br label %select.end
    //    select.false:
    //       br label %select.end
    //    select.end:
    //       %sel = phi i32 [ %c, %select.true ], [ %d, %select.false ]
    //
    // %cmp should be frozen, otherwise it may introduce undefined behavior.
    // In addition, we may sink instructions that produce %c or %d into the
    // destination(s) of the new branch.
    // If the true or false blocks do not contain a sunken instruction, that
    // block and its branch may be optimized away. In that case, one side of the
    // first branch will point directly to select.end, and the corresponding PHI
    // predecessor block will be the start block.

    // Find all the instructions that can be soundly sunk to the true/false
    // blocks. These are instructions that are computed solely for producing the
    // operands of the select instructions in the group and can be sunk without
    // breaking the semantics of the LLVM IR (e.g., cannot sink instructions
    // with side effects).
    SmallVector<std::stack<Instruction *>, 2> TrueSlices, FalseSlices;
    typedef std::stack<Instruction *>::size_type StackSizeType;
    StackSizeType maxTrueSliceLen = 0, maxFalseSliceLen = 0;
    for (SelectLike SI : ASI) {
      // For each select, compute the sinkable dependence chains of the true and
      // false operands.
      if (auto *TI = dyn_cast_or_null<Instruction>(SI.getTrueValue())) {
        std::stack<Instruction *> TrueSlice;
        getExclBackwardsSlice(TI, TrueSlice, SI.getI(), true);
        maxTrueSliceLen = std::max(maxTrueSliceLen, TrueSlice.size());
        TrueSlices.push_back(TrueSlice);
      }
      if (auto *FI = dyn_cast_or_null<Instruction>(SI.getFalseValue())) {
        if (isa<SelectInst>(SI.getI()) || !FI->hasOneUse()) {
          std::stack<Instruction *> FalseSlice;
          getExclBackwardsSlice(FI, FalseSlice, SI.getI(), true);
          maxFalseSliceLen = std::max(maxFalseSliceLen, FalseSlice.size());
          FalseSlices.push_back(FalseSlice);
        }
      }
    }
    // In the case of multiple select instructions in the same group, the order
    // of non-dependent instructions (instructions of different dependence
    // slices) in the true/false blocks appears to affect performance.
    // Interleaving the slices seems to experimentally be the optimal approach.
    // This interleaving scheduling allows for more ILP (with a natural downside
    // of increasing a bit register pressure) compared to a simple ordering of
    // one whole chain after another. One would expect that this ordering would
    // not matter since the scheduling in the backend of the compiler  would
    // take care of it, but apparently the scheduler fails to deliver optimal
    // ILP with a naive ordering here.
    SmallVector<Instruction *, 2> TrueSlicesInterleaved, FalseSlicesInterleaved;
    for (StackSizeType IS = 0; IS < maxTrueSliceLen; ++IS) {
      for (auto &S : TrueSlices) {
        if (!S.empty()) {
          TrueSlicesInterleaved.push_back(S.top());
          S.pop();
        }
      }
    }
    for (StackSizeType IS = 0; IS < maxFalseSliceLen; ++IS) {
      for (auto &S : FalseSlices) {
        if (!S.empty()) {
          FalseSlicesInterleaved.push_back(S.top());
          S.pop();
        }
      }
    }

    // We split the block containing the select(s) into two blocks.
    SelectLike SI = ASI.front();
    SelectLike LastSI = ASI.back();
    BasicBlock *StartBlock = SI.getI()->getParent();
    BasicBlock::iterator SplitPt = ++(BasicBlock::iterator(LastSI.getI()));
    // With RemoveDIs turned off, SplitPt can be a dbg.* intrinsic. With
    // RemoveDIs turned on, SplitPt would instead point to the next
    // instruction. To match existing dbg.* intrinsic behaviour with RemoveDIs,
    // tell splitBasicBlock that we want to include any DbgVariableRecords
    // attached to SplitPt in the splice.
    SplitPt.setHeadBit(true);
    BasicBlock *EndBlock = StartBlock->splitBasicBlock(SplitPt, "select.end");
    BFI->setBlockFreq(EndBlock, BFI->getBlockFreq(StartBlock));
    // Delete the unconditional branch that was just created by the split.
    StartBlock->getTerminator()->eraseFromParent();

    // Move any debug/pseudo instructions and not's that were in-between the
    // select group to the newly-created end block.
    SmallVector<Instruction *, 2> SinkInstrs;
    auto DIt = SI.getI()->getIterator();
    while (&*DIt != LastSI.getI()) {
      if (DIt->isDebugOrPseudoInst())
        SinkInstrs.push_back(&*DIt);
      if (match(&*DIt, m_Not(m_Specific(SI.getCondition()))))
        SinkInstrs.push_back(&*DIt);
      DIt++;
    }
    for (auto *DI : SinkInstrs)
      DI->moveBeforePreserving(&*EndBlock->getFirstInsertionPt());

    // Duplicate implementation for DbgRecords, the non-instruction debug-info
    // format. Helper lambda for moving DbgRecords to the end block.
    auto TransferDbgRecords = [&](Instruction &I) {
      for (auto &DbgRecord :
           llvm::make_early_inc_range(I.getDbgRecordRange())) {
        DbgRecord.removeFromParent();
        EndBlock->insertDbgRecordBefore(&DbgRecord,
                                        EndBlock->getFirstInsertionPt());
      }
    };

    // Iterate over all instructions in between SI and LastSI, not including
    // SI itself. These are all the variable assignments that happen "in the
    // middle" of the select group.
    auto R = make_range(std::next(SI.getI()->getIterator()),
                        std::next(LastSI.getI()->getIterator()));
    llvm::for_each(R, TransferDbgRecords);

    // These are the new basic blocks for the conditional branch.
    // At least one will become an actual new basic block.
    BasicBlock *TrueBlock = nullptr, *FalseBlock = nullptr;
    BranchInst *TrueBranch = nullptr, *FalseBranch = nullptr;
    if (!TrueSlicesInterleaved.empty()) {
      TrueBlock = BasicBlock::Create(EndBlock->getContext(), "select.true.sink",
                                     EndBlock->getParent(), EndBlock);
      TrueBranch = BranchInst::Create(EndBlock, TrueBlock);
      TrueBranch->setDebugLoc(LastSI.getI()->getDebugLoc());
      for (Instruction *TrueInst : TrueSlicesInterleaved)
        TrueInst->moveBefore(TrueBranch);
    }
    if (!FalseSlicesInterleaved.empty()) {
      FalseBlock =
          BasicBlock::Create(EndBlock->getContext(), "select.false.sink",
                             EndBlock->getParent(), EndBlock);
      FalseBranch = BranchInst::Create(EndBlock, FalseBlock);
      FalseBranch->setDebugLoc(LastSI.getI()->getDebugLoc());
      for (Instruction *FalseInst : FalseSlicesInterleaved)
        FalseInst->moveBefore(FalseBranch);
    }
    // If there was nothing to sink, then arbitrarily choose the 'false' side
    // for a new input value to the PHI.
    if (TrueBlock == FalseBlock) {
      assert(TrueBlock == nullptr &&
             "Unexpected basic block transform while optimizing select");

      FalseBlock = BasicBlock::Create(StartBlock->getContext(), "select.false",
                                      EndBlock->getParent(), EndBlock);
      auto *FalseBranch = BranchInst::Create(EndBlock, FalseBlock);
      FalseBranch->setDebugLoc(SI.getI()->getDebugLoc());
    }

    // Insert the real conditional branch based on the original condition.
    // If we did not create a new block for one of the 'true' or 'false' paths
    // of the condition, it means that side of the branch goes to the end block
    // directly and the path originates from the start block from the point of
    // view of the new PHI.
    BasicBlock *TT, *FT;
    if (TrueBlock == nullptr) {
      TT = EndBlock;
      FT = FalseBlock;
      TrueBlock = StartBlock;
    } else if (FalseBlock == nullptr) {
      TT = TrueBlock;
      FT = EndBlock;
      FalseBlock = StartBlock;
    } else {
      TT = TrueBlock;
      FT = FalseBlock;
    }
    IRBuilder<> IB(SI.getI());
    auto *CondFr = IB.CreateFreeze(SI.getCondition(),
                                   SI.getCondition()->getName() + ".frozen");

    SmallPtrSet<const Instruction *, 2> INS;
    for (auto SI : ASI)
      INS.insert(SI.getI());

    // Use reverse iterator because later select may use the value of the
    // earlier select, and we need to propagate value through earlier select
    // to get the PHI operand.
    for (auto It = ASI.rbegin(); It != ASI.rend(); ++It) {
      SelectLike SI = *It;
      // The select itself is replaced with a PHI Node.
      PHINode *PN = PHINode::Create(SI.getType(), 2, "");
      PN->insertBefore(EndBlock->begin());
      PN->takeName(SI.getI());
      PN->addIncoming(getTrueOrFalseValue(SI, true, INS, IB), TrueBlock);
      PN->addIncoming(getTrueOrFalseValue(SI, false, INS, IB), FalseBlock);
      PN->setDebugLoc(SI.getI()->getDebugLoc());
      SI.getI()->replaceAllUsesWith(PN);
      INS.erase(SI.getI());
      ++NumSelectsConverted;
    }
    IB.CreateCondBr(CondFr, TT, FT, SI.getI());

    // Remove the old select instructions, now that they are not longer used.
    for (auto SI : ASI)
      SI.getI()->eraseFromParent();
  }
}

void SelectOptimizeImpl::collectSelectGroups(BasicBlock &BB,
                                             SelectGroups &SIGroups) {
  BasicBlock::iterator BBIt = BB.begin();
  while (BBIt != BB.end()) {
    Instruction *I = &*BBIt++;
    if (SelectLike SI = SelectLike::match(I)) {
      if (!TTI->shouldTreatInstructionLikeSelect(I))
        continue;

      SelectGroup SIGroup;
      SIGroup.push_back(SI);
      while (BBIt != BB.end()) {
        Instruction *NI = &*BBIt;
        // Debug/pseudo instructions should be skipped and not prevent the
        // formation of a select group.
        if (NI->isDebugOrPseudoInst()) {
          ++BBIt;
          continue;
        }

        // Skip not(select(..)), if the not is part of the same select group
        if (match(NI, m_Not(m_Specific(SI.getCondition())))) {
          ++BBIt;
          continue;
        }

        // We only allow selects in the same group, not other select-like
        // instructions.
        if (!isa<SelectInst>(NI))
          break;

        SelectLike NSI = SelectLike::match(NI);
        if (NSI && SI.getCondition() == NSI.getCondition()) {
          SIGroup.push_back(NSI);
        } else if (NSI && match(NSI.getCondition(),
                                m_Not(m_Specific(SI.getCondition())))) {
          NSI.setInverted();
          SIGroup.push_back(NSI);
        } else
          break;
        ++BBIt;
      }

      // If the select type is not supported, no point optimizing it.
      // Instruction selection will take care of it.
      if (!isSelectKindSupported(SI))
        continue;

      LLVM_DEBUG({
        dbgs() << "New Select group with\n";
        for (auto SI : SIGroup)
          dbgs() << "  " << *SI.getI() << "\n";
      });

      SIGroups.push_back(SIGroup);
    }
  }
}

void SelectOptimizeImpl::findProfitableSIGroupsBase(
    SelectGroups &SIGroups, SelectGroups &ProfSIGroups) {
  for (SelectGroup &ASI : SIGroups) {
    ++NumSelectOptAnalyzed;
    if (isConvertToBranchProfitableBase(ASI))
      ProfSIGroups.push_back(ASI);
  }
}

static void EmitAndPrintRemark(OptimizationRemarkEmitter *ORE,
                               DiagnosticInfoOptimizationBase &Rem) {
  LLVM_DEBUG(dbgs() << Rem.getMsg() << "\n");
  ORE->emit(Rem);
}

void SelectOptimizeImpl::findProfitableSIGroupsInnerLoops(
    const Loop *L, SelectGroups &SIGroups, SelectGroups &ProfSIGroups) {
  NumSelectOptAnalyzed += SIGroups.size();
  // For each select group in an inner-most loop,
  // a branch is more preferable than a select/conditional-move if:
  // i) conversion to branches for all the select groups of the loop satisfies
  //    loop-level heuristics including reducing the loop's critical path by
  //    some threshold (see SelectOptimizeImpl::checkLoopHeuristics); and
  // ii) the total cost of the select group is cheaper with a branch compared
  //     to its predicated version. The cost is in terms of latency and the cost
  //     of a select group is the cost of its most expensive select instruction
  //     (assuming infinite resources and thus fully leveraging available ILP).

  DenseMap<const Instruction *, CostInfo> InstCostMap;
  CostInfo LoopCost[2] = {{Scaled64::getZero(), Scaled64::getZero()},
                          {Scaled64::getZero(), Scaled64::getZero()}};
  if (!computeLoopCosts(L, SIGroups, InstCostMap, LoopCost) ||
      !checkLoopHeuristics(L, LoopCost)) {
    return;
  }

  for (SelectGroup &ASI : SIGroups) {
    // Assuming infinite resources, the cost of a group of instructions is the
    // cost of the most expensive instruction of the group.
    Scaled64 SelectCost = Scaled64::getZero(), BranchCost = Scaled64::getZero();
    for (SelectLike SI : ASI) {
      SelectCost = std::max(SelectCost, InstCostMap[SI.getI()].PredCost);
      BranchCost = std::max(BranchCost, InstCostMap[SI.getI()].NonPredCost);
    }
    if (BranchCost < SelectCost) {
      OptimizationRemark OR(DEBUG_TYPE, "SelectOpti", ASI.front().getI());
      OR << "Profitable to convert to branch (loop analysis). BranchCost="
         << BranchCost.toString() << ", SelectCost=" << SelectCost.toString()
         << ". ";
      EmitAndPrintRemark(ORE, OR);
      ++NumSelectConvertedLoop;
      ProfSIGroups.push_back(ASI);
    } else {
      OptimizationRemarkMissed ORmiss(DEBUG_TYPE, "SelectOpti",
                                      ASI.front().getI());
      ORmiss << "Select is more profitable (loop analysis). BranchCost="
             << BranchCost.toString()
             << ", SelectCost=" << SelectCost.toString() << ". ";
      EmitAndPrintRemark(ORE, ORmiss);
    }
  }
}

bool SelectOptimizeImpl::isConvertToBranchProfitableBase(
    const SelectGroup &ASI) {
  SelectLike SI = ASI.front();
  LLVM_DEBUG(dbgs() << "Analyzing select group containing " << *SI.getI()
                    << "\n");
  OptimizationRemark OR(DEBUG_TYPE, "SelectOpti", SI.getI());
  OptimizationRemarkMissed ORmiss(DEBUG_TYPE, "SelectOpti", SI.getI());

  // Skip cold basic blocks. Better to optimize for size for cold blocks.
  if (PSI->isColdBlock(SI.getI()->getParent(), BFI)) {
    ++NumSelectColdBB;
    ORmiss << "Not converted to branch because of cold basic block. ";
    EmitAndPrintRemark(ORE, ORmiss);
    return false;
  }

  // If unpredictable, branch form is less profitable.
  if (SI.getI()->getMetadata(LLVMContext::MD_unpredictable)) {
    ++NumSelectUnPred;
    ORmiss << "Not converted to branch because of unpredictable branch. ";
    EmitAndPrintRemark(ORE, ORmiss);
    return false;
  }

  // If highly predictable, branch form is more profitable, unless a
  // predictable select is inexpensive in the target architecture.
  if (isSelectHighlyPredictable(SI) && TLI->isPredictableSelectExpensive()) {
    ++NumSelectConvertedHighPred;
    OR << "Converted to branch because of highly predictable branch. ";
    EmitAndPrintRemark(ORE, OR);
    return true;
  }

  // Look for expensive instructions in the cold operand's (if any) dependence
  // slice of any of the selects in the group.
  if (hasExpensiveColdOperand(ASI)) {
    ++NumSelectConvertedExpColdOperand;
    OR << "Converted to branch because of expensive cold operand.";
    EmitAndPrintRemark(ORE, OR);
    return true;
  }

  ORmiss << "Not profitable to convert to branch (base heuristic).";
  EmitAndPrintRemark(ORE, ORmiss);
  return false;
}

static InstructionCost divideNearest(InstructionCost Numerator,
                                     uint64_t Denominator) {
  return (Numerator + (Denominator / 2)) / Denominator;
}

static bool extractBranchWeights(const SelectOptimizeImpl::SelectLike SI,
                                 uint64_t &TrueVal, uint64_t &FalseVal) {
  if (isa<SelectInst>(SI.getI()))
    return extractBranchWeights(*SI.getI(), TrueVal, FalseVal);
  return false;
}

bool SelectOptimizeImpl::hasExpensiveColdOperand(const SelectGroup &ASI) {
  bool ColdOperand = false;
  uint64_t TrueWeight, FalseWeight, TotalWeight;
  if (extractBranchWeights(ASI.front(), TrueWeight, FalseWeight)) {
    uint64_t MinWeight = std::min(TrueWeight, FalseWeight);
    TotalWeight = TrueWeight + FalseWeight;
    // Is there a path with frequency <ColdOperandThreshold% (default:20%) ?
    ColdOperand = TotalWeight * ColdOperandThreshold > 100 * MinWeight;
  } else if (PSI->hasProfileSummary()) {
    OptimizationRemarkMissed ORmiss(DEBUG_TYPE, "SelectOpti",
                                    ASI.front().getI());
    ORmiss << "Profile data available but missing branch-weights metadata for "
              "select instruction. ";
    EmitAndPrintRemark(ORE, ORmiss);
  }
  if (!ColdOperand)
    return false;
  // Check if the cold path's dependence slice is expensive for any of the
  // selects of the group.
  for (SelectLike SI : ASI) {
    Instruction *ColdI = nullptr;
    uint64_t HotWeight;
    if (TrueWeight < FalseWeight) {
      ColdI = dyn_cast_or_null<Instruction>(SI.getTrueValue());
      HotWeight = FalseWeight;
    } else {
      ColdI = dyn_cast_or_null<Instruction>(SI.getFalseValue());
      HotWeight = TrueWeight;
    }
    if (ColdI) {
      std::stack<Instruction *> ColdSlice;
      getExclBackwardsSlice(ColdI, ColdSlice, SI.getI());
      InstructionCost SliceCost = 0;
      while (!ColdSlice.empty()) {
        SliceCost += TTI->getInstructionCost(ColdSlice.top(),
                                             TargetTransformInfo::TCK_Latency);
        ColdSlice.pop();
      }
      // The colder the cold value operand of the select is the more expensive
      // the cmov becomes for computing the cold value operand every time. Thus,
      // the colder the cold operand is the more its cost counts.
      // Get nearest integer cost adjusted for coldness.
      InstructionCost AdjSliceCost =
          divideNearest(SliceCost * HotWeight, TotalWeight);
      if (AdjSliceCost >=
          ColdOperandMaxCostMultiplier * TargetTransformInfo::TCC_Expensive)
        return true;
    }
  }
  return false;
}

// Check if it is safe to move LoadI next to the SI.
// Conservatively assume it is safe only if there is no instruction
// modifying memory in-between the load and the select instruction.
static bool isSafeToSinkLoad(Instruction *LoadI, Instruction *SI) {
  // Assume loads from different basic blocks are unsafe to move.
  if (LoadI->getParent() != SI->getParent())
    return false;
  auto It = LoadI->getIterator();
  while (&*It != SI) {
    if (It->mayWriteToMemory())
      return false;
    It++;
  }
  return true;
}

// For a given source instruction, collect its backwards dependence slice
// consisting of instructions exclusively computed for the purpose of producing
// the operands of the source instruction. As an approximation
// (sufficiently-accurate in practice), we populate this set with the
// instructions of the backwards dependence slice that only have one-use and
// form an one-use chain that leads to the source instruction.
void SelectOptimizeImpl::getExclBackwardsSlice(Instruction *I,
                                               std::stack<Instruction *> &Slice,
                                               Instruction *SI,
                                               bool ForSinking) {
  SmallPtrSet<Instruction *, 2> Visited;
  std::queue<Instruction *> Worklist;
  Worklist.push(I);
  while (!Worklist.empty()) {
    Instruction *II = Worklist.front();
    Worklist.pop();

    // Avoid cycles.
    if (!Visited.insert(II).second)
      continue;

    if (!II->hasOneUse())
      continue;

    // Cannot soundly sink instructions with side-effects.
    // Terminator or phi instructions cannot be sunk.
    // Avoid sinking other select instructions (should be handled separetely).
    if (ForSinking && (II->isTerminator() || II->mayHaveSideEffects() ||
                       isa<SelectInst>(II) || isa<PHINode>(II)))
      continue;

    // Avoid sinking loads in order not to skip state-modifying instructions,
    // that may alias with the loaded address.
    // Only allow sinking of loads within the same basic block that are
    // conservatively proven to be safe.
    if (ForSinking && II->mayReadFromMemory() && !isSafeToSinkLoad(II, SI))
      continue;

    // Avoid considering instructions with less frequency than the source
    // instruction (i.e., avoid colder code regions of the dependence slice).
    if (BFI->getBlockFreq(II->getParent()) < BFI->getBlockFreq(I->getParent()))
      continue;

    // Eligible one-use instruction added to the dependence slice.
    Slice.push(II);

    // Explore all the operands of the current instruction to expand the slice.
    for (Value *Op : II->operand_values())
      if (auto *OpI = dyn_cast<Instruction>(Op))
        Worklist.push(OpI);
  }
}

bool SelectOptimizeImpl::isSelectHighlyPredictable(const SelectLike SI) {
  uint64_t TrueWeight, FalseWeight;
  if (extractBranchWeights(SI, TrueWeight, FalseWeight)) {
    uint64_t Max = std::max(TrueWeight, FalseWeight);
    uint64_t Sum = TrueWeight + FalseWeight;
    if (Sum != 0) {
      auto Probability = BranchProbability::getBranchProbability(Max, Sum);
      if (Probability > TTI->getPredictableBranchThreshold())
        return true;
    }
  }
  return false;
}

bool SelectOptimizeImpl::checkLoopHeuristics(const Loop *L,
                                             const CostInfo LoopCost[2]) {
  // Loop-level checks to determine if a non-predicated version (with branches)
  // of the loop is more profitable than its predicated version.

  if (DisableLoopLevelHeuristics)
    return true;

  OptimizationRemarkMissed ORmissL(DEBUG_TYPE, "SelectOpti",
                                   L->getHeader()->getFirstNonPHI());

  if (LoopCost[0].NonPredCost > LoopCost[0].PredCost ||
      LoopCost[1].NonPredCost >= LoopCost[1].PredCost) {
    ORmissL << "No select conversion in the loop due to no reduction of loop's "
               "critical path. ";
    EmitAndPrintRemark(ORE, ORmissL);
    return false;
  }

  Scaled64 Gain[2] = {LoopCost[0].PredCost - LoopCost[0].NonPredCost,
                      LoopCost[1].PredCost - LoopCost[1].NonPredCost};

  // Profitably converting to branches need to reduce the loop's critical path
  // by at least some threshold (absolute gain of GainCycleThreshold cycles and
  // relative gain of 12.5%).
  if (Gain[1] < Scaled64::get(GainCycleThreshold) ||
      Gain[1] * Scaled64::get(GainRelativeThreshold) < LoopCost[1].PredCost) {
    Scaled64 RelativeGain = Scaled64::get(100) * Gain[1] / LoopCost[1].PredCost;
    ORmissL << "No select conversion in the loop due to small reduction of "
               "loop's critical path. Gain="
            << Gain[1].toString()
            << ", RelativeGain=" << RelativeGain.toString() << "%. ";
    EmitAndPrintRemark(ORE, ORmissL);
    return false;
  }

  // If the loop's critical path involves loop-carried dependences, the gradient
  // of the gain needs to be at least GainGradientThreshold% (defaults to 25%).
  // This check ensures that the latency reduction for the loop's critical path
  // keeps decreasing with sufficient rate beyond the two analyzed loop
  // iterations.
  if (Gain[1] > Gain[0]) {
    Scaled64 GradientGain = Scaled64::get(100) * (Gain[1] - Gain[0]) /
                            (LoopCost[1].PredCost - LoopCost[0].PredCost);
    if (GradientGain < Scaled64::get(GainGradientThreshold)) {
      ORmissL << "No select conversion in the loop due to small gradient gain. "
                 "GradientGain="
              << GradientGain.toString() << "%. ";
      EmitAndPrintRemark(ORE, ORmissL);
      return false;
    }
  }
  // If the gain decreases it is not profitable to convert.
  else if (Gain[1] < Gain[0]) {
    ORmissL
        << "No select conversion in the loop due to negative gradient gain. ";
    EmitAndPrintRemark(ORE, ORmissL);
    return false;
  }

  // Non-predicated version of the loop is more profitable than its
  // predicated version.
  return true;
}

// Computes instruction and loop-critical-path costs for both the predicated
// and non-predicated version of the given loop.
// Returns false if unable to compute these costs due to invalid cost of loop
// instruction(s).
bool SelectOptimizeImpl::computeLoopCosts(
    const Loop *L, const SelectGroups &SIGroups,
    DenseMap<const Instruction *, CostInfo> &InstCostMap, CostInfo *LoopCost) {
  LLVM_DEBUG(dbgs() << "Calculating Latency / IPredCost / INonPredCost of loop "
                    << L->getHeader()->getName() << "\n");
  const auto &SImap = getSImap(SIGroups);
  // Compute instruction and loop-critical-path costs across two iterations for
  // both predicated and non-predicated version.
  const unsigned Iterations = 2;
  for (unsigned Iter = 0; Iter < Iterations; ++Iter) {
    // Cost of the loop's critical path.
    CostInfo &MaxCost = LoopCost[Iter];
    for (BasicBlock *BB : L->getBlocks()) {
      for (const Instruction &I : *BB) {
        if (I.isDebugOrPseudoInst())
          continue;
        // Compute the predicated and non-predicated cost of the instruction.
        Scaled64 IPredCost = Scaled64::getZero(),
                 INonPredCost = Scaled64::getZero();

        // Assume infinite resources that allow to fully exploit the available
        // instruction-level parallelism.
        // InstCost = InstLatency + max(Op1Cost, Op2Cost, … OpNCost)
        for (const Use &U : I.operands()) {
          auto UI = dyn_cast<Instruction>(U.get());
          if (!UI)
            continue;
          if (InstCostMap.count(UI)) {
            IPredCost = std::max(IPredCost, InstCostMap[UI].PredCost);
            INonPredCost = std::max(INonPredCost, InstCostMap[UI].NonPredCost);
          }
        }
        auto ILatency = computeInstCost(&I);
        if (!ILatency) {
          OptimizationRemarkMissed ORmissL(DEBUG_TYPE, "SelectOpti", &I);
          ORmissL << "Invalid instruction cost preventing analysis and "
                     "optimization of the inner-most loop containing this "
                     "instruction. ";
          EmitAndPrintRemark(ORE, ORmissL);
          return false;
        }
        IPredCost += Scaled64::get(*ILatency);
        INonPredCost += Scaled64::get(*ILatency);

        // For a select that can be converted to branch,
        // compute its cost as a branch (non-predicated cost).
        //
        // BranchCost = PredictedPathCost + MispredictCost
        // PredictedPathCost = TrueOpCost * TrueProb + FalseOpCost * FalseProb
        // MispredictCost = max(MispredictPenalty, CondCost) * MispredictRate
        if (SImap.contains(&I)) {
          auto SI = SImap.at(&I);
          Scaled64 TrueOpCost = SI.getTrueOpCost(InstCostMap, TTI);
          Scaled64 FalseOpCost = SI.getFalseOpCost(InstCostMap, TTI);
          Scaled64 PredictedPathCost =
              getPredictedPathCost(TrueOpCost, FalseOpCost, SI);

          Scaled64 CondCost = Scaled64::getZero();
          if (auto *CI = dyn_cast<Instruction>(SI.getCondition()))
            if (InstCostMap.count(CI))
              CondCost = InstCostMap[CI].NonPredCost;
          Scaled64 MispredictCost = getMispredictionCost(SI, CondCost);

          INonPredCost = PredictedPathCost + MispredictCost;
        }
        LLVM_DEBUG(dbgs() << " " << ILatency << "/" << IPredCost << "/"
                          << INonPredCost << " for " << I << "\n");

        InstCostMap[&I] = {IPredCost, INonPredCost};
        MaxCost.PredCost = std::max(MaxCost.PredCost, IPredCost);
        MaxCost.NonPredCost = std::max(MaxCost.NonPredCost, INonPredCost);
      }
    }
    LLVM_DEBUG(dbgs() << "Iteration " << Iter + 1
                      << " MaxCost = " << MaxCost.PredCost << " "
                      << MaxCost.NonPredCost << "\n");
  }
  return true;
}

SmallDenseMap<const Instruction *, SelectOptimizeImpl::SelectLike, 2>
SelectOptimizeImpl::getSImap(const SelectGroups &SIGroups) {
  SmallDenseMap<const Instruction *, SelectLike, 2> SImap;
  for (const SelectGroup &ASI : SIGroups)
    for (SelectLike SI : ASI)
      SImap.try_emplace(SI.getI(), SI);
  return SImap;
}

std::optional<uint64_t>
SelectOptimizeImpl::computeInstCost(const Instruction *I) {
  InstructionCost ICost =
      TTI->getInstructionCost(I, TargetTransformInfo::TCK_Latency);
  if (auto OC = ICost.getValue())
    return std::optional<uint64_t>(*OC);
  return std::nullopt;
}

ScaledNumber<uint64_t>
SelectOptimizeImpl::getMispredictionCost(const SelectLike SI,
                                         const Scaled64 CondCost) {
  uint64_t MispredictPenalty = TSchedModel.getMCSchedModel()->MispredictPenalty;

  // Account for the default misprediction rate when using a branch
  // (conservatively set to 25% by default).
  uint64_t MispredictRate = MispredictDefaultRate;
  // If the select condition is obviously predictable, then the misprediction
  // rate is zero.
  if (isSelectHighlyPredictable(SI))
    MispredictRate = 0;

  // CondCost is included to account for cases where the computation of the
  // condition is part of a long dependence chain (potentially loop-carried)
  // that would delay detection of a misprediction and increase its cost.
  Scaled64 MispredictCost =
      std::max(Scaled64::get(MispredictPenalty), CondCost) *
      Scaled64::get(MispredictRate);
  MispredictCost /= Scaled64::get(100);

  return MispredictCost;
}

// Returns the cost of a branch when the prediction is correct.
// TrueCost * TrueProbability + FalseCost * FalseProbability.
ScaledNumber<uint64_t>
SelectOptimizeImpl::getPredictedPathCost(Scaled64 TrueCost, Scaled64 FalseCost,
                                         const SelectLike SI) {
  Scaled64 PredPathCost;
  uint64_t TrueWeight, FalseWeight;
  if (extractBranchWeights(SI, TrueWeight, FalseWeight)) {
    uint64_t SumWeight = TrueWeight + FalseWeight;
    if (SumWeight != 0) {
      PredPathCost = TrueCost * Scaled64::get(TrueWeight) +
                     FalseCost * Scaled64::get(FalseWeight);
      PredPathCost /= Scaled64::get(SumWeight);
      return PredPathCost;
    }
  }
  // Without branch weight metadata, we assume 75% for the one path and 25% for
  // the other, and pick the result with the biggest cost.
  PredPathCost = std::max(TrueCost * Scaled64::get(3) + FalseCost,
                          FalseCost * Scaled64::get(3) + TrueCost);
  PredPathCost /= Scaled64::get(4);
  return PredPathCost;
}

bool SelectOptimizeImpl::isSelectKindSupported(const SelectLike SI) {
  bool VectorCond = !SI.getCondition()->getType()->isIntegerTy(1);
  if (VectorCond)
    return false;
  TargetLowering::SelectSupportKind SelectKind;
  if (SI.getType()->isVectorTy())
    SelectKind = TargetLowering::ScalarCondVectorVal;
  else
    SelectKind = TargetLowering::ScalarValSelect;
  return TLI->isSelectSupported(SelectKind);
}
