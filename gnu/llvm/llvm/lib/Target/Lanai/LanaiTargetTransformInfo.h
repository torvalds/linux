//===-- LanaiTargetTransformInfo.h - Lanai specific TTI ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file a TargetTransformInfo::Concept conforming object specific to the
// Lanai target machine. It uses the target's detailed information to
// provide more precise answers to certain TTI queries, while letting the
// target independent and default TTI implementations handle the rest.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_LANAITARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_LANAI_LANAITARGETTRANSFORMINFO_H

#include "Lanai.h"
#include "LanaiSubtarget.h"
#include "LanaiTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {
class LanaiTTIImpl : public BasicTTIImplBase<LanaiTTIImpl> {
  typedef BasicTTIImplBase<LanaiTTIImpl> BaseT;
  typedef TargetTransformInfo TTI;
  friend BaseT;

  const LanaiSubtarget *ST;
  const LanaiTargetLowering *TLI;

  const LanaiSubtarget *getST() const { return ST; }
  const LanaiTargetLowering *getTLI() const { return TLI; }

public:
  explicit LanaiTTIImpl(const LanaiTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  bool shouldBuildLookupTables() const { return false; }

  TargetTransformInfo::PopcntSupportKind getPopcntSupport(unsigned TyWidth) {
    if (TyWidth == 32)
      return TTI::PSK_FastHardware;
    return TTI::PSK_Software;
  }

  InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                TTI::TargetCostKind CostKind) {
    assert(Ty->isIntegerTy());
    unsigned BitSize = Ty->getPrimitiveSizeInBits();
    // There is no cost model for constants with a bit size of 0. Return
    // TCC_Free here, so that constant hoisting will ignore this constant.
    if (BitSize == 0)
      return TTI::TCC_Free;
    // No cost model for operations on integers larger than 64 bit implemented
    // yet.
    if (BitSize > 64)
      return TTI::TCC_Free;

    if (Imm == 0)
      return TTI::TCC_Free;
    if (isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Basic;
    if (isInt<21>(Imm.getZExtValue()))
      return TTI::TCC_Basic;
    if (isInt<32>(Imm.getSExtValue())) {
      if ((Imm.getSExtValue() & 0xFFFF) == 0)
        return TTI::TCC_Basic;
      return 2 * TTI::TCC_Basic;
    }

    return 4 * TTI::TCC_Basic;
  }

  InstructionCost getIntImmCostInst(unsigned Opc, unsigned Idx,
                                    const APInt &Imm, Type *Ty,
                                    TTI::TargetCostKind CostKind,
                                    Instruction *Inst = nullptr) {
    return getIntImmCost(Imm, Ty, CostKind);
  }

  InstructionCost getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx,
                                      const APInt &Imm, Type *Ty,
                                      TTI::TargetCostKind CostKind) {
    return getIntImmCost(Imm, Ty, CostKind);
  }

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = std::nullopt,
      const Instruction *CxtI = nullptr) {
    int ISD = TLI->InstructionOpcodeToISD(Opcode);

    switch (ISD) {
    default:
      return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                           Op2Info);
    case ISD::MUL:
    case ISD::SDIV:
    case ISD::UDIV:
    case ISD::UREM:
      // This increases the cost associated with multiplication and division
      // to 64 times what the baseline arithmetic cost is. The arithmetic
      // instruction cost was arbitrarily chosen to reduce the desirability
      // of emitting arithmetic instructions that are emulated in software.
      // TODO: Investigate the performance impact given specialized lowerings.
      return 64 * BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info,
                                                Op2Info);
    }
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_LANAI_LANAITARGETTRANSFORMINFO_H
