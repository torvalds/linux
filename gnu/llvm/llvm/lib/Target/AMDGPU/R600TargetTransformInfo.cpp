//===- R600TargetTransformInfo.cpp - AMDGPU specific TTI pass -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file implements a TargetTransformInfo analysis pass specific to the
// R600 target machine. It uses the target's detailed information to provide
// more precise answers to certain TTI queries, while letting the target
// independent and default TTI implementations handle the rest.
//
//===----------------------------------------------------------------------===//

#include "R600TargetTransformInfo.h"
#include "AMDGPU.h"
#include "AMDGPUTargetMachine.h"
#include "R600Subtarget.h"

using namespace llvm;

#define DEBUG_TYPE "R600tti"

R600TTIImpl::R600TTIImpl(const AMDGPUTargetMachine *TM, const Function &F)
    : BaseT(TM, F.getDataLayout()),
      ST(static_cast<const R600Subtarget *>(TM->getSubtargetImpl(F))),
      TLI(ST->getTargetLowering()), CommonTTI(TM, F) {}

unsigned R600TTIImpl::getHardwareNumberOfRegisters(bool Vec) const {
  return 4 * 128; // XXX - 4 channels. Should these count as vector instead?
}

unsigned R600TTIImpl::getNumberOfRegisters(bool Vec) const {
  return getHardwareNumberOfRegisters(Vec);
}

TypeSize
R600TTIImpl::getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const {
  return TypeSize::getFixed(32);
}

unsigned R600TTIImpl::getMinVectorRegisterBitWidth() const { return 32; }

unsigned R600TTIImpl::getLoadStoreVecRegBitWidth(unsigned AddrSpace) const {
  if (AddrSpace == AMDGPUAS::GLOBAL_ADDRESS ||
      AddrSpace == AMDGPUAS::CONSTANT_ADDRESS)
    return 128;
  if (AddrSpace == AMDGPUAS::LOCAL_ADDRESS ||
      AddrSpace == AMDGPUAS::REGION_ADDRESS)
    return 64;
  if (AddrSpace == AMDGPUAS::PRIVATE_ADDRESS)
    return 32;

  if ((AddrSpace == AMDGPUAS::PARAM_D_ADDRESS ||
       AddrSpace == AMDGPUAS::PARAM_I_ADDRESS ||
       (AddrSpace >= AMDGPUAS::CONSTANT_BUFFER_0 &&
        AddrSpace <= AMDGPUAS::CONSTANT_BUFFER_15)))
    return 128;
  llvm_unreachable("unhandled address space");
}

bool R600TTIImpl::isLegalToVectorizeMemChain(unsigned ChainSizeInBytes,
                                             Align Alignment,
                                             unsigned AddrSpace) const {
  // We allow vectorization of flat stores, even though we may need to decompose
  // them later if they may access private memory. We don't have enough context
  // here, and legalization can handle it.
  return (AddrSpace != AMDGPUAS::PRIVATE_ADDRESS);
}

bool R600TTIImpl::isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes,
                                              Align Alignment,
                                              unsigned AddrSpace) const {
  return isLegalToVectorizeMemChain(ChainSizeInBytes, Alignment, AddrSpace);
}

bool R600TTIImpl::isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes,
                                               Align Alignment,
                                               unsigned AddrSpace) const {
  return isLegalToVectorizeMemChain(ChainSizeInBytes, Alignment, AddrSpace);
}

unsigned R600TTIImpl::getMaxInterleaveFactor(ElementCount VF) {
  // Disable unrolling if the loop is not vectorized.
  // TODO: Enable this again.
  if (VF.isScalar())
    return 1;

  return 8;
}

InstructionCost R600TTIImpl::getCFInstrCost(unsigned Opcode,
                                            TTI::TargetCostKind CostKind,
                                            const Instruction *I) {
  if (CostKind == TTI::TCK_CodeSize || CostKind == TTI::TCK_SizeAndLatency)
    return Opcode == Instruction::PHI ? 0 : 1;

  // XXX - For some reason this isn't called for switch.
  switch (Opcode) {
  case Instruction::Br:
  case Instruction::Ret:
    return 10;
  default:
    return BaseT::getCFInstrCost(Opcode, CostKind, I);
  }
}

InstructionCost R600TTIImpl::getVectorInstrCost(unsigned Opcode, Type *ValTy,
                                                TTI::TargetCostKind CostKind,
                                                unsigned Index, Value *Op0,
                                                Value *Op1) {
  switch (Opcode) {
  case Instruction::ExtractElement:
  case Instruction::InsertElement: {
    unsigned EltSize =
        DL.getTypeSizeInBits(cast<VectorType>(ValTy)->getElementType());
    if (EltSize < 32) {
      return BaseT::getVectorInstrCost(Opcode, ValTy, CostKind, Index, Op0,
                                       Op1);
    }

    // Extracts are just reads of a subregister, so are free. Inserts are
    // considered free because we don't want to have any cost for scalarizing
    // operations, and we don't have to copy into a different register class.

    // Dynamic indexing isn't free and is best avoided.
    return Index == ~0u ? 2 : 0;
  }
  default:
    return BaseT::getVectorInstrCost(Opcode, ValTy, CostKind, Index, Op0, Op1);
  }
}

void R600TTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                          TTI::UnrollingPreferences &UP,
                                          OptimizationRemarkEmitter *ORE) {
  CommonTTI.getUnrollingPreferences(L, SE, UP, ORE);
}

void R600TTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                        TTI::PeelingPreferences &PP) {
  CommonTTI.getPeelingPreferences(L, SE, PP);
}
