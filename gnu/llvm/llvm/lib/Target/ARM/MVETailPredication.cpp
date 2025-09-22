//===- MVETailPredication.cpp - MVE Tail Predication ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Armv8.1m introduced MVE, M-Profile Vector Extension, and low-overhead
/// branches to help accelerate DSP applications. These two extensions,
/// combined with a new form of predication called tail-predication, can be used
/// to provide implicit vector predication within a low-overhead loop.
/// This is implicit because the predicate of active/inactive lanes is
/// calculated by hardware, and thus does not need to be explicitly passed
/// to vector instructions. The instructions responsible for this are the
/// DLSTP and WLSTP instructions, which setup a tail-predicated loop and the
/// the total number of data elements processed by the loop. The loop-end
/// LETP instruction is responsible for decrementing and setting the remaining
/// elements to be processed and generating the mask of active lanes.
///
/// The HardwareLoops pass inserts intrinsics identifying loops that the
/// backend will attempt to convert into a low-overhead loop. The vectorizer is
/// responsible for generating a vectorized loop in which the lanes are
/// predicated upon an get.active.lane.mask intrinsic. This pass looks at these
/// get.active.lane.mask intrinsic and attempts to convert them to VCTP
/// instructions. This will be picked up by the ARM Low-overhead loop pass later
/// in the backend, which performs the final transformation to a DLSTP or WLSTP
/// tail-predicated loop.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMSubtarget.h"
#include "ARMTargetTransformInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;

#define DEBUG_TYPE "mve-tail-predication"
#define DESC "Transform predicated vector loops to use MVE tail predication"

cl::opt<TailPredication::Mode> EnableTailPredication(
   "tail-predication", cl::desc("MVE tail-predication pass options"),
   cl::init(TailPredication::Enabled),
   cl::values(clEnumValN(TailPredication::Disabled, "disabled",
                         "Don't tail-predicate loops"),
              clEnumValN(TailPredication::EnabledNoReductions,
                         "enabled-no-reductions",
                         "Enable tail-predication, but not for reduction loops"),
              clEnumValN(TailPredication::Enabled,
                         "enabled",
                         "Enable tail-predication, including reduction loops"),
              clEnumValN(TailPredication::ForceEnabledNoReductions,
                         "force-enabled-no-reductions",
                         "Enable tail-predication, but not for reduction loops, "
                         "and force this which might be unsafe"),
              clEnumValN(TailPredication::ForceEnabled,
                         "force-enabled",
                         "Enable tail-predication, including reduction loops, "
                         "and force this which might be unsafe")));


namespace {

class MVETailPredication : public LoopPass {
  SmallVector<IntrinsicInst*, 4> MaskedInsts;
  Loop *L = nullptr;
  ScalarEvolution *SE = nullptr;
  TargetTransformInfo *TTI = nullptr;
  const ARMSubtarget *ST = nullptr;

public:
  static char ID;

  MVETailPredication() : LoopPass(ID) { }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.setPreservesCFG();
  }

  bool runOnLoop(Loop *L, LPPassManager&) override;

private:
  /// Perform the relevant checks on the loop and convert active lane masks if
  /// possible.
  bool TryConvertActiveLaneMask(Value *TripCount);

  /// Perform several checks on the arguments of @llvm.get.active.lane.mask
  /// intrinsic. E.g., check that the loop induction variable and the element
  /// count are of the form we expect, and also perform overflow checks for
  /// the new expressions that are created.
  const SCEV *IsSafeActiveMask(IntrinsicInst *ActiveLaneMask, Value *TripCount);

  /// Insert the intrinsic to represent the effect of tail predication.
  void InsertVCTPIntrinsic(IntrinsicInst *ActiveLaneMask, Value *Start);
};

} // end namespace

