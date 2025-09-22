//===- HexagonTargetTransformInfo.cpp - Hexagon specific TTI pass ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
/// \file
/// This file implements a TargetTransformInfo analysis pass specific to the
/// Hexagon target machine. It uses the target's detailed information to provide
/// more precise answers to certain TTI queries, while letting the target
/// independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#include "HexagonTargetTransformInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/User.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/LoopPeel.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"

using namespace llvm;

#define DEBUG_TYPE "hexagontti"

static cl::opt<bool> HexagonAutoHVX("hexagon-autohvx", cl::init(false),
    cl::Hidden, cl::desc("Enable loop vectorizer for HVX"));

static cl::opt<bool> EnableV68FloatAutoHVX(
    "force-hvx-float", cl::Hidden,
    cl::desc("Enable auto-vectorization of floatint point types on v68."));

static cl::opt<bool> EmitLookupTables("hexagon-emit-lookup-tables",
    cl::init(true), cl::Hidden,
    cl::desc("Control lookup table emission on Hexagon target"));

static cl::opt<bool> HexagonMaskedVMem("hexagon-masked-vmem", cl::init(true),
    cl::Hidden, cl::desc("Enable masked loads/stores for HVX"));

// Constant "cost factor" to make floating point operations more expensive
// in terms of vectorization cost. This isn't the best way, but it should
// do. Ultimately, the cost should use cycles.
static const unsigned FloatFactor = 4;

bool HexagonTTIImpl::useHVX() const {
  return ST.useHVXOps() && HexagonAutoHVX;
}

bool HexagonTTIImpl::isHVXVectorType(Type *Ty) const {
  auto *VecTy = dyn_cast<VectorType>(Ty);
  if (!VecTy)
    return false;
  if (!ST.isTypeForHVX(VecTy))
    return false;
  if (ST.useHVXV69Ops() || !VecTy->getElementType()->isFloatingPointTy())
    return true;
  return ST.useHVXV68Ops() && EnableV68FloatAutoHVX;
}

unsigned HexagonTTIImpl::getTypeNumElements(Type *Ty) const {
  if (auto *VTy = dyn_cast<FixedVectorType>(Ty))
    return VTy->getNumElements();
  assert((Ty->isIntegerTy() || Ty->isFloatingPointTy()) &&
         "Expecting scalar type");
  return 1;
}

TargetTransformInfo::PopcntSupportKind
HexagonTTIImpl::getPopcntSupport(unsigned IntTyWidthInBit) const {
  // Return fast hardware support as every input < 64 bits will be promoted
  // to 64 bits.
  return TargetTransformInfo::PSK_FastHardware;
}

// The Hexagon target can unroll loops with run-time trip counts.
void HexagonTTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                             TTI::UnrollingPreferences &UP,
                                             OptimizationRemarkEmitter *ORE) {
  UP.Runtime = UP.Partial = true;
}

void HexagonTTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                           TTI::PeelingPreferences &PP) {
  BaseT::getPeelingPreferences(L, SE, PP);
  // Only try to peel innermost loops with small runtime trip counts.
  if (L && L->isInnermost() && canPeel(L) &&
      SE.getSmallConstantTripCount(L) == 0 &&
      SE.getSmallConstantMaxTripCount(L) > 0 &&
      SE.getSmallConstantMaxTripCount(L) <= 5) {
    PP.PeelCount = 2;
  }
}

TTI::AddressingModeKind
HexagonTTIImpl::getPreferredAddressingMode(const Loop *L,
                                           ScalarEvolution *SE) const {
  return TTI::AMK_PostIndexed;
}

/// --- Vector TTI begin ---

unsigned HexagonTTIImpl::getNumberOfRegisters(bool Vector) const {
  if (Vector)
    return useHVX() ? 32 : 0;
  return 32;
}

unsigned HexagonTTIImpl::getMaxInterleaveFactor(ElementCount VF) {
  return useHVX() ? 2 : 1;
}

