//===- VPlanRecipes.cpp - Implementations for VPlan recipes ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains implementations for different VPlan recipes.
///
//===----------------------------------------------------------------------===//

#include "VPlan.h"
#include "VPlanAnalysis.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <cassert>

using namespace llvm;

using VectorParts = SmallVector<Value *, 2>;

namespace llvm {
extern cl::opt<bool> EnableVPlanNativePath;
}
extern cl::opt<unsigned> ForceTargetInstructionCost;

#define LV_NAME "loop-vectorize"
#define DEBUG_TYPE LV_NAME

bool VPRecipeBase::mayWriteToMemory() const {
  switch (getVPDefID()) {
  case VPInterleaveSC:
    return cast<VPInterleaveRecipe>(this)->getNumStoreOperands() > 0;
  case VPWidenStoreEVLSC:
  case VPWidenStoreSC:
    return true;
  case VPReplicateSC:
    return cast<Instruction>(getVPSingleValue()->getUnderlyingValue())
        ->mayWriteToMemory();
  case VPWidenCallSC:
    return !cast<VPWidenCallRecipe>(this)
                ->getCalledScalarFunction()
                ->onlyReadsMemory();
  case VPBranchOnMaskSC:
  case VPScalarIVStepsSC:
  case VPPredInstPHISC:
    return false;
  case VPBlendSC:
  case VPReductionEVLSC:
  case VPReductionSC:
  case VPWidenCanonicalIVSC:
  case VPWidenCastSC:
  case VPWidenGEPSC:
  case VPWidenIntOrFpInductionSC:
  case VPWidenLoadEVLSC:
  case VPWidenLoadSC:
  case VPWidenPHISC:
  case VPWidenSC:
  case VPWidenSelectSC: {
    const Instruction *I =
        dyn_cast_or_null<Instruction>(getVPSingleValue()->getUnderlyingValue());
    (void)I;
    assert((!I || !I->mayWriteToMemory()) &&
           "underlying instruction may write to memory");
    return false;
  }
  default:
    return true;
  }
}

bool VPRecipeBase::mayReadFromMemory() const {
  switch (getVPDefID()) {
  case VPWidenLoadEVLSC:
  case VPWidenLoadSC:
    return true;
  case VPReplicateSC:
    return cast<Instruction>(getVPSingleValue()->getUnderlyingValue())
        ->mayReadFromMemory();
  case VPWidenCallSC:
    return !cast<VPWidenCallRecipe>(this)
                ->getCalledScalarFunction()
                ->onlyWritesMemory();
  case VPBranchOnMaskSC:
  case VPPredInstPHISC:
  case VPScalarIVStepsSC:
  case VPWidenStoreEVLSC:
  case VPWidenStoreSC:
    return false;
  case VPBlendSC:
  case VPReductionEVLSC:
  case VPReductionSC:
  case VPWidenCanonicalIVSC:
  case VPWidenCastSC:
  case VPWidenGEPSC:
  case VPWidenIntOrFpInductionSC:
  case VPWidenPHISC:
  case VPWidenSC:
  case VPWidenSelectSC: {
    const Instruction *I =
        dyn_cast_or_null<Instruction>(getVPSingleValue()->getUnderlyingValue());
    (void)I;
    assert((!I || !I->mayReadFromMemory()) &&
           "underlying instruction may read from memory");
    return false;
  }
  default:
    return true;
  }
}

bool VPRecipeBase::mayHaveSideEffects() const {
  switch (getVPDefID()) {
  case VPDerivedIVSC:
  case VPPredInstPHISC:
  case VPScalarCastSC:
    return false;
  case VPInstructionSC:
    switch (cast<VPInstruction>(this)->getOpcode()) {
    case Instruction::Or:
    case Instruction::ICmp:
    case Instruction::Select:
    case VPInstruction::Not:
    case VPInstruction::CalculateTripCountMinusVF:
    case VPInstruction::CanonicalIVIncrementForPart:
    case VPInstruction::ExtractFromEnd:
    case VPInstruction::FirstOrderRecurrenceSplice:
    case VPInstruction::LogicalAnd:
    case VPInstruction::PtrAdd:
      return false;
    default:
      return true;
    }
  case VPWidenCallSC: {
    Function *Fn = cast<VPWidenCallRecipe>(this)->getCalledScalarFunction();
    return mayWriteToMemory() || !Fn->doesNotThrow() || !Fn->willReturn();
  }
  case VPBlendSC:
  case VPReductionEVLSC:
  case VPReductionSC:
  case VPScalarIVStepsSC:
  case VPWidenCanonicalIVSC:
  case VPWidenCastSC:
  case VPWidenGEPSC:
  case VPWidenIntOrFpInductionSC:
  case VPWidenPHISC:
  case VPWidenPointerInductionSC:
  case VPWidenSC:
  case VPWidenSelectSC: {
    const Instruction *I =
        dyn_cast_or_null<Instruction>(getVPSingleValue()->getUnderlyingValue());
    (void)I;
    assert((!I || !I->mayHaveSideEffects()) &&
           "underlying instruction has side-effects");
    return false;
  }
  case VPInterleaveSC:
    return mayWriteToMemory();
  case VPWidenLoadEVLSC:
  case VPWidenLoadSC:
  case VPWidenStoreEVLSC:
  case VPWidenStoreSC:
    assert(
        cast<VPWidenMemoryRecipe>(this)->getIngredient().mayHaveSideEffects() ==
            mayWriteToMemory() &&
        "mayHaveSideffects result for ingredient differs from this "
        "implementation");
    return mayWriteToMemory();
  case VPReplicateSC: {
    auto *R = cast<VPReplicateRecipe>(this);
    return R->getUnderlyingInstr()->mayHaveSideEffects();
  }
  default:
    return true;
  }
}

