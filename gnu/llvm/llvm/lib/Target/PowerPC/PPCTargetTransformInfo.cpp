//===-- PPCTargetTransformInfo.cpp - PPC specific TTI ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PPCTargetTransformInfo.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/IR/IntrinsicsPowerPC.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include "llvm/Transforms/Utils/Local.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "ppctti"

static cl::opt<bool> VecMaskCost("ppc-vec-mask-cost",
cl::desc("add masking cost for i1 vectors"), cl::init(true), cl::Hidden);

static cl::opt<bool> DisablePPCConstHoist("disable-ppc-constant-hoisting",
cl::desc("disable constant hoisting on PPC"), cl::init(false), cl::Hidden);

static cl::opt<bool>
EnablePPCColdCC("ppc-enable-coldcc", cl::Hidden, cl::init(false),
                cl::desc("Enable using coldcc calling conv for cold "
                         "internal functions"));

static cl::opt<bool>
LsrNoInsnsCost("ppc-lsr-no-insns-cost", cl::Hidden, cl::init(false),
               cl::desc("Do not add instruction count to lsr cost model"));

// The latency of mtctr is only justified if there are more than 4
// comparisons that will be removed as a result.
static cl::opt<unsigned>
SmallCTRLoopThreshold("min-ctr-loop-threshold", cl::init(4), cl::Hidden,
                      cl::desc("Loops with a constant trip count smaller than "
                               "this value will not use the count register."));

//===----------------------------------------------------------------------===//
//
// PPC cost model.
//
//===----------------------------------------------------------------------===//

TargetTransformInfo::PopcntSupportKind
PPCTTIImpl::getPopcntSupport(unsigned TyWidth) {
  assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
  if (ST->hasPOPCNTD() != PPCSubtarget::POPCNTD_Unavailable && TyWidth <= 64)
    return ST->hasPOPCNTD() == PPCSubtarget::POPCNTD_Slow ?
             TTI::PSK_SlowHardware : TTI::PSK_FastHardware;
  return TTI::PSK_Software;
}

std::optional<Instruction *>
PPCTTIImpl::instCombineIntrinsic(InstCombiner &IC, IntrinsicInst &II) const {
  Intrinsic::ID IID = II.getIntrinsicID();
  switch (IID) {
  default:
    break;
  case Intrinsic::ppc_altivec_lvx:
  case Intrinsic::ppc_altivec_lvxl:
    // Turn PPC lvx -> load if the pointer is known aligned.
    if (getOrEnforceKnownAlignment(
            II.getArgOperand(0), Align(16), IC.getDataLayout(), &II,
            &IC.getAssumptionCache(), &IC.getDominatorTree()) >= 16) {
      Value *Ptr = II.getArgOperand(0);
      return new LoadInst(II.getType(), Ptr, "", false, Align(16));
    }
    break;
  case Intrinsic::ppc_vsx_lxvw4x:
  case Intrinsic::ppc_vsx_lxvd2x: {
    // Turn PPC VSX loads into normal loads.
    Value *Ptr = II.getArgOperand(0);
    return new LoadInst(II.getType(), Ptr, Twine(""), false, Align(1));
  }
  case Intrinsic::ppc_altivec_stvx:
  case Intrinsic::ppc_altivec_stvxl:
    // Turn stvx -> store if the pointer is known aligned.
    if (getOrEnforceKnownAlignment(
            II.getArgOperand(1), Align(16), IC.getDataLayout(), &II,
            &IC.getAssumptionCache(), &IC.getDominatorTree()) >= 16) {
      Value *Ptr = II.getArgOperand(1);
      return new StoreInst(II.getArgOperand(0), Ptr, false, Align(16));
    }
    break;
  case Intrinsic::ppc_vsx_stxvw4x:
  case Intrinsic::ppc_vsx_stxvd2x: {
    // Turn PPC VSX stores into normal stores.
    Value *Ptr = II.getArgOperand(1);
    return new StoreInst(II.getArgOperand(0), Ptr, false, Align(1));
  }
  case Intrinsic::ppc_altivec_vperm:
    // Turn vperm(V1,V2,mask) -> shuffle(V1,V2,mask) if mask is a constant.
    // Note that ppc_altivec_vperm has a big-endian bias, so when creating
    // a vectorshuffle for little endian, we must undo the transformation
    // performed on vec_perm in altivec.h.  That is, we must complement
    // the permutation mask with respect to 31 and reverse the order of
    // V1 and V2.
    if (Constant *Mask = dyn_cast<Constant>(II.getArgOperand(2))) {
      assert(cast<FixedVectorType>(Mask->getType())->getNumElements() == 16 &&
             "Bad type for intrinsic!");

      // Check that all of the elements are integer constants or undefs.
      bool AllEltsOk = true;
      for (unsigned i = 0; i != 16; ++i) {
        Constant *Elt = Mask->getAggregateElement(i);
        if (!Elt || !(isa<ConstantInt>(Elt) || isa<UndefValue>(Elt))) {
          AllEltsOk = false;
          break;
        }
      }

      if (AllEltsOk) {
        // Cast the input vectors to byte vectors.
        Value *Op0 =
            IC.Builder.CreateBitCast(II.getArgOperand(0), Mask->getType());
        Value *Op1 =
            IC.Builder.CreateBitCast(II.getArgOperand(1), Mask->getType());
        Value *Result = UndefValue::get(Op0->getType());

        // Only extract each element once.
        Value *ExtractedElts[32];
        memset(ExtractedElts, 0, sizeof(ExtractedElts));

        for (unsigned i = 0; i != 16; ++i) {
          if (isa<UndefValue>(Mask->getAggregateElement(i)))
            continue;
          unsigned Idx =
              cast<ConstantInt>(Mask->getAggregateElement(i))->getZExtValue();
          Idx &= 31; // Match the hardware behavior.
          if (DL.isLittleEndian())
            Idx = 31 - Idx;

          if (!ExtractedElts[Idx]) {
            Value *Op0ToUse = (DL.isLittleEndian()) ? Op1 : Op0;
            Value *Op1ToUse = (DL.isLittleEndian()) ? Op0 : Op1;
            ExtractedElts[Idx] = IC.Builder.CreateExtractElement(
                Idx < 16 ? Op0ToUse : Op1ToUse, IC.Builder.getInt32(Idx & 15));
          }

          // Insert this value into the result vector.
          Result = IC.Builder.CreateInsertElement(Result, ExtractedElts[Idx],
                                                  IC.Builder.getInt32(i));
        }
        return CastInst::Create(Instruction::BitCast, Result, II.getType());
      }
    }
    break;
  }
  return std::nullopt;
}

