//===-- SystemZTargetTransformInfo.h - SystemZ-specific TTI ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZTARGETTRANSFORMINFO_H

#include "SystemZTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

namespace llvm {

class SystemZTTIImpl : public BasicTTIImplBase<SystemZTTIImpl> {
  typedef BasicTTIImplBase<SystemZTTIImpl> BaseT;
  typedef TargetTransformInfo TTI;
  friend BaseT;

  const SystemZSubtarget *ST;
  const SystemZTargetLowering *TLI;

  const SystemZSubtarget *getST() const { return ST; }
  const SystemZTargetLowering *getTLI() const { return TLI; }

  unsigned const LIBCALL_COST = 30;

  bool isInt128InVR(Type *Ty) { return Ty->isIntegerTy(128) && ST->hasVector(); }

public:
  explicit SystemZTTIImpl(const SystemZTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  /// \name Scalar TTI Implementations
  /// @{

  unsigned getInliningThresholdMultiplier() const { return 3; }
  unsigned adjustInliningThreshold(const CallBase *CB) const;

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

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE);

  void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                             TTI::PeelingPreferences &PP);

  bool isLSRCostLess(const TargetTransformInfo::LSRCost &C1,
                     const TargetTransformInfo::LSRCost &C2);
  /// @}

  /// \name Vector TTI Implementations
  /// @{

  unsigned getNumberOfRegisters(unsigned ClassID) const;
  TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const;

  unsigned getCacheLineSize() const override { return 256; }
  unsigned getPrefetchDistance() const override { return 4500; }
  unsigned getMinPrefetchStride(unsigned NumMemAccesses,
                                unsigned NumStridedMemAccesses,
                                unsigned NumPrefetches,
                                bool HasCall) const override;
  bool enableWritePrefetching() const override { return true; }

  bool hasDivRemOp(Type *DataType, bool IsSigned);
  bool prefersVectorizedAddressing() { return false; }
  bool LSRWithInstrQueries() { return true; }
  bool supportsEfficientVectorElementLoadStore() { return true; }
  bool enableInterleavedAccessVectorization() { return true; }

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = std::nullopt,
      const Instruction *CxtI = nullptr);
  InstructionCost getShuffleCost(TTI::ShuffleKind Kind, VectorType *Tp,
                                 ArrayRef<int> Mask,
                                 TTI::TargetCostKind CostKind, int Index,
                                 VectorType *SubTp,
                                 ArrayRef<const Value *> Args = std::nullopt,
                                 const Instruction *CxtI = nullptr);
  unsigned getVectorTruncCost(Type *SrcTy, Type *DstTy);
  unsigned getVectorBitmaskConversionCost(Type *SrcTy, Type *DstTy);
  unsigned getBoolVecToIntConversionCost(unsigned Opcode, Type *Dst,
                                         const Instruction *I);
  InstructionCost getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                   TTI::CastContextHint CCH,
                                   TTI::TargetCostKind CostKind,
                                   const Instruction *I = nullptr);
  InstructionCost getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                                     CmpInst::Predicate VecPred,
                                     TTI::TargetCostKind CostKind,
                                     const Instruction *I = nullptr);
  using BaseT::getVectorInstrCost;
  InstructionCost getVectorInstrCost(unsigned Opcode, Type *Val,
                                     TTI::TargetCostKind CostKind,
                                     unsigned Index, Value *Op0, Value *Op1);
  bool isFoldableLoad(const LoadInst *Ld, const Instruction *&FoldedValue);
  InstructionCost
  getMemoryOpCost(unsigned Opcode, Type *Src, MaybeAlign Alignment,
                  unsigned AddressSpace, TTI::TargetCostKind CostKind,
                  TTI::OperandValueInfo OpInfo = {TTI::OK_AnyValue, TTI::OP_None},
                  const Instruction *I = nullptr);

  InstructionCost getInterleavedMemoryOpCost(
      unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
      Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
      bool UseMaskForCond = false, bool UseMaskForGaps = false);

  InstructionCost getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                        TTI::TargetCostKind CostKind);

  bool shouldExpandReduction(const IntrinsicInst *II) const;
  /// @}
};

} // end namespace llvm

#endif
