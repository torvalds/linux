//===- LoopVectorizationLegality.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides loop vectorization legality analysis. Original code
// resided in LoopVectorize.cpp for a long time.
//
// At this point, it is implemented as a utility class, not as an analysis
// pass. It should be easy to create an analysis pass around it if there
// is a need (but D45420 needs to happen first).
//

#include "llvm/Transforms/Vectorize/LoopVectorizationLegality.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Transforms/Utils/SizeOpts.h"
#include "llvm/Transforms/Vectorize/LoopVectorize.h"

using namespace llvm;
using namespace PatternMatch;

#define LV_NAME "loop-vectorize"
#define DEBUG_TYPE LV_NAME

static cl::opt<bool>
    EnableIfConversion("enable-if-conversion", cl::init(true), cl::Hidden,
                       cl::desc("Enable if-conversion during vectorization."));

static cl::opt<bool>
AllowStridedPointerIVs("lv-strided-pointer-ivs", cl::init(false), cl::Hidden,
                       cl::desc("Enable recognition of non-constant strided "
                                "pointer induction variables."));

namespace llvm {
cl::opt<bool>
    HintsAllowReordering("hints-allow-reordering", cl::init(true), cl::Hidden,
                         cl::desc("Allow enabling loop hints to reorder "
                                  "FP operations during vectorization."));
}

// TODO: Move size-based thresholds out of legality checking, make cost based
// decisions instead of hard thresholds.
static cl::opt<unsigned> VectorizeSCEVCheckThreshold(
    "vectorize-scev-check-threshold", cl::init(16), cl::Hidden,
    cl::desc("The maximum number of SCEV checks allowed."));

static cl::opt<unsigned> PragmaVectorizeSCEVCheckThreshold(
    "pragma-vectorize-scev-check-threshold", cl::init(128), cl::Hidden,
    cl::desc("The maximum number of SCEV checks allowed with a "
             "vectorize(enable) pragma"));

static cl::opt<LoopVectorizeHints::ScalableForceKind>
    ForceScalableVectorization(
        "scalable-vectorization", cl::init(LoopVectorizeHints::SK_Unspecified),
        cl::Hidden,
        cl::desc("Control whether the compiler can use scalable vectors to "
                 "vectorize a loop"),
        cl::values(
            clEnumValN(LoopVectorizeHints::SK_FixedWidthOnly, "off",
                       "Scalable vectorization is disabled."),
            clEnumValN(
                LoopVectorizeHints::SK_PreferScalable, "preferred",
                "Scalable vectorization is available and favored when the "
                "cost is inconclusive."),
            clEnumValN(
                LoopVectorizeHints::SK_PreferScalable, "on",
                "Scalable vectorization is available and favored when the "
                "cost is inconclusive.")));

/// Maximum vectorization interleave count.
static const unsigned MaxInterleaveFactor = 16;

namespace llvm {

bool LoopVectorizeHints::Hint::validate(unsigned Val) {
  switch (Kind) {
  case HK_WIDTH:
    return isPowerOf2_32(Val) && Val <= VectorizerParams::MaxVectorWidth;
  case HK_INTERLEAVE:
    return isPowerOf2_32(Val) && Val <= MaxInterleaveFactor;
  case HK_FORCE:
    return (Val <= 1);
  case HK_ISVECTORIZED:
  case HK_PREDICATE:
  case HK_SCALABLE:
    return (Val == 0 || Val == 1);
  }
  return false;
}

LoopVectorizeHints::LoopVectorizeHints(const Loop *L,
                                       bool InterleaveOnlyWhenForced,
                                       OptimizationRemarkEmitter &ORE,
                                       const TargetTransformInfo *TTI)
    : Width("vectorize.width", VectorizerParams::VectorizationFactor, HK_WIDTH),
      Interleave("interleave.count", InterleaveOnlyWhenForced, HK_INTERLEAVE),
      Force("vectorize.enable", FK_Undefined, HK_FORCE),
      IsVectorized("isvectorized", 0, HK_ISVECTORIZED),
      Predicate("vectorize.predicate.enable", FK_Undefined, HK_PREDICATE),
      Scalable("vectorize.scalable.enable", SK_Unspecified, HK_SCALABLE),
      TheLoop(L), ORE(ORE) {
  // Populate values with existing loop metadata.
  getHintsFromMetadata();

  // force-vector-interleave overrides DisableInterleaving.
  if (VectorizerParams::isInterleaveForced())
    Interleave.Value = VectorizerParams::VectorizationInterleave;

  // If the metadata doesn't explicitly specify whether to enable scalable
  // vectorization, then decide based on the following criteria (increasing
  // level of priority):
  //  - Target default
  //  - Metadata width
  //  - Force option (always overrides)
  if ((LoopVectorizeHints::ScalableForceKind)Scalable.Value == SK_Unspecified) {
    if (TTI)
      Scalable.Value = TTI->enableScalableVectorization() ? SK_PreferScalable
                                                          : SK_FixedWidthOnly;

    if (Width.Value)
      // If the width is set, but the metadata says nothing about the scalable
      // property, then assume it concerns only a fixed-width UserVF.
      // If width is not set, the flag takes precedence.
      Scalable.Value = SK_FixedWidthOnly;
  }

  // If the flag is set to force any use of scalable vectors, override the loop
  // hints.
  if (ForceScalableVectorization.getValue() !=
      LoopVectorizeHints::SK_Unspecified)
    Scalable.Value = ForceScalableVectorization.getValue();

  // Scalable vectorization is disabled if no preference is specified.
  if ((LoopVectorizeHints::ScalableForceKind)Scalable.Value == SK_Unspecified)
    Scalable.Value = SK_FixedWidthOnly;

  if (IsVectorized.Value != 1)
    // If the vectorization width and interleaving count are both 1 then
    // consider the loop to have been already vectorized because there's
    // nothing more that we can do.
    IsVectorized.Value =
        getWidth() == ElementCount::getFixed(1) && getInterleave() == 1;
  LLVM_DEBUG(if (InterleaveOnlyWhenForced && getInterleave() == 1) dbgs()
             << "LV: Interleaving disabled by the pass manager\n");
}

void LoopVectorizeHints::setAlreadyVectorized() {
  LLVMContext &Context = TheLoop->getHeader()->getContext();

  MDNode *IsVectorizedMD = MDNode::get(
      Context,
      {MDString::get(Context, "llvm.loop.isvectorized"),
       ConstantAsMetadata::get(ConstantInt::get(Context, APInt(32, 1)))});
  MDNode *LoopID = TheLoop->getLoopID();
  MDNode *NewLoopID =
      makePostTransformationMetadata(Context, LoopID,
                                     {Twine(Prefix(), "vectorize.").str(),
                                      Twine(Prefix(), "interleave.").str()},
                                     {IsVectorizedMD});
  TheLoop->setLoopID(NewLoopID);

  // Update internal cache.
  IsVectorized.Value = 1;
}

bool LoopVectorizeHints::allowVectorization(
    Function *F, Loop *L, bool VectorizeOnlyWhenForced) const {
  if (getForce() == LoopVectorizeHints::FK_Disabled) {
    LLVM_DEBUG(dbgs() << "LV: Not vectorizing: #pragma vectorize disable.\n");
    emitRemarkWithHints();
    return false;
  }

  if (VectorizeOnlyWhenForced && getForce() != LoopVectorizeHints::FK_Enabled) {
    LLVM_DEBUG(dbgs() << "LV: Not vectorizing: No #pragma vectorize enable.\n");
    emitRemarkWithHints();
    return false;
  }

  if (getIsVectorized() == 1) {
    LLVM_DEBUG(dbgs() << "LV: Not vectorizing: Disabled/already vectorized.\n");
    // FIXME: Add interleave.disable metadata. This will allow
    // vectorize.disable to be used without disabling the pass and errors
    // to differentiate between disabled vectorization and a width of 1.
    ORE.emit([&]() {
      return OptimizationRemarkAnalysis(vectorizeAnalysisPassName(),
                                        "AllDisabled", L->getStartLoc(),
                                        L->getHeader())
             << "loop not vectorized: vectorization and interleaving are "
                "explicitly disabled, or the loop has already been "
                "vectorized";
    });
    return false;
  }

  return true;
}

void LoopVectorizeHints::emitRemarkWithHints() const {
  using namespace ore;

  ORE.emit([&]() {
    if (Force.Value == LoopVectorizeHints::FK_Disabled)
      return OptimizationRemarkMissed(LV_NAME, "MissedExplicitlyDisabled",
                                      TheLoop->getStartLoc(),
                                      TheLoop->getHeader())
             << "loop not vectorized: vectorization is explicitly disabled";
    else {
      OptimizationRemarkMissed R(LV_NAME, "MissedDetails",
                                 TheLoop->getStartLoc(), TheLoop->getHeader());
      R << "loop not vectorized";
      if (Force.Value == LoopVectorizeHints::FK_Enabled) {
        R << " (Force=" << NV("Force", true);
        if (Width.Value != 0)
          R << ", Vector Width=" << NV("VectorWidth", getWidth());
        if (getInterleave() != 0)
          R << ", Interleave Count=" << NV("InterleaveCount", getInterleave());
        R << ")";
      }
      return R;
    }
  });
}

const char *LoopVectorizeHints::vectorizeAnalysisPassName() const {
  if (getWidth() == ElementCount::getFixed(1))
    return LV_NAME;
  if (getForce() == LoopVectorizeHints::FK_Disabled)
    return LV_NAME;
  if (getForce() == LoopVectorizeHints::FK_Undefined && getWidth().isZero())
    return LV_NAME;
  return OptimizationRemarkAnalysis::AlwaysPrint;
}

bool LoopVectorizeHints::allowReordering() const {
  // Allow the vectorizer to change the order of operations if enabling
  // loop hints are provided
  ElementCount EC = getWidth();
  return HintsAllowReordering &&
         (getForce() == LoopVectorizeHints::FK_Enabled ||
          EC.getKnownMinValue() > 1);
}

void LoopVectorizeHints::getHintsFromMetadata() {
  MDNode *LoopID = TheLoop->getLoopID();
  if (!LoopID)
    return;

  // First operand should refer to the loop id itself.
  assert(LoopID->getNumOperands() > 0 && "requires at least one operand");
  assert(LoopID->getOperand(0) == LoopID && "invalid loop id");

  for (const MDOperand &MDO : llvm::drop_begin(LoopID->operands())) {
    const MDString *S = nullptr;
    SmallVector<Metadata *, 4> Args;

    // The expected hint is either a MDString or a MDNode with the first
    // operand a MDString.
    if (const MDNode *MD = dyn_cast<MDNode>(MDO)) {
      if (!MD || MD->getNumOperands() == 0)
        continue;
      S = dyn_cast<MDString>(MD->getOperand(0));
      for (unsigned i = 1, ie = MD->getNumOperands(); i < ie; ++i)
        Args.push_back(MD->getOperand(i));
    } else {
      S = dyn_cast<MDString>(MDO);
      assert(Args.size() == 0 && "too many arguments for MDString");
    }

    if (!S)
      continue;

    // Check if the hint starts with the loop metadata prefix.
    StringRef Name = S->getString();
    if (Args.size() == 1)
      setHint(Name, Args[0]);
  }
}

void LoopVectorizeHints::setHint(StringRef Name, Metadata *Arg) {
  if (!Name.starts_with(Prefix()))
    return;
  Name = Name.substr(Prefix().size(), StringRef::npos);

  const ConstantInt *C = mdconst::dyn_extract<ConstantInt>(Arg);
  if (!C)
    return;
  unsigned Val = C->getZExtValue();

  Hint *Hints[] = {&Width,        &Interleave, &Force,
                   &IsVectorized, &Predicate,  &Scalable};
  for (auto *H : Hints) {
    if (Name == H->Name) {
      if (H->validate(Val))
        H->Value = Val;
      else
        LLVM_DEBUG(dbgs() << "LV: ignoring invalid hint '" << Name << "'\n");
      break;
    }
  }
}

// Return true if the inner loop \p Lp is uniform with regard to the outer loop
// \p OuterLp (i.e., if the outer loop is vectorized, all the vector lanes
// executing the inner loop will execute the same iterations). This check is
// very constrained for now but it will be relaxed in the future. \p Lp is
// considered uniform if it meets all the following conditions:
//   1) it has a canonical IV (starting from 0 and with stride 1),
//   2) its latch terminator is a conditional branch and,
//   3) its latch condition is a compare instruction whose operands are the
//      canonical IV and an OuterLp invariant.
// This check doesn't take into account the uniformity of other conditions not
// related to the loop latch because they don't affect the loop uniformity.
//
// NOTE: We decided to keep all these checks and its associated documentation
// together so that we can easily have a picture of the current supported loop
// nests. However, some of the current checks don't depend on \p OuterLp and
// would be redundantly executed for each \p Lp if we invoked this function for
// different candidate outer loops. This is not the case for now because we
// don't currently have the infrastructure to evaluate multiple candidate outer
// loops and \p OuterLp will be a fixed parameter while we only support explicit
// outer loop vectorization. It's also very likely that these checks go away
// before introducing the aforementioned infrastructure. However, if this is not
// the case, we should move the \p OuterLp independent checks to a separate
// function that is only executed once for each \p Lp.
static bool isUniformLoop(Loop *Lp, Loop *OuterLp) {
  assert(Lp->getLoopLatch() && "Expected loop with a single latch.");

  // If Lp is the outer loop, it's uniform by definition.
  if (Lp == OuterLp)
    return true;
  assert(OuterLp->contains(Lp) && "OuterLp must contain Lp.");

  // 1.
  PHINode *IV = Lp->getCanonicalInductionVariable();
  if (!IV) {
    LLVM_DEBUG(dbgs() << "LV: Canonical IV not found.\n");
    return false;
  }

  // 2.
  BasicBlock *Latch = Lp->getLoopLatch();
  auto *LatchBr = dyn_cast<BranchInst>(Latch->getTerminator());
  if (!LatchBr || LatchBr->isUnconditional()) {
    LLVM_DEBUG(dbgs() << "LV: Unsupported loop latch branch.\n");
    return false;
  }

  // 3.
  auto *LatchCmp = dyn_cast<CmpInst>(LatchBr->getCondition());
  if (!LatchCmp) {
    LLVM_DEBUG(
        dbgs() << "LV: Loop latch condition is not a compare instruction.\n");
    return false;
  }

  Value *CondOp0 = LatchCmp->getOperand(0);
  Value *CondOp1 = LatchCmp->getOperand(1);
  Value *IVUpdate = IV->getIncomingValueForBlock(Latch);
  if (!(CondOp0 == IVUpdate && OuterLp->isLoopInvariant(CondOp1)) &&
      !(CondOp1 == IVUpdate && OuterLp->isLoopInvariant(CondOp0))) {
    LLVM_DEBUG(dbgs() << "LV: Loop latch condition is not uniform.\n");
    return false;
  }

  return true;
}

// Return true if \p Lp and all its nested loops are uniform with regard to \p
// OuterLp.
static bool isUniformLoopNest(Loop *Lp, Loop *OuterLp) {
  if (!isUniformLoop(Lp, OuterLp))
    return false;

  // Check if nested loops are uniform.
  for (Loop *SubLp : *Lp)
    if (!isUniformLoopNest(SubLp, OuterLp))
      return false;

  return true;
}

static Type *convertPointerToIntegerType(const DataLayout &DL, Type *Ty) {
  if (Ty->isPointerTy())
    return DL.getIntPtrType(Ty);

  // It is possible that char's or short's overflow when we ask for the loop's
  // trip count, work around this by changing the type size.
  if (Ty->getScalarSizeInBits() < 32)
    return Type::getInt32Ty(Ty->getContext());

  return Ty;
}

static Type *getWiderType(const DataLayout &DL, Type *Ty0, Type *Ty1) {
  Ty0 = convertPointerToIntegerType(DL, Ty0);
  Ty1 = convertPointerToIntegerType(DL, Ty1);
  if (Ty0->getScalarSizeInBits() > Ty1->getScalarSizeInBits())
    return Ty0;
  return Ty1;
}

/// Check that the instruction has outside loop users and is not an
/// identified reduction variable.
static bool hasOutsideLoopUser(const Loop *TheLoop, Instruction *Inst,
                               SmallPtrSetImpl<Value *> &AllowedExit) {
  // Reductions, Inductions and non-header phis are allowed to have exit users. All
  // other instructions must not have external users.
  if (!AllowedExit.count(Inst))
    // Check that all of the users of the loop are inside the BB.
    for (User *U : Inst->users()) {
      Instruction *UI = cast<Instruction>(U);
      // This user may be a reduction exit value.
      if (!TheLoop->contains(UI)) {
        LLVM_DEBUG(dbgs() << "LV: Found an outside user for : " << *UI << '\n');
        return true;
      }
    }
  return false;
}

/// Returns true if A and B have same pointer operands or same SCEVs addresses
static bool storeToSameAddress(ScalarEvolution *SE, StoreInst *A,
                               StoreInst *B) {
  // Compare store
  if (A == B)
    return true;

  // Otherwise Compare pointers
  Value *APtr = A->getPointerOperand();
  Value *BPtr = B->getPointerOperand();
  if (APtr == BPtr)
    return true;

  // Otherwise compare address SCEVs
  if (SE->getSCEV(APtr) == SE->getSCEV(BPtr))
    return true;

  return false;
}

int LoopVectorizationLegality::isConsecutivePtr(Type *AccessTy,
                                                Value *Ptr) const {
  // FIXME: Currently, the set of symbolic strides is sometimes queried before
  // it's collected.  This happens from canVectorizeWithIfConvert, when the
  // pointer is checked to reference consecutive elements suitable for a
  // masked access.
  const auto &Strides =
    LAI ? LAI->getSymbolicStrides() : DenseMap<Value *, const SCEV *>();

  Function *F = TheLoop->getHeader()->getParent();
  bool OptForSize = F->hasOptSize() ||
                    llvm::shouldOptimizeForSize(TheLoop->getHeader(), PSI, BFI,
                                                PGSOQueryType::IRPass);
  bool CanAddPredicate = !OptForSize;
  int Stride = getPtrStride(PSE, AccessTy, Ptr, TheLoop, Strides,
                            CanAddPredicate, false).value_or(0);
  if (Stride == 1 || Stride == -1)
    return Stride;
  return 0;
}

bool LoopVectorizationLegality::isInvariant(Value *V) const {
  return LAI->isInvariant(V);
}

namespace {
/// A rewriter to build the SCEVs for each of the VF lanes in the expected
/// vectorized loop, which can then be compared to detect their uniformity. This
/// is done by replacing the AddRec SCEVs of the original scalar loop (TheLoop)
/// with new AddRecs where the step is multiplied by StepMultiplier and Offset *
/// Step is added. Also checks if all sub-expressions are analyzable w.r.t.
/// uniformity.
class SCEVAddRecForUniformityRewriter
    : public SCEVRewriteVisitor<SCEVAddRecForUniformityRewriter> {
  /// Multiplier to be applied to the step of AddRecs in TheLoop.
  unsigned StepMultiplier;

  /// Offset to be added to the AddRecs in TheLoop.
  unsigned Offset;

  /// Loop for which to rewrite AddRecsFor.
  Loop *TheLoop;

  /// Is any sub-expressions not analyzable w.r.t. uniformity?
  bool CannotAnalyze = false;

  bool canAnalyze() const { return !CannotAnalyze; }

public:
  SCEVAddRecForUniformityRewriter(ScalarEvolution &SE, unsigned StepMultiplier,
                                  unsigned Offset, Loop *TheLoop)
      : SCEVRewriteVisitor(SE), StepMultiplier(StepMultiplier), Offset(Offset),
        TheLoop(TheLoop) {}

  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) {
    assert(Expr->getLoop() == TheLoop &&
           "addrec outside of TheLoop must be invariant and should have been "
           "handled earlier");
    // Build a new AddRec by multiplying the step by StepMultiplier and
    // incrementing the start by Offset * step.
    Type *Ty = Expr->getType();
    auto *Step = Expr->getStepRecurrence(SE);
    if (!SE.isLoopInvariant(Step, TheLoop)) {
      CannotAnalyze = true;
      return Expr;
    }
    auto *NewStep = SE.getMulExpr(Step, SE.getConstant(Ty, StepMultiplier));
    auto *ScaledOffset = SE.getMulExpr(Step, SE.getConstant(Ty, Offset));
    auto *NewStart = SE.getAddExpr(Expr->getStart(), ScaledOffset);
    return SE.getAddRecExpr(NewStart, NewStep, TheLoop, SCEV::FlagAnyWrap);
  }