InstructionCost PPCTTIImpl::getIntImmCost(const APInt &Imm, Type *Ty,
                                          TTI::TargetCostKind CostKind) {
  if (DisablePPCConstHoist)
    return BaseT::getIntImmCost(Imm, Ty, CostKind);

  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  if (Imm == 0)
    return TTI::TCC_Free;

  if (Imm.getBitWidth() <= 64) {
    if (isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Basic;

    if (isInt<32>(Imm.getSExtValue())) {
      // A constant that can be materialized using lis.
      if ((Imm.getZExtValue() & 0xFFFF) == 0)
        return TTI::TCC_Basic;

      return 2 * TTI::TCC_Basic;
    }
  }

  return 4 * TTI::TCC_Basic;
}

InstructionCost PPCTTIImpl::getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                                const APInt &Imm, Type *Ty,
                                                TTI::TargetCostKind CostKind) {
  if (DisablePPCConstHoist)
    return BaseT::getIntImmCostIntrin(IID, Idx, Imm, Ty, CostKind);

  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  switch (IID) {
  default:
    return TTI::TCC_Free;
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
    if ((Idx == 1) && Imm.getBitWidth() <= 64 && isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Free;
    break;
  case Intrinsic::experimental_stackmap:
    if ((Idx < 2) || (Imm.getBitWidth() <= 64 && isInt<64>(Imm.getSExtValue())))
      return TTI::TCC_Free;
    break;
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint:
    if ((Idx < 4) || (Imm.getBitWidth() <= 64 && isInt<64>(Imm.getSExtValue())))
      return TTI::TCC_Free;
    break;
  }
  return PPCTTIImpl::getIntImmCost(Imm, Ty, CostKind);
}

InstructionCost PPCTTIImpl::getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                              const APInt &Imm, Type *Ty,
                                              TTI::TargetCostKind CostKind,
                                              Instruction *Inst) {
  if (DisablePPCConstHoist)
    return BaseT::getIntImmCostInst(Opcode, Idx, Imm, Ty, CostKind, Inst);

  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return ~0U;

  unsigned ImmIdx = ~0U;
  bool ShiftedFree = false, RunFree = false, UnsignedFree = false,
       ZeroFree = false;
  switch (Opcode) {
  default:
    return TTI::TCC_Free;
  case Instruction::GetElementPtr:
    // Always hoist the base address of a GetElementPtr. This prevents the
    // creation of new constants for every base constant that gets constant
    // folded with the offset.
    if (Idx == 0)
      return 2 * TTI::TCC_Basic;
    return TTI::TCC_Free;
  case Instruction::And:
    RunFree = true; // (for the rotate-and-mask instructions)
    [[fallthrough]];
  case Instruction::Add:
  case Instruction::Or:
  case Instruction::Xor:
    ShiftedFree = true;
    [[fallthrough]];
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    ImmIdx = 1;
    break;
  case Instruction::ICmp:
    UnsignedFree = true;
    ImmIdx = 1;
    // Zero comparisons can use record-form instructions.
    [[fallthrough]];
  case Instruction::Select:
    ZeroFree = true;
    break;
  case Instruction::PHI:
  case Instruction::Call:
  case Instruction::Ret:
  case Instruction::Load:
  case Instruction::Store:
    break;
  }

  if (ZeroFree && Imm == 0)
    return TTI::TCC_Free;

  if (Idx == ImmIdx && Imm.getBitWidth() <= 64) {
    if (isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Free;

    if (RunFree) {
      if (Imm.getBitWidth() <= 32 &&
          (isShiftedMask_32(Imm.getZExtValue()) ||
           isShiftedMask_32(~Imm.getZExtValue())))
        return TTI::TCC_Free;

      if (ST->isPPC64() &&
          (isShiftedMask_64(Imm.getZExtValue()) ||
           isShiftedMask_64(~Imm.getZExtValue())))
        return TTI::TCC_Free;
    }

    if (UnsignedFree && isUInt<16>(Imm.getZExtValue()))
      return TTI::TCC_Free;

    if (ShiftedFree && (Imm.getZExtValue() & 0xFFFF) == 0)
      return TTI::TCC_Free;
  }

  return PPCTTIImpl::getIntImmCost(Imm, Ty, CostKind);
}

// Check if the current Type is an MMA vector type. Valid MMA types are
// v256i1 and v512i1 respectively.
static bool isMMAType(Type *Ty) {
  return Ty->isVectorTy() && (Ty->getScalarSizeInBits() == 1) &&
         (Ty->getPrimitiveSizeInBits() > 128);
}

InstructionCost PPCTTIImpl::getInstructionCost(const User *U,
                                               ArrayRef<const Value *> Operands,
                                               TTI::TargetCostKind CostKind) {
  // We already implement getCastInstrCost and getMemoryOpCost where we perform
  // the vector adjustment there.
  if (isa<CastInst>(U) || isa<LoadInst>(U) || isa<StoreInst>(U))
    return BaseT::getInstructionCost(U, Operands, CostKind);

  if (U->getType()->isVectorTy()) {
    // Instructions that need to be split should cost more.
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(U->getType());
    return LT.first * BaseT::getInstructionCost(U, Operands, CostKind);
  }

  return BaseT::getInstructionCost(U, Operands, CostKind);
}

bool PPCTTIImpl::isHardwareLoopProfitable(Loop *L, ScalarEvolution &SE,
                                          AssumptionCache &AC,
                                          TargetLibraryInfo *LibInfo,
                                          HardwareLoopInfo &HWLoopInfo) {
  const PPCTargetMachine &TM = ST->getTargetMachine();
  TargetSchedModel SchedModel;
  SchedModel.init(ST);

  // Do not convert small short loops to CTR loop.
  unsigned ConstTripCount = SE.getSmallConstantTripCount(L);
  if (ConstTripCount && ConstTripCount < SmallCTRLoopThreshold) {
    SmallPtrSet<const Value *, 32> EphValues;
    CodeMetrics::collectEphemeralValues(L, &AC, EphValues);
    CodeMetrics Metrics;
    for (BasicBlock *BB : L->blocks())
      Metrics.analyzeBasicBlock(BB, *this, EphValues);
    // 6 is an approximate latency for the mtctr instruction.
    if (Metrics.NumInsts <= (6 * SchedModel.getIssueWidth()))
      return false;
  }

  // Check that there is no hardware loop related intrinsics in the loop.
  for (auto *BB : L->getBlocks())
    for (auto &I : *BB)
      if (auto *Call = dyn_cast<IntrinsicInst>(&I))
        if (Call->getIntrinsicID() == Intrinsic::set_loop_iterations ||
            Call->getIntrinsicID() == Intrinsic::loop_decrement)
          return false;

  SmallVector<BasicBlock*, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  // If there is an exit edge known to be frequently taken,
  // we should not transform this loop.
  for (auto &BB : ExitingBlocks) {
    Instruction *TI = BB->getTerminator();
    if (!TI) continue;

    if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
      uint64_t TrueWeight = 0, FalseWeight = 0;
      if (!BI->isConditional() ||
          !extractBranchWeights(*BI, TrueWeight, FalseWeight))
        continue;

      // If the exit path is more frequent than the loop path,
      // we return here without further analysis for this loop.
      bool TrueIsExit = !L->contains(BI->getSuccessor(0));
      if (( TrueIsExit && FalseWeight < TrueWeight) ||
          (!TrueIsExit && FalseWeight > TrueWeight))
        return false;
    }
  }

  LLVMContext &C = L->getHeader()->getContext();
  HWLoopInfo.CountType = TM.isPPC64() ?
    Type::getInt64Ty(C) : Type::getInt32Ty(C);
  HWLoopInfo.LoopDecrement = ConstantInt::get(HWLoopInfo.CountType, 1);
  return true;
}

void PPCTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                         TTI::UnrollingPreferences &UP,
                                         OptimizationRemarkEmitter *ORE) {
  if (ST->getCPUDirective() == PPC::DIR_A2) {
    // The A2 is in-order with a deep pipeline, and concatenation unrolling
    // helps expose latency-hiding opportunities to the instruction scheduler.
    UP.Partial = UP.Runtime = true;

    // We unroll a lot on the A2 (hundreds of instructions), and the benefits
    // often outweigh the cost of a division to compute the trip count.
    UP.AllowExpensiveTripCount = true;
  }

  BaseT::getUnrollingPreferences(L, SE, UP, ORE);
}

void PPCTTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                       TTI::PeelingPreferences &PP) {
  BaseT::getPeelingPreferences(L, SE, PP);
}
// This function returns true to allow using coldcc calling convention.
// Returning true results in coldcc being used for functions which are cold at
// all call sites when the callers of the functions are not calling any other
// non coldcc functions.
bool PPCTTIImpl::useColdCCForColdCall(Function &F) {
  return EnablePPCColdCC;
}

bool PPCTTIImpl::enableAggressiveInterleaving(bool LoopHasReductions) {
  // On the A2, always unroll aggressively.
  if (ST->getCPUDirective() == PPC::DIR_A2)
    return true;

  return LoopHasReductions;
}

PPCTTIImpl::TTI::MemCmpExpansionOptions
PPCTTIImpl::enableMemCmpExpansion(bool OptSize, bool IsZeroCmp) const {
  TTI::MemCmpExpansionOptions Options;
  Options.LoadSizes = {8, 4, 2, 1};
  Options.MaxNumLoads = TLI->getMaxExpandSizeMemcmp(OptSize);
  return Options;
}

bool PPCTTIImpl::enableInterleavedAccessVectorization() {
  return true;
}

