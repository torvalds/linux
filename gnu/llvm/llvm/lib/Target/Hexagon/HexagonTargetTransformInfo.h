//==- HexagonTargetTransformInfo.cpp - Hexagon specific TTI pass -*- C++ -*-==//
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

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONTARGETTRANSFORMINFO_H

#include "Hexagon.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetMachine.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"

namespace llvm {

class Loop;
class ScalarEvolution;
class User;
class Value;

class HexagonTTIImpl : public BasicTTIImplBase<HexagonTTIImpl> {
  using BaseT = BasicTTIImplBase<HexagonTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const HexagonSubtarget &ST;
  const HexagonTargetLowering &TLI;

  const HexagonSubtarget *getST() const { return &ST; }
  const HexagonTargetLowering *getTLI() const { return &TLI; }

  bool useHVX() const;
  bool isHVXVectorType(Type *Ty) const;

  // Returns the number of vector elements of Ty, if Ty is a vector type,
  // or 1 if Ty is a scalar type. It is incorrect to call this function
  // with any other type.
  unsigned getTypeNumElements(Type *Ty) const;

public:
  explicit HexagonTTIImpl(const HexagonTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()),
        ST(*TM->getSubtargetImpl(F)), TLI(*ST.getTargetLowering()) {}

  /// \name Scalar TTI Implementations
  /// @{

  TTI::PopcntSupportKind getPopcntSupport(unsigned IntTyWidthInBit) const;

  // The Hexagon target can unroll loops with run-time trip counts.
  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE);

  void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                             TTI::PeelingPreferences &PP);

  /// Bias LSR towards creating post-increment opportunities.
  TTI::AddressingModeKind
    getPreferredAddressingMode(const Loop *L, ScalarEvolution *SE) const;

  // L1 cache prefetch.
  unsigned getPrefetchDistance() const override;
  unsigned getCacheLineSize() const override;

  /// @}

  /// \name Vector TTI Implementations
  /// @{

  unsigned getNumberOfRegisters(bool vector) const;
  unsigned getMaxInterleaveFactor(ElementCount VF);
  TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const;
  unsigned getMinVectorRegisterBitWidth() const;
  ElementCount getMinimumVF(unsigned ElemWidth, bool IsScalable) const;

  bool
  shouldMaximizeVectorBandwidth(TargetTransformInfo::RegisterKind K) const {
    return true;
  }
  bool supportsEfficientVectorElementLoadStore() { return false; }
  bool hasBranchDivergence(const Function *F = nullptr) { return false; }
  bool enableAggressiveInterleaving(bool LoopHasReductions) {
    return false;
  }
  bool prefersVectorizedAddressing() {
    return false;
  }
  bool enableInterleavedAccessVectorization() {
    return true;
  }

  InstructionCost getCallInstrCost(Function *F, Type *RetTy,
                                   ArrayRef<Type *> Tys,
                                   TTI::TargetCostKind CostKind);
  InstructionCost getIntrinsicInstrCost(const IntrinsicCostAttributes &ICA,
                                        TTI::TargetCostKind CostKind);
  InstructionCost getAddressComputationCost(Type *Tp, ScalarEvolution *SE,
                                            const SCEV *S);
  InstructionCost
  getMemoryOpCost(unsigned Opcode, Type *Src, MaybeAlign Alignment,
                  unsigned AddressSpace, TTI::TargetCostKind CostKind,
                  TTI::OperandValueInfo OpInfo = {TTI::OK_AnyValue, TTI::OP_None},
                  const Instruction *I = nullptr);
  InstructionCost getMaskedMemoryOpCost(unsigned Opcode, Type *Src,
                                        Align Alignment, unsigned AddressSpace,
                                        TTI::TargetCostKind CostKind);
  InstructionCost getShuffleCost(TTI::ShuffleKind Kind, Type *Tp,
                                 ArrayRef<int> Mask,
                                 TTI::TargetCostKind CostKind, int Index,
                                 Type *SubTp,
                                 ArrayRef<const Value *> Args = std::nullopt,
                                 const Instruction *CxtI = nullptr);
  InstructionCost getGatherScatterOpCost(unsigned Opcode, Type *DataTy,
                                         const Value *Ptr, bool VariableMask,
                                         Align Alignment,
                                         TTI::TargetCostKind CostKind,
                                         const Instruction *I);
  InstructionCost getInterleavedMemoryOpCost(
      unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
      Align Alignment, unsigned AddressSpace, TTI::TargetCostKind CostKind,
      bool UseMaskForCond = false, bool UseMaskForGaps = false);
  InstructionCost getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                                     CmpInst::Predicate VecPred,
                                     TTI::TargetCostKind CostKind,
                                     const Instruction *I = nullptr);
  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = std::nullopt,
      const Instruction *CxtI = nullptr);
  InstructionCost getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                                   TTI::CastContextHint CCH,
                                   TTI::TargetCostKind CostKind,
                                   const Instruction *I = nullptr);
  using BaseT::getVectorInstrCost;
  InstructionCost getVectorInstrCost(unsigned Opcode, Type *Val,
                                     TTI::TargetCostKind CostKind,
                                     unsigned Index, Value *Op0, Value *Op1);

  InstructionCost getCFInstrCost(unsigned Opcode, TTI::TargetCostKind CostKind,
                                 const Instruction *I = nullptr) {
    return 1;
  }

  bool isLegalMaskedStore(Type *DataType, Align Alignment);
  bool isLegalMaskedLoad(Type *DataType, Align Alignment);

  /// @}

  InstructionCost getInstructionCost(const User *U,
                                     ArrayRef<const Value *> Operands,
                                     TTI::TargetCostKind CostKind);

  // Hexagon specific decision to generate a lookup table.
  bool shouldBuildLookupTables() const;
};

} // end namespace llvm
#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONTARGETTRANSFORMINFO_H
