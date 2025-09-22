//===- ARMTargetTransformInfo.cpp - ARM specific TTI ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ARMTargetTransformInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Vectorize/LoopVectorizationLegality.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "armtti"

static cl::opt<bool> EnableMaskedLoadStores(
  "enable-arm-maskedldst", cl::Hidden, cl::init(true),
  cl::desc("Enable the generation of masked loads and stores"));

static cl::opt<bool> DisableLowOverheadLoops(
  "disable-arm-loloops", cl::Hidden, cl::init(false),
  cl::desc("Disable the generation of low-overhead loops"));

static cl::opt<bool>
    AllowWLSLoops("allow-arm-wlsloops", cl::Hidden, cl::init(true),
                  cl::desc("Enable the generation of WLS loops"));

extern cl::opt<TailPredication::Mode> EnableTailPredication;

extern cl::opt<bool> EnableMaskedGatherScatters;

extern cl::opt<unsigned> MVEMaxSupportedInterleaveFactor;

/// Convert a vector load intrinsic into a simple llvm load instruction.
/// This is beneficial when the underlying object being addressed comes
/// from a constant, since we get constant-folding for free.
static Value *simplifyNeonVld1(const IntrinsicInst &II, unsigned MemAlign,
                               InstCombiner::BuilderTy &Builder) {
  auto *IntrAlign = dyn_cast<ConstantInt>(II.getArgOperand(1));

  if (!IntrAlign)
    return nullptr;

  unsigned Alignment = IntrAlign->getLimitedValue() < MemAlign
                           ? MemAlign
                           : IntrAlign->getLimitedValue();

  if (!isPowerOf2_32(Alignment))
    return nullptr;

  auto *BCastInst = Builder.CreateBitCast(II.getArgOperand(0),
                                          PointerType::get(II.getType(), 0));
  return Builder.CreateAlignedLoad(II.getType(), BCastInst, Align(Alignment));
}

bool ARMTTIImpl::areInlineCompatible(const Function *Caller,
                                     const Function *Callee) const {
  const TargetMachine &TM = getTLI()->getTargetMachine();
  const FeatureBitset &CallerBits =
      TM.getSubtargetImpl(*Caller)->getFeatureBits();
  const FeatureBitset &CalleeBits =
      TM.getSubtargetImpl(*Callee)->getFeatureBits();

  // To inline a callee, all features not in the allowed list must match exactly.
  bool MatchExact = (CallerBits & ~InlineFeaturesAllowed) ==
                    (CalleeBits & ~InlineFeaturesAllowed);
  // For features in the allowed list, the callee's features must be a subset of
  // the callers'.
  bool MatchSubset = ((CallerBits & CalleeBits) & InlineFeaturesAllowed) ==
                     (CalleeBits & InlineFeaturesAllowed);
  return MatchExact && MatchSubset;
}

TTI::AddressingModeKind
ARMTTIImpl::getPreferredAddressingMode(const Loop *L,
                                       ScalarEvolution *SE) const {
  if (ST->hasMVEIntegerOps())
    return TTI::AMK_PostIndexed;

  if (L->getHeader()->getParent()->hasOptSize())
    return TTI::AMK_None;

  if (ST->isMClass() && ST->isThumb2() &&
      L->getNumBlocks() == 1)
    return TTI::AMK_PreIndexed;

  return TTI::AMK_None;
}