  const SCEV *visit(const SCEV *S) {
    if (CannotAnalyze || SE.isLoopInvariant(S, TheLoop))
      return S;
    return SCEVRewriteVisitor<SCEVAddRecForUniformityRewriter>::visit(S);
  }

  const SCEV *visitUnknown(const SCEVUnknown *S) {
    if (SE.isLoopInvariant(S, TheLoop))
      return S;
    // The value could vary across iterations.
    CannotAnalyze = true;
    return S;
  }

  const SCEV *visitCouldNotCompute(const SCEVCouldNotCompute *S) {
    // Could not analyze the expression.
    CannotAnalyze = true;
    return S;
  }

  static const SCEV *rewrite(const SCEV *S, ScalarEvolution &SE,
                             unsigned StepMultiplier, unsigned Offset,
                             Loop *TheLoop) {
    /// Bail out if the expression does not contain an UDiv expression.
    /// Uniform values which are not loop invariant require operations to strip
    /// out the lowest bits. For now just look for UDivs and use it to avoid
    /// re-writing UDIV-free expressions for other lanes to limit compile time.
    if (!SCEVExprContains(S,
                          [](const SCEV *S) { return isa<SCEVUDivExpr>(S); }))
      return SE.getCouldNotCompute();

    SCEVAddRecForUniformityRewriter Rewriter(SE, StepMultiplier, Offset,
                                             TheLoop);
    const SCEV *Result = Rewriter.visit(S);

    if (Rewriter.canAnalyze())
      return Result;
    return SE.getCouldNotCompute();
  }
};

} // namespace

bool LoopVectorizationLegality::isUniform(Value *V, ElementCount VF) const {
  if (isInvariant(V))
    return true;
  if (VF.isScalable())
    return false;
  if (VF.isScalar())
    return true;

  // Since we rely on SCEV for uniformity, if the type is not SCEVable, it is
  // never considered uniform.
  auto *SE = PSE.getSE();
  if (!SE->isSCEVable(V->getType()))
    return false;
  const SCEV *S = SE->getSCEV(V);

  // Rewrite AddRecs in TheLoop to step by VF and check if the expression for
  // lane 0 matches the expressions for all other lanes.
  unsigned FixedVF = VF.getKnownMinValue();
  const SCEV *FirstLaneExpr =
      SCEVAddRecForUniformityRewriter::rewrite(S, *SE, FixedVF, 0, TheLoop);
  if (isa<SCEVCouldNotCompute>(FirstLaneExpr))
    return false;

  // Make sure the expressions for lanes FixedVF-1..1 match the expression for
  // lane 0. We check lanes in reverse order for compile-time, as frequently
  // checking the last lane is sufficient to rule out uniformity.
  return all_of(reverse(seq<unsigned>(1, FixedVF)), [&](unsigned I) {
    const SCEV *IthLaneExpr =
        SCEVAddRecForUniformityRewriter::rewrite(S, *SE, FixedVF, I, TheLoop);
    return FirstLaneExpr == IthLaneExpr;
  });
}

