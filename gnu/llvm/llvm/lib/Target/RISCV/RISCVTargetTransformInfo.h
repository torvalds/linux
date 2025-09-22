//===- RISCVTargetTransformInfo.h - RISC-V specific TTI ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines a TargetTransformInfo::Concept conforming object specific
/// to the RISC-V target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_RISCVTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_RISCV_RISCVTARGETTRANSFORMINFO_H

#include "RISCVSubtarget.h"
#include "RISCVTargetMachine.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"
#include <optional>

namespace llvm {

class RISCVTTIImpl : public BasicTTIImplBase<RISCVTTIImpl> {
  using BaseT = BasicTTIImplBase<RISCVTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const RISCVSubtarget *ST;
  const RISCVTargetLowering *TLI;

  const RISCVSubtarget *getST() const { return ST; }
  const RISCVTargetLowering *getTLI() const { return TLI; }

  /// This function returns an estimate for VL to be used in VL based terms
  /// of the cost model.  For fixed length vectors, this is simply the
  /// vector length.  For scalable vectors, we return results consistent
  /// with getVScaleForTuning under the assumption that clients are also
  /// using that when comparing costs between scalar and vector representation.
  /// This does unfortunately mean that we can both undershoot and overshot
  /// the true cost significantly if getVScaleForTuning is wildly off for the
  /// actual target hardware.
  unsigned getEstimatedVLFor(VectorType *Ty);

  InstructionCost getRISCVInstructionCost(ArrayRef<unsigned> OpCodes, MVT VT,
                                          TTI::TargetCostKind CostKind);

  /// Return the cost of accessing a constant pool entry of the specified
  /// type.
  InstructionCost getConstantPoolLoadCost(Type *Ty,
                                          TTI::TargetCostKind CostKind);
public:
  explicit RISCVTTIImpl(const RISCVTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  bool areInlineCompatible(const Function *Caller,
                           const Function *Callee) const;

  /// Return the cost of materializing an immediate for a value operand of
  /// a store instruction.
  InstructionCost getStoreImmCost(Type *VecTy, TTI::OperandValueInfo OpInfo,
                                  TTI::TargetCostKind CostKind);

  InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                TTI::TargetCostKind CostKind);
  InstructionCost getIntImmCostInst(unsigned Opcode, unsigned Idx,
                                    const APInt &Imm, Type *Ty,
                                    TTI::TargetCostKind CostKind,
                                    Instruction *Inst = nullptr);
  InstructionCost getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                      const APInt &Imm, Type *Ty,
                                      TTI::TargetCostKind CostKind);

  /// \name EVL Support for predicated vectorization.
  /// Whether the target supports the %evl parameter of VP intrinsic efficiently
  /// in hardware, for the given opcode and type/alignment. (see LLVM Language
  /// Reference - "Vector Predication Intrinsics",
  /// https://llvm.org/docs/LangRef.html#vector-predication-intrinsics and
  /// "IR-level VP intrinsics",
  /// https://llvm.org/docs/Proposals/VectorPredication.html#ir-level-vp-intrinsics).
  /// \param Opcode the opcode of the instruction checked for predicated version
  /// support.
  /// \param DataType the type of the instruction with the \p Opcode checked for
  /// prediction support.
  /// \param Alignment the alignment for memory access operation checked for
  /// predicated version support.
  bool hasActiveVectorLength(unsigned Opcode, Type *DataType,
                             Align Alignment) const;

  TargetTransformInfo::PopcntSupportKind getPopcntSupport(unsigned TyWidth);

  bool shouldExpandReduction(const IntrinsicInst *II) const;
  bool supportsScalableVectors() const { return ST->hasVInstructions(); }
  bool enableOrderedReductions() const { return true; }
  bool enableScalableVectorization() const { return ST->hasVInstructions(); }
  TailFoldingStyle
  getPreferredTailFoldingStyle(bool IVUpdateMayOverflow) const {
    return ST->hasVInstructions() ? TailFoldingStyle::Data
                                  : TailFoldingStyle::DataWithoutLaneMask;
  }
  std::optional<unsigned> getMaxVScale() const;
  std::optional<unsigned> getVScaleForTuning() const;

  TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const;

  unsigned getRegUsageForType(Type *Ty);

  unsigned getMaximumVF(unsigned ElemWidth, unsigned Opcode) const;

  bool preferEpilogueVectorization() const {
    // Epilogue vectorization is usually unprofitable - tail folding or
    // a smaller VF would have been better.  This a blunt hammer - we
    // should re-examine this once vectorization is better tuned.
    return false;
  }

  InstructionCost getMaskedMemoryOpCost(unsigned Opcode, Type *Src,
                                        Align Alignment, unsigned AddressSpace,
                                        TTI::TargetCostKind CostKind);