unsigned PPCTTIImpl::getNumberOfRegisters(unsigned ClassID) const {
  assert(ClassID == GPRRC || ClassID == FPRRC ||
         ClassID == VRRC || ClassID == VSXRC);
  if (ST->hasVSX()) {
    assert(ClassID == GPRRC || ClassID == VSXRC || ClassID == VRRC);
    return ClassID == VSXRC ? 64 : 32;
  }
  assert(ClassID == GPRRC || ClassID == FPRRC || ClassID == VRRC);
  return 32;
}

unsigned PPCTTIImpl::getRegisterClassForType(bool Vector, Type *Ty) const {
  if (Vector)
    return ST->hasVSX() ? VSXRC : VRRC;
  else if (Ty && (Ty->getScalarType()->isFloatTy() ||
                  Ty->getScalarType()->isDoubleTy()))
    return ST->hasVSX() ? VSXRC : FPRRC;
  else if (Ty && (Ty->getScalarType()->isFP128Ty() ||
                  Ty->getScalarType()->isPPC_FP128Ty()))
    return VRRC;
  else if (Ty && Ty->getScalarType()->isHalfTy())
    return VSXRC;
  else
    return GPRRC;
}

const char* PPCTTIImpl::getRegisterClassName(unsigned ClassID) const {

  switch (ClassID) {
    default:
      llvm_unreachable("unknown register class");
      return "PPC::unknown register class";
    case GPRRC:       return "PPC::GPRRC";
    case FPRRC:       return "PPC::FPRRC";
    case VRRC:        return "PPC::VRRC";
    case VSXRC:       return "PPC::VSXRC";
  }
}

TypeSize
PPCTTIImpl::getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
  switch (K) {
  case TargetTransformInfo::RGK_Scalar:
    return TypeSize::getFixed(ST->isPPC64() ? 64 : 32);
  case TargetTransformInfo::RGK_FixedWidthVector:
    return TypeSize::getFixed(ST->hasAltivec() ? 128 : 0);
  case TargetTransformInfo::RGK_ScalableVector:
    return TypeSize::getScalable(0);
  }

  llvm_unreachable("Unsupported register kind");
}

unsigned PPCTTIImpl::getCacheLineSize() const {
  // Starting with P7 we have a cache line size of 128.
  unsigned Directive = ST->getCPUDirective();
  // Assume that Future CPU has the same cache line size as the others.
  if (Directive == PPC::DIR_PWR7 || Directive == PPC::DIR_PWR8 ||
      Directive == PPC::DIR_PWR9 || Directive == PPC::DIR_PWR10 ||
      Directive == PPC::DIR_PWR11 || Directive == PPC::DIR_PWR_FUTURE)
    return 128;

  // On other processors return a default of 64 bytes.
  return 64;
}

unsigned PPCTTIImpl::getPrefetchDistance() const {
  return 300;
}

unsigned PPCTTIImpl::getMaxInterleaveFactor(ElementCount VF) {
  unsigned Directive = ST->getCPUDirective();
  // The 440 has no SIMD support, but floating-point instructions
  // have a 5-cycle latency, so unroll by 5x for latency hiding.
  if (Directive == PPC::DIR_440)
    return 5;

  // The A2 has no SIMD support, but floating-point instructions
  // have a 6-cycle latency, so unroll by 6x for latency hiding.
  if (Directive == PPC::DIR_A2)
    return 6;

  // FIXME: For lack of any better information, do no harm...
  if (Directive == PPC::DIR_E500mc || Directive == PPC::DIR_E5500)
    return 1;

  // For P7 and P8, floating-point instructions have a 6-cycle latency and
  // there are two execution units, so unroll by 12x for latency hiding.
  // FIXME: the same for P9 as previous gen until POWER9 scheduling is ready
  // FIXME: the same for P10 as previous gen until POWER10 scheduling is ready
  // Assume that future is the same as the others.
  if (Directive == PPC::DIR_PWR7 || Directive == PPC::DIR_PWR8 ||
      Directive == PPC::DIR_PWR9 || Directive == PPC::DIR_PWR10 ||
      Directive == PPC::DIR_PWR11 || Directive == PPC::DIR_PWR_FUTURE)
    return 12;

  // For most things, modern systems have two execution units (and
  // out-of-order execution).
  return 2;
}

// Returns a cost adjustment factor to adjust the cost of vector instructions
// on targets which there is overlap between the vector and scalar units,
// thereby reducing the overall throughput of vector code wrt. scalar code.
// An invalid instruction cost is returned if the type is an MMA vector type.
InstructionCost PPCTTIImpl::vectorCostAdjustmentFactor(unsigned Opcode,
                                                       Type *Ty1, Type *Ty2) {
  // If the vector type is of an MMA type (v256i1, v512i1), an invalid
  // instruction cost is returned. This is to signify to other cost computing
  // functions to return the maximum instruction cost in order to prevent any
  // opportunities for the optimizer to produce MMA types within the IR.
  if (isMMAType(Ty1))
    return InstructionCost::getInvalid();

  if (!ST->vectorsUseTwoUnits() || !Ty1->isVectorTy())
    return InstructionCost(1);

  std::pair<InstructionCost, MVT> LT1 = getTypeLegalizationCost(Ty1);
  // If type legalization involves splitting the vector, we don't want to
  // double the cost at every step - only the last step.
  if (LT1.first != 1 || !LT1.second.isVector())
    return InstructionCost(1);

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  if (TLI->isOperationExpand(ISD, LT1.second))
    return InstructionCost(1);

  if (Ty2) {
    std::pair<InstructionCost, MVT> LT2 = getTypeLegalizationCost(Ty2);
    if (LT2.first != 1 || !LT2.second.isVector())
      return InstructionCost(1);
  }

  return InstructionCost(2);
}