bool LoopVectorizationLegality::isUniformMemOp(Instruction &I,
                                               ElementCount VF) const {
  Value *Ptr = getLoadStorePointerOperand(&I);
  if (!Ptr)
    return false;
  // Note: There's nothing inherent which prevents predicated loads and
  // stores from being uniform.  The current lowering simply doesn't handle
  // it; in particular, the cost model distinguishes scatter/gather from
  // scalar w/predication, and we currently rely on the scalar path.
  return isUniform(Ptr, VF) && !blockNeedsPredication(I.getParent());
}

bool LoopVectorizationLegality::canVectorizeOuterLoop() {
  assert(!TheLoop->isInnermost() && "We are not vectorizing an outer loop.");
  // Store the result and return it at the end instead of exiting early, in case
  // allowExtraAnalysis is used to report multiple reasons for not vectorizing.
  bool Result = true;
  bool DoExtraAnalysis = ORE->allowExtraAnalysis(DEBUG_TYPE);

  for (BasicBlock *BB : TheLoop->blocks()) {
    // Check whether the BB terminator is a BranchInst. Any other terminator is
    // not supported yet.
    auto *Br = dyn_cast<BranchInst>(BB->getTerminator());
    if (!Br) {
      reportVectorizationFailure("Unsupported basic block terminator",
          "loop control flow is not understood by vectorizer",
          "CFGNotUnderstood", ORE, TheLoop);
      if (DoExtraAnalysis)
        Result = false;
      else
        return false;
    }

    // Check whether the BranchInst is a supported one. Only unconditional
    // branches, conditional branches with an outer loop invariant condition or
    // backedges are supported.
    // FIXME: We skip these checks when VPlan predication is enabled as we
    // want to allow divergent branches. This whole check will be removed
    // once VPlan predication is on by default.
    if (Br && Br->isConditional() &&
        !TheLoop->isLoopInvariant(Br->getCondition()) &&
        !LI->isLoopHeader(Br->getSuccessor(0)) &&
        !LI->isLoopHeader(Br->getSuccessor(1))) {
      reportVectorizationFailure("Unsupported conditional branch",
          "loop control flow is not understood by vectorizer",
          "CFGNotUnderstood", ORE, TheLoop);
      if (DoExtraAnalysis)
        Result = false;
      else
        return false;
    }
  }

  // Check whether inner loops are uniform. At this point, we only support
  // simple outer loops scenarios with uniform nested loops.
  if (!isUniformLoopNest(TheLoop /*loop nest*/,
                         TheLoop /*context outer loop*/)) {
    reportVectorizationFailure("Outer loop contains divergent loops",
        "loop control flow is not understood by vectorizer",
        "CFGNotUnderstood", ORE, TheLoop);
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  // Check whether we are able to set up outer loop induction.
  if (!setupOuterLoopInductions()) {
    reportVectorizationFailure("Unsupported outer loop Phi(s)",
                               "Unsupported outer loop Phi(s)",
                               "UnsupportedPhi", ORE, TheLoop);
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  return Result;
}

void LoopVectorizationLegality::addInductionPhi(
    PHINode *Phi, const InductionDescriptor &ID,
    SmallPtrSetImpl<Value *> &AllowedExit) {
  Inductions[Phi] = ID;

  // In case this induction also comes with casts that we know we can ignore
  // in the vectorized loop body, record them here. All casts could be recorded
  // here for ignoring, but suffices to record only the first (as it is the
  // only one that may bw used outside the cast sequence).
  const SmallVectorImpl<Instruction *> &Casts = ID.getCastInsts();
  if (!Casts.empty())
    InductionCastsToIgnore.insert(*Casts.begin());

  Type *PhiTy = Phi->getType();
  const DataLayout &DL = Phi->getDataLayout();

  // Get the widest type.
  if (!PhiTy->isFloatingPointTy()) {
    if (!WidestIndTy)
      WidestIndTy = convertPointerToIntegerType(DL, PhiTy);
    else
      WidestIndTy = getWiderType(DL, PhiTy, WidestIndTy);
  }

  // Int inductions are special because we only allow one IV.
  if (ID.getKind() == InductionDescriptor::IK_IntInduction &&
      ID.getConstIntStepValue() && ID.getConstIntStepValue()->isOne() &&
      isa<Constant>(ID.getStartValue()) &&
      cast<Constant>(ID.getStartValue())->isNullValue()) {

    // Use the phi node with the widest type as induction. Use the last
    // one if there are multiple (no good reason for doing this other
    // than it is expedient). We've checked that it begins at zero and
    // steps by one, so this is a canonical induction variable.
    if (!PrimaryInduction || PhiTy == WidestIndTy)
      PrimaryInduction = Phi;
  }

  // Both the PHI node itself, and the "post-increment" value feeding
  // back into the PHI node may have external users.
  // We can allow those uses, except if the SCEVs we have for them rely
  // on predicates that only hold within the loop, since allowing the exit
  // currently means re-using this SCEV outside the loop (see PR33706 for more
  // details).
  if (PSE.getPredicate().isAlwaysTrue()) {
    AllowedExit.insert(Phi);
    AllowedExit.insert(Phi->getIncomingValueForBlock(TheLoop->getLoopLatch()));
  }

  LLVM_DEBUG(dbgs() << "LV: Found an induction variable.\n");
}

bool LoopVectorizationLegality::setupOuterLoopInductions() {
  BasicBlock *Header = TheLoop->getHeader();

  // Returns true if a given Phi is a supported induction.
  auto isSupportedPhi = [&](PHINode &Phi) -> bool {
    InductionDescriptor ID;
    if (InductionDescriptor::isInductionPHI(&Phi, TheLoop, PSE, ID) &&
        ID.getKind() == InductionDescriptor::IK_IntInduction) {
      addInductionPhi(&Phi, ID, AllowedExit);
      return true;
    } else {
      // Bail out for any Phi in the outer loop header that is not a supported
      // induction.
      LLVM_DEBUG(
          dbgs()
          << "LV: Found unsupported PHI for outer loop vectorization.\n");
      return false;
    }
  };

  if (llvm::all_of(Header->phis(), isSupportedPhi))
    return true;
  else
    return false;
}

/// Checks if a function is scalarizable according to the TLI, in
/// the sense that it should be vectorized and then expanded in
/// multiple scalar calls. This is represented in the
/// TLI via mappings that do not specify a vector name, as in the
/// following example:
///
///    const VecDesc VecIntrinsics[] = {
///      {"llvm.phx.abs.i32", "", 4}
///    };
static bool isTLIScalarize(const TargetLibraryInfo &TLI, const CallInst &CI) {
  const StringRef ScalarName = CI.getCalledFunction()->getName();
  bool Scalarize = TLI.isFunctionVectorizable(ScalarName);
  // Check that all known VFs are not associated to a vector
  // function, i.e. the vector name is emty.
  if (Scalarize) {
    ElementCount WidestFixedVF, WidestScalableVF;
    TLI.getWidestVF(ScalarName, WidestFixedVF, WidestScalableVF);
    for (ElementCount VF = ElementCount::getFixed(2);
         ElementCount::isKnownLE(VF, WidestFixedVF); VF *= 2)
      Scalarize &= !TLI.isFunctionVectorizable(ScalarName, VF);
    for (ElementCount VF = ElementCount::getScalable(1);
         ElementCount::isKnownLE(VF, WidestScalableVF); VF *= 2)
      Scalarize &= !TLI.isFunctionVectorizable(ScalarName, VF);
    assert((WidestScalableVF.isZero() || !Scalarize) &&
           "Caller may decide to scalarize a variant using a scalable VF");
  }
  return Scalarize;
}

bool LoopVectorizationLegality::canVectorizeInstrs() {
  BasicBlock *Header = TheLoop->getHeader();

  // For each block in the loop.
  for (BasicBlock *BB : TheLoop->blocks()) {
    // Scan the instructions in the block and look for hazards.
    for (Instruction &I : *BB) {
      if (auto *Phi = dyn_cast<PHINode>(&I)) {
        Type *PhiTy = Phi->getType();
        // Check that this PHI type is allowed.
        if (!PhiTy->isIntegerTy() && !PhiTy->isFloatingPointTy() &&
            !PhiTy->isPointerTy()) {
          reportVectorizationFailure("Found a non-int non-pointer PHI",
                                     "loop control flow is not understood by vectorizer",
                                     "CFGNotUnderstood", ORE, TheLoop);
          return false;
        }

        // If this PHINode is not in the header block, then we know that we
        // can convert it to select during if-conversion. No need to check if
        // the PHIs in this block are induction or reduction variables.
        if (BB != Header) {
          // Non-header phi nodes that have outside uses can be vectorized. Add
          // them to the list of allowed exits.
          // Unsafe cyclic dependencies with header phis are identified during
          // legalization for reduction, induction and fixed order
          // recurrences.
          AllowedExit.insert(&I);
          continue;
        }

        // We only allow if-converted PHIs with exactly two incoming values.
        if (Phi->getNumIncomingValues() != 2) {
          reportVectorizationFailure("Found an invalid PHI",
              "loop control flow is not understood by vectorizer",
              "CFGNotUnderstood", ORE, TheLoop, Phi);
          return false;
        }

        RecurrenceDescriptor RedDes;
        if (RecurrenceDescriptor::isReductionPHI(Phi, TheLoop, RedDes, DB, AC,
                                                 DT, PSE.getSE())) {
          Requirements->addExactFPMathInst(RedDes.getExactFPMathInst());
          AllowedExit.insert(RedDes.getLoopExitInstr());
          Reductions[Phi] = RedDes;
          continue;
        }

        // We prevent matching non-constant strided pointer IVS to preserve
        // historical vectorizer behavior after a generalization of the
        // IVDescriptor code.  The intent is to remove this check, but we
        // have to fix issues around code quality for such loops first.
        auto isDisallowedStridedPointerInduction =
          [](const InductionDescriptor &ID) {
          if (AllowStridedPointerIVs)
            return false;
          return ID.getKind() == InductionDescriptor::IK_PtrInduction &&
            ID.getConstIntStepValue() == nullptr;
        };

        // TODO: Instead of recording the AllowedExit, it would be good to
        // record the complementary set: NotAllowedExit. These include (but may
        // not be limited to):
        // 1. Reduction phis as they represent the one-before-last value, which
        // is not available when vectorized
        // 2. Induction phis and increment when SCEV predicates cannot be used
        // outside the loop - see addInductionPhi
        // 3. Non-Phis with outside uses when SCEV predicates cannot be used
        // outside the loop - see call to hasOutsideLoopUser in the non-phi
        // handling below
        // 4. FixedOrderRecurrence phis that can possibly be handled by
        // extraction.
        // By recording these, we can then reason about ways to vectorize each
        // of these NotAllowedExit.
        InductionDescriptor ID;
        if (InductionDescriptor::isInductionPHI(Phi, TheLoop, PSE, ID) &&
            !isDisallowedStridedPointerInduction(ID)) {
          addInductionPhi(Phi, ID, AllowedExit);
          Requirements->addExactFPMathInst(ID.getExactFPMathInst());
          continue;
        }

        if (RecurrenceDescriptor::isFixedOrderRecurrence(Phi, TheLoop, DT)) {
          AllowedExit.insert(Phi);
          FixedOrderRecurrences.insert(Phi);
          continue;
        }

        // As a last resort, coerce the PHI to a AddRec expression
        // and re-try classifying it a an induction PHI.
        if (InductionDescriptor::isInductionPHI(Phi, TheLoop, PSE, ID, true) &&
            !isDisallowedStridedPointerInduction(ID)) {
          addInductionPhi(Phi, ID, AllowedExit);
          continue;
        }

        reportVectorizationFailure("Found an unidentified PHI",
            "value that could not be identified as "
            "reduction is used outside the loop",
            "NonReductionValueUsedOutsideLoop", ORE, TheLoop, Phi);
        return false;
      } // end of PHI handling

      // We handle calls that:
      //   * Are debug info intrinsics.
      //   * Have a mapping to an IR intrinsic.
      //   * Have a vector version available.
      auto *CI = dyn_cast<CallInst>(&I);

      if (CI && !getVectorIntrinsicIDForCall(CI, TLI) &&
          !isa<DbgInfoIntrinsic>(CI) &&
          !(CI->getCalledFunction() && TLI &&
            (!VFDatabase::getMappings(*CI).empty() ||
             isTLIScalarize(*TLI, *CI)))) {
        // If the call is a recognized math libary call, it is likely that
        // we can vectorize it given loosened floating-point constraints.
        LibFunc Func;
        bool IsMathLibCall =
            TLI && CI->getCalledFunction() &&
            CI->getType()->isFloatingPointTy() &&
            TLI->getLibFunc(CI->getCalledFunction()->getName(), Func) &&
            TLI->hasOptimizedCodeGen(Func);

        if (IsMathLibCall) {
          // TODO: Ideally, we should not use clang-specific language here,
          // but it's hard to provide meaningful yet generic advice.
          // Also, should this be guarded by allowExtraAnalysis() and/or be part
          // of the returned info from isFunctionVectorizable()?
          reportVectorizationFailure(
              "Found a non-intrinsic callsite",
              "library call cannot be vectorized. "
              "Try compiling with -fno-math-errno, -ffast-math, "
              "or similar flags",
              "CantVectorizeLibcall", ORE, TheLoop, CI);
        } else {
          reportVectorizationFailure("Found a non-intrinsic callsite",
                                     "call instruction cannot be vectorized",
                                     "CantVectorizeLibcall", ORE, TheLoop, CI);
        }
        return false;
      }

      // Some intrinsics have scalar arguments and should be same in order for
      // them to be vectorized (i.e. loop invariant).
      if (CI) {
        auto *SE = PSE.getSE();
        Intrinsic::ID IntrinID = getVectorIntrinsicIDForCall(CI, TLI);
        for (unsigned i = 0, e = CI->arg_size(); i != e; ++i)
          if (isVectorIntrinsicWithScalarOpAtArg(IntrinID, i)) {
            if (!SE->isLoopInvariant(PSE.getSCEV(CI->getOperand(i)), TheLoop)) {
              reportVectorizationFailure("Found unvectorizable intrinsic",
                  "intrinsic instruction cannot be vectorized",
                  "CantVectorizeIntrinsic", ORE, TheLoop, CI);
              return false;
            }
          }
      }

      // If we found a vectorized variant of a function, note that so LV can
      // make better decisions about maximum VF.
      if (CI && !VFDatabase::getMappings(*CI).empty())
        VecCallVariantsFound = true;

      // Check that the instruction return type is vectorizable.
      // Also, we can't vectorize extractelement instructions.
      if ((!VectorType::isValidElementType(I.getType()) &&
           !I.getType()->isVoidTy()) ||
          isa<ExtractElementInst>(I)) {
        reportVectorizationFailure("Found unvectorizable type",
            "instruction return type cannot be vectorized",
            "CantVectorizeInstructionReturnType", ORE, TheLoop, &I);
        return false;
      }

      // Check that the stored type is vectorizable.
      if (auto *ST = dyn_cast<StoreInst>(&I)) {
        Type *T = ST->getValueOperand()->getType();
        if (!VectorType::isValidElementType(T)) {
          reportVectorizationFailure("Store instruction cannot be vectorized",
                                     "store instruction cannot be vectorized",
                                     "CantVectorizeStore", ORE, TheLoop, ST);
          return false;
        }

        // For nontemporal stores, check that a nontemporal vector version is
        // supported on the target.
        if (ST->getMetadata(LLVMContext::MD_nontemporal)) {
          // Arbitrarily try a vector of 2 elements.
          auto *VecTy = FixedVectorType::get(T, /*NumElts=*/2);
          assert(VecTy && "did not find vectorized version of stored type");
          if (!TTI->isLegalNTStore(VecTy, ST->getAlign())) {
            reportVectorizationFailure(
                "nontemporal store instruction cannot be vectorized",
                "nontemporal store instruction cannot be vectorized",
                "CantVectorizeNontemporalStore", ORE, TheLoop, ST);
            return false;
          }
        }

      } else if (auto *LD = dyn_cast<LoadInst>(&I)) {
        if (LD->getMetadata(LLVMContext::MD_nontemporal)) {
          // For nontemporal loads, check that a nontemporal vector version is
          // supported on the target (arbitrarily try a vector of 2 elements).
          auto *VecTy = FixedVectorType::get(I.getType(), /*NumElts=*/2);
          assert(VecTy && "did not find vectorized version of load type");
          if (!TTI->isLegalNTLoad(VecTy, LD->getAlign())) {
            reportVectorizationFailure(
                "nontemporal load instruction cannot be vectorized",
                "nontemporal load instruction cannot be vectorized",
                "CantVectorizeNontemporalLoad", ORE, TheLoop, LD);
            return false;
          }
        }

        // FP instructions can allow unsafe algebra, thus vectorizable by
        // non-IEEE-754 compliant SIMD units.
        // This applies to floating-point math operations and calls, not memory
        // operations, shuffles, or casts, as they don't change precision or
        // semantics.
      } else if (I.getType()->isFloatingPointTy() && (CI || I.isBinaryOp()) &&
                 !I.isFast()) {
        LLVM_DEBUG(dbgs() << "LV: Found FP op with unsafe algebra.\n");
        Hints->setPotentiallyUnsafe();
      }

      // Reduction instructions are allowed to have exit users.
      // All other instructions must not have external users.
      if (hasOutsideLoopUser(TheLoop, &I, AllowedExit)) {
        // We can safely vectorize loops where instructions within the loop are
        // used outside the loop only if the SCEV predicates within the loop is
        // same as outside the loop. Allowing the exit means reusing the SCEV
        // outside the loop.
        if (PSE.getPredicate().isAlwaysTrue()) {
          AllowedExit.insert(&I);
          continue;
        }
        reportVectorizationFailure("Value cannot be used outside the loop",
                                   "value cannot be used outside the loop",
                                   "ValueUsedOutsideLoop", ORE, TheLoop, &I);
        return false;
      }
    } // next instr.
  }

  if (!PrimaryInduction) {
    if (Inductions.empty()) {
      reportVectorizationFailure("Did not find one integer induction var",
          "loop induction variable could not be identified",
          "NoInductionVariable", ORE, TheLoop);
      return false;
    } else if (!WidestIndTy) {
      reportVectorizationFailure("Did not find one integer induction var",
          "integer loop induction variable could not be identified",
          "NoIntegerInductionVariable", ORE, TheLoop);
      return false;
    } else {
      LLVM_DEBUG(dbgs() << "LV: Did not find one integer induction var.\n");
    }
  }

  // Now we know the widest induction type, check if our found induction
  // is the same size. If it's not, unset it here and InnerLoopVectorizer
  // will create another.
  if (PrimaryInduction && WidestIndTy != PrimaryInduction->getType())
    PrimaryInduction = nullptr;

  return true;
}

bool LoopVectorizationLegality::canVectorizeMemory() {
  LAI = &LAIs.getInfo(*TheLoop);
  const OptimizationRemarkAnalysis *LAR = LAI->getReport();
  if (LAR) {
    ORE->emit([&]() {
      return OptimizationRemarkAnalysis(Hints->vectorizeAnalysisPassName(),
                                        "loop not vectorized: ", *LAR);
    });
  }

  if (!LAI->canVectorizeMemory())
    return false;

  if (LAI->hasLoadStoreDependenceInvolvingLoopInvariantAddress()) {
    reportVectorizationFailure("We don't allow storing to uniform addresses",
                               "write to a loop invariant address could not "
                               "be vectorized",
                               "CantVectorizeStoreToLoopInvariantAddress", ORE,
                               TheLoop);
    return false;
  }

  // We can vectorize stores to invariant address when final reduction value is
  // guaranteed to be stored at the end of the loop. Also, if decision to
  // vectorize loop is made, runtime checks are added so as to make sure that
  // invariant address won't alias with any other objects.
  if (!LAI->getStoresToInvariantAddresses().empty()) {
    // For each invariant address, check if last stored value is unconditional
    // and the address is not calculated inside the loop.
    for (StoreInst *SI : LAI->getStoresToInvariantAddresses()) {
      if (!isInvariantStoreOfReduction(SI))
        continue;

      if (blockNeedsPredication(SI->getParent())) {
        reportVectorizationFailure(
            "We don't allow storing to uniform addresses",
            "write of conditional recurring variant value to a loop "
            "invariant address could not be vectorized",
            "CantVectorizeStoreToLoopInvariantAddress", ORE, TheLoop);
        return false;
      }

      // Invariant address should be defined outside of loop. LICM pass usually
      // makes sure it happens, but in rare cases it does not, we do not want
      // to overcomplicate vectorization to support this case.
      if (Instruction *Ptr = dyn_cast<Instruction>(SI->getPointerOperand())) {
        if (TheLoop->contains(Ptr)) {
          reportVectorizationFailure(
              "Invariant address is calculated inside the loop",
              "write to a loop invariant address could not "
              "be vectorized",
              "CantVectorizeStoreToLoopInvariantAddress", ORE, TheLoop);
          return false;
        }
      }
    }

    if (LAI->hasStoreStoreDependenceInvolvingLoopInvariantAddress()) {
      // For each invariant address, check its last stored value is the result
      // of one of our reductions.
      //
      // We do not check if dependence with loads exists because that is already
      // checked via hasLoadStoreDependenceInvolvingLoopInvariantAddress.
      ScalarEvolution *SE = PSE.getSE();
      SmallVector<StoreInst *, 4> UnhandledStores;
      for (StoreInst *SI : LAI->getStoresToInvariantAddresses()) {
        if (isInvariantStoreOfReduction(SI)) {
          // Earlier stores to this address are effectively deadcode.
          // With opaque pointers it is possible for one pointer to be used with
          // different sizes of stored values:
          //    store i32 0, ptr %x
          //    store i8 0, ptr %x
          // The latest store doesn't complitely overwrite the first one in the
          // example. That is why we have to make sure that types of stored
          // values are same.
          // TODO: Check that bitwidth of unhandled store is smaller then the
          // one that overwrites it and add a test.
          erase_if(UnhandledStores, [SE, SI](StoreInst *I) {
            return storeToSameAddress(SE, SI, I) &&
                   I->getValueOperand()->getType() ==
                       SI->getValueOperand()->getType();
          });
          continue;
        }
        UnhandledStores.push_back(SI);
      }

      bool IsOK = UnhandledStores.empty();
      // TODO: we should also validate against InvariantMemSets.
      if (!IsOK) {
        reportVectorizationFailure(
            "We don't allow storing to uniform addresses",
            "write to a loop invariant address could not "
            "be vectorized",
            "CantVectorizeStoreToLoopInvariantAddress", ORE, TheLoop);
        return false;
      }
    }
  }

  PSE.addPredicate(LAI->getPSE().getPredicate());
  return true;
}

bool LoopVectorizationLegality::canVectorizeFPMath(
    bool EnableStrictReductions) {

  // First check if there is any ExactFP math or if we allow reassociations
  if (!Requirements->getExactFPInst() || Hints->allowReordering())
    return true;

  // If the above is false, we have ExactFPMath & do not allow reordering.
  // If the EnableStrictReductions flag is set, first check if we have any
  // Exact FP induction vars, which we cannot vectorize.
  if (!EnableStrictReductions ||
      any_of(getInductionVars(), [&](auto &Induction) -> bool {
        InductionDescriptor IndDesc = Induction.second;
        return IndDesc.getExactFPMathInst();
      }))
    return false;

  // We can now only vectorize if all reductions with Exact FP math also
  // have the isOrdered flag set, which indicates that we can move the
  // reduction operations in-loop.
  return (all_of(getReductionVars(), [&](auto &Reduction) -> bool {
    const RecurrenceDescriptor &RdxDesc = Reduction.second;
    return !RdxDesc.hasExactFPMath() || RdxDesc.isOrdered();
  }));
}

bool LoopVectorizationLegality::isInvariantStoreOfReduction(StoreInst *SI) {
  return any_of(getReductionVars(), [&](auto &Reduction) -> bool {
    const RecurrenceDescriptor &RdxDesc = Reduction.second;
    return RdxDesc.IntermediateStore == SI;
  });
}

bool LoopVectorizationLegality::isInvariantAddressOfReduction(Value *V) {
  return any_of(getReductionVars(), [&](auto &Reduction) -> bool {
    const RecurrenceDescriptor &RdxDesc = Reduction.second;
    if (!RdxDesc.IntermediateStore)
      return false;

    ScalarEvolution *SE = PSE.getSE();
    Value *InvariantAddress = RdxDesc.IntermediateStore->getPointerOperand();
    return V == InvariantAddress ||
           SE->getSCEV(V) == SE->getSCEV(InvariantAddress);
  });
}

bool LoopVectorizationLegality::isInductionPhi(const Value *V) const {
  Value *In0 = const_cast<Value *>(V);
  PHINode *PN = dyn_cast_or_null<PHINode>(In0);
  if (!PN)
    return false;

  return Inductions.count(PN);
}

const InductionDescriptor *
LoopVectorizationLegality::getIntOrFpInductionDescriptor(PHINode *Phi) const {
  if (!isInductionPhi(Phi))
    return nullptr;
  auto &ID = getInductionVars().find(Phi)->second;
  if (ID.getKind() == InductionDescriptor::IK_IntInduction ||
      ID.getKind() == InductionDescriptor::IK_FpInduction)
    return &ID;
  return nullptr;
}

const InductionDescriptor *
LoopVectorizationLegality::getPointerInductionDescriptor(PHINode *Phi) const {
  if (!isInductionPhi(Phi))
    return nullptr;
  auto &ID = getInductionVars().find(Phi)->second;
  if (ID.getKind() == InductionDescriptor::IK_PtrInduction)
    return &ID;
  return nullptr;
}

bool LoopVectorizationLegality::isCastedInductionVariable(
    const Value *V) const {
  auto *Inst = dyn_cast<Instruction>(V);
  return (Inst && InductionCastsToIgnore.count(Inst));
}

bool LoopVectorizationLegality::isInductionVariable(const Value *V) const {
  return isInductionPhi(V) || isCastedInductionVariable(V);
}

bool LoopVectorizationLegality::isFixedOrderRecurrence(
    const PHINode *Phi) const {
  return FixedOrderRecurrences.count(Phi);
}

bool LoopVectorizationLegality::blockNeedsPredication(BasicBlock *BB) const {
  return LoopAccessInfo::blockNeedsPredication(BB, TheLoop, DT);
}

bool LoopVectorizationLegality::blockCanBePredicated(
    BasicBlock *BB, SmallPtrSetImpl<Value *> &SafePtrs,
    SmallPtrSetImpl<const Instruction *> &MaskedOp) const {
  for (Instruction &I : *BB) {
    // We can predicate blocks with calls to assume, as long as we drop them in
    // case we flatten the CFG via predication.
    if (match(&I, m_Intrinsic<Intrinsic::assume>())) {
      MaskedOp.insert(&I);
      continue;
    }

    // Do not let llvm.experimental.noalias.scope.decl block the vectorization.
    // TODO: there might be cases that it should block the vectorization. Let's
    // ignore those for now.
    if (isa<NoAliasScopeDeclInst>(&I))
      continue;

    // We can allow masked calls if there's at least one vector variant, even
    // if we end up scalarizing due to the cost model calculations.
    // TODO: Allow other calls if they have appropriate attributes... readonly
    // and argmemonly?
    if (CallInst *CI = dyn_cast<CallInst>(&I))
      if (VFDatabase::hasMaskedVariant(*CI)) {
        MaskedOp.insert(CI);
        continue;
      }

    // Loads are handled via masking (or speculated if safe to do so.)
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (!SafePtrs.count(LI->getPointerOperand()))
        MaskedOp.insert(LI);
      continue;
    }

    // Predicated store requires some form of masking:
    // 1) masked store HW instruction,
    // 2) emulation via load-blend-store (only if safe and legal to do so,
    //    be aware on the race conditions), or
    // 3) element-by-element predicate check and scalar store.
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      MaskedOp.insert(SI);
      continue;
    }

    if (I.mayReadFromMemory() || I.mayWriteToMemory() || I.mayThrow())
      return false;
  }

  return true;
}