std::optional<Instruction *>
ARMTTIImpl::instCombineIntrinsic(InstCombiner &IC, IntrinsicInst &II) const {
  using namespace PatternMatch;
  Intrinsic::ID IID = II.getIntrinsicID();
  switch (IID) {
  default:
    break;
  case Intrinsic::arm_neon_vld1: {
    Align MemAlign =
        getKnownAlignment(II.getArgOperand(0), IC.getDataLayout(), &II,
                          &IC.getAssumptionCache(), &IC.getDominatorTree());
    if (Value *V = simplifyNeonVld1(II, MemAlign.value(), IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;
  }

  case Intrinsic::arm_neon_vld2:
  case Intrinsic::arm_neon_vld3:
  case Intrinsic::arm_neon_vld4:
  case Intrinsic::arm_neon_vld2lane:
  case Intrinsic::arm_neon_vld3lane:
  case Intrinsic::arm_neon_vld4lane:
  case Intrinsic::arm_neon_vst1:
  case Intrinsic::arm_neon_vst2:
  case Intrinsic::arm_neon_vst3:
  case Intrinsic::arm_neon_vst4:
  case Intrinsic::arm_neon_vst2lane:
  case Intrinsic::arm_neon_vst3lane:
  case Intrinsic::arm_neon_vst4lane: {
    Align MemAlign =
        getKnownAlignment(II.getArgOperand(0), IC.getDataLayout(), &II,
                          &IC.getAssumptionCache(), &IC.getDominatorTree());
    unsigned AlignArg = II.arg_size() - 1;
    Value *AlignArgOp = II.getArgOperand(AlignArg);
    MaybeAlign Align = cast<ConstantInt>(AlignArgOp)->getMaybeAlignValue();
    if (Align && *Align < MemAlign) {
      return IC.replaceOperand(
          II, AlignArg,
          ConstantInt::get(Type::getInt32Ty(II.getContext()), MemAlign.value(),
                           false));
    }
    break;
  }

  case Intrinsic::arm_mve_pred_i2v: {
    Value *Arg = II.getArgOperand(0);
    Value *ArgArg;
    if (match(Arg, PatternMatch::m_Intrinsic<Intrinsic::arm_mve_pred_v2i>(
                       PatternMatch::m_Value(ArgArg))) &&
        II.getType() == ArgArg->getType()) {
      return IC.replaceInstUsesWith(II, ArgArg);
    }
    Constant *XorMask;
    if (match(Arg, m_Xor(PatternMatch::m_Intrinsic<Intrinsic::arm_mve_pred_v2i>(
                             PatternMatch::m_Value(ArgArg)),
                         PatternMatch::m_Constant(XorMask))) &&
        II.getType() == ArgArg->getType()) {
      if (auto *CI = dyn_cast<ConstantInt>(XorMask)) {
        if (CI->getValue().trunc(16).isAllOnes()) {
          auto TrueVector = IC.Builder.CreateVectorSplat(
              cast<FixedVectorType>(II.getType())->getNumElements(),
              IC.Builder.getTrue());
          return BinaryOperator::Create(Instruction::Xor, ArgArg, TrueVector);
        }
      }
    }
    KnownBits ScalarKnown(32);
    if (IC.SimplifyDemandedBits(&II, 0, APInt::getLowBitsSet(32, 16),
                                ScalarKnown)) {
      return &II;
    }
    break;
  }
  case Intrinsic::arm_mve_pred_v2i: {
    Value *Arg = II.getArgOperand(0);
    Value *ArgArg;
    if (match(Arg, PatternMatch::m_Intrinsic<Intrinsic::arm_mve_pred_i2v>(
                       PatternMatch::m_Value(ArgArg)))) {
      return IC.replaceInstUsesWith(II, ArgArg);
    }

    if (II.getMetadata(LLVMContext::MD_range))
      break;

    ConstantRange Range(APInt(32, 0), APInt(32, 0x10000));

    if (auto CurrentRange = II.getRange()) {
      Range = Range.intersectWith(*CurrentRange);
      if (Range == CurrentRange)
        break;
    }

    II.addRangeRetAttr(Range);
    II.addRetAttr(Attribute::NoUndef);
    return &II;
  }
  case Intrinsic::arm_mve_vadc:
  case Intrinsic::arm_mve_vadc_predicated: {
    unsigned CarryOp =
        (II.getIntrinsicID() == Intrinsic::arm_mve_vadc_predicated) ? 3 : 2;
    assert(II.getArgOperand(CarryOp)->getType()->getScalarSizeInBits() == 32 &&
           "Bad type for intrinsic!");

    KnownBits CarryKnown(32);
    if (IC.SimplifyDemandedBits(&II, CarryOp, APInt::getOneBitSet(32, 29),
                                CarryKnown)) {
      return &II;
    }
    break;
  }
  case Intrinsic::arm_mve_vmldava: {
    Instruction *I = cast<Instruction>(&II);
    if (I->hasOneUse()) {
      auto *User = cast<Instruction>(*I->user_begin());
      Value *OpZ;
      if (match(User, m_c_Add(m_Specific(I), m_Value(OpZ))) &&
          match(I->getOperand(3), m_Zero())) {
        Value *OpX = I->getOperand(4);
        Value *OpY = I->getOperand(5);
        Type *OpTy = OpX->getType();

        IC.Builder.SetInsertPoint(User);
        Value *V =
            IC.Builder.CreateIntrinsic(Intrinsic::arm_mve_vmldava, {OpTy},
                                       {I->getOperand(0), I->getOperand(1),
                                        I->getOperand(2), OpZ, OpX, OpY});

        IC.replaceInstUsesWith(*User, V);
        return IC.eraseInstFromFunction(*User);
      }
    }
    return std::nullopt;
  }
  }
  return std::nullopt;
}

std::optional<Value *> ARMTTIImpl::simplifyDemandedVectorEltsIntrinsic(
    InstCombiner &IC, IntrinsicInst &II, APInt OrigDemandedElts,
    APInt &UndefElts, APInt &UndefElts2, APInt &UndefElts3,
    std::function<void(Instruction *, unsigned, APInt, APInt &)>
        SimplifyAndSetOp) const {

  // Compute the demanded bits for a narrowing MVE intrinsic. The TopOpc is the
  // opcode specifying a Top/Bottom instruction, which can change between
  // instructions.
  auto SimplifyNarrowInstrTopBottom =[&](unsigned TopOpc) {
    unsigned NumElts = cast<FixedVectorType>(II.getType())->getNumElements();
    unsigned IsTop = cast<ConstantInt>(II.getOperand(TopOpc))->getZExtValue();

    // The only odd/even lanes of operand 0 will only be demanded depending
    // on whether this is a top/bottom instruction.
    APInt DemandedElts =
        APInt::getSplat(NumElts, IsTop ? APInt::getLowBitsSet(2, 1)
                                       : APInt::getHighBitsSet(2, 1));
    SimplifyAndSetOp(&II, 0, OrigDemandedElts & DemandedElts, UndefElts);
    // The other lanes will be defined from the inserted elements.
    UndefElts &= APInt::getSplat(NumElts, IsTop ? APInt::getLowBitsSet(2, 1)
                                                : APInt::getHighBitsSet(2, 1));
    return std::nullopt;
  };

  switch (II.getIntrinsicID()) {
  default:
    break;
  case Intrinsic::arm_mve_vcvt_narrow:
    SimplifyNarrowInstrTopBottom(2);
    break;
  case Intrinsic::arm_mve_vqmovn:
    SimplifyNarrowInstrTopBottom(4);
    break;
  case Intrinsic::arm_mve_vshrn:
    SimplifyNarrowInstrTopBottom(7);
    break;
  }

  return std::nullopt;
}

InstructionCost ARMTTIImpl::getIntImmCost(const APInt &Imm, Type *Ty,
                                          TTI::TargetCostKind CostKind) {
  assert(Ty->isIntegerTy());

 unsigned Bits = Ty->getPrimitiveSizeInBits();
 if (Bits == 0 || Imm.getActiveBits() >= 64)
   return 4;

  int64_t SImmVal = Imm.getSExtValue();
  uint64_t ZImmVal = Imm.getZExtValue();
  if (!ST->isThumb()) {
    if ((SImmVal >= 0 && SImmVal < 65536) ||
        (ARM_AM::getSOImmVal(ZImmVal) != -1) ||
        (ARM_AM::getSOImmVal(~ZImmVal) != -1))
      return 1;
    return ST->hasV6T2Ops() ? 2 : 3;
  }
  if (ST->isThumb2()) {
    if ((SImmVal >= 0 && SImmVal < 65536) ||
        (ARM_AM::getT2SOImmVal(ZImmVal) != -1) ||
        (ARM_AM::getT2SOImmVal(~ZImmVal) != -1))
      return 1;
    return ST->hasV6T2Ops() ? 2 : 3;
  }
  // Thumb1, any i8 imm cost 1.
  if (Bits == 8 || (SImmVal >= 0 && SImmVal < 256))
    return 1;
  if ((~SImmVal < 256) || ARM_AM::isThumbImmShiftedVal(ZImmVal))
    return 2;
  // Load from constantpool.
  return 3;
}

// Constants smaller than 256 fit in the immediate field of
// Thumb1 instructions so we return a zero cost and 1 otherwise.
InstructionCost ARMTTIImpl::getIntImmCodeSizeCost(unsigned Opcode, unsigned Idx,
                                                  const APInt &Imm, Type *Ty) {
  if (Imm.isNonNegative() && Imm.getLimitedValue() < 256)
    return 0;

  return 1;
}

// Checks whether Inst is part of a min(max()) or max(min()) pattern
// that will match to an SSAT instruction. Returns the instruction being
// saturated, or null if no saturation pattern was found.
static Value *isSSATMinMaxPattern(Instruction *Inst, const APInt &Imm) {
  Value *LHS, *RHS;
  ConstantInt *C;
  SelectPatternFlavor InstSPF = matchSelectPattern(Inst, LHS, RHS).Flavor;

  if (InstSPF == SPF_SMAX &&
      PatternMatch::match(RHS, PatternMatch::m_ConstantInt(C)) &&
      C->getValue() == Imm && Imm.isNegative() && Imm.isNegatedPowerOf2()) {

    auto isSSatMin = [&](Value *MinInst) {
      if (isa<SelectInst>(MinInst)) {
        Value *MinLHS, *MinRHS;
        ConstantInt *MinC;
        SelectPatternFlavor MinSPF =
            matchSelectPattern(MinInst, MinLHS, MinRHS).Flavor;
        if (MinSPF == SPF_SMIN &&
            PatternMatch::match(MinRHS, PatternMatch::m_ConstantInt(MinC)) &&
            MinC->getValue() == ((-Imm) - 1))
          return true;
      }
      return false;
    };

    if (isSSatMin(Inst->getOperand(1)))
      return cast<Instruction>(Inst->getOperand(1))->getOperand(1);
    if (Inst->hasNUses(2) &&
        (isSSatMin(*Inst->user_begin()) || isSSatMin(*(++Inst->user_begin()))))
      return Inst->getOperand(1);
  }
  return nullptr;
}

// Look for a FP Saturation pattern, where the instruction can be simplified to
// a fptosi.sat. max(min(fptosi)). The constant in this case is always free.
static bool isFPSatMinMaxPattern(Instruction *Inst, const APInt &Imm) {
  if (Imm.getBitWidth() != 64 ||
      Imm != APInt::getHighBitsSet(64, 33)) // -2147483648
    return false;
  Value *FP = isSSATMinMaxPattern(Inst, Imm);
  if (!FP && isa<ICmpInst>(Inst) && Inst->hasOneUse())
    FP = isSSATMinMaxPattern(cast<Instruction>(*Inst->user_begin()), Imm);
  if (!FP)
    return false;
  return isa<FPToSIInst>(FP);
}

InstructionCost ARMTTIImpl::getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                              const APInt &Imm, Type *Ty,
                                              TTI::TargetCostKind CostKind,
                                              Instruction *Inst) {
  // Division by a constant can be turned into multiplication, but only if we
  // know it's constant. So it's not so much that the immediate is cheap (it's
  // not), but that the alternative is worse.
  // FIXME: this is probably unneeded with GlobalISel.
  if ((Opcode == Instruction::SDiv || Opcode == Instruction::UDiv ||
       Opcode == Instruction::SRem || Opcode == Instruction::URem) &&
      Idx == 1)
    return 0;

  // Leave any gep offsets for the CodeGenPrepare, which will do a better job at
  // splitting any large offsets.
  if (Opcode == Instruction::GetElementPtr && Idx != 0)
    return 0;

  if (Opcode == Instruction::And) {
    // UXTB/UXTH
    if (Imm == 255 || Imm == 65535)
      return 0;
    // Conversion to BIC is free, and means we can use ~Imm instead.
    return std::min(getIntImmCost(Imm, Ty, CostKind),
                    getIntImmCost(~Imm, Ty, CostKind));
  }

  if (Opcode == Instruction::Add)
    // Conversion to SUB is free, and means we can use -Imm instead.
    return std::min(getIntImmCost(Imm, Ty, CostKind),
                    getIntImmCost(-Imm, Ty, CostKind));

  if (Opcode == Instruction::ICmp && Imm.isNegative() &&
      Ty->getIntegerBitWidth() == 32) {
    int64_t NegImm = -Imm.getSExtValue();
    if (ST->isThumb2() && NegImm < 1<<12)
      // icmp X, #-C -> cmn X, #C
      return 0;
    if (ST->isThumb() && NegImm < 1<<8)
      // icmp X, #-C -> adds X, #C
      return 0;
  }

  // xor a, -1 can always be folded to MVN
  if (Opcode == Instruction::Xor && Imm.isAllOnes())
    return 0;

  // Ensures negative constant of min(max()) or max(min()) patterns that
  // match to SSAT instructions don't get hoisted
  if (Inst && ((ST->hasV6Ops() && !ST->isThumb()) || ST->isThumb2()) &&
      Ty->getIntegerBitWidth() <= 32) {
    if (isSSATMinMaxPattern(Inst, Imm) ||
        (isa<ICmpInst>(Inst) && Inst->hasOneUse() &&
         isSSATMinMaxPattern(cast<Instruction>(*Inst->user_begin()), Imm)))
      return 0;
  }

  if (Inst && ST->hasVFP2Base() && isFPSatMinMaxPattern(Inst, Imm))
    return 0;

  // We can convert <= -1 to < 0, which is generally quite cheap.
  if (Inst && Opcode == Instruction::ICmp && Idx == 1 && Imm.isAllOnes()) {
    ICmpInst::Predicate Pred = cast<ICmpInst>(Inst)->getPredicate();
    if (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SLE)
      return std::min(getIntImmCost(Imm, Ty, CostKind),
                      getIntImmCost(Imm + 1, Ty, CostKind));
  }

  return getIntImmCost(Imm, Ty, CostKind);
}

InstructionCost ARMTTIImpl::getCFInstrCost(unsigned Opcode,
                                           TTI::TargetCostKind CostKind,
                                           const Instruction *I) {
  if (CostKind == TTI::TCK_RecipThroughput &&
      (ST->hasNEON() || ST->hasMVEIntegerOps())) {
    // FIXME: The vectorizer is highly sensistive to the cost of these
    // instructions, which suggests that it may be using the costs incorrectly.
    // But, for now, just make them free to avoid performance regressions for
    // vector targets.
    return 0;
  }
  return BaseT::getCFInstrCost(Opcode, CostKind, I);
}

InstructionCost ARMTTIImpl::getCastInstrCost(unsigned Opcode, Type *Dst,
                                             Type *Src,
                                             TTI::CastContextHint CCH,
                                             TTI::TargetCostKind CostKind,
                                             const Instruction *I) {
  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  assert(ISD && "Invalid opcode");

  // TODO: Allow non-throughput costs that aren't binary.
  auto AdjustCost = [&CostKind](InstructionCost Cost) -> InstructionCost {
    if (CostKind != TTI::TCK_RecipThroughput)
      return Cost == 0 ? 0 : 1;
    return Cost;
  };
  auto IsLegalFPType = [this](EVT VT) {
    EVT EltVT = VT.getScalarType();
    return (EltVT == MVT::f32 && ST->hasVFP2Base()) ||
            (EltVT == MVT::f64 && ST->hasFP64()) ||
            (EltVT == MVT::f16 && ST->hasFullFP16());
  };

  EVT SrcTy = TLI->getValueType(DL, Src);
  EVT DstTy = TLI->getValueType(DL, Dst);

  if (!SrcTy.isSimple() || !DstTy.isSimple())
    return AdjustCost(
        BaseT::getCastInstrCost(Opcode, Dst, Src, CCH, CostKind, I));

  // Extending masked load/Truncating masked stores is expensive because we
  // currently don't split them. This means that we'll likely end up
  // loading/storing each element individually (hence the high cost).
  if ((ST->hasMVEIntegerOps() &&
       (Opcode == Instruction::Trunc || Opcode == Instruction::ZExt ||
        Opcode == Instruction::SExt)) ||
      (ST->hasMVEFloatOps() &&
       (Opcode == Instruction::FPExt || Opcode == Instruction::FPTrunc) &&
       IsLegalFPType(SrcTy) && IsLegalFPType(DstTy)))
    if (CCH == TTI::CastContextHint::Masked && DstTy.getSizeInBits() > 128)
      return 2 * DstTy.getVectorNumElements() *
             ST->getMVEVectorCostFactor(CostKind);

  // The extend of other kinds of load is free
  if (CCH == TTI::CastContextHint::Normal ||
      CCH == TTI::CastContextHint::Masked) {
    static const TypeConversionCostTblEntry LoadConversionTbl[] = {
        {ISD::SIGN_EXTEND, MVT::i32, MVT::i16, 0},
        {ISD::ZERO_EXTEND, MVT::i32, MVT::i16, 0},
        {ISD::SIGN_EXTEND, MVT::i32, MVT::i8, 0},
        {ISD::ZERO_EXTEND, MVT::i32, MVT::i8, 0},
        {ISD::SIGN_EXTEND, MVT::i16, MVT::i8, 0},
        {ISD::ZERO_EXTEND, MVT::i16, MVT::i8, 0},
        {ISD::SIGN_EXTEND, MVT::i64, MVT::i32, 1},
        {ISD::ZERO_EXTEND, MVT::i64, MVT::i32, 1},
        {ISD::SIGN_EXTEND, MVT::i64, MVT::i16, 1},
        {ISD::ZERO_EXTEND, MVT::i64, MVT::i16, 1},
        {ISD::SIGN_EXTEND, MVT::i64, MVT::i8, 1},
        {ISD::ZERO_EXTEND, MVT::i64, MVT::i8, 1},
    };
    if (const auto *Entry = ConvertCostTableLookup(
            LoadConversionTbl, ISD, DstTy.getSimpleVT(), SrcTy.getSimpleVT()))
      return AdjustCost(Entry->Cost);

    static const TypeConversionCostTblEntry MVELoadConversionTbl[] = {
        {ISD::SIGN_EXTEND, MVT::v4i32, MVT::v4i16, 0},
        {ISD::ZERO_EXTEND, MVT::v4i32, MVT::v4i16, 0},
        {ISD::SIGN_EXTEND, MVT::v4i32, MVT::v4i8, 0},
        {ISD::ZERO_EXTEND, MVT::v4i32, MVT::v4i8, 0},
        {ISD::SIGN_EXTEND, MVT::v8i16, MVT::v8i8, 0},
        {ISD::ZERO_EXTEND, MVT::v8i16, MVT::v8i8, 0},
        // The following extend from a legal type to an illegal type, so need to
        // split the load. This introduced an extra load operation, but the
        // extend is still "free".
        {ISD::SIGN_EXTEND, MVT::v8i32, MVT::v8i16, 1},
        {ISD::ZERO_EXTEND, MVT::v8i32, MVT::v8i16, 1},
        {ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i8, 3},
        {ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i8, 3},
        {ISD::SIGN_EXTEND, MVT::v16i16, MVT::v16i8, 1},
        {ISD::ZERO_EXTEND, MVT::v16i16, MVT::v16i8, 1},
    };
    if (SrcTy.isVector() && ST->hasMVEIntegerOps()) {
      if (const auto *Entry =
              ConvertCostTableLookup(MVELoadConversionTbl, ISD,
                                     DstTy.getSimpleVT(), SrcTy.getSimpleVT()))
        return Entry->Cost * ST->getMVEVectorCostFactor(CostKind);
    }

    static const TypeConversionCostTblEntry MVEFLoadConversionTbl[] = {
        // FPExtends are similar but also require the VCVT instructions.
        {ISD::FP_EXTEND, MVT::v4f32, MVT::v4f16, 1},
        {ISD::FP_EXTEND, MVT::v8f32, MVT::v8f16, 3},
    };
    if (SrcTy.isVector() && ST->hasMVEFloatOps()) {
      if (const auto *Entry =
              ConvertCostTableLookup(MVEFLoadConversionTbl, ISD,
                                     DstTy.getSimpleVT(), SrcTy.getSimpleVT()))
        return Entry->Cost * ST->getMVEVectorCostFactor(CostKind);
    }

    // The truncate of a store is free. This is the mirror of extends above.
    static const TypeConversionCostTblEntry MVEStoreConversionTbl[] = {
        {ISD::TRUNCATE, MVT::v4i32, MVT::v4i16, 0},
        {ISD::TRUNCATE, MVT::v4i32, MVT::v4i8, 0},
        {ISD::TRUNCATE, MVT::v8i16, MVT::v8i8, 0},
        {ISD::TRUNCATE, MVT::v8i32, MVT::v8i16, 1},
        {ISD::TRUNCATE, MVT::v8i32, MVT::v8i8, 1},
        {ISD::TRUNCATE, MVT::v16i32, MVT::v16i8, 3},
        {ISD::TRUNCATE, MVT::v16i16, MVT::v16i8, 1},
    };
    if (SrcTy.isVector() && ST->hasMVEIntegerOps()) {
      if (const auto *Entry =
              ConvertCostTableLookup(MVEStoreConversionTbl, ISD,
                                     SrcTy.getSimpleVT(), DstTy.getSimpleVT()))
        return Entry->Cost * ST->getMVEVectorCostFactor(CostKind);
    }

    static const TypeConversionCostTblEntry MVEFStoreConversionTbl[] = {
        {ISD::FP_ROUND, MVT::v4f32, MVT::v4f16, 1},
        {ISD::FP_ROUND, MVT::v8f32, MVT::v8f16, 3},
    };
    if (SrcTy.isVector() && ST->hasMVEFloatOps()) {
      if (const auto *Entry =
              ConvertCostTableLookup(MVEFStoreConversionTbl, ISD,
                                     SrcTy.getSimpleVT(), DstTy.getSimpleVT()))
        return Entry->Cost * ST->getMVEVectorCostFactor(CostKind);
    }
  }

  // NEON vector operations that can extend their inputs.
  if ((ISD == ISD::SIGN_EXTEND || ISD == ISD::ZERO_EXTEND) &&
      I && I->hasOneUse() && ST->hasNEON() && SrcTy.isVector()) {
    static const TypeConversionCostTblEntry NEONDoubleWidthTbl[] = {
      // vaddl
      { ISD::ADD, MVT::v4i32, MVT::v4i16, 0 },
      { ISD::ADD, MVT::v8i16, MVT::v8i8,  0 },
      // vsubl
      { ISD::SUB, MVT::v4i32, MVT::v4i16, 0 },
      { ISD::SUB, MVT::v8i16, MVT::v8i8,  0 },
      // vmull
      { ISD::MUL, MVT::v4i32, MVT::v4i16, 0 },
      { ISD::MUL, MVT::v8i16, MVT::v8i8,  0 },
      // vshll
      { ISD::SHL, MVT::v4i32, MVT::v4i16, 0 },
      { ISD::SHL, MVT::v8i16, MVT::v8i8,  0 },
    };

    auto *User = cast<Instruction>(*I->user_begin());
    int UserISD = TLI->InstructionOpcodeToISD(User->getOpcode());
    if (auto *Entry = ConvertCostTableLookup(NEONDoubleWidthTbl, UserISD,
                                             DstTy.getSimpleVT(),
                                             SrcTy.getSimpleVT())) {
      return AdjustCost(Entry->Cost);
    }
  }

  // Single to/from double precision conversions.
  if (Src->isVectorTy() && ST->hasNEON() &&
      ((ISD == ISD::FP_ROUND && SrcTy.getScalarType() == MVT::f64 &&
        DstTy.getScalarType() == MVT::f32) ||
       (ISD == ISD::FP_EXTEND && SrcTy.getScalarType() == MVT::f32 &&
        DstTy.getScalarType() == MVT::f64))) {
    static const CostTblEntry NEONFltDblTbl[] = {
        // Vector fptrunc/fpext conversions.
        {ISD::FP_ROUND, MVT::v2f64, 2},
        {ISD::FP_EXTEND, MVT::v2f32, 2},
        {ISD::FP_EXTEND, MVT::v4f32, 4}};

    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Src);
    if (const auto *Entry = CostTableLookup(NEONFltDblTbl, ISD, LT.second))
      return AdjustCost(LT.first * Entry->Cost);
  }

  // Some arithmetic, load and store operations have specific instructions
  // to cast up/down their types automatically at no extra cost.
  // TODO: Get these tables to know at least what the related operations are.
  static const TypeConversionCostTblEntry NEONVectorConversionTbl[] = {
    { ISD::SIGN_EXTEND, MVT::v4i32, MVT::v4i16, 1 },
    { ISD::ZERO_EXTEND, MVT::v4i32, MVT::v4i16, 1 },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v2i32, 1 },
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v2i32, 1 },
    { ISD::TRUNCATE,    MVT::v4i32, MVT::v4i64, 0 },
    { ISD::TRUNCATE,    MVT::v4i16, MVT::v4i32, 1 },

    // The number of vmovl instructions for the extension.
    { ISD::SIGN_EXTEND, MVT::v8i16, MVT::v8i8,  1 },
    { ISD::ZERO_EXTEND, MVT::v8i16, MVT::v8i8,  1 },
    { ISD::SIGN_EXTEND, MVT::v4i32, MVT::v4i8,  2 },
    { ISD::ZERO_EXTEND, MVT::v4i32, MVT::v4i8,  2 },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v2i8,  3 },
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v2i8,  3 },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v2i16, 2 },
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v2i16, 2 },
    { ISD::SIGN_EXTEND, MVT::v4i64, MVT::v4i16, 3 },
    { ISD::ZERO_EXTEND, MVT::v4i64, MVT::v4i16, 3 },
    { ISD::SIGN_EXTEND, MVT::v8i32, MVT::v8i8, 3 },
    { ISD::ZERO_EXTEND, MVT::v8i32, MVT::v8i8, 3 },
    { ISD::SIGN_EXTEND, MVT::v8i64, MVT::v8i8, 7 },
    { ISD::ZERO_EXTEND, MVT::v8i64, MVT::v8i8, 7 },
    { ISD::SIGN_EXTEND, MVT::v8i64, MVT::v8i16, 6 },
    { ISD::ZERO_EXTEND, MVT::v8i64, MVT::v8i16, 6 },
    { ISD::SIGN_EXTEND, MVT::v16i32, MVT::v16i8, 6 },
    { ISD::ZERO_EXTEND, MVT::v16i32, MVT::v16i8, 6 },

    // Operations that we legalize using splitting.
    { ISD::TRUNCATE,    MVT::v16i8, MVT::v16i32, 6 },
    { ISD::TRUNCATE,    MVT::v8i8, MVT::v8i32, 3 },

    // Vector float <-> i32 conversions.
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i32, 1 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i32, 1 },

    { ISD::SINT_TO_FP,  MVT::v2f32, MVT::v2i8, 3 },
    { ISD::UINT_TO_FP,  MVT::v2f32, MVT::v2i8, 3 },
    { ISD::SINT_TO_FP,  MVT::v2f32, MVT::v2i16, 2 },
    { ISD::UINT_TO_FP,  MVT::v2f32, MVT::v2i16, 2 },
    { ISD::SINT_TO_FP,  MVT::v2f32, MVT::v2i32, 1 },
    { ISD::UINT_TO_FP,  MVT::v2f32, MVT::v2i32, 1 },
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i1, 3 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i1, 3 },
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i8, 3 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i8, 3 },
    { ISD::SINT_TO_FP,  MVT::v4f32, MVT::v4i16, 2 },
    { ISD::UINT_TO_FP,  MVT::v4f32, MVT::v4i16, 2 },
    { ISD::SINT_TO_FP,  MVT::v8f32, MVT::v8i16, 4 },
    { ISD::UINT_TO_FP,  MVT::v8f32, MVT::v8i16, 4 },
    { ISD::SINT_TO_FP,  MVT::v8f32, MVT::v8i32, 2 },
    { ISD::UINT_TO_FP,  MVT::v8f32, MVT::v8i32, 2 },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i16, 8 },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i16, 8 },
    { ISD::SINT_TO_FP,  MVT::v16f32, MVT::v16i32, 4 },
    { ISD::UINT_TO_FP,  MVT::v16f32, MVT::v16i32, 4 },

    { ISD::FP_TO_SINT,  MVT::v4i32, MVT::v4f32, 1 },
    { ISD::FP_TO_UINT,  MVT::v4i32, MVT::v4f32, 1 },
    { ISD::FP_TO_SINT,  MVT::v4i8, MVT::v4f32, 3 },
    { ISD::FP_TO_UINT,  MVT::v4i8, MVT::v4f32, 3 },
    { ISD::FP_TO_SINT,  MVT::v4i16, MVT::v4f32, 2 },
    { ISD::FP_TO_UINT,  MVT::v4i16, MVT::v4f32, 2 },

    // Vector double <-> i32 conversions.
    { ISD::SINT_TO_FP,  MVT::v2f64, MVT::v2i32, 2 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i32, 2 },

    { ISD::SINT_TO_FP,  MVT::v2f64, MVT::v2i8, 4 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i8, 4 },
    { ISD::SINT_TO_FP,  MVT::v2f64, MVT::v2i16, 3 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i16, 3 },
    { ISD::SINT_TO_FP,  MVT::v2f64, MVT::v2i32, 2 },
    { ISD::UINT_TO_FP,  MVT::v2f64, MVT::v2i32, 2 },

    { ISD::FP_TO_SINT,  MVT::v2i32, MVT::v2f64, 2 },
    { ISD::FP_TO_UINT,  MVT::v2i32, MVT::v2f64, 2 },
    { ISD::FP_TO_SINT,  MVT::v8i16, MVT::v8f32, 4 },
    { ISD::FP_TO_UINT,  MVT::v8i16, MVT::v8f32, 4 },
    { ISD::FP_TO_SINT,  MVT::v16i16, MVT::v16f32, 8 },
    { ISD::FP_TO_UINT,  MVT::v16i16, MVT::v16f32, 8 }
  };

  if (SrcTy.isVector() && ST->hasNEON()) {
    if (const auto *Entry = ConvertCostTableLookup(NEONVectorConversionTbl, ISD,
                                                   DstTy.getSimpleVT(),
                                                   SrcTy.getSimpleVT()))
      return AdjustCost(Entry->Cost);
  }

  // Scalar float to integer conversions.
  static const TypeConversionCostTblEntry NEONFloatConversionTbl[] = {
    { ISD::FP_TO_SINT,  MVT::i1, MVT::f32, 2 },
    { ISD::FP_TO_UINT,  MVT::i1, MVT::f32, 2 },
    { ISD::FP_TO_SINT,  MVT::i1, MVT::f64, 2 },
    { ISD::FP_TO_UINT,  MVT::i1, MVT::f64, 2 },
    { ISD::FP_TO_SINT,  MVT::i8, MVT::f32, 2 },
    { ISD::FP_TO_UINT,  MVT::i8, MVT::f32, 2 },
    { ISD::FP_TO_SINT,  MVT::i8, MVT::f64, 2 },
    { ISD::FP_TO_UINT,  MVT::i8, MVT::f64, 2 },
    { ISD::FP_TO_SINT,  MVT::i16, MVT::f32, 2 },
    { ISD::FP_TO_UINT,  MVT::i16, MVT::f32, 2 },
    { ISD::FP_TO_SINT,  MVT::i16, MVT::f64, 2 },
    { ISD::FP_TO_UINT,  MVT::i16, MVT::f64, 2 },
    { ISD::FP_TO_SINT,  MVT::i32, MVT::f32, 2 },
    { ISD::FP_TO_UINT,  MVT::i32, MVT::f32, 2 },
    { ISD::FP_TO_SINT,  MVT::i32, MVT::f64, 2 },
    { ISD::FP_TO_UINT,  MVT::i32, MVT::f64, 2 },
    { ISD::FP_TO_SINT,  MVT::i64, MVT::f32, 10 },
    { ISD::FP_TO_UINT,  MVT::i64, MVT::f32, 10 },
    { ISD::FP_TO_SINT,  MVT::i64, MVT::f64, 10 },
    { ISD::FP_TO_UINT,  MVT::i64, MVT::f64, 10 }
  };
  if (SrcTy.isFloatingPoint() && ST->hasNEON()) {
    if (const auto *Entry = ConvertCostTableLookup(NEONFloatConversionTbl, ISD,
                                                   DstTy.getSimpleVT(),
                                                   SrcTy.getSimpleVT()))
      return AdjustCost(Entry->Cost);
  }

  // Scalar integer to float conversions.
  static const TypeConversionCostTblEntry NEONIntegerConversionTbl[] = {
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i1, 2 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i1, 2 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i1, 2 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i1, 2 },
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i8, 2 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i8, 2 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i8, 2 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i8, 2 },
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i16, 2 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i16, 2 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i16, 2 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i16, 2 },
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i32, 2 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i32, 2 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i32, 2 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i32, 2 },
    { ISD::SINT_TO_FP,  MVT::f32, MVT::i64, 10 },
    { ISD::UINT_TO_FP,  MVT::f32, MVT::i64, 10 },
    { ISD::SINT_TO_FP,  MVT::f64, MVT::i64, 10 },
    { ISD::UINT_TO_FP,  MVT::f64, MVT::i64, 10 }
  };

  if (SrcTy.isInteger() && ST->hasNEON()) {
    if (const auto *Entry = ConvertCostTableLookup(NEONIntegerConversionTbl,
                                                   ISD, DstTy.getSimpleVT(),
                                                   SrcTy.getSimpleVT()))
      return AdjustCost(Entry->Cost);
  }

  // MVE extend costs, taken from codegen tests. i8->i16 or i16->i32 is one
  // instruction, i8->i32 is two. i64 zexts are an VAND with a constant, sext
  // are linearised so take more.
  static const TypeConversionCostTblEntry MVEVectorConversionTbl[] = {
    { ISD::SIGN_EXTEND, MVT::v8i16, MVT::v8i8, 1 },
    { ISD::ZERO_EXTEND, MVT::v8i16, MVT::v8i8, 1 },
    { ISD::SIGN_EXTEND, MVT::v4i32, MVT::v4i8, 2 },
    { ISD::ZERO_EXTEND, MVT::v4i32, MVT::v4i8, 2 },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v2i8, 10 },
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v2i8, 2 },
    { ISD::SIGN_EXTEND, MVT::v4i32, MVT::v4i16, 1 },
    { ISD::ZERO_EXTEND, MVT::v4i32, MVT::v4i16, 1 },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v2i16, 10 },
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v2i16, 2 },
    { ISD::SIGN_EXTEND, MVT::v2i64, MVT::v2i32, 8 },
    { ISD::ZERO_EXTEND, MVT::v2i64, MVT::v2i32, 2 },
  };

  if (SrcTy.isVector() && ST->hasMVEIntegerOps()) {
    if (const auto *Entry = ConvertCostTableLookup(MVEVectorConversionTbl,
                                                   ISD, DstTy.getSimpleVT(),
                                                   SrcTy.getSimpleVT()))
      return Entry->Cost * ST->getMVEVectorCostFactor(CostKind);
  }

  if (ISD == ISD::FP_ROUND || ISD == ISD::FP_EXTEND) {
    // As general rule, fp converts that were not matched above are scalarized
    // and cost 1 vcvt for each lane, so long as the instruction is available.
    // If not it will become a series of function calls.
    const InstructionCost CallCost =
        getCallInstrCost(nullptr, Dst, {Src}, CostKind);
    int Lanes = 1;
    if (SrcTy.isFixedLengthVector())
      Lanes = SrcTy.getVectorNumElements();

    if (IsLegalFPType(SrcTy) && IsLegalFPType(DstTy))
      return Lanes;
    else
      return Lanes * CallCost;
  }

  if (ISD == ISD::TRUNCATE && ST->hasMVEIntegerOps() &&
      SrcTy.isFixedLengthVector()) {
    // Treat a truncate with larger than legal source (128bits for MVE) as
    // expensive, 2 instructions per lane.
    if ((SrcTy.getScalarType() == MVT::i8 ||
         SrcTy.getScalarType() == MVT::i16 ||
         SrcTy.getScalarType() == MVT::i32) &&
        SrcTy.getSizeInBits() > 128 &&
        SrcTy.getSizeInBits() > DstTy.getSizeInBits())
      return SrcTy.getVectorNumElements() * 2;
  }

  // Scalar integer conversion costs.
  static const TypeConversionCostTblEntry ARMIntegerConversionTbl[] = {
    // i16 -> i64 requires two dependent operations.
    { ISD::SIGN_EXTEND, MVT::i64, MVT::i16, 2 },

    // Truncates on i64 are assumed to be free.
    { ISD::TRUNCATE,    MVT::i32, MVT::i64, 0 },
    { ISD::TRUNCATE,    MVT::i16, MVT::i64, 0 },
    { ISD::TRUNCATE,    MVT::i8,  MVT::i64, 0 },
    { ISD::TRUNCATE,    MVT::i1,  MVT::i64, 0 }
  };

  if (SrcTy.isInteger()) {
    if (const auto *Entry = ConvertCostTableLookup(ARMIntegerConversionTbl, ISD,
                                                   DstTy.getSimpleVT(),
                                                   SrcTy.getSimpleVT()))
      return AdjustCost(Entry->Cost);
  }

  int BaseCost = ST->hasMVEIntegerOps() && Src->isVectorTy()
                     ? ST->getMVEVectorCostFactor(CostKind)
                     : 1;
  return AdjustCost(
      BaseCost * BaseT::getCastInstrCost(Opcode, Dst, Src, CCH, CostKind, I));
}