InstructionCost PPCTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args,
    const Instruction *CxtI) {
  assert(TLI->InstructionOpcodeToISD(Opcode) && "Invalid opcode");

  InstructionCost CostFactor = vectorCostAdjustmentFactor(Opcode, Ty, nullptr);
  if (!CostFactor.isValid())
    return InstructionCost::getMax();

  // TODO: Handle more cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput)
    return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                         Op2Info, Args, CxtI);

  // Fallback to the default implementation.
  InstructionCost Cost = BaseT::getArithmeticInstrCost(
      Opcode, Ty, CostKind, Op1Info, Op2Info);
  return Cost * CostFactor;
}

InstructionCost PPCTTIImpl::getShuffleCost(TTI::ShuffleKind Kind, Type *Tp,
                                           ArrayRef<int> Mask,
                                           TTI::TargetCostKind CostKind,
                                           int Index, Type *SubTp,
                                           ArrayRef<const Value *> Args,
                                           const Instruction *CxtI) {

  InstructionCost CostFactor =
      vectorCostAdjustmentFactor(Instruction::ShuffleVector, Tp, nullptr);
  if (!CostFactor.isValid())
    return InstructionCost::getMax();

  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Tp);

  // PPC, for both Altivec/VSX, support cheap arbitrary permutations
  // (at least in the sense that there need only be one non-loop-invariant
  // instruction). We need one such shuffle instruction for each actual
  // register (this is not true for arbitrary shuffles, but is true for the
  // structured types of shuffles covered by TTI::ShuffleKind).
  return LT.first * CostFactor;
}

InstructionCost PPCTTIImpl::getCFInstrCost(unsigned Opcode,
                                           TTI::TargetCostKind CostKind,
                                           const Instruction *I) {
  if (CostKind != TTI::TCK_RecipThroughput)
    return Opcode == Instruction::PHI ? 0 : 1;
  // Branches are assumed to be predicted.
  return 0;
}

InstructionCost PPCTTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst,
                                             Type *Src,
                                             TTI::CastContextHint CCH,
                                             TTI::TargetCostKind CostKind,
                                             const Instruction *I) {
  assert(TLI->InstructionOpcodeToISD(Opcode) && "Invalid opcode");

  InstructionCost CostFactor = vectorCostAdjustmentFactor(Opcode, Dst, Src);
  if (!CostFactor.isValid())
    return InstructionCost::getMax();

  InstructionCost Cost =
      BaseT::getCastInstrCost(Opcode, Dst, Src, CCH, CostKind, I);
  Cost *= CostFactor;
  // TODO: Allow non-throughput costs that aren't binary.
  if (CostKind != TTI::TCK_RecipThroughput)
    return Cost == 0 ? 0 : 1;
  return Cost;
}

InstructionCost PPCTTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                               Type *CondTy,
                                               CmpInst::Predicate VecPred,
                                               TTI::TargetCostKind CostKind,
                                               const Instruction *I) {
  InstructionCost CostFactor =
      vectorCostAdjustmentFactor(Opcode, ValTy, nullptr);
  if (!CostFactor.isValid())
    return InstructionCost::getMax();

  InstructionCost Cost =
      BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, VecPred, CostKind, I);
  // TODO: Handle other cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput)
    return Cost;
  return Cost * CostFactor;
}