bool LoopVectorizationLegality::canVectorizeWithIfConvert() {
  if (!EnableIfConversion) {
    reportVectorizationFailure("If-conversion is disabled",
                               "if-conversion is disabled",
                               "IfConversionDisabled",
                               ORE, TheLoop);
    return false;
  }

  assert(TheLoop->getNumBlocks() > 1 && "Single block loops are vectorizable");

  // A list of pointers which are known to be dereferenceable within scope of
  // the loop body for each iteration of the loop which executes.  That is,
  // the memory pointed to can be dereferenced (with the access size implied by
  // the value's type) unconditionally within the loop header without
  // introducing a new fault.
  SmallPtrSet<Value *, 8> SafePointers;

  // Collect safe addresses.
  for (BasicBlock *BB : TheLoop->blocks()) {
    if (!blockNeedsPredication(BB)) {
      for (Instruction &I : *BB)
        if (auto *Ptr = getLoadStorePointerOperand(&I))
          SafePointers.insert(Ptr);
      continue;
    }

    // For a block which requires predication, a address may be safe to access
    // in the loop w/o predication if we can prove dereferenceability facts
    // sufficient to ensure it'll never fault within the loop. For the moment,
    // we restrict this to loads; stores are more complicated due to
    // concurrency restrictions.
    ScalarEvolution &SE = *PSE.getSE();
    for (Instruction &I : *BB) {
      LoadInst *LI = dyn_cast<LoadInst>(&I);
      if (LI && !LI->getType()->isVectorTy() && !mustSuppressSpeculation(*LI) &&
          isDereferenceableAndAlignedInLoop(LI, TheLoop, SE, *DT, AC))
        SafePointers.insert(LI->getPointerOperand());
    }
  }

  // Collect the blocks that need predication.
  for (BasicBlock *BB : TheLoop->blocks()) {
    // We don't support switch statements inside loops.
    if (!isa<BranchInst>(BB->getTerminator())) {
      reportVectorizationFailure("Loop contains a switch statement",
                                 "loop contains a switch statement",
                                 "LoopContainsSwitch", ORE, TheLoop,
                                 BB->getTerminator());
      return false;
    }

    // We must be able to predicate all blocks that need to be predicated.
    if (blockNeedsPredication(BB) &&
        !blockCanBePredicated(BB, SafePointers, MaskedOp)) {
      reportVectorizationFailure(
          "Control flow cannot be substituted for a select",
          "control flow cannot be substituted for a select", "NoCFGForSelect",
          ORE, TheLoop, BB->getTerminator());
      return false;
    }
  }

  // We can if-convert this loop.
  return true;
}