bool MVETailPredication::runOnLoop(Loop *L, LPPassManager&) {
  if (skipLoop(L) || !EnableTailPredication)
    return false;

  MaskedInsts.clear();
  Function &F = *L->getHeader()->getParent();
  auto &TPC = getAnalysis<TargetPassConfig>();
  auto &TM = TPC.getTM<TargetMachine>();
  ST = &TM.getSubtarget<ARMSubtarget>(F);
  TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  this->L = L;

  // The MVE and LOB extensions are combined to enable tail-predication, but
  // there's nothing preventing us from generating VCTP instructions for v8.1m.
  if (!ST->hasMVEIntegerOps() || !ST->hasV8_1MMainlineOps()) {
    LLVM_DEBUG(dbgs() << "ARM TP: Not a v8.1m.main+mve target.\n");
    return false;
  }

  BasicBlock *Preheader = L->getLoopPreheader();
  if (!Preheader)
    return false;

  auto FindLoopIterations = [](BasicBlock *BB) -> IntrinsicInst* {
    for (auto &I : *BB) {
      auto *Call = dyn_cast<IntrinsicInst>(&I);
      if (!Call)
        continue;

      Intrinsic::ID ID = Call->getIntrinsicID();
      if (ID == Intrinsic::start_loop_iterations ||
          ID == Intrinsic::test_start_loop_iterations)
        return cast<IntrinsicInst>(&I);
    }
    return nullptr;
  };

  // Look for the hardware loop intrinsic that sets the iteration count.
  IntrinsicInst *Setup = FindLoopIterations(Preheader);

  // The test.set iteration could live in the pre-preheader.
  if (!Setup) {
    if (!Preheader->getSinglePredecessor())
      return false;
    Setup = FindLoopIterations(Preheader->getSinglePredecessor());
    if (!Setup)
      return false;
  }

  LLVM_DEBUG(dbgs() << "ARM TP: Running on Loop: " << *L << *Setup << "\n");

  bool Changed = TryConvertActiveLaneMask(Setup->getArgOperand(0));

  return Changed;
}