InstructionCost ARMTTIImpl::getVectorInstrCost(unsigned Opcode, Type *ValTy,
                                               TTI::TargetCostKind CostKind,
                                               unsigned Index, Value *Op0,
                                               Value *Op1) {
  // Penalize inserting into an D-subregister. We end up with a three times
  // lower estimated throughput on swift.
  if (ST->hasSlowLoadDSubregister() && Opcode == Instruction::InsertElement &&
      ValTy->isVectorTy() && ValTy->getScalarSizeInBits() <= 32)
    return 3;

  if (ST->hasNEON() && (Opcode == Instruction::InsertElement ||
                        Opcode == Instruction::ExtractElement)) {
    // Cross-class copies are expensive on many microarchitectures,
    // so assume they are expensive by default.
    if (cast<VectorType>(ValTy)->getElementType()->isIntegerTy())
      return 3;

    // Even if it's not a cross class copy, this likely leads to mixing
    // of NEON and VFP code and should be therefore penalized.
    if (ValTy->isVectorTy() &&
        ValTy->getScalarSizeInBits() <= 32)
      return std::max<InstructionCost>(
          BaseT::getVectorInstrCost(Opcode, ValTy, CostKind, Index, Op0, Op1),
          2U);
  }

  if (ST->hasMVEIntegerOps() && (Opcode == Instruction::InsertElement ||
                                 Opcode == Instruction::ExtractElement)) {
    // Integer cross-lane moves are more expensive than float, which can
    // sometimes just be vmovs. Integer involve being passes to GPR registers,
    // causing more of a delay.
    std::pair<InstructionCost, MVT> LT =
        getTypeLegalizationCost(ValTy->getScalarType());
    return LT.first * (ValTy->getScalarType()->isIntegerTy() ? 4 : 1);
  }

  return BaseT::getVectorInstrCost(Opcode, ValTy, CostKind, Index, Op0, Op1);
}

