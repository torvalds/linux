//===- AMDGPUMCInstLower.h - Lower MachineInstr to MCInst ------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Header of lower AMDGPU MachineInstrs to their corresponding MCInst.
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUMCINSTLOWER_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUMCINSTLOWER_H

#include "AMDGPUTargetMachine.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Casting.h"

namespace llvm {
class AsmPrinter;
class MCContext;
} // namespace llvm

using namespace llvm;

class AMDGPUMCInstLower {
  MCContext &Ctx;
  const TargetSubtargetInfo &ST;
  const AsmPrinter &AP;

public:
  AMDGPUMCInstLower(MCContext &ctx, const TargetSubtargetInfo &ST,
                    const AsmPrinter &AP);

  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp) const;

  /// Lower a MachineInstr to an MCInst
  void lower(const MachineInstr *MI, MCInst &OutMI) const;
};

namespace {
static inline const MCExpr *lowerAddrSpaceCast(const TargetMachine &TM,
                                               const Constant *CV,
                                               MCContext &OutContext) {
  // TargetMachine does not support llvm-style cast. Use C++-style cast.
  // This is safe since TM is always of type AMDGPUTargetMachine or its
  // derived class.
  auto &AT = static_cast<const AMDGPUTargetMachine &>(TM);
  auto *CE = dyn_cast<ConstantExpr>(CV);

  // Lower null pointers in private and local address space.
  // Clang generates addrspacecast for null pointers in private and local
  // address space, which needs to be lowered.
  if (CE && CE->getOpcode() == Instruction::AddrSpaceCast) {
    auto Op = CE->getOperand(0);
    auto SrcAddr = Op->getType()->getPointerAddressSpace();
    if (Op->isNullValue() && AT.getNullPointerValue(SrcAddr) == 0) {
      auto DstAddr = CE->getType()->getPointerAddressSpace();
      return MCConstantExpr::create(AT.getNullPointerValue(DstAddr),
                                    OutContext);
    }
  }
  return nullptr;
}
} // namespace
#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUMCINSTLOWER_H