// The active lane intrinsic has this form:
//
//    @llvm.get.active.lane.mask(IV, TC)
//
// Here we perform checks that this intrinsic behaves as expected,
// which means:
//
// 1) Check that the TripCount (TC) belongs to this loop (originally).
// 2) The element count (TC) needs to be sufficiently large that the decrement
//    of element counter doesn't overflow, which means that we need to prove:
//        ceil(ElementCount / VectorWidth) >= TripCount
//    by rounding up ElementCount up:
//        ((ElementCount + (VectorWidth - 1)) / VectorWidth
//    and evaluate if expression isKnownNonNegative:
//        (((ElementCount + (VectorWidth - 1)) / VectorWidth) - TripCount
// 3) The IV must be an induction phi with an increment equal to the
//    vector width.
const SCEV *MVETailPredication::IsSafeActiveMask(IntrinsicInst *ActiveLaneMask,
                                                 Value *TripCount) {
  bool ForceTailPredication =
    EnableTailPredication == TailPredication::ForceEnabledNoReductions ||
    EnableTailPredication == TailPredication::ForceEnabled;

  Value *ElemCount = ActiveLaneMask->getOperand(1);
  bool Changed = false;
  if (!L->makeLoopInvariant(ElemCount, Changed))
    return nullptr;

  auto *EC= SE->getSCEV(ElemCount);
  auto *TC = SE->getSCEV(TripCount);
  int VectorWidth =
      cast<FixedVectorType>(ActiveLaneMask->getType())->getNumElements();
  if (VectorWidth != 2 && VectorWidth != 4 && VectorWidth != 8 &&
      VectorWidth != 16)
    return nullptr;
  ConstantInt *ConstElemCount = nullptr;

  // 1) Smoke tests that the original scalar loop TripCount (TC) belongs to
  // this loop.  The scalar tripcount corresponds the number of elements
  // processed by the loop, so we will refer to that from this point on.
  if (!SE->isLoopInvariant(EC, L)) {
    LLVM_DEBUG(dbgs() << "ARM TP: element count must be loop invariant.\n");
    return nullptr;
  }

  // 2) Find out if IV is an induction phi. Note that we can't use Loop
  // helpers here to get the induction variable, because the hardware loop is
  // no longer in loopsimplify form, and also the hwloop intrinsic uses a
  // different counter. Using SCEV, we check that the induction is of the
  // form i = i + 4, where the increment must be equal to the VectorWidth.
  auto *IV = ActiveLaneMask->getOperand(0);
  auto *IVExpr = SE->getSCEV(IV);
  auto *AddExpr = dyn_cast<SCEVAddRecExpr>(IVExpr);

  if (!AddExpr) {
    LLVM_DEBUG(dbgs() << "ARM TP: induction not an add expr: "; IVExpr->dump());
    return nullptr;
  }
  // Check that this AddRec is associated with this loop.
  if (AddExpr->getLoop() != L) {
    LLVM_DEBUG(dbgs() << "ARM TP: phi not part of this loop\n");
    return nullptr;
  }
  auto *Step = dyn_cast<SCEVConstant>(AddExpr->getOperand(1));
  if (!Step) {
    LLVM_DEBUG(dbgs() << "ARM TP: induction step is not a constant: ";
               AddExpr->getOperand(1)->dump());
    return nullptr;
  }
  auto StepValue = Step->getValue()->getSExtValue();
  if (VectorWidth != StepValue) {
    LLVM_DEBUG(dbgs() << "ARM TP: Step value " << StepValue
                      << " doesn't match vector width " << VectorWidth << "\n");
    return nullptr;
  }

  if ((ConstElemCount = dyn_cast<ConstantInt>(ElemCount))) {
    ConstantInt *TC = dyn_cast<ConstantInt>(TripCount);
    if (!TC) {
      LLVM_DEBUG(dbgs() << "ARM TP: Constant tripcount expected in "
                           "set.loop.iterations\n");
      return nullptr;
    }

    // Calculate 2 tripcount values and check that they are consistent with
    // each other. The TripCount for a predicated vector loop body is
    // ceil(ElementCount/Width), or floor((ElementCount+Width-1)/Width) as we
    // work it out here.
    uint64_t TC1 = TC->getZExtValue();
    uint64_t TC2 =
        (ConstElemCount->getZExtValue() + VectorWidth - 1) / VectorWidth;

    // If the tripcount values are inconsistent, we can't insert the VCTP and
    // trigger tail-predication; keep the intrinsic as a get.active.lane.mask
    // and legalize this.
    if (TC1 != TC2) {
      LLVM_DEBUG(dbgs() << "ARM TP: inconsistent constant tripcount values: "
                 << TC1 << " from set.loop.iterations, and "
                 << TC2 << " from get.active.lane.mask\n");
      return nullptr;
    }
  } else if (!ForceTailPredication) {
    // 3) We need to prove that the sub expression that we create in the
    // tail-predicated loop body, which calculates the remaining elements to be
    // processed, is non-negative, i.e. it doesn't overflow:
    //
    //   ((ElementCount + VectorWidth - 1) / VectorWidth) - TripCount >= 0
    //
    // This is true if:
    //
    //    TripCount == (ElementCount + VectorWidth - 1) / VectorWidth
    //
    // which what we will be using here.
    //
    auto *VW = SE->getSCEV(ConstantInt::get(TripCount->getType(), VectorWidth));
    // ElementCount + (VW-1):
    auto *Start = AddExpr->getStart();
    auto *ECPlusVWMinus1 = SE->getAddExpr(EC,
        SE->getSCEV(ConstantInt::get(TripCount->getType(), VectorWidth - 1)));

    // Ceil = ElementCount + (VW-1) / VW
    auto *Ceil = SE->getUDivExpr(ECPlusVWMinus1, VW);

    // Prevent unused variable warnings with TC
    (void)TC;
    LLVM_DEBUG({
      dbgs() << "ARM TP: Analysing overflow behaviour for:\n";
      dbgs() << "ARM TP: - TripCount = " << *TC << "\n";
      dbgs() << "ARM TP: - ElemCount = " << *EC << "\n";
      dbgs() << "ARM TP: - Start = " << *Start << "\n";
      dbgs() << "ARM TP: - BETC = " << *SE->getBackedgeTakenCount(L) << "\n";
      dbgs() << "ARM TP: - VecWidth =  " << VectorWidth << "\n";
      dbgs() << "ARM TP: - (ElemCount+VW-1) / VW = " << *Ceil << "\n";
    });

    // As an example, almost all the tripcount expressions (produced by the
    // vectoriser) look like this:
    //
    //   TC = ((-4 + (4 * ((3 + %N) /u 4))<nuw> - start) /u 4)
    //
    // and "ElementCount + (VW-1) / VW":
    //
    //   Ceil = ((3 + %N) /u 4)
    //
    // Check for equality of TC and Ceil by calculating SCEV expression
    // TC - Ceil and test it for zero.
    //
    const SCEV *Div = SE->getUDivExpr(
        SE->getAddExpr(SE->getMulExpr(Ceil, VW), SE->getNegativeSCEV(VW),
                       SE->getNegativeSCEV(Start)),
        VW);
    const SCEV *Sub = SE->getMinusSCEV(SE->getBackedgeTakenCount(L), Div);
    LLVM_DEBUG(dbgs() << "ARM TP: - Sub       = "; Sub->dump());

    // Use context sensitive facts about the path to the loop to refine.  This
    // comes up as the backedge taken count can incorporate context sensitive
    // reasoning, and our RHS just above doesn't.
    Sub = SE->applyLoopGuards(Sub, L);
    LLVM_DEBUG(dbgs() << "ARM TP: - (Guarded) = "; Sub->dump());

    if (!Sub->isZero()) {
      LLVM_DEBUG(dbgs() << "ARM TP: possible overflow in sub expression.\n");
      return nullptr;
    }
  }

  // Check that the start value is a multiple of the VectorWidth.
  // TODO: This could do with a method to check if the scev is a multiple of
  // VectorWidth. For the moment we just check for constants, muls and unknowns
  // (which use MaskedValueIsZero and seems to be the most common).
  if (auto *BaseC = dyn_cast<SCEVConstant>(AddExpr->getStart())) {
    if (BaseC->getAPInt().urem(VectorWidth) == 0)
      return SE->getMinusSCEV(EC, BaseC);
  } else if (auto *BaseV = dyn_cast<SCEVUnknown>(AddExpr->getStart())) {
    Type *Ty = BaseV->getType();
    APInt Mask = APInt::getLowBitsSet(Ty->getPrimitiveSizeInBits(),
                                      Log2_64(VectorWidth));
    if (MaskedValueIsZero(BaseV->getValue(), Mask,
                          L->getHeader()->getDataLayout()))
      return SE->getMinusSCEV(EC, BaseV);
  } else if (auto *BaseMul = dyn_cast<SCEVMulExpr>(AddExpr->getStart())) {
    if (auto *BaseC = dyn_cast<SCEVConstant>(BaseMul->getOperand(0)))
      if (BaseC->getAPInt().urem(VectorWidth) == 0)
        return SE->getMinusSCEV(EC, BaseC);
    if (auto *BaseC = dyn_cast<SCEVConstant>(BaseMul->getOperand(1)))
      if (BaseC->getAPInt().urem(VectorWidth) == 0)
        return SE->getMinusSCEV(EC, BaseC);
  }

  LLVM_DEBUG(
      dbgs() << "ARM TP: induction base is not know to be a multiple of VF: "
             << *AddExpr->getOperand(0) << "\n");
  return nullptr;
}