InstructionCost ARMTTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                               Type *CondTy,
                                               CmpInst::Predicate VecPred,
                                               TTI::TargetCostKind CostKind,
                                               const Instruction *I) {
  int ISD = TLI->InstructionOpcodeToISD(Opcode);

  // Thumb scalar code size cost for select.
  if (CostKind == TTI::TCK_CodeSize && ISD == ISD::SELECT &&
      ST->isThumb() && !ValTy->isVectorTy()) {
    // Assume expensive structs.
    if (TLI->getValueType(DL, ValTy, true) == MVT::Other)
      return TTI::TCC_Expensive;

    // Select costs can vary because they:
    // - may require one or more conditional mov (including an IT),
    // - can't operate directly on immediates,
    // - require live flags, which we can't copy around easily.
    InstructionCost Cost = getTypeLegalizationCost(ValTy).first;

    // Possible IT instruction for Thumb2, or more for Thumb1.
    ++Cost;

    // i1 values may need rematerialising by using mov immediates and/or
    // flag setting instructions.
    if (ValTy->isIntegerTy(1))
      ++Cost;

    return Cost;
  }

  // If this is a vector min/max/abs, use the cost of that intrinsic directly
  // instead. Hopefully when min/max intrinsics are more prevalent this code
  // will not be needed.
  const Instruction *Sel = I;
  if ((Opcode == Instruction::ICmp || Opcode == Instruction::FCmp) && Sel &&
      Sel->hasOneUse())
    Sel = cast<Instruction>(Sel->user_back());
  if (Sel && ValTy->isVectorTy() &&
      (ValTy->isIntOrIntVectorTy() || ValTy->isFPOrFPVectorTy())) {
    const Value *LHS, *RHS;
    SelectPatternFlavor SPF = matchSelectPattern(Sel, LHS, RHS).Flavor;
    unsigned IID = 0;
    switch (SPF) {
    case SPF_ABS:
      IID = Intrinsic::abs;
      break;
    case SPF_SMIN:
      IID = Intrinsic::smin;
      break;
    case SPF_SMAX:
      IID = Intrinsic::smax;
      break;
    case SPF_UMIN:
      IID = Intrinsic::umin;
      break;
    case SPF_UMAX:
      IID = Intrinsic::umax;
      break;
    case SPF_FMINNUM:
      IID = Intrinsic::minnum;
      break;
    case SPF_FMAXNUM:
      IID = Intrinsic::maxnum;
      break;
    default:
      break;
    }
    if (IID) {
      // The ICmp is free, the select gets the cost of the min/max/etc
      if (Sel != I)
        return 0;
      IntrinsicCostAttributes CostAttrs(IID, ValTy, {ValTy, ValTy});
      return getIntrinsicInstrCost(CostAttrs, CostKind);
    }
  }

  // On NEON a vector select gets lowered to vbsl.
  if (ST->hasNEON() && ValTy->isVectorTy() && ISD == ISD::SELECT && CondTy) {
    // Lowering of some vector selects is currently far from perfect.
    static const TypeConversionCostTblEntry NEONVectorSelectTbl[] = {
      { ISD::SELECT, MVT::v4i1, MVT::v4i64, 4*4 + 1*2 + 1 },
      { ISD::SELECT, MVT::v8i1, MVT::v8i64, 50 },
      { ISD::SELECT, MVT::v16i1, MVT::v16i64, 100 }
    };

    EVT SelCondTy = TLI->getValueType(DL, CondTy);
    EVT SelValTy = TLI->getValueType(DL, ValTy);
    if (SelCondTy.isSimple() && SelValTy.isSimple()) {
      if (const auto *Entry = ConvertCostTableLookup(NEONVectorSelectTbl, ISD,
                                                     SelCondTy.getSimpleVT(),
                                                     SelValTy.getSimpleVT()))
        return Entry->Cost;
    }

    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);
    return LT.first;
  }

  if (ST->hasMVEIntegerOps() && ValTy->isVectorTy() &&
      (Opcode == Instruction::ICmp || Opcode == Instruction::FCmp) &&
      cast<FixedVectorType>(ValTy)->getNumElements() > 1) {
    FixedVectorType *VecValTy = cast<FixedVectorType>(ValTy);
    FixedVectorType *VecCondTy = dyn_cast_or_null<FixedVectorType>(CondTy);
    if (!VecCondTy)
      VecCondTy = cast<FixedVectorType>(CmpInst::makeCmpResultType(VecValTy));

    // If we don't have mve.fp any fp operations will need to be scalarized.
    if (Opcode == Instruction::FCmp && !ST->hasMVEFloatOps()) {
      // One scalaization insert, one scalarization extract and the cost of the
      // fcmps.
      return BaseT::getScalarizationOverhead(VecValTy, /*Insert*/ false,
                                             /*Extract*/ true, CostKind) +
             BaseT::getScalarizationOverhead(VecCondTy, /*Insert*/ true,
                                             /*Extract*/ false, CostKind) +
             VecValTy->getNumElements() *
                 getCmpSelInstrCost(Opcode, ValTy->getScalarType(),
                                    VecCondTy->getScalarType(), VecPred,
                                    CostKind, I);
    }

    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);
    int BaseCost = ST->getMVEVectorCostFactor(CostKind);
    // There are two types - the input that specifies the type of the compare
    // and the output vXi1 type. Because we don't know how the output will be
    // split, we may need an expensive shuffle to get two in sync. This has the
    // effect of making larger than legal compares (v8i32 for example)
    // expensive.
    if (LT.second.isVector() && LT.second.getVectorNumElements() > 2) {
      if (LT.first > 1)
        return LT.first * BaseCost +
               BaseT::getScalarizationOverhead(VecCondTy, /*Insert*/ true,
                                               /*Extract*/ false, CostKind);
      return BaseCost;
    }
  }

  // Default to cheap (throughput/size of 1 instruction) but adjust throughput
  // for "multiple beats" potentially needed by MVE instructions.
  int BaseCost = 1;
  if (ST->hasMVEIntegerOps() && ValTy->isVectorTy())
    BaseCost = ST->getMVEVectorCostFactor(CostKind);

  return BaseCost *
         BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, VecPred, CostKind, I);
}

InstructionCost ARMTTIImpl::getAddressComputationCost(Type *Ty,
                                                      ScalarEvolution *SE,
                                                      const SCEV *Ptr) {
  // Address computations in vectorized code with non-consecutive addresses will
  // likely result in more instructions compared to scalar code where the
  // computation can more often be merged into the index mode. The resulting
  // extra micro-ops can significantly decrease throughput.
  unsigned NumVectorInstToHideOverhead = 10;
  int MaxMergeDistance = 64;

  if (ST->hasNEON()) {
    if (Ty->isVectorTy() && SE &&
        !BaseT::isConstantStridedAccessLessThan(SE, Ptr, MaxMergeDistance + 1))
      return NumVectorInstToHideOverhead;

    // In many cases the address computation is not merged into the instruction
    // addressing mode.
    return 1;
  }
  return BaseT::getAddressComputationCost(Ty, SE, Ptr);
}

bool ARMTTIImpl::isProfitableLSRChainElement(Instruction *I) {
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    // If a VCTP is part of a chain, it's already profitable and shouldn't be
    // optimized, else LSR may block tail-predication.
    switch (II->getIntrinsicID()) {
    case Intrinsic::arm_mve_vctp8:
    case Intrinsic::arm_mve_vctp16:
    case Intrinsic::arm_mve_vctp32:
    case Intrinsic::arm_mve_vctp64:
      return true;
    default:
      break;
    }
  }
  return false;
}

bool ARMTTIImpl::isLegalMaskedLoad(Type *DataTy, Align Alignment) {
  if (!EnableMaskedLoadStores || !ST->hasMVEIntegerOps())
    return false;

  if (auto *VecTy = dyn_cast<FixedVectorType>(DataTy)) {
    // Don't support v2i1 yet.
    if (VecTy->getNumElements() == 2)
      return false;

    // We don't support extending fp types.
     unsigned VecWidth = DataTy->getPrimitiveSizeInBits();
    if (VecWidth != 128 && VecTy->getElementType()->isFloatingPointTy())
      return false;
  }

  unsigned EltWidth = DataTy->getScalarSizeInBits();
  return (EltWidth == 32 && Alignment >= 4) ||
         (EltWidth == 16 && Alignment >= 2) || (EltWidth == 8);
}

bool ARMTTIImpl::isLegalMaskedGather(Type *Ty, Align Alignment) {
  if (!EnableMaskedGatherScatters || !ST->hasMVEIntegerOps())
    return false;

  unsigned EltWidth = Ty->getScalarSizeInBits();
  return ((EltWidth == 32 && Alignment >= 4) ||
          (EltWidth == 16 && Alignment >= 2) || EltWidth == 8);
}

/// Given a memcpy/memset/memmove instruction, return the number of memory
/// operations performed, via querying findOptimalMemOpLowering. Returns -1 if a
/// call is used.
int ARMTTIImpl::getNumMemOps(const IntrinsicInst *I) const {
  MemOp MOp;
  unsigned DstAddrSpace = ~0u;
  unsigned SrcAddrSpace = ~0u;
  const Function *F = I->getParent()->getParent();

  if (const auto *MC = dyn_cast<MemTransferInst>(I)) {
    ConstantInt *C = dyn_cast<ConstantInt>(MC->getLength());
    // If 'size' is not a constant, a library call will be generated.
    if (!C)
      return -1;

    const unsigned Size = C->getValue().getZExtValue();
    const Align DstAlign = *MC->getDestAlign();
    const Align SrcAlign = *MC->getSourceAlign();

    MOp = MemOp::Copy(Size, /*DstAlignCanChange*/ false, DstAlign, SrcAlign,
                      /*IsVolatile*/ false);
    DstAddrSpace = MC->getDestAddressSpace();
    SrcAddrSpace = MC->getSourceAddressSpace();
  }
  else if (const auto *MS = dyn_cast<MemSetInst>(I)) {
    ConstantInt *C = dyn_cast<ConstantInt>(MS->getLength());
    // If 'size' is not a constant, a library call will be generated.
    if (!C)
      return -1;

    const unsigned Size = C->getValue().getZExtValue();
    const Align DstAlign = *MS->getDestAlign();

    MOp = MemOp::Set(Size, /*DstAlignCanChange*/ false, DstAlign,
                     /*IsZeroMemset*/ false, /*IsVolatile*/ false);
    DstAddrSpace = MS->getDestAddressSpace();
  }
  else
    llvm_unreachable("Expected a memcpy/move or memset!");

  unsigned Limit, Factor = 2;
  switch(I->getIntrinsicID()) {
    case Intrinsic::memcpy:
      Limit = TLI->getMaxStoresPerMemcpy(F->hasMinSize());
      break;
    case Intrinsic::memmove:
      Limit = TLI->getMaxStoresPerMemmove(F->hasMinSize());
      break;
    case Intrinsic::memset:
      Limit = TLI->getMaxStoresPerMemset(F->hasMinSize());
      Factor = 1;
      break;
    default:
      llvm_unreachable("Expected a memcpy/move or memset!");
  }

  // MemOps will be poplulated with a list of data types that needs to be
  // loaded and stored. That's why we multiply the number of elements by 2 to
  // get the cost for this memcpy.
  std::vector<EVT> MemOps;
  if (getTLI()->findOptimalMemOpLowering(
          MemOps, Limit, MOp, DstAddrSpace,
          SrcAddrSpace, F->getAttributes()))
    return MemOps.size() * Factor;

  // If we can't find an optimal memop lowering, return the default cost
  return -1;
}

InstructionCost ARMTTIImpl::getMemcpyCost(const Instruction *I) {
  int NumOps = getNumMemOps(cast<IntrinsicInst>(I));

  // To model the cost of a library call, we assume 1 for the call, and
  // 3 for the argument setup.
  if (NumOps == -1)
    return 4;
  return NumOps;
}