TypeSize
HexagonTTIImpl::getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
  switch (K) {
  case TargetTransformInfo::RGK_Scalar:
    return TypeSize::getFixed(32);
  case TargetTransformInfo::RGK_FixedWidthVector:
    return TypeSize::getFixed(getMinVectorRegisterBitWidth());
  case TargetTransformInfo::RGK_ScalableVector:
    return TypeSize::getScalable(0);
  }

  llvm_unreachable("Unsupported register kind");
}

unsigned HexagonTTIImpl::getMinVectorRegisterBitWidth() const {
  return useHVX() ? ST.getVectorLength()*8 : 32;
}

ElementCount HexagonTTIImpl::getMinimumVF(unsigned ElemWidth,
                                          bool IsScalable) const {
  assert(!IsScalable && "Scalable VFs are not supported for Hexagon");
  return ElementCount::getFixed((8 * ST.getVectorLength()) / ElemWidth);
}

InstructionCost HexagonTTIImpl::getCallInstrCost(Function *F, Type *RetTy,
                                                 ArrayRef<Type *> Tys,
                                                 TTI::TargetCostKind CostKind) {
  return BaseT::getCallInstrCost(F, RetTy, Tys, CostKind);
}

InstructionCost
HexagonTTIImpl::getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                      TTI::TargetCostKind CostKind) {
  if (ICA.getID() == Intrinsic::bswap) {
    std::pair<InstructionCost, MVT> LT =
        getTypeLegalizationCost(ICA.getReturnType());
    return LT.first + 2;
  }
  return BaseT::getIntrinsicInstrCost(ICA, CostKind);
}

InstructionCost HexagonTTIImpl::getAddressComputationCost(Type *Tp,
                                                          ScalarEvolution *SE,
                                                          const SCEV *S) {
  return 0;
}

InstructionCost HexagonTTIImpl::getMemoryOpCost(unsigned Opcode, Type *Src,
                                                MaybeAlign Alignment,
                                                unsigned AddressSpace,
                                                TTI::TargetCostKind CostKind,
                                                TTI::OperandValueInfo OpInfo,
                                                const Instruction *I) {
  assert(Opcode == Instruction::Load || Opcode == Instruction::Store);
  // TODO: Handle other cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput)
    return 1;

  if (Opcode == Instruction::Store)
    return BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                  CostKind, OpInfo, I);

  if (Src->isVectorTy()) {
    VectorType *VecTy = cast<VectorType>(Src);
    unsigned VecWidth = VecTy->getPrimitiveSizeInBits().getFixedValue();
    if (isHVXVectorType(VecTy)) {
      unsigned RegWidth =
          getRegisterBitWidth(TargetTransformInfo::RGK_FixedWidthVector)
              .getFixedValue();
      assert(RegWidth && "Non-zero vector register width expected");
      // Cost of HVX loads.
      if (VecWidth % RegWidth == 0)
        return VecWidth / RegWidth;
      // Cost of constructing HVX vector from scalar loads
      const Align RegAlign(RegWidth / 8);
      if (!Alignment || *Alignment > RegAlign)
        Alignment = RegAlign;
      assert(Alignment);
      unsigned AlignWidth = 8 * Alignment->value();
      unsigned NumLoads = alignTo(VecWidth, AlignWidth) / AlignWidth;
      return 3 * NumLoads;
    }

    // Non-HVX vectors.
    // Add extra cost for floating point types.
    unsigned Cost =
        VecTy->getElementType()->isFloatingPointTy() ? FloatFactor : 1;

    // At this point unspecified alignment is considered as Align(1).
    const Align BoundAlignment = std::min(Alignment.valueOrOne(), Align(8));
    unsigned AlignWidth = 8 * BoundAlignment.value();
    unsigned NumLoads = alignTo(VecWidth, AlignWidth) / AlignWidth;
    if (Alignment == Align(4) || Alignment == Align(8))
      return Cost * NumLoads;
    // Loads of less than 32 bits will need extra inserts to compose a vector.
    assert(BoundAlignment <= Align(8));
    unsigned LogA = Log2(BoundAlignment);
    return (3 - LogA) * Cost * NumLoads;
  }

  return BaseT::getMemoryOpCost(Opcode, Src, Alignment, AddressSpace, CostKind,
                                OpInfo, I);
}

