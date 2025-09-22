//===- AArch64TargetTransformInfo.h - AArch64 specific TTI ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file a TargetTransformInfo::Concept conforming object specific to the
/// AArch64 target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64TARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64TARGETTRANSFORMINFO_H

#include "AArch64.h"
#include "AArch64Subtarget.h"
#include "AArch64TargetMachine.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include <cstdint>
#include <optional>

namespace llvm {

class APInt;
class Instruction;
class IntrinsicInst;
class Loop;
class SCEV;
class ScalarEvolution;
class Type;
class Value;
class VectorType;

class AArch64TTIImpl : public BasicTTIImplBase<AArch64TTIImpl> {
  using BaseT = BasicTTIImplBase<AArch64TTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const AArch64Subtarget *ST;
  const AArch64TargetLowering *TLI;

  const AArch64Subtarget *getST() const { return ST; }
  const AArch64TargetLowering *getTLI() const { return TLI; }

  enum MemIntrinsicType {
    VECTOR_LDST_TWO_ELEMENTS,
    VECTOR_LDST_THREE_ELEMENTS,
    VECTOR_LDST_FOUR_ELEMENTS
  };

  bool isWideningInstruction(Type *DstTy, unsigned Opcode,
                             ArrayRef<const Value *> Args,
                             Type *SrcOverrideTy = nullptr);