InstructionCost ARMTTIImpl::getShuffleCost(TTI::ShuffleKind Kind,
                                           VectorType *Tp, ArrayRef<int> Mask,
                                           TTI::TargetCostKind CostKind,
                                           int Index, VectorType *SubTp,
                                           ArrayRef<const Value *> Args,
                                           const Instruction *CxtI) {
  Kind = improveShuffleKindFromMask(Kind, Mask, Tp, Index, SubTp);
  // Treat extractsubvector as single op permutation.
  bool IsExtractSubvector = Kind == TTI::SK_ExtractSubvector;
  if (IsExtractSubvector)
    Kind = TTI::SK_PermuteSingleSrc;
  if (ST->hasNEON()) {
    if (Kind == TTI::SK_Broadcast) {
      static const CostTblEntry NEONDupTbl[] = {
          // VDUP handles these cases.
          {ISD::VECTOR_SHUFFLE, MVT::v2i32, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v2f32, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v2i64, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v2f64, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v4i16, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v8i8, 1},

          {ISD::VECTOR_SHUFFLE, MVT::v4i32, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v4f32, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v8i16, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v16i8, 1}};

      std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Tp);
      if (const auto *Entry =
              CostTableLookup(NEONDupTbl, ISD::VECTOR_SHUFFLE, LT.second))
        return LT.first * Entry->Cost;
    }
    if (Kind == TTI::SK_Reverse) {
      static const CostTblEntry NEONShuffleTbl[] = {
          // Reverse shuffle cost one instruction if we are shuffling within a
          // double word (vrev) or two if we shuffle a quad word (vrev, vext).
          {ISD::VECTOR_SHUFFLE, MVT::v2i32, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v2f32, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v2i64, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v2f64, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v4i16, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v8i8, 1},

          {ISD::VECTOR_SHUFFLE, MVT::v4i32, 2},
          {ISD::VECTOR_SHUFFLE, MVT::v4f32, 2},
          {ISD::VECTOR_SHUFFLE, MVT::v8i16, 2},
          {ISD::VECTOR_SHUFFLE, MVT::v16i8, 2}};

      std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Tp);
      if (const auto *Entry =
              CostTableLookup(NEONShuffleTbl, ISD::VECTOR_SHUFFLE, LT.second))
        return LT.first * Entry->Cost;
    }
    if (Kind == TTI::SK_Select) {
      static const CostTblEntry NEONSelShuffleTbl[] = {
          // Select shuffle cost table for ARM. Cost is the number of
          // instructions
          // required to create the shuffled vector.

          {ISD::VECTOR_SHUFFLE, MVT::v2f32, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v2i64, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v2f64, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v2i32, 1},

          {ISD::VECTOR_SHUFFLE, MVT::v4i32, 2},
          {ISD::VECTOR_SHUFFLE, MVT::v4f32, 2},
          {ISD::VECTOR_SHUFFLE, MVT::v4i16, 2},

          {ISD::VECTOR_SHUFFLE, MVT::v8i16, 16},

          {ISD::VECTOR_SHUFFLE, MVT::v16i8, 32}};

      std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Tp);
      if (const auto *Entry = CostTableLookup(NEONSelShuffleTbl,
                                              ISD::VECTOR_SHUFFLE, LT.second))
        return LT.first * Entry->Cost;
    }
  }
  if (ST->hasMVEIntegerOps()) {
    if (Kind == TTI::SK_Broadcast) {
      static const CostTblEntry MVEDupTbl[] = {
          // VDUP handles these cases.
          {ISD::VECTOR_SHUFFLE, MVT::v4i32, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v8i16, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v16i8, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v4f32, 1},
          {ISD::VECTOR_SHUFFLE, MVT::v8f16, 1}};

      std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Tp);
      if (const auto *Entry = CostTableLookup(MVEDupTbl, ISD::VECTOR_SHUFFLE,
                                              LT.second))
        return LT.first * Entry->Cost *
               ST->getMVEVectorCostFactor(TTI::TCK_RecipThroughput);
    }

    if (!Mask.empty()) {
      std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Tp);
      if (LT.second.isVector() &&
          Mask.size() <= LT.second.getVectorNumElements() &&
          (isVREVMask(Mask, LT.second, 16) || isVREVMask(Mask, LT.second, 32) ||
           isVREVMask(Mask, LT.second, 64)))
        return ST->getMVEVectorCostFactor(TTI::TCK_RecipThroughput) * LT.first;
    }
  }

  // Restore optimal kind.
  if (IsExtractSubvector)
    Kind = TTI::SK_ExtractSubvector;
  int BaseCost = ST->hasMVEIntegerOps() && Tp->isVectorTy()
                     ? ST->getMVEVectorCostFactor(TTI::TCK_RecipThroughput)
                     : 1;
  return BaseCost *
         BaseT::getShuffleCost(Kind, Tp, Mask, CostKind, Index, SubTp);
}

InstructionCost ARMTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args,
    const Instruction *CxtI) {
  int ISDOpcode = TLI->InstructionOpcodeToISD(Opcode);
  if (ST->isThumb() && CostKind == TTI::TCK_CodeSize && Ty->isIntegerTy(1)) {
    // Make operations on i1 relatively expensive as this often involves
    // combining predicates. AND and XOR should be easier to handle with IT
    // blocks.
    switch (ISDOpcode) {
    default:
      break;
    case ISD::AND:
    case ISD::XOR:
      return 2;
    case ISD::OR:
      return 3;
    }
  }

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);

  if (ST->hasNEON()) {
    const unsigned FunctionCallDivCost = 20;
    const unsigned ReciprocalDivCost = 10;
    static const CostTblEntry CostTbl[] = {
      // Division.
      // These costs are somewhat random. Choose a cost of 20 to indicate that
      // vectorizing devision (added function call) is going to be very expensive.
      // Double registers types.
      { ISD::SDIV, MVT::v1i64, 1 * FunctionCallDivCost},
      { ISD::UDIV, MVT::v1i64, 1 * FunctionCallDivCost},
      { ISD::SREM, MVT::v1i64, 1 * FunctionCallDivCost},
      { ISD::UREM, MVT::v1i64, 1 * FunctionCallDivCost},
      { ISD::SDIV, MVT::v2i32, 2 * FunctionCallDivCost},
      { ISD::UDIV, MVT::v2i32, 2 * FunctionCallDivCost},
      { ISD::SREM, MVT::v2i32, 2 * FunctionCallDivCost},
      { ISD::UREM, MVT::v2i32, 2 * FunctionCallDivCost},
      { ISD::SDIV, MVT::v4i16,     ReciprocalDivCost},
      { ISD::UDIV, MVT::v4i16,     ReciprocalDivCost},
      { ISD::SREM, MVT::v4i16, 4 * FunctionCallDivCost},
      { ISD::UREM, MVT::v4i16, 4 * FunctionCallDivCost},
      { ISD::SDIV, MVT::v8i8,      ReciprocalDivCost},
      { ISD::UDIV, MVT::v8i8,      ReciprocalDivCost},
      { ISD::SREM, MVT::v8i8,  8 * FunctionCallDivCost},
      { ISD::UREM, MVT::v8i8,  8 * FunctionCallDivCost},
      // Quad register types.
      { ISD::SDIV, MVT::v2i64, 2 * FunctionCallDivCost},
      { ISD::UDIV, MVT::v2i64, 2 * FunctionCallDivCost},
      { ISD::SREM, MVT::v2i64, 2 * FunctionCallDivCost},
      { ISD::UREM, MVT::v2i64, 2 * FunctionCallDivCost},
      { ISD::SDIV, MVT::v4i32, 4 * FunctionCallDivCost},
      { ISD::UDIV, MVT::v4i32, 4 * FunctionCallDivCost},
      { ISD::SREM, MVT::v4i32, 4 * FunctionCallDivCost},
      { ISD::UREM, MVT::v4i32, 4 * FunctionCallDivCost},
      { ISD::SDIV, MVT::v8i16, 8 * FunctionCallDivCost},
      { ISD::UDIV, MVT::v8i16, 8 * FunctionCallDivCost},
      { ISD::SREM, MVT::v8i16, 8 * FunctionCallDivCost},
      { ISD::UREM, MVT::v8i16, 8 * FunctionCallDivCost},
      { ISD::SDIV, MVT::v16i8, 16 * FunctionCallDivCost},
      { ISD::UDIV, MVT::v16i8, 16 * FunctionCallDivCost},
      { ISD::SREM, MVT::v16i8, 16 * FunctionCallDivCost},
      { ISD::UREM, MVT::v16i8, 16 * FunctionCallDivCost},
      // Multiplication.
    };

    if (const auto *Entry = CostTableLookup(CostTbl, ISDOpcode, LT.second))
      return LT.first * Entry->Cost;

    InstructionCost Cost = BaseT::getArithmeticInstrCost(
        Opcode, Ty, CostKind, Op1Info, Op2Info);

    // This is somewhat of a hack. The problem that we are facing is that SROA
    // creates a sequence of shift, and, or instructions to construct values.
    // These sequences are recognized by the ISel and have zero-cost. Not so for
    // the vectorized code. Because we have support for v2i64 but not i64 those
    // sequences look particularly beneficial to vectorize.
    // To work around this we increase the cost of v2i64 operations to make them
    // seem less beneficial.
    if (LT.second == MVT::v2i64 && Op2Info.isUniform() && Op2Info.isConstant())
      Cost += 4;

    return Cost;
  }

  // If this operation is a shift on arm/thumb2, it might well be folded into
  // the following instruction, hence having a cost of 0.
  auto LooksLikeAFreeShift = [&]() {
    if (ST->isThumb1Only() || Ty->isVectorTy())
      return false;

    if (!CxtI || !CxtI->hasOneUse() || !CxtI->isShift())
      return false;
    if (!Op2Info.isUniform() || !Op2Info.isConstant())
      return false;

    // Folded into a ADC/ADD/AND/BIC/CMP/EOR/MVN/ORR/ORN/RSB/SBC/SUB
    switch (cast<Instruction>(CxtI->user_back())->getOpcode()) {
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::And:
    case Instruction::Xor:
    case Instruction::Or:
    case Instruction::ICmp:
      return true;
    default:
      return false;
    }
  };
  if (LooksLikeAFreeShift())
    return 0;

  // Default to cheap (throughput/size of 1 instruction) but adjust throughput
  // for "multiple beats" potentially needed by MVE instructions.
  int BaseCost = 1;
  if (ST->hasMVEIntegerOps() && Ty->isVectorTy())
    BaseCost = ST->getMVEVectorCostFactor(CostKind);

  // The rest of this mostly follows what is done in BaseT::getArithmeticInstrCost,
  // without treating floats as more expensive that scalars or increasing the
  // costs for custom operations. The results is also multiplied by the
  // MVEVectorCostFactor where appropriate.
  if (TLI->isOperationLegalOrCustomOrPromote(ISDOpcode, LT.second))
    return LT.first * BaseCost;

  // Else this is expand, assume that we need to scalarize this op.
  if (auto *VTy = dyn_cast<FixedVectorType>(Ty)) {
    unsigned Num = VTy->getNumElements();
    InstructionCost Cost =
        getArithmeticInstrCost(Opcode, Ty->getScalarType(), CostKind);
    // Return the cost of multiple scalar invocation plus the cost of
    // inserting and extracting the values.
    SmallVector<Type *> Tys(Args.size(), Ty);
    return BaseT::getScalarizationOverhead(VTy, Args, Tys, CostKind) +
           Num * Cost;
  }

  return BaseCost;
}

InstructionCost ARMTTIImpl::getMemoryOpCost(unsigned Opcode, Type *Src,
                                            MaybeAlign Alignment,
                                            unsigned AddressSpace,
                                            TTI::TargetCostKind CostKind,
                                            TTI::OperandValueInfo OpInfo,
                                            const Instruction *I) {
  // TODO: Handle other cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput)
    return 1;

  // Type legalization can't handle structs
  if (TLI->getValueType(DL, Src, true) == MVT::Other)
    return BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                  CostKind);

  if (ST->hasNEON() && Src->isVectorTy() &&
      (Alignment && *Alignment != Align(16)) &&
      cast<VectorType>(Src)->getElementType()->isDoubleTy()) {
    // Unaligned loads/stores are extremely inefficient.
    // We need 4 uops for vst.1/vld.1 vs 1uop for vldr/vstr.
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Src);
    return LT.first * 4;
  }

  // MVE can optimize a fpext(load(4xhalf)) using an extending integer load.
  // Same for stores.
  if (ST->hasMVEFloatOps() && isa<FixedVectorType>(Src) && I &&
      ((Opcode == Instruction::Load && I->hasOneUse() &&
        isa<FPExtInst>(*I->user_begin())) ||
       (Opcode == Instruction::Store && isa<FPTruncInst>(I->getOperand(0))))) {
    FixedVectorType *SrcVTy = cast<FixedVectorType>(Src);
    Type *DstTy =
        Opcode == Instruction::Load
            ? (*I->user_begin())->getType()
            : cast<Instruction>(I->getOperand(0))->getOperand(0)->getType();
    if (SrcVTy->getNumElements() == 4 && SrcVTy->getScalarType()->isHalfTy() &&
        DstTy->getScalarType()->isFloatTy())
      return ST->getMVEVectorCostFactor(CostKind);
  }

  int BaseCost = ST->hasMVEIntegerOps() && Src->isVectorTy()
                     ? ST->getMVEVectorCostFactor(CostKind)
                     : 1;
  return BaseCost * BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                           CostKind, OpInfo, I);
}

InstructionCost
ARMTTIImpl::getMaskedMemoryOpCost(unsigned Opcode, Type *Src, Align Alignment,
                                  unsigned AddressSpace,
                                  TTI::TargetCostKind CostKind) {
  if (ST->hasMVEIntegerOps()) {
    if (Opcode == Instruction::Load && isLegalMaskedLoad(Src, Alignment))
      return ST->getMVEVectorCostFactor(CostKind);
    if (Opcode == Instruction::Store && isLegalMaskedStore(Src, Alignment))
      return ST->getMVEVectorCostFactor(CostKind);
  }
  if (!isa<FixedVectorType>(Src))
    return BaseT::getMaskedMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                        CostKind);
  // Scalar cost, which is currently very high due to the efficiency of the
  // generated code.
  return cast<FixedVectorType>(Src)->getNumElements() * 8;
}

InstructionCost ARMTTIImpl::getInterleavedMemoryOpCost(
    unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
    Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
    bool UseMaskForCond, bool UseMaskForGaps) {
  assert(Factor >= 2 && "Invalid interleave factor");
  assert(isa<VectorType>(VecTy) && "Expect a vector type");

  // vldN/vstN doesn't support vector types of i64/f64 element.
  bool EltIs64Bits = DL.getTypeSizeInBits(VecTy->getScalarType()) == 64;

  if (Factor <= TLI->getMaxSupportedInterleaveFactor() && !EltIs64Bits &&
      !UseMaskForCond && !UseMaskForGaps) {
    unsigned NumElts = cast<FixedVectorType>(VecTy)->getNumElements();
    auto *SubVecTy =
        FixedVectorType::get(VecTy->getScalarType(), NumElts / Factor);

    // vldN/vstN only support legal vector types of size 64 or 128 in bits.
    // Accesses having vector types that are a multiple of 128 bits can be
    // matched to more than one vldN/vstN instruction.
    int BaseCost =
        ST->hasMVEIntegerOps() ? ST->getMVEVectorCostFactor(CostKind) : 1;
    if (NumElts % Factor == 0 &&
        TLI->isLegalInterleavedAccessType(Factor, SubVecTy, Alignment, DL))
      return Factor * BaseCost * TLI->getNumInterleavedAccesses(SubVecTy, DL);

    // Some smaller than legal interleaved patterns are cheap as we can make
    // use of the vmovn or vrev patterns to interleave a standard load. This is
    // true for v4i8, v8i8 and v4i16 at least (but not for v4f16 as it is
    // promoted differently). The cost of 2 here is then a load and vrev or
    // vmovn.
    if (ST->hasMVEIntegerOps() && Factor == 2 && NumElts / Factor > 2 &&
        VecTy->isIntOrIntVectorTy() &&
        DL.getTypeSizeInBits(SubVecTy).getFixedValue() <= 64)
      return 2 * BaseCost;
  }

  return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                           Alignment, AddressSpace, CostKind,
                                           UseMaskForCond, UseMaskForGaps);
}