void MVETailPredication::InsertVCTPIntrinsic(IntrinsicInst *ActiveLaneMask,
                                             Value *Start) {
  IRBuilder<> Builder(L->getLoopPreheader()->getTerminator());
  Module *M = L->getHeader()->getModule();
  Type *Ty = IntegerType::get(M->getContext(), 32);
  unsigned VectorWidth =
      cast<FixedVectorType>(ActiveLaneMask->getType())->getNumElements();

  // Insert a phi to count the number of elements processed by the loop.
  Builder.SetInsertPoint(L->getHeader(), L->getHeader()->getFirstNonPHIIt());
  PHINode *Processed = Builder.CreatePHI(Ty, 2);
  Processed->addIncoming(Start, L->getLoopPreheader());

  // Replace @llvm.get.active.mask() with the ARM specific VCTP intrinic, and
  // thus represent the effect of tail predication.
  Builder.SetInsertPoint(ActiveLaneMask);
  ConstantInt *Factor = ConstantInt::get(cast<IntegerType>(Ty), VectorWidth);

  Intrinsic::ID VCTPID;
  switch (VectorWidth) {
  default:
    llvm_unreachable("unexpected number of lanes");
  case 2:  VCTPID = Intrinsic::arm_mve_vctp64; break;
  case 4:  VCTPID = Intrinsic::arm_mve_vctp32; break;
  case 8:  VCTPID = Intrinsic::arm_mve_vctp16; break;
  case 16: VCTPID = Intrinsic::arm_mve_vctp8; break;
  }
  Function *VCTP = Intrinsic::getDeclaration(M, VCTPID);
  Value *VCTPCall = Builder.CreateCall(VCTP, Processed);
  ActiveLaneMask->replaceAllUsesWith(VCTPCall);

  // Add the incoming value to the new phi.
  // TODO: This add likely already exists in the loop.
  Value *Remaining = Builder.CreateSub(Processed, Factor);
  Processed->addIncoming(Remaining, L->getLoopLatch());
  LLVM_DEBUG(dbgs() << "ARM TP: Insert processed elements phi: "
             << *Processed << "\n"
             << "ARM TP: Inserted VCTP: " << *VCTPCall << "\n");
}

