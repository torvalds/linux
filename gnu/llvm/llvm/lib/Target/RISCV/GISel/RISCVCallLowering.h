//===-- RISCVCallLowering.h - Call lowering ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file describes how to lower LLVM calls to machine code calls.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_RISCVCALLLOWERING_H
#define LLVM_LIB_TARGET_RISCV_RISCVCALLLOWERING_H

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"

namespace llvm {

class MachineInstrBuilder;
class MachineIRBuilder;
class RISCVTargetLowering;

class RISCVCallLowering : public CallLowering {

public:
  RISCVCallLowering(const RISCVTargetLowering &TLI);

  bool lowerReturn(MachineIRBuilder &MIRBuiler, const Value *Val,
                   ArrayRef<Register> VRegs,
                   FunctionLoweringInfo &FLI) const override;

  bool lowerFormalArguments(MachineIRBuilder &MIRBuilder, const Function &F,
                            ArrayRef<ArrayRef<Register>> VRegs,
                            FunctionLoweringInfo &FLI) const override;

  bool lowerCall(MachineIRBuilder &MIRBuilder,
                 CallLoweringInfo &Info) const override;

private:
  bool lowerReturnVal(MachineIRBuilder &MIRBuilder, const Value *Val,
                      ArrayRef<Register> VRegs, MachineInstrBuilder &Ret) const;

  void saveVarArgRegisters(MachineIRBuilder &MIRBuilder,
                           CallLowering::IncomingValueHandler &Handler,
                           IncomingValueAssigner &Assigner,
                           CCState &CCInfo) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_RISCV_RISCVCALLLOWERING_H