  // A helper function called by 'getVectorInstrCost'.
  //
  // 'Val' and 'Index' are forwarded from 'getVectorInstrCost'; 'HasRealUse'
  // indicates whether the vector instruction is available in the input IR or
  // just imaginary in vectorizer passes.
  InstructionCost getVectorInstrCostHelper(const Instruction *I, Type *Val,
                                           unsigned Index, bool HasRealUse);

public:
  explicit AArch64TTIImpl(const AArch64TargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  bool areInlineCompatible(const Function *Caller,
                           const Function *Callee) const;

  bool areTypesABICompatible(const Function *Caller, const Function *Callee,
                             const ArrayRef<Type *> &Types) const;

  unsigned getInlineCallPenalty(const Function *F, const CallBase &Call,
                                unsigned DefaultCallPenalty) const;

  /// \name Scalar TTI Implementations
  /// @{

  using BaseT::getIntImmCost;
  InstructionCost getIntImmCost(int64_t Val);
  InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                TTI::TargetCostKind CostKind);
  InstructionCost getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                    const APInt &Imm, Type *Ty,
                                    TTI::TargetCostKind CostKind,
                                    Instruction *Inst = nullptr);
  InstructionCost getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                      const APInt &Imm, Type *Ty,
                                      TTI::TargetCostKind CostKind);
  TTI::PopcntSupportKind getPopcntSupport(unsigned TyWidth);

  /// @}

  /// \name Vector TTI Implementations
  /// @{

  bool enableInterleavedAccessVectorization() { return true; }

  bool enableMaskedInterleavedAccessVectorization() { return ST->hasSVE(); }

  unsigned getNumberOfRegisters(unsigned ClassID) const {
    bool Vector = (ClassID == 1);
    if (Vector) {
      if (ST->hasNEON())
        return 32;
      return 0;
    }
    return 31;
  }

  InstructionCost getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                        TTI::TargetCostKind CostKind);

  std::optional<Instruction *> instCombineIntrinsic(InstCombiner &IC,
                                                    IntrinsicInst &II) const;

  std::optional<Value *> simplifyDemandedVectorEltsIntrinsic(
      InstCombiner &IC, IntrinsicInst &II, APInt DemandedElts, APInt &UndefElts,
      APInt &UndefElts2, APInt &UndefElts3,
      std::function<void(Instruction *, unsigned, APInt, APInt &)>
          SimplifyAndSetOp) const;

  TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const;

  unsigned getMinVectorRegisterBitWidth() const {
    return ST->getMinVectorRegisterBitWidth();
  }

  std::optional<unsigned> getVScaleForTuning() const {
    return ST->getVScaleForTuning();
  }

  bool isVScaleKnownToBeAPowerOfTwo() const { return true; }

  bool shouldMaximizeVectorBandwidth(TargetTransformInfo::RegisterKind K) const;

  /// Try to return an estimate cost factor that can be used as a multiplier
  /// when scalarizing an operation for a vector with ElementCount \p VF.
  /// For scalable vectors this currently takes the most pessimistic view based
  /// upon the maximum possible value for vscale.
  unsigned getMaxNumElements(ElementCount VF) const {
    if (!VF.isScalable())
      return VF.getFixedValue();

    return VF.getKnownMinValue() * ST->getVScaleForTuning();
  }

  unsigned getMaxInterleaveFactor(ElementCount VF);

  bool prefersVectorizedAddressing() const;

  InstructionCost getMaskedMemoryOpCost(unsigned Opcode, Type *Src,
                                        Align Alignment, unsigned AddressSpace,
                                        TTI::TargetCostKind CostKind);

  InstructionCost getGatherScatterOpCost(unsigned Opcode, Type *DataTy,
                                         const Value *Ptr, bool VariableMask,
                                         Align Alignment,
                                         TTI::TargetCostKind CostKind,
                                         const Instruction *I = nullptr);

  bool isExtPartOfAvgExpr(const Instruction *ExtUser, Type *Dst, Type *Src);

  InstructionCost getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                   TTI::CastContextHint CCH,
                                   TTI::TargetCostKind CostKind,
                                   const Instruction *I = nullptr);

  InstructionCost getExtractWithExtendCost(unsigned Opcode, Type *Dst,
                                           VectorType *VecTy, unsigned Index);

  InstructionCost getCFInstrCost(unsigned Opcode, TTI::TargetCostKind CostKind,
                                 const Instruction *I = nullptr);

  InstructionCost getVectorInstrCost(unsigned Opcode, Type *Val,
                                     TTI::TargetCostKind CostKind,
                                     unsigned Index, Value *Op0, Value *Op1);
  InstructionCost getVectorInstrCost(const Instruction &I, Type *Val,
                                     TTI::TargetCostKind CostKind,
                                     unsigned Index);

  InstructionCost getMinMaxReductionCost(Intrinsic::ID IID, VectorType *Ty,
                                         FastMathFlags FMF,
                                         TTI::TargetCostKind CostKind);

  InstructionCost getArithmeticReductionCostSVE(unsigned Opcode,
                                                VectorType *ValTy,
                                                TTI::TargetCostKind CostKind);

  InstructionCost getSpliceCost(VectorType *Tp, int Index);

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = std::nullopt,
      const Instruction *CxtI = nullptr);

  InstructionCost getAddressComputationCost(Type *Ty, ScalarEvolution *SE,
                                            const SCEV *Ptr);

  InstructionCost getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                                     CmpInst::Predicate VecPred,
                                     TTI::TargetCostKind CostKind,
                                     const Instruction *I = nullptr);

  TTI::MemCmpExpansionOptions enableMemCmpExpansion(bool OptSize,
                                                    bool IsZeroCmp) const;
  bool useNeonVector(const Type *Ty) const;

  InstructionCost
  getMemoryOpCost(unsigned Opcode, Type *Src, MaybeAlign Alignment,
                  unsigned AddressSpace, TTI::TargetCostKind CostKind,
                  TTI::OperandValueInfo OpInfo = {TTI::OK_AnyValue, TTI::OP_None},
                  const Instruction *I = nullptr);

  InstructionCost getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys);

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE);

  void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                             TTI::PeelingPreferences &PP);

  Value *getOrCreateResultFromMemIntrinsic(IntrinsicInst *Inst,
                                           Type *ExpectedType);

  bool getTgtMemIntrinsic(IntrinsicInst *Inst, MemIntrinsicInfo &Info);

  bool isElementTypeLegalForScalableVector(Type *Ty) const {
    if (Ty->isPointerTy())
      return true;

    if (Ty->isBFloatTy() && ST->hasBF16())
      return true;

    if (Ty->isHalfTy() || Ty->isFloatTy() || Ty->isDoubleTy())
      return true;

    if (Ty->isIntegerTy(1) || Ty->isIntegerTy(8) || Ty->isIntegerTy(16) ||
        Ty->isIntegerTy(32) || Ty->isIntegerTy(64))
      return true;

    return false;
  }

  bool isLegalMaskedLoadStore(Type *DataType, Align Alignment) {
    if (!ST->hasSVE())
      return false;

    // For fixed vectors, avoid scalarization if using SVE for them.
    if (isa<FixedVectorType>(DataType) && !ST->useSVEForFixedLengthVectors() &&
        DataType->getPrimitiveSizeInBits() != 128)
      return false; // Fall back to scalarization of masked operations.

    return isElementTypeLegalForScalableVector(DataType->getScalarType());
  }

  bool isLegalMaskedLoad(Type *DataType, Align Alignment) {
    return isLegalMaskedLoadStore(DataType, Alignment);
  }

  bool isLegalMaskedStore(Type *DataType, Align Alignment) {
    return isLegalMaskedLoadStore(DataType, Alignment);
  }

  bool isLegalMaskedGatherScatter(Type *DataType) const {
    if (!ST->isSVEAvailable())
      return false;

    // For fixed vectors, scalarize if not using SVE for them.
    auto *DataTypeFVTy = dyn_cast<FixedVectorType>(DataType);
    if (DataTypeFVTy && (!ST->useSVEForFixedLengthVectors() ||
                         DataTypeFVTy->getNumElements() < 2))
      return false;

    return isElementTypeLegalForScalableVector(DataType->getScalarType());
  }

  bool isLegalMaskedGather(Type *DataType, Align Alignment) const {
    return isLegalMaskedGatherScatter(DataType);
  }

  bool isLegalMaskedScatter(Type *DataType, Align Alignment) const {
    return isLegalMaskedGatherScatter(DataType);
  }

  bool isLegalBroadcastLoad(Type *ElementTy, ElementCount NumElements) const {
    // Return true if we can generate a `ld1r` splat load instruction.
    if (!ST->hasNEON() || NumElements.isScalable())
      return false;
    switch (unsigned ElementBits = ElementTy->getScalarSizeInBits()) {
    case 8:
    case 16:
    case 32:
    case 64: {
      // We accept bit-widths >= 64bits and elements {8,16,32,64} bits.
      unsigned VectorBits = NumElements.getFixedValue() * ElementBits;
      return VectorBits >= 64;
    }
    }
    return false;
  }

  bool isLegalNTStoreLoad(Type *DataType, Align Alignment) {
    // NOTE: The logic below is mostly geared towards LV, which calls it with
    //       vectors with 2 elements. We might want to improve that, if other
    //       users show up.
    // Nontemporal vector loads/stores can be directly lowered to LDNP/STNP, if
    // the vector can be halved so that each half fits into a register. That's
    // the case if the element type fits into a register and the number of
    // elements is a power of 2 > 1.
    if (auto *DataTypeTy = dyn_cast<FixedVectorType>(DataType)) {
      unsigned NumElements = DataTypeTy->getNumElements();
      unsigned EltSize = DataTypeTy->getElementType()->getScalarSizeInBits();
      return NumElements > 1 && isPowerOf2_64(NumElements) && EltSize >= 8 &&
             EltSize <= 128 && isPowerOf2_64(EltSize);
    }
    return BaseT::isLegalNTStore(DataType, Alignment);
  }

  bool isLegalNTStore(Type *DataType, Align Alignment) {
    return isLegalNTStoreLoad(DataType, Alignment);
  }

  bool isLegalNTLoad(Type *DataType, Align Alignment) {
    // Only supports little-endian targets.
    if (ST->isLittleEndian())
      return isLegalNTStoreLoad(DataType, Alignment);
    return BaseT::isLegalNTLoad(DataType, Alignment);
  }

  bool enableOrderedReductions() const { return true; }

  InstructionCost getInterleavedMemoryOpCost(
      unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
      Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
      bool UseMaskForCond = false, bool UseMaskForGaps = false);

  bool
  shouldConsiderAddressTypePromotion(const Instruction &I,
                                     bool &AllowPromotionWithoutCommonHeader);

  bool shouldExpandReduction(const IntrinsicInst *II) const { return false; }

  unsigned getGISelRematGlobalCost() const {
    return 2;
  }

  unsigned getMinTripCountTailFoldingThreshold() const {
    return ST->hasSVE() ? 5 : 0;
  }

  TailFoldingStyle getPreferredTailFoldingStyle(bool IVUpdateMayOverflow) const {
    if (ST->hasSVE())
      return IVUpdateMayOverflow
                 ? TailFoldingStyle::DataAndControlFlowWithoutRuntimeCheck
                 : TailFoldingStyle::DataAndControlFlow;

    return TailFoldingStyle::DataWithoutLaneMask;
  }

  bool preferFixedOverScalableIfEqualCost() const {
    return ST->useFixedOverScalableIfEqualCost();
  }

  bool preferPredicateOverEpilogue(TailFoldingInfo *TFI);

  bool supportsScalableVectors() const {
    return ST->isSVEorStreamingSVEAvailable();
  }

  bool enableScalableVectorization() const;

  bool isLegalToVectorizeReduction(const RecurrenceDescriptor &RdxDesc,
                                   ElementCount VF) const;

  bool preferPredicatedReductionSelect(unsigned Opcode, Type *Ty,
                                       TTI::ReductionFlags Flags) const {
    return ST->hasSVE();
  }

  InstructionCost getArithmeticReductionCost(unsigned Opcode, VectorType *Ty,
                                             std::optional<FastMathFlags> FMF,
                                             TTI::TargetCostKind CostKind);

  InstructionCost getShuffleCost(TTI::ShuffleKind Kind, VectorType *Tp,
                                 ArrayRef<int> Mask,
                                 TTI::TargetCostKind CostKind, int Index,
                                 VectorType *SubTp,
                                 ArrayRef<const Value *> Args = std::nullopt,
                                 const Instruction *CxtI = nullptr);

  InstructionCost getScalarizationOverhead(VectorType *Ty,
                                           const APInt &DemandedElts,
                                           bool Insert, bool Extract,
                                           TTI::TargetCostKind CostKind);

  /// Return the cost of the scaling factor used in the addressing
  /// mode represented by AM for this target, for a load/store
  /// of the specified type.
  /// If the AM is supported, the return value must be >= 0.
  /// If the AM is not supported, it returns a negative value.
  InstructionCost getScalingFactorCost(Type *Ty, GlobalValue *BaseGV,
                                       StackOffset BaseOffset, bool HasBaseReg,
                                       int64_t Scale, unsigned AddrSpace) const;
  /// @}

  bool enableSelectOptimize() { return ST->enableSelectOptimize(); }

  bool shouldTreatInstructionLikeSelect(const Instruction *I);

  unsigned getStoreMinimumVF(unsigned VF, Type *ScalarMemTy,
                             Type *ScalarValTy) const {
    // We can vectorize store v4i8.
    if (ScalarMemTy->isIntegerTy(8) && isPowerOf2_32(VF) && VF >= 4)
      return 4;

    return BaseT::getStoreMinimumVF(VF, ScalarMemTy, ScalarValTy);
  }

  std::optional<unsigned> getMinPageSize() const { return 4096; }

  bool isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                     const TargetTransformInfo::LSRCost &C2);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AARCH64_AARCH64TARGETTRANSFORMINFO_H