InstructionCost ARMTTIImpl::getGatherScatterOpCost(
    unsigned Opcode, Type *DataTy, const Value *Ptr, bool VariableMask,
    Align Alignment, TTI::TargetCostKind CostKind, const Instruction *I) {
  using namespace PatternMatch;
  if (!ST->hasMVEIntegerOps() || !EnableMaskedGatherScatters)
    return BaseT::getGatherScatterOpCost(Opcode, DataTy, Ptr, VariableMask,
                                         Alignment, CostKind, I);

  assert(DataTy->isVectorTy() && "Can't do gather/scatters on scalar!");
  auto *VTy = cast<FixedVectorType>(DataTy);

  // TODO: Splitting, once we do that.

  unsigned NumElems = VTy->getNumElements();
  unsigned EltSize = VTy->getScalarSizeInBits();
  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(DataTy);

  // For now, it is assumed that for the MVE gather instructions the loads are
  // all effectively serialised. This means the cost is the scalar cost
  // multiplied by the number of elements being loaded. This is possibly very
  // conservative, but even so we still end up vectorising loops because the
  // cost per iteration for many loops is lower than for scalar loops.
  InstructionCost VectorCost =
      NumElems * LT.first * ST->getMVEVectorCostFactor(CostKind);
  // The scalarization cost should be a lot higher. We use the number of vector
  // elements plus the scalarization overhead. If masking is required then a lot
  // of little blocks will be needed and potentially a scalarized p0 mask,
  // greatly increasing the cost.
  InstructionCost ScalarCost =
      NumElems * LT.first + (VariableMask ? NumElems * 5 : 0) +
      BaseT::getScalarizationOverhead(VTy, /*Insert*/ true, /*Extract*/ false,
                                      CostKind) +
      BaseT::getScalarizationOverhead(VTy, /*Insert*/ false, /*Extract*/ true,
                                      CostKind);

  if (EltSize < 8 || Alignment < EltSize / 8)
    return ScalarCost;

  unsigned ExtSize = EltSize;
  // Check whether there's a single user that asks for an extended type
  if (I != nullptr) {
    // Dependent of the caller of this function, a gather instruction will
    // either have opcode Instruction::Load or be a call to the masked_gather
    // intrinsic
    if ((I->getOpcode() == Instruction::Load ||
         match(I, m_Intrinsic<Intrinsic::masked_gather>())) &&
        I->hasOneUse()) {
      const User *Us = *I->users().begin();
      if (isa<ZExtInst>(Us) || isa<SExtInst>(Us)) {
        // only allow valid type combinations
        unsigned TypeSize =
            cast<Instruction>(Us)->getType()->getScalarSizeInBits();
        if (((TypeSize == 32 && (EltSize == 8 || EltSize == 16)) ||
             (TypeSize == 16 && EltSize == 8)) &&
            TypeSize * NumElems == 128) {
          ExtSize = TypeSize;
        }
      }
    }
    // Check whether the input data needs to be truncated
    TruncInst *T;
    if ((I->getOpcode() == Instruction::Store ||
         match(I, m_Intrinsic<Intrinsic::masked_scatter>())) &&
        (T = dyn_cast<TruncInst>(I->getOperand(0)))) {
      // Only allow valid type combinations
      unsigned TypeSize = T->getOperand(0)->getType()->getScalarSizeInBits();
      if (((EltSize == 16 && TypeSize == 32) ||
           (EltSize == 8 && (TypeSize == 32 || TypeSize == 16))) &&
          TypeSize * NumElems == 128)
        ExtSize = TypeSize;
    }
  }

  if (ExtSize * NumElems != 128 || NumElems < 4)
    return ScalarCost;

  // Any (aligned) i32 gather will not need to be scalarised.
  if (ExtSize == 32)
    return VectorCost;
  // For smaller types, we need to ensure that the gep's inputs are correctly
  // extended from a small enough value. Other sizes (including i64) are
  // scalarized for now.
  if (ExtSize != 8 && ExtSize != 16)
    return ScalarCost;

  if (const auto *BC = dyn_cast<BitCastInst>(Ptr))
    Ptr = BC->getOperand(0);
  if (const auto *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    if (GEP->getNumOperands() != 2)
      return ScalarCost;
    unsigned Scale = DL.getTypeAllocSize(GEP->getResultElementType());
    // Scale needs to be correct (which is only relevant for i16s).
    if (Scale != 1 && Scale * 8 != ExtSize)
      return ScalarCost;
    // And we need to zext (not sext) the indexes from a small enough type.
    if (const auto *ZExt = dyn_cast<ZExtInst>(GEP->getOperand(1))) {
      if (ZExt->getOperand(0)->getType()->getScalarSizeInBits() <= ExtSize)
        return VectorCost;
    }
    return ScalarCost;
  }
  return ScalarCost;
}

InstructionCost
ARMTTIImpl::getArithmeticReductionCost(unsigned Opcode, VectorType *ValTy,
                                       std::optional<FastMathFlags> FMF,
                                       TTI::TargetCostKind CostKind) {

  EVT ValVT = TLI->getValueType(DL, ValTy);
  int ISD = TLI->InstructionOpcodeToISD(Opcode);
  unsigned EltSize = ValVT.getScalarSizeInBits();

  // In general floating point reductions are a series of elementwise
  // operations, with free extracts on each step. These are either in-order or
  // treewise depending on whether that is allowed by the fast math flags.
  if ((ISD == ISD::FADD || ISD == ISD::FMUL) &&
      ((EltSize == 32 && ST->hasVFP2Base()) ||
       (EltSize == 64 && ST->hasFP64()) ||
       (EltSize == 16 && ST->hasFullFP16()))) {
    unsigned NumElts = cast<FixedVectorType>(ValTy)->getNumElements();
    unsigned VecLimit = ST->hasMVEFloatOps() ? 128 : (ST->hasNEON() ? 64 : -1);
    InstructionCost VecCost = 0;
    while (!TTI::requiresOrderedReduction(FMF) && isPowerOf2_32(NumElts) &&
           NumElts * EltSize > VecLimit) {
      Type *VecTy = FixedVectorType::get(ValTy->getElementType(), NumElts / 2);
      VecCost += getArithmeticInstrCost(Opcode, VecTy, CostKind);
      NumElts /= 2;
    }

    // For fp16 we need to extract the upper lane elements. MVE can add a
    // VREV+FMIN/MAX to perform another vector step instead.
    InstructionCost ExtractCost = 0;
    if (!TTI::requiresOrderedReduction(FMF) && ST->hasMVEFloatOps() &&
        ValVT.getVectorElementType() == MVT::f16 && NumElts == 8) {
      VecCost += ST->getMVEVectorCostFactor(CostKind) * 2;
      NumElts /= 2;
    } else if (ValVT.getVectorElementType() == MVT::f16)
      ExtractCost = NumElts / 2;

    return VecCost + ExtractCost +
           NumElts *
               getArithmeticInstrCost(Opcode, ValTy->getElementType(), CostKind);
  }

  if ((ISD == ISD::AND || ISD == ISD::OR || ISD == ISD::XOR) &&
      (EltSize == 64 || EltSize == 32 || EltSize == 16 || EltSize == 8)) {
    unsigned NumElts = cast<FixedVectorType>(ValTy)->getNumElements();
    unsigned VecLimit =
        ST->hasMVEIntegerOps() ? 128 : (ST->hasNEON() ? 64 : -1);
    InstructionCost VecCost = 0;
    while (isPowerOf2_32(NumElts) && NumElts * EltSize > VecLimit) {
      Type *VecTy = FixedVectorType::get(ValTy->getElementType(), NumElts / 2);
      VecCost += getArithmeticInstrCost(Opcode, VecTy, CostKind);
      NumElts /= 2;
    }
    // For i16/i8, MVE will perform a VREV + VORR/VAND/VEOR for the 64bit vector
    // step.
    if (ST->hasMVEIntegerOps() && ValVT.getScalarSizeInBits() <= 16 &&
        NumElts * EltSize == 64) {
      Type *VecTy = FixedVectorType::get(ValTy->getElementType(), NumElts);
      VecCost += ST->getMVEVectorCostFactor(CostKind) +
                 getArithmeticInstrCost(Opcode, VecTy, CostKind);
      NumElts /= 2;
    }

    // From here we extract the elements and perform the and/or/xor.
    InstructionCost ExtractCost = NumElts;
    return VecCost + ExtractCost +
           (NumElts - 1) * getArithmeticInstrCost(
                               Opcode, ValTy->getElementType(), CostKind);
  }

  if (!ST->hasMVEIntegerOps() || !ValVT.isSimple() || ISD != ISD::ADD ||
      TTI::requiresOrderedReduction(FMF))
    return BaseT::getArithmeticReductionCost(Opcode, ValTy, FMF, CostKind);

  std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);

  static const CostTblEntry CostTblAdd[]{
      {ISD::ADD, MVT::v16i8, 1},
      {ISD::ADD, MVT::v8i16, 1},
      {ISD::ADD, MVT::v4i32, 1},
  };
  if (const auto *Entry = CostTableLookup(CostTblAdd, ISD, LT.second))
    return Entry->Cost * ST->getMVEVectorCostFactor(CostKind) * LT.first;

  return BaseT::getArithmeticReductionCost(Opcode, ValTy, FMF, CostKind);
}

InstructionCost ARMTTIImpl::getExtendedReductionCost(
    unsigned Opcode, bool IsUnsigned, Type *ResTy, VectorType *ValTy,
    FastMathFlags FMF, TTI::TargetCostKind CostKind) {
  EVT ValVT = TLI->getValueType(DL, ValTy);
  EVT ResVT = TLI->getValueType(DL, ResTy);

  int ISD = TLI->InstructionOpcodeToISD(Opcode);

  switch (ISD) {
  case ISD::ADD:
    if (ST->hasMVEIntegerOps() && ValVT.isSimple() && ResVT.isSimple()) {
      std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);

      // The legal cases are:
      //   VADDV u/s 8/16/32
      //   VADDLV u/s 32
      // Codegen currently cannot always handle larger than legal vectors very
      // well, especially for predicated reductions where the mask needs to be
      // split, so restrict to 128bit or smaller input types.
      unsigned RevVTSize = ResVT.getSizeInBits();
      if (ValVT.getSizeInBits() <= 128 &&
          ((LT.second == MVT::v16i8 && RevVTSize <= 32) ||
           (LT.second == MVT::v8i16 && RevVTSize <= 32) ||
           (LT.second == MVT::v4i32 && RevVTSize <= 64)))
        return ST->getMVEVectorCostFactor(CostKind) * LT.first;
    }
    break;
  default:
    break;
  }
  return BaseT::getExtendedReductionCost(Opcode, IsUnsigned, ResTy, ValTy, FMF,
                                         CostKind);
}

InstructionCost
ARMTTIImpl::getMulAccReductionCost(bool IsUnsigned, Type *ResTy,
                                   VectorType *ValTy,
                                   TTI::TargetCostKind CostKind) {
  EVT ValVT = TLI->getValueType(DL, ValTy);
  EVT ResVT = TLI->getValueType(DL, ResTy);

  if (ST->hasMVEIntegerOps() && ValVT.isSimple() && ResVT.isSimple()) {
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);

    // The legal cases are:
    //   VMLAV u/s 8/16/32
    //   VMLALV u/s 16/32
    // Codegen currently cannot always handle larger than legal vectors very
    // well, especially for predicated reductions where the mask needs to be
    // split, so restrict to 128bit or smaller input types.
    unsigned RevVTSize = ResVT.getSizeInBits();
    if (ValVT.getSizeInBits() <= 128 &&
        ((LT.second == MVT::v16i8 && RevVTSize <= 32) ||
         (LT.second == MVT::v8i16 && RevVTSize <= 64) ||
         (LT.second == MVT::v4i32 && RevVTSize <= 64)))
      return ST->getMVEVectorCostFactor(CostKind) * LT.first;
  }

  return BaseT::getMulAccReductionCost(IsUnsigned, ResTy, ValTy, CostKind);
}

InstructionCost
ARMTTIImpl::getMinMaxReductionCost(Intrinsic::ID IID, VectorType *Ty,
                                   FastMathFlags FMF,
                                   TTI::TargetCostKind CostKind) {
  EVT ValVT = TLI->getValueType(DL, Ty);

  // In general floating point reductions are a series of elementwise
  // operations, with free extracts on each step. These are either in-order or
  // treewise depending on whether that is allowed by the fast math flags.
  if ((IID == Intrinsic::minnum || IID == Intrinsic::maxnum) &&
      ((ValVT.getVectorElementType() == MVT::f32 && ST->hasVFP2Base()) ||
       (ValVT.getVectorElementType() == MVT::f64 && ST->hasFP64()) ||
       (ValVT.getVectorElementType() == MVT::f16 && ST->hasFullFP16()))) {
    unsigned NumElts = cast<FixedVectorType>(Ty)->getNumElements();
    unsigned EltSize = ValVT.getScalarSizeInBits();
    unsigned VecLimit = ST->hasMVEFloatOps() ? 128 : (ST->hasNEON() ? 64 : -1);
    InstructionCost VecCost;
    while (isPowerOf2_32(NumElts) && NumElts * EltSize > VecLimit) {
      Type *VecTy = FixedVectorType::get(Ty->getElementType(), NumElts/2);
      IntrinsicCostAttributes ICA(IID, VecTy, {VecTy, VecTy}, FMF);
      VecCost += getIntrinsicInstrCost(ICA, CostKind);
      NumElts /= 2;
    }

    // For fp16 we need to extract the upper lane elements. MVE can add a
    // VREV+FMIN/MAX to perform another vector step instead.
    InstructionCost ExtractCost = 0;
    if (ST->hasMVEFloatOps() && ValVT.getVectorElementType() == MVT::f16 &&
        NumElts == 8) {
      VecCost += ST->getMVEVectorCostFactor(CostKind) * 2;
      NumElts /= 2;
    } else if (ValVT.getVectorElementType() == MVT::f16)
      ExtractCost = cast<FixedVectorType>(Ty)->getNumElements() / 2;

    IntrinsicCostAttributes ICA(IID, Ty->getElementType(),
                                {Ty->getElementType(), Ty->getElementType()},
                                FMF);
    return VecCost + ExtractCost +
           (NumElts - 1) * getIntrinsicInstrCost(ICA, CostKind);
  }

  if (IID == Intrinsic::smin || IID == Intrinsic::smax ||
      IID == Intrinsic::umin || IID == Intrinsic::umax) {
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);

    // All costs are the same for u/s min/max.  These lower to vminv, which are
    // given a slightly higher cost as they tend to take multiple cycles for
    // smaller type sizes.
    static const CostTblEntry CostTblAdd[]{
        {ISD::SMIN, MVT::v16i8, 4},
        {ISD::SMIN, MVT::v8i16, 3},
        {ISD::SMIN, MVT::v4i32, 2},
    };
    if (const auto *Entry = CostTableLookup(CostTblAdd, ISD::SMIN, LT.second))
      return Entry->Cost * ST->getMVEVectorCostFactor(CostKind) * LT.first;
  }

  return BaseT::getMinMaxReductionCost(IID, Ty, FMF, CostKind);
}