InstructionCost PPCTTIImpl::getVectorInstrCost(unsigned Opcode, Type *Val,
                                               TTI::TargetCostKind CostKind,
                                               unsigned Index, Value *Op0,
                                               Value *Op1) {
  assert(Val->isVectorTy() && "This must be a vector type");

  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  InstructionCost CostFactor = vectorCostAdjustmentFactor(Opcode, Val, nullptr);
  if (!CostFactor.isValid())
    return InstructionCost::getMax();

  InstructionCost Cost =
      BaseT::getVectorInstrCost(Opcode, Val, CostKind, Index, Op0, Op1);
  Cost *= CostFactor;

  if (ST->hasVSX() && Val->getScalarType()->isDoubleTy()) {
    // Double-precision scalars are already located in index #0 (or #1 if LE).
    if (ISD == ISD::EXTRACT_VECTOR_ELT &&
        Index == (ST->isLittleEndian() ? 1 : 0))
      return 0;

    return Cost;

  } else if (Val->getScalarType()->isIntegerTy()) {
    unsigned EltSize = Val->getScalarSizeInBits();
    // Computing on 1 bit values requires extra mask or compare operations.
    unsigned MaskCostForOneBitSize = (VecMaskCost && EltSize == 1) ? 1 : 0;
    // Computing on non const index requires extra mask or compare operations.
    unsigned MaskCostForIdx = (Index != -1U) ? 0 : 1;
    if (ST->hasP9Altivec()) {
      // P10 has vxform insert which can handle non const index. The
      // MaskCostForIdx is for masking the index.
      // P9 has insert for const index. A move-to VSR and a permute/insert.
      // Assume vector operation cost for both (cost will be 2x on P9).
      if (ISD == ISD::INSERT_VECTOR_ELT) {
        if (ST->hasP10Vector())
          return CostFactor + MaskCostForIdx;
        else if (Index != -1U)
          return 2 * CostFactor;
      } else if (ISD == ISD::EXTRACT_VECTOR_ELT) {
        // It's an extract.  Maybe we can do a cheap move-from VSR.
        unsigned EltSize = Val->getScalarSizeInBits();
        // P9 has both mfvsrd and mfvsrld for 64 bit integer.
        if (EltSize == 64 && Index != -1U)
          return 1;
        else if (EltSize == 32) {
          unsigned MfvsrwzIndex = ST->isLittleEndian() ? 2 : 1;
          if (Index == MfvsrwzIndex)
            return 1;

          // For other indexs like non const, P9 has vxform extract. The
          // MaskCostForIdx is for masking the index.
          return CostFactor + MaskCostForIdx;
        }

        // We need a vector extract (or mfvsrld).  Assume vector operation cost.
        // The cost of the load constant for a vector extract is disregarded
        // (invariant, easily schedulable).
        return CostFactor + MaskCostForOneBitSize + MaskCostForIdx;
      }
    } else if (ST->hasDirectMove() && Index != -1U) {
      // Assume permute has standard cost.
      // Assume move-to/move-from VSR have 2x standard cost.
      if (ISD == ISD::INSERT_VECTOR_ELT)
        return 3;
      return 3 + MaskCostForOneBitSize;
    }
  }

  // Estimated cost of a load-hit-store delay.  This was obtained
  // experimentally as a minimum needed to prevent unprofitable
  // vectorization for the paq8p benchmark.  It may need to be
  // raised further if other unprofitable cases remain.
  unsigned LHSPenalty = 2;
  if (ISD == ISD::INSERT_VECTOR_ELT)
    LHSPenalty += 7;

  // Vector element insert/extract with Altivec is very expensive,
  // because they require store and reload with the attendant
  // processor stall for load-hit-store.  Until VSX is available,
  // these need to be estimated as very costly.
  if (ISD == ISD::EXTRACT_VECTOR_ELT ||
      ISD == ISD::INSERT_VECTOR_ELT)
    return LHSPenalty + Cost;

  return Cost;
}

InstructionCost PPCTTIImpl::getMemoryOpCost(unsigned Opcode, Type *Src,
                                            MaybeAlign Alignment,
                                            unsigned AddressSpace,
                                            TTI::TargetCostKind CostKind,
                                            TTI::OperandValueInfo OpInfo,
                                            const Instruction *I) {

  InstructionCost CostFactor = vectorCostAdjustmentFactor(Opcode, Src, nullptr);
  if (!CostFactor.isValid())
    return InstructionCost::getMax();

  if (TLI->getValueType(DL, Src,  true) == MVT::Other)
    return BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                  CostKind);
  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Src);
  assert((Opcode == Instruction::Load || Opcode == Instruction::Store) &&
         "Invalid Opcode");

  InstructionCost Cost =
      BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace, CostKind);
  // TODO: Handle other cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput)
    return Cost;

  Cost *= CostFactor;

  bool IsAltivecType = ST->hasAltivec() &&
                       (LT.second == MVT::v16i8 || LT.second == MVT::v8i16 ||
                        LT.second == MVT::v4i32 || LT.second == MVT::v4f32);
  bool IsVSXType = ST->hasVSX() &&
                   (LT.second == MVT::v2f64 || LT.second == MVT::v2i64);

  // VSX has 32b/64b load instructions. Legalization can handle loading of
  // 32b/64b to VSR correctly and cheaply. But BaseT::getMemoryOpCost and
  // PPCTargetLowering can't compute the cost appropriately. So here we
  // explicitly check this case. There are also corresponding store
  // instructions.
  unsigned MemBytes = Src->getPrimitiveSizeInBits();
  if (ST->hasVSX() && IsAltivecType &&
      (MemBytes == 64 || (ST->hasP8Vector() && MemBytes == 32)))
    return 1;

  // Aligned loads and stores are easy.
  unsigned SrcBytes = LT.second.getStoreSize();
  if (!SrcBytes || !Alignment || *Alignment >= SrcBytes)
    return Cost;

  // If we can use the permutation-based load sequence, then this is also
  // relatively cheap (not counting loop-invariant instructions): one load plus
  // one permute (the last load in a series has extra cost, but we're
  // neglecting that here). Note that on the P7, we could do unaligned loads
  // for Altivec types using the VSX instructions, but that's more expensive
  // than using the permutation-based load sequence. On the P8, that's no
  // longer true.
  if (Opcode == Instruction::Load && (!ST->hasP8Vector() && IsAltivecType) &&
      *Alignment >= LT.second.getScalarType().getStoreSize())
    return Cost + LT.first; // Add the cost of the permutations.

  // For VSX, we can do unaligned loads and stores on Altivec/VSX types. On the
  // P7, unaligned vector loads are more expensive than the permutation-based
  // load sequence, so that might be used instead, but regardless, the net cost
  // is about the same (not counting loop-invariant instructions).
  if (IsVSXType || (ST->hasVSX() && IsAltivecType))
    return Cost;

  // Newer PPC supports unaligned memory access.
  if (TLI->allowsMisalignedMemoryAccesses(LT.second, 0))
    return Cost;

  // PPC in general does not support unaligned loads and stores. They'll need
  // to be decomposed based on the alignment factor.

  // Add the cost of each scalar load or store.
  assert(Alignment);
  Cost += LT.first * ((SrcBytes / Alignment->value()) - 1);

  // For a vector type, there is also scalarization overhead (only for
  // stores, loads are expanded using the vector-load + permutation sequence,
  // which is much less expensive).
  if (Src->isVectorTy() && Opcode == Instruction::Store)
    for (int i = 0, e = cast<FixedVectorType>(Src)->getNumElements(); i < e;
         ++i)
      Cost += getVectorInstrCost(Instruction::ExtractElement, Src, CostKind, i,
                                 nullptr, nullptr);

  return Cost;
}