bool MVETailPredication::TryConvertActiveLaneMask(Value *TripCount) {
  SmallVector<IntrinsicInst *, 4> ActiveLaneMasks;
  for (auto *BB : L->getBlocks())
    for (auto &I : *BB)
      if (auto *Int = dyn_cast<IntrinsicInst>(&I))
        if (Int->getIntrinsicID() == Intrinsic::get_active_lane_mask)
          ActiveLaneMasks.push_back(Int);

  if (ActiveLaneMasks.empty())
    return false;

  LLVM_DEBUG(dbgs() << "ARM TP: Found predicated vector loop.\n");

  for (auto *ActiveLaneMask : ActiveLaneMasks) {
    LLVM_DEBUG(dbgs() << "ARM TP: Found active lane mask: "
                      << *ActiveLaneMask << "\n");

    const SCEV *StartSCEV = IsSafeActiveMask(ActiveLaneMask, TripCount);
    if (!StartSCEV) {
      LLVM_DEBUG(dbgs() << "ARM TP: Not safe to insert VCTP.\n");
      return false;
    }
    LLVM_DEBUG(dbgs() << "ARM TP: Safe to insert VCTP. Start is " << *StartSCEV
                      << "\n");
    SCEVExpander Expander(*SE, L->getHeader()->getDataLayout(),
                          "start");
    Instruction *Ins = L->getLoopPreheader()->getTerminator();
    Value *Start = Expander.expandCodeFor(StartSCEV, StartSCEV->getType(), Ins);
    LLVM_DEBUG(dbgs() << "ARM TP: Created start value " << *Start << "\n");
    InsertVCTPIntrinsic(ActiveLaneMask, Start);
  }

  // Remove dead instructions and now dead phis.
  for (auto *II : ActiveLaneMasks)
    RecursivelyDeleteTriviallyDeadInstructions(II);
  for (auto *I : L->blocks())
    DeleteDeadPHIs(I);
  return true;
}

Pass *llvm::createMVETailPredicationPass() {
  return new MVETailPredication();
}

char MVETailPredication::ID = 0;

INITIALIZE_PASS_BEGIN(MVETailPredication, DEBUG_TYPE, DESC, false, false)
INITIALIZE_PASS_END(MVETailPredication, DEBUG_TYPE, DESC, false, false)
