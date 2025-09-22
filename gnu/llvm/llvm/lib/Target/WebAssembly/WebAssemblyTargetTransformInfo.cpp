//===-- WebAssemblyTargetTransformInfo.cpp - WebAssembly-specific TTI -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the WebAssembly-specific TargetTransformInfo
/// implementation.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyTargetTransformInfo.h"
#include "llvm/CodeGen/CostTable.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "wasmtti"

TargetTransformInfo::PopcntSupportKind
WebAssemblyTTIImpl::getPopcntSupport(unsigned TyWidth) const {
  assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
  return TargetTransformInfo::PSK_FastHardware;
}

unsigned WebAssemblyTTIImpl::getNumberOfRegisters(unsigned ClassID) const {
  unsigned Result = BaseT::getNumberOfRegisters(ClassID);

  // For SIMD, use at least 16 registers, as a rough guess.
  bool Vector = (ClassID == 1);
  if (Vector)
    Result = std::max(Result, 16u);

  return Result;
}

TypeSize WebAssemblyTTIImpl::getRegisterBitWidth(
    TargetTransformInfo::RegisterKind K) const {
  switch (K) {
  case TargetTransformInfo::RGK_Scalar:
    return TypeSize::getFixed(64);
  case TargetTransformInfo::RGK_FixedWidthVector:
    return TypeSize::getFixed(getST()->hasSIMD128() ? 128 : 64);
  case TargetTransformInfo::RGK_ScalableVector:
    return TypeSize::getScalable(0);
  }

  llvm_unreachable("Unsupported register kind");
}

InstructionCost WebAssemblyTTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args,
    const Instruction *CxtI) {

  InstructionCost Cost =
      BasicTTIImplBase<WebAssemblyTTIImpl>::getArithmeticInstrCost(
          Opcode, Ty, CostKind, Op1Info, Op2Info);

  if (auto *VTy = dyn_cast<VectorType>(Ty)) {
    switch (Opcode) {
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::Shl:
      // SIMD128's shifts currently only accept a scalar shift count. For each
      // element, we'll need to extract, op, insert. The following is a rough
      // approximation.
      if (!Op2Info.isUniform())
        Cost =
            cast<FixedVectorType>(VTy)->getNumElements() *
            (TargetTransformInfo::TCC_Basic +
             getArithmeticInstrCost(Opcode, VTy->getElementType(), CostKind) +
             TargetTransformInfo::TCC_Basic);
      break;
    }
  }
  return Cost;
}

InstructionCost
WebAssemblyTTIImpl::getVectorInstrCost(unsigned Opcode, Type *Val,
                                       TTI::TargetCostKind CostKind,
                                       unsigned Index, Value *Op0, Value *Op1) {
  InstructionCost Cost = BasicTTIImplBase::getVectorInstrCost(
      Opcode, Val, CostKind, Index, Op0, Op1);

  // SIMD128's insert/extract currently only take constant indices.
  if (Index == -1u)
    return Cost + 25 * TargetTransformInfo::TCC_Expensive;

  return Cost;
}

TTI::ReductionShuffle WebAssemblyTTIImpl::getPreferredExpandedReductionShuffle(
    const IntrinsicInst *II) const {

  switch (II->getIntrinsicID()) {
  default:
    break;
  case Intrinsic::vector_reduce_fadd:
    return TTI::ReductionShuffle::Pairwise;
  }
  return TTI::ReductionShuffle::SplitHalf;
}

bool WebAssemblyTTIImpl::areInlineCompatible(const Function *Caller,
                                             const Function *Callee) const {
  // Allow inlining only when the Callee has a subset of the Caller's
  // features. In principle, we should be able to inline regardless of any
  // features because WebAssembly supports features at module granularity, not
  // function granularity, but without this restriction it would be possible for
  // a module to "forget" about features if all the functions that used them
  // were inlined.
  const TargetMachine &TM = getTLI()->getTargetMachine();

  const FeatureBitset &CallerBits =
      TM.getSubtargetImpl(*Caller)->getFeatureBits();
  const FeatureBitset &CalleeBits =
      TM.getSubtargetImpl(*Callee)->getFeatureBits();

  return (CallerBits & CalleeBits) == CalleeBits;
}

void WebAssemblyTTIImpl::getUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, TTI::UnrollingPreferences &UP,
    OptimizationRemarkEmitter *ORE) const {
  // Scan the loop: don't unroll loops with calls. This is a standard approach
  // for most (all?) targets.
  for (BasicBlock *BB : L->blocks())
    for (Instruction &I : *BB)
      if (isa<CallInst>(I) || isa<InvokeInst>(I))
        if (const Function *F = cast<CallBase>(I).getCalledFunction())
          if (isLoweredToCall(F))
            return;

  // The chosen threshold is within the range of 'LoopMicroOpBufferSize' of
  // the various microarchitectures that use the BasicTTI implementation and
  // has been selected through heuristics across multiple cores and runtimes.
  UP.Partial = UP.Runtime = UP.UpperBound = true;
  UP.PartialThreshold = 30;

  // Avoid unrolling when optimizing for size.
  UP.OptSizeThreshold = 0;
  UP.PartialOptSizeThreshold = 0;

  // Set number of instructions optimized when "back edge"
  // becomes "fall through" to default value of 2.
  UP.BEInsns = 2;
}

bool WebAssemblyTTIImpl::supportsTailCalls() const {
  return getST()->hasTailCall();
}