  InstructionCost getPointersChainCost(ArrayRef<const Value *> Ptrs,
                                       const Value *Base,
                                       const TTI::PointersChainInfo &Info,
                                       Type *AccessTy,
                                       TTI::TargetCostKind CostKind);

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE);

  void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                             TTI::PeelingPreferences &PP);

  unsigned getMinVectorRegisterBitWidth() const {
    return ST->useRVVForFixedLengthVectors() ? 16 : 0;
  }

  InstructionCost getShuffleCost(TTI::ShuffleKind Kind, VectorType *Tp,
                                 ArrayRef<int> Mask,
                                 TTI::TargetCostKind CostKind, int Index,
                                 VectorType *SubTp,
                                 ArrayRef<const Value *> Args = std::nullopt,
                                 const Instruction *CxtI = nullptr);

  InstructionCost getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                        TTI::TargetCostKind CostKind);

  InstructionCost getInterleavedMemoryOpCost(
      unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
      Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
      bool UseMaskForCond = false, bool UseMaskForGaps = false);

  InstructionCost getGatherScatterOpCost(unsigned Opcode, Type *DataTy,
                                         const Value *Ptr, bool VariableMask,
                                         Align Alignment,
                                         TTI::TargetCostKind CostKind,
                                         const Instruction *I);

  InstructionCost getStridedMemoryOpCost(unsigned Opcode, Type *DataTy,
                                         const Value *Ptr, bool VariableMask,
                                         Align Alignment,
                                         TTI::TargetCostKind CostKind,
                                         const Instruction *I);

  InstructionCost getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                   TTI::CastContextHint CCH,
                                   TTI::TargetCostKind CostKind,
                                   const Instruction *I = nullptr);

  InstructionCost getMinMaxReductionCost(Intrinsic::ID IID, VectorType *Ty,
                                         FastMathFlags FMF,
                                         TTI::TargetCostKind CostKind);

  InstructionCost getArithmeticReductionCost(unsigned Opcode, VectorType *Ty,
                                             std::optional<FastMathFlags> FMF,
                                             TTI::TargetCostKind CostKind);

  InstructionCost getExtendedReductionCost(unsigned Opcode, bool IsUnsigned,
                                           Type *ResTy, VectorType *ValTy,
                                           FastMathFlags FMF,
                                           TTI::TargetCostKind CostKind);

  InstructionCost
  getMemoryOpCost(unsigned Opcode, Type *Src, MaybeAlign Alignment,
                  unsigned AddressSpace, TTI::TargetCostKind CostKind,
                  TTI::OperandValueInfo OpdInfo = {TTI::OK_AnyValue, TTI::OP_None},
                  const Instruction *I = nullptr);

  InstructionCost getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                                     CmpInst::Predicate VecPred,
                                     TTI::TargetCostKind CostKind,
                                     const Instruction *I = nullptr);

  InstructionCost getCFInstrCost(unsigned Opcode, TTI::TargetCostKind CostKind,
                                 const Instruction *I = nullptr);

  using BaseT::getVectorInstrCost;
  InstructionCost getVectorInstrCost(unsigned Opcode, Type *Val,
                                     TTI::TargetCostKind CostKind,
                                     unsigned Index, Value *Op0, Value *Op1);

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = std::nullopt,
      const Instruction *CxtI = nullptr);

  bool isElementTypeLegalForScalableVector(Type *Ty) const {
    return TLI->isLegalElementTypeForRVV(TLI->getValueType(DL, Ty));
  }

  bool isLegalMaskedLoadStore(Type *DataType, Align Alignment) {
    if (!ST->hasVInstructions())
      return false;

    EVT DataTypeVT = TLI->getValueType(DL, DataType);

    // Only support fixed vectors if we know the minimum vector size.
    if (DataTypeVT.isFixedLengthVector() && !ST->useRVVForFixedLengthVectors())
      return false;

    EVT ElemType = DataTypeVT.getScalarType();
    if (!ST->enableUnalignedVectorMem() && Alignment < ElemType.getStoreSize())
      return false;

    return TLI->isLegalElementTypeForRVV(ElemType);

  }

  bool isLegalMaskedLoad(Type *DataType, Align Alignment) {
    return isLegalMaskedLoadStore(DataType, Alignment);
  }
  bool isLegalMaskedStore(Type *DataType, Align Alignment) {
    return isLegalMaskedLoadStore(DataType, Alignment);
  }

  bool isLegalMaskedGatherScatter(Type *DataType, Align Alignment) {
    if (!ST->hasVInstructions())
      return false;

    EVT DataTypeVT = TLI->getValueType(DL, DataType);

    // Only support fixed vectors if we know the minimum vector size.
    if (DataTypeVT.isFixedLengthVector() && !ST->useRVVForFixedLengthVectors())
      return false;

    EVT ElemType = DataTypeVT.getScalarType();
    if (!ST->enableUnalignedVectorMem() && Alignment < ElemType.getStoreSize())
      return false;

    return TLI->isLegalElementTypeForRVV(ElemType);
  }

  bool isLegalMaskedGather(Type *DataType, Align Alignment) {
    return isLegalMaskedGatherScatter(DataType, Alignment);
  }
  bool isLegalMaskedScatter(Type *DataType, Align Alignment) {
    return isLegalMaskedGatherScatter(DataType, Alignment);
  }

  bool forceScalarizeMaskedGather(VectorType *VTy, Align Alignment) {
    // Scalarize masked gather for RV64 if EEW=64 indices aren't supported.
    return ST->is64Bit() && !ST->hasVInstructionsI64();
  }

  bool forceScalarizeMaskedScatter(VectorType *VTy, Align Alignment) {
    // Scalarize masked scatter for RV64 if EEW=64 indices aren't supported.
    return ST->is64Bit() && !ST->hasVInstructionsI64();
  }

  bool isLegalStridedLoadStore(Type *DataType, Align Alignment) {
    EVT DataTypeVT = TLI->getValueType(DL, DataType);
    return TLI->isLegalStridedLoadStore(DataTypeVT, Alignment);
  }

  bool isLegalMaskedCompressStore(Type *DataTy, Align Alignment);

  bool isVScaleKnownToBeAPowerOfTwo() const {
    return TLI->isVScaleKnownToBeAPowerOfTwo();
  }

  /// \returns How the target needs this vector-predicated operation to be
  /// transformed.
  TargetTransformInfo::VPLegalization
  getVPLegalizationStrategy(const VPIntrinsic &PI) const {
    using VPLegalization = TargetTransformInfo::VPLegalization;
    if (!ST->hasVInstructions() ||
        (PI.getIntrinsicID() == Intrinsic::vp_reduce_mul &&
         cast<VectorType>(PI.getArgOperand(1)->getType())
                 ->getElementType()
                 ->getIntegerBitWidth() != 1))
      return VPLegalization(VPLegalization::Discard, VPLegalization::Convert);
    return VPLegalization(VPLegalization::Legal, VPLegalization::Legal);
  }

  bool isLegalToVectorizeReduction(const RecurrenceDescriptor &RdxDesc,
                                   ElementCount VF) const {
    if (!VF.isScalable())
      return true;

    Type *Ty = RdxDesc.getRecurrenceType();
    if (!TLI->isLegalElementTypeForRVV(TLI->getValueType(DL, Ty)))
      return false;

    switch (RdxDesc.getRecurrenceKind()) {
    case RecurKind::Add:
    case RecurKind::FAdd:
    case RecurKind::And:
    case RecurKind::Or:
    case RecurKind::Xor:
    case RecurKind::SMin:
    case RecurKind::SMax:
    case RecurKind::UMin:
    case RecurKind::UMax:
    case RecurKind::FMin:
    case RecurKind::FMax:
    case RecurKind::FMulAdd:
    case RecurKind::IAnyOf:
    case RecurKind::FAnyOf:
      return true;
    default:
      return false;
    }
  }

  unsigned getMaxInterleaveFactor(ElementCount VF) {
    // Don't interleave if the loop has been vectorized with scalable vectors.
    if (VF.isScalable())
      return 1;
    // If the loop will not be vectorized, don't interleave the loop.
    // Let regular unroll to unroll the loop.
    return VF.isScalar() ? 1 : ST->getMaxInterleaveFactor();
  }

  bool enableInterleavedAccessVectorization() { return true; }

  enum RISCVRegisterClass { GPRRC, FPRRC, VRRC };
  unsigned getNumberOfRegisters(unsigned ClassID) const {
    switch (ClassID) {
    case RISCVRegisterClass::GPRRC:
      // 31 = 32 GPR - x0 (zero register)
      // FIXME: Should we exclude fixed registers like SP, TP or GP?
      return 31;
    case RISCVRegisterClass::FPRRC:
      if (ST->hasStdExtF())
        return 32;
      return 0;
    case RISCVRegisterClass::VRRC:
      // Although there are 32 vector registers, v0 is special in that it is the
      // only register that can be used to hold a mask.
      // FIXME: Should we conservatively return 31 as the number of usable
      // vector registers?
      return ST->hasVInstructions() ? 32 : 0;
    }
    llvm_unreachable("unknown register class");
  }

  unsigned getRegisterClassForType(bool Vector, Type *Ty = nullptr) const {
    if (Vector)
      return RISCVRegisterClass::VRRC;
    if (!Ty)
      return RISCVRegisterClass::GPRRC;

    Type *ScalarTy = Ty->getScalarType();
    if ((ScalarTy->isHalfTy() && ST->hasStdExtZfhmin()) ||
        (ScalarTy->isFloatTy() && ST->hasStdExtF()) ||
        (ScalarTy->isDoubleTy() && ST->hasStdExtD())) {
      return RISCVRegisterClass::FPRRC;
    }

    return RISCVRegisterClass::GPRRC;
  }

  const char *getRegisterClassName(unsigned ClassID) const {
    switch (ClassID) {
    case RISCVRegisterClass::GPRRC:
      return "RISCV::GPRRC";
    case RISCVRegisterClass::FPRRC:
      return "RISCV::FPRRC";
    case RISCVRegisterClass::VRRC:
      return "RISCV::VRRC";
    }
    llvm_unreachable("unknown register class");
  }

  bool isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                     const TargetTransformInfo::LSRCost &C2);

  bool shouldFoldTerminatingConditionAfterLSR() const {
    return true;
  }

  std::optional<unsigned> getMinPageSize() const { return 4096; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_RISCV_RISCVTARGETTRANSFORMINFO_H