InstructionCost PPCTTIImpl::getInterleavedMemoryOpCost(
    unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
    Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
    bool UseMaskForCond, bool UseMaskForGaps) {
  InstructionCost CostFactor =
      vectorCostAdjustmentFactor(Opcode, VecTy, nullptr);
  if (!CostFactor.isValid())
    return InstructionCost::getMax();

  if (UseMaskForCond || UseMaskForGaps)
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace, CostKind,
                                             UseMaskForCond, UseMaskForGaps);

  assert(isa<VectorType>(VecTy) &&
         "Expect a vector type for interleaved memory op");

  // Legalize the type.
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(VecTy);

  // Firstly, the cost of load/store operation.
  InstructionCost Cost = getMemoryOpCost(Opcode, VecTy, MaybeAlign(Alignment),
                                         AddressSpace, CostKind);

  // PPC, for both Altivec/VSX, support cheap arbitrary permutations
  // (at least in the sense that there need only be one non-loop-invariant
  // instruction). For each result vector, we need one shuffle per incoming
  // vector (except that the first shuffle can take two incoming vectors
  // because it does not need to take itself).
  Cost += Factor*(LT.first-1);

  return Cost;
}

InstructionCost
PPCTTIImpl::getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                  TTI::TargetCostKind CostKind) {
  return BaseT::getIntrinsicInstrCost(ICA, CostKind);
}

bool PPCTTIImpl::areTypesABICompatible(const Function *Caller,
                                       const Function *Callee,
                                       const ArrayRef<Type *> &Types) const {

  // We need to ensure that argument promotion does not
  // attempt to promote pointers to MMA types (__vector_pair
  // and __vector_quad) since these types explicitly cannot be
  // passed as arguments. Both of these types are larger than
  // the 128-bit Altivec vectors and have a scalar size of 1 bit.
  if (!BaseT::areTypesABICompatible(Caller, Callee, Types))
    return false;

  return llvm::none_of(Types, [](Type *Ty) {
    if (Ty->isSized())
      return Ty->isIntOrIntVectorTy(1) && Ty->getPrimitiveSizeInBits() > 128;
    return false;
  });
}

bool PPCTTIImpl::canSaveCmp(Loop *L, BranchInst **BI, ScalarEvolution *SE,
                            LoopInfo *LI, DominatorTree *DT,
                            AssumptionCache *AC, TargetLibraryInfo *LibInfo) {
  // Process nested loops first.
  for (Loop *I : *L)
    if (canSaveCmp(I, BI, SE, LI, DT, AC, LibInfo))
      return false; // Stop search.

  HardwareLoopInfo HWLoopInfo(L);

  if (!HWLoopInfo.canAnalyze(*LI))
    return false;

  if (!isHardwareLoopProfitable(L, *SE, *AC, LibInfo, HWLoopInfo))
    return false;

  if (!HWLoopInfo.isHardwareLoopCandidate(*SE, *LI, *DT))
    return false;

  *BI = HWLoopInfo.ExitBranch;
  return true;
}

bool PPCTTIImpl::isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                               const TargetTransformInfo::LSRCost &C2) {
  // PowerPC default behaviour here is "instruction number 1st priority".
  // If LsrNoInsnsCost is set, call default implementation.
  if (!LsrNoInsnsCost)
    return std::tie(C1.Insns, C1.NumRegs, C1.AddRecCost, C1.NumIVMuls,
                    C1.NumBaseAdds, C1.ScaleCost, C1.ImmCost, C1.SetupCost) <
           std::tie(C2.Insns, C2.NumRegs, C2.AddRecCost, C2.NumIVMuls,
                    C2.NumBaseAdds, C2.ScaleCost, C2.ImmCost, C2.SetupCost);
  else
    return TargetTransformInfoImplBase::isLSRCostLess(C1, C2);
}

bool PPCTTIImpl::isNumRegsMajorCostOfLSR() {
  return false;
}

bool PPCTTIImpl::shouldBuildRelLookupTables() const {
  const PPCTargetMachine &TM = ST->getTargetMachine();
  // XCOFF hasn't implemented lowerRelativeReference, disable non-ELF for now.
  if (!TM.isELFv2ABI())
    return false;
  return BaseT::shouldBuildRelLookupTables();
}

