//===-- NVPTXTargetTransformInfo.h - NVPTX specific TTI ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file a TargetTransformInfo::Concept conforming object specific to the
/// NVPTX target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXTARGETTRANSFORMINFO_H

#include "NVPTXTargetMachine.h"
#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/TargetLowering.h"
#include <optional>

namespace llvm {

class NVPTXTTIImpl : public BasicTTIImplBase<NVPTXTTIImpl> {
  typedef BasicTTIImplBase<NVPTXTTIImpl> BaseT;
  typedef TargetTransformInfo TTI;
  friend BaseT;

  const NVPTXSubtarget *ST;
  const NVPTXTargetLowering *TLI;

  const NVPTXSubtarget *getST() const { return ST; };
  const NVPTXTargetLowering *getTLI() const { return TLI; };

public:
  explicit NVPTXTTIImpl(const NVPTXTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl()),
        TLI(ST->getTargetLowering()) {}

  bool hasBranchDivergence(const Function *F = nullptr) { return true; }

  bool isSourceOfDivergence(const Value *V);

  unsigned getFlatAddressSpace() const {
    return AddressSpace::ADDRESS_SPACE_GENERIC;
  }

  bool canHaveNonUndefGlobalInitializerInAddressSpace(unsigned AS) const {
    return AS != AddressSpace::ADDRESS_SPACE_SHARED &&
           AS != AddressSpace::ADDRESS_SPACE_LOCAL && AS != ADDRESS_SPACE_PARAM;
  }

  std::optional<Instruction *> instCombineIntrinsic(InstCombiner &IC,
                                                    IntrinsicInst &II) const;

  // Loads and stores can be vectorized if the alignment is at least as big as
  // the load/store we want to vectorize.
  bool isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes, Align Alignment,
                                   unsigned AddrSpace) const {
    return Alignment >= ChainSizeInBytes;
  }
  bool isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes, Align Alignment,
                                    unsigned AddrSpace) const {
    return isLegalToVectorizeLoadChain(ChainSizeInBytes, Alignment, AddrSpace);
  }

  // NVPTX has infinite registers of all kinds, but the actual machine doesn't.
  // We conservatively return 1 here which is just enough to enable the
  // vectorizers but disables heuristics based on the number of registers.
  // FIXME: Return a more reasonable number, while keeping an eye on
  // LoopVectorizer's unrolling heuristics.
  unsigned getNumberOfRegisters(bool Vector) const { return 1; }

  // Only <2 x half> should be vectorized, so always return 32 for the vector
  // register size.
  TypeSize getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
    return TypeSize::getFixed(32);
  }
  unsigned getMinVectorRegisterBitWidth() const { return 32; }

  // We don't want to prevent inlining because of target-cpu and -features
  // attributes that were added to newer versions of LLVM/Clang: There are
  // no incompatible functions in PTX, ptxas will throw errors in such cases.
  bool areInlineCompatible(const Function *Caller,
                           const Function *Callee) const {
    return true;
  }

  // Increase the inlining cost threshold by a factor of 11, reflecting that
  // calls are particularly expensive in NVPTX.
  unsigned getInliningThresholdMultiplier() const { return 11; }

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = std::nullopt,
      const Instruction *CxtI = nullptr);

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP,
                               OptimizationRemarkEmitter *ORE);

  void getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                             TTI::PeelingPreferences &PP);

  bool hasVolatileVariant(Instruction *I, unsigned AddrSpace) {
    // Volatile loads/stores are only supported for shared and global address
    // spaces, or for generic AS that maps to them.
    if (!(AddrSpace == llvm::ADDRESS_SPACE_GENERIC ||
          AddrSpace == llvm::ADDRESS_SPACE_GLOBAL ||
          AddrSpace == llvm::ADDRESS_SPACE_SHARED))
      return false;

    switch(I->getOpcode()){
    default:
      return false;
    case Instruction::Load:
    case Instruction::Store:
      return true;
    }
  }
};

} // end namespace llvm

#endif