InstructionCost
HexagonTTIImpl::getMaskedMemoryOpCost(unsigned Opcode, Type *Src,
                                      Align Alignment, unsigned AddressSpace,
                                      TTI::TargetCostKind CostKind) {
  return BaseT::getMaskedMemoryOpCost(Opcode, Src, Alignment, AddressSpace,
                                      CostKind);
}

InstructionCost HexagonTTIImpl::getShuffleCost(TTI::ShuffleKind Kind, Type *Tp,
                                               ArrayRef<int> Mask,
                                               TTI::TargetCostKind CostKind,
                                               int Index, Type *SubTp,
                                               ArrayRef<const Value *> Args,
                                               const Instruction *CxtI) {
  return 1;
}

InstructionCost HexagonTTIImpl::getGatherScatterOpCost(
    unsigned Opcode, Type *DataTy, const Value *Ptr, bool VariableMask,
    Align Alignment, TTI::TargetCostKind CostKind, const Instruction *I) {
  return BaseT::getGatherScatterOpCost(Opcode, DataTy, Ptr, VariableMask,
                                       Alignment, CostKind, I);
}

InstructionCost HexagonTTIImpl::getInterleavedMemoryOpCost(
    unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
    Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
    bool UseMaskForCond, bool UseMaskForGaps) {
  if (Indices.size() != Factor || UseMaskForCond || UseMaskForGaps)
    return BaseT::getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                             Alignment, AddressSpace,
                                             CostKind,
                                             UseMaskForCond, UseMaskForGaps);
  return getMemoryOpCost(Opcode, VecTy, MaybeAlign(Alignment), AddressSpace,
                         CostKind);
}

InstructionCost HexagonTTIImpl::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                                   Type *CondTy,
                                                   CmpInst::Predicate VecPred,
                                                   TTI::TargetCostKind CostKind,
                                                   const Instruction *I) {
  if (ValTy->isVectorTy() && CostKind == TTI::TCK_RecipThroughput) {
    if (!isHVXVectorType(ValTy) && ValTy->isFPOrFPVectorTy())
      return InstructionCost::getMax();
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(ValTy);
    if (Opcode == Instruction::FCmp)
      return LT.first + FloatFactor * getTypeNumElements(ValTy);
  }
  return BaseT::getCmpSelInstrCost(Opcode, ValTy, CondTy, VecPred, CostKind, I);
}

InstructionCost HexagonTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args,
    const Instruction *CxtI) {
  // TODO: Handle more cost kinds.
  if (CostKind != TTI::TCK_RecipThroughput)
    return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                         Op2Info, Args, CxtI);

  if (Ty->isVectorTy()) {
    if (!isHVXVectorType(Ty) && Ty->isFPOrFPVectorTy())
      return InstructionCost::getMax();
    std::pair<InstructionCost, MVT> LT = getTypeLegalizationCost(Ty);
    if (LT.second.isFloatingPoint())
      return LT.first + FloatFactor * getTypeNumElements(Ty);
  }
  return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info, Op2Info,
                                       Args, CxtI);
}

InstructionCost HexagonTTIImpl::getCastInstrCost(unsigned Opcode, Type *DstTy,
                                                 Type *SrcTy,
                                                 TTI::CastContextHint CCH,
                                                 TTI::TargetCostKind CostKind,
                                                 const Instruction *I) {
  auto isNonHVXFP = [this] (Type *Ty) {
    return Ty->isVectorTy() && !isHVXVectorType(Ty) && Ty->isFPOrFPVectorTy();
  };
  if (isNonHVXFP(SrcTy) || isNonHVXFP(DstTy))
    return InstructionCost::getMax();

  if (SrcTy->isFPOrFPVectorTy() || DstTy->isFPOrFPVectorTy()) {
    unsigned SrcN = SrcTy->isFPOrFPVectorTy() ? getTypeNumElements(SrcTy) : 0;
    unsigned DstN = DstTy->isFPOrFPVectorTy() ? getTypeNumElements(DstTy) : 0;

    std::pair<InstructionCost, MVT> SrcLT = getTypeLegalizationCost(SrcTy);
    std::pair<InstructionCost, MVT> DstLT = getTypeLegalizationCost(DstTy);
    InstructionCost Cost =
        std::max(SrcLT.first, DstLT.first) + FloatFactor * (SrcN + DstN);
    // TODO: Allow non-throughput costs that aren't binary.
    if (CostKind != TTI::TCK_RecipThroughput)
      return Cost == 0 ? 0 : 1;
    return Cost;
  }
  return 1;
}

InstructionCost HexagonTTIImpl::getVectorInstrCost(unsigned Opcode, Type *Val,
                                                   TTI::TargetCostKind CostKind,
                                                   unsigned Index, Value *Op0,
                                                   Value *Op1) {
  Type *ElemTy = Val->isVectorTy() ? cast<VectorType>(Val)->getElementType()
                                   : Val;
  if (Opcode == Instruction::InsertElement) {
    // Need two rotations for non-zero index.
    unsigned Cost = (Index != 0) ? 2 : 0;
    if (ElemTy->isIntegerTy(32))
      return Cost;
    // If it's not a 32-bit value, there will need to be an extract.
    return Cost + getVectorInstrCost(Instruction::ExtractElement, Val, CostKind,
                                     Index, Op0, Op1);
  }

  if (Opcode == Instruction::ExtractElement)
    return 2;

  return 1;
}

bool HexagonTTIImpl::isLegalMaskedStore(Type *DataType, Align /*Alignment*/) {
  // This function is called from scalarize-masked-mem-intrin, which runs
  // in pre-isel. Use ST directly instead of calling isHVXVectorType.
  return HexagonMaskedVMem && ST.isTypeForHVX(DataType);
}

bool HexagonTTIImpl::isLegalMaskedLoad(Type *DataType, Align /*Alignment*/) {
  // This function is called from scalarize-masked-mem-intrin, which runs
  // in pre-isel. Use ST directly instead of calling isHVXVectorType.
  return HexagonMaskedVMem && ST.isTypeForHVX(DataType);
}

/// --- Vector TTI end ---

unsigned HexagonTTIImpl::getPrefetchDistance() const {
  return ST.getL1PrefetchDistance();
}

unsigned HexagonTTIImpl::getCacheLineSize() const {
  return ST.getL1CacheLineSize();
}

InstructionCost
HexagonTTIImpl::getInstructionCost(const User *U,
                                   ArrayRef<const Value *> Operands,
                                   TTI::TargetCostKind CostKind) {
  auto isCastFoldedIntoLoad = [this](const CastInst *CI) -> bool {
    if (!CI->isIntegerCast())
      return false;
    // Only extensions from an integer type shorter than 32-bit to i32
    // can be folded into the load.
    const DataLayout &DL = getDataLayout();
    unsigned SBW = DL.getTypeSizeInBits(CI->getSrcTy());
    unsigned DBW = DL.getTypeSizeInBits(CI->getDestTy());
    if (DBW != 32 || SBW >= DBW)
      return false;

    const LoadInst *LI = dyn_cast<const LoadInst>(CI->getOperand(0));
    // Technically, this code could allow multiple uses of the load, and
    // check if all the uses are the same extension operation, but this
    // should be sufficient for most cases.
    return LI && LI->hasOneUse();
  };

  if (const CastInst *CI = dyn_cast<const CastInst>(U))
    if (isCastFoldedIntoLoad(CI))
      return TargetTransformInfo::TCC_Free;
  return BaseT::getInstructionCost(U, Operands, CostKind);
}

bool HexagonTTIImpl::shouldBuildLookupTables() const {
  return EmitLookupTables;
}