bool PPCTTIImpl::getTgtMemIntrinsic(IntrinsicInst *Inst,
                                    MemIntrinsicInfo &Info) {
  switch (Inst->getIntrinsicID()) {
  case Intrinsic::ppc_altivec_lvx:
  case Intrinsic::ppc_altivec_lvxl:
  case Intrinsic::ppc_altivec_lvebx:
  case Intrinsic::ppc_altivec_lvehx:
  case Intrinsic::ppc_altivec_lvewx:
  case Intrinsic::ppc_vsx_lxvd2x:
  case Intrinsic::ppc_vsx_lxvw4x:
  case Intrinsic::ppc_vsx_lxvd2x_be:
  case Intrinsic::ppc_vsx_lxvw4x_be:
  case Intrinsic::ppc_vsx_lxvl:
  case Intrinsic::ppc_vsx_lxvll:
  case Intrinsic::ppc_vsx_lxvp: {
    Info.PtrVal = Inst->getArgOperand(0);
    Info.ReadMem = true;
    Info.WriteMem = false;
    return true;
  }
  case Intrinsic::ppc_altivec_stvx:
  case Intrinsic::ppc_altivec_stvxl:
  case Intrinsic::ppc_altivec_stvebx:
  case Intrinsic::ppc_altivec_stvehx:
  case Intrinsic::ppc_altivec_stvewx:
  case Intrinsic::ppc_vsx_stxvd2x:
  case Intrinsic::ppc_vsx_stxvw4x:
  case Intrinsic::ppc_vsx_stxvd2x_be:
  case Intrinsic::ppc_vsx_stxvw4x_be:
  case Intrinsic::ppc_vsx_stxvl:
  case Intrinsic::ppc_vsx_stxvll:
  case Intrinsic::ppc_vsx_stxvp: {
    Info.PtrVal = Inst->getArgOperand(1);
    Info.ReadMem = false;
    Info.WriteMem = true;
    return true;
  }
  case Intrinsic::ppc_stbcx:
  case Intrinsic::ppc_sthcx:
  case Intrinsic::ppc_stdcx:
  case Intrinsic::ppc_stwcx: {
    Info.PtrVal = Inst->getArgOperand(0);
    Info.ReadMem = false;
    Info.WriteMem = true;
    return true;
  }
  default:
    break;
  }

  return false;
}

bool PPCTTIImpl::hasActiveVectorLength(unsigned Opcode, Type *DataType,
                                       Align Alignment) const {
  // Only load and stores instructions can have variable vector length on Power.
  if (Opcode != Instruction::Load && Opcode != Instruction::Store)
    return false;
  // Loads/stores with length instructions use bits 0-7 of the GPR operand and
  // therefore cannot be used in 32-bit mode.
  if ((!ST->hasP9Vector() && !ST->hasP10Vector()) || !ST->isPPC64())
    return false;
  if (isa<FixedVectorType>(DataType)) {
    unsigned VecWidth = DataType->getPrimitiveSizeInBits();
    return VecWidth == 128;
  }
  Type *ScalarTy = DataType->getScalarType();

  if (ScalarTy->isPointerTy())
    return true;

  if (ScalarTy->isFloatTy() || ScalarTy->isDoubleTy())
    return true;

  if (!ScalarTy->isIntegerTy())
    return false;

  unsigned IntWidth = ScalarTy->getIntegerBitWidth();
  return IntWidth == 8 || IntWidth == 16 || IntWidth == 32 || IntWidth == 64;
}

InstructionCost PPCTTIImpl::getVPMemoryOpCost(unsigned Opcode, Type *Src,
                                              Align Alignment,
                                              unsigned AddressSpace,
                                              TTI::TargetCostKind CostKind,
                                              const Instruction *I) {
  InstructionCost Cost = BaseT::getVPMemoryOpCost(Opcode, Src, Alignment,
                                                  AddressSpace, CostKind, I);
  if (TLI->getValueType(DL, Src, true) == MVT::Other)
    return Cost;
  // TODO: Handle other cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput)
    return Cost;

  assert((Opcode == Instruction::Load || Opcode == Instruction::Store) &&
         "Invalid Opcode");

  auto *SrcVTy = dyn_cast<FixedVectorType>(Src);
  assert(SrcVTy && "Expected a vector type for VP memory operations");

  if (hasActiveVectorLength(Opcode, Src, Alignment)) {
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(SrcVTy);

    InstructionCost CostFactor =
        vectorCostAdjustmentFactor(Opcode, Src, nullptr);
    if (!CostFactor.isValid())
      return InstructionCost::getMax();

    InstructionCost Cost = LT.first * CostFactor;
    assert(Cost.isValid() && "Expected valid cost");

    // On P9 but not on P10, if the op is misaligned then it will cause a
    // pipeline flush. Otherwise the VSX masked memops cost the same as unmasked
    // ones.
    const Align DesiredAlignment(16);
    if (Alignment >= DesiredAlignment || ST->getCPUDirective() != PPC::DIR_PWR9)
      return Cost;

    // Since alignment may be under estimated, we try to compute the probability
    // that the actual address is aligned to the desired boundary. For example
    // an 8-byte aligned load is assumed to be actually 16-byte aligned half the
    // time, while a 4-byte aligned load has a 25% chance of being 16-byte
    // aligned.
    float AlignmentProb = ((float)Alignment.value()) / DesiredAlignment.value();
    float MisalignmentProb = 1.0 - AlignmentProb;
    return (MisalignmentProb * P9PipelineFlushEstimate) +
           (AlignmentProb * *Cost.getValue());
  }

  // Usually we should not get to this point, but the following is an attempt to
  // model the cost of legalization. Currently we can only lower intrinsics with
  // evl but no mask, on Power 9/10. Otherwise, we must scalarize.
  return getMaskedMemoryOpCost(Opcode, Src, Alignment, AddressSpace, CostKind);
}

bool PPCTTIImpl::supportsTailCallFor(const CallBase *CB) const {
  return TLI->supportsTailCallFor(CB);
}