void VPLiveOut::fixPhi(VPlan &Plan, VPTransformState &State) {
  VPValue *ExitValue = getOperand(0);
  auto Lane = vputils::isUniformAfterVectorization(ExitValue)
                  ? VPLane::getFirstLane()
                  : VPLane::getLastLaneForVF(State.VF);
  VPBasicBlock *MiddleVPBB =
      cast<VPBasicBlock>(Plan.getVectorLoopRegion()->getSingleSuccessor());
  VPRecipeBase *ExitingRecipe = ExitValue->getDefiningRecipe();
  auto *ExitingVPBB = ExitingRecipe ? ExitingRecipe->getParent() : nullptr;
  // Values leaving the vector loop reach live out phi's in the exiting block
  // via middle block.
  auto *PredVPBB = !ExitingVPBB || ExitingVPBB->getEnclosingLoopRegion()
                       ? MiddleVPBB
                       : ExitingVPBB;
  BasicBlock *PredBB = State.CFG.VPBB2IRBB[PredVPBB];
  // Set insertion point in PredBB in case an extract needs to be generated.
  // TODO: Model extracts explicitly.
  State.Builder.SetInsertPoint(PredBB, PredBB->getFirstNonPHIIt());
  Value *V = State.get(ExitValue, VPIteration(State.UF - 1, Lane));
  if (Phi->getBasicBlockIndex(PredBB) != -1)
    Phi->setIncomingValueForBlock(PredBB, V);
  else
    Phi->addIncoming(V, PredBB);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPLiveOut::print(raw_ostream &O, VPSlotTracker &SlotTracker) const {
  O << "Live-out ";
  getPhi()->printAsOperand(O);
  O << " = ";
  getOperand(0)->printAsOperand(O, SlotTracker);
  O << "\n";
}
#endif

void VPRecipeBase::insertBefore(VPRecipeBase *InsertPos) {
  assert(!Parent && "Recipe already in some VPBasicBlock");
  assert(InsertPos->getParent() &&
         "Insertion position not in any VPBasicBlock");
  InsertPos->getParent()->insert(this, InsertPos->getIterator());
}

void VPRecipeBase::insertBefore(VPBasicBlock &BB,
                                iplist<VPRecipeBase>::iterator I) {
  assert(!Parent && "Recipe already in some VPBasicBlock");
  assert(I == BB.end() || I->getParent() == &BB);
  BB.insert(this, I);
}

void VPRecipeBase::insertAfter(VPRecipeBase *InsertPos) {
  assert(!Parent && "Recipe already in some VPBasicBlock");
  assert(InsertPos->getParent() &&
         "Insertion position not in any VPBasicBlock");
  InsertPos->getParent()->insert(this, std::next(InsertPos->getIterator()));
}

void VPRecipeBase::removeFromParent() {
  assert(getParent() && "Recipe not in any VPBasicBlock");
  getParent()->getRecipeList().remove(getIterator());
  Parent = nullptr;
}

iplist<VPRecipeBase>::iterator VPRecipeBase::eraseFromParent() {
  assert(getParent() && "Recipe not in any VPBasicBlock");
  return getParent()->getRecipeList().erase(getIterator());
}

void VPRecipeBase::moveAfter(VPRecipeBase *InsertPos) {
  removeFromParent();
  insertAfter(InsertPos);
}

void VPRecipeBase::moveBefore(VPBasicBlock &BB,
                              iplist<VPRecipeBase>::iterator I) {
  removeFromParent();
  insertBefore(BB, I);
}

/// Return the underlying instruction to be used for computing \p R's cost via
/// the legacy cost model. Return nullptr if there's no suitable instruction.
static Instruction *getInstructionForCost(const VPRecipeBase *R) {
  if (auto *S = dyn_cast<VPSingleDefRecipe>(R))
    return dyn_cast_or_null<Instruction>(S->getUnderlyingValue());
  if (auto *IG = dyn_cast<VPInterleaveRecipe>(R))
    return IG->getInsertPos();
  if (auto *WidenMem = dyn_cast<VPWidenMemoryRecipe>(R))
    return &WidenMem->getIngredient();
  return nullptr;
}

InstructionCost VPRecipeBase::cost(ElementCount VF, VPCostContext &Ctx) {
  if (auto *UI = getInstructionForCost(this))
    if (Ctx.skipCostComputation(UI, VF.isVector()))
      return 0;

  InstructionCost RecipeCost = computeCost(VF, Ctx);
  if (ForceTargetInstructionCost.getNumOccurrences() > 0 &&
      RecipeCost.isValid())
    RecipeCost = InstructionCost(ForceTargetInstructionCost);

  LLVM_DEBUG({
    dbgs() << "Cost of " << RecipeCost << " for VF " << VF << ": ";
    dump();
  });
  return RecipeCost;
}

InstructionCost VPRecipeBase::computeCost(ElementCount VF,
                                          VPCostContext &Ctx) const {
  // Compute the cost for the recipe falling back to the legacy cost model using
  // the underlying instruction. If there is no underlying instruction, returns
  // 0.
  Instruction *UI = getInstructionForCost(this);
  if (UI && isa<VPReplicateRecipe>(this)) {
    // VPReplicateRecipe may be cloned as part of an existing VPlan-to-VPlan
    // transform, avoid computing their cost multiple times for now.
    Ctx.SkipCostComputation.insert(UI);
  }
  return UI ? Ctx.getLegacyCost(UI, VF) : 0;
}

FastMathFlags VPRecipeWithIRFlags::getFastMathFlags() const {
  assert(OpType == OperationType::FPMathOp &&
         "recipe doesn't have fast math flags");
  FastMathFlags Res;
  Res.setAllowReassoc(FMFs.AllowReassoc);
  Res.setNoNaNs(FMFs.NoNaNs);
  Res.setNoInfs(FMFs.NoInfs);
  Res.setNoSignedZeros(FMFs.NoSignedZeros);
  Res.setAllowReciprocal(FMFs.AllowReciprocal);
  Res.setAllowContract(FMFs.AllowContract);
  Res.setApproxFunc(FMFs.ApproxFunc);
  return Res;
}

VPInstruction::VPInstruction(unsigned Opcode, CmpInst::Predicate Pred,
                             VPValue *A, VPValue *B, DebugLoc DL,
                             const Twine &Name)
    : VPRecipeWithIRFlags(VPDef::VPInstructionSC, ArrayRef<VPValue *>({A, B}),
                          Pred, DL),
      Opcode(Opcode), Name(Name.str()) {
  assert(Opcode == Instruction::ICmp &&
         "only ICmp predicates supported at the moment");
}

VPInstruction::VPInstruction(unsigned Opcode,
                             std::initializer_list<VPValue *> Operands,
                             FastMathFlags FMFs, DebugLoc DL, const Twine &Name)
    : VPRecipeWithIRFlags(VPDef::VPInstructionSC, Operands, FMFs, DL),
      Opcode(Opcode), Name(Name.str()) {
  // Make sure the VPInstruction is a floating-point operation.
  assert(isFPMathOp() && "this op can't take fast-math flags");
}

bool VPInstruction::doesGeneratePerAllLanes() const {
  return Opcode == VPInstruction::PtrAdd && !vputils::onlyFirstLaneUsed(this);
}

bool VPInstruction::canGenerateScalarForFirstLane() const {
  if (Instruction::isBinaryOp(getOpcode()))
    return true;
  if (isSingleScalar() || isVectorToScalar())
    return true;
  switch (Opcode) {
  case Instruction::ICmp:
  case VPInstruction::BranchOnCond:
  case VPInstruction::BranchOnCount:
  case VPInstruction::CalculateTripCountMinusVF:
  case VPInstruction::CanonicalIVIncrementForPart:
  case VPInstruction::PtrAdd:
  case VPInstruction::ExplicitVectorLength:
    return true;
  default:
    return false;
  }
}

Value *VPInstruction::generatePerLane(VPTransformState &State,
                                      const VPIteration &Lane) {
  IRBuilderBase &Builder = State.Builder;

  assert(getOpcode() == VPInstruction::PtrAdd &&
         "only PtrAdd opcodes are supported for now");
  return Builder.CreatePtrAdd(State.get(getOperand(0), Lane),
                              State.get(getOperand(1), Lane), Name);
}

Value *VPInstruction::generatePerPart(VPTransformState &State, unsigned Part) {
  IRBuilderBase &Builder = State.Builder;

  if (Instruction::isBinaryOp(getOpcode())) {
    bool OnlyFirstLaneUsed = vputils::onlyFirstLaneUsed(this);
    Value *A = State.get(getOperand(0), Part, OnlyFirstLaneUsed);
    Value *B = State.get(getOperand(1), Part, OnlyFirstLaneUsed);
    auto *Res =
        Builder.CreateBinOp((Instruction::BinaryOps)getOpcode(), A, B, Name);
    if (auto *I = dyn_cast<Instruction>(Res))
      setFlags(I);
    return Res;
  }

  switch (getOpcode()) {
  case VPInstruction::Not: {
    Value *A = State.get(getOperand(0), Part);
    return Builder.CreateNot(A, Name);
  }
  case Instruction::ICmp: {
    bool OnlyFirstLaneUsed = vputils::onlyFirstLaneUsed(this);
    Value *A = State.get(getOperand(0), Part, OnlyFirstLaneUsed);
    Value *B = State.get(getOperand(1), Part, OnlyFirstLaneUsed);
    return Builder.CreateCmp(getPredicate(), A, B, Name);
  }
  case Instruction::Select: {
    Value *Cond = State.get(getOperand(0), Part);
    Value *Op1 = State.get(getOperand(1), Part);
    Value *Op2 = State.get(getOperand(2), Part);
    return Builder.CreateSelect(Cond, Op1, Op2, Name);
  }
  case VPInstruction::ActiveLaneMask: {
    // Get first lane of vector induction variable.
    Value *VIVElem0 = State.get(getOperand(0), VPIteration(Part, 0));
    // Get the original loop tripcount.
    Value *ScalarTC = State.get(getOperand(1), VPIteration(Part, 0));

    // If this part of the active lane mask is scalar, generate the CMP directly
    // to avoid unnecessary extracts.
    if (State.VF.isScalar())
      return Builder.CreateCmp(CmpInst::Predicate::ICMP_ULT, VIVElem0, ScalarTC,
                               Name);

    auto *Int1Ty = Type::getInt1Ty(Builder.getContext());
    auto *PredTy = VectorType::get(Int1Ty, State.VF);
    return Builder.CreateIntrinsic(Intrinsic::get_active_lane_mask,
                                   {PredTy, ScalarTC->getType()},
                                   {VIVElem0, ScalarTC}, nullptr, Name);
  }
  case VPInstruction::FirstOrderRecurrenceSplice: {
    // Generate code to combine the previous and current values in vector v3.
    //
    //   vector.ph:
    //     v_init = vector(..., ..., ..., a[-1])
    //     br vector.body
    //
    //   vector.body
    //     i = phi [0, vector.ph], [i+4, vector.body]
    //     v1 = phi [v_init, vector.ph], [v2, vector.body]
    //     v2 = a[i, i+1, i+2, i+3];
    //     v3 = vector(v1(3), v2(0, 1, 2))

    // For the first part, use the recurrence phi (v1), otherwise v2.
    auto *V1 = State.get(getOperand(0), 0);
    Value *PartMinus1 = Part == 0 ? V1 : State.get(getOperand(1), Part - 1);
    if (!PartMinus1->getType()->isVectorTy())
      return PartMinus1;
    Value *V2 = State.get(getOperand(1), Part);
    return Builder.CreateVectorSplice(PartMinus1, V2, -1, Name);
  }
  case VPInstruction::CalculateTripCountMinusVF: {
    if (Part != 0)
      return State.get(this, 0, /*IsScalar*/ true);

    Value *ScalarTC = State.get(getOperand(0), {0, 0});
    Value *Step =
        createStepForVF(Builder, ScalarTC->getType(), State.VF, State.UF);
    Value *Sub = Builder.CreateSub(ScalarTC, Step);
    Value *Cmp = Builder.CreateICmp(CmpInst::Predicate::ICMP_UGT, ScalarTC, Step);
    Value *Zero = ConstantInt::get(ScalarTC->getType(), 0);
    return Builder.CreateSelect(Cmp, Sub, Zero);
  }
  case VPInstruction::ExplicitVectorLength: {
    // Compute EVL
    auto GetEVL = [=](VPTransformState &State, Value *AVL) {
      assert(AVL->getType()->isIntegerTy() &&
             "Requested vector length should be an integer.");

      // TODO: Add support for MaxSafeDist for correct loop emission.
      assert(State.VF.isScalable() && "Expected scalable vector factor.");
      Value *VFArg = State.Builder.getInt32(State.VF.getKnownMinValue());

      Value *EVL = State.Builder.CreateIntrinsic(
          State.Builder.getInt32Ty(), Intrinsic::experimental_get_vector_length,
          {AVL, VFArg, State.Builder.getTrue()});
      return EVL;
    };
    // TODO: Restructure this code with an explicit remainder loop, vsetvli can
    // be outside of the main loop.
    assert(Part == 0 && "No unrolling expected for predicated vectorization.");
    // Compute VTC - IV as the AVL (requested vector length).
    Value *Index = State.get(getOperand(0), VPIteration(0, 0));
    Value *TripCount = State.get(getOperand(1), VPIteration(0, 0));
    Value *AVL = State.Builder.CreateSub(TripCount, Index);
    Value *EVL = GetEVL(State, AVL);
    return EVL;
  }
  case VPInstruction::CanonicalIVIncrementForPart: {
    auto *IV = State.get(getOperand(0), VPIteration(0, 0));
    if (Part == 0)
      return IV;

    // The canonical IV is incremented by the vectorization factor (num of SIMD
    // elements) times the unroll part.
    Value *Step = createStepForVF(Builder, IV->getType(), State.VF, Part);
    return Builder.CreateAdd(IV, Step, Name, hasNoUnsignedWrap(),
                             hasNoSignedWrap());
  }
  case VPInstruction::BranchOnCond: {
    if (Part != 0)
      return nullptr;

    Value *Cond = State.get(getOperand(0), VPIteration(Part, 0));
    // Replace the temporary unreachable terminator with a new conditional
    // branch, hooking it up to backward destination for exiting blocks now and
    // to forward destination(s) later when they are created.
    BranchInst *CondBr =
        Builder.CreateCondBr(Cond, Builder.GetInsertBlock(), nullptr);
    CondBr->setSuccessor(0, nullptr);
    Builder.GetInsertBlock()->getTerminator()->eraseFromParent();

    if (!getParent()->isExiting())
      return CondBr;

    VPRegionBlock *ParentRegion = getParent()->getParent();
    VPBasicBlock *Header = ParentRegion->getEntryBasicBlock();
    CondBr->setSuccessor(1, State.CFG.VPBB2IRBB[Header]);
    return CondBr;
  }
  case VPInstruction::BranchOnCount: {
    if (Part != 0)
      return nullptr;
    // First create the compare.
    Value *IV = State.get(getOperand(0), Part, /*IsScalar*/ true);
    Value *TC = State.get(getOperand(1), Part, /*IsScalar*/ true);
    Value *Cond = Builder.CreateICmpEQ(IV, TC);

    // Now create the branch.
    auto *Plan = getParent()->getPlan();
    VPRegionBlock *TopRegion = Plan->getVectorLoopRegion();
    VPBasicBlock *Header = TopRegion->getEntry()->getEntryBasicBlock();

    // Replace the temporary unreachable terminator with a new conditional
    // branch, hooking it up to backward destination (the header) now and to the
    // forward destination (the exit/middle block) later when it is created.
    // Note that CreateCondBr expects a valid BB as first argument, so we need
    // to set it to nullptr later.
    BranchInst *CondBr = Builder.CreateCondBr(Cond, Builder.GetInsertBlock(),
                                              State.CFG.VPBB2IRBB[Header]);
    CondBr->setSuccessor(0, nullptr);
    Builder.GetInsertBlock()->getTerminator()->eraseFromParent();
    return CondBr;
  }
  case VPInstruction::ComputeReductionResult: {
    if (Part != 0)
      return State.get(this, 0, /*IsScalar*/ true);

    // FIXME: The cross-recipe dependency on VPReductionPHIRecipe is temporary
    // and will be removed by breaking up the recipe further.
    auto *PhiR = cast<VPReductionPHIRecipe>(getOperand(0));
    auto *OrigPhi = cast<PHINode>(PhiR->getUnderlyingValue());
    // Get its reduction variable descriptor.
    const RecurrenceDescriptor &RdxDesc = PhiR->getRecurrenceDescriptor();

    RecurKind RK = RdxDesc.getRecurrenceKind();

    VPValue *LoopExitingDef = getOperand(1);
    Type *PhiTy = OrigPhi->getType();
    VectorParts RdxParts(State.UF);
    for (unsigned Part = 0; Part < State.UF; ++Part)
      RdxParts[Part] = State.get(LoopExitingDef, Part, PhiR->isInLoop());

    // If the vector reduction can be performed in a smaller type, we truncate
    // then extend the loop exit value to enable InstCombine to evaluate the
    // entire expression in the smaller type.
    // TODO: Handle this in truncateToMinBW.
    if (State.VF.isVector() && PhiTy != RdxDesc.getRecurrenceType()) {
      Type *RdxVecTy = VectorType::get(RdxDesc.getRecurrenceType(), State.VF);
      for (unsigned Part = 0; Part < State.UF; ++Part)
        RdxParts[Part] = Builder.CreateTrunc(RdxParts[Part], RdxVecTy);
    }
    // Reduce all of the unrolled parts into a single vector.
    Value *ReducedPartRdx = RdxParts[0];
    unsigned Op = RecurrenceDescriptor::getOpcode(RK);
    if (RecurrenceDescriptor::isAnyOfRecurrenceKind(RK))
      Op = Instruction::Or;

    if (PhiR->isOrdered()) {
      ReducedPartRdx = RdxParts[State.UF - 1];
    } else {
      // Floating-point operations should have some FMF to enable the reduction.
      IRBuilderBase::FastMathFlagGuard FMFG(Builder);
      Builder.setFastMathFlags(RdxDesc.getFastMathFlags());
      for (unsigned Part = 1; Part < State.UF; ++Part) {
        Value *RdxPart = RdxParts[Part];
        if (Op != Instruction::ICmp && Op != Instruction::FCmp)
          ReducedPartRdx = Builder.CreateBinOp(
              (Instruction::BinaryOps)Op, RdxPart, ReducedPartRdx, "bin.rdx");
        else
          ReducedPartRdx = createMinMaxOp(Builder, RK, ReducedPartRdx, RdxPart);
      }
    }

    // Create the reduction after the loop. Note that inloop reductions create
    // the target reduction in the loop using a Reduction recipe.
    if ((State.VF.isVector() ||
         RecurrenceDescriptor::isAnyOfRecurrenceKind(RK)) &&
        !PhiR->isInLoop()) {
      ReducedPartRdx =
          createTargetReduction(Builder, RdxDesc, ReducedPartRdx, OrigPhi);
      // If the reduction can be performed in a smaller type, we need to extend
      // the reduction to the wider type before we branch to the original loop.
      if (PhiTy != RdxDesc.getRecurrenceType())
        ReducedPartRdx = RdxDesc.isSigned()
                             ? Builder.CreateSExt(ReducedPartRdx, PhiTy)
                             : Builder.CreateZExt(ReducedPartRdx, PhiTy);
    }

    // If there were stores of the reduction value to a uniform memory address
    // inside the loop, create the final store here.
    if (StoreInst *SI = RdxDesc.IntermediateStore) {
      auto *NewSI = Builder.CreateAlignedStore(
          ReducedPartRdx, SI->getPointerOperand(), SI->getAlign());
      propagateMetadata(NewSI, SI);
    }

    return ReducedPartRdx;
  }
  case VPInstruction::ExtractFromEnd: {
    if (Part != 0)
      return State.get(this, 0, /*IsScalar*/ true);

    auto *CI = cast<ConstantInt>(getOperand(1)->getLiveInIRValue());
    unsigned Offset = CI->getZExtValue();
    assert(Offset > 0 && "Offset from end must be positive");
    Value *Res;
    if (State.VF.isVector()) {
      assert(Offset <= State.VF.getKnownMinValue() &&
             "invalid offset to extract from");
      // Extract lane VF - Offset from the operand.
      Res = State.get(
          getOperand(0),
          VPIteration(State.UF - 1, VPLane::getLaneFromEnd(State.VF, Offset)));
    } else {
      assert(Offset <= State.UF && "invalid offset to extract from");
      // When loop is unrolled without vectorizing, retrieve UF - Offset.
      Res = State.get(getOperand(0), State.UF - Offset);
    }
    if (isa<ExtractElementInst>(Res))
      Res->setName(Name);
    return Res;
  }
  case VPInstruction::LogicalAnd: {
    Value *A = State.get(getOperand(0), Part);
    Value *B = State.get(getOperand(1), Part);
    return Builder.CreateLogicalAnd(A, B, Name);
  }
  case VPInstruction::PtrAdd: {
    assert(vputils::onlyFirstLaneUsed(this) &&
           "can only generate first lane for PtrAdd");
    Value *Ptr = State.get(getOperand(0), Part, /* IsScalar */ true);
    Value *Addend = State.get(getOperand(1), Part, /* IsScalar */ true);
    return Builder.CreatePtrAdd(Ptr, Addend, Name);
  }
  case VPInstruction::ResumePhi: {
    if (Part != 0)
      return State.get(this, 0, /*IsScalar*/ true);
    Value *IncomingFromVPlanPred =
        State.get(getOperand(0), Part, /* IsScalar */ true);
    Value *IncomingFromOtherPreds =
        State.get(getOperand(1), Part, /* IsScalar */ true);
    auto *NewPhi =
        Builder.CreatePHI(IncomingFromOtherPreds->getType(), 2, Name);
    BasicBlock *VPlanPred =
        State.CFG
            .VPBB2IRBB[cast<VPBasicBlock>(getParent()->getSinglePredecessor())];
    NewPhi->addIncoming(IncomingFromVPlanPred, VPlanPred);
    for (auto *OtherPred : predecessors(Builder.GetInsertBlock())) {
      assert(OtherPred != VPlanPred &&
             "VPlan predecessors should not be connected yet");
      NewPhi->addIncoming(IncomingFromOtherPreds, OtherPred);
    }
    return NewPhi;
  }

  default:
    llvm_unreachable("Unsupported opcode for instruction");
  }
}

bool VPInstruction::isVectorToScalar() const {
  return getOpcode() == VPInstruction::ExtractFromEnd ||
         getOpcode() == VPInstruction::ComputeReductionResult;
}

bool VPInstruction::isSingleScalar() const {
  return getOpcode() == VPInstruction::ResumePhi;
}

#if !defined(NDEBUG)
bool VPInstruction::isFPMathOp() const {
  // Inspired by FPMathOperator::classof. Notable differences are that we don't
  // support Call, PHI and Select opcodes here yet.
  return Opcode == Instruction::FAdd || Opcode == Instruction::FMul ||
         Opcode == Instruction::FNeg || Opcode == Instruction::FSub ||
         Opcode == Instruction::FDiv || Opcode == Instruction::FRem ||
         Opcode == Instruction::FCmp || Opcode == Instruction::Select;
}
#endif

void VPInstruction::execute(VPTransformState &State) {
  assert(!State.Instance && "VPInstruction executing an Instance");
  IRBuilderBase::FastMathFlagGuard FMFGuard(State.Builder);
  assert((hasFastMathFlags() == isFPMathOp() ||
          getOpcode() == Instruction::Select) &&
         "Recipe not a FPMathOp but has fast-math flags?");
  if (hasFastMathFlags())
    State.Builder.setFastMathFlags(getFastMathFlags());
  State.setDebugLocFrom(getDebugLoc());
  bool GeneratesPerFirstLaneOnly = canGenerateScalarForFirstLane() &&
                                   (vputils::onlyFirstLaneUsed(this) ||
                                    isVectorToScalar() || isSingleScalar());
  bool GeneratesPerAllLanes = doesGeneratePerAllLanes();
  bool OnlyFirstPartUsed = vputils::onlyFirstPartUsed(this);
  for (unsigned Part = 0; Part < State.UF; ++Part) {
    if (GeneratesPerAllLanes) {
      for (unsigned Lane = 0, NumLanes = State.VF.getKnownMinValue();
           Lane != NumLanes; ++Lane) {
        Value *GeneratedValue = generatePerLane(State, VPIteration(Part, Lane));
        assert(GeneratedValue && "generatePerLane must produce a value");
        State.set(this, GeneratedValue, VPIteration(Part, Lane));
      }
      continue;
    }

    if (Part != 0 && OnlyFirstPartUsed && hasResult()) {
      Value *Part0 = State.get(this, 0, /*IsScalar*/ GeneratesPerFirstLaneOnly);
      State.set(this, Part0, Part,
                /*IsScalar*/ GeneratesPerFirstLaneOnly);
      continue;
    }

    Value *GeneratedValue = generatePerPart(State, Part);
    if (!hasResult())
      continue;
    assert(GeneratedValue && "generatePerPart must produce a value");
    assert((GeneratedValue->getType()->isVectorTy() ==
                !GeneratesPerFirstLaneOnly ||
            State.VF.isScalar()) &&
           "scalar value but not only first lane defined");
    State.set(this, GeneratedValue, Part,
              /*IsScalar*/ GeneratesPerFirstLaneOnly);
  }
}

bool VPInstruction::onlyFirstLaneUsed(const VPValue *Op) const {
  assert(is_contained(operands(), Op) && "Op must be an operand of the recipe");
  if (Instruction::isBinaryOp(getOpcode()))
    return vputils::onlyFirstLaneUsed(this);

  switch (getOpcode()) {
  default:
    return false;
  case Instruction::ICmp:
  case VPInstruction::PtrAdd:
    // TODO: Cover additional opcodes.
    return vputils::onlyFirstLaneUsed(this);
  case VPInstruction::ActiveLaneMask:
  case VPInstruction::ExplicitVectorLength:
  case VPInstruction::CalculateTripCountMinusVF:
  case VPInstruction::CanonicalIVIncrementForPart:
  case VPInstruction::BranchOnCount:
  case VPInstruction::BranchOnCond:
  case VPInstruction::ResumePhi:
    return true;
  };
  llvm_unreachable("switch should return");
}

bool VPInstruction::onlyFirstPartUsed(const VPValue *Op) const {
  assert(is_contained(operands(), Op) && "Op must be an operand of the recipe");
  if (Instruction::isBinaryOp(getOpcode()))
    return vputils::onlyFirstPartUsed(this);

  switch (getOpcode()) {
  default:
    return false;
  case Instruction::ICmp:
  case Instruction::Select:
    return vputils::onlyFirstPartUsed(this);
  case VPInstruction::BranchOnCount:
  case VPInstruction::BranchOnCond:
  case VPInstruction::CanonicalIVIncrementForPart:
    return true;
  };
  llvm_unreachable("switch should return");
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPInstruction::dump() const {
  VPSlotTracker SlotTracker(getParent()->getPlan());
  print(dbgs(), "", SlotTracker);
}

void VPInstruction::print(raw_ostream &O, const Twine &Indent,
                          VPSlotTracker &SlotTracker) const {
  O << Indent << "EMIT ";

  if (hasResult()) {
    printAsOperand(O, SlotTracker);
    O << " = ";
  }

  switch (getOpcode()) {
  case VPInstruction::Not:
    O << "not";
    break;
  case VPInstruction::SLPLoad:
    O << "combined load";
    break;
  case VPInstruction::SLPStore:
    O << "combined store";
    break;
  case VPInstruction::ActiveLaneMask:
    O << "active lane mask";
    break;
  case VPInstruction::ResumePhi:
    O << "resume-phi";
    break;
  case VPInstruction::ExplicitVectorLength:
    O << "EXPLICIT-VECTOR-LENGTH";
    break;
  case VPInstruction::FirstOrderRecurrenceSplice:
    O << "first-order splice";
    break;
  case VPInstruction::BranchOnCond:
    O << "branch-on-cond";
    break;
  case VPInstruction::CalculateTripCountMinusVF:
    O << "TC > VF ? TC - VF : 0";
    break;
  case VPInstruction::CanonicalIVIncrementForPart:
    O << "VF * Part +";
    break;
  case VPInstruction::BranchOnCount:
    O << "branch-on-count";
    break;
  case VPInstruction::ExtractFromEnd:
    O << "extract-from-end";
    break;
  case VPInstruction::ComputeReductionResult:
    O << "compute-reduction-result";
    break;
  case VPInstruction::LogicalAnd:
    O << "logical-and";
    break;
  case VPInstruction::PtrAdd:
    O << "ptradd";
    break;
  default:
    O << Instruction::getOpcodeName(getOpcode());
  }

  printFlags(O);
  printOperands(O, SlotTracker);

  if (auto DL = getDebugLoc()) {
    O << ", !dbg ";
    DL.print(O);
  }
}
#endif

void VPWidenCallRecipe::execute(VPTransformState &State) {
  assert(State.VF.isVector() && "not widening");
  Function *CalledScalarFn = getCalledScalarFunction();
  assert(!isDbgInfoIntrinsic(CalledScalarFn->getIntrinsicID()) &&
         "DbgInfoIntrinsic should have been dropped during VPlan construction");
  State.setDebugLocFrom(getDebugLoc());

  bool UseIntrinsic = VectorIntrinsicID != Intrinsic::not_intrinsic;
  FunctionType *VFTy = nullptr;
  if (Variant)
    VFTy = Variant->getFunctionType();
  for (unsigned Part = 0; Part < State.UF; ++Part) {
    SmallVector<Type *, 2> TysForDecl;
    // Add return type if intrinsic is overloaded on it.
    if (UseIntrinsic &&
        isVectorIntrinsicWithOverloadTypeAtArg(VectorIntrinsicID, -1))
      TysForDecl.push_back(VectorType::get(
          CalledScalarFn->getReturnType()->getScalarType(), State.VF));
    SmallVector<Value *, 4> Args;
    for (const auto &I : enumerate(arg_operands())) {
      // Some intrinsics have a scalar argument - don't replace it with a
      // vector.
      Value *Arg;
      if (UseIntrinsic &&
          isVectorIntrinsicWithScalarOpAtArg(VectorIntrinsicID, I.index()))
        Arg = State.get(I.value(), VPIteration(0, 0));
      // Some vectorized function variants may also take a scalar argument,
      // e.g. linear parameters for pointers. This needs to be the scalar value
      // from the start of the respective part when interleaving.
      else if (VFTy && !VFTy->getParamType(I.index())->isVectorTy())
        Arg = State.get(I.value(), VPIteration(Part, 0));
      else
        Arg = State.get(I.value(), Part);
      if (UseIntrinsic &&
          isVectorIntrinsicWithOverloadTypeAtArg(VectorIntrinsicID, I.index()))
        TysForDecl.push_back(Arg->getType());
      Args.push_back(Arg);
    }

    Function *VectorF;
    if (UseIntrinsic) {
      // Use vector version of the intrinsic.
      Module *M = State.Builder.GetInsertBlock()->getModule();
      VectorF = Intrinsic::getDeclaration(M, VectorIntrinsicID, TysForDecl);
      assert(VectorF && "Can't retrieve vector intrinsic.");
    } else {
#ifndef NDEBUG
      assert(Variant != nullptr && "Can't create vector function.");
#endif
      VectorF = Variant;
    }

    auto *CI = cast_or_null<CallInst>(getUnderlyingInstr());
    SmallVector<OperandBundleDef, 1> OpBundles;
    if (CI)
      CI->getOperandBundlesAsDefs(OpBundles);

    CallInst *V = State.Builder.CreateCall(VectorF, Args, OpBundles);

    if (isa<FPMathOperator>(V))
      V->copyFastMathFlags(CI);

    if (!V->getType()->isVoidTy())
      State.set(this, V, Part);
    State.addMetadata(V, CI);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPWidenCallRecipe::print(raw_ostream &O, const Twine &Indent,
                              VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN-CALL ";

  Function *CalledFn = getCalledScalarFunction();
  if (CalledFn->getReturnType()->isVoidTy())
    O << "void ";
  else {
    printAsOperand(O, SlotTracker);
    O << " = ";
  }

  O << "call @" << CalledFn->getName() << "(";
  interleaveComma(arg_operands(), O, [&O, &SlotTracker](VPValue *Op) {
    Op->printAsOperand(O, SlotTracker);
  });
  O << ")";

  if (VectorIntrinsicID)
    O << " (using vector intrinsic)";
  else {
    O << " (using library function";
    if (Variant->hasName())
      O << ": " << Variant->getName();
    O << ")";
  }
}

void VPWidenSelectRecipe::print(raw_ostream &O, const Twine &Indent,
                                VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN-SELECT ";
  printAsOperand(O, SlotTracker);
  O << " = select ";
  getOperand(0)->printAsOperand(O, SlotTracker);
  O << ", ";
  getOperand(1)->printAsOperand(O, SlotTracker);
  O << ", ";
  getOperand(2)->printAsOperand(O, SlotTracker);
  O << (isInvariantCond() ? " (condition is loop invariant)" : "");
}
#endif

void VPWidenSelectRecipe::execute(VPTransformState &State) {
  State.setDebugLocFrom(getDebugLoc());

  // The condition can be loop invariant but still defined inside the
  // loop. This means that we can't just use the original 'cond' value.
  // We have to take the 'vectorized' value and pick the first lane.
  // Instcombine will make this a no-op.
  auto *InvarCond =
      isInvariantCond() ? State.get(getCond(), VPIteration(0, 0)) : nullptr;

  for (unsigned Part = 0; Part < State.UF; ++Part) {
    Value *Cond = InvarCond ? InvarCond : State.get(getCond(), Part);
    Value *Op0 = State.get(getOperand(1), Part);
    Value *Op1 = State.get(getOperand(2), Part);
    Value *Sel = State.Builder.CreateSelect(Cond, Op0, Op1);
    State.set(this, Sel, Part);
    State.addMetadata(Sel, dyn_cast_or_null<Instruction>(getUnderlyingValue()));
  }
}

VPRecipeWithIRFlags::FastMathFlagsTy::FastMathFlagsTy(
    const FastMathFlags &FMF) {
  AllowReassoc = FMF.allowReassoc();
  NoNaNs = FMF.noNaNs();
  NoInfs = FMF.noInfs();
  NoSignedZeros = FMF.noSignedZeros();
  AllowReciprocal = FMF.allowReciprocal();
  AllowContract = FMF.allowContract();
  ApproxFunc = FMF.approxFunc();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPRecipeWithIRFlags::printFlags(raw_ostream &O) const {
  switch (OpType) {
  case OperationType::Cmp:
    O << " " << CmpInst::getPredicateName(getPredicate());
    break;
  case OperationType::DisjointOp:
    if (DisjointFlags.IsDisjoint)
      O << " disjoint";
    break;
  case OperationType::PossiblyExactOp:
    if (ExactFlags.IsExact)
      O << " exact";
    break;
  case OperationType::OverflowingBinOp:
    if (WrapFlags.HasNUW)
      O << " nuw";
    if (WrapFlags.HasNSW)
      O << " nsw";
    break;
  case OperationType::FPMathOp:
    getFastMathFlags().print(O);
    break;
  case OperationType::GEPOp:
    if (GEPFlags.IsInBounds)
      O << " inbounds";
    break;
  case OperationType::NonNegOp:
    if (NonNegFlags.NonNeg)
      O << " nneg";
    break;
  case OperationType::Other:
    break;
  }
  if (getNumOperands() > 0)
    O << " ";
}
#endif

void VPWidenRecipe::execute(VPTransformState &State) {
  State.setDebugLocFrom(getDebugLoc());
  auto &Builder = State.Builder;
  switch (Opcode) {
  case Instruction::Call:
  case Instruction::Br:
  case Instruction::PHI:
  case Instruction::GetElementPtr:
  case Instruction::Select:
    llvm_unreachable("This instruction is handled by a different recipe.");
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::SRem:
  case Instruction::URem:
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::FNeg:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor: {
    // Just widen unops and binops.
    for (unsigned Part = 0; Part < State.UF; ++Part) {
      SmallVector<Value *, 2> Ops;
      for (VPValue *VPOp : operands())
        Ops.push_back(State.get(VPOp, Part));

      Value *V = Builder.CreateNAryOp(Opcode, Ops);

      if (auto *VecOp = dyn_cast<Instruction>(V))
        setFlags(VecOp);

      // Use this vector value for all users of the original instruction.
      State.set(this, V, Part);
      State.addMetadata(V, dyn_cast_or_null<Instruction>(getUnderlyingValue()));
    }

    break;
  }
  case Instruction::Freeze: {
    for (unsigned Part = 0; Part < State.UF; ++Part) {
      Value *Op = State.get(getOperand(0), Part);

      Value *Freeze = Builder.CreateFreeze(Op);
      State.set(this, Freeze, Part);
    }
    break;
  }
  case Instruction::ICmp:
  case Instruction::FCmp: {
    // Widen compares. Generate vector compares.
    bool FCmp = Opcode == Instruction::FCmp;
    for (unsigned Part = 0; Part < State.UF; ++Part) {
      Value *A = State.get(getOperand(0), Part);
      Value *B = State.get(getOperand(1), Part);
      Value *C = nullptr;
      if (FCmp) {
        // Propagate fast math flags.
        IRBuilder<>::FastMathFlagGuard FMFG(Builder);
        if (auto *I = dyn_cast_or_null<Instruction>(getUnderlyingValue()))
          Builder.setFastMathFlags(I->getFastMathFlags());
        C = Builder.CreateFCmp(getPredicate(), A, B);
      } else {
        C = Builder.CreateICmp(getPredicate(), A, B);
      }
      State.set(this, C, Part);
      State.addMetadata(C, dyn_cast_or_null<Instruction>(getUnderlyingValue()));
    }

    break;
  }
  default:
    // This instruction is not vectorized by simple widening.
    LLVM_DEBUG(dbgs() << "LV: Found an unhandled opcode : "
                      << Instruction::getOpcodeName(Opcode));
    llvm_unreachable("Unhandled instruction!");
  } // end of switch.

#if !defined(NDEBUG)
  // Verify that VPlan type inference results agree with the type of the
  // generated values.
  for (unsigned Part = 0; Part < State.UF; ++Part) {
    assert(VectorType::get(State.TypeAnalysis.inferScalarType(this),
                           State.VF) == State.get(this, Part)->getType() &&
           "inferred type and type from generated instructions do not match");
  }
#endif
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPWidenRecipe::print(raw_ostream &O, const Twine &Indent,
                          VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN ";
  printAsOperand(O, SlotTracker);
  O << " = " << Instruction::getOpcodeName(Opcode);
  printFlags(O);
  printOperands(O, SlotTracker);
}
#endif

void VPWidenCastRecipe::execute(VPTransformState &State) {
  State.setDebugLocFrom(getDebugLoc());
  auto &Builder = State.Builder;
  /// Vectorize casts.
  assert(State.VF.isVector() && "Not vectorizing?");
  Type *DestTy = VectorType::get(getResultType(), State.VF);
  VPValue *Op = getOperand(0);
  for (unsigned Part = 0; Part < State.UF; ++Part) {
    if (Part > 0 && Op->isLiveIn()) {
      // FIXME: Remove once explicit unrolling is implemented using VPlan.
      State.set(this, State.get(this, 0), Part);
      continue;
    }
    Value *A = State.get(Op, Part);
    Value *Cast = Builder.CreateCast(Instruction::CastOps(Opcode), A, DestTy);
    State.set(this, Cast, Part);
    State.addMetadata(Cast, cast_or_null<Instruction>(getUnderlyingValue()));
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPWidenCastRecipe::print(raw_ostream &O, const Twine &Indent,
                              VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN-CAST ";
  printAsOperand(O, SlotTracker);
  O << " = " << Instruction::getOpcodeName(Opcode) << " ";
  printFlags(O);
  printOperands(O, SlotTracker);
  O << " to " << *getResultType();
}
#endif

/// This function adds
/// (StartIdx * Step, (StartIdx + 1) * Step, (StartIdx + 2) * Step, ...)
/// to each vector element of Val. The sequence starts at StartIndex.
/// \p Opcode is relevant for FP induction variable.
static Value *getStepVector(Value *Val, Value *StartIdx, Value *Step,
                            Instruction::BinaryOps BinOp, ElementCount VF,
                            IRBuilderBase &Builder) {
  assert(VF.isVector() && "only vector VFs are supported");

  // Create and check the types.
  auto *ValVTy = cast<VectorType>(Val->getType());
  ElementCount VLen = ValVTy->getElementCount();

  Type *STy = Val->getType()->getScalarType();
  assert((STy->isIntegerTy() || STy->isFloatingPointTy()) &&
         "Induction Step must be an integer or FP");
  assert(Step->getType() == STy && "Step has wrong type");

  SmallVector<Constant *, 8> Indices;

  // Create a vector of consecutive numbers from zero to VF.
  VectorType *InitVecValVTy = ValVTy;
  if (STy->isFloatingPointTy()) {
    Type *InitVecValSTy =
        IntegerType::get(STy->getContext(), STy->getScalarSizeInBits());
    InitVecValVTy = VectorType::get(InitVecValSTy, VLen);
  }
  Value *InitVec = Builder.CreateStepVector(InitVecValVTy);

  // Splat the StartIdx
  Value *StartIdxSplat = Builder.CreateVectorSplat(VLen, StartIdx);

  if (STy->isIntegerTy()) {
    InitVec = Builder.CreateAdd(InitVec, StartIdxSplat);
    Step = Builder.CreateVectorSplat(VLen, Step);
    assert(Step->getType() == Val->getType() && "Invalid step vec");
    // FIXME: The newly created binary instructions should contain nsw/nuw
    // flags, which can be found from the original scalar operations.
    Step = Builder.CreateMul(InitVec, Step);
    return Builder.CreateAdd(Val, Step, "induction");
  }

  // Floating point induction.
  assert((BinOp == Instruction::FAdd || BinOp == Instruction::FSub) &&
         "Binary Opcode should be specified for FP induction");
  InitVec = Builder.CreateUIToFP(InitVec, ValVTy);
  InitVec = Builder.CreateFAdd(InitVec, StartIdxSplat);

  Step = Builder.CreateVectorSplat(VLen, Step);
  Value *MulOp = Builder.CreateFMul(InitVec, Step);
  return Builder.CreateBinOp(BinOp, Val, MulOp, "induction");
}

/// A helper function that returns an integer or floating-point constant with
/// value C.
static Constant *getSignedIntOrFpConstant(Type *Ty, int64_t C) {
  return Ty->isIntegerTy() ? ConstantInt::getSigned(Ty, C)
                           : ConstantFP::get(Ty, C);
}

static Value *getRuntimeVFAsFloat(IRBuilderBase &B, Type *FTy,
                                  ElementCount VF) {
  assert(FTy->isFloatingPointTy() && "Expected floating point type!");
  Type *IntTy = IntegerType::get(FTy->getContext(), FTy->getScalarSizeInBits());
  Value *RuntimeVF = getRuntimeVF(B, IntTy, VF);
  return B.CreateUIToFP(RuntimeVF, FTy);
}

void VPWidenIntOrFpInductionRecipe::execute(VPTransformState &State) {
  assert(!State.Instance && "Int or FP induction being replicated.");

  Value *Start = getStartValue()->getLiveInIRValue();
  const InductionDescriptor &ID = getInductionDescriptor();
  TruncInst *Trunc = getTruncInst();
  IRBuilderBase &Builder = State.Builder;
  assert(IV->getType() == ID.getStartValue()->getType() && "Types must match");
  assert(State.VF.isVector() && "must have vector VF");

  // The value from the original loop to which we are mapping the new induction
  // variable.
  Instruction *EntryVal = Trunc ? cast<Instruction>(Trunc) : IV;

  // Fast-math-flags propagate from the original induction instruction.
  IRBuilder<>::FastMathFlagGuard FMFG(Builder);
  if (ID.getInductionBinOp() && isa<FPMathOperator>(ID.getInductionBinOp()))
    Builder.setFastMathFlags(ID.getInductionBinOp()->getFastMathFlags());

  // Now do the actual transformations, and start with fetching the step value.
  Value *Step = State.get(getStepValue(), VPIteration(0, 0));

  assert((isa<PHINode>(EntryVal) || isa<TruncInst>(EntryVal)) &&
         "Expected either an induction phi-node or a truncate of it!");

  // Construct the initial value of the vector IV in the vector loop preheader
  auto CurrIP = Builder.saveIP();
  BasicBlock *VectorPH = State.CFG.getPreheaderBBFor(this);
  Builder.SetInsertPoint(VectorPH->getTerminator());
  if (isa<TruncInst>(EntryVal)) {
    assert(Start->getType()->isIntegerTy() &&
           "Truncation requires an integer type");
    auto *TruncType = cast<IntegerType>(EntryVal->getType());
    Step = Builder.CreateTrunc(Step, TruncType);
    Start = Builder.CreateCast(Instruction::Trunc, Start, TruncType);
  }

  Value *Zero = getSignedIntOrFpConstant(Start->getType(), 0);
  Value *SplatStart = Builder.CreateVectorSplat(State.VF, Start);
  Value *SteppedStart = getStepVector(
      SplatStart, Zero, Step, ID.getInductionOpcode(), State.VF, State.Builder);

  // We create vector phi nodes for both integer and floating-point induction
  // variables. Here, we determine the kind of arithmetic we will perform.
  Instruction::BinaryOps AddOp;
  Instruction::BinaryOps MulOp;
  if (Step->getType()->isIntegerTy()) {
    AddOp = Instruction::Add;
    MulOp = Instruction::Mul;
  } else {
    AddOp = ID.getInductionOpcode();
    MulOp = Instruction::FMul;
  }

  // Multiply the vectorization factor by the step using integer or
  // floating-point arithmetic as appropriate.
  Type *StepType = Step->getType();
  Value *RuntimeVF;
  if (Step->getType()->isFloatingPointTy())
    RuntimeVF = getRuntimeVFAsFloat(Builder, StepType, State.VF);
  else
    RuntimeVF = getRuntimeVF(Builder, StepType, State.VF);
  Value *Mul = Builder.CreateBinOp(MulOp, Step, RuntimeVF);

  // Create a vector splat to use in the induction update.
  //
  // FIXME: If the step is non-constant, we create the vector splat with
  //        IRBuilder. IRBuilder can constant-fold the multiply, but it doesn't
  //        handle a constant vector splat.
  Value *SplatVF = isa<Constant>(Mul)
                       ? ConstantVector::getSplat(State.VF, cast<Constant>(Mul))
                       : Builder.CreateVectorSplat(State.VF, Mul);
  Builder.restoreIP(CurrIP);

  // We may need to add the step a number of times, depending on the unroll
  // factor. The last of those goes into the PHI.
  PHINode *VecInd = PHINode::Create(SteppedStart->getType(), 2, "vec.ind");
  VecInd->insertBefore(State.CFG.PrevBB->getFirstInsertionPt());
  VecInd->setDebugLoc(EntryVal->getDebugLoc());
  Instruction *LastInduction = VecInd;
  for (unsigned Part = 0; Part < State.UF; ++Part) {
    State.set(this, LastInduction, Part);

    if (isa<TruncInst>(EntryVal))
      State.addMetadata(LastInduction, EntryVal);

    LastInduction = cast<Instruction>(
        Builder.CreateBinOp(AddOp, LastInduction, SplatVF, "step.add"));
    LastInduction->setDebugLoc(EntryVal->getDebugLoc());
  }

  LastInduction->setName("vec.ind.next");
  VecInd->addIncoming(SteppedStart, VectorPH);
  // Add induction update using an incorrect block temporarily. The phi node
  // will be fixed after VPlan execution. Note that at this point the latch
  // block cannot be used, as it does not exist yet.
  // TODO: Model increment value in VPlan, by turning the recipe into a
  // multi-def and a subclass of VPHeaderPHIRecipe.
  VecInd->addIncoming(LastInduction, VectorPH);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPWidenIntOrFpInductionRecipe::print(raw_ostream &O, const Twine &Indent,
                                          VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN-INDUCTION";
  if (getTruncInst()) {
    O << "\\l\"";
    O << " +\n" << Indent << "\"  " << VPlanIngredient(IV) << "\\l\"";
    O << " +\n" << Indent << "\"  ";
    getVPValue(0)->printAsOperand(O, SlotTracker);
  } else
    O << " " << VPlanIngredient(IV);

  O << ", ";
  getStepValue()->printAsOperand(O, SlotTracker);
}
#endif

bool VPWidenIntOrFpInductionRecipe::isCanonical() const {
  // The step may be defined by a recipe in the preheader (e.g. if it requires
  // SCEV expansion), but for the canonical induction the step is required to be
  // 1, which is represented as live-in.
  if (getStepValue()->getDefiningRecipe())
    return false;
  auto *StepC = dyn_cast<ConstantInt>(getStepValue()->getLiveInIRValue());
  auto *StartC = dyn_cast<ConstantInt>(getStartValue()->getLiveInIRValue());
  auto *CanIV = cast<VPCanonicalIVPHIRecipe>(&*getParent()->begin());
  return StartC && StartC->isZero() && StepC && StepC->isOne() &&
         getScalarType() == CanIV->getScalarType();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPDerivedIVRecipe::print(raw_ostream &O, const Twine &Indent,
                              VPSlotTracker &SlotTracker) const {
  O << Indent;
  printAsOperand(O, SlotTracker);
  O << Indent << "= DERIVED-IV ";
  getStartValue()->printAsOperand(O, SlotTracker);
  O << " + ";
  getOperand(1)->printAsOperand(O, SlotTracker);
  O << " * ";
  getStepValue()->printAsOperand(O, SlotTracker);
}
#endif

void VPScalarIVStepsRecipe::execute(VPTransformState &State) {
  // Fast-math-flags propagate from the original induction instruction.
  IRBuilder<>::FastMathFlagGuard FMFG(State.Builder);
  if (hasFastMathFlags())
    State.Builder.setFastMathFlags(getFastMathFlags());

  /// Compute scalar induction steps. \p ScalarIV is the scalar induction
  /// variable on which to base the steps, \p Step is the size of the step.

  Value *BaseIV = State.get(getOperand(0), VPIteration(0, 0));
  Value *Step = State.get(getStepValue(), VPIteration(0, 0));
  IRBuilderBase &Builder = State.Builder;

  // Ensure step has the same type as that of scalar IV.
  Type *BaseIVTy = BaseIV->getType()->getScalarType();
  assert(BaseIVTy == Step->getType() && "Types of BaseIV and Step must match!");

  // We build scalar steps for both integer and floating-point induction
  // variables. Here, we determine the kind of arithmetic we will perform.
  Instruction::BinaryOps AddOp;
  Instruction::BinaryOps MulOp;
  if (BaseIVTy->isIntegerTy()) {
    AddOp = Instruction::Add;
    MulOp = Instruction::Mul;
  } else {
    AddOp = InductionOpcode;
    MulOp = Instruction::FMul;
  }

  // Determine the number of scalars we need to generate for each unroll
  // iteration.
  bool FirstLaneOnly = vputils::onlyFirstLaneUsed(this);
  // Compute the scalar steps and save the results in State.
  Type *IntStepTy =
      IntegerType::get(BaseIVTy->getContext(), BaseIVTy->getScalarSizeInBits());
  Type *VecIVTy = nullptr;
  Value *UnitStepVec = nullptr, *SplatStep = nullptr, *SplatIV = nullptr;
  if (!FirstLaneOnly && State.VF.isScalable()) {
    VecIVTy = VectorType::get(BaseIVTy, State.VF);
    UnitStepVec =
        Builder.CreateStepVector(VectorType::get(IntStepTy, State.VF));
    SplatStep = Builder.CreateVectorSplat(State.VF, Step);
    SplatIV = Builder.CreateVectorSplat(State.VF, BaseIV);
  }

  unsigned StartPart = 0;
  unsigned EndPart = State.UF;
  unsigned StartLane = 0;
  unsigned EndLane = FirstLaneOnly ? 1 : State.VF.getKnownMinValue();
  if (State.Instance) {
    StartPart = State.Instance->Part;
    EndPart = StartPart + 1;
    StartLane = State.Instance->Lane.getKnownLane();
    EndLane = StartLane + 1;
  }
  for (unsigned Part = StartPart; Part < EndPart; ++Part) {
    Value *StartIdx0 = createStepForVF(Builder, IntStepTy, State.VF, Part);

    if (!FirstLaneOnly && State.VF.isScalable()) {
      auto *SplatStartIdx = Builder.CreateVectorSplat(State.VF, StartIdx0);
      auto *InitVec = Builder.CreateAdd(SplatStartIdx, UnitStepVec);
      if (BaseIVTy->isFloatingPointTy())
        InitVec = Builder.CreateSIToFP(InitVec, VecIVTy);
      auto *Mul = Builder.CreateBinOp(MulOp, InitVec, SplatStep);
      auto *Add = Builder.CreateBinOp(AddOp, SplatIV, Mul);
      State.set(this, Add, Part);
      // It's useful to record the lane values too for the known minimum number
      // of elements so we do those below. This improves the code quality when
      // trying to extract the first element, for example.
    }

    if (BaseIVTy->isFloatingPointTy())
      StartIdx0 = Builder.CreateSIToFP(StartIdx0, BaseIVTy);

    for (unsigned Lane = StartLane; Lane < EndLane; ++Lane) {
      Value *StartIdx = Builder.CreateBinOp(
          AddOp, StartIdx0, getSignedIntOrFpConstant(BaseIVTy, Lane));
      // The step returned by `createStepForVF` is a runtime-evaluated value
      // when VF is scalable. Otherwise, it should be folded into a Constant.
      assert((State.VF.isScalable() || isa<Constant>(StartIdx)) &&
             "Expected StartIdx to be folded to a constant when VF is not "
             "scalable");
      auto *Mul = Builder.CreateBinOp(MulOp, StartIdx, Step);
      auto *Add = Builder.CreateBinOp(AddOp, BaseIV, Mul);
      State.set(this, Add, VPIteration(Part, Lane));
    }
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPScalarIVStepsRecipe::print(raw_ostream &O, const Twine &Indent,
                                  VPSlotTracker &SlotTracker) const {
  O << Indent;
  printAsOperand(O, SlotTracker);
  O << " = SCALAR-STEPS ";
  printOperands(O, SlotTracker);
}
#endif

void VPWidenGEPRecipe::execute(VPTransformState &State) {
  assert(State.VF.isVector() && "not widening");
  auto *GEP = cast<GetElementPtrInst>(getUnderlyingInstr());
  // Construct a vector GEP by widening the operands of the scalar GEP as
  // necessary. We mark the vector GEP 'inbounds' if appropriate. A GEP
  // results in a vector of pointers when at least one operand of the GEP
  // is vector-typed. Thus, to keep the representation compact, we only use
  // vector-typed operands for loop-varying values.

  if (areAllOperandsInvariant()) {
    // If we are vectorizing, but the GEP has only loop-invariant operands,
    // the GEP we build (by only using vector-typed operands for
    // loop-varying values) would be a scalar pointer. Thus, to ensure we
    // produce a vector of pointers, we need to either arbitrarily pick an
    // operand to broadcast, or broadcast a clone of the original GEP.
    // Here, we broadcast a clone of the original.
    //
    // TODO: If at some point we decide to scalarize instructions having
    //       loop-invariant operands, this special case will no longer be
    //       required. We would add the scalarization decision to
    //       collectLoopScalars() and teach getVectorValue() to broadcast
    //       the lane-zero scalar value.
    SmallVector<Value *> Ops;
    for (unsigned I = 0, E = getNumOperands(); I != E; I++)
      Ops.push_back(State.get(getOperand(I), VPIteration(0, 0)));

    auto *NewGEP =
        State.Builder.CreateGEP(GEP->getSourceElementType(), Ops[0],
                                ArrayRef(Ops).drop_front(), "", isInBounds());
    for (unsigned Part = 0; Part < State.UF; ++Part) {
      Value *EntryPart = State.Builder.CreateVectorSplat(State.VF, NewGEP);
      State.set(this, EntryPart, Part);
      State.addMetadata(EntryPart, GEP);
    }
  } else {
    // If the GEP has at least one loop-varying operand, we are sure to
    // produce a vector of pointers. But if we are only unrolling, we want
    // to produce a scalar GEP for each unroll part. Thus, the GEP we
    // produce with the code below will be scalar (if VF == 1) or vector
    // (otherwise). Note that for the unroll-only case, we still maintain
    // values in the vector mapping with initVector, as we do for other
    // instructions.
    for (unsigned Part = 0; Part < State.UF; ++Part) {
      // The pointer operand of the new GEP. If it's loop-invariant, we
      // won't broadcast it.
      auto *Ptr = isPointerLoopInvariant()
                      ? State.get(getOperand(0), VPIteration(0, 0))
                      : State.get(getOperand(0), Part);

      // Collect all the indices for the new GEP. If any index is
      // loop-invariant, we won't broadcast it.
      SmallVector<Value *, 4> Indices;
      for (unsigned I = 1, E = getNumOperands(); I < E; I++) {
        VPValue *Operand = getOperand(I);
        if (isIndexLoopInvariant(I - 1))
          Indices.push_back(State.get(Operand, VPIteration(0, 0)));
        else
          Indices.push_back(State.get(Operand, Part));
      }

      // Create the new GEP. Note that this GEP may be a scalar if VF == 1,
      // but it should be a vector, otherwise.
      auto *NewGEP = State.Builder.CreateGEP(GEP->getSourceElementType(), Ptr,
                                             Indices, "", isInBounds());
      assert((State.VF.isScalar() || NewGEP->getType()->isVectorTy()) &&
             "NewGEP is not a pointer vector");
      State.set(this, NewGEP, Part);
      State.addMetadata(NewGEP, GEP);
    }
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPWidenGEPRecipe::print(raw_ostream &O, const Twine &Indent,
                             VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN-GEP ";
  O << (isPointerLoopInvariant() ? "Inv" : "Var");
  for (size_t I = 0; I < getNumOperands() - 1; ++I)
    O << "[" << (isIndexLoopInvariant(I) ? "Inv" : "Var") << "]";

  O << " ";
  printAsOperand(O, SlotTracker);
  O << " = getelementptr";
  printFlags(O);
  printOperands(O, SlotTracker);
}
#endif

void VPVectorPointerRecipe ::execute(VPTransformState &State) {
  auto &Builder = State.Builder;
  State.setDebugLocFrom(getDebugLoc());
  for (unsigned Part = 0; Part < State.UF; ++Part) {
    // Calculate the pointer for the specific unroll-part.
    Value *PartPtr = nullptr;
    // Use i32 for the gep index type when the value is constant,
    // or query DataLayout for a more suitable index type otherwise.
    const DataLayout &DL =
        Builder.GetInsertBlock()->getDataLayout();
    Type *IndexTy = State.VF.isScalable() && (IsReverse || Part > 0)
                        ? DL.getIndexType(IndexedTy->getPointerTo())
                        : Builder.getInt32Ty();
    Value *Ptr = State.get(getOperand(0), VPIteration(0, 0));
    bool InBounds = isInBounds();
    if (IsReverse) {
      // If the address is consecutive but reversed, then the
      // wide store needs to start at the last vector element.
      // RunTimeVF =  VScale * VF.getKnownMinValue()
      // For fixed-width VScale is 1, then RunTimeVF = VF.getKnownMinValue()
      Value *RunTimeVF = getRuntimeVF(Builder, IndexTy, State.VF);
      // NumElt = -Part * RunTimeVF
      Value *NumElt = Builder.CreateMul(
          ConstantInt::get(IndexTy, -(int64_t)Part), RunTimeVF);
      // LastLane = 1 - RunTimeVF
      Value *LastLane =
          Builder.CreateSub(ConstantInt::get(IndexTy, 1), RunTimeVF);
      PartPtr = Builder.CreateGEP(IndexedTy, Ptr, NumElt, "", InBounds);
      PartPtr = Builder.CreateGEP(IndexedTy, PartPtr, LastLane, "", InBounds);
    } else {
      Value *Increment = createStepForVF(Builder, IndexTy, State.VF, Part);
      PartPtr = Builder.CreateGEP(IndexedTy, Ptr, Increment, "", InBounds);
    }

    State.set(this, PartPtr, Part, /*IsScalar*/ true);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPVectorPointerRecipe::print(raw_ostream &O, const Twine &Indent,
                                  VPSlotTracker &SlotTracker) const {
  O << Indent;
  printAsOperand(O, SlotTracker);
  O << " = vector-pointer ";
  if (IsReverse)
    O << "(reverse) ";

  printOperands(O, SlotTracker);
}
#endif

void VPBlendRecipe::execute(VPTransformState &State) {
  State.setDebugLocFrom(getDebugLoc());
  // We know that all PHIs in non-header blocks are converted into
  // selects, so we don't have to worry about the insertion order and we
  // can just use the builder.
  // At this point we generate the predication tree. There may be
  // duplications since this is a simple recursive scan, but future
  // optimizations will clean it up.

  unsigned NumIncoming = getNumIncomingValues();

  // Generate a sequence of selects of the form:
  // SELECT(Mask3, In3,
  //        SELECT(Mask2, In2,
  //               SELECT(Mask1, In1,
  //                      In0)))
  // Note that Mask0 is never used: lanes for which no path reaches this phi and
  // are essentially undef are taken from In0.
 VectorParts Entry(State.UF);
 bool OnlyFirstLaneUsed = vputils::onlyFirstLaneUsed(this);
 for (unsigned In = 0; In < NumIncoming; ++In) {
   for (unsigned Part = 0; Part < State.UF; ++Part) {
     // We might have single edge PHIs (blocks) - use an identity
     // 'select' for the first PHI operand.
     Value *In0 = State.get(getIncomingValue(In), Part, OnlyFirstLaneUsed);
     if (In == 0)
       Entry[Part] = In0; // Initialize with the first incoming value.
     else {
       // Select between the current value and the previous incoming edge
       // based on the incoming mask.
       Value *Cond = State.get(getMask(In), Part, OnlyFirstLaneUsed);
       Entry[Part] =
           State.Builder.CreateSelect(Cond, In0, Entry[Part], "predphi");
     }
   }
 }
  for (unsigned Part = 0; Part < State.UF; ++Part)
    State.set(this, Entry[Part], Part, OnlyFirstLaneUsed);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPBlendRecipe::print(raw_ostream &O, const Twine &Indent,
                          VPSlotTracker &SlotTracker) const {
  O << Indent << "BLEND ";
  printAsOperand(O, SlotTracker);
  O << " =";
  if (getNumIncomingValues() == 1) {
    // Not a User of any mask: not really blending, this is a
    // single-predecessor phi.
    O << " ";
    getIncomingValue(0)->printAsOperand(O, SlotTracker);
  } else {
    for (unsigned I = 0, E = getNumIncomingValues(); I < E; ++I) {
      O << " ";
      getIncomingValue(I)->printAsOperand(O, SlotTracker);
      if (I == 0)
        continue;
      O << "/";
      getMask(I)->printAsOperand(O, SlotTracker);
    }
  }
}
#endif

void VPReductionRecipe::execute(VPTransformState &State) {
  assert(!State.Instance && "Reduction being replicated.");
  Value *PrevInChain = State.get(getChainOp(), 0, /*IsScalar*/ true);
  RecurKind Kind = RdxDesc.getRecurrenceKind();
  // Propagate the fast-math flags carried by the underlying instruction.
  IRBuilderBase::FastMathFlagGuard FMFGuard(State.Builder);
  State.Builder.setFastMathFlags(RdxDesc.getFastMathFlags());
  for (unsigned Part = 0; Part < State.UF; ++Part) {
    Value *NewVecOp = State.get(getVecOp(), Part);
    if (VPValue *Cond = getCondOp()) {
      Value *NewCond = State.get(Cond, Part, State.VF.isScalar());
      VectorType *VecTy = dyn_cast<VectorType>(NewVecOp->getType());
      Type *ElementTy = VecTy ? VecTy->getElementType() : NewVecOp->getType();
      Value *Iden = RdxDesc.getRecurrenceIdentity(Kind, ElementTy,
                                                  RdxDesc.getFastMathFlags());
      if (State.VF.isVector()) {
        Iden = State.Builder.CreateVectorSplat(VecTy->getElementCount(), Iden);
      }

      Value *Select = State.Builder.CreateSelect(NewCond, NewVecOp, Iden);
      NewVecOp = Select;
    }
    Value *NewRed;
    Value *NextInChain;
    if (IsOrdered) {
      if (State.VF.isVector())
        NewRed = createOrderedReduction(State.Builder, RdxDesc, NewVecOp,
                                        PrevInChain);
      else
        NewRed = State.Builder.CreateBinOp(
            (Instruction::BinaryOps)RdxDesc.getOpcode(Kind), PrevInChain,
            NewVecOp);
      PrevInChain = NewRed;
    } else {
      PrevInChain = State.get(getChainOp(), Part, /*IsScalar*/ true);
      NewRed = createTargetReduction(State.Builder, RdxDesc, NewVecOp);
    }
    if (RecurrenceDescriptor::isMinMaxRecurrenceKind(Kind)) {
      NextInChain = createMinMaxOp(State.Builder, RdxDesc.getRecurrenceKind(),
                                   NewRed, PrevInChain);
    } else if (IsOrdered)
      NextInChain = NewRed;
    else
      NextInChain = State.Builder.CreateBinOp(
          (Instruction::BinaryOps)RdxDesc.getOpcode(Kind), NewRed, PrevInChain);
    State.set(this, NextInChain, Part, /*IsScalar*/ true);
  }
}

void VPReductionEVLRecipe::execute(VPTransformState &State) {
  assert(!State.Instance && "Reduction being replicated.");
  assert(State.UF == 1 &&
         "Expected only UF == 1 when vectorizing with explicit vector length.");

  auto &Builder = State.Builder;
  // Propagate the fast-math flags carried by the underlying instruction.
  IRBuilderBase::FastMathFlagGuard FMFGuard(Builder);
  const RecurrenceDescriptor &RdxDesc = getRecurrenceDescriptor();
  Builder.setFastMathFlags(RdxDesc.getFastMathFlags());

  RecurKind Kind = RdxDesc.getRecurrenceKind();
  Value *Prev = State.get(getChainOp(), 0, /*IsScalar*/ true);
  Value *VecOp = State.get(getVecOp(), 0);
  Value *EVL = State.get(getEVL(), VPIteration(0, 0));

  VectorBuilder VBuilder(Builder);
  VBuilder.setEVL(EVL);
  Value *Mask;
  // TODO: move the all-true mask generation into VectorBuilder.
  if (VPValue *CondOp = getCondOp())
    Mask = State.get(CondOp, 0);
  else
    Mask = Builder.CreateVectorSplat(State.VF, Builder.getTrue());
  VBuilder.setMask(Mask);

  Value *NewRed;
  if (isOrdered()) {
    NewRed = createOrderedReduction(VBuilder, RdxDesc, VecOp, Prev);
  } else {
    NewRed = createSimpleTargetReduction(VBuilder, VecOp, RdxDesc);
    if (RecurrenceDescriptor::isMinMaxRecurrenceKind(Kind))
      NewRed = createMinMaxOp(Builder, Kind, NewRed, Prev);
    else
      NewRed = Builder.CreateBinOp(
          (Instruction::BinaryOps)RdxDesc.getOpcode(Kind), NewRed, Prev);
  }
  State.set(this, NewRed, 0, /*IsScalar*/ true);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPReductionRecipe::print(raw_ostream &O, const Twine &Indent,
                              VPSlotTracker &SlotTracker) const {
  O << Indent << "REDUCE ";
  printAsOperand(O, SlotTracker);
  O << " = ";
  getChainOp()->printAsOperand(O, SlotTracker);
  O << " +";
  if (isa<FPMathOperator>(getUnderlyingInstr()))
    O << getUnderlyingInstr()->getFastMathFlags();
  O << " reduce." << Instruction::getOpcodeName(RdxDesc.getOpcode()) << " (";
  getVecOp()->printAsOperand(O, SlotTracker);
  if (isConditional()) {
    O << ", ";
    getCondOp()->printAsOperand(O, SlotTracker);
  }
  O << ")";
  if (RdxDesc.IntermediateStore)
    O << " (with final reduction value stored in invariant address sank "
         "outside of loop)";
}

void VPReductionEVLRecipe::print(raw_ostream &O, const Twine &Indent,
                                 VPSlotTracker &SlotTracker) const {
  const RecurrenceDescriptor &RdxDesc = getRecurrenceDescriptor();
  O << Indent << "REDUCE ";
  printAsOperand(O, SlotTracker);
  O << " = ";
  getChainOp()->printAsOperand(O, SlotTracker);
  O << " +";
  if (isa<FPMathOperator>(getUnderlyingInstr()))
    O << getUnderlyingInstr()->getFastMathFlags();
  O << " vp.reduce." << Instruction::getOpcodeName(RdxDesc.getOpcode()) << " (";
  getVecOp()->printAsOperand(O, SlotTracker);
  O << ", ";
  getEVL()->printAsOperand(O, SlotTracker);
  if (isConditional()) {
    O << ", ";
    getCondOp()->printAsOperand(O, SlotTracker);
  }
  O << ")";
  if (RdxDesc.IntermediateStore)
    O << " (with final reduction value stored in invariant address sank "
         "outside of loop)";
}
#endif

bool VPReplicateRecipe::shouldPack() const {
  // Find if the recipe is used by a widened recipe via an intervening
  // VPPredInstPHIRecipe. In this case, also pack the scalar values in a vector.
  return any_of(users(), [](const VPUser *U) {
    if (auto *PredR = dyn_cast<VPPredInstPHIRecipe>(U))
      return any_of(PredR->users(), [PredR](const VPUser *U) {
        return !U->usesScalars(PredR);
      });
    return false;
  });
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPReplicateRecipe::print(raw_ostream &O, const Twine &Indent,
                              VPSlotTracker &SlotTracker) const {
  O << Indent << (IsUniform ? "CLONE " : "REPLICATE ");

  if (!getUnderlyingInstr()->getType()->isVoidTy()) {
    printAsOperand(O, SlotTracker);
    O << " = ";
  }
  if (auto *CB = dyn_cast<CallBase>(getUnderlyingInstr())) {
    O << "call";
    printFlags(O);
    O << "@" << CB->getCalledFunction()->getName() << "(";
    interleaveComma(make_range(op_begin(), op_begin() + (getNumOperands() - 1)),
                    O, [&O, &SlotTracker](VPValue *Op) {
                      Op->printAsOperand(O, SlotTracker);
                    });
    O << ")";
  } else {
    O << Instruction::getOpcodeName(getUnderlyingInstr()->getOpcode());
    printFlags(O);
    printOperands(O, SlotTracker);
  }

  if (shouldPack())
    O << " (S->V)";
}
#endif

/// Checks if \p C is uniform across all VFs and UFs. It is considered as such
/// if it is either defined outside the vector region or its operand is known to
/// be uniform across all VFs and UFs (e.g. VPDerivedIV or VPCanonicalIVPHI).
/// TODO: Uniformity should be associated with a VPValue and there should be a
/// generic way to check.
static bool isUniformAcrossVFsAndUFs(VPScalarCastRecipe *C) {
  return C->isDefinedOutsideVectorRegions() ||
         isa<VPDerivedIVRecipe>(C->getOperand(0)) ||
         isa<VPCanonicalIVPHIRecipe>(C->getOperand(0));
}

Value *VPScalarCastRecipe ::generate(VPTransformState &State, unsigned Part) {
  assert(vputils::onlyFirstLaneUsed(this) &&
         "Codegen only implemented for first lane.");
  switch (Opcode) {
  case Instruction::SExt:
  case Instruction::ZExt:
  case Instruction::Trunc: {
    // Note: SExt/ZExt not used yet.
    Value *Op = State.get(getOperand(0), VPIteration(Part, 0));
    return State.Builder.CreateCast(Instruction::CastOps(Opcode), Op, ResultTy);
  }
  default:
    llvm_unreachable("opcode not implemented yet");
  }
}

void VPScalarCastRecipe ::execute(VPTransformState &State) {
  bool IsUniformAcrossVFsAndUFs = isUniformAcrossVFsAndUFs(this);
  for (unsigned Part = 0; Part != State.UF; ++Part) {
    Value *Res;
    // Only generate a single instance, if the recipe is uniform across UFs and
    // VFs.
    if (Part > 0 && IsUniformAcrossVFsAndUFs)
      Res = State.get(this, VPIteration(0, 0));
    else
      Res = generate(State, Part);
    State.set(this, Res, VPIteration(Part, 0));
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPScalarCastRecipe ::print(raw_ostream &O, const Twine &Indent,
                                VPSlotTracker &SlotTracker) const {
  O << Indent << "SCALAR-CAST ";
  printAsOperand(O, SlotTracker);
  O << " = " << Instruction::getOpcodeName(Opcode) << " ";
  printOperands(O, SlotTracker);
  O << " to " << *ResultTy;
}
#endif

void VPBranchOnMaskRecipe::execute(VPTransformState &State) {
  assert(State.Instance && "Branch on Mask works only on single instance.");

  unsigned Part = State.Instance->Part;
  unsigned Lane = State.Instance->Lane.getKnownLane();

  Value *ConditionBit = nullptr;
  VPValue *BlockInMask = getMask();
  if (BlockInMask) {
    ConditionBit = State.get(BlockInMask, Part);
    if (ConditionBit->getType()->isVectorTy())
      ConditionBit = State.Builder.CreateExtractElement(
          ConditionBit, State.Builder.getInt32(Lane));
  } else // Block in mask is all-one.
    ConditionBit = State.Builder.getTrue();

  // Replace the temporary unreachable terminator with a new conditional branch,
  // whose two destinations will be set later when they are created.
  auto *CurrentTerminator = State.CFG.PrevBB->getTerminator();
  assert(isa<UnreachableInst>(CurrentTerminator) &&
         "Expected to replace unreachable terminator with conditional branch.");
  auto *CondBr = BranchInst::Create(State.CFG.PrevBB, nullptr, ConditionBit);
  CondBr->setSuccessor(0, nullptr);
  ReplaceInstWithInst(CurrentTerminator, CondBr);
}

void VPPredInstPHIRecipe::execute(VPTransformState &State) {
  assert(State.Instance && "Predicated instruction PHI works per instance.");
  Instruction *ScalarPredInst =
      cast<Instruction>(State.get(getOperand(0), *State.Instance));
  BasicBlock *PredicatedBB = ScalarPredInst->getParent();
  BasicBlock *PredicatingBB = PredicatedBB->getSinglePredecessor();
  assert(PredicatingBB && "Predicated block has no single predecessor.");
  assert(isa<VPReplicateRecipe>(getOperand(0)) &&
         "operand must be VPReplicateRecipe");

  // By current pack/unpack logic we need to generate only a single phi node: if
  // a vector value for the predicated instruction exists at this point it means
  // the instruction has vector users only, and a phi for the vector value is
  // needed. In this case the recipe of the predicated instruction is marked to
  // also do that packing, thereby "hoisting" the insert-element sequence.
  // Otherwise, a phi node for the scalar value is needed.
  unsigned Part = State.Instance->Part;
  if (State.hasVectorValue(getOperand(0), Part)) {
    Value *VectorValue = State.get(getOperand(0), Part);
    InsertElementInst *IEI = cast<InsertElementInst>(VectorValue);
    PHINode *VPhi = State.Builder.CreatePHI(IEI->getType(), 2);
    VPhi->addIncoming(IEI->getOperand(0), PredicatingBB); // Unmodified vector.
    VPhi->addIncoming(IEI, PredicatedBB); // New vector with inserted element.
    if (State.hasVectorValue(this, Part))
      State.reset(this, VPhi, Part);
    else
      State.set(this, VPhi, Part);
    // NOTE: Currently we need to update the value of the operand, so the next
    // predicated iteration inserts its generated value in the correct vector.
    State.reset(getOperand(0), VPhi, Part);
  } else {
    Type *PredInstType = getOperand(0)->getUnderlyingValue()->getType();
    PHINode *Phi = State.Builder.CreatePHI(PredInstType, 2);
    Phi->addIncoming(PoisonValue::get(ScalarPredInst->getType()),
                     PredicatingBB);
    Phi->addIncoming(ScalarPredInst, PredicatedBB);
    if (State.hasScalarValue(this, *State.Instance))
      State.reset(this, Phi, *State.Instance);
    else
      State.set(this, Phi, *State.Instance);
    // NOTE: Currently we need to update the value of the operand, so the next
    // predicated iteration inserts its generated value in the correct vector.
    State.reset(getOperand(0), Phi, *State.Instance);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPPredInstPHIRecipe::print(raw_ostream &O, const Twine &Indent,
                                VPSlotTracker &SlotTracker) const {
  O << Indent << "PHI-PREDICATED-INSTRUCTION ";
  printAsOperand(O, SlotTracker);
  O << " = ";
  printOperands(O, SlotTracker);
}

void VPWidenLoadRecipe::print(raw_ostream &O, const Twine &Indent,
                              VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN ";
  printAsOperand(O, SlotTracker);
  O << " = load ";
  printOperands(O, SlotTracker);
}

void VPWidenLoadEVLRecipe::print(raw_ostream &O, const Twine &Indent,
                                 VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN ";
  printAsOperand(O, SlotTracker);
  O << " = vp.load ";
  printOperands(O, SlotTracker);
}

void VPWidenStoreRecipe::print(raw_ostream &O, const Twine &Indent,
                               VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN store ";
  printOperands(O, SlotTracker);
}

void VPWidenStoreEVLRecipe::print(raw_ostream &O, const Twine &Indent,
                                  VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN vp.store ";
  printOperands(O, SlotTracker);
}
#endif

static Value *createBitOrPointerCast(IRBuilderBase &Builder, Value *V,
                                     VectorType *DstVTy, const DataLayout &DL) {
  // Verify that V is a vector type with same number of elements as DstVTy.
  auto VF = DstVTy->getElementCount();
  auto *SrcVecTy = cast<VectorType>(V->getType());
  assert(VF == SrcVecTy->getElementCount() && "Vector dimensions do not match");
  Type *SrcElemTy = SrcVecTy->getElementType();
  Type *DstElemTy = DstVTy->getElementType();
  assert((DL.getTypeSizeInBits(SrcElemTy) == DL.getTypeSizeInBits(DstElemTy)) &&
         "Vector elements must have same size");

  // Do a direct cast if element types are castable.
  if (CastInst::isBitOrNoopPointerCastable(SrcElemTy, DstElemTy, DL)) {
    return Builder.CreateBitOrPointerCast(V, DstVTy);
  }
  // V cannot be directly casted to desired vector type.
  // May happen when V is a floating point vector but DstVTy is a vector of
  // pointers or vice-versa. Handle this using a two-step bitcast using an
  // intermediate Integer type for the bitcast i.e. Ptr <-> Int <-> Float.
  assert((DstElemTy->isPointerTy() != SrcElemTy->isPointerTy()) &&
         "Only one type should be a pointer type");
  assert((DstElemTy->isFloatingPointTy() != SrcElemTy->isFloatingPointTy()) &&
         "Only one type should be a floating point type");
  Type *IntTy =
      IntegerType::getIntNTy(V->getContext(), DL.getTypeSizeInBits(SrcElemTy));
  auto *VecIntTy = VectorType::get(IntTy, VF);
  Value *CastVal = Builder.CreateBitOrPointerCast(V, VecIntTy);
  return Builder.CreateBitOrPointerCast(CastVal, DstVTy);
}

/// Return a vector containing interleaved elements from multiple
/// smaller input vectors.
static Value *interleaveVectors(IRBuilderBase &Builder, ArrayRef<Value *> Vals,
                                const Twine &Name) {
  unsigned Factor = Vals.size();
  assert(Factor > 1 && "Tried to interleave invalid number of vectors");

  VectorType *VecTy = cast<VectorType>(Vals[0]->getType());
#ifndef NDEBUG
  for (Value *Val : Vals)
    assert(Val->getType() == VecTy && "Tried to interleave mismatched types");
#endif

  // Scalable vectors cannot use arbitrary shufflevectors (only splats), so
  // must use intrinsics to interleave.
  if (VecTy->isScalableTy()) {
    VectorType *WideVecTy = VectorType::getDoubleElementsVectorType(VecTy);
    return Builder.CreateIntrinsic(WideVecTy, Intrinsic::vector_interleave2,
                                   Vals,
                                   /*FMFSource=*/nullptr, Name);
  }

  // Fixed length. Start by concatenating all vectors into a wide vector.
  Value *WideVec = concatenateVectors(Builder, Vals);

  // Interleave the elements into the wide vector.
  const unsigned NumElts = VecTy->getElementCount().getFixedValue();
  return Builder.CreateShuffleVector(
      WideVec, createInterleaveMask(NumElts, Factor), Name);
}

// Try to vectorize the interleave group that \p Instr belongs to.
//
// E.g. Translate following interleaved load group (factor = 3):
//   for (i = 0; i < N; i+=3) {
//     R = Pic[i];             // Member of index 0
//     G = Pic[i+1];           // Member of index 1
//     B = Pic[i+2];           // Member of index 2
//     ... // do something to R, G, B
//   }
// To:
//   %wide.vec = load <12 x i32>                       ; Read 4 tuples of R,G,B
//   %R.vec = shuffle %wide.vec, poison, <0, 3, 6, 9>   ; R elements
//   %G.vec = shuffle %wide.vec, poison, <1, 4, 7, 10>  ; G elements
//   %B.vec = shuffle %wide.vec, poison, <2, 5, 8, 11>  ; B elements
//
// Or translate following interleaved store group (factor = 3):
//   for (i = 0; i < N; i+=3) {
//     ... do something to R, G, B
//     Pic[i]   = R;           // Member of index 0
//     Pic[i+1] = G;           // Member of index 1
//     Pic[i+2] = B;           // Member of index 2
//   }
// To:
//   %R_G.vec = shuffle %R.vec, %G.vec, <0, 1, 2, ..., 7>
//   %B_U.vec = shuffle %B.vec, poison, <0, 1, 2, 3, u, u, u, u>
//   %interleaved.vec = shuffle %R_G.vec, %B_U.vec,
//        <0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11>    ; Interleave R,G,B elements
//   store <12 x i32> %interleaved.vec              ; Write 4 tuples of R,G,B
void VPInterleaveRecipe::execute(VPTransformState &State) {
  assert(!State.Instance && "Interleave group being replicated.");
  const InterleaveGroup<Instruction> *Group = IG;
  Instruction *Instr = Group->getInsertPos();

  // Prepare for the vector type of the interleaved load/store.
  Type *ScalarTy = getLoadStoreType(Instr);
  unsigned InterleaveFactor = Group->getFactor();
  auto *VecTy = VectorType::get(ScalarTy, State.VF * InterleaveFactor);

  // Prepare for the new pointers.
  SmallVector<Value *, 2> AddrParts;
  unsigned Index = Group->getIndex(Instr);

  // TODO: extend the masked interleaved-group support to reversed access.
  VPValue *BlockInMask = getMask();
  assert((!BlockInMask || !Group->isReverse()) &&
         "Reversed masked interleave-group not supported.");

  Value *Idx;
  // If the group is reverse, adjust the index to refer to the last vector lane
  // instead of the first. We adjust the index from the first vector lane,
  // rather than directly getting the pointer for lane VF - 1, because the
  // pointer operand of the interleaved access is supposed to be uniform. For
  // uniform instructions, we're only required to generate a value for the
  // first vector lane in each unroll iteration.
  if (Group->isReverse()) {
    Value *RuntimeVF =
        getRuntimeVF(State.Builder, State.Builder.getInt32Ty(), State.VF);
    Idx = State.Builder.CreateSub(RuntimeVF, State.Builder.getInt32(1));
    Idx = State.Builder.CreateMul(Idx,
                                  State.Builder.getInt32(Group->getFactor()));
    Idx = State.Builder.CreateAdd(Idx, State.Builder.getInt32(Index));
    Idx = State.Builder.CreateNeg(Idx);
  } else
    Idx = State.Builder.getInt32(-Index);

  VPValue *Addr = getAddr();
  for (unsigned Part = 0; Part < State.UF; Part++) {
    Value *AddrPart = State.get(Addr, VPIteration(Part, 0));
    if (auto *I = dyn_cast<Instruction>(AddrPart))
      State.setDebugLocFrom(I->getDebugLoc());

    // Notice current instruction could be any index. Need to adjust the address
    // to the member of index 0.
    //
    // E.g.  a = A[i+1];     // Member of index 1 (Current instruction)
    //       b = A[i];       // Member of index 0
    // Current pointer is pointed to A[i+1], adjust it to A[i].
    //
    // E.g.  A[i+1] = a;     // Member of index 1
    //       A[i]   = b;     // Member of index 0
    //       A[i+2] = c;     // Member of index 2 (Current instruction)
    // Current pointer is pointed to A[i+2], adjust it to A[i].

    bool InBounds = false;
    if (auto *gep = dyn_cast<GetElementPtrInst>(AddrPart->stripPointerCasts()))
      InBounds = gep->isInBounds();
    AddrPart = State.Builder.CreateGEP(ScalarTy, AddrPart, Idx, "", InBounds);
    AddrParts.push_back(AddrPart);
  }

  State.setDebugLocFrom(Instr->getDebugLoc());
  Value *PoisonVec = PoisonValue::get(VecTy);

  auto CreateGroupMask = [&BlockInMask, &State, &InterleaveFactor](
                             unsigned Part, Value *MaskForGaps) -> Value * {
    if (State.VF.isScalable()) {
      assert(!MaskForGaps && "Interleaved groups with gaps are not supported.");
      assert(InterleaveFactor == 2 &&
             "Unsupported deinterleave factor for scalable vectors");
      auto *BlockInMaskPart = State.get(BlockInMask, Part);
      SmallVector<Value *, 2> Ops = {BlockInMaskPart, BlockInMaskPart};
      auto *MaskTy = VectorType::get(State.Builder.getInt1Ty(),
                                     State.VF.getKnownMinValue() * 2, true);
      return State.Builder.CreateIntrinsic(
          MaskTy, Intrinsic::vector_interleave2, Ops,
          /*FMFSource=*/nullptr, "interleaved.mask");
    }

    if (!BlockInMask)
      return MaskForGaps;

    Value *BlockInMaskPart = State.get(BlockInMask, Part);
    Value *ShuffledMask = State.Builder.CreateShuffleVector(
        BlockInMaskPart,
        createReplicatedMask(InterleaveFactor, State.VF.getKnownMinValue()),
        "interleaved.mask");
    return MaskForGaps ? State.Builder.CreateBinOp(Instruction::And,
                                                   ShuffledMask, MaskForGaps)
                       : ShuffledMask;
  };

  const DataLayout &DL = Instr->getDataLayout();
  // Vectorize the interleaved load group.
  if (isa<LoadInst>(Instr)) {
    Value *MaskForGaps = nullptr;
    if (NeedsMaskForGaps) {
      MaskForGaps = createBitMaskForGaps(State.Builder,
                                         State.VF.getKnownMinValue(), *Group);
      assert(MaskForGaps && "Mask for Gaps is required but it is null");
    }

    // For each unroll part, create a wide load for the group.
    SmallVector<Value *, 2> NewLoads;
    for (unsigned Part = 0; Part < State.UF; Part++) {
      Instruction *NewLoad;
      if (BlockInMask || MaskForGaps) {
        Value *GroupMask = CreateGroupMask(Part, MaskForGaps);
        NewLoad = State.Builder.CreateMaskedLoad(VecTy, AddrParts[Part],
                                                 Group->getAlign(), GroupMask,
                                                 PoisonVec, "wide.masked.vec");
      } else
        NewLoad = State.Builder.CreateAlignedLoad(
            VecTy, AddrParts[Part], Group->getAlign(), "wide.vec");
      Group->addMetadata(NewLoad);
      NewLoads.push_back(NewLoad);
    }

    ArrayRef<VPValue *> VPDefs = definedValues();
    const DataLayout &DL = State.CFG.PrevBB->getDataLayout();
    if (VecTy->isScalableTy()) {
      assert(InterleaveFactor == 2 &&
             "Unsupported deinterleave factor for scalable vectors");

      for (unsigned Part = 0; Part < State.UF; ++Part) {
        // Scalable vectors cannot use arbitrary shufflevectors (only splats),
        // so must use intrinsics to deinterleave.
        Value *DI = State.Builder.CreateIntrinsic(
            Intrinsic::vector_deinterleave2, VecTy, NewLoads[Part],
            /*FMFSource=*/nullptr, "strided.vec");
        unsigned J = 0;
        for (unsigned I = 0; I < InterleaveFactor; ++I) {
          Instruction *Member = Group->getMember(I);

          if (!Member)
            continue;

          Value *StridedVec = State.Builder.CreateExtractValue(DI, I);
          // If this member has different type, cast the result type.
          if (Member->getType() != ScalarTy) {
            VectorType *OtherVTy = VectorType::get(Member->getType(), State.VF);
            StridedVec =
                createBitOrPointerCast(State.Builder, StridedVec, OtherVTy, DL);
          }

          if (Group->isReverse())
            StridedVec =
                State.Builder.CreateVectorReverse(StridedVec, "reverse");

          State.set(VPDefs[J], StridedVec, Part);
          ++J;
        }
      }

      return;
    }

    // For each member in the group, shuffle out the appropriate data from the
    // wide loads.
    unsigned J = 0;
    for (unsigned I = 0; I < InterleaveFactor; ++I) {
      Instruction *Member = Group->getMember(I);

      // Skip the gaps in the group.
      if (!Member)
        continue;

      auto StrideMask =
          createStrideMask(I, InterleaveFactor, State.VF.getKnownMinValue());
      for (unsigned Part = 0; Part < State.UF; Part++) {
        Value *StridedVec = State.Builder.CreateShuffleVector(
            NewLoads[Part], StrideMask, "strided.vec");

        // If this member has different type, cast the result type.
        if (Member->getType() != ScalarTy) {
          assert(!State.VF.isScalable() && "VF is assumed to be non scalable.");
          VectorType *OtherVTy = VectorType::get(Member->getType(), State.VF);
          StridedVec =
              createBitOrPointerCast(State.Builder, StridedVec, OtherVTy, DL);
        }

        if (Group->isReverse())
          StridedVec = State.Builder.CreateVectorReverse(StridedVec, "reverse");

        State.set(VPDefs[J], StridedVec, Part);
      }
      ++J;
    }
    return;
  }

  // The sub vector type for current instruction.
  auto *SubVT = VectorType::get(ScalarTy, State.VF);

  // Vectorize the interleaved store group.
  Value *MaskForGaps =
      createBitMaskForGaps(State.Builder, State.VF.getKnownMinValue(), *Group);
  assert((!MaskForGaps || !State.VF.isScalable()) &&
         "masking gaps for scalable vectors is not yet supported.");
  ArrayRef<VPValue *> StoredValues = getStoredValues();
  for (unsigned Part = 0; Part < State.UF; Part++) {
    // Collect the stored vector from each member.
    SmallVector<Value *, 4> StoredVecs;
    unsigned StoredIdx = 0;
    for (unsigned i = 0; i < InterleaveFactor; i++) {
      assert((Group->getMember(i) || MaskForGaps) &&
             "Fail to get a member from an interleaved store group");
      Instruction *Member = Group->getMember(i);

      // Skip the gaps in the group.
      if (!Member) {
        Value *Undef = PoisonValue::get(SubVT);
        StoredVecs.push_back(Undef);
        continue;
      }

      Value *StoredVec = State.get(StoredValues[StoredIdx], Part);
      ++StoredIdx;

      if (Group->isReverse())
        StoredVec = State.Builder.CreateVectorReverse(StoredVec, "reverse");

      // If this member has different type, cast it to a unified type.

      if (StoredVec->getType() != SubVT)
        StoredVec = createBitOrPointerCast(State.Builder, StoredVec, SubVT, DL);

      StoredVecs.push_back(StoredVec);
    }

    // Interleave all the smaller vectors into one wider vector.
    Value *IVec =
        interleaveVectors(State.Builder, StoredVecs, "interleaved.vec");
    Instruction *NewStoreInstr;
    if (BlockInMask || MaskForGaps) {
      Value *GroupMask = CreateGroupMask(Part, MaskForGaps);
      NewStoreInstr = State.Builder.CreateMaskedStore(
          IVec, AddrParts[Part], Group->getAlign(), GroupMask);
    } else
      NewStoreInstr = State.Builder.CreateAlignedStore(IVec, AddrParts[Part],
                                                       Group->getAlign());

    Group->addMetadata(NewStoreInstr);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPInterleaveRecipe::print(raw_ostream &O, const Twine &Indent,
                               VPSlotTracker &SlotTracker) const {
  O << Indent << "INTERLEAVE-GROUP with factor " << IG->getFactor() << " at ";
  IG->getInsertPos()->printAsOperand(O, false);
  O << ", ";
  getAddr()->printAsOperand(O, SlotTracker);
  VPValue *Mask = getMask();
  if (Mask) {
    O << ", ";
    Mask->printAsOperand(O, SlotTracker);
  }

  unsigned OpIdx = 0;
  for (unsigned i = 0; i < IG->getFactor(); ++i) {
    if (!IG->getMember(i))
      continue;
    if (getNumStoreOperands() > 0) {
      O << "\n" << Indent << "  store ";
      getOperand(1 + OpIdx)->printAsOperand(O, SlotTracker);
      O << " to index " << i;
    } else {
      O << "\n" << Indent << "  ";
      getVPValue(OpIdx)->printAsOperand(O, SlotTracker);
      O << " = load from index " << i;
    }
    ++OpIdx;
  }
}
#endif

void VPCanonicalIVPHIRecipe::execute(VPTransformState &State) {
  Value *Start = getStartValue()->getLiveInIRValue();
  PHINode *EntryPart = PHINode::Create(Start->getType(), 2, "index");
  EntryPart->insertBefore(State.CFG.PrevBB->getFirstInsertionPt());

  BasicBlock *VectorPH = State.CFG.getPreheaderBBFor(this);
  EntryPart->addIncoming(Start, VectorPH);
  EntryPart->setDebugLoc(getDebugLoc());
  for (unsigned Part = 0, UF = State.UF; Part < UF; ++Part)
    State.set(this, EntryPart, Part, /*IsScalar*/ true);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPCanonicalIVPHIRecipe::print(raw_ostream &O, const Twine &Indent,
                                   VPSlotTracker &SlotTracker) const {
  O << Indent << "EMIT ";
  printAsOperand(O, SlotTracker);
  O << " = CANONICAL-INDUCTION ";
  printOperands(O, SlotTracker);
}
#endif

bool VPCanonicalIVPHIRecipe::isCanonical(
    InductionDescriptor::InductionKind Kind, VPValue *Start,
    VPValue *Step) const {
  // Must be an integer induction.
  if (Kind != InductionDescriptor::IK_IntInduction)
    return false;
  // Start must match the start value of this canonical induction.
  if (Start != getStartValue())
    return false;

  // If the step is defined by a recipe, it is not a ConstantInt.
  if (Step->getDefiningRecipe())
    return false;

  ConstantInt *StepC = dyn_cast<ConstantInt>(Step->getLiveInIRValue());
  return StepC && StepC->isOne();
}

bool VPWidenPointerInductionRecipe::onlyScalarsGenerated(bool IsScalable) {
  return IsScalarAfterVectorization &&
         (!IsScalable || vputils::onlyFirstLaneUsed(this));
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPWidenPointerInductionRecipe::print(raw_ostream &O, const Twine &Indent,
                                          VPSlotTracker &SlotTracker) const {
  O << Indent << "EMIT ";
  printAsOperand(O, SlotTracker);
  O << " = WIDEN-POINTER-INDUCTION ";
  getStartValue()->printAsOperand(O, SlotTracker);
  O << ", " << *IndDesc.getStep();
}
#endif

void VPExpandSCEVRecipe::execute(VPTransformState &State) {
  assert(!State.Instance && "cannot be used in per-lane");
  const DataLayout &DL = State.CFG.PrevBB->getDataLayout();
  SCEVExpander Exp(SE, DL, "induction");

  Value *Res = Exp.expandCodeFor(Expr, Expr->getType(),
                                 &*State.Builder.GetInsertPoint());
  assert(!State.ExpandedSCEVs.contains(Expr) &&
         "Same SCEV expanded multiple times");
  State.ExpandedSCEVs[Expr] = Res;
  for (unsigned Part = 0, UF = State.UF; Part < UF; ++Part)
    State.set(this, Res, {Part, 0});
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPExpandSCEVRecipe::print(raw_ostream &O, const Twine &Indent,
                               VPSlotTracker &SlotTracker) const {
  O << Indent << "EMIT ";
  getVPSingleValue()->printAsOperand(O, SlotTracker);
  O << " = EXPAND SCEV " << *Expr;
}
#endif

void VPWidenCanonicalIVRecipe::execute(VPTransformState &State) {
  Value *CanonicalIV = State.get(getOperand(0), 0, /*IsScalar*/ true);
  Type *STy = CanonicalIV->getType();
  IRBuilder<> Builder(State.CFG.PrevBB->getTerminator());
  ElementCount VF = State.VF;
  Value *VStart = VF.isScalar()
                      ? CanonicalIV
                      : Builder.CreateVectorSplat(VF, CanonicalIV, "broadcast");
  for (unsigned Part = 0, UF = State.UF; Part < UF; ++Part) {
    Value *VStep = createStepForVF(Builder, STy, VF, Part);
    if (VF.isVector()) {
      VStep = Builder.CreateVectorSplat(VF, VStep);
      VStep =
          Builder.CreateAdd(VStep, Builder.CreateStepVector(VStep->getType()));
    }
    Value *CanonicalVectorIV = Builder.CreateAdd(VStart, VStep, "vec.iv");
    State.set(this, CanonicalVectorIV, Part);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPWidenCanonicalIVRecipe::print(raw_ostream &O, const Twine &Indent,
                                     VPSlotTracker &SlotTracker) const {
  O << Indent << "EMIT ";
  printAsOperand(O, SlotTracker);
  O << " = WIDEN-CANONICAL-INDUCTION ";
  printOperands(O, SlotTracker);
}
#endif

void VPFirstOrderRecurrencePHIRecipe::execute(VPTransformState &State) {
  auto &Builder = State.Builder;
  // Create a vector from the initial value.
  auto *VectorInit = getStartValue()->getLiveInIRValue();

  Type *VecTy = State.VF.isScalar()
                    ? VectorInit->getType()
                    : VectorType::get(VectorInit->getType(), State.VF);

  BasicBlock *VectorPH = State.CFG.getPreheaderBBFor(this);
  if (State.VF.isVector()) {
    auto *IdxTy = Builder.getInt32Ty();
    auto *One = ConstantInt::get(IdxTy, 1);
    IRBuilder<>::InsertPointGuard Guard(Builder);
    Builder.SetInsertPoint(VectorPH->getTerminator());
    auto *RuntimeVF = getRuntimeVF(Builder, IdxTy, State.VF);
    auto *LastIdx = Builder.CreateSub(RuntimeVF, One);
    VectorInit = Builder.CreateInsertElement(
        PoisonValue::get(VecTy), VectorInit, LastIdx, "vector.recur.init");
  }

  // Create a phi node for the new recurrence.
  PHINode *EntryPart = PHINode::Create(VecTy, 2, "vector.recur");
  EntryPart->insertBefore(State.CFG.PrevBB->getFirstInsertionPt());
  EntryPart->addIncoming(VectorInit, VectorPH);
  State.set(this, EntryPart, 0);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPFirstOrderRecurrencePHIRecipe::print(raw_ostream &O, const Twine &Indent,
                                            VPSlotTracker &SlotTracker) const {
  O << Indent << "FIRST-ORDER-RECURRENCE-PHI ";
  printAsOperand(O, SlotTracker);
  O << " = phi ";
  printOperands(O, SlotTracker);
}
#endif

void VPReductionPHIRecipe::execute(VPTransformState &State) {
  auto &Builder = State.Builder;

  // Reductions do not have to start at zero. They can start with
  // any loop invariant values.
  VPValue *StartVPV = getStartValue();
  Value *StartV = StartVPV->getLiveInIRValue();

  // In order to support recurrences we need to be able to vectorize Phi nodes.
  // Phi nodes have cycles, so we need to vectorize them in two stages. This is
  // stage #1: We create a new vector PHI node with no incoming edges. We'll use
  // this value when we vectorize all of the instructions that use the PHI.
  bool ScalarPHI = State.VF.isScalar() || IsInLoop;
  Type *VecTy = ScalarPHI ? StartV->getType()
                          : VectorType::get(StartV->getType(), State.VF);

  BasicBlock *HeaderBB = State.CFG.PrevBB;
  assert(State.CurrentVectorLoop->getHeader() == HeaderBB &&
         "recipe must be in the vector loop header");
  unsigned LastPartForNewPhi = isOrdered() ? 1 : State.UF;
  for (unsigned Part = 0; Part < LastPartForNewPhi; ++Part) {
    Instruction *EntryPart = PHINode::Create(VecTy, 2, "vec.phi");
    EntryPart->insertBefore(HeaderBB->getFirstInsertionPt());
    State.set(this, EntryPart, Part, IsInLoop);
  }

  BasicBlock *VectorPH = State.CFG.getPreheaderBBFor(this);

  Value *Iden = nullptr;
  RecurKind RK = RdxDesc.getRecurrenceKind();
  if (RecurrenceDescriptor::isMinMaxRecurrenceKind(RK) ||
      RecurrenceDescriptor::isAnyOfRecurrenceKind(RK)) {
    // MinMax and AnyOf reductions have the start value as their identity.
    if (ScalarPHI) {
      Iden = StartV;
    } else {
      IRBuilderBase::InsertPointGuard IPBuilder(Builder);
      Builder.SetInsertPoint(VectorPH->getTerminator());
      StartV = Iden =
          Builder.CreateVectorSplat(State.VF, StartV, "minmax.ident");
    }
  } else {
    Iden = RdxDesc.getRecurrenceIdentity(RK, VecTy->getScalarType(),
                                         RdxDesc.getFastMathFlags());

    if (!ScalarPHI) {
      Iden = Builder.CreateVectorSplat(State.VF, Iden);
      IRBuilderBase::InsertPointGuard IPBuilder(Builder);
      Builder.SetInsertPoint(VectorPH->getTerminator());
      Constant *Zero = Builder.getInt32(0);
      StartV = Builder.CreateInsertElement(Iden, StartV, Zero);
    }
  }

  for (unsigned Part = 0; Part < LastPartForNewPhi; ++Part) {
    Value *EntryPart = State.get(this, Part, IsInLoop);
    // Make sure to add the reduction start value only to the
    // first unroll part.
    Value *StartVal = (Part == 0) ? StartV : Iden;
    cast<PHINode>(EntryPart)->addIncoming(StartVal, VectorPH);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPReductionPHIRecipe::print(raw_ostream &O, const Twine &Indent,
                                 VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN-REDUCTION-PHI ";

  printAsOperand(O, SlotTracker);
  O << " = phi ";
  printOperands(O, SlotTracker);
}
#endif

void VPWidenPHIRecipe::execute(VPTransformState &State) {
  assert(EnableVPlanNativePath &&
         "Non-native vplans are not expected to have VPWidenPHIRecipes.");

  Value *Op0 = State.get(getOperand(0), 0);
  Type *VecTy = Op0->getType();
  Value *VecPhi = State.Builder.CreatePHI(VecTy, 2, "vec.phi");
  State.set(this, VecPhi, 0);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPWidenPHIRecipe::print(raw_ostream &O, const Twine &Indent,
                             VPSlotTracker &SlotTracker) const {
  O << Indent << "WIDEN-PHI ";

  auto *OriginalPhi = cast<PHINode>(getUnderlyingValue());
  // Unless all incoming values are modeled in VPlan  print the original PHI
  // directly.
  // TODO: Remove once all VPWidenPHIRecipe instances keep all relevant incoming
  // values as VPValues.
  if (getNumOperands() != OriginalPhi->getNumOperands()) {
    O << VPlanIngredient(OriginalPhi);
    return;
  }

  printAsOperand(O, SlotTracker);
  O << " = phi ";
  printOperands(O, SlotTracker);
}
#endif

// TODO: It would be good to use the existing VPWidenPHIRecipe instead and
// remove VPActiveLaneMaskPHIRecipe.
void VPActiveLaneMaskPHIRecipe::execute(VPTransformState &State) {
  BasicBlock *VectorPH = State.CFG.getPreheaderBBFor(this);
  for (unsigned Part = 0, UF = State.UF; Part < UF; ++Part) {
    Value *StartMask = State.get(getOperand(0), Part);
    PHINode *EntryPart =
        State.Builder.CreatePHI(StartMask->getType(), 2, "active.lane.mask");
    EntryPart->addIncoming(StartMask, VectorPH);
    EntryPart->setDebugLoc(getDebugLoc());
    State.set(this, EntryPart, Part);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPActiveLaneMaskPHIRecipe::print(raw_ostream &O, const Twine &Indent,
                                      VPSlotTracker &SlotTracker) const {
  O << Indent << "ACTIVE-LANE-MASK-PHI ";

  printAsOperand(O, SlotTracker);
  O << " = phi ";
  printOperands(O, SlotTracker);
}
#endif

void VPEVLBasedIVPHIRecipe::execute(VPTransformState &State) {
  BasicBlock *VectorPH = State.CFG.getPreheaderBBFor(this);
  assert(State.UF == 1 && "Expected unroll factor 1 for VP vectorization.");
  Value *Start = State.get(getOperand(0), VPIteration(0, 0));
  PHINode *EntryPart =
      State.Builder.CreatePHI(Start->getType(), 2, "evl.based.iv");
  EntryPart->addIncoming(Start, VectorPH);
  EntryPart->setDebugLoc(getDebugLoc());
  State.set(this, EntryPart, 0, /*IsScalar=*/true);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void VPEVLBasedIVPHIRecipe::print(raw_ostream &O, const Twine &Indent,
                                  VPSlotTracker &SlotTracker) const {
  O << Indent << "EXPLICIT-VECTOR-LENGTH-BASED-IV-PHI ";

  printAsOperand(O, SlotTracker);
  O << " = phi ";
  printOperands(O, SlotTracker);
}
#endif