InstructionCost
ARMTTIImpl::getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                  TTI::TargetCostKind CostKind) {
  switch (ICA.getID()) {
  case Intrinsic::get_active_lane_mask:
    // Currently we make a somewhat optimistic assumption that
    // active_lane_mask's are always free. In reality it may be freely folded
    // into a tail predicated loop, expanded into a VCPT or expanded into a lot
    // of add/icmp code. We may need to improve this in the future, but being
    // able to detect if it is free or not involves looking at a lot of other
    // code. We currently assume that the vectorizer inserted these, and knew
    // what it was doing in adding one.
    if (ST->hasMVEIntegerOps())
      return 0;
    break;
  case Intrinsic::sadd_sat:
  case Intrinsic::ssub_sat:
  case Intrinsic::uadd_sat:
  case Intrinsic::usub_sat: {
    if (!ST->hasMVEIntegerOps())
      break;
    Type *VT = ICA.getReturnType();

    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(VT);
    if (LT.second == MVT::v4i32 || LT.second == MVT::v8i16 ||
        LT.second == MVT::v16i8) {
      // This is a base cost of 1 for the vqadd, plus 3 extract shifts if we
      // need to extend the type, as it uses shr(qadd(shl, shl)).
      unsigned Instrs =
          LT.second.getScalarSizeInBits() == VT->getScalarSizeInBits() ? 1 : 4;
      return LT.first * ST->getMVEVectorCostFactor(CostKind) * Instrs;
    }
    break;
  }
  case Intrinsic::abs:
  case Intrinsic::smin:
  case Intrinsic::smax:
  case Intrinsic::umin:
  case Intrinsic::umax: {
    if (!ST->hasMVEIntegerOps())
      break;
    Type *VT = ICA.getReturnType();

    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(VT);
    if (LT.second == MVT::v4i32 || LT.second == MVT::v8i16 ||
        LT.second == MVT::v16i8)
      return LT.first * ST->getMVEVectorCostFactor(CostKind);
    break;
  }
  case Intrinsic::minnum:
  case Intrinsic::maxnum: {
    if (!ST->hasMVEFloatOps())
      break;
    Type *VT = ICA.getReturnType();
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(VT);
    if (LT.second == MVT::v4f32 || LT.second == MVT::v8f16)
      return LT.first * ST->getMVEVectorCostFactor(CostKind);
    break;
  }
  case Intrinsic::fptosi_sat:
  case Intrinsic::fptoui_sat: {
    if (ICA.getArgTypes().empty())
      break;
    bool IsSigned = ICA.getID() == Intrinsic::fptosi_sat;
    auto LT = getTypeLegalizationCost(ICA.getArgTypes()[0]);
    EVT MTy = TLI->getValueType(DL, ICA.getReturnType());
    // Check for the legal types, with the corect subtarget features.
    if ((ST->hasVFP2Base() && LT.second == MVT::f32 && MTy == MVT::i32) ||
        (ST->hasFP64() && LT.second == MVT::f64 && MTy == MVT::i32) ||
        (ST->hasFullFP16() && LT.second == MVT::f16 && MTy == MVT::i32))
      return LT.first;

    // Equally for MVE vector types
    if (ST->hasMVEFloatOps() &&
        (LT.second == MVT::v4f32 || LT.second == MVT::v8f16) &&
        LT.second.getScalarSizeInBits() == MTy.getScalarSizeInBits())
      return LT.first * ST->getMVEVectorCostFactor(CostKind);

    // Otherwise we use a legal convert followed by a min+max
    if (((ST->hasVFP2Base() && LT.second == MVT::f32) ||
         (ST->hasFP64() && LT.second == MVT::f64) ||
         (ST->hasFullFP16() && LT.second == MVT::f16) ||
         (ST->hasMVEFloatOps() &&
          (LT.second == MVT::v4f32 || LT.second == MVT::v8f16))) &&
        LT.second.getScalarSizeInBits() >= MTy.getScalarSizeInBits()) {
      Type *LegalTy = Type::getIntNTy(ICA.getReturnType()->getContext(),
                                      LT.second.getScalarSizeInBits());
      InstructionCost Cost =
          LT.second.isVector() ? ST->getMVEVectorCostFactor(CostKind) : 1;
      IntrinsicCostAttributes Attrs1(IsSigned ? Intrinsic::smin
                                              : Intrinsic::umin,
                                     LegalTy, {LegalTy, LegalTy});
      Cost += getIntrinsicInstrCost(Attrs1, CostKind);
      IntrinsicCostAttributes Attrs2(IsSigned ? Intrinsic::smax
                                              : Intrinsic::umax,
                                     LegalTy, {LegalTy, LegalTy});
      Cost += getIntrinsicInstrCost(Attrs2, CostKind);
      return LT.first * Cost;
    }
    break;
  }
  }

  return BaseT::getIntrinsicInstrCost(ICA, CostKind);
}

bool ARMTTIImpl::isLoweredToCall(const Function *F) {
  if (!F->isIntrinsic())
    return BaseT::isLoweredToCall(F);

  // Assume all Arm-specific intrinsics map to an instruction.
  if (F->getName().starts_with("llvm.arm"))
    return false;

  switch (F->getIntrinsicID()) {
  default: break;
  case Intrinsic::powi:
  case Intrinsic::sin:
  case Intrinsic::cos:
  case Intrinsic::pow:
  case Intrinsic::log:
  case Intrinsic::log10:
  case Intrinsic::log2:
  case Intrinsic::exp:
  case Intrinsic::exp2:
    return true;
  case Intrinsic::sqrt:
  case Intrinsic::fabs:
  case Intrinsic::copysign:
  case Intrinsic::floor:
  case Intrinsic::ceil:
  case Intrinsic::trunc:
  case Intrinsic::rint:
  case Intrinsic::nearbyint:
  case Intrinsic::round:
  case Intrinsic::canonicalize:
  case Intrinsic::lround:
  case Intrinsic::llround:
  case Intrinsic::lrint:
  case Intrinsic::llrint:
    if (F->getReturnType()->isDoubleTy() && !ST->hasFP64())
      return true;
    if (F->getReturnType()->isHalfTy() && !ST->hasFullFP16())
      return true;
    // Some operations can be handled by vector instructions and assume
    // unsupported vectors will be expanded into supported scalar ones.
    // TODO Handle scalar operations properly.
    return !ST->hasFPARMv8Base() && !ST->hasVFP2Base();
  case Intrinsic::masked_store:
  case Intrinsic::masked_load:
  case Intrinsic::masked_gather:
  case Intrinsic::masked_scatter:
    return !ST->hasMVEIntegerOps();
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::usub_with_overflow:
  case Intrinsic::sadd_sat:
  case Intrinsic::uadd_sat:
  case Intrinsic::ssub_sat:
  case Intrinsic::usub_sat:
    return false;
  }

  return BaseT::isLoweredToCall(F);
}

bool ARMTTIImpl::maybeLoweredToCall(Instruction &I) {
  unsigned ISD = TLI->InstructionOpcodeToISD(I.getOpcode());
  EVT VT = TLI->getValueType(DL, I.getType(), true);
  if (TLI->getOperationAction(ISD, VT) == TargetLowering::LibCall)
    return true;

  // Check if an intrinsic will be lowered to a call and assume that any
  // other CallInst will generate a bl.
  if (auto *Call = dyn_cast<CallInst>(&I)) {
    if (auto *II = dyn_cast<IntrinsicInst>(Call)) {
      switch(II->getIntrinsicID()) {
        case Intrinsic::memcpy:
        case Intrinsic::memset:
        case Intrinsic::memmove:
          return getNumMemOps(II) == -1;
        default:
          if (const Function *F = Call->getCalledFunction())
            return isLoweredToCall(F);
      }
    }
    return true;
  }

  // FPv5 provides conversions between integer, double-precision,
  // single-precision, and half-precision formats.
  switch (I.getOpcode()) {
  default:
    break;
  case Instruction::FPToSI:
  case Instruction::FPToUI:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
    return !ST->hasFPARMv8Base();
  }

  // FIXME: Unfortunately the approach of checking the Operation Action does
  // not catch all cases of Legalization that use library calls. Our
  // Legalization step categorizes some transformations into library calls as
  // Custom, Expand or even Legal when doing type legalization. So for now
  // we have to special case for instance the SDIV of 64bit integers and the
  // use of floating point emulation.
  if (VT.isInteger() && VT.getSizeInBits() >= 64) {
    switch (ISD) {
    default:
      break;
    case ISD::SDIV:
    case ISD::UDIV:
    case ISD::SREM:
    case ISD::UREM:
    case ISD::SDIVREM:
    case ISD::UDIVREM:
      return true;
    }
  }

  // Assume all other non-float operations are supported.
  if (!VT.isFloatingPoint())
    return false;

  // We'll need a library call to handle most floats when using soft.
  if (TLI->useSoftFloat()) {
    switch (I.getOpcode()) {
    default:
      return true;
    case Instruction::Alloca:
    case Instruction::Load:
    case Instruction::Store:
    case Instruction::Select:
    case Instruction::PHI:
      return false;
    }
  }

  // We'll need a libcall to perform double precision operations on a single
  // precision only FPU.
  if (I.getType()->isDoubleTy() && !ST->hasFP64())
    return true;

  // Likewise for half precision arithmetic.
  if (I.getType()->isHalfTy() && !ST->hasFullFP16())
    return true;

  return false;
}

bool ARMTTIImpl::isHardwareLoopProfitable(Loop *L, ScalarEvolution &SE,
                                          AssumptionCache &AC,
                                          TargetLibraryInfo *LibInfo,
                                          HardwareLoopInfo &HWLoopInfo) {
  // Low-overhead branches are only supported in the 'low-overhead branch'
  // extension of v8.1-m.
  if (!ST->hasLOB() || DisableLowOverheadLoops) {
    LLVM_DEBUG(dbgs() << "ARMHWLoops: Disabled\n");
    return false;
  }

  if (!SE.hasLoopInvariantBackedgeTakenCount(L)) {
    LLVM_DEBUG(dbgs() << "ARMHWLoops: No BETC\n");
    return false;
  }

  const SCEV *BackedgeTakenCount = SE.getBackedgeTakenCount(L);
  if (isa<SCEVCouldNotCompute>(BackedgeTakenCount)) {
    LLVM_DEBUG(dbgs() << "ARMHWLoops: Uncomputable BETC\n");
    return false;
  }

  const SCEV *TripCountSCEV =
    SE.getAddExpr(BackedgeTakenCount,
                  SE.getOne(BackedgeTakenCount->getType()));

  // We need to store the trip count in LR, a 32-bit register.
  if (SE.getUnsignedRangeMax(TripCountSCEV).getBitWidth() > 32) {
    LLVM_DEBUG(dbgs() << "ARMHWLoops: Trip count does not fit into 32bits\n");
    return false;
  }

  // Making a call will trash LR and clear LO_BRANCH_INFO, so there's little
  // point in generating a hardware loop if that's going to happen.

  auto IsHardwareLoopIntrinsic = [](Instruction &I) {
    if (auto *Call = dyn_cast<IntrinsicInst>(&I)) {
      switch (Call->getIntrinsicID()) {
      default:
        break;
      case Intrinsic::start_loop_iterations:
      case Intrinsic::test_start_loop_iterations:
      case Intrinsic::loop_decrement:
      case Intrinsic::loop_decrement_reg:
        return true;
      }
    }
    return false;
  };

  // Scan the instructions to see if there's any that we know will turn into a
  // call or if this loop is already a low-overhead loop or will become a tail
  // predicated loop.
  bool IsTailPredLoop = false;
  auto ScanLoop = [&](Loop *L) {
    for (auto *BB : L->getBlocks()) {
      for (auto &I : *BB) {
        if (maybeLoweredToCall(I) || IsHardwareLoopIntrinsic(I) ||
            isa<InlineAsm>(I)) {
          LLVM_DEBUG(dbgs() << "ARMHWLoops: Bad instruction: " << I << "\n");
          return false;
        }
        if (auto *II = dyn_cast<IntrinsicInst>(&I))
          IsTailPredLoop |=
              II->getIntrinsicID() == Intrinsic::get_active_lane_mask ||
              II->getIntrinsicID() == Intrinsic::arm_mve_vctp8 ||
              II->getIntrinsicID() == Intrinsic::arm_mve_vctp16 ||
              II->getIntrinsicID() == Intrinsic::arm_mve_vctp32 ||
              II->getIntrinsicID() == Intrinsic::arm_mve_vctp64;
      }
    }
    return true;
  };

  // Visit inner loops.
  for (auto *Inner : *L)
    if (!ScanLoop(Inner))
      return false;

  if (!ScanLoop(L))
    return false;

  // TODO: Check whether the trip count calculation is expensive. If L is the
  // inner loop but we know it has a low trip count, calculating that trip
  // count (in the parent loop) may be detrimental.

  LLVMContext &C = L->getHeader()->getContext();
  HWLoopInfo.CounterInReg = true;
  HWLoopInfo.IsNestingLegal = false;
  HWLoopInfo.PerformEntryTest = AllowWLSLoops && !IsTailPredLoop;
  HWLoopInfo.CountType = Type::getInt32Ty(C);
  HWLoopInfo.LoopDecrement = ConstantInt::get(HWLoopInfo.CountType, 1);
  return true;
}