// Helper function to canVectorizeLoopNestCFG.
bool LoopVectorizationLegality::canVectorizeLoopCFG(Loop *Lp,
                                                    bool UseVPlanNativePath) {
  assert((UseVPlanNativePath || Lp->isInnermost()) &&
         "VPlan-native path is not enabled.");

  // TODO: ORE should be improved to show more accurate information when an
  // outer loop can't be vectorized because a nested loop is not understood or
  // legal. Something like: "outer_loop_location: loop not vectorized:
  // (inner_loop_location) loop control flow is not understood by vectorizer".

  // Store the result and return it at the end instead of exiting early, in case
  // allowExtraAnalysis is used to report multiple reasons for not vectorizing.
  bool Result = true;
  bool DoExtraAnalysis = ORE->allowExtraAnalysis(DEBUG_TYPE);

  // We must have a loop in canonical form. Loops with indirectbr in them cannot
  // be canonicalized.
  if (!Lp->getLoopPreheader()) {
    reportVectorizationFailure("Loop doesn't have a legal pre-header",
        "loop control flow is not understood by vectorizer",
        "CFGNotUnderstood", ORE, TheLoop);
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  // We must have a single backedge.
  if (Lp->getNumBackEdges() != 1) {
    reportVectorizationFailure("The loop must have a single backedge",
        "loop control flow is not understood by vectorizer",
        "CFGNotUnderstood", ORE, TheLoop);
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  return Result;
}

bool LoopVectorizationLegality::canVectorizeLoopNestCFG(
    Loop *Lp, bool UseVPlanNativePath) {
  // Store the result and return it at the end instead of exiting early, in case
  // allowExtraAnalysis is used to report multiple reasons for not vectorizing.
  bool Result = true;
  bool DoExtraAnalysis = ORE->allowExtraAnalysis(DEBUG_TYPE);
  if (!canVectorizeLoopCFG(Lp, UseVPlanNativePath)) {
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  // Recursively check whether the loop control flow of nested loops is
  // understood.
  for (Loop *SubLp : *Lp)
    if (!canVectorizeLoopNestCFG(SubLp, UseVPlanNativePath)) {
      if (DoExtraAnalysis)
        Result = false;
      else
        return false;
    }

  return Result;
}

bool LoopVectorizationLegality::canVectorize(bool UseVPlanNativePath) {
  // Store the result and return it at the end instead of exiting early, in case
  // allowExtraAnalysis is used to report multiple reasons for not vectorizing.
  bool Result = true;

  bool DoExtraAnalysis = ORE->allowExtraAnalysis(DEBUG_TYPE);
  // Check whether the loop-related control flow in the loop nest is expected by
  // vectorizer.
  if (!canVectorizeLoopNestCFG(TheLoop, UseVPlanNativePath)) {
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  // We need to have a loop header.
  LLVM_DEBUG(dbgs() << "LV: Found a loop: " << TheLoop->getHeader()->getName()
                    << '\n');

  // Specific checks for outer loops. We skip the remaining legal checks at this
  // point because they don't support outer loops.
  if (!TheLoop->isInnermost()) {
    assert(UseVPlanNativePath && "VPlan-native path is not enabled.");

    if (!canVectorizeOuterLoop()) {
      reportVectorizationFailure("Unsupported outer loop",
                                 "unsupported outer loop",
                                 "UnsupportedOuterLoop",
                                 ORE, TheLoop);
      // TODO: Implement DoExtraAnalysis when subsequent legal checks support
      // outer loops.
      return false;
    }

    LLVM_DEBUG(dbgs() << "LV: We can vectorize this outer loop!\n");
    return Result;
  }

  assert(TheLoop->isInnermost() && "Inner loop expected.");
  // Check if we can if-convert non-single-bb loops.
  unsigned NumBlocks = TheLoop->getNumBlocks();
  if (NumBlocks != 1 && !canVectorizeWithIfConvert()) {
    LLVM_DEBUG(dbgs() << "LV: Can't if-convert the loop.\n");
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  // Check if we can vectorize the instructions and CFG in this loop.
  if (!canVectorizeInstrs()) {
    LLVM_DEBUG(dbgs() << "LV: Can't vectorize the instructions or CFG\n");
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  // Go over each instruction and look at memory deps.
  if (!canVectorizeMemory()) {
    LLVM_DEBUG(dbgs() << "LV: Can't vectorize due to memory conflicts\n");
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  if (isa<SCEVCouldNotCompute>(PSE.getBackedgeTakenCount())) {
    reportVectorizationFailure("could not determine number of loop iterations",
                               "could not determine number of loop iterations",
                               "CantComputeNumberOfIterations", ORE, TheLoop);
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  LLVM_DEBUG(dbgs() << "LV: We can vectorize this loop"
                    << (LAI->getRuntimePointerChecking()->Need
                            ? " (with a runtime bound check)"
                            : "")
                    << "!\n");

  unsigned SCEVThreshold = VectorizeSCEVCheckThreshold;
  if (Hints->getForce() == LoopVectorizeHints::FK_Enabled)
    SCEVThreshold = PragmaVectorizeSCEVCheckThreshold;

  if (PSE.getPredicate().getComplexity() > SCEVThreshold) {
    reportVectorizationFailure("Too many SCEV checks needed",
        "Too many SCEV assumptions need to be made and checked at runtime",
        "TooManySCEVRunTimeChecks", ORE, TheLoop);
    if (DoExtraAnalysis)
      Result = false;
    else
      return false;
  }

  // Okay! We've done all the tests. If any have failed, return false. Otherwise
  // we can vectorize, and at this point we don't have any other mem analysis
  // which may limit our maximum vectorization factor, so just return true with
  // no restrictions.
  return Result;
}

bool LoopVectorizationLegality::canFoldTailByMasking() const {

  LLVM_DEBUG(dbgs() << "LV: checking if tail can be folded by masking.\n");

  SmallPtrSet<const Value *, 8> ReductionLiveOuts;

  for (const auto &Reduction : getReductionVars())
    ReductionLiveOuts.insert(Reduction.second.getLoopExitInstr());

  // TODO: handle non-reduction outside users when tail is folded by masking.
  for (auto *AE : AllowedExit) {
    // Check that all users of allowed exit values are inside the loop or
    // are the live-out of a reduction.
    if (ReductionLiveOuts.count(AE))
      continue;
    for (User *U : AE->users()) {
      Instruction *UI = cast<Instruction>(U);
      if (TheLoop->contains(UI))
        continue;
      LLVM_DEBUG(
          dbgs()
          << "LV: Cannot fold tail by masking, loop has an outside user for "
          << *UI << "\n");
      return false;
    }
  }

  for (const auto &Entry : getInductionVars()) {
    PHINode *OrigPhi = Entry.first;
    for (User *U : OrigPhi->users()) {
      auto *UI = cast<Instruction>(U);
      if (!TheLoop->contains(UI)) {
        LLVM_DEBUG(dbgs() << "LV: Cannot fold tail by masking, loop IV has an "
                             "outside user for "
                          << *UI << "\n");
        return false;
      }
    }
  }

  // The list of pointers that we can safely read and write to remains empty.
  SmallPtrSet<Value *, 8> SafePointers;

  // Check all blocks for predication, including those that ordinarily do not
  // need predication such as the header block.
  SmallPtrSet<const Instruction *, 8> TmpMaskedOp;
  for (BasicBlock *BB : TheLoop->blocks()) {
    if (!blockCanBePredicated(BB, SafePointers, TmpMaskedOp)) {
      LLVM_DEBUG(dbgs() << "LV: Cannot fold tail by masking.\n");
      return false;
    }
  }

  LLVM_DEBUG(dbgs() << "LV: can fold tail by masking.\n");

  return true;
}

void LoopVectorizationLegality::prepareToFoldTailByMasking() {
  // The list of pointers that we can safely read and write to remains empty.
  SmallPtrSet<Value *, 8> SafePointers;

  // Mark all blocks for predication, including those that ordinarily do not
  // need predication such as the header block.
  for (BasicBlock *BB : TheLoop->blocks()) {
    [[maybe_unused]] bool R = blockCanBePredicated(BB, SafePointers, MaskedOp);
    assert(R && "Must be able to predicate block when tail-folding.");
  }
}

} // namespace llvm