static bool canTailPredicateInstruction(Instruction &I, int &ICmpCount) {
  // We don't allow icmp's, and because we only look at single block loops,
  // we simply count the icmps, i.e. there should only be 1 for the backedge.
  if (isa<ICmpInst>(&I) && ++ICmpCount > 1)
    return false;
  // FIXME: This is a workaround for poor cost modelling. Min/Max intrinsics are
  // not currently canonical, but soon will be. Code without them uses icmp, and
  // so is not tail predicated as per the condition above. In order to get the
  // same performance we treat min and max the same as an icmp for tailpred
  // purposes for the moment (we often rely on non-tailpred and higher VF's to
  // pick more optimial instructions like VQDMULH. They need to be recognized
  // directly by the vectorizer).
  if (auto *II = dyn_cast<IntrinsicInst>(&I))
    if ((II->getIntrinsicID() == Intrinsic::smin ||
         II->getIntrinsicID() == Intrinsic::smax ||
         II->getIntrinsicID() == Intrinsic::umin ||
         II->getIntrinsicID() == Intrinsic::umax) &&
        ++ICmpCount > 1)
      return false;

  if (isa<FCmpInst>(&I))
    return false;

  // We could allow extending/narrowing FP loads/stores, but codegen is
  // too inefficient so reject this for now.
  if (isa<FPExtInst>(&I) || isa<FPTruncInst>(&I))
    return false;

  // Extends have to be extending-loads
  if (isa<SExtInst>(&I) || isa<ZExtInst>(&I) )
    if (!I.getOperand(0)->hasOneUse() || !isa<LoadInst>(I.getOperand(0)))
      return false;

  // Truncs have to be narrowing-stores
  if (isa<TruncInst>(&I) )
    if (!I.hasOneUse() || !isa<StoreInst>(*I.user_begin()))
      return false;

  return true;
}

// To set up a tail-predicated loop, we need to know the total number of
// elements processed by that loop. Thus, we need to determine the element
// size and:
// 1) it should be uniform for all operations in the vector loop, so we
//    e.g. don't want any widening/narrowing operations.
// 2) it should be smaller than i64s because we don't have vector operations
//    that work on i64s.
// 3) we don't want elements to be reversed or shuffled, to make sure the
//    tail-predication masks/predicates the right lanes.
//
static bool canTailPredicateLoop(Loop *L, LoopInfo *LI, ScalarEvolution &SE,
                                 const DataLayout &DL,
                                 const LoopAccessInfo *LAI) {
  LLVM_DEBUG(dbgs() << "Tail-predication: checking allowed instructions\n");

  // If there are live-out values, it is probably a reduction. We can predicate
  // most reduction operations freely under MVE using a combination of
  // prefer-predicated-reduction-select and inloop reductions. We limit this to
  // floating point and integer reductions, but don't check for operators
  // specifically here. If the value ends up not being a reduction (and so the
  // vectorizer cannot tailfold the loop), we should fall back to standard
  // vectorization automatically.
  SmallVector< Instruction *, 8 > LiveOuts;
  LiveOuts = llvm::findDefsUsedOutsideOfLoop(L);
  bool ReductionsDisabled =
      EnableTailPredication == TailPredication::EnabledNoReductions ||
      EnableTailPredication == TailPredication::ForceEnabledNoReductions;

  for (auto *I : LiveOuts) {
    if (!I->getType()->isIntegerTy() && !I->getType()->isFloatTy() &&
        !I->getType()->isHalfTy()) {
      LLVM_DEBUG(dbgs() << "Don't tail-predicate loop with non-integer/float "
                           "live-out value\n");
      return false;
    }
    if (ReductionsDisabled) {
      LLVM_DEBUG(dbgs() << "Reductions not enabled\n");
      return false;
    }
  }

  // Next, check that all instructions can be tail-predicated.
  PredicatedScalarEvolution PSE = LAI->getPSE();
  SmallVector<Instruction *, 16> LoadStores;
  int ICmpCount = 0;

  for (BasicBlock *BB : L->blocks()) {
    for (Instruction &I : BB->instructionsWithoutDebug()) {
      if (isa<PHINode>(&I))
        continue;
      if (!canTailPredicateInstruction(I, ICmpCount)) {
        LLVM_DEBUG(dbgs() << "Instruction not allowed: "; I.dump());
        return false;
      }

      Type *T  = I.getType();
      if (T->getScalarSizeInBits() > 32) {
        LLVM_DEBUG(dbgs() << "Unsupported Type: "; T->dump());
        return false;
      }
      if (isa<StoreInst>(I) || isa<LoadInst>(I)) {
        Value *Ptr = getLoadStorePointerOperand(&I);
        Type *AccessTy = getLoadStoreType(&I);
        int64_t NextStride = getPtrStride(PSE, AccessTy, Ptr, L).value_or(0);
        if (NextStride == 1) {
          // TODO: for now only allow consecutive strides of 1. We could support
          // other strides as long as it is uniform, but let's keep it simple
          // for now.
          continue;
        } else if (NextStride == -1 ||
                   (NextStride == 2 && MVEMaxSupportedInterleaveFactor >= 2) ||
                   (NextStride == 4 && MVEMaxSupportedInterleaveFactor >= 4)) {
          LLVM_DEBUG(dbgs()
                     << "Consecutive strides of 2 found, vld2/vstr2 can't "
                        "be tail-predicated\n.");
          return false;
          // TODO: don't tail predicate if there is a reversed load?
        } else if (EnableMaskedGatherScatters) {
          // Gather/scatters do allow loading from arbitrary strides, at
          // least if they are loop invariant.
          // TODO: Loop variant strides should in theory work, too, but
          // this requires further testing.
          const SCEV *PtrScev = PSE.getSE()->getSCEV(Ptr);
          if (auto AR = dyn_cast<SCEVAddRecExpr>(PtrScev)) {
            const SCEV *Step = AR->getStepRecurrence(*PSE.getSE());
            if (PSE.getSE()->isLoopInvariant(Step, L))
              continue;
          }
        }
        LLVM_DEBUG(dbgs() << "Bad stride found, can't "
                             "tail-predicate\n.");
        return false;
      }
    }
  }

  LLVM_DEBUG(dbgs() << "tail-predication: all instructions allowed!\n");
  return true;
}

bool ARMTTIImpl::preferPredicateOverEpilogue(TailFoldingInfo *TFI) {
  if (!EnableTailPredication) {
    LLVM_DEBUG(dbgs() << "Tail-predication not enabled.\n");
    return false;
  }

  // Creating a predicated vector loop is the first step for generating a
  // tail-predicated hardware loop, for which we need the MVE masked
  // load/stores instructions:
  if (!ST->hasMVEIntegerOps())
    return false;

  LoopVectorizationLegality *LVL = TFI->LVL;
  Loop *L = LVL->getLoop();

  // For now, restrict this to single block loops.
  if (L->getNumBlocks() > 1) {
    LLVM_DEBUG(dbgs() << "preferPredicateOverEpilogue: not a single block "
                         "loop.\n");
    return false;
  }

  assert(L->isInnermost() && "preferPredicateOverEpilogue: inner-loop expected");

  LoopInfo *LI = LVL->getLoopInfo();
  HardwareLoopInfo HWLoopInfo(L);
  if (!HWLoopInfo.canAnalyze(*LI)) {
    LLVM_DEBUG(dbgs() << "preferPredicateOverEpilogue: hardware-loop is not "
                         "analyzable.\n");
    return false;
  }

  AssumptionCache *AC = LVL->getAssumptionCache();
  ScalarEvolution *SE = LVL->getScalarEvolution();

  // This checks if we have the low-overhead branch architecture
  // extension, and if we will create a hardware-loop:
  if (!isHardwareLoopProfitable(L, *SE, *AC, TFI->TLI, HWLoopInfo)) {
    LLVM_DEBUG(dbgs() << "preferPredicateOverEpilogue: hardware-loop is not "
                         "profitable.\n");
    return false;
  }

  DominatorTree *DT = LVL->getDominatorTree();
  if (!HWLoopInfo.isHardwareLoopCandidate(*SE, *LI, *DT)) {
    LLVM_DEBUG(dbgs() << "preferPredicateOverEpilogue: hardware-loop is not "
                         "a candidate.\n");
    return false;
  }

  return canTailPredicateLoop(L, LI, *SE, DL, LVL->getLAI());
}

TailFoldingStyle
ARMTTIImpl::getPreferredTailFoldingStyle(bool IVUpdateMayOverflow) const {
  if (!ST->hasMVEIntegerOps() || !EnableTailPredication)
    return TailFoldingStyle::DataWithoutLaneMask;

  // Intrinsic @llvm.get.active.lane.mask is supported.
  // It is used in the MVETailPredication pass, which requires the number of
  // elements processed by this vector loop to setup the tail-predicated
  // loop.
  return TailFoldingStyle::Data;
}
void ARMTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                         TTI::UnrollingPreferences &UP,
                                         OptimizationRemarkEmitter *ORE) {
  // Enable Upper bound unrolling universally, providing that we do not see an
  // active lane mask, which will be better kept as a loop to become tail
  // predicated than to be conditionally unrolled.
  UP.UpperBound =
      !ST->hasMVEIntegerOps() || !any_of(*L->getHeader(), [](Instruction &I) {
        return isa<IntrinsicInst>(I) &&
               cast<IntrinsicInst>(I).getIntrinsicID() ==
                   Intrinsic::get_active_lane_mask;
      });

  // Only currently enable these preferences for M-Class cores.
  if (!ST->isMClass())
    return BasicTTIImplBase::getUnrollingPreferences(L, SE, UP, ORE);

  // Disable loop unrolling for Oz and Os.
  UP.OptSizeThreshold = 0;
  UP.PartialOptSizeThreshold = 0;
  if (L->getHeader()->getParent()->hasOptSize())
    return;

  SmallVector<BasicBlock*, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  LLVM_DEBUG(dbgs() << "Loop has:\n"
                    << "Blocks: " << L->getNumBlocks() << "\n"
                    << "Exit blocks: " << ExitingBlocks.size() << "\n");

  // Only allow another exit other than the latch. This acts as an early exit
  // as it mirrors the profitability calculation of the runtime unroller.
  if (ExitingBlocks.size() > 2)
    return;

  // Limit the CFG of the loop body for targets with a branch predictor.
  // Allowing 4 blocks permits if-then-else diamonds in the body.
  if (ST->hasBranchPredictor() && L->getNumBlocks() > 4)
    return;

  // Don't unroll vectorized loops, including the remainder loop
  if (getBooleanLoopAttribute(L, "llvm.loop.isvectorized"))
    return;

  // Scan the loop: don't unroll loops with calls as this could prevent
  // inlining.
  InstructionCost Cost = 0;
  for (auto *BB : L->getBlocks()) {
    for (auto &I : *BB) {
      // Don't unroll vectorised loop. MVE does not benefit from it as much as
      // scalar code.
      if (I.getType()->isVectorTy())
        return;

      if (isa<CallInst>(I) || isa<InvokeInst>(I)) {
        if (const Function *F = cast<CallBase>(I).getCalledFunction()) {
          if (!isLoweredToCall(F))
            continue;
        }
        return;
      }

      SmallVector<const Value*, 4> Operands(I.operand_values());
      Cost += getInstructionCost(&I, Operands,
                                 TargetTransformInfo::TCK_SizeAndLatency);
    }
  }

  // On v6m cores, there are very few registers available. We can easily end up
  // spilling and reloading more registers in an unrolled loop. Look at the
  // number of LCSSA phis as a rough measure of how many registers will need to
  // be live out of the loop, reducing the default unroll count if more than 1
  // value is needed.  In the long run, all of this should be being learnt by a
  // machine.
  unsigned UnrollCount = 4;
  if (ST->isThumb1Only()) {
    unsigned ExitingValues = 0;
    SmallVector<BasicBlock *, 4> ExitBlocks;
    L->getExitBlocks(ExitBlocks);
    for (auto *Exit : ExitBlocks) {
      // Count the number of LCSSA phis. Exclude values coming from GEP's as
      // only the last is expected to be needed for address operands.
      unsigned LiveOuts = count_if(Exit->phis(), [](auto &PH) {
        return PH.getNumOperands() != 1 ||
               !isa<GetElementPtrInst>(PH.getOperand(0));
      });
      ExitingValues = ExitingValues < LiveOuts ? LiveOuts : ExitingValues;
    }
    if (ExitingValues)
      UnrollCount /= ExitingValues;
    if (UnrollCount <= 1)
      return;
  }

  LLVM_DEBUG(dbgs() << "Cost of loop: " << Cost << "\n");
  LLVM_DEBUG(dbgs() << "Default Runtime Unroll Count: " << UnrollCount << "\n");

  UP.Partial = true;
  UP.Runtime = true;
  UP.UnrollRemainder = true;
  UP.DefaultUnrollRuntimeCount = UnrollCount;
  UP.UnrollAndJam = true;
  UP.UnrollAndJamInnerLoopThreshold = 60;

  // Force unrolling small loops can be very useful because of the branch
  // taken cost of the backedge.
  if (Cost < 12)
    UP.Force = true;
}

void ARMTTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                       TTI::PeelingPreferences &PP) {
  BaseT::getPeelingPreferences(L, SE, PP);
}

bool ARMTTIImpl::preferInLoopReduction(unsigned Opcode, Type *Ty,
                                       TTI::ReductionFlags Flags) const {
  if (!ST->hasMVEIntegerOps())
    return false;

  unsigned ScalarBits = Ty->getScalarSizeInBits();
  switch (Opcode) {
  case Instruction::Add:
    return ScalarBits <= 64;
  default:
    return false;
  }
}

bool ARMTTIImpl::preferPredicatedReductionSelect(
    unsigned Opcode, Type *Ty, TTI::ReductionFlags Flags) const {
  if (!ST->hasMVEIntegerOps())
    return false;
  return true;
}

InstructionCost ARMTTIImpl::getScalingFactorCost(Type *Ty, GlobalValue *BaseGV,
                                                 StackOffset BaseOffset,
                                                 bool HasBaseReg, int64_t Scale,
                                                 unsigned AddrSpace) const {
  TargetLoweringBase::AddrMode AM;
  AM.BaseGV = BaseGV;
  AM.BaseOffs = BaseOffset.getFixed();
  AM.HasBaseReg = HasBaseReg;
  AM.Scale = Scale;
  AM.ScalableOffset = BaseOffset.getScalable();
  if (getTLI()->isLegalAddressingMode(DL, AM, Ty, AddrSpace)) {
    if (ST->hasFPAO())
      return AM.Scale < 0 ? 1 : 0; // positive offsets execute faster
    return 0;
  }
  return -1;
}

bool ARMTTIImpl::hasArmWideBranch(bool Thumb) const {
  if (Thumb) {
    // B.W is available in any Thumb2-supporting target, and also in every
    // version of Armv8-M, even Baseline which does not include the rest of
    // Thumb2.
    return ST->isThumb2() || ST->hasV8MBaselineOps();
  } else {
    // B is available in all versions of the Arm ISA, so the only question is
    // whether that ISA is available at all.
    return ST->hasARMOps();
  }
}
